/* $Id: l3_udss1.c,v 1.26 2004/06/17 12:31:12 keil Exp $
 *
 * EURO/DSS1 D-channel protocol
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 *		This file is (c) under GNU PUBLIC LICENSE
 *		For changes and modifications please read
 *		../../../Documentation/isdn/mISDN.cert
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 */

#include <linux/module.h>

#include "layer3.h"
#include "helper.h"
#include "debug.h"
#include "dss1.h"

static int debug = 0;
static mISDNobject_t u_dss1;


const char *dss1_revision = "$Revision: 1.26 $";

static int dss1man(l3_process_t *, u_int, void *);

static int
parseQ931(struct sk_buff *skb) {
	Q931_info_t	*qi;
	int		l, codeset, maincodeset;
	int		len, iep, pos = 0, cnt = 0;
	u16		*ie, cr;
	u_char		t, *p = skb->data;

	if (skb->len < 3)
		return(-1);
	p++;
	l = (*p++) & 0xf;
	if (l>2)
		return(-2);
	if (l)
		cr = *p++;
	else
		cr = 0;
	if (l == 2) {
		cr <<= 8;
		cr |= *p++;
	} else if (l == 1)
		if (cr & 0x80) {
			cr |= 0x8000;
			cr &= 0xFF7F;
		}
	t = *p;
	if ((u_long)p & 1)
		pos = 1;
	else
		pos = 0;
	skb_pull(skb, (p - skb->data) - pos);
	len = skb->len;
	p = skb->data;
	if (skb_headroom(skb) < (int)L3_EXTRA_SIZE) {
		int_error();
		return(-3);
	}
	qi = (Q931_info_t *)skb_push(skb, L3_EXTRA_SIZE);
	mISDN_initQ931_info(qi);
	qi->type = t;
	qi->crlen = l;
	qi->cr = cr;
	pos++;
	codeset = maincodeset = 0;
	ie = &qi->bearer_capability;
	while (pos < len) {
		if ((p[pos] & 0xf0) == 0x90) {
			codeset = p[pos] & 0x07;
			if (!(p[pos] & 0x08))
				maincodeset = codeset;
			pos++;
			continue;
		}
		if (codeset == 0) {
			if (p[pos] & 0x80) { /* single octett IE */
				if (p[pos] == IE_MORE_DATA)
					qi->more_data = pos;
				else if (p[pos] == IE_COMPLETE)
					qi->sending_complete = pos;
				else if ((p[pos] & 0xf0) == IE_CONGESTION)
					qi->congestion_level = pos;
				cnt++;
				pos++;
			} else {
				iep = mISDN_l3_ie2pos(p[pos]);
				if ((pos+1) >= len)
					return(-4);
				l = p[pos+1];
				if ((pos+l+1) >= len)
					return(-5);
				if (iep>=0) {
					if (!ie[iep])
						ie[iep] = pos;
				}
				pos += l + 2;
				cnt++;
			}
		}
		codeset = maincodeset;
	}
	return(cnt);
}

static int
calc_msg_len(Q931_info_t *qi)
{
	int	i, cnt = 0;
	u_char	*buf = (u_char *)qi;
	u16	*v_ie;

	buf += L3_EXTRA_SIZE;
	if (qi->more_data)
		cnt++;
	if (qi->sending_complete)
		cnt++;
	if (qi->congestion_level)
		cnt++;
	v_ie = &qi->bearer_capability;
	for (i=0; i<32; i++) {
		if (v_ie[i])
			cnt += buf[v_ie[i] + 1] + 2;
	}
	return(cnt);
}

static int
compose_msg(struct sk_buff *skb, Q931_info_t *qi)
{
	int	i, l;
	u_char	*p, *buf = (u_char *)qi;
	u16	*v_ie;

	buf += L3_EXTRA_SIZE;
	
	if (qi->more_data) {
		p = skb_put(skb, 1);
		*p = buf[qi->more_data];
	}
	if (qi->sending_complete) {
		p = skb_put(skb, 1);
		*p = buf[qi->sending_complete];
	}
	if (qi->congestion_level) {
		p = skb_put(skb, 1);
		*p = buf[qi->congestion_level];
	}
	v_ie = &qi->bearer_capability;
	for (i=0; i<32; i++) {
		if (v_ie[i]) {
			l = buf[v_ie[i] + 1] +1;
			p = skb_put(skb, l + 1);
			*p++ = mISDN_l3_pos2ie(i);
			memcpy(p, &buf[v_ie[i] + 1], l);
		}
	}
	return(0);
}

static struct sk_buff
*MsgStart(l3_process_t *pc, u_char mt, int len) {
	struct sk_buff	*skb;
	int		lx = 4;
	u_char		*p;

	if (test_bit(FLG_CRLEN2, &pc->l3->Flag))
		lx++;
	if (pc->callref == -1) /* dummy cr */
		lx = 3;
	if (!(skb = alloc_stack_skb(len + lx, pc->l3->down_headerlen)))
		return(NULL);
	p = skb_put(skb, lx);
	*p++ = 8;
	if (lx == 3)
		*p++ = 0;
	else if (lx == 5) {
		*p++ = 2;
		*p++ = (pc->callref >> 8)  ^ 0x80;
		*p++ = pc->callref & 0xff;
	} else {
		*p++ = 1;
		*p = pc->callref & 0xff;
		if (!(pc->callref & 0x8000))
			*p |= 0x80;
		p++;
	}
	*p = mt;
	return(skb);
}

static int SendMsg(l3_process_t *pc, struct sk_buff *skb, int state) {
	int		l;
	int		ret;
	struct sk_buff	*nskb;
	Q931_info_t	*qi;

	if (!skb)
		return(-EINVAL);
	qi = (Q931_info_t *)skb->data;
	l = calc_msg_len(qi);
	if (!(nskb = MsgStart(pc, qi->type, l))) {
		kfree_skb(skb);
		return(-ENOMEM);
	}
	if (l)
		compose_msg(nskb, qi);
	kfree_skb(skb);
	if (state != -1)
		newl3state(pc, state);
	if ((ret=l3_msg(pc->l3, DL_DATA | REQUEST, 0, 0, nskb)))
		kfree_skb(nskb);
	return(ret);
}

static int
l3dss1_message(l3_process_t *pc, u_char mt)
{
	struct sk_buff	*skb;
	int		ret;

	if (!(skb = MsgStart(pc, mt, 0)))
		return(-ENOMEM);
	if ((ret=l3_msg(pc->l3, DL_DATA | REQUEST, 0, 0, skb)))
		kfree_skb(skb);
	return(ret);
}

static void
l3dss1_message_cause(l3_process_t *pc, u_char mt, u_char cause)
{
	struct sk_buff	*skb;
	u_char		*p;
	int		ret;

	if (!(skb = MsgStart(pc, mt, 4)))
		return;
	p = skb_put(skb, 4);
	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80 | CAUSE_LOC_USER;
	*p++ = 0x80 | cause;
	if ((ret=l3_msg(pc->l3, DL_DATA | REQUEST, 0, 0, skb)))
		kfree_skb(skb);
}

static void
l3dss1_status_send(l3_process_t *pc, u_char cause)
{
	struct sk_buff	*skb;
	u_char		*p;
	int		ret;

	if (!(skb = MsgStart(pc, MT_STATUS, 7)))
		return;
	p = skb_put(skb, 7);
	*p++ = IE_CAUSE;
	*p++ = 2;
	*p++ = 0x80 | CAUSE_LOC_USER;
	*p++ = 0x80 | cause;

	*p++ = IE_CALL_STATE;
	*p++ = 1;
	*p++ = pc->state & 0x3f;
	if ((ret=l3_msg(pc->l3, DL_DATA | REQUEST, 0, 0, skb)))
		kfree_skb(skb);
}

static void
l3dss1_msg_without_setup(l3_process_t *pc, u_char cause)
{
	/* This routine is called if here was no SETUP made (checks in dss1up and in
	 * l3dss1_setup) and a RELEASE_COMPLETE have to be sent with an error code
	 * MT_STATUS_ENQUIRE in the NULL state is handled too
	 */
	switch (cause) {
		case 81:	/* invalid callreference */
		case 88:	/* incomp destination */
		case 96:	/* mandory IE missing */
		case 100:       /* invalid IE contents */
		case 101:	/* incompatible Callstate */
			l3dss1_message_cause(pc, MT_RELEASE_COMPLETE, cause);
			break;
		default:
			printk(KERN_ERR "mISDN l3dss1_msg_without_setup wrong cause %d\n",
				cause);
	}
	release_l3_process(pc);
}

static int ie_ALERTING[] = {IE_BEARER, IE_CHANNEL_ID | IE_MANDATORY_1,
		IE_FACILITY, IE_PROGRESS, IE_DISPLAY, IE_SIGNAL, IE_HLC,
		IE_USER_USER, -1};
static int ie_CALL_PROCEEDING[] = {IE_BEARER, IE_CHANNEL_ID | IE_MANDATORY_1,
		IE_FACILITY, IE_PROGRESS, IE_DISPLAY, IE_HLC, -1};
static int ie_CONNECT[] = {IE_BEARER, IE_CHANNEL_ID | IE_MANDATORY_1, 
		IE_FACILITY, IE_PROGRESS, IE_DISPLAY, IE_DATE, IE_SIGNAL,
		IE_CONNECT_PN, IE_CONNECT_SUB, IE_LLC, IE_HLC, IE_USER_USER, -1};
static int ie_CONNECT_ACKNOWLEDGE[] = {IE_CHANNEL_ID, IE_DISPLAY, IE_SIGNAL, -1};
static int ie_DISCONNECT[] = {IE_CAUSE | IE_MANDATORY, IE_FACILITY,
		IE_PROGRESS, IE_DISPLAY, IE_SIGNAL, IE_USER_USER, -1};
static int ie_INFORMATION[] = {IE_COMPLETE, IE_DISPLAY, IE_KEYPAD, IE_SIGNAL,
		IE_CALLED_PN, -1};
static int ie_NOTIFY[] = {IE_BEARER, IE_NOTIFY | IE_MANDATORY, IE_DISPLAY, -1};
static int ie_PROGRESS[] = {IE_BEARER, IE_CAUSE, IE_FACILITY, IE_PROGRESS |
		IE_MANDATORY, IE_DISPLAY, IE_HLC, IE_USER_USER, -1};
static int ie_RELEASE[] = {IE_CAUSE | IE_MANDATORY_1, IE_FACILITY, IE_DISPLAY,
		IE_SIGNAL, IE_USER_USER, -1};
/* a RELEASE_COMPLETE with errors don't require special actions 
static int ie_RELEASE_COMPLETE[] = {IE_CAUSE | IE_MANDATORY_1, IE_FACILITY,
		IE_DISPLAY, IE_SIGNAL, IE_USER_USER, -1};
*/
static int ie_RESUME_ACKNOWLEDGE[] = {IE_CHANNEL_ID| IE_MANDATORY, IE_FACILITY,
		IE_DISPLAY, -1};
static int ie_RESUME_REJECT[] = {IE_CAUSE | IE_MANDATORY, IE_DISPLAY, -1};
static int ie_SETUP[] = {IE_COMPLETE, IE_BEARER  | IE_MANDATORY,
		IE_CHANNEL_ID| IE_MANDATORY, IE_FACILITY, IE_PROGRESS,
		IE_NET_FAC, IE_DISPLAY, IE_KEYPAD, IE_SIGNAL, IE_CALLING_PN,
		IE_CALLING_SUB, IE_CALLED_PN, IE_CALLED_SUB, IE_REDIR_NR,
		IE_LLC, IE_HLC, IE_USER_USER, -1};
static int ie_SETUP_ACKNOWLEDGE[] = {IE_CHANNEL_ID | IE_MANDATORY, IE_FACILITY,
		IE_PROGRESS, IE_DISPLAY, IE_SIGNAL, -1};
static int ie_STATUS[] = {IE_CAUSE | IE_MANDATORY, IE_CALL_STATE |
		IE_MANDATORY, IE_DISPLAY, -1};
static int ie_STATUS_ENQUIRY[] = {IE_DISPLAY, -1};
static int ie_SUSPEND_ACKNOWLEDGE[] = {IE_FACILITY, IE_DISPLAY, -1};
static int ie_SUSPEND_REJECT[] = {IE_CAUSE | IE_MANDATORY, IE_DISPLAY, -1};
/* not used 
 * static int ie_CONGESTION_CONTROL[] = {IE_CONGESTION | IE_MANDATORY,
 *		IE_CAUSE | IE_MANDATORY, IE_DISPLAY, -1};
 * static int ie_USER_INFORMATION[] = {IE_MORE_DATA, IE_USER_USER | IE_MANDATORY, -1};
 * static int ie_RESTART[] = {IE_CHANNEL_ID, IE_DISPLAY, IE_RESTART_IND |
 *		IE_MANDATORY, -1};
 */
static int ie_FACILITY[] = {IE_FACILITY | IE_MANDATORY, IE_DISPLAY, -1};
static int comp_required[] = {1,2,3,5,6,7,9,10,11,14,15,-1};
static int l3_valid_states[] = {0,1,2,3,4,6,7,8,9,10,11,12,15,17,19,25,-1};

struct ie_len {
	int ie;
	int len;
};

static
struct ie_len max_ie_len[] = {
	{IE_SEGMENT, 4},
	{IE_BEARER, 12},
	{IE_CAUSE, 32},
	{IE_CALL_ID, 10},
	{IE_CALL_STATE, 3},
	{IE_CHANNEL_ID,	34},
	{IE_FACILITY, 255},
	{IE_PROGRESS, 4},
	{IE_NET_FAC, 255},
	{IE_NOTIFY, 3},
	{IE_DISPLAY, 82},
	{IE_DATE, 8},
	{IE_KEYPAD, 34},
	{IE_SIGNAL, 3},
	{IE_INFORATE, 6},
	{IE_E2E_TDELAY, 11},
	{IE_TDELAY_SEL, 5},
	{IE_PACK_BINPARA, 3},
	{IE_PACK_WINSIZE, 4},
	{IE_PACK_SIZE, 4},
	{IE_CUG, 7},
	{IE_REV_CHARGE, 3},
	{IE_CALLING_PN, 24},
	{IE_CALLING_SUB, 23},
	{IE_CALLED_PN, 24},
	{IE_CALLED_SUB, 23},
	{IE_REDIR_NR, 255},
	{IE_TRANS_SEL, 255},
	{IE_RESTART_IND, 3},
	{IE_LLC, 18},
	{IE_HLC, 5},
	{IE_USER_USER, 131},
	{-1,0},
};

static int
getmax_ie_len(u_char ie) {
	int i = 0;
	while (max_ie_len[i].ie != -1) {
		if (max_ie_len[i].ie == ie)
			return(max_ie_len[i].len);
		i++;
	}
	return(255);
}

static int
ie_in_set(l3_process_t *pc, u_char ie, int *checklist) {
	int ret = 1;

	while (*checklist != -1) {
		if ((*checklist & 0xff) == ie) {
			if (ie & 0x80)
				return(-ret);
			else
				return(ret);
		}
		ret++;
		checklist++;
	}
	return(0);
}

static int
check_infoelements(l3_process_t *pc, struct sk_buff *skb, int *checklist)
{
	Q931_info_t	*qi = (Q931_info_t *)skb->data;
	int		*cl = checklist;
	u_char		*p, ie;
	u16		*iep;
	int		i, l, newpos, oldpos;
	int		err_seq = 0, err_len = 0, err_compr = 0, err_ureg = 0;
	
	p = skb->data;
	p += L3_EXTRA_SIZE;
	iep = &qi->bearer_capability;
	oldpos = -1;
	for (i=0; i<32; i++) {
		if (iep[i]) {
			ie = mISDN_l3_pos2ie(i);
			if ((newpos = ie_in_set(pc, ie, cl))) {
				if (newpos > 0) {
					if (newpos < oldpos)
						err_seq++;
					else
						oldpos = newpos;
				}
			} else {
				if (ie_in_set(pc, ie, comp_required))
					err_compr++;
				else
					err_ureg++;
			}
			l = p[iep[i] +1];
			if (l > getmax_ie_len(ie))
				err_len++;
		}
	}
	if (err_compr | err_ureg | err_len | err_seq) {
		if (pc->l3->debug & L3_DEB_CHECK)
			l3_debug(pc->l3, "check IE MT(%x) %d/%d/%d/%d",
				qi->type, err_compr, err_ureg, err_len, err_seq);
		if (err_compr)
			return(ERR_IE_COMPREHENSION);
		if (err_ureg)
			return(ERR_IE_UNRECOGNIZED);
		if (err_len)
			return(ERR_IE_LENGTH);
		if (err_seq)
			return(ERR_IE_SEQUENCE);
	} 
	return(0);
}

/* verify if a message type exists and contain no IE error */
static int
l3dss1_check_messagetype_validity(l3_process_t *pc, int mt, void *arg)
{
	switch (mt) {
		case MT_ALERTING:
		case MT_CALL_PROCEEDING:
		case MT_CONNECT:
		case MT_CONNECT_ACKNOWLEDGE:
		case MT_DISCONNECT:
		case MT_INFORMATION:
		case MT_FACILITY:
		case MT_NOTIFY:
		case MT_PROGRESS:
		case MT_RELEASE:
		case MT_RELEASE_COMPLETE:
		case MT_SETUP:
		case MT_SETUP_ACKNOWLEDGE:
		case MT_RESUME_ACKNOWLEDGE:
		case MT_RESUME_REJECT:
		case MT_SUSPEND_ACKNOWLEDGE:
		case MT_SUSPEND_REJECT:
		case MT_USER_INFORMATION:
		case MT_RESTART:
		case MT_RESTART_ACKNOWLEDGE:
		case MT_CONGESTION_CONTROL:
		case MT_STATUS:
		case MT_STATUS_ENQUIRY:
			if (pc->l3->debug & L3_DEB_CHECK)
				l3_debug(pc->l3, "l3dss1_check_messagetype_validity mt(%x) OK", mt);
			break;
		case MT_RESUME: /* RESUME only in user->net */
		case MT_SUSPEND: /* SUSPEND only in user->net */
		default:
			if (pc->l3->debug & (L3_DEB_CHECK | L3_DEB_WARN))
				l3_debug(pc->l3, "l3dss1_check_messagetype_validity mt(%x) fail", mt);
			l3dss1_status_send(pc, CAUSE_MT_NOTIMPLEMENTED);
			return(1);
	}
	return(0);
}

static void
l3dss1_std_ie_err(l3_process_t *pc, int ret) {

	if (pc->l3->debug & L3_DEB_CHECK)
		l3_debug(pc->l3, "check_infoelements ret %d", ret);
	switch(ret) {
		case 0: 
			break;
		case ERR_IE_COMPREHENSION:
			l3dss1_status_send(pc, CAUSE_MANDATORY_IE_MISS);
			break;
		case ERR_IE_UNRECOGNIZED:
			l3dss1_status_send(pc, CAUSE_IE_NOTIMPLEMENTED);
			break;
		case ERR_IE_LENGTH:
			l3dss1_status_send(pc, CAUSE_INVALID_CONTENTS);
			break;
		case ERR_IE_SEQUENCE:
		default:
			break;
	}
}

static int
l3dss1_get_channel_id(l3_process_t *pc, struct sk_buff *skb) {
	Q931_info_t	*qi = (Q931_info_t *)skb->data;
	u_char		*p;

	if (qi->channel_id) {
		p = skb->data;
		p += L3_EXTRA_SIZE + qi->channel_id;
		p++;
		if (test_bit(FLG_EXTCID, &pc->l3->Flag)) {
			if (*p != 1) {
				pc->bc = 1;
				return (0);
			}
		}
		if (*p != 1) { /* len for BRI = 1 */
			if (pc->l3->debug & L3_DEB_WARN)
				l3_debug(pc->l3, "wrong chid len %d", *p);
			return (-2);
		}
		p++;
		if (*p & 0x60) { /* only base rate interface */
			if (pc->l3->debug & L3_DEB_WARN)
				l3_debug(pc->l3, "wrong chid %x", *p);
			return (-3);
		}
		pc->bc = *p & 3;
	} else
		return(-1);
	return(0);
}

static int
l3dss1_get_cause(l3_process_t *pc, struct sk_buff *skb) {
	Q931_info_t	*qi = (Q931_info_t *)skb->data;
	u_char		l;
	u_char		*p;

	if (qi->cause) {
		p = skb->data;
		p += L3_EXTRA_SIZE + qi->cause;
		p++;
		l = *p++;
		if (l>30) {
			return(-30);
		}
		if (l)
			l--;
		else {
			return(-2);
		}
		if (l && !(*p & 0x80)) {
			l--;
			p++; /* skip recommendation */
		}
		p++;
		if (l) {
			if (!(*p & 0x80)) {
				return(-3);
			}
			pc->err = *p & 0x7F;
		} else {
			return(-4);
		}
	} else
		return(-1);
	return(0);
}

static void
l3dss1_release_req(l3_process_t *pc, u_char pr, void *arg)
{
	StopAllL3Timer(pc);
	if (arg) {
		SendMsg(pc, arg, 19);
	} else {
		newl3state(pc, 19);
		l3dss1_message(pc, MT_RELEASE);
	}
	L3AddTimer(&pc->timer, T308, CC_T308_1);
}

static void
l3dss1_setup_req(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff	*skb = skb_clone(arg, GFP_ATOMIC);

	if (!SendMsg(pc, arg, 1)) {
		L3DelTimer(&pc->timer);
		L3AddTimer(&pc->timer, T303, CC_T303);
		pc->t303skb = skb;
	} else
		dev_kfree_skb(skb);
}

static void
l3dss1_disconnect_req(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff	*skb = arg;
	Q931_info_t	*qi;
	u_char		*p;

	StopAllL3Timer(pc);
	if (arg) {
		qi = (Q931_info_t *)skb->data;
		if (!qi->cause) {
			qi->cause = skb->len - L3_EXTRA_SIZE;
			p = skb_put(skb, 4);
			*p++ = IE_CAUSE;
			*p++ = 2;
			*p++ = 0x80 | CAUSE_LOC_USER;
			*p++ = 0x80 | CAUSE_NORMALUNSPECIFIED;
		}
		SendMsg(pc, arg, 11);
	} else {
		newl3state(pc, 11);
		l3dss1_message_cause(pc, MT_DISCONNECT, CAUSE_NORMALUNSPECIFIED);
	}
	L3AddTimer(&pc->timer, T305, CC_T305);
}

static void
l3dss1_connect_req(l3_process_t *pc, u_char pr, void *arg)
{
	if (!pc->bc) {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "D-chan connect for waiting call");
		l3dss1_disconnect_req(pc, pr, NULL);
		return;
	}
	if (arg) {
		SendMsg(pc, arg, 8);
	} else {
		newl3state(pc, 8);
		l3dss1_message(pc, MT_CONNECT);
	}
	L3DelTimer(&pc->timer);
	L3AddTimer(&pc->timer, T313, CC_T313);
}

static void
l3dss1_release_cmpl_req(l3_process_t *pc, u_char pr, void *arg)
{
	StopAllL3Timer(pc);
	if (arg) {
		SendMsg(pc, arg, 0);
	} else {
		newl3state(pc, 0);
		l3dss1_message(pc, MT_RELEASE_COMPLETE);
	}
	mISDN_l3up(pc, CC_RELEASE_COMPLETE | CONFIRM, NULL);
	release_l3_process(pc);
}

static void
l3dss1_alert_req(l3_process_t *pc, u_char pr, void *arg)
{
	if (arg) {
		SendMsg(pc, arg, 7);
	} else {
		newl3state(pc, 7);
		l3dss1_message(pc, MT_ALERTING);
	}
	L3DelTimer(&pc->timer);
}

static void
l3dss1_proceed_req(l3_process_t *pc, u_char pr, void *arg)
{
	if (arg) {
		SendMsg(pc, arg, 9);
	} else {
		newl3state(pc, 9);
		l3dss1_message(pc, MT_CALL_PROCEEDING);
	}
	L3DelTimer(&pc->timer);
}

static void
l3dss1_setup_ack_req(l3_process_t *pc, u_char pr, void *arg)
{
	if (arg) {
		SendMsg(pc, arg, 25);
	} else {
		newl3state(pc, 25);
		l3dss1_message(pc, MT_SETUP_ACKNOWLEDGE);
	}
	L3DelTimer(&pc->timer);
	L3AddTimer(&pc->timer, T302, CC_T302);
}

static void
l3dss1_suspend_req(l3_process_t *pc, u_char pr, void *arg)
{
	if (arg) {
		SendMsg(pc, arg, 15);
	} else {
		newl3state(pc, 15);
		l3dss1_message(pc, MT_SUSPEND);
	}
	L3AddTimer(&pc->timer, T319, CC_T319);
}

static void
l3dss1_resume_req(l3_process_t *pc, u_char pr, void *arg)
{
	if (arg) {
		SendMsg(pc, arg, 17);
	} else {
		newl3state(pc, 17);
		l3dss1_message(pc, MT_RESUME);
	}
	L3AddTimer(&pc->timer, T318, CC_T318);
}

static void
l3dss1_status_enq_req(l3_process_t *pc, u_char pr, void *arg)
{
	if (arg)
		dev_kfree_skb(arg);
	l3dss1_message(pc, MT_STATUS_ENQUIRY);
}

static void
l3dss1_information_req(l3_process_t *pc, u_char pr, void *arg)
{
	if (pc->state == 2) {
		L3DelTimer(&pc->timer);
		L3AddTimer(&pc->timer, T304, CC_T304);
	}

	if (arg) {
		SendMsg(pc, arg, 2);
	}
}

static void
l3dss1_progress_req(l3_process_t *pc, u_char pr, void *arg)
{
	if (arg) {
		SendMsg(pc, arg, 10);
	}
}

static void
l3dss1_release_cmpl(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff	*skb = arg;

	StopAllL3Timer(pc);
	newl3state(pc, 0);
	if (mISDN_l3up(pc, CC_RELEASE_COMPLETE | INDICATION, skb))
		dev_kfree_skb(skb);
	release_l3_process(pc);
}

static void
l3dss1_alerting(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;

	ret = check_infoelements(pc, skb, ie_ALERTING);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		dev_kfree_skb(skb);
		return;
	}
	L3DelTimer(&pc->timer);	/* T304 */
	if (pc->t303skb) {
		dev_kfree_skb(pc->t303skb);
		pc->t303skb = NULL;
	}
	newl3state(pc, 4);
	if (ret)
		l3dss1_std_ie_err(pc, ret);
	if (mISDN_l3up(pc, CC_ALERTING | INDICATION, skb))
		dev_kfree_skb(skb);
}

static void
l3dss1_call_proc(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff	*skb = arg;
	int		ret;
	u_char		cause;

	if (!(ret = l3dss1_get_channel_id(pc, skb))) {
		if ((0 == pc->bc) || (3 == pc->bc)) {
			if (pc->l3->debug & L3_DEB_WARN)
				l3_debug(pc->l3, "setup answer with wrong chid %x", pc->bc);
			l3dss1_status_send(pc, CAUSE_INVALID_CONTENTS);
			return;
		}
	} else if (1 == pc->state) {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "setup answer wrong chid (ret %d)", ret);
		if (ret == -1)
			cause = CAUSE_MANDATORY_IE_MISS;
		else
			cause = CAUSE_INVALID_CONTENTS;
		l3dss1_status_send(pc, cause);
		return;
	}
	/* Now we are on none mandatory IEs */
	ret = check_infoelements(pc, skb, ie_CALL_PROCEEDING);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		dev_kfree_skb(skb);
		return;
	}
	L3DelTimer(&pc->timer);
	if (pc->t303skb) {
		dev_kfree_skb(pc->t303skb);
		pc->t303skb = NULL;
	}
	newl3state(pc, 3);
	L3AddTimer(&pc->timer, T310, CC_T310);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3dss1_std_ie_err(pc, ret);
	if (mISDN_l3up(pc, CC_PROCEEDING | INDICATION, skb))
		dev_kfree_skb(skb);
}

static void
l3dss1_connect(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff	*skb = arg;
	int		ret;

	ret = check_infoelements(pc, skb, ie_CONNECT);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		dev_kfree_skb(skb);
		return;
	}
	L3DelTimer(&pc->timer);	/* T310 */
	if (pc->t303skb) {
		dev_kfree_skb(pc->t303skb);
		pc->t303skb = NULL;
	}
	l3dss1_message(pc, MT_CONNECT_ACKNOWLEDGE);
	newl3state(pc, 10);
	if (ret)
		l3dss1_std_ie_err(pc, ret);
	if (mISDN_l3up(pc, CC_CONNECT | INDICATION, skb))
		dev_kfree_skb(skb);
}

static void
l3dss1_connect_ack(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff	*skb = arg;
	int		ret;

	ret = check_infoelements(pc, skb, ie_CONNECT_ACKNOWLEDGE);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		dev_kfree_skb(skb);
		return;
	}
	newl3state(pc, 10);
	L3DelTimer(&pc->timer);
	if (pc->t303skb) {
		dev_kfree_skb(pc->t303skb);
		pc->t303skb = NULL;
	}
	if (ret)
		l3dss1_std_ie_err(pc, ret);
	if (mISDN_l3up(pc, CC_CONNECT_ACKNOWLEDGE | INDICATION, skb))
		dev_kfree_skb(skb);
}

static void
l3dss1_disconnect(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff	*skb = arg;
	int		ret;
	u_char		cause = 0;

	StopAllL3Timer(pc);
	if ((ret = l3dss1_get_cause(pc, skb))) {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "DISC get_cause ret(%d)", ret);
		if (ret == -1)
			cause = CAUSE_MANDATORY_IE_MISS;
		else
			cause = CAUSE_INVALID_CONTENTS;
	} 
	ret = check_infoelements(pc, skb, ie_DISCONNECT);
	if (ERR_IE_COMPREHENSION == ret)
		cause = CAUSE_MANDATORY_IE_MISS;
	else if ((!cause) && (ERR_IE_UNRECOGNIZED == ret))
		cause = CAUSE_IE_NOTIMPLEMENTED;
	ret = pc->state;
	if (cause)
		newl3state(pc, 19);
	else
		newl3state(pc, 12);
       	if (11 != ret) {
		if (mISDN_l3up(pc, CC_DISCONNECT | INDICATION, skb))
			dev_kfree_skb(skb);
	} else if (!cause) {
		l3dss1_release_req(pc, pr, NULL);
		dev_kfree_skb(skb);
	} else
		dev_kfree_skb(skb);
	if (cause) {
		l3dss1_message_cause(pc, MT_RELEASE, cause);
		L3AddTimer(&pc->timer, T308, CC_T308_1);
	}
}

static void
l3dss1_setup_ack(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff	*skb = arg;
	int		ret;
	u_char		cause;

	if (!(ret = l3dss1_get_channel_id(pc, skb))) {
		if ((0 == pc->bc) || (3 == pc->bc)) {
			if (pc->l3->debug & L3_DEB_WARN)
				l3_debug(pc->l3, "setup answer with wrong chid %x", pc->bc);
			l3dss1_status_send(pc, CAUSE_INVALID_CONTENTS);
			dev_kfree_skb(skb);
			return;
		}
	} else {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "setup answer wrong chid (ret %d)", ret);
		if (ret == -1)
			cause = CAUSE_MANDATORY_IE_MISS;
		else
			cause = CAUSE_INVALID_CONTENTS;
		l3dss1_status_send(pc, cause);
		dev_kfree_skb(skb);
		return;
	}
	/* Now we are on none mandatory IEs */
	ret = check_infoelements(pc, skb, ie_SETUP_ACKNOWLEDGE);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		dev_kfree_skb(skb);
		return;
	}
	L3DelTimer(&pc->timer);
	if (pc->t303skb) {
		dev_kfree_skb(pc->t303skb);
		pc->t303skb = NULL;
	}
	newl3state(pc, 2);
	L3AddTimer(&pc->timer, T304, CC_T304);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3dss1_std_ie_err(pc, ret);
	if (mISDN_l3up(pc, CC_SETUP_ACKNOWLEDGE | INDICATION, skb))
		dev_kfree_skb(skb);
}

static void
l3dss1_setup(l3_process_t *pc, u_char pr, void *arg)
{
	u_char		*p, cause, bc2 = 0;
	int		bcfound = 0;
	struct sk_buff	*skb = arg;
	Q931_info_t	*qi = (Q931_info_t *)skb->data;
	int		err = 0;

	/*
	 * Bearer Capabilities
	 */
	/* only the first occurence 'll be detected ! */
	p = skb->data;
	if (qi->bearer_capability) {
		p += L3_EXTRA_SIZE + qi->bearer_capability;
		p++;
		if ((p[0] < 2) || (p[0] > 11))
			err = 1;
		else {
			bc2 = p[2] & 0x7f;
			switch (p[1] & 0x7f) {
				case 0x00: /* Speech */
				case 0x10: /* 3.1 Khz audio */
				case 0x08: /* Unrestricted digital information */
				case 0x09: /* Restricted digital information */
				case 0x11:
					/* Unrestr. digital information  with 
					 * tones/announcements ( or 7 kHz audio
					 */
				case 0x18: /* Video */
					break;
				default:
					err = 2;
					break;
			}
			switch (p[2] & 0x7f) {
				case 0x40: /* packed mode */
				case 0x10: /* 64 kbit */
				case 0x11: /* 2*64 kbit */
				case 0x13: /* 384 kbit */
				case 0x15: /* 1536 kbit */
				case 0x17: /* 1920 kbit */
					break;
				default:
					err = 3;
					break;
			}
		}
		if (err) {
			if (pc->l3->debug & L3_DEB_WARN)
				l3_debug(pc->l3, "setup with wrong bearer(l=%d:%x,%x)",
					p[0], p[1], p[2]);
			l3dss1_msg_without_setup(pc, CAUSE_INVALID_CONTENTS);
			dev_kfree_skb(skb);
			return;
		} 
	} else {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "setup without bearer capabilities");
		/* ETS 300-104 1.3.3 */
		l3dss1_msg_without_setup(pc, CAUSE_MANDATORY_IE_MISS);
		dev_kfree_skb(skb);
		return;
	}
	/*
	 * Channel Identification
	 */
	if (!(err = l3dss1_get_channel_id(pc, skb))) {
		if (pc->bc) {
			if ((3 == pc->bc) && (0x10 == bc2)) {
				if (pc->l3->debug & L3_DEB_WARN)
					l3_debug(pc->l3, "setup with wrong chid %x",
						pc->bc);
				l3dss1_msg_without_setup(pc,
					CAUSE_INVALID_CONTENTS);
				dev_kfree_skb(skb);
				return;
			}
			bcfound++;
		} else {
			if (pc->l3->debug & L3_DEB_WARN)
				l3_debug(pc->l3, "setup without bchannel, call waiting");
			bcfound++;
		} 
	} else {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "setup with wrong chid ret %d", err);
		if (err == -1)
			cause = CAUSE_MANDATORY_IE_MISS;
		else
			cause = CAUSE_INVALID_CONTENTS;
		l3dss1_msg_without_setup(pc, cause);
		dev_kfree_skb(skb);
		return;
	}
	/* Now we are on none mandatory IEs */
	err = check_infoelements(pc, skb, ie_SETUP);
	if (ERR_IE_COMPREHENSION == err) {
		l3dss1_msg_without_setup(pc, CAUSE_MANDATORY_IE_MISS);
		dev_kfree_skb(skb);
		return;
	}
	newl3state(pc, 6);
	L3DelTimer(&pc->timer);
	L3AddTimer(&pc->timer, T_CTRL, CC_TCTRL);
	if (err) /* STATUS for none mandatory IE errors after actions are taken */
		l3dss1_std_ie_err(pc, err);
// already done
//	err = mISDN_l3up(pc, CC_NEW_CR | INDICATION, NULL);
	if (mISDN_l3up(pc, CC_SETUP | INDICATION, skb))
		dev_kfree_skb(skb);
}

static void
l3dss1_reset(l3_process_t *pc, u_char pr, void *arg)
{
	release_l3_process(pc);
}

static void
l3dss1_release(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff	*skb = arg;
	int		ret, cause=0;

	StopAllL3Timer(pc);
	if ((ret = l3dss1_get_cause(pc, skb))) {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "REL get_cause ret(%d)", ret);
		if ((ret == -1) && (pc->state != 11))
			cause = CAUSE_MANDATORY_IE_MISS;
		else if (ret != -1)
			cause = CAUSE_INVALID_CONTENTS;
	}
	ret = check_infoelements(pc, skb, ie_RELEASE);
	if (ERR_IE_COMPREHENSION == ret)
		cause = CAUSE_MANDATORY_IE_MISS;
	else if ((ERR_IE_UNRECOGNIZED == ret) && (!cause))
		cause = CAUSE_IE_NOTIMPLEMENTED;
	if (cause)
		l3dss1_message_cause(pc, MT_RELEASE_COMPLETE, cause);
	else
		l3dss1_message(pc, MT_RELEASE_COMPLETE);
	if (mISDN_l3up(pc, CC_RELEASE | INDICATION, skb))
		dev_kfree_skb(skb);
	newl3state(pc, 0);
	release_l3_process(pc);
}

static void
l3dss1_progress(l3_process_t *pc, u_char pr, void *arg) {
	struct sk_buff	*skb = arg;
	Q931_info_t	*qi = (Q931_info_t *)skb->data;
	int		err = 0;
	u_char		*p, cause = CAUSE_INVALID_CONTENTS;

	if (qi->progress) {
		p = skb->data;
		p += L3_EXTRA_SIZE + qi->progress;
		p++;
		if (p[0] != 2) {
			err = 1;
		} else if (!(p[1] & 0x70)) {
			switch (p[1]) {
				case 0x80:
				case 0x81:
				case 0x82:
				case 0x84:
				case 0x85:
				case 0x87:
				case 0x8a:
					switch (p[2]) {
						case 0x81:
						case 0x82:
						case 0x83:
						case 0x84:
						case 0x88:
							break;
						default:
							err = 2;
							break;
					}
					break;
				default:
					err = 3;
					break;
			}
		}
	} else {
		cause = CAUSE_MANDATORY_IE_MISS;
		err = 4;
	}
	if (err) {	
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "progress error %d", err);
		l3dss1_status_send(pc, cause);
		dev_kfree_skb(skb);
		return;
	}
	/* Now we are on none mandatory IEs */
	err = check_infoelements(pc, skb, ie_PROGRESS);
	if (err)
		l3dss1_std_ie_err(pc, err);
	if (ERR_IE_COMPREHENSION != err) {
		if (mISDN_l3up(pc, CC_PROGRESS | INDICATION, skb))
			dev_kfree_skb(skb);
	} else
		dev_kfree_skb(skb);
}

static void
l3dss1_notify(l3_process_t *pc, u_char pr, void *arg) {
	struct sk_buff	*skb = arg;
	Q931_info_t	*qi = (Q931_info_t *)skb->data;
	int		err = 0;
	u_char		*p, cause = CAUSE_INVALID_CONTENTS;
                        
	if (qi->notify) {
		p = skb->data;
		p += L3_EXTRA_SIZE + qi->notify;
		p++;
		if (p[0] != 1) {
			err = 1;
		} else {
			switch (p[1]) {
				case 0x80:
				case 0x81:
				case 0x82:
					break;
				default:
					err = 2;
					break;
			}
		}
	} else {
		cause = CAUSE_MANDATORY_IE_MISS;
		err = 3;
	}
	if (err) {	
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "notify error %d", err);
		l3dss1_status_send(pc, cause);
		dev_kfree_skb(skb);
		return;
	}
	/* Now we are on none mandatory IEs */
	err = check_infoelements(pc, skb, ie_NOTIFY);
	if (err)
		l3dss1_std_ie_err(pc, err);
	if (ERR_IE_COMPREHENSION != err) {
		if (mISDN_l3up(pc, CC_NOTIFY | INDICATION, skb))
			dev_kfree_skb(skb);
	} else
		dev_kfree_skb(skb);
}

static void
l3dss1_status_enq(l3_process_t *pc, u_char pr, void *arg) {
	int		ret;
	struct sk_buff	*skb = arg;

	ret = check_infoelements(pc, skb, ie_STATUS_ENQUIRY);
	l3dss1_std_ie_err(pc, ret);
	l3dss1_status_send(pc, CAUSE_STATUS_RESPONSE);
	if (mISDN_l3up(pc, CC_STATUS_ENQUIRY | INDICATION, skb))
		dev_kfree_skb(skb);
}

static void
l3dss1_information(l3_process_t *pc, u_char pr, void *arg) {
	int		ret;
	struct sk_buff	*skb = arg;

	ret = check_infoelements(pc, skb, ie_INFORMATION);
	if (ret)
		l3dss1_std_ie_err(pc, ret);
	if (pc->state == 25) { /* overlap receiving */
		L3DelTimer(&pc->timer);
		L3AddTimer(&pc->timer, T302, CC_T302);
	}
	if (mISDN_l3up(pc, CC_INFORMATION | INDICATION, skb))
		dev_kfree_skb(skb);
}

static void
l3dss1_release_ind(l3_process_t *pc, u_char pr, void *arg)
{
	u_char		*p;
	struct sk_buff	*skb = arg;
	int		err, callState = -1;
	Q931_info_t	*qi = (Q931_info_t *)skb->data;

	if (qi->call_state) {
		p = skb->data;
		p += L3_EXTRA_SIZE + qi->call_state;
		p++;
		if (1 == *p++)
			callState = *p;
	}
	if (callState == 0) {
		/* ETS 300-104 7.6.1, 8.6.1, 10.6.1... and 16.1
		 * set down layer 3 without sending any message
		 */
		newl3state(pc, 0);
		err = mISDN_l3up(pc, CC_RELEASE | INDICATION, skb);
		release_l3_process(pc);
	} else {
		err = mISDN_l3up(pc, CC_RELEASE | INDICATION, skb);
	}
	if (err)
		dev_kfree_skb(skb);
}

static void
l3dss1_restart(l3_process_t *pc, u_char pr, void *arg) {
	struct sk_buff	*skb = arg;
	L3DelTimer(&pc->timer);
	mISDN_l3up(pc, CC_RELEASE | INDICATION, NULL);
	release_l3_process(pc);
	if (skb)
		dev_kfree_skb(skb);
}

static void
l3dss1_status(l3_process_t *pc, u_char pr, void *arg) {
	struct sk_buff	*skb = arg;
	Q931_info_t	*qi = (Q931_info_t *)skb->data;
	int		ret = 0; 
	u_char		*p, cause = 0, callState = 0xff;
	
	if ((ret = l3dss1_get_cause(pc, skb))) {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "STATUS get_cause ret(%d)", ret);
		if (ret == -1)
			cause = CAUSE_MANDATORY_IE_MISS;
		else
			cause = CAUSE_INVALID_CONTENTS;
	}
	if (qi->call_state) {
		p = skb->data;
		p += L3_EXTRA_SIZE + qi->call_state;
		p++;
		if (1 == *p++) {
			callState = *p;
			if (!ie_in_set(pc, callState, l3_valid_states))
				cause = CAUSE_INVALID_CONTENTS;
		} else
			cause = CAUSE_INVALID_CONTENTS;
	} else
		cause = CAUSE_MANDATORY_IE_MISS;
	if (!cause) { /*  no error before */
		ret = check_infoelements(pc, skb, ie_STATUS);
		if (ERR_IE_COMPREHENSION == ret)
			cause = CAUSE_MANDATORY_IE_MISS;
		else if (ERR_IE_UNRECOGNIZED == ret)
			cause = CAUSE_IE_NOTIMPLEMENTED;
	}
	if (cause) {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "STATUS error(%d/%d)", ret, cause);
		l3dss1_status_send(pc, cause);
		if (cause != CAUSE_IE_NOTIMPLEMENTED) {
			dev_kfree_skb(skb);
			return;
		}
	}
	if (qi->cause)
		cause = pc->err & 0x7f;
	if ((cause == CAUSE_PROTOCOL_ERROR) && (callState == 0)) {
		/* ETS 300-104 7.6.1, 8.6.1, 10.6.1...
		 * if received MT_STATUS with cause == 111 and call
		 * state == 0, then we must set down layer 3
		 */
		newl3state(pc, 0);
		ret = mISDN_l3up(pc, CC_STATUS| INDICATION, skb);
		release_l3_process(pc);
	} else
		ret = mISDN_l3up(pc, CC_STATUS | INDICATION, skb);
	if (ret)
		dev_kfree_skb(skb);
}

static void
l3dss1_facility(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff	*skb = arg;
	Q931_info_t	*qi = (Q931_info_t *)skb->data;
	int		ret;
	
	ret = check_infoelements(pc, skb, ie_FACILITY);
	l3dss1_std_ie_err(pc, ret);
	if (!qi->facility) {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "FACILITY without IE_FACILITY");
		dev_kfree_skb(skb);
		return;
	}		
	if (mISDN_l3up(pc, CC_FACILITY | INDICATION, skb))
		dev_kfree_skb(skb);
}

static void
l3dss1_suspend_ack(l3_process_t *pc, u_char pr, void *arg) {
	struct sk_buff	*skb = arg;
	int		ret;

	L3DelTimer(&pc->timer);
	newl3state(pc, 0);
	/* We don't handle suspend_ack for IE errors now */
	if ((ret = check_infoelements(pc, skb, ie_SUSPEND_ACKNOWLEDGE)))
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "SUSPACK check ie(%d)",ret);
	if (mISDN_l3up(pc, CC_SUSPEND_ACKNOWLEDGE | INDICATION, skb))
		dev_kfree_skb(skb);
	release_l3_process(pc);
}

static void
l3dss1_suspend_rej(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff	*skb = arg;
	int		ret;
	u_char		cause;

	if ((ret = l3dss1_get_cause(pc, skb))) {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "SUSP_REJ get_cause err(%d)", ret);
		if (ret == -1) 
			cause = CAUSE_MANDATORY_IE_MISS;
		else
			cause = CAUSE_INVALID_CONTENTS;
		l3dss1_status_send(pc, cause);
		dev_kfree_skb(skb);
		return;
	}
	ret = check_infoelements(pc, skb, ie_SUSPEND_REJECT);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		dev_kfree_skb(skb);
		return;
	}
	L3DelTimer(&pc->timer);
	if (mISDN_l3up(pc, CC_SUSPEND_REJECT | INDICATION, skb))
		dev_kfree_skb(skb);
	newl3state(pc, 10);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3dss1_std_ie_err(pc, ret);
}

static void
l3dss1_resume_ack(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff	*skb = arg;
	int		ret;

	if (!(ret = l3dss1_get_channel_id(pc, skb))) {
		if ((0 == pc->bc) || (3 == pc->bc)) {
			if (pc->l3->debug & L3_DEB_WARN)
				l3_debug(pc->l3, "resume ack with wrong chid %x",
					pc->bc);
			l3dss1_status_send(pc, CAUSE_INVALID_CONTENTS);
			dev_kfree_skb(skb);
			return;
		}
	} else if (1 == pc->state) {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "resume ack without chid err(%d)",
				ret);
		l3dss1_status_send(pc, CAUSE_MANDATORY_IE_MISS);
		dev_kfree_skb(skb);
		return;
	}
	ret = check_infoelements(pc, skb, ie_RESUME_ACKNOWLEDGE);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		dev_kfree_skb(skb);
		return;
	}
	L3DelTimer(&pc->timer);
	if (mISDN_l3up(pc, CC_RESUME_ACKNOWLEDGE | INDICATION, skb))
		dev_kfree_skb(skb);
	newl3state(pc, 10);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3dss1_std_ie_err(pc, ret);
}

static void
l3dss1_resume_rej(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff	*skb = arg;
	int		ret;
	u_char		cause;

	if ((ret = l3dss1_get_cause(pc, skb))) {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "RES_REJ get_cause err(%d)", ret);
		if (ret == -1) 
			cause = CAUSE_MANDATORY_IE_MISS;
		else
			cause = CAUSE_INVALID_CONTENTS;
		l3dss1_status_send(pc, cause);
		dev_kfree_skb(skb);
		return;
	}
	ret = check_infoelements(pc, skb, ie_RESUME_REJECT);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		dev_kfree_skb(skb);
		return;
	}
	L3DelTimer(&pc->timer);
	if (mISDN_l3up(pc, CC_RESUME_REJECT | INDICATION, skb))
		dev_kfree_skb(skb);
	newl3state(pc, 0);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3dss1_std_ie_err(pc, ret);
	release_l3_process(pc);
}

static void
l3dss1_global_restart(l3_process_t *pc, u_char pr, void *arg)
{
	u_char		*p, ri, ch = 0, chan = 0;
	struct sk_buff	*skb = arg;
	Q931_info_t	*qi = (Q931_info_t *)skb->data;
	l3_process_t	*up, *n;

//	newl3state(pc, 2);
	L3DelTimer(&pc->timer);
	if (qi->restart_ind) {
		p = skb->data;
		p += L3_EXTRA_SIZE + qi->restart_ind;
		p++;
		ri = p[1];
		l3_debug(pc->l3, "Restart %x", ri);
	} else {
		l3_debug(pc->l3, "Restart without restart IE");
		ri = 0x86;
	}
	if (qi->channel_id) {
		p = skb->data;
		p += L3_EXTRA_SIZE + qi->channel_id;
		p++;
		chan = p[1] & 3;
		ch = p[1];
		if (pc->l3->debug)
			l3_debug(pc->l3, "Restart for channel %d", chan);
	}
	list_for_each_entry_safe(up, n, &pc->l3->plist, list) {
		if ((ri & 7) == 7)
			dss1man(up, CC_RESTART | REQUEST, NULL);
		else if (up->bc == chan)
			mISDN_l3up(up, CC_RESTART | REQUEST, NULL);
	}
	dev_kfree_skb(skb);
	skb = MsgStart(pc, MT_RESTART_ACKNOWLEDGE, chan ? 6 : 3);
	p = skb_put(skb, chan ? 6 : 3);
	if (chan) {
		*p++ = IE_CHANNEL_ID;
		*p++ = 1;
		*p++ = ch | 0x80;
	}
	*p++ = IE_RESTART_IND;
	*p++ = 1;
	*p++ = ri;
	if (l3_msg(pc->l3, DL_DATA | REQUEST, 0, 0, skb))
		kfree_skb(skb);
}

static void
l3dss1_dummy(l3_process_t *pc, u_char pr, void *arg)
{
}

static void
l3dss1_t302(l3_process_t *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	newl3state(pc, 11);
	l3dss1_message_cause(pc, MT_DISCONNECT, CAUSE_INVALID_NUMBER);
	mISDN_l3up(pc, CC_TIMEOUT | INDICATION, NULL);
}

static void
l3dss1_t303(l3_process_t *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	if (pc->n303 > 0) {
		pc->n303--;
		if (pc->t303skb) {
			struct sk_buff	*skb;
			if (pc->n303 > 0) {
				skb = skb_clone(pc->t303skb, GFP_ATOMIC);
			} else {
				skb = pc->t303skb;
				pc->t303skb = NULL;
			}
			if (skb)
				SendMsg(pc, skb, -1);
		}
		L3AddTimer(&pc->timer, T303, CC_T303);
		return;
	}
	if (pc->t303skb)
		kfree_skb(pc->t303skb);
	pc->t303skb = NULL;
	l3dss1_message_cause(pc, MT_RELEASE_COMPLETE, CAUSE_TIMER_EXPIRED);
	mISDN_l3up(pc, CC_TIMEOUT | INDICATION, NULL);
	release_l3_process(pc);
}

static void
l3dss1_t304(l3_process_t *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	newl3state(pc, 11);
	l3dss1_message_cause(pc, MT_DISCONNECT, CAUSE_TIMER_EXPIRED);
	mISDN_l3up(pc, CC_TIMEOUT | INDICATION, NULL);
}

static void
l3dss1_t305(l3_process_t *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
#if 0
	if (pc->cause != NO_CAUSE)
		cause = pc->cause;
#endif
	newl3state(pc, 19);
	l3dss1_message_cause(pc, MT_RELEASE, CAUSE_NORMALUNSPECIFIED);
	L3AddTimer(&pc->timer, T308, CC_T308_1);
}

static void
l3dss1_t310(l3_process_t *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	newl3state(pc, 11);
	l3dss1_message_cause(pc, MT_DISCONNECT, CAUSE_TIMER_EXPIRED);
	mISDN_l3up(pc, CC_TIMEOUT | INDICATION, NULL);
}

static void
l3dss1_t313(l3_process_t *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	newl3state(pc, 11);
	l3dss1_message_cause(pc, MT_DISCONNECT, CAUSE_TIMER_EXPIRED);
	mISDN_l3up(pc, CC_TIMEOUT | INDICATION, NULL);
}

static void
l3dss1_t308_1(l3_process_t *pc, u_char pr, void *arg)
{
	newl3state(pc, 19);
	L3DelTimer(&pc->timer);
	l3dss1_message(pc, MT_RELEASE);
	L3AddTimer(&pc->timer, T308, CC_T308_2);
}

static void
l3dss1_t308_2(l3_process_t *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	mISDN_l3up(pc, CC_TIMEOUT | INDICATION, NULL);
	release_l3_process(pc);
}

static void
l3dss1_t318(l3_process_t *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
#if 0
	pc->cause = 102;	/* Timer expiry */
	pc->para.loc = 0;	/* local */
#endif
	mISDN_l3up(pc, CC_RESUME_REJECT | INDICATION, NULL);
	newl3state(pc, 19);
	l3dss1_message(pc, MT_RELEASE);
	L3AddTimer(&pc->timer, T308, CC_T308_1);
}

static void
l3dss1_t319(l3_process_t *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
#if 0
	pc->cause = 102;	/* Timer expiry */
	pc->para.loc = 0;	/* local */
#endif
	mISDN_l3up(pc, CC_SUSPEND_REJECT | INDICATION, NULL);
	newl3state(pc, 10);
}

static void
l3dss1_dl_reset(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff	*nskb, *skb = alloc_skb(L3_EXTRA_SIZE + 10, GFP_ATOMIC);
	Q931_info_t	*qi;
	u_char		*p;

	if (!skb)
		return;
	qi = (Q931_info_t *)skb_put(skb, L3_EXTRA_SIZE);
	mISDN_initQ931_info(qi);
	qi->type = MT_DISCONNECT;
	qi->cause = 1;
	p = skb_put(skb, 5);
	p++;
	*p++ = IE_CAUSE;
	*p++ = 2;
	*p++ = 0x80 | CAUSE_LOC_USER;
	*p++ = 0x80 | CAUSE_TEMPORARY_FAILURE;
	nskb = skb_clone(skb, GFP_ATOMIC);
	l3dss1_disconnect_req(pc, pr, skb);
	if (nskb) {
		if (mISDN_l3up(pc, CC_DISCONNECT | REQUEST, nskb))
			dev_kfree_skb(nskb);
	}
}

static void
l3dss1_dl_release(l3_process_t *pc, u_char pr, void *arg)
{
	newl3state(pc, 0);
#if 0
        pc->cause = 0x1b;          /* Destination out of order */
        pc->para.loc = 0;
#endif
	release_l3_process(pc);
}

static void
l3dss1_dl_reestablish(l3_process_t *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	L3AddTimer(&pc->timer, T309, CC_T309);
	l3_msg(pc->l3, DL_ESTABLISH | REQUEST, 0, 0, NULL);
}
 
static void
l3dss1_dl_reest_status(l3_process_t *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);

	l3dss1_status_send(pc, CAUSE_NORMALUNSPECIFIED);
}

/* *INDENT-OFF* */
static struct stateentry downstatelist[] =
{
	{SBIT(0),
	 CC_SETUP | REQUEST, l3dss1_setup_req},
	{SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(9) |
		SBIT(10) | SBIT(11) | SBIT(12) | SBIT(15) | SBIT(25),
	 CC_INFORMATION | REQUEST, l3dss1_information_req},
	{SBIT(10),
	 CC_PROGRESS | REQUEST, l3dss1_progress_req},
	{SBIT(0),
	 CC_RESUME | REQUEST, l3dss1_resume_req},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(6) | SBIT(7) |
		SBIT(8) | SBIT(9) | SBIT(10) | SBIT(25),
	 CC_DISCONNECT | REQUEST, l3dss1_disconnect_req},
	{SBIT(11) | SBIT(12),
	 CC_RELEASE | REQUEST, l3dss1_release_req},
	{ALL_STATES,
	 CC_RESTART | REQUEST, l3dss1_restart},
	{SBIT(6) | SBIT(25),
	 CC_SETUP | RESPONSE, l3dss1_release_cmpl_req},
	{ALL_STATES,
	 CC_RELEASE_COMPLETE | REQUEST, l3dss1_release_cmpl_req},
	{SBIT(6) | SBIT(25),
	 CC_PROCEEDING | REQUEST, l3dss1_proceed_req},
	{SBIT(6),
	 CC_SETUP_ACKNOWLEDGE | REQUEST, l3dss1_setup_ack_req},
	{SBIT(25),
	 CC_SETUP_ACKNOWLEDGE | REQUEST, l3dss1_dummy},
	{SBIT(6) | SBIT(9) | SBIT(25),
	 CC_ALERTING | REQUEST, l3dss1_alert_req},
	{SBIT(6) | SBIT(7) | SBIT(9) | SBIT(25),
	 CC_CONNECT | REQUEST, l3dss1_connect_req},
	{SBIT(10),
	 CC_SUSPEND | REQUEST, l3dss1_suspend_req},
	{ALL_STATES,
	 CC_STATUS_ENQUIRY | REQUEST, l3dss1_status_enq_req},
};

#define DOWNSLLEN \
	(sizeof(downstatelist) / sizeof(struct stateentry))

static struct stateentry datastatelist[] =
{
	{ALL_STATES,
	 MT_STATUS_ENQUIRY, l3dss1_status_enq},
	{ALL_STATES,
	 MT_FACILITY, l3dss1_facility},
	{SBIT(19),
	 MT_STATUS, l3dss1_release_ind},
	{ALL_STATES,
	 MT_STATUS, l3dss1_status},
	{SBIT(0),
	 MT_SETUP, l3dss1_setup},
	{SBIT(6) | SBIT(7) | SBIT(8) | SBIT(9) | SBIT(10) | SBIT(11) | SBIT(12) |
	 SBIT(15) | SBIT(17) | SBIT(19) | SBIT(25),
	 MT_SETUP, l3dss1_dummy},
	{SBIT(1) | SBIT(2),
	 MT_CALL_PROCEEDING, l3dss1_call_proc},
	{SBIT(1),
	 MT_SETUP_ACKNOWLEDGE, l3dss1_setup_ack},
	{SBIT(2) | SBIT(3),
	 MT_ALERTING, l3dss1_alerting},
	{SBIT(2) | SBIT(3),
	 MT_PROGRESS, l3dss1_progress},
	{SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(9) | SBIT(10) |
	 SBIT(11) | SBIT(12) | SBIT(15) | SBIT(17) | SBIT(19) | SBIT(25),
	 MT_INFORMATION, l3dss1_information},
	{SBIT(10) | SBIT(11) | SBIT(15),
	 MT_NOTIFY, l3dss1_notify},
	{SBIT(0) | SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(10) |
	 SBIT(11) | SBIT(12) | SBIT(15) | SBIT(17) | SBIT(19) | SBIT(25),
	 MT_RELEASE_COMPLETE, l3dss1_release_cmpl},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(9) | SBIT(10) | SBIT(11) | SBIT(12) | SBIT(15) | SBIT(17) | SBIT(25),
	 MT_RELEASE, l3dss1_release},
	{SBIT(19),  MT_RELEASE, l3dss1_release_ind},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(7) | SBIT(8) | SBIT(9) | SBIT(10) | SBIT(11) | SBIT(15) | SBIT(17) | SBIT(25),
	 MT_DISCONNECT, l3dss1_disconnect},
	{SBIT(19),
	 MT_DISCONNECT, l3dss1_dummy},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4),
	 MT_CONNECT, l3dss1_connect},
	{SBIT(8),
	 MT_CONNECT_ACKNOWLEDGE, l3dss1_connect_ack},
	{SBIT(15),
	 MT_SUSPEND_ACKNOWLEDGE, l3dss1_suspend_ack},
	{SBIT(15),
	 MT_SUSPEND_REJECT, l3dss1_suspend_rej},
	{SBIT(17),
	 MT_RESUME_ACKNOWLEDGE, l3dss1_resume_ack},
	{SBIT(17),
	 MT_RESUME_REJECT, l3dss1_resume_rej},
};

#define DATASLLEN \
	(sizeof(datastatelist) / sizeof(struct stateentry))

static struct stateentry globalmes_list[] =
{
	{ALL_STATES,
	 MT_STATUS, l3dss1_status},
	{SBIT(0),
	 MT_RESTART, l3dss1_global_restart},
/*	{SBIT(1),
	 MT_RESTART_ACKNOWLEDGE, l3dss1_restart_ack},
*/
};
#define GLOBALM_LEN \
	(sizeof(globalmes_list) / sizeof(struct stateentry))

static struct stateentry manstatelist[] =
{
        {SBIT(2),
         DL_ESTABLISH | INDICATION, l3dss1_dl_reset},
        {SBIT(10),
         DL_ESTABLISH | CONFIRM, l3dss1_dl_reest_status},
        {SBIT(10),
         DL_RELEASE | INDICATION, l3dss1_dl_reestablish},
        {ALL_STATES,
         DL_RELEASE | INDICATION, l3dss1_dl_release},
	{SBIT(25),
	 CC_T302, l3dss1_t302},
	{SBIT(1),
	 CC_T303, l3dss1_t303},
	{SBIT(2),
	 CC_T304, l3dss1_t304},
	{SBIT(3),
	 CC_T310, l3dss1_t310},
	{SBIT(8),
	 CC_T313, l3dss1_t313},
	{SBIT(11),
	 CC_T305, l3dss1_t305},
	{SBIT(15),
	 CC_T319, l3dss1_t319},
	{SBIT(17),
	 CC_T318, l3dss1_t318},
	{SBIT(19),
	 CC_T308_1, l3dss1_t308_1},
	{SBIT(19),
	 CC_T308_2, l3dss1_t308_2},
	{SBIT(10),
	 CC_T309, l3dss1_dl_release},
	{SBIT(6),
	 CC_TCTRL, l3dss1_reset},
	{ALL_STATES,
	 CC_RESTART | REQUEST, l3dss1_restart},
};

#define MANSLLEN \
        (sizeof(manstatelist) / sizeof(struct stateentry))
/* *INDENT-ON* */


static void
global_handler(layer3_t *l3, u_int mt, struct sk_buff *skb)
{
	u_int		i;
	l3_process_t	*proc = l3->global;

	proc->callref = skb->data[2]; /* cr flag */
	for (i = 0; i < GLOBALM_LEN; i++)
		if ((mt == globalmes_list[i].primitive) &&
		    ((1 << proc->state) & globalmes_list[i].state))
			break;
	if (i == GLOBALM_LEN) {
		if (l3->debug & L3_DEB_STATE) {
			l3_debug(l3, "dss1 global state %d mt %x unhandled",
				proc->state, mt);
		}
		l3dss1_status_send(proc, CAUSE_INVALID_CALLREF);
		dev_kfree_skb(skb);
	} else {
		if (l3->debug & L3_DEB_STATE) {
			l3_debug(l3, "dss1 global %d mt %x",
				proc->state, mt);
		}
		globalmes_list[i].rout(proc, mt, skb);
	}
}

static int
dss1_fromdown(mISDNif_t *hif, struct sk_buff *skb)
{
	layer3_t	*l3;
	u_int		i;
	int		cause, callState, ret = -EINVAL;
	char		*ptr;
	l3_process_t	*proc;
	mISDN_head_t	*hh;
	Q931_info_t	*qi;
	

	if (!hif || !skb)
		return(ret);
	l3 = hif->fdata;
	hh = mISDN_HEAD_P(skb);
	if (debug)
		printk(KERN_DEBUG "%s: prim(%x)\n", __FUNCTION__, hh->prim);
	if (!l3)
		return(ret);
	switch (hh->prim) {
		case (DL_DATA | INDICATION):
		case (DL_UNITDATA | INDICATION):
			break;
		case (DL_ESTABLISH | CONFIRM):
		case (DL_ESTABLISH | INDICATION):
		case (DL_RELEASE | INDICATION):
		case (DL_RELEASE | CONFIRM):
			l3_msg(l3, hh->prim, hh->dinfo, 0, NULL);
			dev_kfree_skb(skb);
			return(0);
			break;
		case (DL_DATA | CONFIRM):
		case (DL_UNITDATA | CONFIRM):
			dev_kfree_skb(skb);
			return(0);
			break;
                default:
                        printk(KERN_WARNING "%s: unknown pr=%04x\n",
                        	__FUNCTION__, hh->prim);
                        return(-EINVAL);
	}
	if (skb->len < 3) {
		l3_debug(l3, "dss1up frame too short(%d)", skb->len);
		dev_kfree_skb(skb);
		return(0);
	}
	if (skb->data[0] != PROTO_DIS_EURO) {
		if (l3->debug & L3_DEB_PROTERR) {
			l3_debug(l3, "dss1up%sunexpected discriminator %x message len %d",
				 (hh->prim == (DL_DATA | INDICATION)) ? " " : "(broadcast) ",
				 skb->data[0], skb->len);
		}
		dev_kfree_skb(skb);
		return(0);
	}
	ret = parseQ931(skb);
	if (ret < 0) {
		if (l3->debug & L3_DEB_PROTERR)
			l3_debug(l3, "dss1up: parse IE error %d", ret);
		printk(KERN_WARNING "dss1up: parse IE error %d\n", ret);
		dev_kfree_skb(skb);
		return(0);
	}
	qi = (Q931_info_t *)skb->data;
	ptr = skb->data;
	ptr += L3_EXTRA_SIZE;
	if (l3->debug & L3_DEB_STATE)
		l3_debug(l3, "dss1up cr %d", qi->cr);
	if (qi->crlen == 0) {	/* Dummy Callref */
		if (qi->type == MT_FACILITY) {
			l3dss1_facility(l3->dummy, hh->prim, skb);
			return(0);
		} else if (l3->debug & L3_DEB_WARN)
			l3_debug(l3, "dss1up dummy Callref (no facility msg or ie)");
		dev_kfree_skb(skb);
		return(0);
	} else if ((qi->cr & 0x7fff) == 0) {	/* Global CallRef */
		if (l3->debug & L3_DEB_STATE)
			l3_debug(l3, "dss1up Global CallRef");
		global_handler(l3, qi->type, skb);
		return(0);
	} else if (!(proc = getl3proc(l3, qi->cr))) {
		/* No transaction process exist, that means no call with
		 * this callreference is active
		 */
		if (qi->type == MT_SETUP) {
			/* Setup creates a new l3 process */
			if (qi->cr & 0x8000) {
				/* Setup with wrong CREF flag */
				if (l3->debug & L3_DEB_STATE)
					l3_debug(l3, "dss1up wrong CRef flag");
				dev_kfree_skb(skb);
				return(0);
			}
			if (!(proc = new_l3_process(l3, qi->cr, N303, MISDN_ID_ANY))) {
				/* May be to answer with RELEASE_COMPLETE and
				 * CAUSE 0x2f "Resource unavailable", but this
				 * need a new_l3_process too ... arghh
				 */
				dev_kfree_skb(skb);
				return(0);
			}
			/* register this ID in L4 */
			ret = mISDN_l3up(proc, CC_NEW_CR | INDICATION, NULL);
			if (ret) {
				printk(KERN_WARNING "dss1up: cannot register ID(%x)\n",
					proc->id);
				dev_kfree_skb(skb);
				release_l3_process(proc);
				return(0);
			}
		} else if (qi->type == MT_STATUS) {
			cause = 0;
			if (qi->cause) {
				if (ptr[qi->cause +1] >= 2)
					cause = ptr[qi->cause + 3] & 0x7f;
				else
					cause = ptr[qi->cause + 2] & 0x7f;	
			}
			callState = 0;
			if (qi->call_state) {
				callState = ptr[qi->cause + 2];
			}
			/* ETS 300-104 part 2.4.1
			 * if setup has not been made and a message type
			 * MT_STATUS is received with call state == 0,
			 * we must send nothing
			 */
			if (callState != 0) {
				/* ETS 300-104 part 2.4.2
				 * if setup has not been made and a message type
				 * MT_STATUS is received with call state != 0,
				 * we must send MT_RELEASE_COMPLETE cause 101
				 */
				if ((proc = new_l3_process(l3, qi->cr, N303, MISDN_ID_ANY))) {
					l3dss1_msg_without_setup(proc,
						CAUSE_NOTCOMPAT_STATE);
				}
			}
			dev_kfree_skb(skb);
			return(0);
		} else if (qi->type == MT_RELEASE_COMPLETE) {
			dev_kfree_skb(skb);
			return(0);
		} else {
			/* ETS 300-104 part 2
			 * if setup has not been made and a message type
			 * (except MT_SETUP and RELEASE_COMPLETE) is received,
			 * we must send MT_RELEASE_COMPLETE cause 81 */
			if ((proc = new_l3_process(l3, qi->cr, N303, MISDN_ID_ANY))) {
				l3dss1_msg_without_setup(proc,
					CAUSE_INVALID_CALLREF);
			}
			dev_kfree_skb(skb);
			return(0);
		}
	}
	if (l3dss1_check_messagetype_validity(proc, qi->type, skb)) {
		dev_kfree_skb(skb);
		return(0);
	}
	for (i = 0; i < DATASLLEN; i++)
		if ((qi->type == datastatelist[i].primitive) &&
		    ((1 << proc->state) & datastatelist[i].state))
			break;
	if (i == DATASLLEN) {
		if (l3->debug & L3_DEB_STATE) {
			l3_debug(l3, "dss1up%sstate %d mt %#x unhandled",
				(hh->prim == (DL_DATA | INDICATION)) ? " " : "(broadcast) ",
				proc->state, qi->type);
		}
		if ((MT_RELEASE_COMPLETE != qi->type) && (MT_RELEASE != qi->type)) {
			l3dss1_status_send(proc, CAUSE_NOTCOMPAT_STATE);
		}
		dev_kfree_skb(skb);
	} else {
		if (l3->debug & L3_DEB_STATE) {
			l3_debug(l3, "dss1up%sstate %d mt %x",
				(hh->prim == (DL_DATA | INDICATION)) ?
				" " : "(broadcast) ", proc->state, qi->type);
		}
		datastatelist[i].rout(proc, hh->prim, skb);
	}
	return(0);
}

static int
dss1_fromup(mISDNif_t *hif, struct sk_buff *skb)
{
	layer3_t	*l3;
	u_int		i;
	int		cr, ret = -EINVAL;
	l3_process_t	*proc;
	mISDN_head_t	*hh;

	if (!hif || !skb)
		return(ret);
	l3 = hif->fdata;
	hh = mISDN_HEAD_P(skb);
	if (debug)
		printk(KERN_DEBUG  "%s: prim(%x)\n", __FUNCTION__, hh->prim);
	if (!l3)
		return(ret);
	if ((DL_ESTABLISH | REQUEST) == hh->prim) {
		l3_msg(l3, hh->prim, 0, 0, NULL);
		dev_kfree_skb(skb);
		return(0);
	}
	proc = getl3proc4id(l3, hh->dinfo);
	if ((CC_NEW_CR | REQUEST) == hh->prim) {
		if (proc) {
			printk(KERN_WARNING "%s: proc(%x) allready exist\n",
				__FUNCTION__, hh->dinfo);
			ret = -EBUSY;
		} else {
			cr = newcallref(l3);
			cr |= 0x8000;
			ret = -ENOMEM;
			if ((proc = new_l3_process(l3, cr, N303, hh->dinfo))) {
				ret = 0;
				dev_kfree_skb(skb);
			}
		}
		return(ret);
	} 
	if (!proc) {
		printk(KERN_ERR "mISDN dss1 fromup without proc pr=%04x\n",
			hh->prim);
				return(-EINVAL);
	}
	for (i = 0; i < DOWNSLLEN; i++)
		if ((hh->prim == downstatelist[i].primitive) &&
		    ((1 << proc->state) & downstatelist[i].state))
			break;
	if (i == DOWNSLLEN) {
		if (l3->debug & L3_DEB_STATE) {
			l3_debug(l3, "dss1down state %d prim %#x unhandled",
				proc->state, hh->prim);
		}
		dev_kfree_skb(skb);
	} else {
		if (l3->debug & L3_DEB_STATE) {
			l3_debug(l3, "dss1down state %d prim %#x para len %d",
				proc->state, hh->prim, skb->len);
		}
		if (skb->len)
			downstatelist[i].rout(proc, hh->prim, skb);
		else {
			downstatelist[i].rout(proc, hh->prim, NULL);
			dev_kfree_skb(skb);
		}
	}
	return(0);
}

static int
dss1man(l3_process_t *proc, u_int pr, void *arg)
{
	u_int	i;
 
	if (!proc) {
		printk(KERN_ERR "mISDN dss1man without proc pr=%04x\n", pr);
		return(-EINVAL);
	}
	for (i = 0; i < MANSLLEN; i++)
		if ((pr == manstatelist[i].primitive) &&
			((1 << proc->state) & manstatelist[i].state))
			break;
		if (i == MANSLLEN) {
			if (proc->l3->debug & L3_DEB_STATE) {
				l3_debug(proc->l3, "cr %d dss1man state %d prim %#x unhandled",
					proc->callref & 0x7fff, proc->state, pr);
			}
		} else {
			if (proc->l3->debug & L3_DEB_STATE) {
				l3_debug(proc->l3, "cr %d dss1man state %d prim %#x",
					proc->callref & 0x7fff, proc->state, pr);
			}
			manstatelist[i].rout(proc, pr, arg);
	}
	return(0);
}

static void
release_udss1(layer3_t *l3)
{
	mISDNinstance_t  *inst = &l3->inst;

	printk(KERN_DEBUG "release_udss1 refcnt %d l3(%p) inst(%p)\n",
		u_dss1.refcnt, l3, inst);
	release_l3(l3);
	if (inst->up.peer) {
		inst->up.peer->obj->ctrl(inst->up.peer,
			MGR_DISCONNECT | REQUEST, &inst->up);
	}
	if (inst->down.peer) {
		inst->down.peer->obj->ctrl(inst->down.peer,
			MGR_DISCONNECT | REQUEST, &inst->down);
	}
	list_del(&l3->list);
	u_dss1.ctrl(inst, MGR_UNREGLAYER | REQUEST, NULL);
	if (l3->entity != MISDN_ENTITY_NONE)
		u_dss1.ctrl(inst, MGR_DELENTITY | REQUEST, (void *)l3->entity);
	kfree(l3);
}

static int
new_udss1(mISDNstack_t *st, mISDN_pid_t *pid)
{
	layer3_t	*nl3;
	int		err;

	if (!st || !pid)
		return(-EINVAL);
	if (!(nl3 = kmalloc(sizeof(layer3_t), GFP_ATOMIC))) {
		printk(KERN_ERR "kmalloc layer3 failed\n");
		return(-ENOMEM);
	}
	memset(nl3, 0, sizeof(layer3_t));
	memcpy(&nl3->inst.pid, pid, sizeof(mISDN_pid_t));
	nl3->debug = debug;
	mISDN_init_instance(&nl3->inst, &u_dss1, nl3);
	if (!mISDN_SetHandledPID(&u_dss1, &nl3->inst.pid)) {
		int_error();
		return(-ENOPROTOOPT);
	}
	if ((pid->protocol[3] & ~ISDN_PID_FEATURE_MASK) != ISDN_PID_L3_DSS1USER) {
		printk(KERN_ERR "udss1 create failed prt %x\n",
			pid->protocol[3]);
		kfree(nl3);
		return(-ENOPROTOOPT);
	}
	init_l3(nl3);
	if (pid->protocol[3] & ISDN_PID_L3_DF_PTP)
		test_and_set_bit(FLG_PTP, &nl3->Flag);
	if (pid->protocol[3] & ISDN_PID_L3_DF_EXTCID)
		test_and_set_bit(FLG_EXTCID, &nl3->Flag);
	if (pid->protocol[3] & ISDN_PID_L3_DF_CRLEN2)
		test_and_set_bit(FLG_CRLEN2, &nl3->Flag);
	if (!(nl3->global = kmalloc(sizeof(l3_process_t), GFP_ATOMIC))) {
		printk(KERN_ERR "mISDN can't get memory for dss1 global CR\n");
		release_l3(nl3);
		kfree(nl3);
		return(-ENOMEM);
	}
	nl3->global->state = 0;
	nl3->global->callref = 0;
	nl3->global->id = MISDN_ID_GLOBAL;
	INIT_LIST_HEAD(&nl3->global->list);
	nl3->global->n303 = N303;
	nl3->global->l3 = nl3;
	nl3->global->t303skb = NULL;
	L3InitTimer(nl3->global, &nl3->global->timer);
	if (!(nl3->dummy = kmalloc(sizeof(l3_process_t), GFP_ATOMIC))) {
		printk(KERN_ERR "mISDN can't get memory for dss1 dummy CR\n");
		release_l3(nl3);
		kfree(nl3);
		return(-ENOMEM);
	}
	nl3->dummy->state = 0;
	nl3->dummy->callref = -1;
	nl3->dummy->id = MISDN_ID_DUMMY;
	INIT_LIST_HEAD(&nl3->dummy->list);
	nl3->dummy->n303 = N303;
	nl3->dummy->l3 = nl3;
	nl3->dummy->t303skb = NULL;
	L3InitTimer(nl3->dummy, &nl3->dummy->timer);
	sprintf(nl3->inst.name, "DSS1 %d", st->id);
	nl3->p_mgr = dss1man;
	list_add_tail(&nl3->list, &u_dss1.ilist);
	err = u_dss1.ctrl(&nl3->inst, MGR_NEWENTITY | REQUEST, NULL);
	if (err) {
		printk(KERN_WARNING "mISDN %s: MGR_NEWENTITY REQUEST failed err(%x)\n",
			__FUNCTION__, err);
	}
	err = u_dss1.ctrl(st, MGR_REGLAYER | INDICATION, &nl3->inst);
	if (err) {
		release_l3(nl3);
		list_del(&nl3->list);
		kfree(nl3);
	} else {
		mISDN_stPara_t	stp;

	    	if (st->para.down_headerlen)
		    	nl3->down_headerlen = st->para.down_headerlen;
		stp.maxdatalen = 0;
		stp.up_headerlen = L3_EXTRA_SIZE;
		stp.down_headerlen = 0;
		u_dss1.ctrl(st, MGR_ADDSTPARA | REQUEST, &stp);
	}
	return(err);
}

static char MName[] = "UDSS1";

#ifdef MODULE
MODULE_AUTHOR("Karsten Keil");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
MODULE_PARM(debug, "1i");
#endif

static int
udss1_manager(void *data, u_int prim, void *arg) {
	mISDNinstance_t *inst = data;
	layer3_t *l3l;

	if (debug & 0x1000)
		printk(KERN_DEBUG "udss1_manager data:%p prim:%x arg:%p\n", data, prim, arg);
	if (!data)
		return(-EINVAL);
	list_for_each_entry(l3l, &u_dss1.ilist, list) {
		if (&l3l->inst == inst)
			break;
	}
	if (&l3l->list == &u_dss1.ilist)
		l3l = NULL;
	if (prim == (MGR_NEWLAYER | REQUEST))
		return(new_udss1(data, arg));
	if (!l3l) {
		if (debug & 0x1)
			printk(KERN_WARNING "udss1_manager prim(%x) no instance\n", prim);
		return(-EINVAL);
	}
	switch(prim) {
	    case MGR_NEWENTITY | CONFIRM:
		l3l->entity = (int)arg;
		break;
	    case MGR_ADDSTPARA | INDICATION:
	    	l3l->down_headerlen = ((mISDN_stPara_t *)arg)->down_headerlen;
	    case MGR_CLRSTPARA | INDICATION:
		break;
	    case MGR_CONNECT | REQUEST:
		return(mISDN_ConnectIF(inst, arg));
	    case MGR_SETIF | REQUEST:
	    case MGR_SETIF | INDICATION:
		return(mISDN_SetIF(inst, arg, prim, dss1_fromup, dss1_fromdown, l3l));
	    case MGR_DISCONNECT | REQUEST:
	    case MGR_DISCONNECT | INDICATION:
		return(mISDN_DisConnectIF(inst, arg));
	    case MGR_RELEASE | INDICATION:
	    case MGR_UNREGLAYER | REQUEST:
	    	if (debug & 0x1000)
			printk(KERN_DEBUG "release_udss1 id %x\n", l3l->inst.st->id);
	    	release_udss1(l3l);
	    	break;
	    PRIM_NOT_HANDLED(MGR_CTRLREADY | INDICATION);
	    default:
	    	if (debug & 0x1)
			printk(KERN_WARNING "udss1 prim %x not handled\n", prim);
		return(-EINVAL);
	}
	return(0);
}

int UDSS1Init(void)
{
	int err;
	char tmp[32];

	strcpy(tmp, dss1_revision);
	printk(KERN_INFO "mISDN: DSS1 Rev. %s\n", mISDN_getrev(tmp));
#ifdef MODULE
	u_dss1.owner = THIS_MODULE;
#endif
	INIT_LIST_HEAD(&u_dss1.ilist);
	u_dss1.name = MName;
	u_dss1.DPROTO.protocol[3] = ISDN_PID_L3_DSS1USER |
		ISDN_PID_L3_DF_PTP |
		ISDN_PID_L3_DF_EXTCID |
		ISDN_PID_L3_DF_CRLEN2;
	u_dss1.own_ctrl = udss1_manager;
	mISDNl3New();
	if ((err = mISDN_register(&u_dss1))) {
		printk(KERN_ERR "Can't register %s error(%d)\n", MName, err);
		mISDNl3Free();
	}
	return(err);
}

#ifdef MODULE
void UDSS1_cleanup(void)
{
	int err;
	layer3_t	*l3, *next;

	if ((err = mISDN_unregister(&u_dss1))) {
		printk(KERN_ERR "Can't unregister User DSS1 error(%d)\n", err);
	}
	if (!list_empty(&u_dss1.ilist)) {
		printk(KERN_WARNING "mISDNl3 u_dss1 list not empty\n");
		list_for_each_entry_safe(l3, next, &u_dss1.ilist, list)
			release_udss1(l3);
	}
	mISDNl3Free();
}

module_init(UDSS1Init);
module_exit(UDSS1_cleanup);
#endif

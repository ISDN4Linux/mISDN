/* $Id: l3_udss1.c,v 0.2 2001/02/11 22:57:24 kkeil Exp $
 *
 * EURO/DSS1 D-channel protocol
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 *		This file is (c) under GNU PUBLIC LICENSE
 *		For changes and modifications please read
 *		../../../Documentation/isdn/HiSax.cert
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 */

#include <linux/module.h>

#include "hisax.h"
#include "hisaxl3.h"
#include "dss1.h"

static layer3_t *dss1list = NULL;
static int debug = 0;
static hisaxobject_t u_dss1;


extern char *HiSax_getrev(const char *revision);
const char *dss1_revision = "$Revision: 0.2 $";

#define EXT_BEARER_CAPS 1

#define	MsgHead(ptr, cref, mty) \
	*ptr++ = 0x8; \
	if (cref == -1) { \
		*ptr++ = 0x0; \
	} else { \
		*ptr++ = 0x1; \
		*ptr++ = cref^0x80; \
	} \
	*ptr++ = mty

static void
l3dss1_message(l3_process_t *pc, u_char mt)
{
	struct sk_buff *skb;
	u_char *p;

	if (!(skb = l3_alloc_skb(4)))
		return;
	p = skb_put(skb, 4);
	MsgHead(p, pc->callref, mt);
	l3_msg(pc->l3, DL_DATA | REQUEST, skb);
}

static void
l3dss1_message_cause(l3_process_t *pc, u_char mt, u_char cause)
{
	struct sk_buff *skb;
	u_char tmp[16];
	u_char *p = tmp;
	int l;

	MsgHead(p, pc->callref, mt);
	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = 0x80 | CAUSE_LOC_USER;
	*p++ = cause | 0x80;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	l3_msg(pc->l3, DL_DATA | REQUEST, skb);
}

static void
l3dss1_status_send(l3_process_t *pc, u_char loc, u_char cause)
{
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	struct sk_buff *skb;

	MsgHead(p, pc->callref, MT_STATUS);

	*p++ = IE_CAUSE;
	*p++ = 0x2;
	*p++ = loc | 0x80;
	*p++ = cause | 0x80;

	*p++ = IE_CALL_STATE;
	*p++ = 0x1;
	*p++ = pc->state & 0x3f;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	l3_msg(pc->l3, DL_DATA | REQUEST, skb);
}

static void
l3dss1_msg_without_setup(l3_process_t *pc, u_char loc, u_char cause)
{
	/* This routine is called if here was no SETUP made (checks in dss1up and in
	 * l3dss1_setup) and a RELEASE_COMPLETE have to be sent with an error code
	 * MT_STATUS_ENQUIRE in the NULL state is handled too
	 */
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	struct sk_buff *skb;

	switch (cause) {
		case 81:	/* invalid callreference */
		case 88:	/* incomp destination */
		case 96:	/* mandory IE missing */
		case 100:       /* invalid IE contents */
		case 101:	/* incompatible Callstate */
			MsgHead(p, pc->callref, MT_RELEASE_COMPLETE);
			*p++ = IE_CAUSE;
			*p++ = 0x2;
			*p++ = loc | 0x80;
			*p++ = cause | 0x80;
			break;
		default:
			printk(KERN_ERR "HiSax l3dss1_msg_without_setup wrong cause %d\n",
				cause);
			return;
	}
	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	l3_msg(pc->l3, DL_DATA | REQUEST, skb);
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
static int ie_RELEASE_COMPLETE[] = {IE_CAUSE | IE_MANDATORY_1, IE_DISPLAY, IE_SIGNAL, IE_USER_USER, -1};
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
static int ie_SUSPEND_ACKNOWLEDGE[] = {IE_DISPLAY, IE_FACILITY, -1};
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
	int *cl = checklist;
	u_char mt;
	u_char *p, ie;
	int l, newpos, oldpos;
	int err_seq = 0, err_len = 0, err_compr = 0, err_ureg = 0;
	u_char codeset = 0;
	u_char old_codeset = 0;
	u_char codelock = 1;
	
	p = skb->data;
	/* skip cr */
	p++;
	l = (*p++) & 0xf;
	p += l;
	mt = *p++;
	oldpos = 0;
	while ((p - skb->data) < skb->len) {
		if ((*p & 0xf0) == 0x90) { /* shift codeset */
			old_codeset = codeset;
			codeset = *p & 7;
			if (*p & 0x08)
				codelock = 0;
			else
				codelock = 1;
			if (pc->debug & L3_DEB_CHECK)
				l3_debug(pc->l3, "check IE shift%scodeset %d->%d",
					codelock ? " locking ": " ", old_codeset, codeset);
			p++;
			continue;
		}
		if (!codeset) { /* only codeset 0 */
			if ((newpos = ie_in_set(pc, *p, cl))) {
				if (newpos > 0) {
					if (newpos < oldpos)
						err_seq++;
					else
						oldpos = newpos;
				}
			} else {
				if (ie_in_set(pc, *p, comp_required))
					err_compr++;
				else
					err_ureg++;
			}
		}
		ie = *p++;
		if (ie & 0x80) {
			l = 1;
		} else {
			l = *p++;
			p += l;
			l += 2;
		}
		if (!codeset && (l > getmax_ie_len(ie)))
			err_len++;
		if (!codelock) {
			if (pc->debug & L3_DEB_CHECK)
				l3_debug(pc->l3, "check IE shift back codeset %d->%d",
					codeset, old_codeset);
			codeset = old_codeset;
			codelock = 1;
		}
	}
	if (err_compr | err_ureg | err_len | err_seq) {
		if (pc->debug & L3_DEB_CHECK)
			l3_debug(pc->l3, "check IE MT(%x) %d/%d/%d/%d",
				mt, err_compr, err_ureg, err_len, err_seq);
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
			if (pc->debug & L3_DEB_CHECK)
				l3_debug(pc->l3, "l3dss1_check_messagetype_validity mt(%x) OK", mt);
			break;
		case MT_RESUME: /* RESUME only in user->net */
		case MT_SUSPEND: /* SUSPEND only in user->net */
		default:
			if (pc->debug & (L3_DEB_CHECK | L3_DEB_WARN))
				l3_debug(pc->l3, "l3dss1_check_messagetype_validity mt(%x) fail", mt);
			l3dss1_status_send(pc, CAUSE_LOC_USER, CAUSE_MT_NOTIMPLEMENTED);
			return(1);
	}
	return(0);
}

static void
l3dss1_std_ie_err(l3_process_t *pc, int ret) {

	if (pc->debug & L3_DEB_CHECK)
		l3_debug(pc->l3, "check_infoelements ret %d", ret);
	switch(ret) {
		case 0: 
			break;
		case ERR_IE_COMPREHENSION:
			l3dss1_status_send(pc, CAUSE_LOC_USER, CAUSE_MANDATORY_IE_MISS);
			break;
		case ERR_IE_UNRECOGNIZED:
			l3dss1_status_send(pc, CAUSE_LOC_USER, CAUSE_IE_NOTIMPLEMENTED);
			break;
		case ERR_IE_LENGTH:
			l3dss1_status_send(pc, CAUSE_LOC_USER, CAUSE_INVALID_CONTENTS);
			break;
		case ERR_IE_SEQUENCE:
		default:
			break;
	}
}

static int
l3dss1_get_channel_id(l3_process_t *pc, struct sk_buff *skb, channel_t *chid) {
	u_char *p, *sp;

	if ((p = findie(skb->data, skb->len, IE_CHANNEL_ID, 0))) {
		p++;
		sp = p;
		if (*p != 1) { /* len for BRI = 1 */
			if (pc->debug & L3_DEB_WARN)
				l3_debug(pc->l3, "wrong chid len %d", *p);
			return (-2);
		}
		p++;
		if (*p & 0x60) { /* only base rate interface */
			if (pc->debug & L3_DEB_WARN)
				l3_debug(pc->l3, "wrong chid %x", *p);
			return (-3);
		}
		memcpy(chid, sp, *sp);
		return(chid->chan & 0x3);
	} else
		return(-1);
}

static int
l3dss1_get_cause(l3_process_t *pc, struct sk_buff *skb) {
	u_char l, i=0;
	u_char *p;

	memset(&pc->para.cause, 0, sizeof(cause_t));
	pc->para.cause.val = CAUSE_NORMALUNSPECIFIED;
	if ((p = findie(skb->data, skb->len, IE_CAUSE, 0))) {
		p++;
		l = *p++;
		if (l>30)
			return(1);
		pc->para.cause.len = l;
		if (l) {
			pc->para.cause.loc = *p++;
			l--;
		} else {
			return(2);
		}
		if (l && !(pc->para.cause.loc & 0x80)) {
			l--;
			pc->para.cause.rec = *p++; /* skip recommendation */
		}
		if (l) {
			pc->para.cause.val = *p++;
			l--;
			if (!(pc->para.cause.val & 0x80))
				return(3);
		} else
			return(4);
		while (l && (i<28)) {
			pc->para.cause.diag[i++] = *p++;
			l--;
		}
	} else
		return(-1);
	return(0);
}

#if 0
static void
l3dss1_msg_with_uus(l3_process_t *pc, u_char cmd)
{
	struct sk_buff *skb;
	u_char tmp[16+40];
	u_char *p = tmp;
	int l;

	MsgHead(p, pc->callref, cmd);

        if (pc->prot.dss1.uus1_data[0])
	 { *p++ = IE_USER_USER; /* UUS info element */
           *p++ = strlen(pc->prot.dss1.uus1_data) + 1;
           *p++ = 0x04; /* IA5 chars */
           strcpy(p,pc->prot.dss1.uus1_data);
           p += strlen(pc->prot.dss1.uus1_data);
           pc->prot.dss1.uus1_data[0] = '\0';   
         } 

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	l3_msg(pc->l3, DL_DATA | REQUEST, skb);
}
#endif

static void
l3dss1_release_req(l3_process_t *pc, u_char pr, void *arg)
{
	StopAllL3Timer(pc);
	newl3state(pc, 19);
//	if (!pc->prot.dss1.uus1_data[0]) 
		l3dss1_message(pc, MT_RELEASE);
//	else
//		l3dss1_msg_with_uus(pc, MT_RELEASE);
	L3AddTimer(&pc->timer, T308, CC_T308_1);
}

static void
l3dss1_release_cmpl(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;
	u_char cause;

	if ((ret = l3dss1_get_cause(pc, skb))>0) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "RELCMPL get_cause ret(%d)",ret);
	} else if (ret < 0)
		cause = NO_CAUSE;
	StopAllL3Timer(pc);
	newl3state(pc, 0);
	hisax_l3up(pc, CC_RELEASE | CONFIRM, pc);
	release_l3_process(pc);
}

static void
l3dss1_setup_req(l3_process_t *pc, u_char pr, void *arg)
{
	setup_t *setup = arg;
	struct sk_buff *skb;
	u_char tmp[128];
	u_char *p = tmp;
	u_char *sp;
	int l;

	MsgHead(p, pc->callref, MT_SETUP);

	if (setup->sending_cmpl)
		*p++ = 0xa1;		/* complete indicator */
	*p++ = IE_BEARER;
	sp = &setup->bc[0];
	l = *sp;
	*p++ = *sp++;
	while(l)
		*p++ = *sp++;
	sp = &setup->channel.len;
	if (*sp) {
		*p++ = IE_CHANNEL_ID;
		l = *sp;
		*p++ = *sp++;
		while(l)
			*p++ = *sp++;
	}
	sp = &setup->calling_nr[0];
	if (*sp) {
		*p++ = IE_CALLING_PN;
		l = *sp;
		*p++ = *sp++;
		while(l)
			*p++ = *sp++;
	}
	sp = &setup->calling_sub[0];
	if (*sp) {
		*p++ = IE_CALLING_SUB;
		l = *sp;
		*p++ = *sp++;
		while(l)
			*p++ = *sp++;
	}
	sp = &setup->called_nr[0];
	if (*sp) {
		*p++ = IE_CALLED_PN;
		l = *sp;
		*p++ = *sp++;
		while(l)
			*p++ = *sp++;
	}
	sp = &setup->called_sub[0];
	if (*sp) {
		*p++ = IE_CALLED_SUB;
		l = *sp;
		*p++ = *sp++;
		while(l)
			*p++ = *sp++;
	}
	sp = &setup->llc[0];
	if (*sp) {
		*p++ = IE_LLC;
		l = *sp;
		*p++ = *sp++;
		while(l)
			*p++ = *sp++;
	}
	sp = &setup->hlc[0];
	if (*sp) {
		*p++ = IE_HLC;
		l = *sp;
		*p++ = *sp++;
		while(l)
			*p++ = *sp++;
	}
	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	L3DelTimer(&pc->timer);
	L3AddTimer(&pc->timer, T303, CC_T303);
	newl3state(pc, 1);
	l3_msg(pc->l3, DL_DATA | REQUEST, skb);
}

static void
l3dss1_call_proc(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int id, ret;
	u_char cause;

	if ((id = l3dss1_get_channel_id(pc, skb, &pc->para.channel)) >= 0) {
		if ((0 == id) || (3 == id)) {
			if (pc->debug & L3_DEB_WARN)
				l3_debug(pc->l3, "setup answer with wrong chid %x", id);
			l3dss1_status_send(pc, CAUSE_LOC_USER,  CAUSE_INVALID_CONTENTS);
			return;
		}
	} else if (1 == pc->state) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "setup answer wrong chid (ret %d)", id);
		if (id == -1)
			cause = CAUSE_MANDATORY_IE_MISS;
		else
			cause = CAUSE_INVALID_CONTENTS;
		l3dss1_status_send(pc, CAUSE_LOC_USER, cause);
		return;
	}
	/* Now we are on none mandatory IEs */
	ret = check_infoelements(pc, skb, ie_CALL_PROCEEDING);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		return;
	}
	L3DelTimer(&pc->timer);
	newl3state(pc, 3);
	L3AddTimer(&pc->timer, T310, CC_T310);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3dss1_std_ie_err(pc, ret);
	hisax_l3up(pc, CC_PROCEEDING | INDICATION, pc);
}

static void
l3dss1_setup_ack(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int id, ret;
	u_char cause;

	if ((id = l3dss1_get_channel_id(pc, skb, &pc->para.channel)) >= 0) {
		if ((0 == id) || (3 == id)) {
			if (pc->debug & L3_DEB_WARN)
				l3_debug(pc->l3, "setup answer with wrong chid %x", id);
			l3dss1_status_send(pc, CAUSE_LOC_USER, CAUSE_INVALID_CONTENTS);
			return;
		}
	} else {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "setup answer wrong chid (ret %d)", id);
		if (id == -1)
			cause = CAUSE_MANDATORY_IE_MISS;
		else
			cause = CAUSE_INVALID_CONTENTS;
		l3dss1_status_send(pc, CAUSE_LOC_USER, cause);
		return;
	}
	/* Now we are on none mandatory IEs */
	ret = check_infoelements(pc, skb, ie_SETUP_ACKNOWLEDGE);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		return;
	}
	L3DelTimer(&pc->timer);
	newl3state(pc, 2);
	L3AddTimer(&pc->timer, T304, CC_T304);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3dss1_std_ie_err(pc, ret);
	hisax_l3up(pc, CC_MORE_INFO | INDICATION, pc);
}

static void
l3dss1_disconnect(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;
	u_char cause = 0;

	StopAllL3Timer(pc);
	if ((ret = l3dss1_get_cause(pc, skb))) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "DISC get_cause ret(%d)", ret);
		if (ret < 0)
			cause = CAUSE_MANDATORY_IE_MISS;
		else if (ret > 0)
			cause = CAUSE_INVALID_CONTENTS;
	} 
#if 0
	{
		u_char *p;	
		if ((p = findie(skb->data, skb->len, IE_FACILITY, 0)))
			l3dss1_parse_facility(pc->l3, pc, pc->callref, p);
	}
#endif
	ret = check_infoelements(pc, skb, ie_DISCONNECT);
	if (ERR_IE_COMPREHENSION == ret)
		cause = CAUSE_MANDATORY_IE_MISS;
	else if ((!cause) && (ERR_IE_UNRECOGNIZED == ret))
		cause = CAUSE_IE_NOTIMPLEMENTED;
	ret = pc->state;
	newl3state(pc, 12);
	if (cause)
		newl3state(pc, 19);
       	if (11 != ret)
		hisax_l3up(pc, CC_DISCONNECT | INDICATION, pc);
       	else if (!cause)
		   l3dss1_release_req(pc, pr, NULL);
	if (cause) {
		l3dss1_message_cause(pc, MT_RELEASE, cause);
		L3AddTimer(&pc->timer, T308, CC_T308_1);
	}
}

static void
l3dss1_connect(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;

	ret = check_infoelements(pc, skb, ie_CONNECT);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		return;
	}
	L3DelTimer(&pc->timer);	/* T310 */
	newl3state(pc, 10);
	/* here should inserted COLP handling KKe */
	if (ret)
		l3dss1_std_ie_err(pc, ret);
	hisax_l3up(pc, CC_SETUP | CONFIRM, pc);
}

static void
l3dss1_alerting(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;

	ret = check_infoelements(pc, skb, ie_ALERTING);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		return;
	}
	L3DelTimer(&pc->timer);	/* T304 */
	newl3state(pc, 4);
	if (ret)
		l3dss1_std_ie_err(pc, ret);
	hisax_l3up(pc, CC_ALERTING | INDICATION, pc);
}

static void
l3dss1_setup(l3_process_t *pc, u_char pr, void *arg)
{
	u_char *p, cause;
	int bcfound = 0;
	struct sk_buff *skb = arg;
	int id;
	int err = 0;

	/*
	 * Bearer Capabilities
	 */
	/* only the first occurence 'll be detected ! */
	if ((p = findie(skb->data, skb->len, IE_BEARER, 0))) {
		if ((p[1] < 2) || (p[1] > 11))
			err = 1;
		else {
			switch (p[2] & 0x7f) {
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
			switch (p[3] & 0x7f) {
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
			if (pc->debug & L3_DEB_WARN)
				l3_debug(pc->l3, "setup with wrong bearer(l=%d:%x,%x)",
					p[1], p[2], p[3]);
			l3dss1_msg_without_setup(pc, CAUSE_LOC_USER, CAUSE_INVALID_CONTENTS);
			return;
		} else {
			memcpy(&pc->para.setup.bc, &p[1], p[1]);
		}
	} else {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "setup without bearer capabilities");
		/* ETS 300-104 1.3.3 */
		l3dss1_msg_without_setup(pc, CAUSE_LOC_USER, CAUSE_MANDATORY_IE_MISS);
		return;
	}
	/*
	 * Channel Identification
	 */
	if ((id = l3dss1_get_channel_id(pc, skb, &pc->para.setup.channel)) >= 0) {
		if (id) {
			if ((3 == id) && (0x10 == (pc->para.setup.bc[3] & 0x7f))) {
				if (pc->debug & L3_DEB_WARN)
					l3_debug(pc->l3, "setup with wrong chid %x",
						id);
				l3dss1_msg_without_setup(pc, CAUSE_LOC_USER,
					CAUSE_INVALID_CONTENTS);
				return;
			}
			bcfound++;
		} else {
			if (pc->debug & L3_DEB_WARN)
				l3_debug(pc->l3, "setup without bchannel, call waiting");
			bcfound++;
		} 
	} else {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "setup with wrong chid ret %d", id);
		if (id == -1)
			cause = CAUSE_MANDATORY_IE_MISS;
		else
			cause = CAUSE_INVALID_CONTENTS;
		l3dss1_msg_without_setup(pc, CAUSE_LOC_USER, cause);
		return;
	}
	/* Now we are on none mandatory IEs */
	err = check_infoelements(pc, skb, ie_SETUP);
	if (ERR_IE_COMPREHENSION == err) {
		l3dss1_msg_without_setup(pc, CAUSE_LOC_USER, CAUSE_MANDATORY_IE_MISS);
		return;
	}
	if ((p = findie(skb->data, skb->len, IE_COMPLETE, 0)))
		pc->para.setup.sending_cmpl = *p;
	else
		pc->para.setup.sending_cmpl = 0;

	if ((p = findie(skb->data, skb->len, IE_CALLED_PN, 0))) {
		p++;
		memcpy(&pc->para.setup.called_nr[0], p, *p);
	} else
		pc->para.setup.called_nr[0] = 0;

	if ((p = findie(skb->data, skb->len, IE_CALLED_SUB, 0))) {
		p++;
		memcpy(&pc->para.setup.called_sub[0], p, *p);
	} else
		pc->para.setup.called_sub[0] = 0;
	if ((p = findie(skb->data, skb->len, IE_CALLING_PN, 0))) {
		p++;
		memcpy(&pc->para.setup.calling_nr[0], p, *p);
	} else
		pc->para.setup.calling_nr[0] = 0;
	if ((p = findie(skb->data, skb->len, IE_CALLING_SUB, 0))) {
		p++;
		memcpy(&pc->para.setup.calling_sub[0], p, *p);
	} else
		pc->para.setup.calling_sub[0] = 0;
	if ((p = findie(skb->data, skb->len, IE_LLC, 0))) {
		p++;
		memcpy(&pc->para.setup.llc[0], p, *p);
	} else
		pc->para.setup.llc[0] = 0;
	if ((p = findie(skb->data, skb->len, IE_HLC, 0))) {
		p++;
		memcpy(&pc->para.setup.hlc[0], p, *p);
	} else
		pc->para.setup.hlc[0] = 0;
	newl3state(pc, 6);
	if (err) /* STATUS for none mandatory IE errors after actions are taken */
		l3dss1_std_ie_err(pc, err);
	hisax_l3up(pc, CC_SETUP | INDICATION, pc);
}

static void
l3dss1_reset(l3_process_t *pc, u_char pr, void *arg)
{
	release_l3_process(pc);
}

static void
l3dss1_disconnect_req(l3_process_t *pc, u_char pr, void *arg)
{
	cause_t *cause = arg;
	struct sk_buff *skb;
	u_char tmp[16+40];
	u_char *cp, *p = tmp;
	int l;
	cause_t tmpcause;

	StopAllL3Timer(pc);
	MsgHead(p, pc->callref, MT_DISCONNECT);
	if (!cause) {
		cause = &tmpcause;
		cause->len = 2;
		cause->loc = 0x80 | CAUSE_LOC_USER;
		cause->val = 0x80 | CAUSE_NORMALUNSPECIFIED;
	}
	*p++ = IE_CAUSE;
	cp = (u_char *)cause;
	l = *cp;
	*p++ = *cp++;
	while(l--)
		*p++ = *cp++;
#if 0
        if (pc->prot.dss1.uus1_data[0])
	 { *p++ = IE_USER_USER; /* UUS info element */
           *p++ = strlen(pc->prot.dss1.uus1_data) + 1;
           *p++ = 0x04; /* IA5 chars */
           strcpy(p,pc->prot.dss1.uus1_data);
           p += strlen(pc->prot.dss1.uus1_data);
           pc->prot.dss1.uus1_data[0] = '\0';   
         } 
#endif
	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	newl3state(pc, 11);
	l3_msg(pc->l3, DL_DATA | REQUEST, skb);
	L3AddTimer(&pc->timer, T305, CC_T305);
}

static void
l3dss1_setup_rsp(l3_process_t *pc, u_char pr,
		 void *arg)
{
#if 0
	if (!pc->para.bchannel) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "D-chan connect for waiting call");
		l3dss1_disconnect_req(pc, pr, NULL);
		return;
	}
#endif
	newl3state(pc, 8);
	l3dss1_message(pc, MT_CONNECT);
	L3DelTimer(&pc->timer);
	L3AddTimer(&pc->timer, T313, CC_T313);
}

static void
l3dss1_connect_ack(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;

	ret = check_infoelements(pc, skb, ie_CONNECT_ACKNOWLEDGE);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		return;
	}
	newl3state(pc, 10);
	L3DelTimer(&pc->timer);
	if (ret)
		l3dss1_std_ie_err(pc, ret);
	hisax_l3up(pc, CC_SETUP_COMPL | INDICATION, pc);
}

static void
l3dss1_reject_req(l3_process_t *pc, u_char pr, void *arg)
{
	cause_t *cause = arg;
	struct sk_buff *skb;
	u_char tmp[16];
	u_char *cp,*p = tmp;
	int l;
	cause_t tmpcause;

	MsgHead(p, pc->callref, MT_RELEASE_COMPLETE);
	if (!cause) {
		cause = &tmpcause;
		cause->len = 2;
		cause->loc = 0x80 | CAUSE_LOC_USER;
		cause->val = 0x80 | CAUSE_CALL_REJECTED;
	}

	*p++ = IE_CAUSE;
	cp = (u_char *)cause;
	l = *cp;
	*p++ = *cp++;
	while(l--)
		*p++ = *cp++;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	l3_msg(pc->l3, DL_DATA | REQUEST, skb);
	hisax_l3up(pc, CC_RELEASE | INDICATION, pc);
	newl3state(pc, 0);
	release_l3_process(pc);
}

static void
l3dss1_release(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret, cause=0;

	StopAllL3Timer(pc);
	if ((ret = l3dss1_get_cause(pc, skb))>0) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "REL get_cause ret(%d)", ret);
	} else if (ret<0)
		pc->para.cause.val = NO_CAUSE;
#if 0
	{
		u_char *p;
		if ((p = findie(skb->data, skb->len, IE_FACILITY, 0)))
			l3dss1_parse_facility(pc->l3, pc, pc->callref, p);
	}
#endif
	if ((ret<0) && (pc->state != 11))
		cause = CAUSE_MANDATORY_IE_MISS;
	else if (ret>0)
		cause = CAUSE_INVALID_CONTENTS;
	ret = check_infoelements(pc, skb, ie_RELEASE);
	if (ERR_IE_COMPREHENSION == ret)
		cause = CAUSE_MANDATORY_IE_MISS;
	else if ((ERR_IE_UNRECOGNIZED == ret) && (!cause))
		cause = CAUSE_IE_NOTIMPLEMENTED;
	if (cause)
		l3dss1_message_cause(pc, MT_RELEASE_COMPLETE, cause);
	else
		l3dss1_message(pc, MT_RELEASE_COMPLETE);
	hisax_l3up(pc, CC_RELEASE | INDICATION, pc);
	newl3state(pc, 0);
	release_l3_process(pc);
}

static void
l3dss1_alert_req(l3_process_t *pc, u_char pr, void *arg)
{
	newl3state(pc, 7);
//	if (!pc->prot.dss1.uus1_data[0]) 
		l3dss1_message(pc, MT_ALERTING);
//	else
//		l3dss1_msg_with_uus(pc, MT_ALERTING); 
}

static void
l3dss1_proceed_req(l3_process_t *pc, u_char pr, void *arg)
{
	newl3state(pc, 9);
	l3dss1_message(pc, MT_CALL_PROCEEDING);
	hisax_l3up(pc, CC_PROCEED_SEND | INDICATION, pc); 
}

static void
l3dss1_setup_ack_req(l3_process_t *pc, u_char pr, void *arg)
{
	newl3state(pc, 25);
	L3DelTimer(&pc->timer);
	L3AddTimer(&pc->timer, T302, CC_T302);
	l3dss1_message(pc, MT_SETUP_ACKNOWLEDGE);
}

#if 0
/********************************************/
/* deliver a incoming display message to HL */
/********************************************/
static void
l3dss1_deliver_display(l3_process_t *pc, int pr, u_char *infp)
{       u_char len;
        isdn_ctrl ic; 
	struct IsdnCardState *cs;
        char *p; 

        if (*infp++ != IE_DISPLAY) return;
        if ((len = *infp++) > 80) return; /* total length <= 82 */
	if (!pc->chan) return;

	p = ic.parm.display; 
        while (len--)
	  *p++ = *infp++;
	*p = '\0';
	ic.command = ISDN_STAT_DISPLAY;
	cs = pc->l3->l1.hardware;
	ic.driver = cs->myid;
	ic.arg = pc->chan->chan; 
	cs->iif.statcallb(&ic);
} /* l3dss1_deliver_display */
#endif

static void
l3dss1_progress(l3_process_t *pc, u_char pr, void *arg) {
	struct sk_buff *skb = arg;
	int err = 0;
	u_char *p, cause = CAUSE_INVALID_CONTENTS;

	if ((p = findie(skb->data, skb->len, IE_PROGRESS, 0))) {
		if (p[1] != 2) {
			err = 1;
		} else if (!(p[2] & 0x70)) {
			switch (p[2]) {
				case 0x80:
				case 0x81:
				case 0x82:
				case 0x84:
				case 0x85:
				case 0x87:
				case 0x8a:
					switch (p[3]) {
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
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "progress error %d", err);
		l3dss1_status_send(pc, CAUSE_LOC_USER, cause);
		return;
	}
	/* Now we are on none mandatory IEs */
	err = check_infoelements(pc, skb, ie_PROGRESS);
	if (err)
		l3dss1_std_ie_err(pc, err);
	if (ERR_IE_COMPREHENSION != err)
		hisax_l3up(pc, CC_PROGRESS | INDICATION, pc);
}

static void
l3dss1_notify(l3_process_t *pc, u_char pr, void *arg) {
	struct sk_buff *skb = arg;
	int err = 0;
	u_char *p, cause = CAUSE_INVALID_CONTENTS;

	if ((p = findie(skb->data, skb->len, IE_NOTIFY, 0))) {
		if (p[1] != 1) {
			err = 1;
		} else {
			switch (p[2]) {
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
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "notify error %d", err);
		l3dss1_status_send(pc, CAUSE_LOC_USER, cause);
		return;
	}
	/* Now we are on none mandatory IEs */
	err = check_infoelements(pc, skb, ie_NOTIFY);
	if (err)
		l3dss1_std_ie_err(pc, err);
	if (ERR_IE_COMPREHENSION != err)
		hisax_l3up(pc, CC_NOTIFY | INDICATION, pc);
}

static void
l3dss1_status_enq(l3_process_t *pc, u_char pr, void *arg) {
	int ret;
	struct sk_buff *skb = arg;

	ret = check_infoelements(pc, skb, ie_STATUS_ENQUIRY);
	l3dss1_std_ie_err(pc, ret);
        l3dss1_status_send(pc, CAUSE_LOC_USER, CAUSE_STATUS_RESPONSE);
}

static void
l3dss1_information(l3_process_t *pc, u_char pr, void *arg) {
	int ret;
	struct sk_buff *skb = arg;

	ret = check_infoelements(pc, skb, ie_INFORMATION);
	if (ret)
		l3dss1_std_ie_err(pc, ret);
#if 0
	u_char *p;
	char tmp[32];
	if (pc->state == 25) { /* overlap receiving */
		L3DelTimer(&pc->timer);
		if ((p = findie(skb->data, skb->len, IE_CALLED_PN, 0))) {
			iecpy(tmp, p, 1);
			strcat(pc->para.setup.eazmsn, tmp);
			hisax_l3up(pc, CC_MORE_INFO | INDICATION, pc);
		}
		L3AddTimer(&pc->timer, T302, CC_T302);
	}
#endif
}

#if 0
/******************************/
/* handle deflection requests */
/******************************/
static void l3dss1_redir_req(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff *skb;
	u_char tmp[128];
	u_char *p = tmp;
        u_char *subp;
        u_char len_phone = 0;
        u_char len_sub = 0;
	int l; 


        strcpy(pc->prot.dss1.uus1_data,pc->chan->setup.eazmsn); /* copy uus element if available */
        if (!pc->chan->setup.phone[0])
          { pc->para.cause = -1;
            l3dss1_disconnect_req(pc,pr,arg); /* disconnect immediately */
            return;
          } /* only uus */
 
        if (pc->prot.dss1.invoke_id) 
          free_invoke_id(pc->l3,pc->prot.dss1.invoke_id);
 
        if (!(pc->prot.dss1.invoke_id = new_invoke_id(pc->l3))) 
          return;

        MsgHead(p, pc->callref, MT_FACILITY);

        for (subp = pc->chan->setup.phone; (*subp) && (*subp != '.'); subp++) len_phone++; /* len of phone number */
        if (*subp++ == '.') len_sub = strlen(subp) + 2; /* length including info subadress element */ 

	*p++ = 0x1c;   /* Facility info element */
        *p++ = len_phone + len_sub + 2 + 2 + 8 + 3 + 3; /* length of element */
        *p++ = 0x91;  /* remote operations protocol */
        *p++ = 0xa1;  /* invoke component */
	  
        *p++ = len_phone + len_sub + 2 + 2 + 8 + 3; /* length of data */
        *p++ = 0x02;  /* invoke id tag, integer */
	*p++ = 0x01;  /* length */
        *p++ = pc->prot.dss1.invoke_id;  /* invoke id */ 
        *p++ = 0x02;  /* operation value tag, integer */
	*p++ = 0x01;  /* length */
        *p++ = 0x0D;  /* Call Deflect */
	  
        *p++ = 0x30;  /* sequence phone number */
        *p++ = len_phone + 2 + 2 + 3 + len_sub; /* length */
	  
        *p++ = 0x30;  /* Deflected to UserNumber */
        *p++ = len_phone+2+len_sub; /* length */
        *p++ = 0x80; /* NumberDigits */
	*p++ = len_phone; /* length */
        for (l = 0; l < len_phone; l++)
	 *p++ = pc->chan->setup.phone[l];

        if (len_sub)
	  { *p++ = 0x04; /* called party subadress */
            *p++ = len_sub - 2;
            while (*subp) *p++ = *subp++;
          }

        *p++ = 0x01; /* screening identifier */
        *p++ = 0x01;
        *p++ = pc->chan->setup.screen;

	l = p - tmp;
	if (!(skb = l3_alloc_skb(l))) return;
	memcpy(skb_put(skb, l), tmp, l);

        l3_msg(pc->l3, DL_DATA | REQUEST, skb);
} /* l3dss1_redir_req */

/********************************************/
/* handle deflection request in early state */
/********************************************/
static void l3dss1_redir_req_early(l3_process_t *pc, u_char pr, void *arg)
{
  l3dss1_proceed_req(pc,pr,arg);
  l3dss1_redir_req(pc,pr,arg);
} /* l3dss1_redir_req_early */

/***********************************************/
/* handle special commands for this protocol.  */
/* Examples are call independant services like */
/* remote operations with dummy  callref.      */
/***********************************************/
static int l3dss1_cmd_global(struct PStack *st, isdn_ctrl *ic)
{ u_char id;
  u_char temp[265];
  u_char *p = temp;
  int i, l, proc_len; 
  struct sk_buff *skb;
  l3_process_t *pc = NULL;

  switch (ic->arg)
   { case DSS1_CMD_INVOKE:
       if (ic->parm.dss1_io.datalen < 0) return(-2); /* invalid parameter */ 

       for (proc_len = 1, i = ic->parm.dss1_io.proc >> 8; i; i++) 
         i = i >> 8; /* add one byte */    
       l = ic->parm.dss1_io.datalen + proc_len + 8; /* length excluding ie header */
       if (l > 255) 
         return(-2); /* too long */

       if (!(id = new_invoke_id(st))) 
         return(0); /* first get a invoke id -> return if no available */
       
       i = -1; 
       MsgHead(p, i, MT_FACILITY); /* build message head */
       *p++ = 0x1C; /* Facility IE */
       *p++ = l; /* length of ie */
       *p++ = 0x91; /* remote operations */
       *p++ = 0xA1; /* invoke */
       *p++ = l - 3; /* length of invoke */
       *p++ = 0x02; /* invoke id tag */
       *p++ = 0x01; /* length is 1 */
       *p++ = id; /* invoke id */
       *p++ = 0x02; /* operation */
       *p++ = proc_len; /* length of operation */
       
       for (i = proc_len; i; i--)
         *p++ = (ic->parm.dss1_io.proc >> (i-1)) & 0xFF;
       memcpy(p, ic->parm.dss1_io.data, ic->parm.dss1_io.datalen); /* copy data */
       l = (p - temp) + ic->parm.dss1_io.datalen; /* total length */         

       if (ic->parm.dss1_io.timeout > 0)
        if (!(pc = dss1_new_l3_process(st, -1)))
          { free_invoke_id(st, id);
            return(-2);
          } 
       pc->prot.dss1.ll_id = ic->parm.dss1_io.ll_id; /* remember id */ 
       pc->prot.dss1.proc = ic->parm.dss1_io.proc; /* and procedure */

       if (!(skb = l3_alloc_skb(l))) 
         { free_invoke_id(st, id);
           if (pc)
           	release_l3_process(pc);
           return(-2);
         }
       memcpy(skb_put(skb, l), temp, l);
       
       if (pc)
        { pc->prot.dss1.invoke_id = id; /* remember id */
          L3AddTimer(&pc->timer, ic->parm.dss1_io.timeout, CC_TDSS1_IO | REQUEST);
        }
       
       l3_msg(st, DL_DATA | REQUEST, skb);
       ic->parm.dss1_io.hl_id = id; /* return id */
       return(0);

     case DSS1_CMD_INVOKE_ABORT:
       if ((pc = l3dss1_search_dummy_proc(st, ic->parm.dss1_io.hl_id)))
	{ L3DelTimer(&pc->timer); /* remove timer */
          release_l3_process(pc);
          return(0); 
        } 
       else
	{ l3_debug(st, "l3dss1_cmd_global abort unknown id");
          return(-2);
        } 
       break;
    
     default: 
       l3_debug(st, "l3dss1_cmd_global unknown cmd 0x%lx", ic->arg);
       return(-1);  
   } /* switch ic-> arg */
  return(-1);
} /* l3dss1_cmd_global */

static void 
l3dss1_io_timer(l3_process_t *pc)
{ isdn_ctrl ic;
  struct IsdnCardState *cs = pc->l3->l1.hardware;

  L3DelTimer(&pc->timer); /* remove timer */

  ic.driver = cs->myid;
  ic.command = ISDN_STAT_PROT;
  ic.arg = DSS1_STAT_INVOKE_ERR;
  ic.parm.dss1_io.hl_id = pc->prot.dss1.invoke_id;
  ic.parm.dss1_io.ll_id = pc->prot.dss1.ll_id;
  ic.parm.dss1_io.proc = pc->prot.dss1.proc;
  ic.parm.dss1_io.timeout= -1;
  ic.parm.dss1_io.datalen = 0;
  ic.parm.dss1_io.data = NULL;
  free_invoke_id(pc->l3, pc->prot.dss1.invoke_id);
  pc->prot.dss1.invoke_id = 0; /* reset id */

  cs->iif.statcallb(&ic);

  release_l3_process(pc); 
} /* l3dss1_io_timer */
#endif

static void
l3dss1_release_ind(l3_process_t *pc, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;
	int callState = 0;

	if ((p = findie(skb->data, skb->len, IE_CALL_STATE, 0))) {
		p++;
		if (1 == *p++)
			callState = *p;
	}
	if (callState == 0) {
		/* ETS 300-104 7.6.1, 8.6.1, 10.6.1... and 16.1
		 * set down layer 3 without sending any message
		 */
		hisax_l3up(pc, CC_RELEASE | INDICATION, pc);
		newl3state(pc, 0);
		release_l3_process(pc);
	} else {
		hisax_l3up(pc, CC_IGNORE | INDICATION, pc);
	}
}

static void
l3dss1_dummy(l3_process_t *pc, u_char pr, void *arg)
{
}

static void
l3dss1_t302(l3_process_t *pc, u_char pr, void *arg)
{
	cause_t cause;

	L3DelTimer(&pc->timer);
	cause.len = 2;
	cause.loc = 0x80 | CAUSE_LOC_USER;
	cause.val = 0x80 | CAUSE_INVALID_NUMBER;
	l3dss1_disconnect_req(pc, pr, &cause);
	hisax_l3up(pc, CC_SETUP_ERR, pc);
}

static void
l3dss1_t303(l3_process_t *pc, u_char pr, void *arg)
{
	if (pc->N303 > 0) {
		pc->N303--;
		L3DelTimer(&pc->timer);
		l3dss1_setup_req(pc, pr, arg);
	} else {
		L3DelTimer(&pc->timer);
		l3dss1_message_cause(pc, MT_RELEASE_COMPLETE, CAUSE_TIMER_EXPIRED);
		hisax_l3up(pc, CC_NOSETUP_RSP, pc);
		release_l3_process(pc);
	}
}

static void
l3dss1_t304(l3_process_t *pc, u_char pr, void *arg)
{
	cause_t cause;

	L3DelTimer(&pc->timer);
	cause.len = 2;
	cause.loc = 0x80 | CAUSE_LOC_USER;
	cause.val = 0x80 | CAUSE_TIMER_EXPIRED;
	l3dss1_disconnect_req(pc, pr, &cause);
	hisax_l3up(pc, CC_SETUP_ERR, pc);
}

static void
l3dss1_t305(l3_process_t *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
#if 0
	if (pc->para.cause != NO_CAUSE)
		cause = pc->para.cause;
#endif
	newl3state(pc, 19);
	l3dss1_message_cause(pc, MT_RELEASE, CAUSE_NORMALUNSPECIFIED);
	L3AddTimer(&pc->timer, T308, CC_T308_1);
}

static void
l3dss1_t310(l3_process_t *pc, u_char pr, void *arg)
{
	cause_t cause;

	L3DelTimer(&pc->timer);
	cause.len = 2;
	cause.loc = 0x80 | CAUSE_LOC_USER;
	cause.val = 0x80 | CAUSE_TIMER_EXPIRED;
	l3dss1_disconnect_req(pc, pr, &cause);
	hisax_l3up(pc, CC_SETUP_ERR, pc);
}

static void
l3dss1_t313(l3_process_t *pc, u_char pr, void *arg)
{
	cause_t cause;

	L3DelTimer(&pc->timer);
	cause.len = 2;
	cause.loc = 0x80 | CAUSE_LOC_USER;
	cause.val = 0x80 | CAUSE_TIMER_EXPIRED;
	l3dss1_disconnect_req(pc, pr, &cause);
	hisax_l3up(pc, CC_CONNECT_ERR, pc);
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
	hisax_l3up(pc, CC_RELEASE_ERR, pc);
	release_l3_process(pc);
}

static void
l3dss1_t318(l3_process_t *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
#if 0
	pc->para.cause = 102;	/* Timer expiry */
	pc->para.loc = 0;	/* local */
#endif
	hisax_l3up(pc, CC_RESUME_ERR, pc);
	newl3state(pc, 19);
	l3dss1_message(pc, MT_RELEASE);
	L3AddTimer(&pc->timer, T308, CC_T308_1);
}

static void
l3dss1_t319(l3_process_t *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
#if 0
	pc->para.cause = 102;	/* Timer expiry */
	pc->para.loc = 0;	/* local */
#endif
	hisax_l3up(pc, CC_SUSPEND_ERR, pc);
	newl3state(pc, 10);
}

static void
l3dss1_restart(l3_process_t *pc, u_char pr, void *arg) {
	L3DelTimer(&pc->timer);
	hisax_l3up(pc, CC_RELEASE | INDICATION, pc);
	release_l3_process(pc);
}

static void
l3dss1_status(l3_process_t *pc, u_char pr, void *arg) {
	u_char *p;
	struct sk_buff *skb = arg;
	int ret; 
	u_char cause = 0, callState = 0;
	
	if ((ret = l3dss1_get_cause(pc, skb))) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "STATUS get_cause ret(%d)",ret);
		if (ret < 0)
			cause = CAUSE_MANDATORY_IE_MISS;
		else if (ret > 0)
			cause = CAUSE_INVALID_CONTENTS;
	}
	if ((p = findie(skb->data, skb->len, IE_CALL_STATE, 0))) {
		p++;
		if (1 == *p++) {
			callState = *p;
			if (!ie_in_set(pc, *p, l3_valid_states))
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
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "STATUS error(%d/%d)",ret,cause);
		l3dss1_status_send(pc, CAUSE_LOC_USER, cause);
		if (cause != CAUSE_IE_NOTIMPLEMENTED)
			return;
	}
	cause = pc->para.cause.val;
	if (((cause & 0x7f) == CAUSE_PROTOCOL_ERROR) && (callState == 0)) {
		/* ETS 300-104 7.6.1, 8.6.1, 10.6.1...
		 * if received MT_STATUS with cause == 111 and call
		 * state == 0, then we must set down layer 3
		 */
		hisax_l3up(pc, CC_RELEASE | INDICATION, pc);
		newl3state(pc, 0);
		release_l3_process(pc);
	}
}

static void
l3dss1_facility(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;
	
	ret = check_infoelements(pc, skb, ie_FACILITY);
	l3dss1_std_ie_err(pc, ret);
#if 0
	{
		u_char *p;
		if ((p = findie(skb->data, skb->len, IE_FACILITY, 0)))
			l3dss1_parse_facility(pc->l3, pc, pc->callref, p);
	}
#endif
}

static void
l3dss1_suspend_req(l3_process_t *pc, u_char pr, void *arg)
{
	u_char *msg = arg;
	struct sk_buff *skb;
	u_char tmp[32];
	u_char *p = tmp;
	u_char i, l;

	MsgHead(p, pc->callref, MT_SUSPEND);
	l = *msg++;
	if (l && (l <= 10)) {	/* Max length 10 octets */
		*p++ = IE_CALL_ID;
		*p++ = l;
		for (i = 0; i < l; i++)
			*p++ = *msg++;
	} else if (l) {
		l3_debug(pc->l3, "SUS wrong CALL_ID len %d", l);
		return;
	}
	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	l3_msg(pc->l3, DL_DATA | REQUEST, skb);
	newl3state(pc, 15);
	L3AddTimer(&pc->timer, T319, CC_T319);
}

static void
l3dss1_suspend_ack(l3_process_t *pc, u_char pr, void *arg) {
	struct sk_buff *skb = arg;
	int ret;

	L3DelTimer(&pc->timer);
	newl3state(pc, 0);
	pc->para.cause.val = NO_CAUSE;
	hisax_l3up(pc, CC_SUSPEND | CONFIRM, pc);
	/* We don't handle suspend_ack for IE errors now */
	if ((ret = check_infoelements(pc, skb, ie_SUSPEND_ACKNOWLEDGE)))
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "SUSPACK check ie(%d)",ret);
	release_l3_process(pc);
}

static void
l3dss1_suspend_rej(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;
	u_char cause;

	if ((ret = l3dss1_get_cause(pc, skb))) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "SUSP_REJ get_cause ret(%d)",ret);
		if (ret < 0) 
			cause = CAUSE_MANDATORY_IE_MISS;
		else
			cause = CAUSE_INVALID_CONTENTS;
		l3dss1_status_send(pc, CAUSE_LOC_USER, cause);
		return;
	}
	ret = check_infoelements(pc, skb, ie_SUSPEND_REJECT);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		return;
	}
	L3DelTimer(&pc->timer);
	hisax_l3up(pc, CC_SUSPEND_ERR, pc);
	newl3state(pc, 10);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3dss1_std_ie_err(pc, ret);
}

static void
l3dss1_resume_req(l3_process_t *pc, u_char pr, void *arg)
{
	u_char *msg = arg;
	struct sk_buff *skb;
	u_char tmp[32];
	u_char *p = tmp;
	u_char i, l;

	MsgHead(p, pc->callref, MT_RESUME);

	l = *msg++;
	if (l && (l <= 10)) {	/* Max length 10 octets */
		*p++ = IE_CALL_ID;
		*p++ = l;
		for (i = 0; i < l; i++)
			*p++ = *msg++;
	} else if (l) {
		l3_debug(pc->l3, "RES wrong CALL_ID len %d", l);
		return;
	}
	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	l3_msg(pc->l3, DL_DATA | REQUEST, skb);
	newl3state(pc, 17);
	L3AddTimer(&pc->timer, T318, CC_T318);
}

static void
l3dss1_resume_ack(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int id, ret;

	if ((id = l3dss1_get_channel_id(pc, skb, &pc->para.channel)) > 0) {
		if ((0 == id) || (3 == id)) {
			if (pc->debug & L3_DEB_WARN)
				l3_debug(pc->l3, "resume ack with wrong chid %x", id);
			l3dss1_status_send(pc, CAUSE_LOC_USER, CAUSE_INVALID_CONTENTS);
			return;
		}
//		pc->para.bchannel = id;
	} else if (1 == pc->state) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "resume ack without chid (ret %d)", id);
		l3dss1_status_send(pc, CAUSE_LOC_USER, CAUSE_MANDATORY_IE_MISS);
		return;
	}
	ret = check_infoelements(pc, skb, ie_RESUME_ACKNOWLEDGE);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		return;
	}
	L3DelTimer(&pc->timer);
	hisax_l3up(pc, CC_RESUME | CONFIRM, pc);
	newl3state(pc, 10);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3dss1_std_ie_err(pc, ret);
}

static void
l3dss1_resume_rej(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;
	u_char cause;

	if ((ret = l3dss1_get_cause(pc, skb))) {
		if (pc->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "RES_REJ get_cause ret(%d)",ret);
		if (ret < 0) 
			cause = CAUSE_MANDATORY_IE_MISS;
		else
			cause = CAUSE_INVALID_CONTENTS;
		l3dss1_status_send(pc, CAUSE_LOC_USER, cause);
		return;
	}
	ret = check_infoelements(pc, skb, ie_RESUME_REJECT);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		return;
	}
	L3DelTimer(&pc->timer);
	hisax_l3up(pc, CC_RESUME_ERR, pc);
	newl3state(pc, 0);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3dss1_std_ie_err(pc, ret);
	release_l3_process(pc);
}

static void
l3dss1_global_restart(l3_process_t *pc, u_char pr, void *arg)
{
	u_char tmp[32];
	u_char *p;
	u_char ri, ch = 0, chan = 0;
	int l;
	struct sk_buff *skb = arg;
	l3_process_t *up;

	newl3state(pc, 2);
	L3DelTimer(&pc->timer);
	if ((p = findie(skb->data, skb->len, IE_RESTART_IND, 0))) {
		ri = p[2];
		l3_debug(pc->l3, "Restart %x", ri);
	} else {
		l3_debug(pc->l3, "Restart without restart IE");
		ri = 0x86;
	}
	if ((p = findie(skb->data, skb->len, IE_CHANNEL_ID, 0))) {
		chan = p[2] & 3;
		ch = p[2];
		if (pc->debug)
			l3_debug(pc->l3, "Restart for channel %d", chan);
	}
	newl3state(pc, 2);
	up = pc->l3->proc;
	while (up) {
		if ((ri & 7) == 7)
			up->l3->p_down(up->l3, CC_RESTART | REQUEST, up);
//		else if (up->para.bchannel == chan)
//			up->st->lli.l4l3(up->st, CC_RESTART | REQUEST, up);
		up = up->next;
	}
	p = tmp;
	MsgHead(p, pc->callref, MT_RESTART_ACKNOWLEDGE);
	if (chan) {
		*p++ = IE_CHANNEL_ID;
		*p++ = 1;
		*p++ = ch | 0x80;
	}
	*p++ = IE_RESTART_IND;
	*p++ = 1;
	*p++ = ri;
	l = p - tmp;
	if (!(skb = l3_alloc_skb(l)))
		return;
	memcpy(skb_put(skb, l), tmp, l);
	newl3state(pc, 0);
	l3_msg(pc->l3, DL_DATA | REQUEST, skb);
}

static void
l3dss1_dl_reset(l3_process_t *pc, u_char pr, void *arg)
{
	cause_t cause;

	cause.len = 2;
	cause.loc = 0x80 | CAUSE_LOC_USER;
	cause.val = 0x80 | CAUSE_TEMPORARY_FAILURE;
        l3dss1_disconnect_req(pc, pr, &cause);
        hisax_l3up(pc, CC_SETUP_ERR, pc);
}

static void
l3dss1_dl_release(l3_process_t *pc, u_char pr, void *arg)
{
        newl3state(pc, 0);
#if 0
        pc->para.cause = 0x1b;          /* Destination out of order */
        pc->para.loc = 0;
#endif
        hisax_l3up(pc, CC_RELEASE | INDICATION, pc);
        release_l3_process(pc);
}

static void
l3dss1_dl_reestablish(l3_process_t *pc, u_char pr, void *arg)
{
        L3DelTimer(&pc->timer);
        L3AddTimer(&pc->timer, T309, CC_T309);
        l3_msg(pc->l3, DL_ESTABLISH | REQUEST, NULL);
}
 
static void
l3dss1_dl_reest_status(l3_process_t *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);

	l3dss1_status_send(pc, CAUSE_LOC_USER, CAUSE_NORMALUNSPECIFIED);
}

/* *INDENT-OFF* */
static struct stateentry downstatelist[] =
{
	{SBIT(0),
	 CC_SETUP | REQUEST, l3dss1_setup_req},
	{SBIT(0),
	 CC_RESUME | REQUEST, l3dss1_resume_req},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(6) | SBIT(7) | SBIT(8) | SBIT(9) | SBIT(10) | SBIT(25),
	 CC_DISCONNECT | REQUEST, l3dss1_disconnect_req},
	{SBIT(12),
	 CC_RELEASE | REQUEST, l3dss1_release_req},
	{ALL_STATES,
	 CC_RESTART | REQUEST, l3dss1_restart},
	{SBIT(6) | SBIT(25),
	 CC_IGNORE | REQUEST, l3dss1_reset},
	{SBIT(6) | SBIT(25),
	 CC_REJECT | REQUEST, l3dss1_reject_req},
	{SBIT(6) | SBIT(25),
	 CC_PROCEED_SEND | REQUEST, l3dss1_proceed_req},
	{SBIT(6),
	 CC_MORE_INFO | REQUEST, l3dss1_setup_ack_req},
	{SBIT(25),
	 CC_MORE_INFO | REQUEST, l3dss1_dummy},
	{SBIT(6) | SBIT(9) | SBIT(25),
	 CC_ALERTING | REQUEST, l3dss1_alert_req},
	{SBIT(6) | SBIT(7) | SBIT(9) | SBIT(25),
	 CC_SETUP | RESPONSE, l3dss1_setup_rsp},
	{SBIT(10),
	 CC_SUSPEND | REQUEST, l3dss1_suspend_req},
#if 0
        {SBIT(7) | SBIT(9) | SBIT(25),
         CC_REDIR | REQUEST, l3dss1_redir_req},
        {SBIT(6),
         CC_REDIR | REQUEST, l3dss1_redir_req_early},
#endif
        {SBIT(9) | SBIT(25),
         CC_DISCONNECT | REQUEST, l3dss1_disconnect_req},
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
};

#define MANSLLEN \
        (sizeof(manstatelist) / sizeof(struct stateentry))
/* *INDENT-ON* */


static void
global_handler(layer3_t *l3, int mt, struct sk_buff *skb)
{
	u_char tmp[16];
	u_char *p = tmp;
	int l;
	int i;
	l3_process_t *proc = l3->global;

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
		MsgHead(p, proc->callref, MT_STATUS);
		*p++ = IE_CAUSE;
		*p++ = 0x2;
		*p++ = 0x80 | CAUSE_LOC_USER;
		*p++ = 0x80 | CAUSE_INVALID_CALLREF;
		*p++ = IE_CALL_STATE;
		*p++ = 0x1;
		*p++ = proc->state & 0x3f;
		l = p - tmp;
		if (!(skb = l3_alloc_skb(l)))
			return;
		memcpy(skb_put(skb, l), tmp, l);
		l3_msg(l3, DL_DATA | REQUEST, skb);
	} else {
		if (l3->debug & L3_DEB_STATE) {
			l3_debug(l3, "dss1 global %d mt %x",
				proc->state, mt);
		}
		globalmes_list[i].rout(proc, mt, skb);
	}
}

static void
dss1up(layer3_t *l3, int pr, void *arg)
{
	int i, mt, cr, cause, callState;
	char *ptr;
	struct sk_buff *skb = arg;
	l3_process_t *proc;

	switch (pr) {
		case (DL_DATA | INDICATION):
		case (DL_UNITDATA | INDICATION):
			break;
		case (DL_ESTABLISH | CONFIRM):
		case (DL_ESTABLISH | INDICATION):
		case (DL_RELEASE | INDICATION):
		case (DL_RELEASE | CONFIRM):
			l3_msg(l3, pr, arg);
			return;
			break;
                default:
                        printk(KERN_ERR "HiSax dss1up unknown pr=%04x\n", pr);
                        return;
	}
	if (skb->len < 3) {
		l3_debug(l3, "dss1up frame too short(%d)", skb->len);
		dev_kfree_skb(skb);
		return;
	}

	if (skb->data[0] != PROTO_DIS_EURO) {
		if (l3->debug & L3_DEB_PROTERR) {
			l3_debug(l3, "dss1up%sunexpected discriminator %x message len %d",
				 (pr == (DL_DATA | INDICATION)) ? " " : "(broadcast) ",
				 skb->data[0], skb->len);
		}
		dev_kfree_skb(skb);
		return;
	}
	cr = getcallref(skb->data);
	if (skb->len < ((skb->data[1] & 0x0f) + 3)) {
		l3_debug(l3, "dss1up frame too short(%d)", skb->len);
		dev_kfree_skb(skb);
		return;
	}
	mt = skb->data[skb->data[1] + 2];
	if (l3->debug & L3_DEB_STATE)
		l3_debug(l3, "dss1up cr %d", cr);
	if (cr == -2) {  /* wrong Callref */
		if (l3->debug & L3_DEB_WARN)
			l3_debug(l3, "dss1up wrong Callref");
		dev_kfree_skb(skb);
		return;
	} else if (cr == -1) {	/* Dummy Callref */
#if 0
	u_char *p;
		if (mt == MT_FACILITY)
			if ((p = findie(skb->data, skb->len, IE_FACILITY, 0))) {
				l3dss1_parse_facility(st, NULL, 
					(pr == (DL_DATA | INDICATION)) ? -1 : -2, p); 
				dev_kfree_skb(skb);
				return;  
			}
#endif
		if (l3->debug & L3_DEB_WARN)
			l3_debug(l3, "dss1up dummy Callref (no facility msg or ie)");
		dev_kfree_skb(skb);
		return;
	} else if ((((skb->data[1] & 0x0f) == 1) && (0==(cr & 0x7f))) ||
		(((skb->data[1] & 0x0f) == 2) && (0==(cr & 0x7fff)))) {	/* Global CallRef */
		if (l3->debug & L3_DEB_STATE)
			l3_debug(l3, "dss1up Global CallRef");
		global_handler(l3, mt, skb);
		dev_kfree_skb(skb);
		return;
	} else if (!(proc = getl3proc(l3, cr))) {
		/* No transaction process exist, that means no call with
		 * this callreference is active
		 */
		if (mt == MT_SETUP) {
			/* Setup creates a new transaction process */
			if (skb->data[2] & 0x80) {
				/* Setup with wrong CREF flag */
				if (l3->debug & L3_DEB_STATE)
					l3_debug(l3, "dss1up wrong CRef flag");
				dev_kfree_skb(skb);
				return;
			}
			if (!(proc = new_l3_process(l3, cr))) {
				/* May be to answer with RELEASE_COMPLETE and
				 * CAUSE 0x2f "Resource unavailable", but this
				 * need a new_l3_process too ... arghh
				 */
				dev_kfree_skb(skb);
				return;
			}
		} else if (mt == MT_STATUS) {
			cause = 0;
			if ((ptr = findie(skb->data, skb->len, IE_CAUSE, 0)) != NULL) {
				ptr++;
				if (*ptr++ == 2)
					ptr++;
				cause = *ptr & 0x7f;
			}
			callState = 0;
			if ((ptr = findie(skb->data, skb->len, IE_CALL_STATE, 0)) != NULL) {
				ptr++;
				if (*ptr++ == 2)
					ptr++;
				callState = *ptr;
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
				if ((proc = new_l3_process(l3, cr))) {
					l3dss1_msg_without_setup(proc,
						CAUSE_LOC_USER, CAUSE_NOTCOMPAT_STATE);
				}
			}
			dev_kfree_skb(skb);
			return;
		} else if (mt == MT_RELEASE_COMPLETE) {
			dev_kfree_skb(skb);
			return;
		} else {
			/* ETS 300-104 part 2
			 * if setup has not been made and a message type
			 * (except MT_SETUP and RELEASE_COMPLETE) is received,
			 * we must send MT_RELEASE_COMPLETE cause 81 */
			dev_kfree_skb(skb);
			if ((proc = new_l3_process(l3, cr))) {
				l3dss1_msg_without_setup(proc, CAUSE_LOC_USER,
					CAUSE_INVALID_CALLREF);
			}
			return;
		}
	}
	if (l3dss1_check_messagetype_validity(proc, mt, skb)) {
		dev_kfree_skb(skb);
		return;
	}
#if 0
	if ((p = findie(skb->data, skb->len, IE_DISPLAY, 0)) != NULL) 
	  l3dss1_deliver_display(proc, pr, p); /* Display IE included */
#endif
	for (i = 0; i < DATASLLEN; i++)
		if ((mt == datastatelist[i].primitive) &&
		    ((1 << proc->state) & datastatelist[i].state))
			break;
	if (i == DATASLLEN) {
		if (l3->debug & L3_DEB_STATE) {
			l3_debug(l3, "dss1up%sstate %d mt %#x unhandled",
				(pr == (DL_DATA | INDICATION)) ? " " : "(broadcast) ",
				proc->state, mt);
		}
		if ((MT_RELEASE_COMPLETE != mt) && (MT_RELEASE != mt)) {
			l3dss1_status_send(proc, CAUSE_LOC_USER, CAUSE_NOTCOMPAT_STATE);
		}
	} else {
		if (l3->debug & L3_DEB_STATE) {
			l3_debug(l3, "dss1up%sstate %d mt %x",
				(pr == (DL_DATA | INDICATION)) ? " " : "(broadcast) ",
				proc->state, mt);
		}
		datastatelist[i].rout(proc, pr, skb);
	}
	dev_kfree_skb(skb);
	return;
}

static void
dss1down(layer3_t *l3, int pr, void *arg)
{
	int i, cr;
	l3_process_t *proc;

	if ((DL_ESTABLISH | REQUEST) == pr) {
		l3_msg(l3, pr, NULL);
		return;
	} else if (((CC_SETUP | REQUEST) == pr) || ((CC_RESUME | REQUEST) == pr)) {
		cr = newcallref();
		cr |= 0x80;
		proc = new_l3_process(l3, cr);
	} else {
		proc = arg;
	}
	if (!proc) {
		printk(KERN_ERR "HiSax dss1down without proc pr=%04x\n", pr);
		return;
	}
#if 0
	if ( pr == (CC_TDSS1_IO | REQUEST)) {
		l3dss1_io_timer(proc); /* timer expires */ 
		return;
	}  
#endif
	for (i = 0; i < DOWNSLLEN; i++)
		if ((pr == downstatelist[i].primitive) &&
		    ((1 << proc->state) & downstatelist[i].state))
			break;
	if (i == DOWNSLLEN) {
		if (l3->debug & L3_DEB_STATE) {
			l3_debug(l3, "dss1down state %d prim %#x unhandled",
				proc->state, pr);
		}
	} else {
		if (l3->debug & L3_DEB_STATE) {
			l3_debug(l3, "dss1down state %d prim %#x",
				proc->state, pr);
		}
		downstatelist[i].rout(proc, pr, arg);
	}
}

static void
dss1man(layer3_t *l3, int pr, void *arg)
{
        int i;
        l3_process_t *proc = arg;
 
        if (!proc) {
                printk(KERN_ERR "HiSax dss1man without proc pr=%04x\n", pr);
                return;
        }
        for (i = 0; i < MANSLLEN; i++)
                if ((pr == manstatelist[i].primitive) &&
                    ((1 << proc->state) & manstatelist[i].state))
                        break;
        if (i == MANSLLEN) {
                if (l3->debug & L3_DEB_STATE) {
                        l3_debug(l3, "cr %d dss1man state %d prim %#x unhandled",
                                proc->callref & 0x7f, proc->state, pr);
                }
        } else {
                if (l3->debug & L3_DEB_STATE) {
                        l3_debug(l3, "cr %d dss1man state %d prim %#x",
                                proc->callref & 0x7f, proc->state, pr);
                }
                manstatelist[i].rout(proc, pr, arg);
        }
}

int 
init_dss1(layer3_t *l3)
{
	char tmp[64];
	int i;

	l3->p_down = dss1down;
	l3->N303 = 1;
	if (!(l3->global = kmalloc(sizeof(l3_process_t), GFP_ATOMIC))) {
		printk(KERN_ERR "HiSax can't get memory for dss1 global CR\n");
	} else {
		l3->global->state = 0;
		l3->global->callref = 0;
		l3->global->next = NULL;
		l3->global->debug = L3_DEB_WARN;
		l3->global->l3 = l3;
		l3->global->N303 = 1;
		L3InitTimer(l3->global, &l3->global->timer);
	}
	strcpy(tmp, dss1_revision);
	printk(KERN_INFO "HiSax: DSS1 Rev. %s\n", HiSax_getrev(tmp));
}

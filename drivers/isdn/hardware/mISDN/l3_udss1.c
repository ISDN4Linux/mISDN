/* $Id: l3_udss1.c,v 0.12 2001/03/04 17:08:33 kkeil Exp $
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

#include "hisaxl3.h"
#include "helper.h"
#include "debug.h"
#include "dss1.h"

static layer3_t *dss1list = NULL;
static int debug = 0;
static hisaxobject_t u_dss1;


const char *dss1_revision = "$Revision: 0.12 $";

static int dss1man(l3_process_t *, u_int, void *);

static void MsgStart(l3_process_t *pc, u_char mt) {
	pc->op = &pc->obuf[0];
	*pc->op++ = 8;
	if (pc->callref == -1) { /* dummy cr */
		*pc->op++ = 0;
	} else {
		*pc->op++ = 1;
		*pc->op++ = pc->callref ^ 0x80;
	}
	*pc->op++ = mt;
}

static void AddvarIE(l3_process_t *pc, u_char ie, u_char *iep) {
	u_char len = *iep;

	*pc->op++ = ie;
	*pc->op++ = *iep++;
	while(len--)
		*pc->op++ = *iep++;	
}

static int SendMsg(l3_process_t *pc, int state) {
	int l;
	int ret;
	struct sk_buff *skb;

	l = pc->op - &pc->obuf[0];
	if (!(skb = l3_alloc_skb(l)))
		return(-ENOMEM);
	memcpy(skb_put(skb, l), &pc->obuf[0], l);
	if (state != -1)
		newl3state(pc, state);
	if ((ret=l3_msg(pc->l3, DL_DATA | REQUEST, DINFO_SKB, 0, skb)))
		dev_kfree_skb(skb);
	return(ret);
}

static int
l3dss1_message(l3_process_t *pc, u_char mt)
{
	struct sk_buff *skb;
	u_char *p;
	int ret;

	if (!(skb = l3_alloc_skb(4)))
		return(-ENOMEM);
	p = skb_put(skb, 4);
	*p++ = 8;
	*p++ = 1;
	*p++ = pc->callref ^ 0x80;
	*p++ = mt;
	if ((ret=l3_msg(pc->l3, DL_DATA | REQUEST, DINFO_SKB, 0, skb)))
		dev_kfree_skb(skb);
	return(ret);
}

static void
l3dss1_message_cause(l3_process_t *pc, u_char mt, u_char cause)
{
	MsgStart(pc, mt);
	*pc->op++ = IE_CAUSE;
	*pc->op++ = 0x2;
	*pc->op++ = 0x80 | CAUSE_LOC_USER;
	*pc->op++ = 0x80 | cause;
	SendMsg(pc, -1); 
}

static void
l3dss1_status_send(l3_process_t *pc, u_char cause)
{

	MsgStart(pc, MT_STATUS);
	*pc->op++ = IE_CAUSE;
	*pc->op++ = 2;
	*pc->op++ = 0x80 | CAUSE_LOC_USER;
	*pc->op++ = 0x80 | cause;

	*pc->op++ = IE_CALL_STATE;
	*pc->op++ = 1;
	*pc->op++ = pc->state & 0x3f;
	SendMsg(pc, -1); 
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
			printk(KERN_ERR "HiSax l3dss1_msg_without_setup wrong cause %d\n",
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
			if (pc->l3->debug & L3_DEB_CHECK)
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
			if (pc->l3->debug & L3_DEB_CHECK)
				l3_debug(pc->l3, "check IE shift back codeset %d->%d",
					codeset, old_codeset);
			codeset = old_codeset;
			codelock = 1;
		}
	}
	if (err_compr | err_ureg | err_len | err_seq) {
		if (pc->l3->debug & L3_DEB_CHECK)
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

static u_char *
l3dss1_get_channel_id(l3_process_t *pc, struct sk_buff *skb) {
	u_char *sp, *p;

	if ((sp = p = findie(skb->data, skb->len, IE_CHANNEL_ID, 0))) {
		if (*p != 1) { /* len for BRI = 1 */
			if (pc->l3->debug & L3_DEB_WARN)
				l3_debug(pc->l3, "wrong chid len %d", *p);
			pc->err = -2;
			return (NULL);
		}
		p++;
		if (*p & 0x60) { /* only base rate interface */
			if (pc->l3->debug & L3_DEB_WARN)
				l3_debug(pc->l3, "wrong chid %x", *p);
			pc->err = -3;
			return (NULL);
		}
		pc->bc = *p & 3;
	} else
		pc->err = -1;
	return(sp);
}

static u_char *
l3dss1_get_cause(l3_process_t *pc, struct sk_buff *skb) {
	u_char l;
	u_char *p, *sp;

	if ((sp = p = findie(skb->data, skb->len, IE_CAUSE, 0))) {
		l = *p++;
		if (l>30) {
			pc->err = 1;
			return(NULL);
		}
		if (l)
			l--;
		else {
			pc->err = 2;
			return(NULL);
		}
		if (l && !(*p & 0x80)) {
			l--;
			p++; /* skip recommendation */
		}
		p++;
		if (l) {
			if (!(*p & 0x80)) {
				pc->err = 3;
				return(NULL);
			}
			pc->err = *p & 0x7F;
		} else {
			pc->err = 4;
			return(NULL);
		}
	} else
		pc->err = -1;
	return(sp);
}

static void
l3dss1_release_req(l3_process_t *pc, u_char pr, void *arg)
{
	RELEASE_t *rel = arg;

	StopAllL3Timer(pc);
	if (rel) {
		MsgStart(pc, MT_RELEASE);
		if (rel->CAUSE)
			AddvarIE(pc, IE_CAUSE, rel->CAUSE);
		if (rel->FACILITY)
			AddvarIE(pc, IE_FACILITY, rel->FACILITY);
		if (rel->USER_USER)
			AddvarIE(pc, IE_USER_USER, rel->USER_USER);
		SendMsg(pc, 19);
	} else {
		newl3state(pc, 19);
		l3dss1_message(pc, MT_RELEASE);
	}
	L3AddTimer(&pc->timer, T308, CC_T308_1);
}

static void
l3dss1_setup_req(l3_process_t *pc, u_char pr, void *arg)
{
	SETUP_t *setup = arg;

	MsgStart(pc, MT_SETUP);
	if (setup->COMPLETE)
		*pc->op++ = IE_COMPLETE;
	if (setup->BEARER)
		AddvarIE(pc, IE_BEARER, setup->BEARER);
	if (setup->CHANNEL_ID)
		AddvarIE(pc, IE_CHANNEL_ID, setup->CHANNEL_ID);
	if (setup->FACILITY)
		AddvarIE(pc, IE_FACILITY, setup->FACILITY);
	if (setup->PROGRESS)
		AddvarIE(pc, IE_PROGRESS, setup->PROGRESS);
	if (setup->NET_FAC)
		AddvarIE(pc, IE_NET_FAC, setup->NET_FAC);
	if (setup->KEYPAD)
		AddvarIE(pc, IE_KEYPAD, setup->KEYPAD);
	if (setup->CALLING_PN)
		AddvarIE(pc, IE_CALLING_PN, setup->CALLING_PN);
	if (setup->CALLING_SUB)
		AddvarIE(pc, IE_CALLING_SUB, setup->CALLING_SUB);
	if (setup->CALLED_PN)
		AddvarIE(pc, IE_CALLED_PN, setup->CALLED_PN);
	if (setup->CALLED_SUB)
		AddvarIE(pc, IE_CALLED_SUB, setup->CALLED_SUB);
	if (setup->LLC)
		AddvarIE(pc, IE_LLC, setup->LLC);
	if (setup->HLC)
		AddvarIE(pc, IE_HLC, setup->HLC);
	if (setup->USER_USER)
		AddvarIE(pc, IE_USER_USER, setup->USER_USER);
	
	if (!SendMsg(pc, 1)) {
		L3DelTimer(&pc->timer);
		L3AddTimer(&pc->timer, T303, CC_T303);
	}
}

static void
l3dss1_disconnect_req(l3_process_t *pc, u_char pr, void *arg)
{
	DISCONNECT_t *disc = arg;

	StopAllL3Timer(pc);
	if (disc) {
		MsgStart(pc, MT_DISCONNECT);
		if (disc->CAUSE){ 
			AddvarIE(pc, IE_CAUSE, disc->CAUSE);
		} else {
			*pc->op++ = IE_CAUSE;
			*pc->op++ = 2;
			*pc->op++ = 0x80 | CAUSE_LOC_USER;
			*pc->op++ = 0x80 | CAUSE_NORMALUNSPECIFIED;
		}
		if (disc->FACILITY)
			AddvarIE(pc, IE_FACILITY, disc->FACILITY);
		if (disc->PROGRESS)
			AddvarIE(pc, IE_PROGRESS, disc->PROGRESS);
		if (disc->USER_USER)
			AddvarIE(pc, IE_USER_USER, disc->USER_USER);
		SendMsg(pc, 11);
	
	} else {
		newl3state(pc, 11);
		l3dss1_message_cause(pc, MT_DISCONNECT, CAUSE_NORMALUNSPECIFIED);
	}
	L3AddTimer(&pc->timer, T305, CC_T305);
}

static void
l3dss1_connect_req(l3_process_t *pc, u_char pr, void *arg)
{
	CONNECT_t *conn = arg;

	if (!pc->bc) {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "D-chan connect for waiting call");
		l3dss1_disconnect_req(pc, pr, NULL);
		return;
	}
	if (conn) {
		MsgStart(pc, MT_CONNECT);
		if (conn->BEARER)
			AddvarIE(pc, IE_BEARER, conn->BEARER);
		if (conn->CHANNEL_ID)
			AddvarIE(pc, IE_CHANNEL_ID, conn->CHANNEL_ID);
		if (conn->FACILITY)
			AddvarIE(pc, IE_FACILITY, conn->FACILITY);
		if (conn->PROGRESS)
			AddvarIE(pc, IE_PROGRESS, conn->PROGRESS);
		if (conn->CONNECT_PN)
			AddvarIE(pc, IE_CONNECT_PN, conn->CONNECT_PN);
		if (conn->CONNECT_SUB)
			AddvarIE(pc, IE_CONNECT_SUB, conn->CONNECT_SUB);
		if (conn->LLC)
			AddvarIE(pc, IE_LLC, conn->LLC);
		if (conn->HLC)
			AddvarIE(pc, IE_HLC, conn->HLC);
		if (conn->USER_USER)
			AddvarIE(pc, IE_USER_USER, conn->USER_USER);
		SendMsg(pc, 8);
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
	RELEASE_COMPLETE_t *rcmpl = arg;

	StopAllL3Timer(pc);
	if (rcmpl) {
		MsgStart(pc, MT_RELEASE_COMPLETE);
		if (rcmpl->CAUSE)
			AddvarIE(pc, IE_CAUSE, rcmpl->CAUSE);
		if (rcmpl->FACILITY)
			AddvarIE(pc, IE_FACILITY, rcmpl->FACILITY);
		if (rcmpl->USER_USER)
			AddvarIE(pc, IE_USER_USER, rcmpl->USER_USER);
		SendMsg(pc, 0);
	} else {
		newl3state(pc, 0);
		l3dss1_message(pc, MT_RELEASE_COMPLETE);
	}
	hisax_l3up(pc, CC_RELEASE_COMPLETE | CONFIRM, 0, NULL);
	release_l3_process(pc);
}

static void
l3dss1_alert_req(l3_process_t *pc, u_char pr, void *arg)
{
	ALERTING_t *alert = arg;

	if (alert) {
		MsgStart(pc, MT_ALERTING);
		if (alert->BEARER)
			AddvarIE(pc, IE_BEARER, alert->BEARER);
		if (alert->CHANNEL_ID)
			AddvarIE(pc, IE_CHANNEL_ID, alert->CHANNEL_ID);
		if (alert->FACILITY)
			AddvarIE(pc, IE_FACILITY, alert->FACILITY);
		if (alert->PROGRESS)
			AddvarIE(pc, IE_PROGRESS, alert->PROGRESS);
		if (alert->HLC)
			AddvarIE(pc, IE_HLC, alert->HLC);
		if (alert->USER_USER)
			AddvarIE(pc, IE_USER_USER, alert->USER_USER);
		SendMsg(pc, 7);
	} else {
		newl3state(pc, 7);
		l3dss1_message(pc, MT_ALERTING);
	}
	L3DelTimer(&pc->timer);
}

static void
l3dss1_proceed_req(l3_process_t *pc, u_char pr, void *arg)
{
	CALL_PROCEEDING_t *cproc = arg;

	if (cproc) {
		MsgStart(pc, MT_CALL_PROCEEDING);
		if (cproc->BEARER)
			AddvarIE(pc, IE_BEARER, cproc->BEARER);
		if (cproc->CHANNEL_ID)
			AddvarIE(pc, IE_CHANNEL_ID, cproc->CHANNEL_ID);
		if (cproc->FACILITY)
			AddvarIE(pc, IE_FACILITY, cproc->FACILITY);
		if (cproc->PROGRESS)
			AddvarIE(pc, IE_PROGRESS, cproc->PROGRESS);
		if (cproc->HLC)
			AddvarIE(pc, IE_HLC, cproc->HLC);
		SendMsg(pc, 9);
	} else {
		newl3state(pc, 9);
		l3dss1_message(pc, MT_CALL_PROCEEDING);
	}
	L3DelTimer(&pc->timer);
}

static void
l3dss1_setup_ack_req(l3_process_t *pc, u_char pr, void *arg)
{
	SETUP_ACKNOWLEDGE_t *setup_ack = arg;

	if (setup_ack) {
		MsgStart(pc, MT_SETUP_ACKNOWLEDGE);
		if (setup_ack->CHANNEL_ID)
			AddvarIE(pc, IE_CHANNEL_ID, setup_ack->CHANNEL_ID);
		if (setup_ack->FACILITY)
			AddvarIE(pc, IE_FACILITY, setup_ack->FACILITY);
		if (setup_ack->PROGRESS)
			AddvarIE(pc, IE_PROGRESS, setup_ack->PROGRESS);
		SendMsg(pc, 25);
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
	SUSPEND_t *susp = arg;

	if (susp) {
		MsgStart(pc, MT_SUSPEND);
		if (susp->CALL_ID)
			AddvarIE(pc, IE_CALL_ID, susp->CALL_ID);
		if (susp->FACILITY)
			AddvarIE(pc, IE_FACILITY, susp->FACILITY);
		SendMsg(pc, 15);
	} else {
		newl3state(pc, 15);
		l3dss1_message(pc, MT_SUSPEND);
	}
	L3AddTimer(&pc->timer, T319, CC_T319);
}

static void
l3dss1_resume_req(l3_process_t *pc, u_char pr, void *arg)
{
	RESUME_t *res = arg;

	if (res) {
		MsgStart(pc, MT_RESUME);
		if (res->CALL_ID)
			AddvarIE(pc, IE_CALL_ID, res->CALL_ID);
		if (res->FACILITY)
			AddvarIE(pc, IE_FACILITY, res->FACILITY);
		SendMsg(pc, 17);
	} else {
		newl3state(pc, 17);
		l3dss1_message(pc, MT_RESUME);
	}
	L3AddTimer(&pc->timer, T318, CC_T318);
}

static void
l3dss1_release_cmpl(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;

	StopAllL3Timer(pc);
	newl3state(pc, 0);
	memset(&pc->para.RELEASE_COMPLETE, 0, sizeof(RELEASE_COMPLETE_t));
	if (!(pc->para.RELEASE_COMPLETE.CAUSE = l3dss1_get_cause(pc, skb))) {
		if (pc->err > 0)
			if (pc->l3->debug & L3_DEB_WARN)
				l3_debug(pc->l3, "RELCMPL get_cause err(%d)",
					pc->err);
	}
	pc->para.RELEASE_COMPLETE.FACILITY =
		findie(skb->data, skb->len, IE_FACILITY, 0);
	pc->para.RELEASE_COMPLETE.DISPLAY =
		findie(skb->data, skb->len, IE_DISPLAY, 0);
	pc->para.RELEASE_COMPLETE.SIGNAL =
		findie(skb->data, skb->len, IE_SIGNAL, 0);
	pc->para.RELEASE_COMPLETE.USER_USER =
		findie(skb->data, skb->len, IE_USER_USER, 0);
	hisax_l3up(pc, CC_RELEASE_COMPLETE | INDICATION,
		sizeof(RELEASE_COMPLETE_t), &pc->para.RELEASE_COMPLETE);
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
		return;
	}
	L3DelTimer(&pc->timer);	/* T304 */
	newl3state(pc, 4);
	pc->para.ALERTING.BEARER =
		findie(skb->data, skb->len, IE_BEARER, 0);
	pc->para.ALERTING.FACILITY =
		findie(skb->data, skb->len, IE_FACILITY, 0);
	pc->para.ALERTING.PROGRESS =
		findie(skb->data, skb->len, IE_PROGRESS, 0);
	pc->para.ALERTING.DISPLAY =
		findie(skb->data, skb->len, IE_DISPLAY, 0);
	pc->para.ALERTING.SIGNAL =
		findie(skb->data, skb->len, IE_SIGNAL, 0);
	pc->para.ALERTING.HLC =
		findie(skb->data, skb->len, IE_HLC, 0);
	pc->para.ALERTING.USER_USER =
		findie(skb->data, skb->len, IE_USER_USER, 0);
	if (ret)
		l3dss1_std_ie_err(pc, ret);
	hisax_l3up(pc, CC_ALERTING | INDICATION, sizeof(ALERTING_t), &pc->para.ALERTING);
}

static void
l3dss1_call_proc(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;
	u_char cause;

	if ((pc->para.CALL_PROCEEDING.CHANNEL_ID =
		l3dss1_get_channel_id(pc, skb))) {
		if ((0 == pc->bc) || (3 == pc->bc)) {
			if (pc->l3->debug & L3_DEB_WARN)
				l3_debug(pc->l3, "setup answer with wrong chid %x", pc->bc);
			l3dss1_status_send(pc, CAUSE_INVALID_CONTENTS);
			return;
		}
	} else if (1 == pc->state) {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "setup answer wrong chid (ret %d)", pc->err);
		if (pc->err == -1)
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
		return;
	}
	pc->para.CALL_PROCEEDING.BEARER =
		findie(skb->data, skb->len, IE_BEARER, 0);
	pc->para.CALL_PROCEEDING.FACILITY =
		findie(skb->data, skb->len, IE_FACILITY, 0);
	pc->para.CALL_PROCEEDING.PROGRESS =
		findie(skb->data, skb->len, IE_PROGRESS, 0);
	pc->para.CALL_PROCEEDING.DISPLAY =
		findie(skb->data, skb->len, IE_DISPLAY, 0);
	pc->para.CALL_PROCEEDING.HLC =
		findie(skb->data, skb->len, IE_HLC, 0);
	L3DelTimer(&pc->timer);
	newl3state(pc, 3);
	L3AddTimer(&pc->timer, T310, CC_T310);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3dss1_std_ie_err(pc, ret);
	hisax_l3up(pc, CC_PROCEEDING | INDICATION, sizeof(CALL_PROCEEDING_t),
		&pc->para.CALL_PROCEEDING);
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
	pc->para.CONNECT.BEARER =
		findie(skb->data, skb->len, IE_BEARER, 0);
	pc->para.CONNECT.FACILITY =
		findie(skb->data, skb->len, IE_FACILITY, 0);
	pc->para.CONNECT.PROGRESS =
		findie(skb->data, skb->len, IE_PROGRESS, 0);
	pc->para.CONNECT.DISPLAY =
		findie(skb->data, skb->len, IE_DISPLAY, 0);
	pc->para.CONNECT.DATE =
		findie(skb->data, skb->len, IE_DATE, 0);
	pc->para.CONNECT.SIGNAL =
		findie(skb->data, skb->len, IE_SIGNAL, 0);
	pc->para.CONNECT.CONNECT_PN =
		findie(skb->data, skb->len, IE_CONNECT_PN, 0);
	pc->para.CONNECT.CONNECT_SUB =
		findie(skb->data, skb->len, IE_CONNECT_SUB, 0);
	pc->para.CONNECT.HLC =
		findie(skb->data, skb->len, IE_HLC, 0);
	pc->para.CONNECT.LLC =
		findie(skb->data, skb->len, IE_LLC, 0);
	pc->para.CONNECT.USER_USER =
		findie(skb->data, skb->len, IE_USER_USER, 0);
	if (ret)
		l3dss1_std_ie_err(pc, ret);
	hisax_l3up(pc, CC_CONNECT | INDICATION, sizeof(CONNECT_t), &pc->para.CONNECT);
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
	pc->para.CONNECT_ACKNOWLEDGE.CHANNEL_ID =
		l3dss1_get_channel_id(pc, skb);
	pc->para.CONNECT_ACKNOWLEDGE.DISPLAY =
		findie(skb->data, skb->len, IE_DISPLAY, 0);
	pc->para.CONNECT_ACKNOWLEDGE.SIGNAL =
		findie(skb->data, skb->len, IE_SIGNAL, 0);
	if (ret)
		l3dss1_std_ie_err(pc, ret);
	hisax_l3up(pc, CC_CONNECT_ACKNOWLEDGE | INDICATION,
		sizeof(CONNECT_ACKNOWLEDGE_t), &pc->para.CONNECT_ACKNOWLEDGE);
}

static void
l3dss1_disconnect(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;
	u_char cause = 0;

	StopAllL3Timer(pc);
	if (!(pc->para.DISCONNECT.CAUSE = l3dss1_get_cause(pc, skb))) {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "DISC get_cause ret(%d)", pc->err);
		if (pc->err < 0)
			cause = CAUSE_MANDATORY_IE_MISS;
		else if (pc->err > 0)
			cause = CAUSE_INVALID_CONTENTS;
	} 
	ret = check_infoelements(pc, skb, ie_DISCONNECT);
	if (ERR_IE_COMPREHENSION == ret)
		cause = CAUSE_MANDATORY_IE_MISS;
	else if ((!cause) && (ERR_IE_UNRECOGNIZED == ret))
		cause = CAUSE_IE_NOTIMPLEMENTED;
	pc->para.DISCONNECT.FACILITY =
		findie(skb->data, skb->len, IE_FACILITY, 0);
	pc->para.DISCONNECT.PROGRESS =
		findie(skb->data, skb->len, IE_PROGRESS, 0);
	pc->para.DISCONNECT.DISPLAY =
		findie(skb->data, skb->len, IE_DISPLAY, 0);
	pc->para.DISCONNECT.SIGNAL =
		findie(skb->data, skb->len, IE_SIGNAL, 0);
	pc->para.DISCONNECT.USER_USER =
		findie(skb->data, skb->len, IE_USER_USER, 0);
	ret = pc->state;
	if (cause)
		newl3state(pc, 19);
	else
		newl3state(pc, 12);
       	if (11 != ret)
		hisax_l3up(pc, CC_DISCONNECT | INDICATION, sizeof(DISCONNECT_t),
			&pc->para.DISCONNECT);
	else if (!cause)
		l3dss1_release_req(pc, pr, NULL);
	if (cause) {
		l3dss1_message_cause(pc, MT_RELEASE, cause);
		L3AddTimer(&pc->timer, T308, CC_T308_1);
	}
}

static void
l3dss1_setup_ack(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;
	u_char cause;

	if ((pc->para.SETUP_ACKNOWLEDGE.CHANNEL_ID =
		l3dss1_get_channel_id(pc, skb))) {
		if ((0 == pc->bc) || (3 == pc->bc)) {
			if (pc->l3->debug & L3_DEB_WARN)
				l3_debug(pc->l3, "setup answer with wrong chid %x", pc->bc);
			l3dss1_status_send(pc, CAUSE_INVALID_CONTENTS);
			return;
		}
	} else {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "setup answer wrong chid (ret %d)", pc->err);
		if (pc->err == -1)
			cause = CAUSE_MANDATORY_IE_MISS;
		else
			cause = CAUSE_INVALID_CONTENTS;
		l3dss1_status_send(pc, cause);
		return;
	}
	/* Now we are on none mandatory IEs */
	ret = check_infoelements(pc, skb, ie_SETUP_ACKNOWLEDGE);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		return;
	}
	pc->para.SETUP_ACKNOWLEDGE.FACILITY =
		findie(skb->data, skb->len, IE_FACILITY, 0);
	pc->para.SETUP_ACKNOWLEDGE.PROGRESS =
		findie(skb->data, skb->len, IE_PROGRESS, 0);
	pc->para.SETUP_ACKNOWLEDGE.DISPLAY =
		findie(skb->data, skb->len, IE_DISPLAY, 0);
	pc->para.SETUP_ACKNOWLEDGE.SIGNAL =
		findie(skb->data, skb->len, IE_SIGNAL, 0);
	L3DelTimer(&pc->timer);
	newl3state(pc, 2);
	L3AddTimer(&pc->timer, T304, CC_T304);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3dss1_std_ie_err(pc, ret);
	hisax_l3up(pc, CC_SETUP_ACKNOWLEDGE | INDICATION,
		sizeof(SETUP_ACKNOWLEDGE_t), &pc->para.SETUP_ACKNOWLEDGE);
}

static void
l3dss1_setup(l3_process_t *pc, u_char pr, void *arg)
{
	u_char *p, cause;
	int bcfound = 0;
	struct sk_buff *skb = arg;
	int err = 0;

	/*
	 * Bearer Capabilities
	 */
	/* only the first occurence 'll be detected ! */
	if ((p = pc->para.SETUP.BEARER = findie(skb->data, skb->len, IE_BEARER, 0))) {
		if ((p[0] < 2) || (p[0] > 11))
			err = 1;
		else {
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
			return;
		} 
	} else {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "setup without bearer capabilities");
		/* ETS 300-104 1.3.3 */
		l3dss1_msg_without_setup(pc, CAUSE_MANDATORY_IE_MISS);
		return;
	}
	/*
	 * Channel Identification
	 */
	if ((pc->para.SETUP.CHANNEL_ID = l3dss1_get_channel_id(pc, skb))) {
		if (pc->bc) {
			if ((3 == pc->bc) && (0x10 == (pc->para.SETUP.BEARER[2] & 0x7f))) {
				if (pc->l3->debug & L3_DEB_WARN)
					l3_debug(pc->l3, "setup with wrong chid %x",
						pc->bc);
				l3dss1_msg_without_setup(pc,
					CAUSE_INVALID_CONTENTS);
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
			l3_debug(pc->l3, "setup with wrong chid ret %d", pc->err);
		if (pc->err == -1)
			cause = CAUSE_MANDATORY_IE_MISS;
		else
			cause = CAUSE_INVALID_CONTENTS;
		l3dss1_msg_without_setup(pc, cause);
		return;
	}
	/* Now we are on none mandatory IEs */
	err = check_infoelements(pc, skb, ie_SETUP);
	if (ERR_IE_COMPREHENSION == err) {
		l3dss1_msg_without_setup(pc, CAUSE_MANDATORY_IE_MISS);
		return;
	}
	pc->para.SETUP.COMPLETE =
		findie(skb->data, skb->len, IE_COMPLETE, 0);
	pc->para.SETUP.FACILITY =
		findie(skb->data, skb->len, IE_FACILITY, 0);
	pc->para.SETUP.PROGRESS =
		findie(skb->data, skb->len, IE_PROGRESS, 0);
	pc->para.SETUP.NET_FAC =
		findie(skb->data, skb->len, IE_NET_FAC, 0);
	pc->para.SETUP.DISPLAY =
		findie(skb->data, skb->len, IE_DISPLAY, 0);
	pc->para.SETUP.SIGNAL =
		findie(skb->data, skb->len, IE_SIGNAL, 0);
	pc->para.SETUP.CALLED_PN =
		findie(skb->data, skb->len, IE_CALLED_PN, 0);
	pc->para.SETUP.CALLED_SUB =
		findie(skb->data, skb->len, IE_CALLED_SUB, 0);
	pc->para.SETUP.CALLING_PN =
		findie(skb->data, skb->len, IE_CALLING_PN, 0);
	pc->para.SETUP.CALLING_SUB =
		findie(skb->data, skb->len, IE_CALLING_SUB, 0);
	pc->para.SETUP.REDIR_NR =
		findie(skb->data, skb->len, IE_REDIR_NR, 0);
	pc->para.SETUP.LLC =
		findie(skb->data, skb->len, IE_LLC, 0);
	pc->para.SETUP.HLC =
		findie(skb->data, skb->len, IE_HLC, 0);
	pc->para.SETUP.USER_USER =
		findie(skb->data, skb->len, IE_USER_USER, 0);
	newl3state(pc, 6);
	L3DelTimer(&pc->timer);
	L3AddTimer(&pc->timer, T_CTRL, CC_TCTRL);
	if (err) /* STATUS for none mandatory IE errors after actions are taken */
		l3dss1_std_ie_err(pc, err);
	if (hisax_l3up(pc, CC_NEW_CR | INDICATION, 4, &pc->id)) {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "cannot register SETUP CR");
		release_l3_process(pc);
	} else
		hisax_l3up(pc, CC_SETUP | INDICATION, sizeof(SETUP_t),
			&pc->para.SETUP);
}

static void
l3dss1_reset(l3_process_t *pc, u_char pr, void *arg)
{
	release_l3_process(pc);
}

static void
l3dss1_release(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret, cause=0;

	StopAllL3Timer(pc);
	if (!(pc->para.RELEASE.CAUSE = l3dss1_get_cause(pc, skb))) {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "REL get_cause ret(%d)", pc->err);
		if ((pc->err<0) && (pc->state != 11))
			cause = CAUSE_MANDATORY_IE_MISS;
		else if (pc->err>0)
			cause = CAUSE_INVALID_CONTENTS;
	}
	ret = check_infoelements(pc, skb, ie_RELEASE);
	if (ERR_IE_COMPREHENSION == ret)
		cause = CAUSE_MANDATORY_IE_MISS;
	else if ((ERR_IE_UNRECOGNIZED == ret) && (!cause))
		cause = CAUSE_IE_NOTIMPLEMENTED;
	pc->para.RELEASE.FACILITY =
		findie(skb->data, skb->len, IE_FACILITY, 0);
	pc->para.RELEASE.DISPLAY =
		findie(skb->data, skb->len, IE_DISPLAY, 0);
	pc->para.RELEASE.SIGNAL =
		findie(skb->data, skb->len, IE_SIGNAL, 0);
	pc->para.RELEASE.USER_USER =
		findie(skb->data, skb->len, IE_USER_USER, 0);
	if (cause)
		l3dss1_message_cause(pc, MT_RELEASE_COMPLETE, cause);
	else
		l3dss1_message(pc, MT_RELEASE_COMPLETE);
	hisax_l3up(pc, CC_RELEASE | INDICATION, sizeof(RELEASE_t), &pc->para.RELEASE);
	newl3state(pc, 0);
	release_l3_process(pc);
}

static void
l3dss1_progress(l3_process_t *pc, u_char pr, void *arg) {
	struct sk_buff *skb = arg;
	int err = 0;
	u_char cause = CAUSE_INVALID_CONTENTS;

	if ((pc->para.PROGRESS.PROGRESS = findie(skb->data, skb->len, IE_PROGRESS, 0))) {
		if (pc->para.PROGRESS.PROGRESS[0] != 2) {
			err = 1;
		} else if (!(pc->para.PROGRESS.PROGRESS[1] & 0x70)) {
			switch (pc->para.PROGRESS.PROGRESS[1]) {
				case 0x80:
				case 0x81:
				case 0x82:
				case 0x84:
				case 0x85:
				case 0x87:
				case 0x8a:
					switch (pc->para.PROGRESS.PROGRESS[2]) {
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
		return;
	}
	/* Now we are on none mandatory IEs */
	pc->para.PROGRESS.BEARER =
		findie(skb->data, skb->len, IE_BEARER, 0);
	pc->para.PROGRESS.CAUSE = l3dss1_get_cause(pc, skb);
	pc->para.PROGRESS.FACILITY =
		findie(skb->data, skb->len, IE_FACILITY, 0);
	pc->para.PROGRESS.DISPLAY =
		findie(skb->data, skb->len, IE_DISPLAY, 0);
	pc->para.PROGRESS.HLC =
		findie(skb->data, skb->len, IE_HLC, 0);
	pc->para.PROGRESS.USER_USER =
		findie(skb->data, skb->len, IE_USER_USER, 0);
	err = check_infoelements(pc, skb, ie_PROGRESS);
	if (err)
		l3dss1_std_ie_err(pc, err);
	if (ERR_IE_COMPREHENSION != err)
		hisax_l3up(pc, CC_PROGRESS | INDICATION, sizeof(PROGRESS_t), &pc->para.PROGRESS);
}

static void
l3dss1_notify(l3_process_t *pc, u_char pr, void *arg) {
	struct sk_buff *skb = arg;
	int err = 0;
	u_char cause = CAUSE_INVALID_CONTENTS;

	if ((pc->para.NOTIFY.NOTIFY = findie(skb->data, skb->len, IE_NOTIFY, 0))) {
		if (pc->para.NOTIFY.NOTIFY[0] != 1) {
			err = 1;
		} else {
			switch (pc->para.NOTIFY.NOTIFY[1]) {
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
		return;
	}
	/* Now we are on none mandatory IEs */
	pc->para.NOTIFY.BEARER =
		findie(skb->data, skb->len, IE_BEARER, 0);
	pc->para.NOTIFY.DISPLAY =
		findie(skb->data, skb->len, IE_DISPLAY, 0);
	err = check_infoelements(pc, skb, ie_NOTIFY);
	if (err)
		l3dss1_std_ie_err(pc, err);
	if (ERR_IE_COMPREHENSION != err)
		hisax_l3up(pc, CC_NOTIFY | INDICATION, sizeof(NOTIFY_t),
			&pc->para.NOTIFY);
}

static void
l3dss1_status_enq(l3_process_t *pc, u_char pr, void *arg) {
	int ret;
	struct sk_buff *skb = arg;

	ret = check_infoelements(pc, skb, ie_STATUS_ENQUIRY);
	pc->para.STATUS_ENQUIRY.DISPLAY =
		findie(skb->data, skb->len, IE_DISPLAY, 0);
	l3dss1_std_ie_err(pc, ret);
	l3dss1_status_send(pc, CAUSE_STATUS_RESPONSE);
	hisax_l3up(pc, CC_STATUS_ENQUIRY | INDICATION,
		sizeof(STATUS_ENQUIRY_t), &pc->para.STATUS_ENQUIRY);
}

static void
l3dss1_information(l3_process_t *pc, u_char pr, void *arg) {
	int ret;
	struct sk_buff *skb = arg;

	pc->para.INFORMATION.COMPLETE =
		findie(skb->data, skb->len, IE_COMPLETE, 0);
	pc->para.INFORMATION.DISPLAY =
		findie(skb->data, skb->len, IE_DISPLAY, 0);
	pc->para.INFORMATION.KEYPAD =
		findie(skb->data, skb->len, IE_KEYPAD, 0);
	pc->para.INFORMATION.SIGNAL =
		findie(skb->data, skb->len, IE_SIGNAL, 0);
	pc->para.INFORMATION.CALLED_PN =
		findie(skb->data, skb->len, IE_CALLED_PN, 0);
	ret = check_infoelements(pc, skb, ie_INFORMATION);
	if (ret)
		l3dss1_std_ie_err(pc, ret);
	if (pc->state == 25) { /* overlap receiving */
		L3DelTimer(&pc->timer);
		L3AddTimer(&pc->timer, T302, CC_T302);
	}
	hisax_l3up(pc, CC_INFORMATION | INDICATION, sizeof(INFORMATION_t),
		&pc->para.INFORMATION);
}

static void
l3dss1_release_ind(l3_process_t *pc, u_char pr, void *arg)
{
	u_char *p;
	struct sk_buff *skb = arg;
	int callState = 0;

	if ((p = findie(skb->data, skb->len, IE_CALL_STATE, 0))) {
		if (1 == *p++)
			callState = *p;
	}
	if (callState == 0) {
		/* ETS 300-104 7.6.1, 8.6.1, 10.6.1... and 16.1
		 * set down layer 3 without sending any message
		 */
		hisax_l3up(pc, CC_RELEASE | INDICATION, 0, NULL);
		newl3state(pc, 0);
		release_l3_process(pc);
	} else {
		hisax_l3up(pc, CC_RELEASE | INDICATION, 0, NULL);
	}
}

static void
l3dss1_restart(l3_process_t *pc, u_char pr, void *arg) {
	L3DelTimer(&pc->timer);
	hisax_l3up(pc, CC_RELEASE | INDICATION, 0, NULL);
	release_l3_process(pc);
}

static void
l3dss1_status(l3_process_t *pc, u_char pr, void *arg) {
	struct sk_buff *skb = arg;
	int ret = 0; 
	u_char cause = 0, callState = 0;
	
	if (!(pc->para.STATUS.CAUSE = l3dss1_get_cause(pc, skb))) {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "STATUS get_cause ret(%d)", pc->err);
		if (pc->err < 0)
			cause = CAUSE_MANDATORY_IE_MISS;
		else if (pc->err > 0)
			cause = CAUSE_INVALID_CONTENTS;
		ret = pc->err;
	}
	if ((pc->para.STATUS.CALL_STATE =
		findie(skb->data, skb->len, IE_CALL_STATE, 0))) {
		if (1 == pc->para.STATUS.CALL_STATE[0]) {
			callState = pc->para.STATUS.CALL_STATE[1];
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
		if (cause != CAUSE_IE_NOTIMPLEMENTED)
			return;
	}
	pc->para.STATUS.DISPLAY =
		findie(skb->data, skb->len, IE_DISPLAY, 0);
	if (pc->para.STATUS.CAUSE)
		cause = pc->err & 0x7f;
	if ((cause == CAUSE_PROTOCOL_ERROR) && (callState == 0)) {
		/* ETS 300-104 7.6.1, 8.6.1, 10.6.1...
		 * if received MT_STATUS with cause == 111 and call
		 * state == 0, then we must set down layer 3
		 */
		hisax_l3up(pc, CC_STATUS| INDICATION, sizeof(STATUS_t),
			&pc->para.STATUS);
		newl3state(pc, 0);
		release_l3_process(pc);
	} else
		hisax_l3up(pc, CC_STATUS | INDICATION, sizeof(STATUS_t),
			&pc->para.STATUS);
}

static void
l3dss1_facility(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;
	
	ret = check_infoelements(pc, skb, ie_FACILITY);
	l3dss1_std_ie_err(pc, ret);
	if (!(pc->para.FACILITY.FACILITY =
		findie(skb->data, skb->len, IE_FACILITY, 0))) {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "FACILITY without IE_FACILITY");
		return;
	}		
	pc->para.FACILITY.DISPLAY =
		findie(skb->data, skb->len, IE_DISPLAY, 0);
	hisax_l3up(pc, CC_FACILITY | INDICATION, sizeof(FACILITY_t),
		&pc->para.FACILITY);
}

static void
l3dss1_suspend_ack(l3_process_t *pc, u_char pr, void *arg) {
	struct sk_buff *skb = arg;
	int ret;

	L3DelTimer(&pc->timer);
	newl3state(pc, 0);
	pc->para.SUSPEND_ACKNOWLEDGE.FACILITY =
		findie(skb->data, skb->len, IE_FACILITY, 0);
	pc->para.SUSPEND_ACKNOWLEDGE.DISPLAY =
		findie(skb->data, skb->len, IE_DISPLAY, 0);
	/* We don't handle suspend_ack for IE errors now */
	if ((ret = check_infoelements(pc, skb, ie_SUSPEND_ACKNOWLEDGE)))
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "SUSPACK check ie(%d)",ret);
	hisax_l3up(pc, CC_SUSPEND_ACKNOWLEDGE | INDICATION,
		sizeof(SUSPEND_ACKNOWLEDGE_t), &pc->para.SUSPEND_ACKNOWLEDGE);
	release_l3_process(pc);
}

static void
l3dss1_suspend_rej(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;
	u_char cause;

	if (!(pc->para.SUSPEND_REJECT.CAUSE = l3dss1_get_cause(pc, skb))) {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "SUSP_REJ get_cause err(%d)",pc->err);
		if (pc->err < 0) 
			cause = CAUSE_MANDATORY_IE_MISS;
		else
			cause = CAUSE_INVALID_CONTENTS;
		l3dss1_status_send(pc, cause);
		return;
	}
	pc->para.SUSPEND_REJECT.DISPLAY =
		findie(skb->data, skb->len, IE_DISPLAY, 0);
	ret = check_infoelements(pc, skb, ie_SUSPEND_REJECT);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		return;
	}
	L3DelTimer(&pc->timer);
	hisax_l3up(pc, CC_SUSPEND_REJECT | INDICATION,
		sizeof(SUSPEND_REJECT_t), &pc->para.SUSPEND_REJECT);
	newl3state(pc, 10);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3dss1_std_ie_err(pc, ret);
}

static void
l3dss1_resume_ack(l3_process_t *pc, u_char pr, void *arg)
{
	struct sk_buff *skb = arg;
	int ret;

	if ((pc->para.RESUME_ACKNOWLEDGE.CHANNEL_ID = l3dss1_get_channel_id(pc, skb))) {
		if ((0 == pc->bc) || (3 == pc->bc)) {
			if (pc->l3->debug & L3_DEB_WARN)
				l3_debug(pc->l3, "resume ack with wrong chid %x",
					pc->bc);
			l3dss1_status_send(pc, CAUSE_INVALID_CONTENTS);
			return;
		}
	} else if (1 == pc->state) {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "resume ack without chid err(%d)",
				pc->err);
		l3dss1_status_send(pc, CAUSE_MANDATORY_IE_MISS);
		return;
	}
	ret = check_infoelements(pc, skb, ie_RESUME_ACKNOWLEDGE);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		return;
	}
	pc->para.RESUME_ACKNOWLEDGE.FACILITY =
		findie(skb->data, skb->len, IE_FACILITY, 0);
	pc->para.RESUME_ACKNOWLEDGE.DISPLAY =
		findie(skb->data, skb->len, IE_DISPLAY, 0);
	L3DelTimer(&pc->timer);
	hisax_l3up(pc, CC_RESUME_ACKNOWLEDGE | INDICATION,
		sizeof(RESUME_ACKNOWLEDGE_t), &pc->para.RESUME_ACKNOWLEDGE);
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

	if (!(pc->para.RESUME_REJECT.CAUSE = l3dss1_get_cause(pc, skb))) {
		if (pc->l3->debug & L3_DEB_WARN)
			l3_debug(pc->l3, "RES_REJ get_cause err(%d)",pc->err);
		if (pc->err < 0) 
			cause = CAUSE_MANDATORY_IE_MISS;
		else
			cause = CAUSE_INVALID_CONTENTS;
		l3dss1_status_send(pc, cause);
		return;
	}
	pc->para.RESUME_REJECT.DISPLAY =
		findie(skb->data, skb->len, IE_DISPLAY, 0);
	ret = check_infoelements(pc, skb, ie_RESUME_REJECT);
	if (ERR_IE_COMPREHENSION == ret) {
		l3dss1_std_ie_err(pc, ret);
		return;
	}
	L3DelTimer(&pc->timer);
	hisax_l3up(pc, CC_RESUME_REJECT | INDICATION, sizeof(RESUME_REJECT_t),
		&pc->para.RESUME_REJECT);
	newl3state(pc, 0);
	if (ret) /* STATUS for none mandatory IE errors after actions are taken */
		l3dss1_std_ie_err(pc, ret);
	release_l3_process(pc);
}

static void
l3dss1_global_restart(l3_process_t *pc, u_char pr, void *arg)
{
	u_char *p, ri, ch = 0, chan = 0;
	struct sk_buff *skb = arg;
	l3_process_t *up;

	newl3state(pc, 2);
	L3DelTimer(&pc->timer);
	if ((p = findie(skb->data, skb->len, IE_RESTART_IND, 0))) {
		ri = p[1];
		l3_debug(pc->l3, "Restart %x", ri);
	} else {
		l3_debug(pc->l3, "Restart without restart IE");
		ri = 0x86;
	}
	if ((p = findie(skb->data, skb->len, IE_CHANNEL_ID, 0))) {
		chan = p[1] & 3;
		ch = p[1];
		if (pc->l3->debug)
			l3_debug(pc->l3, "Restart for channel %d", chan);
	}
	newl3state(pc, 2);
	up = pc->l3->proc;
	while (up) {
		if ((ri & 7) == 7)
			dss1man(up, CC_RESTART | REQUEST, NULL);
		else if (up->bc == chan)
			hisax_l3up(up, CC_RESTART | REQUEST, 0, NULL);
		up = up->next;
	}
	MsgStart(pc, MT_RESTART_ACKNOWLEDGE);
	if (chan) {
		*pc->op++ = IE_CHANNEL_ID;
		*pc->op++ = 1;
		*pc->op++ = ch | 0x80;
	}
	*pc->op++ = IE_RESTART_IND;
	*pc->op++ = 1;
	*pc->op++ = ri;
	SendMsg(pc, 0);
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
	hisax_l3up(pc, CC_TIMEOUT | INDICATION, 0, NULL);
}

static void
l3dss1_t303(l3_process_t *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	if (pc->n303 > 0) {
		pc->n303--;
		if (pc->obuf[3] == MT_SETUP) {
			if (!SendMsg(pc, -1)) {
				L3AddTimer(&pc->timer, T303, CC_T303);
				return;
			}
		}
	}
	l3dss1_message_cause(pc, MT_RELEASE_COMPLETE, CAUSE_TIMER_EXPIRED);
	hisax_l3up(pc, CC_TIMEOUT | INDICATION, 0, NULL);
	release_l3_process(pc);
}

static void
l3dss1_t304(l3_process_t *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	newl3state(pc, 11);
	l3dss1_message_cause(pc, MT_DISCONNECT, CAUSE_TIMER_EXPIRED);
	hisax_l3up(pc, CC_TIMEOUT | INDICATION, 0, NULL);
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
	hisax_l3up(pc, CC_TIMEOUT | INDICATION, 0, NULL);
}

static void
l3dss1_t313(l3_process_t *pc, u_char pr, void *arg)
{
	L3DelTimer(&pc->timer);
	newl3state(pc, 11);
	l3dss1_message_cause(pc, MT_DISCONNECT, CAUSE_TIMER_EXPIRED);
	hisax_l3up(pc, CC_TIMEOUT | INDICATION, 0, NULL);
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
	hisax_l3up(pc, CC_TIMEOUT | INDICATION, 0, NULL);
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
	hisax_l3up(pc, CC_RESUME_REJECT | INDICATION, 0, NULL);
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
	hisax_l3up(pc, CC_SUSPEND_REJECT | INDICATION, 0, NULL);
	newl3state(pc, 10);
}

static void
l3dss1_dl_reset(l3_process_t *pc, u_char pr, void *arg)
{
	DISCONNECT_t disc;
	u_char cause[4];

	memset(&disc, 0, sizeof(DISCONNECT_t));
	disc.CAUSE = cause;
	cause[0] = 2;
	cause[1] = 0x80 | CAUSE_LOC_USER;
	cause[2] = 0x80 | CAUSE_TEMPORARY_FAILURE;
	l3dss1_disconnect_req(pc, pr, &disc);
	hisax_l3up(pc, CC_DISCONNECT | REQUEST, sizeof(DISCONNECT_t), &disc);
}

static void
l3dss1_dl_release(l3_process_t *pc, u_char pr, void *arg)
{
	newl3state(pc, 0);
#if 0
        pc->cause = 0x1b;          /* Destination out of order */
        pc->para.loc = 0;
#endif
	hisax_l3up(pc, DL_RELEASE | INDICATION, 0, NULL);
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
	{SBIT(0),
	 CC_RESUME | REQUEST, l3dss1_resume_req},
	{SBIT(1) | SBIT(2) | SBIT(3) | SBIT(4) | SBIT(6) | SBIT(7) |
		SBIT(8) | SBIT(9) | SBIT(10) | SBIT(25),
	 CC_DISCONNECT | REQUEST, l3dss1_disconnect_req},
	{SBIT(12),
	 CC_RELEASE | REQUEST, l3dss1_release_req},
	{ALL_STATES,
	 CC_RESTART | REQUEST, l3dss1_restart},
	{SBIT(6) | SBIT(25),
	 CC_SETUP | RESPONSE, l3dss1_release_cmpl_req},
	{SBIT(6) | SBIT(25),
	 CC_PROCEED_SEND | REQUEST, l3dss1_proceed_req},
	{SBIT(6),
	 CC_INFO | REQUEST, l3dss1_setup_ack_req},
	{SBIT(25),
	 CC_INFO | REQUEST, l3dss1_dummy},
	{SBIT(6) | SBIT(9) | SBIT(25),
	 CC_ALERTING | REQUEST, l3dss1_alert_req},
	{SBIT(6) | SBIT(7) | SBIT(9) | SBIT(25),
	 CC_CONNECT | REQUEST, l3dss1_connect_req},
	{SBIT(10),
	 CC_SUSPEND | REQUEST, l3dss1_suspend_req},
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
global_handler(layer3_t *l3, int mt, struct sk_buff *skb)
{
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
		l3dss1_status_send(proc, CAUSE_INVALID_CALLREF);
	} else {
		if (l3->debug & L3_DEB_STATE) {
			l3_debug(l3, "dss1 global %d mt %x",
				proc->state, mt);
		}
		globalmes_list[i].rout(proc, mt, skb);
	}
}

static int
dss1_fromdown(hisaxif_t *hif, u_int prim, int dinfo, int len, void *arg) {
	layer3_t *l3;
	int i, mt, cr, cause, callState;
	char *ptr;
	struct sk_buff *skb = arg;
	l3_process_t *proc;

	if (!hif || !hif->fdata)
		return(-EINVAL);
	l3 = hif->fdata;
	switch (prim) {
		case (DL_DATA | INDICATION):
		case (DL_UNITDATA | INDICATION):
			break;
		case (DL_ESTABLISH | CONFIRM):
		case (DL_ESTABLISH | INDICATION):
		case (DL_RELEASE | INDICATION):
		case (DL_RELEASE | CONFIRM):
			l3_msg(l3, prim, dinfo, len, arg);
			return(0);
			break;
                default:
                        printk(KERN_WARNING
                        	"HiSax dss1up unknown pr=%04x\n", prim);
                        return(-EINVAL);
	}
	if ((dinfo != DINFO_SKB) || !arg) {
		printk(KERN_WARNING
			"HiSax dss1_fromdown prim %x dinfo %x skb %p\n",
			prim, dinfo, arg);
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
				 (prim == (DL_DATA | INDICATION)) ? " " : "(broadcast) ",
				 skb->data[0], skb->len);
		}
		dev_kfree_skb(skb);
		return(0);
	}
	cr = getcallref(skb->data);
	if (skb->len < ((skb->data[1] & 0x0f) + 3)) {
		l3_debug(l3, "dss1up frame too short(%d)", skb->len);
		dev_kfree_skb(skb);
		return(0);
	}
	mt = skb->data[skb->data[1] + 2];
	if (l3->debug & L3_DEB_STATE)
		l3_debug(l3, "dss1up cr %d", cr);
	if (cr == -2) {  /* wrong Callref */
		if (l3->debug & L3_DEB_WARN)
			l3_debug(l3, "dss1up wrong Callref");
		dev_kfree_skb(skb);
		return(0);
	} else if (cr == -1) {	/* Dummy Callref */
		if (mt == MT_FACILITY)
			l3dss1_facility(l3->dummy, prim, skb);
		else if (l3->debug & L3_DEB_WARN)
			l3_debug(l3, "dss1up dummy Callref (no facility msg or ie)");
		dev_kfree_skb(skb);
		return(0);
	} else if ((((skb->data[1] & 0x0f) == 1) && (0==(cr & 0x7f))) ||
		(((skb->data[1] & 0x0f) == 2) && (0==(cr & 0x7fff)))) {	/* Global CallRef */
		if (l3->debug & L3_DEB_STATE)
			l3_debug(l3, "dss1up Global CallRef");
		global_handler(l3, mt, skb);
		dev_kfree_skb(skb);
		return(0);
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
				return(0);
			}
			if (!(proc = new_l3_process(l3, cr, N303))) {
				/* May be to answer with RELEASE_COMPLETE and
				 * CAUSE 0x2f "Resource unavailable", but this
				 * need a new_l3_process too ... arghh
				 */
				dev_kfree_skb(skb);
				return(0);
			}
		} else if (mt == MT_STATUS) {
			cause = 0;
			if ((ptr = findie(skb->data, skb->len, IE_CAUSE, 0)) != NULL) {
				if (*ptr++ == 2)
					ptr++;
				cause = *ptr & 0x7f;
			}
			callState = 0;
			if ((ptr = findie(skb->data, skb->len, IE_CALL_STATE, 0)) != NULL) {
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
				if ((proc = new_l3_process(l3, cr, N303))) {
					l3dss1_msg_without_setup(proc,
						CAUSE_NOTCOMPAT_STATE);
				}
			}
			dev_kfree_skb(skb);
			return(0);
		} else if (mt == MT_RELEASE_COMPLETE) {
			dev_kfree_skb(skb);
			return(0);
		} else {
			/* ETS 300-104 part 2
			 * if setup has not been made and a message type
			 * (except MT_SETUP and RELEASE_COMPLETE) is received,
			 * we must send MT_RELEASE_COMPLETE cause 81 */
			dev_kfree_skb(skb);
			if ((proc = new_l3_process(l3, cr, N303))) {
				l3dss1_msg_without_setup(proc,
					CAUSE_INVALID_CALLREF);
			}
			return(0);
		}
	}
	if (l3dss1_check_messagetype_validity(proc, mt, skb)) {
		dev_kfree_skb(skb);
		return(0);
	}
	for (i = 0; i < DATASLLEN; i++)
		if ((mt == datastatelist[i].primitive) &&
		    ((1 << proc->state) & datastatelist[i].state))
			break;
	if (i == DATASLLEN) {
		if (l3->debug & L3_DEB_STATE) {
			l3_debug(l3, "dss1up%sstate %d mt %#x unhandled",
				(prim == (DL_DATA | INDICATION)) ? " " : "(broadcast) ",
				proc->state, mt);
		}
		if ((MT_RELEASE_COMPLETE != mt) && (MT_RELEASE != mt)) {
			l3dss1_status_send(proc, CAUSE_NOTCOMPAT_STATE);
		}
	} else {
		if (l3->debug & L3_DEB_STATE) {
			l3_debug(l3, "dss1up%sstate %d mt %x",
				(prim == (DL_DATA | INDICATION)) ? " " : "(broadcast) ",
				proc->state, mt);
		}
		datastatelist[i].rout(proc, prim, skb);
	}
	dev_kfree_skb(skb);
	return(0);
}

static int
dss1_fromup(hisaxif_t *hif, u_int prim, int dinfo, int len, void *arg) {
	layer3_t *l3;
	int i, cr;
	l3_process_t *proc;

	if (!hif || !hif->fdata)
		return(-EINVAL);
	l3 = hif->fdata;
	if ((DL_ESTABLISH | REQUEST) == prim) {
		l3_msg(l3, prim, 0, 0, NULL);
		return(0);
	}
	if ((CC_NEW_CR | REQUEST) == prim) {
		cr = newcallref();
		cr |= 0x80;
		if ((proc = new_l3_process(l3, cr, N303))) {
			proc->id = dinfo;
			return(0);
		}
		return(-ENOMEM);
	} else {
		proc = getl3proc4id(l3, dinfo);
	}
	if (!proc) {
		printk(KERN_ERR "HiSax dss1 fromup without proc pr=%04x\n", prim);
		return(-EINVAL);
	}
	for (i = 0; i < DOWNSLLEN; i++)
		if ((prim == downstatelist[i].primitive) &&
		    ((1 << proc->state) & downstatelist[i].state))
			break;
	if (i == DOWNSLLEN) {
		if (l3->debug & L3_DEB_STATE) {
			l3_debug(l3, "dss1down state %d prim %#x unhandled",
				proc->state, prim);
		}
	} else {
		if (l3->debug & L3_DEB_STATE) {
			l3_debug(l3, "dss1down state %d prim %#x",
				proc->state, prim);
		}
		downstatelist[i].rout(proc, prim, arg);
	}
	return(0);
}

static int
dss1man(l3_process_t *proc, u_int pr, void *arg)
{
	int i;
 
	if (!proc) {
		printk(KERN_ERR "HiSax dss1man without proc pr=%04x\n", pr);
		return(-EINVAL);
	}
	for (i = 0; i < MANSLLEN; i++)
		if ((pr == manstatelist[i].primitive) &&
			((1 << proc->state) & manstatelist[i].state))
			break;
		if (i == MANSLLEN) {
			if (proc->l3->debug & L3_DEB_STATE) {
				l3_debug(proc->l3, "cr %d dss1man state %d prim %#x unhandled",
					proc->callref & 0x7f, proc->state, pr);
			}
		} else {
			if (proc->l3->debug & L3_DEB_STATE) {
				l3_debug(proc->l3, "cr %d dss1man state %d prim %#x",
					proc->callref & 0x7f, proc->state, pr);
			}
			manstatelist[i].rout(proc, pr, arg);
	}
	return(0);
}

static void
release_udss1(layer3_t *l3)
{
	hisaxinstance_t  *inst = &l3->inst;
	hisaxif_t	hif;

	printk(KERN_DEBUG "release_udss1 refcnt %d l3(%p) inst(%p)\n",
		u_dss1.refcnt, l3, inst);
	release_l3(l3);
	memset(&hif, 0, sizeof(hisaxif_t));
	hif.fdata = l3;
	hif.func = dss1_fromup;
	hif.protocol = inst->up.protocol;
	hif.layermask = inst->up.layermask;
	u_dss1.ctrl(inst->st, MGR_DELIF | REQUEST, &hif);
	hif.fdata = l3;
	hif.func = dss1_fromdown;
	hif.protocol = inst->down.protocol;
	hif.layermask = inst->down.layermask;
	u_dss1.ctrl(inst->st, MGR_DELIF | REQUEST, &hif);
	REMOVE_FROM_LISTBASE(l3, dss1list);
	u_dss1.ctrl(inst->st, MGR_DELLAYER | REQUEST, inst);
	kfree(l3);
}

static layer3_t *
create_udss1(hisaxstack_t *st, hisaxif_t *hif) {
	layer3_t *nl3;
	int err, lay;

	if (!hif)
		return(NULL);
	printk(KERN_DEBUG "create_udss1 prot %x\n", hif->protocol);
	if (!st) {
		printk(KERN_ERR "create_udss1 no stack\n");
		return(NULL);
	}
	lay = layermask2layer(hif->layermask);
	if (lay < 0) {
		int_errtxt("lm %x", hif->layermask);
		return(NULL);
	}
	if (!(nl3 = kmalloc(sizeof(layer3_t), GFP_ATOMIC))) {
		printk(KERN_ERR "kmalloc layer3 failed\n");
		return(NULL);
	}
	memset(nl3, 0, sizeof(layer3_t));
	nl3->debug = debug;
	if (hif->protocol != ISDN_PID_L3_DSS1USER) {
		printk(KERN_ERR "udss1 create failed prt %x\n",hif->protocol);
		kfree(nl3);
		return(NULL);
	}
	init_l3(nl3);
	if (!(nl3->global = kmalloc(sizeof(l3_process_t), GFP_ATOMIC))) {
		printk(KERN_ERR "HiSax can't get memory for dss1 global CR\n");
		release_l3(nl3);
		kfree(nl3);
		return(NULL);
	} else {
		nl3->global->state = 0;
		nl3->global->callref = 0;
		nl3->global->next = NULL;
		nl3->global->n303 = N303;
		nl3->global->l3 = nl3;
		L3InitTimer(nl3->global, &nl3->global->timer);
	}
	if (!(nl3->dummy = kmalloc(sizeof(l3_process_t), GFP_ATOMIC))) {
		printk(KERN_ERR "HiSax can't get memory for dss1 dummy CR\n");
		release_l3(nl3);
		kfree(nl3);
		return(NULL);
	} else {
		nl3->dummy->state = 0;
		nl3->dummy->callref = -1;
		nl3->dummy->next = NULL;
		nl3->dummy->n303 = N303;
		nl3->dummy->l3 = nl3;
		L3InitTimer(nl3->dummy, &nl3->dummy->timer);
	}
	nl3->inst.pid.protocol[lay] = hif->protocol;
	nl3->inst.obj = &u_dss1;
	nl3->inst.layermask = hif->layermask;
	nl3->inst.data = nl3;
	nl3->p_mgr = dss1man;
	APPEND_TO_LIST(nl3, dss1list);
	u_dss1.ctrl(st, MGR_ADDLAYER | INDICATION, &nl3->inst);
	nl3->inst.up.layermask = get_up_layer(nl3->inst.layermask);
	nl3->inst.up.protocol = get_protocol(st, nl3->inst.up.layermask);
	nl3->inst.up.stat = IF_DOWN;
	nl3->inst.down.layermask = get_down_layer(nl3->inst.layermask);
	nl3->inst.down.protocol = get_protocol(st, nl3->inst.down.layermask);
	nl3->inst.down.stat = IF_UP;
	err = u_dss1.ctrl(st, MGR_ADDIF | REQUEST, &nl3->inst.down);
	if (err) {
		release_udss1(nl3);
		printk(KERN_ERR "udss1 down interface request failed %d\n", err);
		return(NULL);
	}
	err = u_dss1.ctrl(st, MGR_ADDIF | REQUEST, &nl3->inst.up);
	if (err) {
		release_udss1(nl3);
		printk(KERN_ERR "udss1 up interface request failed %d\n", err);
		return(NULL);
	}
	return(nl3);
}

static int
add_if(layer3_t *l3, hisaxif_t *hif) {
	int err;
	hisaxinstance_t *inst = &l3->inst;

	printk(KERN_DEBUG "layer3 add_if lay %x/%x prot %x\n", hif->layermask,
		hif->stat, hif->protocol);
	hif->fdata = l3;
	if (IF_TYPE(hif) == IF_UP) {
		hif->func = dss1_fromup;
		if (inst->up.stat == IF_NOACTIV) {
			inst->up.stat = IF_DOWN;
			inst->up.protocol = get_protocol(inst->st, inst->up.layermask);
			err = u_dss1.ctrl(inst->st, MGR_ADDIF | REQUEST, &inst->up);
			if (err)
				inst->up.stat = IF_NOACTIV;
		}
	} else if (IF_TYPE(hif) == IF_DOWN) {
		hif->func = dss1_fromdown;
		if (inst->down.stat == IF_NOACTIV) {
			inst->down.stat = IF_UP;
			inst->down.protocol = get_protocol(inst->st, inst->down.layermask);
			err = u_dss1.ctrl(inst->st, MGR_ADDIF | REQUEST, &inst->down);
			if (err)
				inst->down.stat = IF_NOACTIV;
		}
	} else
		return(-EINVAL);
	return(0);
}

static int
del_if(layer3_t *l3, hisaxif_t *hif) {
	int err;
	hisaxinstance_t *inst = &l3->inst;

	printk(KERN_DEBUG "layer3 del_if lay %x/%x %p/%p\n", hif->layermask,
		hif->stat, hif->func, hif->fdata);
	if ((hif->func == inst->up.func) && (hif->fdata == inst->up.fdata)) {
		inst->up.stat = IF_NOACTIV;
		inst->up.protocol = ISDN_PID_NONE;
		err = u_dss1.ctrl(inst->st, MGR_ADDIF | REQUEST, &inst->up);
	} else if ((hif->func == inst->down.func) && (hif->fdata == inst->down.fdata)) {
		inst->down.stat = IF_NOACTIV;
		inst->down.protocol = ISDN_PID_NONE;
		err = u_dss1.ctrl(inst->st, MGR_ADDIF | REQUEST, &inst->down);
	} else {
		printk(KERN_DEBUG "layer3 del_if no if found\n");
		return(-EINVAL);
	}
	return(0);
}

static char MName[] = "UDSS1";

#ifdef MODULE
MODULE_AUTHOR("Karsten Keil");
MODULE_PARM(debug, "1i");
#define UDSS1Init init_module
#endif

static int
udss1_manager(void *data, u_int prim, void *arg) {
	hisaxstack_t *st = data;
	layer3_t *l3l = dss1list;

	printk(KERN_DEBUG "udss1_manager data:%p prim:%x arg:%p\n", data, prim, arg);
	if (!data)
		return(-EINVAL);
	while(l3l) {
		if (l3l->inst.st == st)
			break;
		l3l = l3l->next;
	}
	switch(prim) {
	    case MGR_ADDIF | REQUEST:
		if (!l3l)
			l3l = create_udss1(st, arg);
		if (!l3l) {
			printk(KERN_WARNING "udss1_manager create_udss1 failed\n");
			return(-EINVAL);
		}
		return(add_if(l3l, arg));
		break;
	    case MGR_DELIF | REQUEST:
		if (!l3l) {
			printk(KERN_WARNING "udss1_manager delif no instance\n");
			return(-EINVAL);
		}
		return(del_if(l3l, arg));
		break;
	    case MGR_RELEASE | INDICATION:
	    case MGR_DELLAYER | REQUEST:
	    	if (l3l) {
			printk(KERN_DEBUG "release_udss1 id %x\n", l3l->inst.st->id);
	    		release_udss1(l3l);
	    	} else 
	    		printk(KERN_WARNING "udss1_manager release no instance\n");
	    	break;
	    		
	    default:
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
	printk(KERN_INFO "HiSax: DSS1 Rev. %s\n", HiSax_getrev(tmp));
	u_dss1.name = MName;
	u_dss1.DPROTO.protocol[3] = ISDN_PID_L3_DSS1USER;
	u_dss1.own_ctrl = udss1_manager;
	u_dss1.prev = NULL;
	u_dss1.next = NULL;
	HiSaxl3New();
	if ((err = HiSax_register(&u_dss1))) {
		printk(KERN_ERR "Can't register %s error(%d)\n", MName, err);
		HiSaxl3Free();
	}
	return(err);
}

#ifdef MODULE
void cleanup_module(void)
{
	int err;

	if ((err = HiSax_unregister(&u_dss1))) {
		printk(KERN_ERR "Can't unregister User DSS1 error(%d)\n", err);
	}
	if (dss1list) {
		printk(KERN_WARNING "hisaxl3 u_dss1list not empty\n");
		while(dss1list)
			release_udss1(dss1list);
	}
	HiSaxl3Free();
}
#endif

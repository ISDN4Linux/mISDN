/* $Id: tei.c,v 1.8 2004/01/26 22:21:30 keil Exp $
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 *		This file is (c) under GNU PUBLIC LICENSE
 *		For changes and modifications please read
 *		../../../Documentation/isdn/mISDN.cert
 *
 */
#include "layer2.h"
#include "helper.h"
#include "debug.h"
#include <linux/random.h>

const char *tei_revision = "$Revision: 1.8 $";

#define ID_REQUEST	1
#define ID_ASSIGNED	2
#define ID_DENIED	3
#define ID_CHK_REQ	4
#define ID_CHK_RES	5
#define ID_REMOVE	6
#define ID_VERIFY	7

#define TEI_ENTITY_ID	0xf

static
struct Fsm teifsm =
{NULL, 0, 0, NULL, NULL};

enum {
	ST_TEI_NOP,
	ST_TEI_IDREQ,
	ST_TEI_IDVERIFY,
};

#define TEI_STATE_COUNT (ST_TEI_IDVERIFY+1)

static char *strTeiState[] =
{
	"ST_TEI_NOP",
	"ST_TEI_IDREQ",
	"ST_TEI_IDVERIFY",
};

enum {
	EV_IDREQ,
	EV_ASSIGN,
	EV_ASSIGN_REQ,
	EV_DENIED,
	EV_CHKREQ,
	EV_REMOVE,
	EV_VERIFY,
	EV_T202,
};

#define TEI_EVENT_COUNT (EV_T202+1)

static char *strTeiEvent[] =
{
	"EV_IDREQ",
	"EV_ASSIGN",
	"EV_ASSIGN_REQ",
	"EV_DENIED",
	"EV_CHKREQ",
	"EV_REMOVE",
	"EV_VERIFY",
	"EV_T202",
};

unsigned int
random_ri(void)
{
	unsigned int x;

	get_random_bytes(&x, sizeof(x));
	return (x & 0xffff);
}

static teimgr_t *
findtei(teimgr_t *tm, int tei)
{
	static teimgr_t *ptr = NULL;
	struct sk_buff *skb;

	if (tei == 127)
		return (NULL);
	skb = create_link_skb(MDL_FINDTEI | REQUEST, tei, sizeof(void), &ptr, 0);
	if (!skb)
		return (NULL);
	if (!tei_l2(tm->l2, skb))
		return(ptr);
	dev_kfree_skb(skb);
	return (NULL);
}

static void
put_tei_msg(teimgr_t *tm, u_char m_id, unsigned int ri, u_char tei)
{
	struct sk_buff *skb;
	u_char bp[8];

	bp[0] = (TEI_SAPI << 2);
	if (test_bit(FLG_LAPD_NET, &tm->l2->flag))
		bp[0] |= 2; /* CR:=1 for net command */
	bp[1] = (GROUP_TEI << 1) | 0x1;
	bp[2] = UI;
	bp[3] = TEI_ENTITY_ID;
	bp[4] = ri >> 8;
	bp[5] = ri & 0xff;
	bp[6] = m_id;
	bp[7] = (tei << 1) | 1;
	skb = create_link_skb(MDL_UNITDATA | REQUEST, 0, 8, bp, 0);
	if (!skb) {
		printk(KERN_WARNING "mISDN: No skb for TEI manager\n");
		return;
	}
	if (tei_l2(tm->l2, skb))
		dev_kfree_skb(skb);
}

static void
tei_id_request(struct FsmInst *fi, int event, void *arg)
{
	teimgr_t *tm = fi->userdata;

	if (tm->l2->tei != -1) {
		tm->tei_m.printdebug(&tm->tei_m,
			"assign request for allready assigned tei %d",
			tm->l2->tei);
		return;
	}
	tm->ri = random_ri();
	if (tm->debug)
		tm->tei_m.printdebug(&tm->tei_m,
			"assign request ri %d", tm->ri);
	put_tei_msg(tm, ID_REQUEST, tm->ri, 127);
	mISDN_FsmChangeState(fi, ST_TEI_IDREQ);
	mISDN_FsmAddTimer(&tm->t202, tm->T202, EV_T202, NULL, 1);
	tm->N202 = 3;
}

static void
tei_assign_req(struct FsmInst *fi, int event, void *arg)
{
	teimgr_t *tm = fi->userdata;
	u_char *dp = arg;

	if (tm->l2->tei == -1) {
		tm->tei_m.printdebug(&tm->tei_m,
			"net tei assign request without tei");
		return;
	}
	tm->ri = ((unsigned int) *dp++ << 8);
	tm->ri += *dp++;
	if (tm->debug)
		tm->tei_m.printdebug(&tm->tei_m,
			"net assign request ri %d teim %d", tm->ri, *dp);
	put_tei_msg(tm, ID_ASSIGNED, tm->ri, tm->l2->tei);
	mISDN_FsmChangeState(fi, ST_TEI_NOP);
}

static void
tei_id_assign(struct FsmInst *fi, int event, void *arg)
{
	teimgr_t *otm, *tm = fi->userdata;
	struct sk_buff *skb;
	u_char *dp = arg;
	int ri, tei;

	ri = ((unsigned int) *dp++ << 8);
	ri += *dp++;
	dp++;
	tei = *dp >> 1;
	if (tm->debug)
		tm->tei_m.printdebug(fi, "identity assign ri %d tei %d",
			ri, tei);
	if ((otm = findtei(tm, tei))) {	/* same tei is in use */
		if (ri != otm->ri) {
			tm->tei_m.printdebug(fi,
				"possible duplicate assignment tei %d", tei);
			skb = create_link_skb(MDL_ERROR | RESPONSE, 0, 0,
				NULL, 0);
			if (!skb)
				return;
			if (tei_l2(otm->l2, skb))
				dev_kfree_skb(skb);
		}
	} else if (ri == tm->ri) {
		mISDN_FsmDelTimer(&tm->t202, 1);
		mISDN_FsmChangeState(fi, ST_TEI_NOP);
		skb = create_link_skb(MDL_ASSIGN | REQUEST, tei, 0, NULL, 0);
		if (!skb)
			return;
		if (tei_l2(tm->l2, skb))
			dev_kfree_skb(skb);
//		cs->cardmsg(cs, MDL_ASSIGN | REQUEST, NULL);
	}
}

static void
tei_id_test_dup(struct FsmInst *fi, int event, void *arg)
{
	teimgr_t *otm, *tm = fi->userdata;
	u_char *dp = arg;
	int tei, ri;

	ri = ((unsigned int) *dp++ << 8);
	ri += *dp++;
	dp++;
	tei = *dp >> 1;
	if (tm->debug)
		tm->tei_m.printdebug(fi, "foreign identity assign ri %d tei %d",
			ri, tei);
	if ((otm = findtei(tm, tei))) {	/* same tei is in use */
		if (ri != otm->ri) {	/* and it wasn't our request */
			tm->tei_m.printdebug(fi,
				"possible duplicate assignment tei %d", tei);
			mISDN_FsmEvent(&otm->tei_m, EV_VERIFY, NULL);
		}
	} 
}

static void
tei_id_denied(struct FsmInst *fi, int event, void *arg)
{
	teimgr_t *tm = fi->userdata;
	u_char *dp = arg;
	int ri, tei;

	ri = ((unsigned int) *dp++ << 8);
	ri += *dp++;
	dp++;
	tei = *dp >> 1;
	if (tm->debug)
		tm->tei_m.printdebug(fi, "identity denied ri %d tei %d",
			ri, tei);
}

static void
tei_id_chk_req(struct FsmInst *fi, int event, void *arg)
{
	teimgr_t *tm = fi->userdata;
	u_char *dp = arg;
	int tei;

	tei = *(dp+3) >> 1;
	if (tm->debug)
		tm->tei_m.printdebug(fi, "identity check req tei %d", tei);
	if ((tm->l2->tei != -1) && ((tei == GROUP_TEI) || (tei == tm->l2->tei))) {
		mISDN_FsmDelTimer(&tm->t202, 4);
		mISDN_FsmChangeState(&tm->tei_m, ST_TEI_NOP);
		put_tei_msg(tm, ID_CHK_RES, random_ri(), tm->l2->tei);
	}
}

static void
tei_id_remove(struct FsmInst *fi, int event, void *arg)
{
	teimgr_t *tm = fi->userdata;
	u_char *dp = arg;
	struct sk_buff *skb;
	int tei;

	tei = *(dp+3) >> 1;
	if (tm->debug)
		tm->tei_m.printdebug(fi, "identity remove tei %d", tei);
	if ((tm->l2->tei != -1) && ((tei == GROUP_TEI) || (tei == tm->l2->tei))) {
		mISDN_FsmDelTimer(&tm->t202, 5);
		mISDN_FsmChangeState(&tm->tei_m, ST_TEI_NOP);
		skb = create_link_skb(MDL_REMOVE | REQUEST, 0, 0, NULL, 0);
		if (!skb)
			return;
		if (tei_l2(tm->l2, skb))
			dev_kfree_skb(skb);
//		cs->cardmsg(cs, MDL_REMOVE | REQUEST, NULL);
	}
}

static void
tei_id_verify(struct FsmInst *fi, int event, void *arg)
{
	teimgr_t *tm = fi->userdata;

	if (tm->debug)
		tm->tei_m.printdebug(fi, "id verify request for tei %d",
			tm->l2->tei);
	put_tei_msg(tm, ID_VERIFY, 0, tm->l2->tei);
	mISDN_FsmChangeState(&tm->tei_m, ST_TEI_IDVERIFY);
	mISDN_FsmAddTimer(&tm->t202, tm->T202, EV_T202, NULL, 2);
	tm->N202 = 2;
}

static void
tei_id_req_tout(struct FsmInst *fi, int event, void *arg)
{
	teimgr_t *tm = fi->userdata;
	struct sk_buff *skb;

	if (--tm->N202) {
		tm->ri = random_ri();
		if (tm->debug)
			tm->tei_m.printdebug(fi, "assign req(%d) ri %d",
				4 - tm->N202, tm->ri);
		put_tei_msg(tm, ID_REQUEST, tm->ri, 127);
		mISDN_FsmAddTimer(&tm->t202, tm->T202, EV_T202, NULL, 3);
	} else {
		tm->tei_m.printdebug(fi, "assign req failed");
		skb = create_link_skb(MDL_ERROR | REQUEST, 0, 0, NULL, 0);
		if (!skb)
			return;
		if (tei_l2(tm->l2, skb))
			dev_kfree_skb(skb);
//		cs->cardmsg(cs, MDL_REMOVE | REQUEST, NULL);
		mISDN_FsmChangeState(fi, ST_TEI_NOP);
	}
}

static void
tei_id_ver_tout(struct FsmInst *fi, int event, void *arg)
{
	teimgr_t *tm = fi->userdata;
	struct sk_buff *skb;

	if (--tm->N202) {
		if (tm->debug)
			tm->tei_m.printdebug(fi,
				"id verify req(%d) for tei %d",
				3 - tm->N202, tm->l2->tei);
		put_tei_msg(tm, ID_VERIFY, 0, tm->l2->tei);
		mISDN_FsmAddTimer(&tm->t202, tm->T202, EV_T202, NULL, 4);
	} else {
		tm->tei_m.printdebug(fi, "verify req for tei %d failed",
			tm->l2->tei);
		skb = create_link_skb(MDL_REMOVE | REQUEST, 0, 0, NULL, 0);
		if (!skb)
			return;
		if (tei_l2(tm->l2, skb))
			dev_kfree_skb(skb);
//		cs->cardmsg(cs, MDL_REMOVE | REQUEST, NULL);
		mISDN_FsmChangeState(fi, ST_TEI_NOP);
	}
}

static int
tei_ph_data_ind(teimgr_t *tm, int dtyp, struct sk_buff *skb)
{
	u_char *dp;
	int mt;
	int ret = -EINVAL;

	if (!skb)
		return(ret);
	if (test_bit(FLG_FIXED_TEI, &tm->l2->flag) &&
		!test_bit(FLG_LAPD_NET, &tm->l2->flag))
		return(ret);
	if (skb->len < 8) {
		tm->tei_m.printdebug(&tm->tei_m,
			"short mgr frame %ld/8", skb->len);
		return(ret);
	}
	dp = skb->data + 2;
	if ((*dp & 0xef) != UI) {
		tm->tei_m.printdebug(&tm->tei_m,
			"mgr frame is not ui %x", *dp);
		return(ret);
	}
	dp++;
	if (*dp++ != TEI_ENTITY_ID) {
		/* wrong management entity identifier, ignore */
		dp--;
		tm->tei_m.printdebug(&tm->tei_m,
			"tei handler wrong entity id %x", *dp);
		return(ret);
	} else {
		mt = *(dp+2);
		if (tm->debug)
			tm->tei_m.printdebug(&tm->tei_m, "tei handler mt %x", mt);
		if (mt == ID_ASSIGNED)
			mISDN_FsmEvent(&tm->tei_m, EV_ASSIGN, dp);
		else if (mt == ID_DENIED)
			mISDN_FsmEvent(&tm->tei_m, EV_DENIED, dp);
		else if (mt == ID_CHK_REQ)
			mISDN_FsmEvent(&tm->tei_m, EV_CHKREQ, dp);
		else if (mt == ID_REMOVE)
			mISDN_FsmEvent(&tm->tei_m, EV_REMOVE, dp);
		else if (mt == ID_REQUEST && 
			test_bit(FLG_LAPD_NET, &tm->l2->flag))
			mISDN_FsmEvent(&tm->tei_m, EV_ASSIGN_REQ, dp);
		else {
			tm->tei_m.printdebug(&tm->tei_m,
				"tei handler wrong mt %x", mt);
			return(ret);
		}
	}
	dev_kfree_skb(skb);
	return(0);
}

int
l2_tei(teimgr_t *tm, struct sk_buff *skb)
{
	mISDN_head_t	*hh;
	int		ret = -EINVAL;

	if (!tm || !skb)
		return(ret);
	hh = mISDN_HEAD_P(skb);
	if (tm->debug)
		printk(KERN_DEBUG "%s: prim(%x)\n", __FUNCTION__, hh->prim);
	switch(hh->prim) {
	    case (MDL_UNITDATA | INDICATION):
	    	return(tei_ph_data_ind(tm, hh->dinfo, skb));
	    case (MDL_ASSIGN | INDICATION):
		if (test_bit(FLG_FIXED_TEI, &tm->l2->flag)) {
			if (tm->debug)
				tm->tei_m.printdebug(&tm->tei_m,
					"fixed assign tei %d", tm->l2->tei);
			skb_trim(skb, 0);
			mISDN_sethead(MDL_ASSIGN | REQUEST, tm->l2->tei, skb);
			if (!tei_l2(tm->l2, skb))
				return(0);
//			cs->cardmsg(cs, MDL_ASSIGN | REQUEST, NULL);
		} else
			mISDN_FsmEvent(&tm->tei_m, EV_IDREQ, NULL);
		break;
	    case (MDL_ERROR | INDICATION):
	    	if (!test_bit(FLG_FIXED_TEI, &tm->l2->flag))
			mISDN_FsmEvent(&tm->tei_m, EV_VERIFY, NULL);
		break;
	}
	dev_kfree_skb(skb);
	return(0);
}

static void
tei_debug(struct FsmInst *fi, char *fmt, ...)
{
	teimgr_t	*tm = fi->userdata;
	logdata_t	log;
	char		head[24];

	va_start(log.args, fmt);
	sprintf(head,"tei %s", tm->l2->inst.name);
	log.fmt = fmt;
	log.head = head;
	tm->l2->inst.obj->ctrl(&tm->l2->inst, MGR_DEBUGDATA | REQUEST, &log);
	va_end(log.args);
}

static struct FsmNode TeiFnList[] =
{
	{ST_TEI_NOP, EV_IDREQ, tei_id_request},
	{ST_TEI_NOP, EV_ASSIGN, tei_id_test_dup},
	{ST_TEI_NOP, EV_ASSIGN_REQ, tei_assign_req},
	{ST_TEI_NOP, EV_VERIFY, tei_id_verify},
	{ST_TEI_NOP, EV_REMOVE, tei_id_remove},
	{ST_TEI_NOP, EV_CHKREQ, tei_id_chk_req},
	{ST_TEI_IDREQ, EV_T202, tei_id_req_tout},
	{ST_TEI_IDREQ, EV_ASSIGN, tei_id_assign},
	{ST_TEI_IDREQ, EV_DENIED, tei_id_denied},
	{ST_TEI_IDVERIFY, EV_T202, tei_id_ver_tout},
	{ST_TEI_IDVERIFY, EV_REMOVE, tei_id_remove},
	{ST_TEI_IDVERIFY, EV_CHKREQ, tei_id_chk_req},
};

#define TEI_FN_COUNT (sizeof(TeiFnList)/sizeof(struct FsmNode))

void
release_tei(teimgr_t *tm)
{
	mISDN_FsmDelTimer(&tm->t202, 1);
	kfree(tm);
}

int
create_teimgr(layer2_t *l2) {
	teimgr_t *ntei;

	if (!l2) {
		printk(KERN_ERR "create_tei no layer2\n");
		return(-EINVAL);
	}
	if (!(ntei = kmalloc(sizeof(teimgr_t), GFP_ATOMIC))) {
		printk(KERN_ERR "kmalloc teimgr failed\n");
		return(-ENOMEM);
	}
	memset(ntei, 0, sizeof(teimgr_t));
	ntei->l2 = l2;
	ntei->T202 = 2000;	/* T202  2000 milliseconds */
	ntei->debug = l2->debug;
	ntei->tei_m.debug = l2->debug;
	ntei->tei_m.userdata = ntei;
	ntei->tei_m.printdebug = tei_debug;
	if (test_bit(FLG_LAPD_NET, &l2->flag)) {
		ntei->tei_m.fsm = &teifsm;
		ntei->tei_m.state = ST_TEI_NOP;
	} else {
		ntei->tei_m.fsm = &teifsm;
		ntei->tei_m.state = ST_TEI_NOP;
	}
	mISDN_FsmInitTimer(&ntei->tei_m, &ntei->t202);
	l2->tm = ntei;
	return(0);
}

int TEIInit(void)
{
	teifsm.state_count = TEI_STATE_COUNT;
	teifsm.event_count = TEI_EVENT_COUNT;
	teifsm.strEvent = strTeiEvent;
	teifsm.strState = strTeiState;
	mISDN_FsmNew(&teifsm, TeiFnList, TEI_FN_COUNT);
	return(0);
}

void TEIFree(void)
{
	mISDN_FsmFree(&teifsm);
}

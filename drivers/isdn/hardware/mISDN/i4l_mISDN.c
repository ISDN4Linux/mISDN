/* $Id: i4l_mISDN.c,v 1.6 2004/01/26 22:21:30 keil Exp $
 *
 * interface for old I4L hardware drivers to the CAPI driver
 *
 * Copyright  (C) 2003 Karsten Keil (kkeil@suse.de)
 *
 * Author     Karsten Keil (kkeil@suse.de)
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/isdnif.h>
#include <linux/delay.h>
#include <asm/semaphore.h>
#include <linux/mISDNif.h>
#include "fsm.h"
#include "helper.h"
#include "dss1.h"
#include "debug.h"

static char *i4lcapi_revision = "$Revision: 1.6 $";

/* data struct */
typedef struct _i4l_channel	i4l_channel_t;
typedef struct _i4l_capi	i4l_capi_t;

struct _i4l_channel {
	mISDNinstance_t		inst;
	i4l_capi_t		*drv;
	int			nr;
	u_int			Flags;
	int			cause_loc;
	int			cause_val;
	u_int			l4id;
	struct FsmInst		i4lm;
	struct sk_buff_head	sendq;
	struct sk_buff_head	ackq;
};

struct _i4l_capi {
	i4l_capi_t		*prev;
	i4l_capi_t		*next;
	isdn_if			*interface;
	mISDNinstance_t		inst;
	mISDN_pid_t		pid;
	int			idx;
	int			locks;
	int			debug;
	int			nr_ch;
	i4l_channel_t		*ch;
};

#define I4L_FLG_LOCK		0
#define	I4L_FLG_L1TRANS		1
#define I4L_FLG_L1HDLC		2
#define I4L_FLG_LAYER1		3
#define I4L_FLG_BREADY		4
#define I4L_FLG_BCONN		5
#define	I4L_FLG_HANGUP		6

static
struct Fsm i4lfsm_s =
{NULL, 0, 0, NULL, NULL};

enum {
	ST_NULL,
	ST_ICALL,
	ST_OCALL,
	ST_PROCEED,
	ST_ALERT,
	ST_WAITDCONN,
	ST_ACTIVD,
	ST_BREADY,
	ST_ACTIVB,
	ST_HANGUP,
};

#define STATE_COUNT (ST_HANGUP+1)

static char *strI4LState[] =
{
	"ST_NULL",
	"ST_ICALL",
	"ST_OCALL",
	"ST_PROCEED",
	"ST_ALERT",
	"ST_WAITDCONN",
	"ST_ACTIVD",
	"ST_BREADY",
	"ST_ACTIVB",
	"ST_HANGUP",
};

enum {
	EV_I4L_ICALL,
	EV_I4L_DCONN,
	EV_I4L_BCONN,
	EV_I4L_DHUP,
	EV_I4L_BHUP,
	EV_I4L_L1ERR,
	EV_STACKREADY,
	EV_CAPI_OCALL,
	EV_CAPI_ALERT,
	EV_CAPI_PROCEED,
	EV_CAPI_DCONNECT,
	EV_CAPI_ESTABLISHB,
	EV_CAPI_RELEASEB,
	EV_CAPI_DISCONNECT,
	EV_CAPI_RELEASE,
};

#define EVENT_COUNT (EV_CAPI_RELEASE + 1)

static char *strI4LEvent[] =
{
	"EV_I4L_ICALL",
	"EV_I4L_DCONN",
	"EV_I4L_BCONN",
	"EV_I4L_DHUP",
	"EV_I4L_BHUP",
	"EV_I4L_L1ERR",
	"EV_STACKREADY",
	"EV_CAPI_OCALL",
	"EV_CAPI_ALERT",
	"EV_CAPI_PROCEED",
	"EV_CAPI_DCONNECT",
	"EV_CAPI_ESTABLISHB",
	"EV_CAPI_RELEASEB",
	"EV_CAPI_DISCONNECT",
	"EV_CAPI_RELEASE",
};

static void
i4lm_debug(struct FsmInst *fi, char *fmt, ...)
{
	i4l_channel_t	*ch = fi->userdata;
	logdata_t	log;

	va_start(log.args, fmt);
	log.fmt = fmt;
	log.head = ch->inst.name;
	ch->inst.obj->ctrl(&ch->inst, MGR_DEBUGDATA | REQUEST, &log);
	va_end(log.args);
}

#define MAX_CARDS	8
static i4l_capi_t	*drvmap[MAX_CARDS];
static mISDNobject_t	I4Lcapi;

static int debug;

#ifdef MODULE
MODULE_AUTHOR("Karsten Keil");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
MODULE_PARM(debug, "1i");
#endif

static void
i4l_lock_drv(i4l_capi_t *ic)
{
	isdn_ctrl cmd;

	cmd.driver = ic->idx;
	cmd.arg = 0;
	cmd.command = ISDN_CMD_LOCK;
	ic->interface->command(&cmd);
	ic->locks++;
}

static void
i4l_unlock_drv(i4l_capi_t *ic)
{
	isdn_ctrl cmd;

	cmd.driver = ic->idx;
	cmd.arg = 0;
	cmd.command = ISDN_CMD_UNLOCK;
	ic->interface->command(&cmd);
	ic->locks--;
}

static int
i4l_cmd(i4l_capi_t *ic, int arg, int cmd)
{
	isdn_ctrl ctrl;

	ctrl.driver = ic->idx;
	ctrl.arg = arg;
	ctrl.command = cmd;
	return(ic->interface->command(&ctrl));
}

static void
init_channel(i4l_capi_t *ic, int nr)
{
	i4l_channel_t	*ch;

	ch = ic->ch + nr;
	memset(ch, 0, sizeof(i4l_channel_t));
	ch->nr = nr;
	ch->drv = ic;
	ch->i4lm.debug = debug & 0x8;
	ch->i4lm.userdata = ch;
	ch->i4lm.userint = 0;
	ch->i4lm.printdebug = i4lm_debug;
	ch->i4lm.fsm = &i4lfsm_s;
	ch->i4lm.state = ST_NULL;
	skb_queue_head_init(&ch->sendq);
	skb_queue_head_init(&ch->ackq);
	ch->inst.obj = &I4Lcapi;
	ch->inst.data = ch;
	ch->inst.pid.layermask = ISDN_LAYER(0);
	ch->inst.up.owner = &ch->inst;
	ch->inst.down.owner = &ch->inst;
	I4Lcapi.ctrl(NULL, MGR_DISCONNECT | REQUEST, &ch->inst.down);
	sprintf(ch->inst.name, "%s B%d", ic->inst.name, nr+1);
}

static void
reset_channel(i4l_channel_t *ch)
{
	ch->cause_loc = 0;
	ch->cause_val = 0;
	ch->l4id = 0;
	skb_queue_purge(&ch->sendq);
	skb_queue_purge(&ch->ackq);
	if (test_and_clear_bit(I4L_FLG_LOCK, &ch->Flags))
		i4l_unlock_drv(ch->drv);
	ch->Flags = 0;
}

static void
release_card(int idx) {
	i4l_capi_t	*ic = drvmap[idx];
	i4l_channel_t	*ch;
	int		i;
	mISDNinstance_t	*inst;

	if (!ic)
		return;
	drvmap[idx] = NULL;
	ch = ic->ch;
	for (i=0; i<ic->nr_ch; i++) {
		inst = &ch->inst;
		if (inst->up.peer)
			inst->up.peer->obj->ctrl(inst->up.peer,
				MGR_DISCONNECT | REQUEST, &inst->up);
		reset_channel(ch);
		ch++;
	}
	inst = &ic->inst;
	if (inst->up.peer) {
		inst->up.peer->obj->ctrl(inst->up.peer,
			MGR_DISCONNECT | REQUEST, &inst->up);
	}
	REMOVE_FROM_LISTBASE(ic, ((i4l_capi_t *)I4Lcapi.ilist));
	while (ic->locks > 0)
		i4l_unlock_drv(ic);
	kfree(ic->ch);
	ic->ch = NULL;
	kfree(ic);
	I4Lcapi.refcnt--;
}

static int
sendup(i4l_channel_t *ch, int Dchannel, int prim, struct sk_buff *skb)
{
	int		ret;
	mISDN_headext_t	*hhe;
	mISDNinstance_t	*I;

	if (!skb) {
		skb = alloc_skb(8, GFP_ATOMIC);
		if (!skb)
			return(-ENOMEM);
	}
	hhe = mISDN_HEADEXT_P(skb);
	hhe->prim = prim;
	hhe->dinfo = ch->l4id;
	if (ch->drv->debug & 0x4)
		mISDN_LogL3Msg(skb);
	if (Dchannel)
		I = &ch->drv->inst;
	else
		I = &ch->inst;
	if (!I->up.func) {
		int_error();
		dev_kfree_skb(skb);
		return(-EUNATCH);
	}
	if (in_interrupt()) {
		hhe->func.iff = I->up.func;
		hhe->data[0] = &I->up;
		ret = I->obj->ctrl(NULL, MGR_QUEUEIF | REQUEST, skb);
	} else
		ret = I->up.func(&I->up, skb);
	if (ret)
		dev_kfree_skb(skb);
	return(ret);
}

static int
sendqueued(i4l_channel_t *ch)
{
	struct sk_buff	*skb, *s_skb;
	int		len, ret;

	if (!test_bit(I4L_FLG_BCONN, &ch->Flags)) {
		if (ch->drv->debug & 0x40)
			printk(KERN_DEBUG "%s: bc%d not ready\n", __FUNCTION__, ch->nr);
		return(0);
	}
	while ((skb = skb_dequeue(&ch->sendq))) {
		s_skb = skb_clone(skb, GFP_ATOMIC);
		len = s_skb->len;
		skb_queue_tail(&ch->ackq, skb);
		ret = ch->drv->interface->writebuf_skb(ch->drv->idx, ch->nr, 1, s_skb);
		if (ch->drv->debug & 0x800)
			printk(KERN_DEBUG "bc%d sent skb(%p) %d(%d)\n", ch->nr, skb, ret, len);
		if (ret == len) {
			continue;
		} else if (ret > 0) {
			skb_queue_head(&ch->sendq, s_skb);
			break;
		} else {
			skb_unlink(skb);
			skb_queue_head(&ch->sendq, skb);
			if (!ret)
				dev_kfree_skb(s_skb);
			break;
		}
	}
	return(0);
}

static u_char *
EncodeASyncParams(u_char * p, u_char si2)
{				// 7c 06 88  90 21 42 00 bb
	p[0] = 0;
	p[1] = 0x40;		// Intermediate rate: 16 kbit/s jj 2000.02.19
	p[2] = 0x80;
	if (si2 & 32)		// 7 data bits
		p[2] += 16;
	else			// 8 data bits
		p[2] += 24;

	if (si2 & 16)		// 2 stop bits
		p[2] += 96;
	else			// 1 stop bit
		p[2] += 32;
	if (si2 & 8)		// even parity
		p[2] += 2;
	else			// no parity
		p[2] += 3;
	switch (si2 & 0x07) {
		case 0:
			p[0] = 66;	// 1200 bit/s
			break;
		case 1:
			p[0] = 88;	// 1200/75 bit/s
			break;
		case 2:
			p[0] = 87;	// 75/1200 bit/s
			break;
		case 3:
			p[0] = 67;	// 2400 bit/s
			break;
		case 4:
			p[0] = 69;	// 4800 bit/s
			break;
		case 5:
			p[0] = 72;	// 9600 bit/s
			break;
		case 6:
			p[0] = 73;	// 14400 bit/s
			break;
		case 7:
			p[0] = 75;	// 19200 bit/s
			break;
	}
	return p + 3;
}

static  u_char
EncodeSyncParams(u_char si2, u_char ai)
{
	switch (si2) {
		case 0:
			return ai + 2;	// 1200 bit/s
		case 1:
			return ai + 24;	// 1200/75 bit/s
		case 2:
			return ai + 23;	// 75/1200 bit/s
		case 3:
			return ai + 3;	// 2400 bit/s
		case 4:
			return ai + 5;	// 4800 bit/s
		case 5:
			return ai + 8;	// 9600 bit/s
		case 6:
			return ai + 9;	// 14400 bit/s
		case 7:
			return ai + 11;	// 19200 bit/s
		case 8:
			return ai + 14;	// 48000 bit/s
		case 9:
			return ai + 15;	// 56000 bit/s
		case 15:
			return ai + 40;	// negotiate bit/s
		default:
			break;
	}
	return ai;
}

static u_char
DecodeASyncParams(u_char si2, u_char * p)
{
	u_char info;

	switch (p[5]) {
		case 66:	// 1200 bit/s
			break;	// si2 don't change
		case 88:	// 1200/75 bit/s
			si2 += 1;
			break;
		case 87:	// 75/1200 bit/s
			si2 += 2;
			break;
		case 67:	// 2400 bit/s
			si2 += 3;
			break;
		case 69:	// 4800 bit/s
			si2 += 4;
			break;
		case 72:	// 9600 bit/s
			si2 += 5;
			break;
		case 73:	// 14400 bit/s
			si2 += 6;
			break;
		case 75:	// 19200 bit/s
			si2 += 7;
			break;
	}
	info = p[7] & 0x7f;
	if ((info & 16) && (!(info & 8)))	// 7 data bits
		si2 += 32;	// else 8 data bits
	if ((info & 96) == 96)	// 2 stop bits
		si2 += 16;	// else 1 stop bit
	if ((info & 2) && (!(info & 1)))	// even parity
		si2 += 8;	// else no parity
	return si2;
}


static u_char
DecodeSyncParams(u_char si2, u_char info)
{
	switch (info & 0x7f) {
		case 40:	// bit/s negotiation failed  ai := 165 not 175!
			return si2 + 15;
		case 15:	// 56000 bit/s failed, ai := 0 not 169 !
			return si2 + 9;
		case 14:	// 48000 bit/s
			return si2 + 8;
		case 11:	// 19200 bit/s
			return si2 + 7;
		case 9:	// 14400 bit/s
			return si2 + 6;
		case 8:	// 9600  bit/s
			return si2 + 5;
		case 5:	// 4800  bit/s
			return si2 + 4;
		case 3:	// 2400  bit/s
			return si2 + 3;
		case 23:	// 75/1200 bit/s
			return si2 + 2;
		case 24:	// 1200/75 bit/s
			return si2 + 1;
		default:	// 1200 bit/s
			return si2;
	}
}

static u_char
DecodeSI2(u_char *p)
{

	if (p) {
		switch (p[4] & 0x0f) {
			case 0x01:
				if (p[1] == 0x04)	// sync. Bitratenadaption
					return DecodeSyncParams(160, p[5]);	// V.110/X.30
				else if (p[1] == 0x06)	// async. Bitratenadaption
					return DecodeASyncParams(192, p);	// V.110/X.30
				break;
			case 0x08:	// if (p[5] == 0x02) // sync. Bitratenadaption
				if (p[1] > 3) 
					return DecodeSyncParams(176, p[5]);	// V.120
				break;
		}
	}
	return 0;
}

static void
i4l_l1err(struct FsmInst *fi, int event, void *arg)
{
	i4l_channel_t	*ch = fi->userdata;

	sendup(ch, 1, DL_RELEASE | INDICATION, NULL);
	reset_channel(ch);
	mISDN_FsmChangeState(fi, ST_NULL);
}

static void
i4l_dhup(struct FsmInst *fi, int event, void *arg)
{
	i4l_channel_t	*ch = fi->userdata;
	struct sk_buff	*skb;
	u_char		tmp[8];

	skb = mISDN_alloc_l3msg(8, MT_RELEASE);
	if (!skb)
		return;

	tmp[0] = IE_CAUSE;
	tmp[1] = 2;
	if (ch->cause_val) {
		tmp[2] = ch->cause_loc;
		tmp[3] = ch->cause_val;
	} else {
		tmp[2] = 0x80;
		tmp[3] = 0x9f; /* normal, unspecified */
	}
	mISDN_AddvarIE(skb, tmp);
	sendup(ch, 1, CC_RELEASE | INDICATION, skb);
	reset_channel(ch);
	mISDN_FsmChangeState(fi, ST_NULL);
}

static void
i4l_icall(struct FsmInst *fi, int event, void *arg)
{
	i4l_channel_t	*ch = fi->userdata;
	setup_parm	*setup = arg;
	u_char		tmp[36], *p;
	struct sk_buff	*skb;
	int		i,j;

	test_and_set_bit(I4L_FLG_LOCK, &ch->Flags);
	i4l_lock_drv(ch->drv);
	if ((skb = alloc_skb(sizeof(int *) + 8, GFP_ATOMIC))) {
		int	**idp = (int **)skb_put(skb, sizeof(idp));
		mISDN_head_t *hh = mISDN_HEAD_P(skb);

		*idp = &ch->l4id;
		hh->prim = CC_NEW_CR | INDICATION;
		i = ch->drv->inst.up.func(&ch->drv->inst.up, skb);
		if (i) {
			int_error();
			dev_kfree_skb(skb);
			return;
		}
		if (ch->drv->debug & 0x2)
			printk(KERN_DEBUG "%s: l4id(%x) ch(%p)->nr %d\n", __FUNCTION__, ch->l4id, ch, ch->nr);
	} else
		return;
	skb = mISDN_alloc_l3msg(260, MT_SETUP);
	if (!skb)
		return;
	p = tmp;
        switch (setup->si1) {
		case 1:			/* Telephony                        */
			*p++ = IE_BEARER;
			*p++ = 0x3;	/* Length                           */
			*p++ = 0x90;	/* Coding Std. CCITT, 3.1 kHz audio */
			*p++ = 0x90;	/* Circuit-Mode 64kbps              */
			*p++ = 0xa3;	/* A-Law Audio                      */
			break;
		case 5:			/* Datatransmission 64k, BTX        */
		case 7:			/* Datatransmission 64k             */
		default:
			*p++ = IE_BEARER;
			*p++ = 0x2;	/* Length                           */
			*p++ = 0x88;	/* Coding CCITT, unrestr. dig. Info.*/
			*p++ = 0x90;	/* Circuit-Mode 64kbps              */
			break;
	}
	mISDN_AddvarIE(skb, tmp);
	tmp[0] = IE_CHANNEL_ID;
	tmp[1] = 1;
	tmp[2] = 0x85 + ch->nr;
	mISDN_AddvarIE(skb, tmp);
	if (setup->phone[0]) {
		i = 1;
		if (setup->plan) {
			tmp[i++] = setup->plan;
			if (!(setup->plan & 0x80))
				tmp[i++] = setup->screen;
		} else
			tmp[i++] = 0x81;
		j = 0;
		while (setup->phone[j]) {
			if (setup->phone[j] == '.') /* subaddress */
				break;
			tmp[i++] = setup->phone[j++];
		}
		tmp[0] = i-1;
		mISDN_AddIE(skb, IE_CALLING_PN, tmp);
		if (setup->phone[j] == '.') {
			i = 1;
			tmp[i++] = 0x80;
			j++;
			while (setup->phone[j])
				tmp[i++] = setup->phone[j++];
			tmp[0] = i-1;
			mISDN_AddIE(skb, IE_CALLING_SUB, tmp);
		}
	}
	if (setup->eazmsn[0]) {
		i = 1;
		tmp[i++] = 0x81;
		j = 0;
		while (setup->eazmsn[j]) {
			if (setup->eazmsn[j] == '.') /* subaddress */
				break;
			tmp[i++] = setup->eazmsn[j++];
		}
		tmp[0] = i-1;
		mISDN_AddIE(skb, IE_CALLED_PN, tmp);
		if (setup->eazmsn[j] == '.') {
			i = 1;
			tmp[i++] = 0x80;
			j++;
			while (setup->eazmsn[j])
				tmp[i++] = setup->eazmsn[j++];
			tmp[0] = i-1;
			mISDN_AddIE(skb, IE_CALLED_SUB, tmp);
		}
	}
	p = tmp;
	*p++ = IE_LLC;
	if ((setup->si2 >= 160) && (setup->si2 <= 175)) { // sync. Bitratenadaption, V.110/X.30
		*p++ = 0x04;
		*p++ = 0x88;
		*p++ = 0x90;
		*p++ = 0x21;
		*p++ = EncodeSyncParams(setup->si2 - 160, 0x80);
		test_and_set_bit(I4L_FLG_L1TRANS, &ch->Flags);
	} else if ((setup->si2 >= 176) && (setup->si2 <= 191)) { // sync. Bitratenadaption, V.120
		*p++ = 0x05;
		*p++ = 0x88;
		*p++ = 0x90;
		*p++ = 0x28;
		*p++ = EncodeSyncParams(setup->si2 - 176, 0);
		*p++ = 0x82;
		test_and_set_bit(I4L_FLG_L1TRANS, &ch->Flags);
	} else if (setup->si2 >= 192) { // async. Bitratenadaption, V.110/X.30
		*p++ = 0x06;
		*p++ = 0x88;
		*p++ = 0x90;
		*p++ = 0x21;
		p = EncodeASyncParams(p, setup->si2 - 192);
		test_and_set_bit(I4L_FLG_L1TRANS, &ch->Flags);
	} else {
		switch(setup->si1) {
			case 1:
				*p++ = 0x3;
				*p++ = 0x90;
				*p++ = 0x90;
				*p++ = 0xa3;
				test_and_set_bit(I4L_FLG_L1TRANS, &ch->Flags);
				break;
			case 5:
			case 7:
			default:
				*p++ = 0x2;
				*p++ = 0x88;
				*p++ = 0x90;
				test_and_set_bit(I4L_FLG_L1HDLC, &ch->Flags);
				break;
		}
	}
	mISDN_AddvarIE(skb, tmp);
	mISDN_FsmChangeState(fi, ST_ICALL);
	sendup(ch, 1, CC_SETUP | INDICATION, skb);
}

static void
i4l_dconn_out(struct FsmInst *fi, int event, void *arg)
{
	i4l_channel_t	*ch = fi->userdata;
	struct sk_buff	*skb;
	u_char		tmp[4];

	skb = mISDN_alloc_l3msg(4, MT_CONNECT);
	if (!skb)
		return;

	tmp[0] = IE_CHANNEL_ID;
	tmp[1] = 1;
	tmp[2] = 0x85 + ch->nr;
	mISDN_AddvarIE(skb, tmp);
	sendup(ch, 1, CC_CONNECT | INDICATION, skb);
	mISDN_FsmChangeState(fi, ST_ACTIVD);
}

static void
i4l_dconn_in(struct FsmInst *fi, int event, void *arg)
{
	i4l_channel_t	*ch = fi->userdata;

	sendup(ch, 1, CC_CONNECT_ACKNOWLEDGE | INDICATION, NULL);
	mISDN_FsmChangeState(fi, ST_ACTIVD);
}

static void
i4l_bconn_notready(struct FsmInst *fi, int event, void *arg)
{
	i4l_channel_t	*ch = fi->userdata;

	test_and_set_bit(I4L_FLG_BCONN, &ch->Flags);
}

static void
i4l_bconn(struct FsmInst *fi, int event, void *arg)
{
	i4l_channel_t	*ch = fi->userdata;
	int		prim = test_bit(I4L_FLG_LAYER1, &ch->Flags) ? PH_ACTIVATE : DL_ESTABLISH;

	sendup(ch, 0, prim | INDICATION, NULL);
	test_and_set_bit(I4L_FLG_BCONN, &ch->Flags);
	mISDN_FsmChangeState(fi, ST_ACTIVB);
	if (skb_queue_len(&ch->sendq))
		sendqueued(ch);
}

static void
i4l_bhup(struct FsmInst *fi, int event, void *arg)
{
	i4l_channel_t	*ch = fi->userdata;
	int		prim = test_bit(I4L_FLG_LAYER1, &ch->Flags) ? PH_DEACTIVATE : DL_RELEASE;

	mISDN_FsmChangeState(fi, ST_ACTIVD);
	sendup(ch, 0, prim | INDICATION, NULL);
}

static void
stackready(struct FsmInst *fi, int event, void *arg)
{
	i4l_channel_t	*ch = fi->userdata;

	mISDN_FsmChangeState(fi, ST_BREADY);
	test_and_set_bit(I4L_FLG_BREADY, &ch->Flags);
	if (test_bit(I4L_FLG_BCONN, &ch->Flags))
		mISDN_FsmEvent(&ch->i4lm, EV_I4L_BCONN, NULL);
}

static void
capi_ocall(struct FsmInst *fi, int event, void *arg)
{
	i4l_channel_t	*ch = fi->userdata;
	struct sk_buff	*skb = arg;
	Q931_info_t	*qi = (Q931_info_t *)skb->data;
	u_char		*p, *ps = skb->data;
	isdn_ctrl	ctrl;
	int		i,l;

	mISDN_FsmChangeState(fi, ST_OCALL);
	test_and_set_bit(I4L_FLG_LOCK, &ch->Flags);
	i4l_lock_drv(ch->drv);
	ps += L3_EXTRA_SIZE;
	ctrl.parm.setup.si1 = 0;
	ctrl.parm.setup.si2 = 0;
	if (qi->bearer_capability) {
		p = ps + qi->bearer_capability;
		if ((p[1] > 1) && (p[1] < 11)) {
			switch (p[2] & 0x7f) {
				case 0x00: /* Speech */
				case 0x10: /* 3.1 Khz audio */
					ctrl.parm.setup.si1 = 1;
					break;
				case 0x08: /* Unrestricted digital information */
					ctrl.parm.setup.si1 = 7;
					if (qi->llc) 
						ctrl.parm.setup.si2 = DecodeSI2(ps + qi->llc);
					break;
				case 0x09: /* Restricted digital information */
					ctrl.parm.setup.si1 = 2;
					break;
				case 0x11:
					/* Unrestr. digital information  with 
					 * tones/announcements ( or 7 kHz audio)
					 */
					ctrl.parm.setup.si1 = 3;
					break;
				case 0x18: /* Video */
					ctrl.parm.setup.si1 = 4;
					break;
			}
			switch (p[3] & 0x7f) {
				case 0x40: /* packed mode */
					ctrl.parm.setup.si1 = 8;
					break;
			}
		}
	}
	if ((ctrl.parm.setup.si1 == 7) && (ctrl.parm.setup.si2 < 160))
		test_and_set_bit(I4L_FLG_L1HDLC, &ch->Flags);
	else
		test_and_set_bit(I4L_FLG_L1TRANS, &ch->Flags);
	i = 0;
	if (qi->calling_nr) {
		p = ps + qi->calling_nr + 1;
		l = *p++;
		ctrl.parm.setup.plan = *p;
		l--;
		if (!(*p & 0x80)) {
			p++;
			ctrl.parm.setup.screen = *p;
			l--;
		} else
			ctrl.parm.setup.screen = 0;
		p++;
		while(i<l)
			ctrl.parm.setup.eazmsn[i++] = *p++;
		ctrl.parm.setup.eazmsn[i] = 0;
	} else
		ctrl.parm.setup.eazmsn[0] = 0;
	if (qi->calling_sub) {
		p = ps + qi->calling_sub + 1;
		l = *p++;
		l--;
		p++;
		if (l>0)
			ctrl.parm.setup.eazmsn[i++] = '.';
		while(l>0) {
			ctrl.parm.setup.eazmsn[i++] = *p++;
			l--;
		}
		ctrl.parm.setup.eazmsn[i] = 0;
	}
	i = 0;
	if (qi->called_nr) {
		p = ps + qi->called_nr + 1;
		l = *p++;
		p++;
		l--;
		while(i<l)
			ctrl.parm.setup.phone[i++] = *p++;
		ctrl.parm.setup.phone[i] = 0;
	} else
		ctrl.parm.setup.phone[0] = 0;
	if (qi->called_sub) {
		p = ps + qi->called_sub + 1;
		l = *p++;
		l--;
		p++;
		if (l>0)
			ctrl.parm.setup.phone[i++] = '.';
		while(l>0) {
			ctrl.parm.setup.phone[i++] = *p++;
			l--;
		}
		ctrl.parm.setup.phone[i] = 0;
	}
	if (test_bit(I4L_FLG_L1TRANS, &ch->Flags)) {
		i4l_cmd(ch->drv, ch->nr | (ISDN_PROTO_L2_TRANS << 8), ISDN_CMD_SETL2);
		i4l_cmd(ch->drv, ch->nr | (ISDN_PROTO_L3_TRANS << 8), ISDN_CMD_SETL3);
	} else {
		i4l_cmd(ch->drv, ch->nr | (ISDN_PROTO_L2_HDLC << 8), ISDN_CMD_SETL2);
		i4l_cmd(ch->drv, ch->nr | (ISDN_PROTO_L3_TRANS << 8), ISDN_CMD_SETL3);
	}
	if (ch->drv->debug & 0x4)
		printk(KERN_DEBUG "ocall from %s, si(%d/%d) -> %s\n", ctrl.parm.setup.eazmsn,
			ctrl.parm.setup.si1, ctrl.parm.setup.si2, ctrl.parm.setup.phone);
	ctrl.driver = ch->drv->idx;
	ctrl.arg = ch->nr;
	ctrl.command = ISDN_CMD_DIAL;
	ch->drv->interface->command(&ctrl);
	dev_kfree_skb(skb);
}

static void
capi_alert(struct FsmInst *fi, int event, void *arg)
{
	i4l_channel_t	*ch = fi->userdata;
	struct sk_buff	*skb = arg;

	mISDN_FsmChangeState(fi, ST_ALERT);
	i4l_cmd(ch->drv, ch->nr, ISDN_CMD_ALERT);
	if (skb)
		dev_kfree_skb(skb);
}

static void
capi_connect(struct FsmInst *fi, int event, void *arg)
{
	i4l_channel_t	*ch = fi->userdata;
	struct sk_buff	*skb = arg;

	if (test_bit(I4L_FLG_L1TRANS, &ch->Flags)) {
		i4l_cmd(ch->drv, ch->nr | (ISDN_PROTO_L2_TRANS << 8), ISDN_CMD_SETL2);
		i4l_cmd(ch->drv, ch->nr | (ISDN_PROTO_L3_TRANS << 8), ISDN_CMD_SETL3);
	} else {
		i4l_cmd(ch->drv, ch->nr | (ISDN_PROTO_L2_HDLC << 8), ISDN_CMD_SETL2);
		i4l_cmd(ch->drv, ch->nr | (ISDN_PROTO_L3_TRANS << 8), ISDN_CMD_SETL3);
	}
	mISDN_FsmChangeState(fi, ST_WAITDCONN);
	i4l_cmd(ch->drv, ch->nr, ISDN_CMD_ACCEPTD);
	if (skb)
		dev_kfree_skb(skb);
}

static void
capi_disconnect(struct FsmInst *fi, int event, void *arg)
{
	i4l_channel_t	*ch = fi->userdata;
	struct sk_buff	*skb = arg;

	mISDN_FsmChangeState(fi, ST_HANGUP);
	test_and_set_bit(I4L_FLG_HANGUP, &ch->Flags);
	i4l_cmd(ch->drv, ch->nr, ISDN_CMD_HANGUP);
	if (skb)
		dev_kfree_skb(skb);
}

static void
capi_release(struct FsmInst *fi, int event, void *arg)
{
	i4l_channel_t	*ch = fi->userdata;
	struct sk_buff	*skb = arg;

	if (!test_and_clear_bit(I4L_FLG_HANGUP, &ch->Flags))
		i4l_cmd(ch->drv, ch->nr, ISDN_CMD_HANGUP);
	if (skb)
		dev_kfree_skb(skb);
	reset_channel(ch);
	mISDN_FsmChangeState(fi, ST_NULL);
}

static void
capi_establishb(struct FsmInst *fi, int event, void *arg)
{
	i4l_channel_t	*ch = fi->userdata;

	i4l_cmd(ch->drv, ch->nr, ISDN_CMD_ACCEPTB);
}

static void
capi_releaseb(struct FsmInst *fi, int event, void *arg)
{
	i4l_channel_t	*ch = fi->userdata;

	test_and_clear_bit(I4L_FLG_BREADY, &ch->Flags);
	mISDN_FsmChangeState(fi, ST_ACTIVD);
}

static int
Dchannel_i4l(mISDNif_t *hif, struct sk_buff *skb)
{
	int		i, ret = -EINVAL;
	mISDN_head_t	*hh;
	i4l_capi_t	*ic;
	i4l_channel_t	*ch;

	if (!hif || !skb)
		return(ret);
	ic = hif->fdata;
	hh = mISDN_HEAD_P(skb);
	if (ic->debug & 0x2)
		printk(KERN_DEBUG "%s: prim(%x) id(%x)\n", __FUNCTION__, hh->prim, hh->dinfo);
	if (!ic)
		return(ret);
	if ((DL_ESTABLISH | REQUEST) == hh->prim) {
		// FIXME
		dev_kfree_skb(skb);
		return(0);
	}
	ch = ic->ch;
	for (i=0; i < ic->nr_ch; i++) {
		if (ch->l4id == hh->dinfo)
			break;
		ch++;
	}
	if (i == ic->nr_ch)
		ch = NULL;
	if ((CC_NEW_CR | REQUEST) == hh->prim) {
		if (ch) {
			printk(KERN_WARNING "%s: ch%x in use\n", __FUNCTION__, ch->nr);
			ret = -EBUSY;
		} else {
			ch = ic->ch;
			for (i=0; i < ic->nr_ch; i++) {
				if (ch->l4id == 0)
					break;
				ch++;
			}
			if (i == ic->nr_ch) {
				ret = -EBUSY;
			} else {
				ch->l4id = hh->dinfo;
				ret = 0;
				dev_kfree_skb(skb);
			}
		}
		return(ret);
	}
	if (!ch) {
		printk(KERN_WARNING "%s: no channel prim(%x) id(%x)\n", __FUNCTION__, hh->prim, hh->dinfo);
		return(ret);
	}
	if (ch->drv->debug & 0x4)
		mISDN_LogL3Msg(skb);
	switch(hh->prim) {
		case CC_SETUP | REQUEST:
			ret = mISDN_FsmEvent(&ch->i4lm, EV_CAPI_OCALL, skb);
			break;
		case CC_ALERTING | REQUEST:
			ret = mISDN_FsmEvent(&ch->i4lm, EV_CAPI_ALERT, skb);
			break;
		case CC_CONNECT | REQUEST:
			ret = mISDN_FsmEvent(&ch->i4lm, EV_CAPI_DCONNECT, skb);
			break;
		case CC_DISCONNECT | REQUEST:
		case CC_RELEASE | REQUEST:
			ret = mISDN_FsmEvent(&ch->i4lm, EV_CAPI_DISCONNECT, skb);
			break;
		case CC_RELEASE_COMPLETE | REQUEST:
			ret = mISDN_FsmEvent(&ch->i4lm, EV_CAPI_RELEASE, skb);
			break;
		default:
			if (debug)
				printk(KERN_DEBUG "%s: ch%x prim(%x) id(%x) not handled\n",
					__FUNCTION__, ch->nr, hh->prim, hh->dinfo);
			break;
	}
	return(ret);
}

static int
Bchannel_i4l(mISDNif_t *hif, struct sk_buff *skb)
{
	i4l_channel_t	*ch;
	int		ret = -EINVAL;
	mISDN_head_t	*hh;

	if (!hif || !skb)
		return(ret);
	ch = hif->fdata;
	hh = mISDN_HEAD_P(skb);
	if (ch->drv->debug & 0x20)
		printk(KERN_DEBUG  "%s: prim(%x)\n", __FUNCTION__, hh->prim);
	switch(hh->prim) {
		case PH_ACTIVATE | REQUEST:
		case DL_ESTABLISH | REQUEST:
			mISDN_FsmEvent(&ch->i4lm, EV_CAPI_ESTABLISHB, NULL);
			skb_trim(skb, 0);
			ret = if_newhead(&ch->inst.up, hh->prim | CONFIRM, 0, skb);
			break;
		case PH_DEACTIVATE | REQUEST:
		case DL_RELEASE | REQUEST:
			mISDN_FsmEvent(&ch->i4lm, EV_CAPI_RELEASEB, NULL);
			skb_trim(skb, 0);
			ret = if_newhead(&ch->inst.up, hh->prim | CONFIRM, 0, skb);
			break;
		case PH_DATA | REQUEST:
		case DL_DATA | REQUEST:
			skb_queue_tail(&ch->sendq, skb);
			ret = sendqueued(ch);
			break;
		default:
			if (debug)
				printk(KERN_DEBUG "%s: ch%x prim(%x) id(%x) not handled\n",
					__FUNCTION__, ch->nr, hh->prim, hh->dinfo);
			break;
	}
	return(ret);
}


/*
 * Receive a packet from B-Channel. (Called from low-level-module)
 */
static void
I4Lcapi_receive_skb_callback(int drvidx, int channel, struct sk_buff *skb)
{
	i4l_capi_t	*ic = drvmap[drvidx];
	i4l_channel_t	*ch;
	mISDN_headext_t	*hhe = mISDN_HEADEXT_P(skb);
	int		ret;

	if (!ic) {
		int_error();
		return;
	}
	ch = ic->ch + channel;
	if (!test_bit(I4L_FLG_BREADY, &ch->Flags)) {
		if (ic->debug & 0x10)
			printk(KERN_WARNING "I4Lcapi_receive_skb_callback: bc%d/%d not ready\n", channel, ch->nr);
		dev_kfree_skb(skb);
		return;
	}
	hhe->prim = test_bit(I4L_FLG_LAYER1, &ch->Flags) ? PH_DATA_IND : DL_DATA_IND;
	if (!ch->inst.up.func) {
		dev_kfree_skb(skb);
		int_error();
		return;
	}
	if (in_interrupt()) {
		hhe->func.iff = ch->inst.up.func;
		hhe->data[0] = &ch->inst.up;
		ret = ch->inst.obj->ctrl(NULL, MGR_QUEUEIF | REQUEST, skb);
	} else
		ret = ch->inst.up.func(&ch->inst.up, skb);
	if (ret)
		dev_kfree_skb(skb);
}

static int
i4l_stat_run(i4l_capi_t *ic) {
	int	err;

	err = I4Lcapi.ctrl(ic->inst.st, MGR_SETSTACK | REQUEST, &ic->pid);
	if (err) {
		printk(KERN_ERR  "MGR_SETSTACK REQUEST dch err(%d)\n", err);
		I4Lcapi.ctrl(ic->inst.st, MGR_DELSTACK | REQUEST, NULL);
		return(err);
	}
	return(0);
}

static int
i4l_sent_pkt(i4l_capi_t *drv, isdn_ctrl *c)
{
	i4l_channel_t	*ch = drv->ch;
	struct sk_buff	*skb;
	int		ret;
	mISDN_headext_t	*hhe;

	if (c->arg < 0)
		return -1;
	ch += c->arg;
	skb = skb_dequeue(&ch->ackq);
	if (!skb) {
		int_error();
		return(-1);
	}
	if (drv->debug & 0x800)
		printk(KERN_DEBUG "bc%ld ack skb(%p)\n", c->arg, skb);
	if (skb_queue_len(&ch->sendq))
		sendqueued(ch);
	skb_trim(skb, 0);
	hhe = mISDN_HEADEXT_P(skb);
	hhe->prim |= CONFIRM;
	if (in_interrupt()) {
		hhe->func.iff = ch->inst.up.func;
		hhe->data[0] = &ch->inst.up;
		ret = ch->inst.obj->ctrl(NULL, MGR_QUEUEIF | REQUEST, skb);
	} else
		ret = ch->inst.up.func(&ch->inst.up, skb);
	if (ret)
		dev_kfree_skb(skb);
	return(ret);
}

#define I4L_LOGBUF_SIZE	256
static char	logbuf[I4L_LOGBUF_SIZE];

static int
i4l_stavail(i4l_capi_t *drv, isdn_ctrl *c)
{
	int		len = c->arg;

	if (drv->interface->readstat) {
		while(len>0) {
			if (len < I4L_LOGBUF_SIZE) {
				drv->interface->readstat(logbuf, len, 0, drv->idx, 0);
				logbuf[len] = 0;
			} else {
				drv->interface->readstat(logbuf, I4L_LOGBUF_SIZE - 1, 0, drv->idx, 0);
				logbuf[I4L_LOGBUF_SIZE] = 0;
			}
			if (drv->debug & 0x1)
				printk(KERN_DEBUG "%s", logbuf);
			len -= (I4L_LOGBUF_SIZE -1);
		}
	}
	return(0);
}

static int
I4Lcapi_status_callback(isdn_ctrl *c)
{
	i4l_capi_t	*drv = drvmap[c->driver];
	i4l_channel_t	*ch;
	int		i, ret = -1;

	if (!drv)
		return(-1);
	if (c->command == ISDN_STAT_BSENT)
		return(i4l_sent_pkt(drv, c));
	if (c->command == ISDN_STAT_STAVAIL)
		return(i4l_stavail(drv, c));
	ch = drv->ch;
	if (drv->debug & 0x8)
		printk(KERN_DEBUG "drv%d cmd(%d) arg(%ld)\n",
			c->driver, c->command, c->arg);
	switch (c->command) {
		case ISDN_STAT_RUN:
			ret = i4l_stat_run(drv);
			break;
		case ISDN_STAT_STOP:
			// FIXME
			ret = 0;
			break;
		case ISDN_STAT_ICALL:
			if (c->arg < 0)
				return -1;
			ch += c->arg;
			ret = mISDN_FsmEvent(&ch->i4lm, EV_I4L_ICALL, &c->parm.setup);
			break;
		case ISDN_STAT_CINF:
			if (c->arg < 0)
				return -1;
			// FIXME
			ret = 0;
			break;
		case ISDN_STAT_CAUSE:
			if (c->arg < 0)
				return -1;
			ch += c->arg;
			if ((c->parm.num[0] == 'E') || (c->parm.num[0] == 'L'))
				i = 1;
			else
				i = 0;
			sscanf(&c->parm.num[i], "%2X%2X", &ch->cause_loc, &ch->cause_val);
			ch->cause_loc |= 0x80;
			ch->cause_val |= 0x80;
			if (drv->debug & 0x1)
				printk(KERN_DEBUG "isdn: ch%ld cause: %s %02x%02x\n",
					c->arg, c->parm.num, ch->cause_loc, ch->cause_val);
			ret = 0;
			break;
		case ISDN_STAT_DISPLAY:
			// FIXME
			ret = 0;
			break;
		case ISDN_STAT_DCONN:
			if (c->arg < 0)
				return -1;
			ch += c->arg;
			ret = mISDN_FsmEvent(&ch->i4lm, EV_I4L_DCONN, NULL);
			break;
		case ISDN_STAT_DHUP:
			if (c->arg < 0)
				return -1;
			ch += c->arg;
			ret = mISDN_FsmEvent(&ch->i4lm, EV_I4L_DHUP, NULL);
			break;
		case ISDN_STAT_BCONN:
			if (c->arg < 0)
				return -1;
			ch += c->arg;
			ret = mISDN_FsmEvent(&ch->i4lm, EV_I4L_BCONN, NULL);
			break;
		case ISDN_STAT_BHUP:
			if (c->arg < 0)
				return -1;
			ch += c->arg;
			ret = mISDN_FsmEvent(&ch->i4lm, EV_I4L_BHUP, NULL);
			break;
		case ISDN_STAT_NODCH:
		case ISDN_STAT_L1ERR:
			if (c->arg < 0)
				return -1;
			ch += c->arg;
			ret = mISDN_FsmEvent(&ch->i4lm, EV_I4L_L1ERR, NULL);
			break;
		case ISDN_STAT_ADDCH:
		case ISDN_STAT_DISCH:
			// FIXME
			ret = 0;
			break;
		case ISDN_STAT_UNLOAD:
			ret = I4Lcapi.ctrl(drv->inst.st, MGR_DELSTACK | REQUEST, NULL);
			MOD_DEC_USE_COUNT;
			break;
		case CAPI_PUT_MESSAGE:
			// FIXME
			break;
		case ISDN_STAT_FAXIND:
			// FIXME
			break;
		case ISDN_STAT_AUDIO:
			// FIXME
			break;
	        case ISDN_STAT_PROT:
	        case ISDN_STAT_REDIR:
	        	// FIXME
			break;	        	
		default:
			break;
	}
	return(ret);
}  

static int
I4Lcapi_manager(void *data, u_int prim, void *arg) {
	i4l_capi_t	*card = I4Lcapi.ilist;
	mISDNinstance_t	*inst = data;
	i4l_channel_t	*channel = NULL;
	int		nr_ch = -2;

	if (debug & 0x100)
		printk(KERN_DEBUG "%s: data:%p prim:%x arg:%p\n",
			__FUNCTION__, data, prim, arg);
	if (prim == (MGR_HASPROTOCOL | REQUEST))
		return(mISDN_HasProtocolP(&I4Lcapi, arg));
	if (!data) {
		printk(KERN_ERR "I4Lcapi_manager no data prim %x arg %p\n",
			prim, arg);
		return(-EINVAL);
	}
	while(card) {
		if (&card->inst == inst) {
			nr_ch = -1;
			break;
		}
		channel = card->ch;
		for (nr_ch = 0; nr_ch < card->nr_ch; nr_ch++) {
			if (&channel->inst == inst)
				break;
			channel++;
		}
		if (nr_ch != card->nr_ch)
			break;
		card = card->next;
		channel = NULL;
		nr_ch = -2;
	}
	if (nr_ch == -2) {
		printk(KERN_ERR "I4Lcapi_manager no channel data %p prim %x arg %p\n",
			data, prim, arg);
		return(-EINVAL);
	}
	switch(prim) {
	    case MGR_REGLAYER | CONFIRM:
		break;
	    case MGR_UNREGLAYER | REQUEST:
		I4Lcapi.ctrl(inst->up.peer, MGR_DISCONNECT | REQUEST, &inst->up);
		I4Lcapi.ctrl(inst, MGR_UNREGLAYER | REQUEST, NULL);
		break;
	    case MGR_RELEASE | INDICATION:
		if (nr_ch == -1) {
			release_card(card->idx);
		} else {
			I4Lcapi.refcnt--;
		}
		break;
	    case MGR_CONNECT | REQUEST:
		return(mISDN_ConnectIF(inst, arg));
	    case MGR_SETIF | REQUEST:
	    case MGR_SETIF | INDICATION:
		if (nr_ch == -1)
			return(mISDN_SetIF(inst, arg, prim, Dchannel_i4l, NULL, card));
		else
			return(mISDN_SetIF(inst, arg, prim, Bchannel_i4l, NULL, channel));
	    case MGR_DISCONNECT | REQUEST:
	    case MGR_DISCONNECT | INDICATION:
		return(mISDN_DisConnectIF(inst, arg));
	    case MGR_SETSTACK | CONFIRM:
	    	if (nr_ch >= 0) {
			if (inst->pid.protocol[2] != ISDN_PID_L2_B_TRANS)
				test_and_set_bit(I4L_FLG_LAYER1, &channel->Flags);
			mISDN_FsmEvent(&channel->i4lm, EV_STACKREADY, NULL);
		}
		break;
	    default:
		if (debug)
			printk(KERN_DEBUG "I4Lcapi_manager prim %x not handled\n", prim);
		return(-EINVAL);
	}
	return(0);
}

static i4l_capi_reg_t	I4Lcapireg;

static int
I4Lcapi_register(isdn_if *iif)
{
	int		drvidx = 0;
	int		i, err;
	i4l_channel_t	*ch;

	if (!iif->writebuf_skb) {
		printk(KERN_ERR "I4Lcapi_register: No write routine given.\n");
		return 0;
	}
	for (drvidx=0; drvidx<MAX_CARDS; drvidx++) {
		if (drvmap[drvidx] == NULL)
			break;
	}
	if (drvidx == MAX_CARDS) {
		printk(KERN_ERR "I4Lcapi_register: no driver slot this card\n");
		return(0);
	}
	drvmap[drvidx] = kmalloc(sizeof(i4l_capi_t), GFP_KERNEL);
	if (!drvmap[drvidx]) {
		printk(KERN_ERR "I4Lcapi_register: no memory for i4l_capi_t\n");
		return(0);
	}
	memset(drvmap[drvidx], 0, sizeof(i4l_capi_t));
	drvmap[drvidx]->ch = kmalloc(iif->channels * sizeof(i4l_channel_t), GFP_KERNEL);
	if (!drvmap[drvidx]->ch) {
		printk(KERN_ERR "I4Lcapi_register: no memory for i4l_channel_t\n");
		kfree(drvmap[drvidx]);
		drvmap[drvidx] = NULL;
		return(0);
	}
	drvmap[drvidx]->idx = drvidx;
	drvmap[drvidx]->interface = iif;
	drvmap[drvidx]->nr_ch = iif->channels;
	iif->channels = drvidx;

	iif->rcvcallb_skb = I4Lcapi_receive_skb_callback;
	iif->statcallb = I4Lcapi_status_callback;

	APPEND_TO_LIST(drvmap[drvidx], ((i4l_capi_t *)I4Lcapi.ilist));
	drvmap[drvidx]->debug = debug;
	drvmap[drvidx]->inst.pid.layermask = ISDN_LAYER(0) | ISDN_LAYER(1) | ISDN_LAYER(2) | ISDN_LAYER(3);
	drvmap[drvidx]->inst.pid.protocol[0] = ISDN_PID_L0_TE_S0;
	drvmap[drvidx]->inst.pid.protocol[1] = ISDN_PID_L1_TE_S0;
	drvmap[drvidx]->inst.pid.protocol[2] = ISDN_PID_L2_LAPD;
	drvmap[drvidx]->inst.pid.protocol[3] = ISDN_PID_L3_DSS1USER;
	mISDN_init_instance(&drvmap[drvidx]->inst, &I4Lcapi, drvmap[drvidx]);
	sprintf(drvmap[drvidx]->inst.name, "Fritz%d", drvidx+1);
	mISDN_set_dchannel_pid(&drvmap[drvidx]->pid, 2, 0);
	for (i=0; i < drvmap[drvidx]->nr_ch; i++) {
		init_channel(drvmap[drvidx], i);
	}
	err = I4Lcapi.ctrl(NULL, MGR_NEWSTACK | REQUEST, &drvmap[drvidx]->inst);
	if (err) {
		release_card(drvidx);
		return(err);
	}
	ch = drvmap[drvidx]->ch;
	for (i=0; i < drvmap[drvidx]->nr_ch; i++) {
		err = I4Lcapi.ctrl(drvmap[drvidx]->inst.st, MGR_NEWSTACK | REQUEST, &ch->inst);
		if (err) {
			printk(KERN_ERR "MGR_ADDSTACK bchan error %d\n", err);
			I4Lcapi.ctrl(drvmap[drvidx]->inst.st, MGR_DELSTACK | REQUEST, NULL);
			return(err);
		}
		ch++;
	}
	MOD_INC_USE_COUNT;
	return(1);
}

static struct FsmNode I4LFnList[] =
{
	{ST_NULL,	EV_I4L_ICALL,		i4l_icall},
	{ST_NULL,	EV_CAPI_OCALL,		capi_ocall},
	{ST_ICALL,	EV_I4L_DHUP,		i4l_dhup},
	{ST_ICALL,	EV_I4L_L1ERR,		i4l_l1err},
	{ST_ICALL,	EV_CAPI_ALERT,		capi_alert},
	{ST_ICALL,	EV_CAPI_DCONNECT,	capi_connect},
	{ST_ICALL,	EV_CAPI_DISCONNECT,	capi_disconnect},
	{ST_ICALL,	EV_CAPI_RELEASE,	capi_release},
	{ST_OCALL,	EV_I4L_DHUP,		i4l_dhup},
	{ST_OCALL,	EV_I4L_L1ERR,		i4l_l1err},
	{ST_OCALL,	EV_CAPI_DISCONNECT,	capi_disconnect},
	{ST_OCALL,	EV_CAPI_RELEASE,	capi_release},
	{ST_OCALL,	EV_I4L_DCONN,		i4l_dconn_out},
	{ST_PROCEED,	EV_I4L_DHUP,		i4l_dhup},
	{ST_PROCEED,	EV_I4L_L1ERR,		i4l_l1err},
	{ST_PROCEED,	EV_CAPI_ALERT,		capi_alert},
	{ST_PROCEED,	EV_CAPI_DCONNECT,	capi_connect},
	{ST_PROCEED,	EV_CAPI_DISCONNECT,	capi_disconnect},
	{ST_PROCEED,	EV_CAPI_RELEASE,	capi_release},
	{ST_ALERT,	EV_I4L_DHUP,		i4l_dhup},
	{ST_ALERT,	EV_I4L_L1ERR,		i4l_l1err},
	{ST_ALERT,	EV_CAPI_DCONNECT,	capi_connect},
	{ST_ALERT,	EV_CAPI_DISCONNECT,	capi_disconnect},
	{ST_ALERT,	EV_CAPI_RELEASE,	capi_release},
	{ST_WAITDCONN,	EV_I4L_DCONN,		i4l_dconn_in},
	{ST_WAITDCONN,	EV_CAPI_DISCONNECT,	capi_disconnect},
	{ST_WAITDCONN,	EV_CAPI_RELEASE,	capi_release},
	{ST_WAITDCONN,	EV_I4L_DHUP,		i4l_dhup},
	{ST_ACTIVD,	EV_I4L_DHUP,		i4l_dhup},
	{ST_ACTIVD,	EV_I4L_L1ERR,		i4l_l1err},
	{ST_ACTIVD,	EV_CAPI_DISCONNECT,	capi_disconnect},
	{ST_ACTIVD,	EV_CAPI_RELEASE,	capi_release},
	{ST_ACTIVD,	EV_I4L_BCONN,		i4l_bconn_notready},
	{ST_ACTIVD,	EV_STACKREADY,		stackready},
	{ST_BREADY,	EV_CAPI_ESTABLISHB,	capi_establishb},
	{ST_BREADY,	EV_I4L_BCONN,		i4l_bconn},
	{ST_BREADY,	EV_I4L_DHUP,		i4l_dhup},
	{ST_BREADY,	EV_I4L_BHUP,		i4l_bhup},
	{ST_BREADY,	EV_I4L_L1ERR,		i4l_l1err},
	{ST_BREADY,	EV_CAPI_RELEASEB,	capi_releaseb},
	{ST_BREADY,	EV_CAPI_DISCONNECT,	capi_disconnect},
	{ST_BREADY,	EV_CAPI_RELEASE,	capi_release},
	{ST_ACTIVB,	EV_I4L_DHUP,		i4l_dhup},
	{ST_ACTIVB,	EV_I4L_BHUP,		i4l_bhup},
	{ST_ACTIVB,	EV_I4L_L1ERR,		i4l_l1err},
	{ST_ACTIVB,	EV_CAPI_DISCONNECT,	capi_disconnect},
	{ST_ACTIVB,	EV_CAPI_RELEASE,	capi_release},
	{ST_ACTIVB,	EV_CAPI_RELEASEB,	capi_releaseb},
	{ST_HANGUP,	EV_I4L_DHUP,		i4l_dhup},
	{ST_HANGUP,	EV_I4L_L1ERR,		i4l_l1err},
	{ST_HANGUP,	EV_CAPI_RELEASE,	capi_release},
};

#define I4L_FN_COUNT	(sizeof(I4LFnList)/sizeof(struct FsmNode))

static char	*I4L_capi_name = "I4L CAPI";

int
I4Lcapi_init(void)
{
	int err;

	printk(KERN_INFO "I4L CAPI interface modul version %s\n", mISDN_getrev(i4lcapi_revision));
#ifdef MODULE
	I4Lcapi.owner = THIS_MODULE;
#endif
	I4Lcapi.name = I4L_capi_name;
	I4Lcapi.own_ctrl = I4Lcapi_manager;
	I4Lcapi.DPROTO.protocol[0] = ISDN_PID_L0_TE_S0;
	I4Lcapi.DPROTO.protocol[1] = ISDN_PID_L1_TE_S0;
	I4Lcapi.DPROTO.protocol[2] = ISDN_PID_L2_LAPD | ISDN_PID_L2_DF_PTP;
	I4Lcapi.DPROTO.protocol[3] = ISDN_PID_L3_DSS1USER | ISDN_PID_L3_DF_PTP;
	I4Lcapi.BPROTO.protocol[1] = ISDN_PID_L1_B_64TRANS | ISDN_PID_L1_B_64HDLC;
	I4Lcapi.BPROTO.protocol[2] = ISDN_PID_L2_B_TRANS;
	I4Lcapi.prev = NULL;
	I4Lcapi.next = NULL;
	
	i4lfsm_s.state_count = STATE_COUNT;
	i4lfsm_s.event_count = EVENT_COUNT;
	i4lfsm_s.strEvent = strI4LEvent;
	i4lfsm_s.strState = strI4LState;
	mISDN_FsmNew(&i4lfsm_s, I4LFnList, I4L_FN_COUNT);
	if ((err = mISDN_register(&I4Lcapi))) {
		printk(KERN_ERR "Can't register I4L CAPI error(%d)\n", err);
		mISDN_FsmFree(&i4lfsm_s);
		return(err);
	}
	I4Lcapireg.register_func = I4Lcapi_register;
	strcpy(I4Lcapireg.name, "I4L CAPI");
	err = register_i4lcapi(&I4Lcapireg);
	printk(KERN_INFO "registered I4L CAPI %s err(%d)\n", i4lcapi_revision, err);
	return(0);
}

#ifdef MODULE
void
I4Lcapi_cleanup(void)
{
	
	int err;
	if ((err = mISDN_unregister(&I4Lcapi))) {
		printk(KERN_ERR "Can't unregister I4Lcapi error(%d)\n", err);
		return;
	}
	while(I4Lcapi.ilist) {
		printk(KERN_ERR "I4Lcapi card struct not empty refs %d\n",
			I4Lcapi.refcnt);
		release_card(((i4l_capi_t *)I4Lcapi.ilist)->idx);
	}
	mISDN_FsmFree(&i4lfsm_s);
	unregister_i4lcapi();
	return;
}

module_init(I4Lcapi_init);
module_exit(I4Lcapi_cleanup);

#endif

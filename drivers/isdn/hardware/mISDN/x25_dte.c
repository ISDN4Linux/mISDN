/* $Id: x25_dte.c,v 1.6 2004/06/17 12:31:12 keil Exp $
 *
 * Linux modular ISDN subsystem, mISDN
 * X.25/X.31 Layer3 for DTE mode   
 *
 * Author	Karsten Keil (kkeil@suse.de)
 *
 * Copyright 2003 by Karsten Keil (kkeil@suse.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include "x25_l3.h"
#include "helper.h"
#include "debug.h"

static int debug = 0;

static mISDNobject_t x25dte_obj;

static char *mISDN_dte_revision = "$Revision: 1.6 $";

/* local prototypes */
static x25_channel_t *	dte_create_channel(x25_l3_t *, int, u_char, __u16, int, u_char *);


/* X.25 Restart state machine */
static struct Fsm dte_rfsm = {NULL, 0, 0, NULL, NULL};

/* X.25 connection state machine */
static struct Fsm dte_pfsm = {NULL, 0, 0, NULL, NULL};

/* X.25 Flowcontrol state machine */
static struct Fsm dte_dfsm = {NULL, 0, 0, NULL, NULL};


/* X.25 Restart state machine implementation */

static void
r_llready(struct FsmInst *fi, int event, void *arg)
{
	x25_l3_t *l3 = fi->userdata;

	mISDN_FsmChangeState(fi, ST_R1);
	if (test_and_clear_bit(X25_STATE_ESTABLISH, &l3->state)) {
		mISDN_FsmEvent(&l3->x25r, EV_L3_RESTART_REQ, NULL);
	}
}

static void
r_r0_restart_l3(struct FsmInst *fi, int event, void *arg)
{
	x25_l3_t	*l3 = fi->userdata;

	if (arg)
		memcpy(l3->cause, arg, 2);
	else
		memset(l3->cause, 0, 2);
	test_and_set_bit(X25_STATE_ESTABLISH, &l3->state);
	mISDN_FsmEvent(&l3->l2l3m, EV_L3_ESTABLISH_REQ, NULL);
}

static void
r_restart_l3(struct FsmInst *fi, int event, void *arg)
{
	x25_l3_t	*l3 = fi->userdata;

	if (arg)
		memcpy(l3->cause, arg, 2);
	mISDN_FsmChangeState(fi, ST_R2);
	l3->TRval = T20_VALUE;
	l3->TRrep = R20_VALUE;
	X25sendL3frame(NULL, l3, X25_PTYPE_RESTART, 2, l3->cause);
	mISDN_FsmAddTimer(&l3->TR, l3->TRval, EV_L3_RESTART_TOUT, NULL, 0);
}

static void
r_restart_ind(struct FsmInst *fi, int event, void *arg)
{
	x25_l3_t	*l3 = fi->userdata;

	mISDN_FsmChangeState(fi, ST_R3);
	X25sendL3frame(NULL, l3, X25_PTYPE_RESTART_CNF, 0, NULL);
	mISDN_FsmChangeState(fi, ST_R1);
	mISDN_FsmDelTimer(&l3->TR, 0);
	X25_restart(l3);
}

static void
r_restart_cnf(struct FsmInst *fi, int event, void *arg)
{
	x25_l3_t	*l3 = fi->userdata;

	mISDN_FsmChangeState(fi, ST_R1);
	mISDN_FsmDelTimer(&l3->TR, 0);
	X25_restart(l3);
}

static void
r_restart_cnf_err(struct FsmInst *fi, int event, void *arg)
{
	x25_l3_t	*l3 = fi->userdata;
	u_char		cause[2] = {0, 17};

	if (fi->state == ST_R3)
		cause[1] = 19;
	mISDN_FsmEvent(&l3->x25r, EV_L3_RESTART_REQ, cause);
}

static void
r_timeout(struct FsmInst *fi, int event, void *arg)
{
	x25_l3_t	*l3 = fi->userdata;

	if (l3->TRrep) {
		X25sendL3frame(NULL, l3, X25_PTYPE_RESTART, 2, l3->cause);
		mISDN_FsmRestartTimer(&l3->TR, l3->TRval, EV_L3_RESTART_TOUT, NULL, 0);
		l3->TRrep--;
	} else {
		mISDN_FsmDelTimer(&l3->TR, 0);
		mISDN_FsmChangeState(fi, ST_R1);
		/* signal failure */
	}
}

/* *INDENT-OFF* */
static struct FsmNode RFnList[] =
{
	{ST_R0,	EV_LL_READY,		r_llready},
	{ST_R0,	EV_L3_RESTART_REQ,	r_r0_restart_l3},
	{ST_R1,	EV_LL_READY,		r_llready},
	{ST_R1,	EV_L3_RESTART_REQ,	r_restart_l3},
	{ST_R1,	EV_L2_RESTART,		r_restart_ind},
	{ST_R1,	EV_L2_RESTART_CNF,	r_restart_cnf_err},
	{ST_R2,	EV_L3_RESTART_REQ,	r_restart_l3},
	{ST_R2,	EV_L2_RESTART,		r_restart_ind},
	{ST_R2,	EV_L2_RESTART_CNF,	r_restart_cnf},
	{ST_R2,	EV_L3_RESTART_TOUT,	r_timeout},
	{ST_R2,	EV_LL_READY,		r_llready},
	{ST_R3,	EV_L3_RESTART_REQ,	r_restart_l3},
	{ST_R3,	EV_L2_RESTART_CNF,	r_restart_cnf_err},
	{ST_R3,	EV_LL_READY,		r_llready},
};
/* *INDENT-ON* */

#define R_FN_COUNT (sizeof(RFnList)/sizeof(struct FsmNode))

/* X.25 connection state machine */

static void
p_p0_ready(struct FsmInst *fi, int event, void *arg)
{
	x25_channel_t	*l3c = fi->userdata;

	if (test_bit(X25_STATE_PERMANENT, &l3c->state)) {
		mISDN_FsmChangeState(fi, ST_P4);
		/* connect */
		mISDN_FsmEvent(&l3c->x25d, EV_L3_CONNECT, NULL);
	} else {
		mISDN_FsmChangeState(fi, ST_P1);
		if (test_bit(X25_STATE_ORGINATE, &l3c->state))
			mISDN_FsmEvent(fi, EV_L3_OUTGOING_CALL, NULL);
	}
}

static void
X25_clear_connection(x25_channel_t *l3c, struct sk_buff *skb, int reason)
{
	if (skb) {
		if (test_bit(X25_STATE_DBIT, &l3c->state))
			reason |= 0x10000;
		X25sendL4frame(l3c, l3c->l3, CAPI_DISCONNECT_B3_IND, reason, skb->len, skb->data);
	} else
		X25sendL4frame(l3c, l3c->l3, CAPI_DISCONNECT_B3_IND, reason, 0, NULL);
}

static void
p_p0_outgoing(struct FsmInst *fi, int event, void *arg)
{
}

static void
p_ready(struct FsmInst *fi, int event, void *arg)
{
//	x25_channel_t	*l3c = fi->userdata;
}

static void
p_outgoing(struct FsmInst *fi, int event, void *arg)
{
	x25_channel_t	*l3c = fi->userdata;
	struct sk_buff	*skb = arg;

	mISDN_FsmChangeState(fi, ST_P2);
	if (skb)
		X25sendL3frame(l3c, l3c->l3, X25_PTYPE_CALL, skb->len, skb->data);
	else
		X25sendL3frame(l3c, l3c->l3, X25_PTYPE_CALL, l3c->ncpi_len, l3c->ncpi_data);
	mISDN_FsmAddTimer(&l3c->TP, T21_VALUE, EV_L3_CALL_TOUT, NULL, 0);
}

static void
p_incoming(struct FsmInst *fi, int event, void *arg)
{
	x25_channel_t	*l3c = fi->userdata;
	int		flg = 0;

	if (test_bit(X25_STATE_DBIT, &l3c->state))
		flg = 0x10000;
	mISDN_FsmChangeState(fi, ST_P3);
	X25sendL4frame(l3c, l3c->l3, CAPI_CONNECT_B3_IND, flg, l3c->ncpi_len, l3c->ncpi_data);
}

static void
p_call_accept(struct FsmInst *fi, int event, void *arg)
{
	x25_channel_t	*l3c = fi->userdata;
	struct sk_buff	*skb = arg;

	if (skb)
		X25sendL3frame(l3c, l3c->l3, X25_PTYPE_CALL_CNF, skb->len, skb->data);
	else
		X25sendL3frame(l3c, l3c->l3, X25_PTYPE_CALL_CNF, 0, NULL);
	mISDN_FsmChangeState(fi, ST_P4);
	mISDN_FsmEvent(&l3c->x25d, EV_L3_CONNECT, NULL);
	X25sendL4frame(l3c, l3c->l3, CAPI_CONNECT_B3_ACTIVE_IND, 0, 0, NULL);
}

static void
p_collision(struct FsmInst *fi, int event, void *arg)
{
	mISDN_FsmChangeState(fi, ST_P5);
}

static void
p_connect(struct FsmInst *fi, int event, void *arg)
{
	x25_channel_t	*l3c = fi->userdata;
	struct sk_buff	*skb = arg;
	int		flg = 0;

	mISDN_FsmDelTimer(&l3c->TP, 0);
	mISDN_FsmChangeState(fi, ST_P4);
	if (test_bit(X25_STATE_DBIT, &l3c->state))
		flg = 0x10000;
	mISDN_FsmEvent(&l3c->x25d, EV_L3_CONNECT, NULL);
	if (skb)
		X25sendL4frame(l3c, l3c->l3, CAPI_CONNECT_B3_ACTIVE_IND, flg, skb->len, skb->data);
	else
		X25sendL4frame(l3c, l3c->l3, CAPI_CONNECT_B3_ACTIVE_IND, flg, 0, NULL);
}

static void
p_call_timeout(struct FsmInst *fi, int event, void *arg)
{
	x25_channel_t	*l3c = fi->userdata;

	mISDN_FsmChangeState(fi, ST_P6);
	l3c->cause[0] = 0;
	l3c->cause[1] = 49;
	l3c->TPval = T23_VALUE;
	l3c->TPrep = R23_VALUE;
	mISDN_FsmAddTimer(&l3c->TP, l3c->TPval, EV_L3_CLEAR_TOUT, NULL, 0);
	X25sendL3frame(l3c, l3c->l3, X25_PTYPE_CLEAR, 2, l3c->cause);
}

static void
p_clear_ind(struct FsmInst *fi, int event, void *arg)
{
	x25_channel_t	*l3c = fi->userdata;

	mISDN_FsmChangeState(fi, ST_P7);
	mISDN_FsmDelTimer(&l3c->TP, 0);
	X25sendL3frame(l3c, l3c->l3, X25_PTYPE_CLEAR_CNF, 0, NULL);
	mISDN_FsmChangeState(fi, ST_P1);
	X25_clear_connection(l3c, arg, 0);
}

static void
p_clear_cnf(struct FsmInst *fi, int event, void *arg)
{
	x25_channel_t	*l3c = fi->userdata;

	mISDN_FsmChangeState(fi, ST_P1);
	mISDN_FsmDelTimer(&l3c->TP, 0);
	X25_clear_connection(l3c, arg, 0);
}

static void
p_clear_timeout(struct FsmInst *fi, int event, void *arg)
{
	x25_channel_t	*l3c = fi->userdata;

	if (l3c->TPrep) {
		mISDN_FsmAddTimer(&l3c->TP, l3c->TPval, EV_L3_CLEAR_TOUT, NULL, 0);
		X25sendL3frame(l3c, l3c->l3, X25_PTYPE_CLEAR, 2, l3c->cause);
	} else {
		l3c->cause[0] = 0;
		l3c->cause[0] = 50;
		mISDN_FsmChangeState(fi, ST_P1);
		X25_clear_connection(l3c, NULL, 0x3303);
	}
}

static void
p_clearing_req(struct FsmInst *fi, int event, void *arg)
{
	x25_channel_t	*l3c = fi->userdata;
	struct sk_buff	*skb = arg;

	mISDN_FsmChangeState(fi, ST_P6);
	l3c->TPval = T23_VALUE;
	l3c->TPrep = R23_VALUE;
	mISDN_FsmAddTimer(&l3c->TP, l3c->TPval, EV_L3_CLEAR_TOUT, NULL, 0);
	if (skb) {
		if (skb->len >= 2) {
			memcpy(l3c->cause, skb->data, 2);
			X25sendL3frame(l3c, l3c->l3, X25_PTYPE_CLEAR, skb->len, skb->data);
			return;
		}
		l3c->cause[0] = 0;
		l3c->cause[1] = 0;
	}
	X25sendL3frame(l3c, l3c->l3, X25_PTYPE_CLEAR, 2, l3c->cause);
}

static void
p_invalid_pkt(struct FsmInst *fi, int event, void *arg)
{
	x25_channel_t	*l3c = fi->userdata;

	l3c->cause[0] = 0;
	switch(fi->state) {
		case ST_P1:
			l3c->cause[1] = 20;
			break;
		case ST_P2:
			l3c->cause[1] = 21;
			break;
		case ST_P3:
			l3c->cause[1] = 22;
			break;
		case ST_P4:
			l3c->cause[1] = 23;
			break;
		case ST_P5:
			l3c->cause[1] = 24;
			break;
		case ST_P6:
			l3c->cause[1] = 25;
			break;
		case ST_P7:
			l3c->cause[1] = 26;
			break;
		default:
			l3c->cause[1] = 16;
			break;
	}
	X25sendL3frame(l3c, l3c->l3, X25_PTYPE_CLEAR, 2, l3c->cause);
}

/* *INDENT-OFF* */
static struct FsmNode PFnList[] =
{
	{ST_P0,	EV_L3_READY,		p_p0_ready},
	{ST_P0,	EV_L3_OUTGOING_CALL,	p_p0_outgoing},
	{ST_P0, EV_L3_CLEARING,		p_clearing_req},
	{ST_P1,	EV_L3_READY,		p_ready},
	{ST_P1,	EV_L3_OUTGOING_CALL,	p_outgoing},		
	{ST_P1,	EV_L2_INCOMING_CALL,	p_incoming},
	{ST_P1,	EV_L2_CLEAR,		p_clear_ind},
	{ST_P1,	EV_L2_CALL_CNF,		p_invalid_pkt},
	{ST_P1,	EV_L2_CLEAR_CNF,	p_invalid_pkt},
	{ST_P1,	EV_L2_INVALPKT,		p_invalid_pkt},
	{ST_P1, EV_L3_CLEARING,		p_clearing_req},
	{ST_P2,	EV_L2_INCOMING_CALL,	p_collision},
	{ST_P2,	EV_L2_CALL_CNF,		p_connect},
	{ST_P2,	EV_L2_CLEAR,		p_clear_ind},
	{ST_P2,	EV_L3_CALL_TOUT,	p_call_timeout},
	{ST_P2,	EV_L2_CLEAR_CNF,	p_invalid_pkt},
	{ST_P2,	EV_L2_INVALPKT,		p_invalid_pkt},
	{ST_P2, EV_L3_CLEARING,		p_clearing_req},
	{ST_P3, EV_L3_CALL_ACCEPT,	p_call_accept},
 	{ST_P3,	EV_L2_CLEAR,		p_clear_ind},
	{ST_P3,	EV_L2_INCOMING_CALL,	p_invalid_pkt},
	{ST_P3,	EV_L2_CALL_CNF,		p_invalid_pkt},
	{ST_P3,	EV_L2_CLEAR_CNF,	p_invalid_pkt},
	{ST_P3,	EV_L2_INVALPKT,		p_invalid_pkt},
	{ST_P3, EV_L3_CLEARING,		p_clearing_req},
	{ST_P4,	EV_L2_CLEAR,		p_clear_ind},
	{ST_P4,	EV_L2_INCOMING_CALL,	p_invalid_pkt},
	{ST_P4,	EV_L2_CALL_CNF,		p_invalid_pkt},
	{ST_P4,	EV_L2_CLEAR_CNF,	p_invalid_pkt},
	{ST_P4, EV_L3_CLEARING,		p_clearing_req},
	{ST_P5,	EV_L2_CALL_CNF,		p_connect},
	{ST_P5, EV_L3_CALL_ACCEPT,	p_call_accept},
	{ST_P5,	EV_L2_CLEAR,		p_clear_ind},
	{ST_P5,	EV_L3_CALL_TOUT,	p_call_timeout},
	{ST_P5,	EV_L2_INCOMING_CALL,	p_invalid_pkt},
	{ST_P5,	EV_L2_CLEAR_CNF,	p_invalid_pkt},
	{ST_P5,	EV_L2_INVALPKT,		p_invalid_pkt},
	{ST_P5, EV_L3_CLEARING,		p_clearing_req},
	{ST_P6,	EV_L2_CLEAR_CNF,	p_clear_cnf},
	{ST_P6,	EV_L3_CLEAR_TOUT,	p_clear_timeout},
	{ST_P6, EV_L3_CLEARING,		p_clearing_req},
	{ST_P7,	EV_L2_INCOMING_CALL,	p_invalid_pkt},
	{ST_P7,	EV_L2_CALL_CNF,		p_invalid_pkt},
	{ST_P7,	EV_L2_CLEAR_CNF,	p_invalid_pkt},
	{ST_P7,	EV_L2_INVALPKT,		p_invalid_pkt},
};
/* *INDENT-ON* */

#define P_FN_COUNT (sizeof(PFnList)/sizeof(struct FsmNode))

static void
d_connect(struct FsmInst *fi, int event, void *arg)
{
	mISDN_FsmChangeState(fi, ST_D1);
}

static void
d_reset_req(struct FsmInst *fi, int event, void *arg)
{
	x25_channel_t	*l3c = fi->userdata;
	struct sk_buff	*skb = arg;

	mISDN_FsmChangeState(fi, ST_D2);
	l3c->TDval = T22_VALUE;
	l3c->TDrep = R22_VALUE;
	mISDN_FsmAddTimer(&l3c->TD, l3c->TDval, EV_L3_RESET_TOUT, NULL, 0);
	if (skb) {
		if (skb->len >= 2) {
			memcpy(l3c->cause, skb->data, 2);
			X25sendL3frame(l3c, l3c->l3, X25_PTYPE_RESET, skb->len, skb->data);
			return;
		}
		l3c->cause[0] = 0;
		l3c->cause[1] = 0;
	}
	X25sendL3frame(l3c, l3c->l3, X25_PTYPE_RESET, 2, l3c->cause);
}

static void
d_reset_ind(struct FsmInst *fi, int event, void *arg)
{
	x25_channel_t	*l3c = fi->userdata;

	mISDN_FsmChangeState(fi, ST_D3);
	X25_reset_channel(l3c, arg);
	mISDN_FsmChangeState(fi, ST_D1);
	/* TODO normal operation trigger */
}

static void
d_reset_cnf(struct FsmInst *fi, int event, void *arg)
{
	x25_channel_t	*l3c = fi->userdata;

	mISDN_FsmDelTimer(&l3c->TD, 0);
	X25_reset_channel(l3c, NULL);
	/* TODO normal opration trigger */
	mISDN_FsmChangeState(fi, ST_D1);
}

static void
d_reset_cnf_err(struct FsmInst *fi, int event, void *arg)
{
	x25_channel_t	*l3c = fi->userdata;

	if (arg)
		memcpy(l3c->cause, arg, 2);
	mISDN_FsmChangeState(fi, ST_D2);
	l3c->TDval = T22_VALUE;
	l3c->TDrep = R22_VALUE;
	X25sendL3frame(l3c, l3c->l3, X25_PTYPE_RESET, 2, l3c->cause);
	mISDN_FsmAddTimer(&l3c->TD, l3c->TDval, EV_L3_RESET_TOUT, NULL, 0);
}

static void
d_reset_timeout(struct FsmInst *fi, int event, void *arg)
{
	x25_channel_t	*l3c = fi->userdata;

	if (l3c->TDrep) {
		X25sendL3frame(l3c, l3c->l3, X25_PTYPE_RESET, 2, l3c->cause);
		l3c->TDrep--;
	} else {
		l3c->cause[0] = 0;
		l3c->cause[1] = 51;
		mISDN_FsmChangeState(fi, ST_D0);
		if (test_bit(X25_STATE_PERMANENT, &l3c->state))
			X25_clear_connection(l3c, NULL, 0x3303); 
		else
			mISDN_FsmEvent(&l3c->x25p, EV_L3_CLEARING, NULL);
	}
}

/* *INDENT-OFF* */
static struct FsmNode DFnList[] =
{
	{ST_D0,	EV_L3_CONNECT,		d_connect},
	{ST_D1,	EV_L2_RESET,		d_reset_ind},
	{ST_D1,	EV_L2_RESET_CNF,        d_reset_cnf_err},
	{ST_D1,	EV_L3_RESETING,		d_reset_req},
	{ST_D2,	EV_L2_RESET,		d_reset_cnf},
	{ST_D2,	EV_L2_RESET_CNF,        d_reset_cnf},
	{ST_D3,	EV_L2_RESET_CNF,        d_reset_cnf_err},
	{ST_D3,	EV_L3_RESETING,		d_reset_req},
	{ST_D1, EV_L3_RESET_TOUT,	d_reset_timeout},
};
/* *INDENT-ON* */

#define D_FN_COUNT (sizeof(DFnList)/sizeof(struct FsmNode))

static int
got_diagnositic(x25_l3_t *l3, struct sk_buff *skb, u_char gfi, __u16 channel)
{
	/* not implemented yet */
	return(X25_ERRCODE_DISCARD);
}

static int
got_register(x25_l3_t *l3, struct sk_buff *skb, u_char gfi, __u16 channel)
{
	/* not implemented yet */
	return(X25_ERRCODE_DISCARD);
}

static int
got_register_cnf(x25_l3_t *l3, struct sk_buff *skb, u_char gfi, __u16 channel)
{
	/* not implemented yet */
	return(X25_ERRCODE_DISCARD);
}

static int
dte_data_ind_d(x25_channel_t *chan, struct sk_buff *skb, u_char gfi, u_char ptype)
{
	int	pr_m, ps, event;
	u_char  ptype_s = ptype;

	if ((ptype & 1) == 0)
		ptype = X25_PTYPE_DATA;
	else {
		if ((!test_bit(X25_STATE_MOD128, &chan->state)) &&
			!test_bit(X25_STATE_MOD32768, &chan->state))
			ptype = ptype & 0x1f;
		if ((ptype != X25_PTYPE_RR) &&
			(ptype != X25_PTYPE_RNR) &&
			(ptype != X25_PTYPE_REJ))
			ptype = ptype_s;
	}
	switch (ptype) {
		case X25_PTYPE_RESET:
			event = EV_L2_RESET;
			break;
		case X25_PTYPE_RESET_CNF:
			event = EV_L2_RESET_CNF;
			break;
		case X25_PTYPE_NOTYPE:
			chan->cause[0] = 0;
			chan->cause[1] = 38;
			event = EV_L3_RESETING;
			break;
		case X25_PTYPE_RESTART:
		case X25_PTYPE_RESTART_CNF:
			chan->cause[0] = 0;
			chan->cause[1] = 41;
			event = EV_L3_RESETING;
			break;
		case X25_PTYPE_INTERRUPT:
		case X25_PTYPE_INTERRUPT_CNF:
		case X25_PTYPE_DATA:
		case X25_PTYPE_RR:
		case X25_PTYPE_RNR:
		case X25_PTYPE_REJ:
			if (chan->x25d.state == ST_D2)
				return(X25_ERRCODE_DISCARD);
			else if (chan->x25d.state == ST_D3) {
				chan->cause[0] = 0;
				chan->cause[1] = 29;
				event = EV_L3_RESETING;
			} else
				event = -1;
			break;
		default:
			/* unknown paket type */
			chan->cause[0] = 0;
			chan->cause[1] = 33;
			event = EV_L3_RESETING;
			break;
	}
	if (event != -1) {
		if (event == EV_L3_RESETING)
			mISDN_FsmEvent(&chan->x25d, event, NULL);
		else
			mISDN_FsmEvent(&chan->x25d, event, skb);
		return(X25_ERRCODE_DISCARD);
	}
	switch (ptype) {
		case X25_PTYPE_INTERRUPT:
			if (test_and_set_bit(X25_STATE_DXE_INTSENT, &chan->state)) {
				chan->cause[0] = 0;
				chan->cause[1] = 44;
				mISDN_FsmEvent(&chan->x25d, EV_L3_RESETING, NULL);
			} else {
				// X25_got_interrupt(chan, skb);
			}
			break;
		case X25_PTYPE_INTERRUPT_CNF:
			if (!test_and_clear_bit(X25_STATE_DTE_INTSENT, &chan->state)) {
				chan->cause[0] = 0;
				chan->cause[1] = 43;
				mISDN_FsmEvent(&chan->x25d, EV_L3_RESETING, NULL);
			}
			break;
		case X25_PTYPE_RR:
		case X25_PTYPE_RNR:
		case X25_PTYPE_REJ:
			pr_m = X25_get_and_test_pr(chan, ptype_s, skb);
			if (pr_m < 0) {
				chan->cause[0] = 0;
				chan->cause[1] = -pr_m;
				mISDN_FsmEvent(&chan->x25d, EV_L3_RESETING, NULL);
			} else if (skb->len) {
				chan->cause[0] = 0;
				chan->cause[1] = 39;
				mISDN_FsmEvent(&chan->x25d, EV_L3_RESETING, NULL);
			} else {
				if (ptype == X25_PTYPE_RR) {
					test_and_clear_bit(X25_STATE_DXE_RNR, &chan->state);
					if (X25_cansend(chan))
						X25_invoke_sending(chan);
				} else if (ptype == X25_PTYPE_RNR) {
					if (!test_and_set_bit(X25_STATE_DXE_RNR, &chan->state)) {
						/* action for DXE RNR */
					}
				} else {
					/* TODO REJ */
				}
			}
			break;
		case X25_PTYPE_DATA:
			ps = X25_get_and_test_ps(chan, ptype_s, skb);
			if (ps == -38)
				pr_m = ps;
			else
				pr_m = X25_get_and_test_pr(chan, ptype_s, skb);
			if (pr_m < 0) {
				chan->cause[0] = 0;
				chan->cause[1] = -pr_m;
				mISDN_FsmEvent(&chan->x25d, EV_L3_RESETING, NULL);
			} else if (ps < 0) {
				chan->cause[0] = 0;
				chan->cause[1] = -ps;
				mISDN_FsmEvent(&chan->x25d, EV_L3_RESETING, NULL);
			} else {
				int flag = (pr_m & 1) ? CAPI_FLAG_MOREDATA : 0;
				
				if (gfi & X25_GFI_QBIT)
					flag |= CAPI_FLAG_QUALIFIER;
				if (gfi & X25_GFI_DBIT)
					flag |= CAPI_FLAG_DELIVERCONF;
				return(X25_receive_data(chan, ps, flag, skb));
			}
			break;
	}
	return(X25_ERRCODE_DISCARD);
}

static int
dte_data_ind_p(x25_l3_t *l3, struct sk_buff *skb, u_char gfi, __u16 channel, u_char ptype)
{
	x25_channel_t	*chan = X25_get_channel(l3, channel);
	int		event, ret = X25_ERRCODE_DISCARD;

	if (ptype == X25_PTYPE_CALL) {
		if (!chan)
			chan = dte_create_channel(l3, X25_CHANNEL_INCOMING, gfi, channel, skb->len, skb->data);
		if (chan) {
			mISDN_FsmEvent(&chan->x25p, EV_L2_INCOMING_CALL, skb);
		} else {
			ret = 36; /* unassigned channel */
		}
		return(ret);
	}
	if (!chan)
		return(36); /* unassigned channel */
	switch (ptype) {
		case X25_PTYPE_CALL_CNF:
			event = EV_L2_CALL_CNF;
			break;
		case X25_PTYPE_CLEAR:
			event = EV_L2_CLEAR;
			break;
		case X25_PTYPE_CLEAR_CNF:
			event = EV_L2_CLEAR_CNF;
			break;
		case X25_PTYPE_NOTYPE:
			chan->cause[0] = 0;
			chan->cause[1] = 38;
			event = EV_L3_CLEARING;
			break;
		case X25_PTYPE_RESTART:
		case X25_PTYPE_RESTART_CNF:
			chan->cause[0] = 0;
			chan->cause[1] = 41;
			event = EV_L3_CLEARING;
			break;
		case X25_PTYPE_RESET:
		case X25_PTYPE_RESET_CNF:
		case X25_PTYPE_INTERRUPT:
		case X25_PTYPE_INTERRUPT_CNF:
			event = EV_L2_INVALPKT;
			break;
		default:
			if ((ptype & 1) == 0) {
				event = EV_L2_INVALPKT;
				break;
			}
			if (!test_bit(X25_STATE_MOD128, &chan->state) &&
				!test_bit(X25_STATE_MOD32768, &chan->state))
				event = ptype & 0x1f;
			else
				event = ptype;
			if ((event == X25_PTYPE_RR) ||
				(event == X25_PTYPE_RNR) ||
				(event == X25_PTYPE_REJ)) {
				event = EV_L2_INVALPKT;
				break;
			}
			/* unknown paket type */
			chan->cause[0] = 0;
			chan->cause[1] = 33;
			event = EV_L3_CLEARING;
			break;
	}
	if (chan->x25p.state == ST_P4) {
		if ((event == EV_L2_INVALPKT) || (event == EV_L3_CLEARING))
			return(dte_data_ind_d(chan, skb, gfi, ptype));
	}
	if (event == EV_L3_CLEARING)
		mISDN_FsmEvent(&chan->x25p, event, NULL);
	else
		mISDN_FsmEvent(&chan->x25p, event, skb);
	return(ret);
}

static int
dte_data_ind_r(x25_l3_t *l3, struct sk_buff *skb, u_char gfi, __u16 channel, u_char ptype)
{
	int	ret = X25_ERRCODE_DISCARD;

	
	if (ptype == X25_PTYPE_NOTYPE) {
		if (channel) {
			if (l3->x25r.state == ST_R1)
				return(dte_data_ind_p(l3, skb, gfi, channel, ptype));
			if (l3->x25r.state == ST_R2) {
				l3->cause[0] = 0;
				l3->cause[1] = 38;
				mISDN_FsmEvent(&l3->x25r, EV_L3_RESTART_REQ, NULL);
			}
		} else {
			if (l3->x25r.state == ST_R1)
				ret = 38; // packet too short
			else if (l3->x25r.state == ST_R3) {
				l3->cause[0] = 0;
				l3->cause[1] = 38;
				mISDN_FsmEvent(&l3->x25r, EV_L3_RESTART_REQ, NULL);
			}
		}
		return(ret);
	}
	if ((ptype == X25_PTYPE_RESTART) || (ptype == X25_PTYPE_RESTART_CNF)) {
		if (channel) {
			if (l3->x25r.state == ST_R1)
				return(dte_data_ind_p(l3, skb, gfi, channel, ptype));
			else if (l3->x25r.state == ST_R3) {
				l3->cause[0] = 0;
				l3->cause[1] = 41;
				mISDN_FsmEvent(&l3->x25r, EV_L3_RESTART_REQ, NULL);
			}
			return(ret);
		}
		if (ptype == X25_PTYPE_RESTART)
			mISDN_FsmEvent(&l3->x25r, EV_L2_RESTART, skb);
		else
			mISDN_FsmEvent(&l3->x25r, EV_L2_RESTART_CNF, skb);
	} else {
		if (l3->x25r.state == ST_R1)
			return(dte_data_ind_p(l3, skb, gfi, channel, ptype));
		if (l3->x25r.state == ST_R3) {
			l3->cause[0] = 0;
			l3->cause[1] = 19;
			mISDN_FsmEvent(&l3->x25r, EV_L3_RESTART_REQ, NULL);
		}
	}
	return(ret);
}

static int
dte_dl_data_ind(x25_l3_t *l3, struct sk_buff *skb)
{
	int	ret;
	__u16	channel;
	u_char	gfi, ptype = 0;

	ret = X25_get_header(l3, skb, &gfi, &channel, &ptype);
	if (ret && (ptype != X25_PTYPE_NOTYPE)) {
		if (test_bit(X25_STATE_DTEDTE, &l3->state))
			X25_send_diagnostic(l3, skb, ret, channel);
		dev_kfree_skb(skb);
		return(0);
	}
	switch(ptype) {
		case X25_PTYPE_DIAGNOSTIC:
			ret = got_diagnositic(l3, skb, gfi, channel);
			break;
		case X25_PTYPE_REGISTER:
			ret = got_register(l3, skb, gfi, channel);
			break;
		case X25_PTYPE_REGISTER_CNF:
			ret = got_register_cnf(l3, skb, gfi, channel);
			break;
		default:
			ret = dte_data_ind_r(l3, skb, gfi, channel, ptype);
			break;
	}
	if (ret == X25_ERRCODE_DISCARD) {
		dev_kfree_skb(skb);
		ret = 0;
	} else if (ret) {
		if (test_bit(X25_STATE_DTEDTE, &l3->state)) {
			if (ptype != X25_PTYPE_NOTYPE) {
				if (test_bit(X25_STATE_MOD32768, &l3->state))
					skb_push(skb, 4);
				else
					skb_push(skb, 3);
			}
			X25_send_diagnostic(l3, skb, ret, channel);
		}
		dev_kfree_skb(skb);
		ret = 0;
	}
	return(ret);
}

static int
dte_from_down(mISDNif_t *hif,  struct sk_buff *skb)
{
	x25_l3_t	*l3;
	mISDN_head_t	*hh;
	int		ret = -EINVAL;

	if (!hif || !hif->fdata || !skb)
		return(ret);
	l3 = hif->fdata;
	if (!l3->inst.up.func) {
		return(-ENXIO);
	}
	hh = mISDN_HEAD_P(skb);
	if (l3->debug)
		printk(KERN_DEBUG "%s: prim(%x) dinfo(%x)\n", __FUNCTION__, hh->prim, hh->dinfo);
	switch(hh->prim) {
		case DL_DATA_IND:
			ret = dte_dl_data_ind(l3, skb);
			break;
		case DL_DATA | CONFIRM:
			break;
		case DL_ESTABLISH_CNF:
			ret = mISDN_FsmEvent(&l3->l2l3m, EV_LL_ESTABLISH_CNF, NULL);
			if (ret) {
				int_error();
			}
			ret = 0;
			dev_kfree_skb(skb);
			break;
		case DL_ESTABLISH_IND:
			ret = mISDN_FsmEvent(&l3->l2l3m, EV_LL_ESTABLISH_IND, NULL);
			if (ret) {
				int_error();
			}
			ret = 0;
			dev_kfree_skb(skb);
			break;
		case DL_RELEASE_CNF:
			ret = mISDN_FsmEvent(&l3->l2l3m, EV_LL_RELEASE_CNF, NULL);
			if (ret) {
				int_error();
			}
			ret = 0;
			dev_kfree_skb(skb);
			break;
		case DL_RELEASE_IND:
			ret = mISDN_FsmEvent(&l3->l2l3m, EV_LL_RELEASE_IND, NULL);
			if (ret) {
				int_error();
			}
			ret = 0;
			dev_kfree_skb(skb);
			break;
		default:
			int_error();
			break;
	}
	return(ret);
}

static x25_channel_t *
dte_create_channel(x25_l3_t *l3, int typ, u_char flag, __u16 ch, int len, u_char *data)
{
	x25_channel_t	*l3c;
	__u16		nch = ch;
	int		ret;
	
	if (typ == X25_CHANNEL_OUTGOING) {
		if (ch == 0) {
			/* first search for allready created channels in P1 state */
			if (l3->B3cfg.HOC) {
				for (nch = l3->B3cfg.HOC; (nch && (nch >= l3->B3cfg.LOC)); nch--) {
					l3c = X25_get_channel(l3, nch);
					if (l3c && (l3c->x25p.state == ST_P1)) {
						X25_realloc_ncpi_data(l3c, len, data);
						return(l3c);
					}
				}
			}
			if (l3->B3cfg.HTC) {
				for (nch = l3->B3cfg.HTC; (nch && (nch >= l3->B3cfg.LTC)); nch--) {
					l3c = X25_get_channel(l3, nch);
					if (l3c && (l3c->x25p.state == ST_P1)) {
						X25_realloc_ncpi_data(l3c, len, data);
						return(l3c);
					}
				}
			}
			/* now search for still unused channels */
			nch = 0;
			if (l3->B3cfg.HOC) {
				l3c = (x25_channel_t *)1; /* if loop is not executed */
				for (nch = l3->B3cfg.HOC; (nch && (nch >= l3->B3cfg.LOC)); nch--) {
					l3c = X25_get_channel(l3, nch);
					if (!l3c)
						break;
				}
				if (l3c)
					nch = 0;
			}
			if ((nch == 0) && l3->B3cfg.HTC) {
				l3c = (x25_channel_t *)1; /* if loop is not executed */
				for (nch = l3->B3cfg.HTC; (nch && (nch >= l3->B3cfg.LTC)); nch--) {
					l3c = X25_get_channel(l3, nch);
					if (!l3c)
						break;
				}
				if (l3c)
					nch = 0;
			}
		} else {
			if (ch >= l3->B3cfg.LIC) /* not a permanent channel */
				return(NULL);
			l3c = X25_get_channel(l3, nch);
			if (l3c) {
				if (test_bit(X25_STATE_PERMANENT, &l3c->state)) {
					if (l3c->ncci) /* allready in use */
						return(NULL);
					else {
						X25_realloc_ncpi_data(l3c, len, data);
						return(l3c);
					}
				}
			}
			nch = ch;
		}
		if (flag & 1)
			flag = X25_GFI_DBIT;
	} else {
		if (!ch) /* not Reference Number procedure implemented */
			return(NULL);
		if (l3->B3cfg.HTC) {
			if (ch > l3->B3cfg.HTC)
				return(NULL);
		} else if (l3->B3cfg.HIC) {
			if (ch > l3->B3cfg.HIC)
				return(NULL);
		}
		nch = ch;
		if (l3->B3cfg.LIC && ch < l3->B3cfg.LIC) /* permanent channel */
			nch = ch;
		else {
			nch = ch;
			ch = 0;
		}
	}
	if (!nch)
		return(NULL);
	ret = new_x25_channel(l3, &l3c, nch, len, data);
	if (ret)
		return(NULL);
	l3c->x25p.fsm = &dte_pfsm;
	l3c->x25d.fsm = &dte_dfsm;
	if (ch) {
		test_and_set_bit(X25_STATE_PERMANENT, &l3c->state);
		if (l3c->l3->x25r.state == ST_R1) {
			l3c->x25p.state = ST_P4;
			l3c->x25d.state = ST_D1;
		} else {
			l3c->x25p.state = ST_P0;
			l3c->x25d.state = ST_D0;
		}
	} else {
		test_and_clear_bit(X25_STATE_PERMANENT, &l3c->state);
		if (l3c->l3->x25r.state == ST_R1) {
			l3c->x25p.state = ST_P1;
			l3c->x25d.state = ST_D0;
		} else {
			l3c->x25p.state = ST_P0;
			l3c->x25d.state = ST_D0;
		}
	}
	if (flag & X25_GFI_DBIT)
		test_and_set_bit(X25_STATE_DBIT, &l3c->state);
	else
		test_and_clear_bit(X25_STATE_DBIT, &l3c->state);
	if (flag & X25_GFI_ABIT)
		test_and_set_bit(X25_STATE_ABIT, &l3c->state);
	else
		test_and_clear_bit(X25_STATE_DBIT, &l3c->state);
	return(l3c);
}

static int
new_l3(mISDNstack_t *st, mISDN_pid_t *pid) {
	x25_l3_t	*n_l3;
	int		err;

	err = new_x25_l3(&n_l3, st, pid, &x25dte_obj, debug);
	if (err)
		return(err);

	n_l3->x25r.fsm = &dte_rfsm;
	n_l3->x25r.state = ST_R0;
	return(0);
}

static int
dte_from_up(mISDNif_t *hif, struct sk_buff *skb)
{
	x25_l3_t	*l3;
	x25_channel_t   *l3c;
	mISDN_head_t	*hh;
	__u32		addr;
	__u16		info = 0;
	int		err = 0;

	if (!hif || !hif->fdata || !skb)
		return(-EINVAL);
	l3 = hif->fdata;
	if (!l3->inst.down.func) {
		return(-ENXIO);
	}
	hh = mISDN_HEAD_P(skb);
	if (l3->debug)
		printk(KERN_DEBUG "%s: prim(%x) dinfo(%x) len(%d)\n", __FUNCTION__, hh->prim, hh->dinfo, skb->len);
	if (skb->len < 4) {
		printk(KERN_WARNING "%s: skb too short (%d)\n", __FUNCTION__, skb->len);
		return(-EINVAL);
	} else {
		addr = CAPIMSG_U32(skb->data, 0);
		skb_pull(skb, 4);
	}
	if (l3->debug)
		printk(KERN_DEBUG "%s: addr(%x)\n", __FUNCTION__, addr);
	l3c = X25_get_channel4NCCI(l3, addr);
	switch(hh->prim) {
		case CAPI_DATA_B3_REQ:
			info = x25_data_b3_req(l3c, hh->dinfo, skb);
			if (info) {
				if (info == CAPI_SENDQUEUEFULL) {
					err = -EXFULL;
					break;
				}
				skb_trim(skb, 2);
				memcpy(skb->data, &info, 2);
				err = X25sendL4skb(l3c, l3, addr, CAPI_RESET_B3_CONF, hh->dinfo, skb);
			} else
				err = 0;
			break;
		case CAPI_DATA_B3_RESP:
			return(x25_data_b3_resp(l3c, hh->dinfo, skb));
		case CAPI_CONNECT_B3_REQ:
			if (!l3c) {
				x25_ncpi_t	*ncpi;
				if (skb->len <= 4) { // default NCPI
					u_char	a = 0;
					l3c = dte_create_channel(l3, X25_CHANNEL_OUTGOING, 0, 0, 1, &a);
				} else {
					ncpi = (x25_ncpi_t *)skb->data;
					l3c = dte_create_channel(l3, X25_CHANNEL_OUTGOING, ncpi->Flags,
						(ncpi->Group<<8) | ncpi->Channel,
						ncpi->len - 3, &ncpi->Contens[0]);  
				}
				if (l3c)
					l3c->ncci = addr | (l3c->lchan << 16);
			}
			if (l3c) {
				err = 0;
				if (l3->x25r.state == ST_R0)
					mISDN_FsmEvent(&l3->x25r, EV_L3_RESTART_REQ, "\000\000");
				else
					err = mISDN_FsmEvent(&l3c->x25p, EV_L3_OUTGOING_CALL, NULL);
				if (err)
					info = 0x2001; /* wrong state */
				else
					info = 0;
			} else
				info = 0x2004; /* no NCCI available */
			skb_trim(skb, 2);
			memcpy(skb->data, &info, 2);
			err = X25sendL4skb(l3c, l3, addr, CAPI_CONNECT_B3_CONF, hh->dinfo, skb);
			break;
		case CAPI_RESET_B3_REQ:
			if (l3c) {
				if (!(l3c->ncci & 0xffff))
					l3c->ncci = addr;
				if (skb->len <= 4) { // default NCPI
					l3c->cause[0] = 0;
					l3c->cause[1] = 0;
					err = mISDN_FsmEvent(&l3c->x25d, EV_L3_RESETING, NULL);
				} else {
					skb_pull(skb, 4);
					err = mISDN_FsmEvent(&l3c->x25d, EV_L3_RESETING, skb);
					skb_push(skb, 4);
				}
				if (err)
					info = 0x2001;
				else
					info = 0;
			} else
				info = 0x2002;
			skb_trim(skb, 2);
			memcpy(skb->data, &info, 2);
			err = X25sendL4skb(l3c, l3, addr, CAPI_RESET_B3_CONF, hh->dinfo, skb);
			break;
		case CAPI_DISCONNECT_B3_REQ:
			if (l3c) {
				if (!(l3c->ncci & 0xffff))
					l3c->ncci = addr;
				if (skb->len <= 4) { // default NCPI
					l3c->cause[0] = 0;
					l3c->cause[1] = 0;
					err = mISDN_FsmEvent(&l3c->x25p, EV_L3_CLEARING, NULL);
				} else {
					skb_pull(skb, 4);
					err = mISDN_FsmEvent(&l3c->x25p, EV_L3_CLEARING, skb);
					skb_push(skb, 4);
				}
				if (err)
					info = 0x2001;
				else
					info = 0;
			} else
				info = 0x2002;
			skb_trim(skb, 2);
			memcpy(skb->data, &info, 2);
			err = X25sendL4skb(l3c, l3, addr, CAPI_DISCONNECT_B3_CONF, hh->dinfo, skb);
			break;
		case CAPI_CONNECT_B3_RESP:
			if (l3c) {
				int event = EV_L3_CLEARING;

				l3c->ncci = addr;
				if (skb->len <= 2) {
					printk(KERN_WARNING "%s: CAPI_CONNECT_B3_RESP skb too short (%d)\n",
						__FUNCTION__, skb->len);
					skb_push(skb, 4);
					return(-EINVAL);
				}
				info = CAPIMSG_U16(skb->data, 0);
				skb_pull(skb, 2);
				if (info == 0)
					event = EV_L3_CALL_ACCEPT;
				if (skb->len <= 4) { // default NCPI
					l3c->cause[0] = 0;
					l3c->cause[1] = 0;
					err = mISDN_FsmEvent(&l3c->x25p, event, NULL);
				} else {
					skb_pull(skb, 4);
					err = mISDN_FsmEvent(&l3c->x25p, event, skb);
				}
			} else {
				skb_push(skb, 4);
				printk(KERN_WARNING "%s: CAPI_CONNECT_B3_RESP no channel found\n",
					__FUNCTION__);
				return(-ENODEV);
			}
			dev_kfree_skb(skb);
			err = 0;
			break;
		case CAPI_CONNECT_B3_ACTIVE_RESP:
		case CAPI_RESET_B3_RESP:
			if (!l3c) {
				skb_push(skb, 4);
				printk(KERN_WARNING "%s: prim %x dinfo %x no channel found\n",
					__FUNCTION__, hh->prim, hh->dinfo);
				return(-ENODEV);
			}
			dev_kfree_skb(skb);
			err = 0;
			break;
		case CAPI_DISCONNECT_B3_RESP:
			if (l3c) {
				l3c->ncci = 0;
				// TODO release NCCI
			} else {
				skb_push(skb, 4);
				printk(KERN_WARNING "%s: CAPI_DISCONNECT_B3_RESP no channel found\n",
					__FUNCTION__);
				return(-ENODEV);
			}
			dev_kfree_skb(skb);
			err = 0;
			break;
		default:
			printk(KERN_WARNING "%s: unknown prim %x dinfo %x\n",
				__FUNCTION__, hh->prim, hh->dinfo);
			err = -EINVAL;
	}
	return(err);
}


static char MName[] = "X25_DTE";

#ifdef MODULE
MODULE_AUTHOR("Karsten Keil");
MODULE_PARM(debug, "1i");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
#endif

static int
dte_manager(void *data, u_int prim, void *arg) {
	mISDNinstance_t	*inst = data;
	x25_l3_t	*l3_l;
	int		err = -EINVAL;

	if (debug & DEBUG_L3X25_MGR)
		printk(KERN_DEBUG "l3x25_manager data:%p prim:%x arg:%p\n", data, prim, arg);
	if (!data)
		return(err);
	list_for_each_entry(l3_l, &x25dte_obj.ilist, list) {
		if (&l3_l->inst == inst) {
			err = 0;
			break;
		}
	}
	if (prim == (MGR_NEWLAYER | REQUEST))
		return(new_l3(data, arg));
	if (err) {
		printk(KERN_WARNING "l3x25_manager prim(%x) no instance\n", prim);
		return(err);
	}
	switch(prim) {
	    case MGR_NEWENTITY | CONFIRM:
		l3_l->entity = (int)arg;
		break;
	    case MGR_ADDSTPARA | INDICATION:
		{
			mISDN_stPara_t *stp = arg;

			if (stp->down_headerlen)
				l3_l->down_headerlen = stp->down_headerlen;
			if (stp->up_headerlen)
				l3_l->up_headerlen = stp->up_headerlen;
			printk(KERN_DEBUG "MGR_ADDSTPARA: (%d/%d/%d)\n",
				stp->maxdatalen, stp->down_headerlen, stp->up_headerlen);
	    	}
	    	break;
	    case MGR_CLRSTPARA | INDICATION:
	    case MGR_CLONELAYER | REQUEST:
		break;
	    case MGR_CONNECT | REQUEST:
		return(mISDN_ConnectIF(inst, arg));
	    case MGR_SETIF | REQUEST:
	    case MGR_SETIF | INDICATION:
		return(mISDN_SetIF(inst, arg, prim, dte_from_up, dte_from_down, l3_l));
	    case MGR_DISCONNECT | REQUEST:
	    case MGR_DISCONNECT | INDICATION:
		return(mISDN_DisConnectIF(inst, arg));
	    case MGR_UNREGLAYER | REQUEST:
	    case MGR_RELEASE | INDICATION:
		if (debug & DEBUG_L3X25_MGR)
			printk(KERN_DEBUG "X25_release_l3 id %x\n", l3_l->inst.st->id);
		X25_release_l3(l3_l);
		break;
//	    case MGR_STATUS | REQUEST:
//		return(l3x25_status(l3x25_l, arg));
	    default:
		if (debug & DEBUG_L3X25_MGR)
			printk(KERN_WARNING "l3x25_manager prim %x not handled\n", prim);
		return(-EINVAL);
	}
	return(0);
}

static int
x25_dte_init(void)
{
	int err;

	printk(KERN_INFO "X25 DTE modul version %s\n", mISDN_getrev(mISDN_dte_revision));
#ifdef MODULE
	x25dte_obj.owner = THIS_MODULE;
#endif
	x25dte_obj.name = MName;
	x25dte_obj.BPROTO.protocol[3] = ISDN_PID_L3_B_X25DTE;
	x25dte_obj.own_ctrl = dte_manager;
	INIT_LIST_HEAD(&x25dte_obj.ilist);
	if ((err = mISDN_register(&x25dte_obj))) {
		printk(KERN_ERR "Can't register %s error(%d)\n", MName, err);
	} else {
		X25_l3_init();
		dte_rfsm.state_count = R_STATE_COUNT;
		dte_rfsm.event_count = R_EVENT_COUNT;
		dte_rfsm.strEvent = X25strREvent;
		dte_rfsm.strState = X25strRState;
		mISDN_FsmNew(&dte_rfsm, RFnList, R_FN_COUNT);
		dte_pfsm.state_count = P_STATE_COUNT;
		dte_pfsm.event_count = P_EVENT_COUNT;
		dte_pfsm.strEvent = X25strPEvent;
		dte_pfsm.strState = X25strPState;
		mISDN_FsmNew(&dte_pfsm, PFnList, P_FN_COUNT);
		dte_dfsm.state_count = D_STATE_COUNT;
		dte_dfsm.event_count = D_EVENT_COUNT;
		dte_dfsm.strEvent = X25strDEvent;
		dte_dfsm.strState = X25strDState;
		mISDN_FsmNew(&dte_dfsm, DFnList, D_FN_COUNT);
	}
	return(err);
}

static void
x25_dte_cleanup(void)
{
	x25_l3_t	*l3, *nl3;
	int		err;

	if ((err = mISDN_unregister(&x25dte_obj))) {
		printk(KERN_ERR "Can't unregister l3x25 error(%d)\n", err);
	}
	if(!list_empty(&x25dte_obj.ilist)) {
		printk(KERN_WARNING "l3x25 inst list not empty\n");
		list_for_each_entry_safe(l3, nl3, &x25dte_obj.ilist, list)
			X25_release_l3(l3);
	}
	X25_l3_cleanup();
	mISDN_FsmFree(&dte_rfsm);
	mISDN_FsmFree(&dte_pfsm);
	mISDN_FsmFree(&dte_dfsm);
}

module_init(x25_dte_init);
module_exit(x25_dte_cleanup);

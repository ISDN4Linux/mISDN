/* $Id: x25_l3.c,v 1.3 2004/06/17 12:31:12 keil Exp $
 *
 * Linux modular ISDN subsystem, mISDN
 * X.25/X.31 common Layer3 functions 
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

/* LinkLayer (L2) maintained by L3 statemachine */

static struct Fsm llfsm = {NULL, 0, 0, NULL, NULL};

enum {
	ST_LL_REL,
	ST_LL_ESTAB_WAIT,
	ST_LL_REL_WAIT,
	ST_LL_ESTAB,
};

#define LL_STATE_COUNT	(ST_LL_ESTAB+1)

static char *strLLState[] =
{
	"ST_LL_REL",
	"ST_LL_ESTAB_WAIT",
	"ST_LL_REL_WAIT",
	"ST_LL_ESTAB",
};

static char *strLLEvent[] =
{
	"EV_L3_ESTABLISH_REQ",
	"EV_LL_ESTABLISH_IND",
	"EV_LL_ESTABLISH_CNF",
	"EV_L3_RELEASE_REQ",
	"EV_LL_RELEASE_CNF",
	"EV_LL_RELEASE_IND",
};

/* X.25 Restart state machine */

char *X25strRState[] =
{
	"ST_R0",
	"ST_R1",
	"ST_R2",
	"ST_R3",
};

char *X25strREvent[] =
{
	"EV_LL_READY",
 	"EV_L3_RESTART_REQ",
	"EV_L2_RESTART",
	"EV_L2_RESTART_CNF",
	"EV_L3_RESTART_TOUT",
};

/* X.25 connection state machine */

char *X25strPState[] =
{
	"ST_P0",
	"ST_P1",
	"ST_P2",
	"ST_P3",
	"ST_P4",
	"ST_P5",
	"ST_P6",
	"ST_P7",
};

char *X25strPEvent[] =
{
	"EV_L3_READY",
	"EV_L3_OUTGOING_CALL",
	"EV_L2_INCOMING_CALL",
	"EV_L2_CALL_CNF",
	"EV_L3_CALL_ACCEPT",
	"EV_L3_CLEARING",
	"EV_L2_CLEAR",
	"EV_L2_CLEAR_CNF",
	"EV_L2_INVALPKT",
	"EV_L3_CALL_TOUT",
	"EV_L3_CLEAR_TOUT",
};

/* X.25 Flowcontrol state machine */

char *X25strDState[] =
{
	"ST_D0",
	"ST_D1",
	"ST_D2",
	"ST_D3",
};

char *X25strDEvent[] =
{
	"EV_L3_CONNECT",
 	"EV_L2_RESETING",
	"EV_L2_RESET",
	"EV_L2_RESET_CNF",
	"EV_L3_RESET_TOUT",
};

static void
l3m_debug(struct FsmInst *fi, char *fmt, ...)
{
	x25_l3_t	*l3 = fi->userdata;
	logdata_t	log;

	va_start(log.args, fmt);
	log.fmt = fmt;
	log.head = l3->inst.name;
	l3->inst.obj->ctrl(&l3->inst, MGR_DEBUGDATA | REQUEST, &log);
	va_end(log.args);
}

/* LinkLayer (L2) maintained by L3 statemachine */

static void
ll_activate(struct FsmInst *fi, int event, void *arg)
{
	x25_l3_t *l3 = fi->userdata;

	mISDN_FsmChangeState(fi, ST_LL_ESTAB_WAIT);
	X25_l3down(l3, DL_ESTABLISH | REQUEST, 0, NULL);
}

static void
ll_connect(struct FsmInst *fi, int event, void *arg)
{
	x25_l3_t *l3 = fi->userdata;
	struct sk_buff *skb;
	int dequeued = 0;

	mISDN_FsmChangeState(fi, ST_LL_ESTAB);
	mISDN_FsmEvent(&l3->x25r, EV_LL_READY, NULL);
	while ((skb = skb_dequeue(&l3->downq))) {
		mISDN_head_t	*hh = mISDN_HEAD_P(skb);
		if (X25_l3down(l3, hh->prim, hh->dinfo, skb))
			dev_kfree_skb(skb);
		dequeued++;
	}
}

static void
ll_connected(struct FsmInst *fi, int event, void *arg)
{
	x25_l3_t *l3 = fi->userdata;
	struct sk_buff *skb;
	int dequeued = 0;

	mISDN_FsmChangeState(fi, ST_LL_ESTAB);
	mISDN_FsmEvent(&l3->x25r, EV_LL_READY, NULL);
	while ((skb = skb_dequeue(&l3->downq))) {
		mISDN_head_t	*hh = mISDN_HEAD_P(skb);
		if (X25_l3down(l3, hh->prim, hh->dinfo, skb))
			dev_kfree_skb(skb);
		dequeued++;
	}
}

static void
ll_release_req(struct FsmInst *fi, int event, void *arg)
{
	x25_l3_t *l3 = fi->userdata;

	mISDN_FsmChangeState(fi, ST_LL_REL_WAIT);
	X25_l3down(l3, DL_RELEASE | REQUEST, 0, NULL);
}

static void
ll_release_ind(struct FsmInst *fi, int event, void *arg)
{
	x25_l3_t *l3 = fi->userdata;

	mISDN_FsmChangeState(fi, ST_LL_REL);
	discard_queue(&l3->downq);
}

static void
ll_release_cnf(struct FsmInst *fi, int event, void *arg)
{
	x25_l3_t *l3 = fi->userdata;

	mISDN_FsmChangeState(fi, ST_LL_REL);
	discard_queue(&l3->downq);
}


/* *INDENT-OFF* */
static struct FsmNode LLFnList[] =
{
	{ST_LL_REL,		EV_L3_ESTABLISH_REQ,	ll_activate},
	{ST_LL_REL,		EV_LL_ESTABLISH_IND,	ll_connect},
	{ST_LL_REL,		EV_LL_ESTABLISH_CNF,	ll_connect},
	{ST_LL_ESTAB_WAIT,	EV_LL_ESTABLISH_CNF,	ll_connected},
	{ST_LL_ESTAB_WAIT,	EV_L3_RELEASE_REQ,	ll_release_req},
	{ST_LL_ESTAB_WAIT,	EV_LL_RELEASE_IND,	ll_release_ind},
	{ST_LL_ESTAB,		EV_LL_RELEASE_IND,	ll_release_ind},
	{ST_LL_ESTAB,		EV_L3_RELEASE_REQ,	ll_release_req},
	{ST_LL_REL_WAIT,	EV_LL_RELEASE_CNF,	ll_release_cnf},
	{ST_LL_REL_WAIT,	EV_L3_ESTABLISH_REQ,	ll_activate},
};
/* *INDENT-ON* */

#define LL_FN_COUNT (sizeof(LLFnList)/sizeof(struct FsmNode))

static void
l3c_debug(struct FsmInst *fi, char *fmt, ...)
{
	x25_channel_t	*l3c = fi->userdata;
	logdata_t	log;

	va_start(log.args, fmt);
	log.fmt = fmt;
	log.head = l3c->l3->inst.name;
	l3c->l3->inst.obj->ctrl(&l3c->l3->inst, MGR_DEBUGDATA | REQUEST, &log);
	va_end(log.args);
}

static void
discard_confq(x25_channel_t *l3c)
{
	int		i;
	x25_ConfQueue_t	*cq = l3c->confq;

	for (i = 0; i < l3c->lwin; i++) {
		if (cq->PktId) {
			dev_kfree_skb(cq->skb);
			cq->PktId = 0;
		}
		cq++;
	}
}

int
X25_reset_channel(x25_channel_t *l3c, struct sk_buff *skb)
{
	discard_confq(l3c);
	l3c->pr = 0;
	l3c->ps = 0;
	l3c->rps = 0;
	// TODO requeue outstanding pakets
	// TODO timers ???
	if (skb) {
		if (skb->len == 2)
			memcpy(l3c->cause, skb->data, 2);
		else
			printk(KERN_DEBUG "%s: skb len not 2 (%d)\n", __FUNCTION__, skb->len);
	}
	if (ST_P4 == l3c->x25p.state) {
		X25sendL4frame(l3c, l3c->l3, CAPI_RESET_B3_IND, 0, 2, l3c->cause);
		// invoke send ???
	}
	return(0);
}

int
X25_restart(x25_l3_t *l3)
{
	x25_channel_t	*l3c;

	list_for_each_entry(l3c, &l3->channellist, list) {
		memcpy(l3c->cause, l3->cause, 2);
		X25_reset_channel(l3c, NULL);
		mISDN_FsmEvent(&l3c->x25p, EV_L3_READY, NULL);
	}
	return(0);
}

int
X25_next_id(x25_l3_t *l3)
{
	u_long	flags;
	int	id;

	spin_lock_irqsave(&l3->lock, flags);
	id = l3->next_id++;
	if (id == 0x0fff)
		l3->next_id = 1;
	spin_unlock_irqrestore(&l3->lock, flags);
	id |= (l3->entity << 16);
	return(id);
}

int
X25_get_header(x25_l3_t *l3, struct sk_buff *skb, u_char *gfi, __u16 *channel, u_char *ptype)
{
	u_char	*p = skb->data;
	int	l = 3;

	if (skb->len < 2)
		return(38); // packet too short
	if ((*p & 0x30) == 0x30) {
		if (*p != 0x30)
			return(40); // invalid GFI
		p++;
		l++;
		if (skb->len < 3)
			return(38); // packet too short
		if ((*p & 0x30)!= 0x30)
			return(40); // invalid GFI
		if (!test_bit(X25_STATE_MOD32768, &l3->state))
			return(40); // invalid GFI
	}
	*gfi = (*p & 0xf0);
	if (!(*gfi & 0x30))
		return(40); // invalid GFI
	if (((*gfi & 0x30) == 0x20) && !test_bit(X25_STATE_MOD128, &l3->state)) 
		return(40); // invalid GFI
	*channel = (*p & 0xf) << 8;
	p++;
	*channel |= *p++;
	if (skb->len < l) {
		*ptype = X25_PTYPE_NOTYPE;
		return(38);
	}
	*ptype = *p;
	skb_pull(skb, l);
	return(0);
}

int
X25_cansend(x25_channel_t *chan)
{
	u_int	m = 7;

	if (test_bit(X25_STATE_MOD128, &chan->state))
		m = 0x7f;
	else if (test_bit(X25_STATE_MOD32768, &chan->state))
		m = 0x7fff;
	
	return((((chan->ps - chan->pr) & m) < chan->lwin) &&
		!test_bit(X25_STATE_DTE_RNR, &chan->state) && (chan->x25d.state == ST_D1));
}

void
X25_confirmed(x25_channel_t *chan)
{
	int		i, ret;
	x25_ConfQueue_t	*cq = chan->confq;
	struct sk_buff	*skb;

	for (i = 0; i < chan->lwin; i++) {
		if ((cq->PktId & 0x7fff) == chan->pr)
			break;
		cq++;
	}
	if (i == chan->lwin) {
		int_error();
		return;
	}
	skb = cq->skb;
	cq->skb = NULL;
	if (!skb) {
		return;
	}
	skb_push(skb, 8);
	skb_trim(skb, 0);
	capimsg_setu32(skb_put(skb, 4), 0, chan->ncci);
	capimsg_setu16(skb_put(skb, 2), 0, cq->DataHandle);
	capimsg_setu16(skb_put(skb, 2), 0, 0);
	i = cq->MsgId;
	/* free entry */
	cq->PktId = 0;
	ret = if_newhead(&chan->l3->inst.up, CAPI_DATA_B3_CONF, i, skb);
	if (ret) {
		printk(KERN_WARNING "%s: up error %d\n", __FUNCTION__, ret);
		dev_kfree_skb(skb);
	}
	return;
}

void
X25_confirm_pr(x25_channel_t *chan, u_int pr)
{
	u_int	mod = 8;

	if (test_bit(X25_STATE_MOD128, &chan->state))
		mod = 128;
	else if (test_bit(X25_STATE_MOD32768, &chan->state))
		mod = 32768;
	while (chan->pr != pr) {
		X25_confirmed(chan);
		chan->pr++;
		if (chan->pr >= mod)
			chan->pr = 0;
	}
}

int
X25_receive_data(x25_channel_t *chan, int ps, int flag, struct sk_buff *skb)
{
	int		l, i, m = 8;
	u_char		*p = skb->data;
	struct sk_buff	*nskb;
	
	if (test_bit(X25_STATE_DTE_RNR, &chan->state))
		return(X25_ERRCODE_DISCARD);
	for (i = 0; i < CAPI_MAXDATAWINDOW; i++) {
		if (chan->recv_handles[i] == 0)
			break;
	}

	if (i == CAPI_MAXDATAWINDOW) {
		test_and_set_bit(X25_STATE_DTE_RNR, &chan->state);
		printk(KERN_DEBUG "%s: frame %d dropped\n", __FUNCTION__, skb->len);
		return(X25_ERRCODE_DISCARD);
	}

	if (skb_headroom(skb) < CAPI_B3_DATA_IND_HEADER_SIZE) {
		printk(KERN_DEBUG "%s: only %d bytes headroom, need %d",
			__FUNCTION__, skb_headroom(skb), CAPI_B3_DATA_IND_HEADER_SIZE);
		nskb = skb_realloc_headroom(skb, CAPI_B3_DATA_IND_HEADER_SIZE);
		dev_kfree_skb(skb);
		if (!nskb) {
			int_error();
			return(0);
		}
      	} else { 
		nskb = skb;
	}
	chan->recv_handles[i] = 0x100 | flag;
	l = skb->len;
	skb_push(nskb, CAPI_B3_DATA_IND_HEADER_SIZE - CAPIMSG_BASELEN);
	capimsg_setu32(nskb->data, 0, chan->ncci);
	if (sizeof(nskb) == 4) {
		capimsg_setu32(nskb->data, 4, (u_long)p);
		capimsg_setu32(nskb->data, 14, 0);
		capimsg_setu32(nskb->data, 18, 0);
		
	} else {
		capimsg_setu32(nskb->data, 4, 0);
		capimsg_setu32(nskb->data, 14, ((u_long)p) & 0xffffffff);
		capimsg_setu32(nskb->data, 18, (((__u64)((u_long)p)) >> 32) & 0xffffffff);
	}
	capimsg_setu16(nskb->data, 8, l);
	capimsg_setu16(nskb->data, 10, i);
	capimsg_setu16(nskb->data, 12, flag);

	if (if_newhead(&chan->l3->inst.up, CAPI_DATA_B3_IND, 0, nskb)) {
		chan->recv_handles[i] = 0;
		return(X25_ERRCODE_DISCARD);
	}
	if (!(flag & CAPI_FLAG_DELIVERCONF)) {
		if (test_bit(X25_STATE_MOD32768, &chan->state))
			m = 32768;
		else if (test_bit(X25_STATE_MOD128, &chan->state))
			m = 128;
		chan->rps++;
		if (chan->rps >= m)
			chan->rps = 0;
		if (X25_cansend(chan) && skb_queue_len(&chan->dataq))
			X25_invoke_sending(chan);
		else {
			if (test_bit(X25_STATE_DTE_RNR, &chan->state))
				X25sendL3frame(chan, chan->l3, X25_PTYPE_RNR, 0, NULL);
			else
				X25sendL3frame(chan, chan->l3, X25_PTYPE_RR, 0, NULL);
		}
	}
	return(0);
}

int
X25_get_and_test_pr(x25_channel_t *chan, u_char ptype, struct sk_buff *skb)
{
	u_char	*p = skb->data;
	u_int	pr_m, pr, m = 7;

	if (test_bit(X25_STATE_MOD128, &chan->state)) {
		if (skb->len < 1)
			return(-38);
		pr_m = *p;
		skb_pull(skb, 1);
		m = 0x7f;
	} else if (test_bit(X25_STATE_MOD32768, &chan->state)) {
		if (skb->len < 2)
			return(-38);
		pr_m = *p++;
		pr_m |= (*p << 8);
		skb_pull(skb, 2);
		m = 0x7fff;
	} else {
		pr_m = ptype >> 4; 
	}
	pr = pr_m >> 1;
	if (chan->debug)
		printk(KERN_DEBUG "%s: pr(%d) chan: pr(%d) ps(%d)\n",
			__FUNCTION__, pr, chan->pr, chan->ps);
	if (((pr - chan->pr) & m) <= ((chan->ps - chan->pr) & m)) {
		if (chan->pr != pr)
			X25_confirm_pr(chan, pr);
		return(pr_m);
	} else
		return(-1);
}

int
X25_get_and_test_ps(x25_channel_t *chan, u_char ptype, struct sk_buff *skb)
{
	u_char	*p = skb->data;
	int	m = 128, ps = ptype >> 1;

	if (test_bit(X25_STATE_MOD32768, &chan->state)) {
		if (skb->len < 1)
			return(-38);
		ps |= (*p << 7);
		skb_pull(skb, 1);
		m = 32768;
	} else if (!test_bit(X25_STATE_MOD128, &chan->state)) {
		ps &= 7;
		m = 8;
	}
	if (chan->debug)
		printk(KERN_DEBUG "%s: ps(%d) chan: rps(%d)\n",
			__FUNCTION__, ps, chan->rps);
	if (ps != chan->rps)
		return(-2);
	return(ps);
}

void
X25_release_channel(x25_channel_t *l3c)
{
	list_del(&l3c->list);
	if (l3c->ncpi_data)
		kfree(l3c->ncpi_data);
	l3c->ncpi_data = NULL;
	l3c->ncpi_len = 0;
	discard_queue(&l3c->dataq);
	mISDN_FsmDelTimer(&l3c->TP, 1);
	mISDN_FsmDelTimer(&l3c->TD, 2);
	discard_queue(&l3c->dataq);
	discard_confq(l3c);
	if (l3c->confq) {
		kfree(l3c->confq);
		l3c->confq = NULL;
	}
	kfree(l3c);
}

void
X25_release_l3(x25_l3_t *l3) {
	mISDNinstance_t	*inst = &l3->inst;
	x25_channel_t	*ch, *nch;

	if (inst->up.peer) {
		inst->up.peer->obj->ctrl(inst->up.peer,
			MGR_DISCONNECT | REQUEST, &inst->up);
	}
	if (inst->down.peer) {
		inst->down.peer->obj->ctrl(inst->down.peer,
			MGR_DISCONNECT | REQUEST, &inst->down);
	}
	if (inst->obj) {
		list_del_init(&l3->list);
	}
	discard_queue(&l3->downq);
	list_for_each_entry_safe(ch, nch, &l3->channellist, list)
		X25_release_channel(ch);
	mISDN_FsmDelTimer(&l3->TR, 3);
	if (inst->obj) {
		if (l3->entity != MISDN_ENTITY_NONE)
			inst->obj->ctrl(inst, MGR_DELENTITY | REQUEST, (void *)l3->entity);
		inst->obj->ctrl(inst, MGR_UNREGLAYER | REQUEST, NULL);
	}
	kfree(l3);
}

int
X25_realloc_ncpi_data(x25_channel_t *l3c, int len, u_char *data)
{
	if (len) {
		if (len > l3c->ncpi_len) {
			if (l3c->ncpi_data)
				kfree(l3c->ncpi_data);
			l3c->ncpi_data = kmalloc(len, GFP_ATOMIC);
			if (!l3c->ncpi_data) {
				l3c->ncpi_len = 0;
				return(-ENOMEM);
			}
		}
		memcpy(l3c->ncpi_data, data, len);
	} else if (l3c->ncpi_data) {
		kfree(l3c->ncpi_data);
		l3c->ncpi_data = NULL;
	}
	l3c->ncpi_len = len;
	return(0);
}

int
new_x25_channel(x25_l3_t *l3, x25_channel_t **ch_p, __u16 ch, int dlen, u_char *data)
{
	x25_channel_t	*l3c;

	l3c = kmalloc(sizeof(x25_channel_t), GFP_ATOMIC);
	if (!l3c) {
		printk(KERN_ERR "kmalloc x25_channel_t failed\n");
		return(-ENOMEM);
	}
	memset(l3c, 0, sizeof(x25_channel_t));
	if (X25_realloc_ncpi_data(l3c, dlen, data)) {
		printk(KERN_ERR "kmalloc ncpi_data (%d) failed\n", dlen);
		kfree(l3c);
		return(-ENOMEM);
	}
	l3c->lwin = l3->B3cfg.winsize;
	l3c->rwin = l3->B3cfg.winsize;
	l3c->datasize = l3->maxdatalen;
	l3c->lchan = ch;
	l3c->ncci = ch << 16;
	l3c->confq = kmalloc(l3c->lwin * sizeof(x25_ConfQueue_t), GFP_ATOMIC);
	if (!l3c->confq) {
		printk(KERN_ERR "kmalloc confq %d entries failed\n", l3c->lwin);
		if (l3c->ncpi_data)
			kfree(l3c->ncpi_data);
		kfree(l3c);
		return(-ENOMEM);
	}
	memset(l3c->confq, 0, l3c->lwin * sizeof(x25_ConfQueue_t));
	l3c->l3 = l3;
	l3c->debug = l3->debug;
	l3c->state = l3->state;

	l3c->x25p.debug = l3->debug;
	l3c->x25p.userdata = l3c;
	l3c->x25p.userint = 0;
	l3c->x25p.printdebug = l3c_debug;
	mISDN_FsmInitTimer(&l3c->x25p, &l3c->TP);

	l3c->x25d.debug = l3->debug;
	l3c->x25d.userdata = l3c;
	l3c->x25d.userint = 0;
	l3c->x25d.printdebug = l3c_debug;
	mISDN_FsmInitTimer(&l3c->x25d, &l3c->TD);
	skb_queue_head_init(&l3c->dataq);

	list_add_tail(&l3c->list, &l3->channellist);
	*ch_p = l3c;
	return(0);
}

int
new_x25_l3(x25_l3_t **l3_p, mISDNstack_t *st, mISDN_pid_t *pid, mISDNobject_t *obj, int debug) {
	x25_l3_t	*n_l3;
	int		err;

	if (!st || !pid)
		return(-EINVAL);
	if (!(n_l3 = kmalloc(sizeof(x25_l3_t), GFP_ATOMIC))) {
		printk(KERN_ERR "kmalloc x25_l3_t failed\n");
		return(-ENOMEM);
	}
	memset(n_l3, 0, sizeof(x25_l3_t));
	INIT_LIST_HEAD(&n_l3->channellist);
	n_l3->entity = MISDN_ENTITY_NONE;
	n_l3->next_id = 1;
	spin_lock_init(&n_l3->lock);
	memcpy(&n_l3->inst.pid, pid, sizeof(mISDN_pid_t));
	mISDN_init_instance(&n_l3->inst, obj, n_l3);
	if (!mISDN_SetHandledPID(obj, &n_l3->inst.pid)) {
		int_error();
		kfree(n_l3);
		return(-ENOPROTOOPT);
	}
	n_l3->debug = debug;
	n_l3->B3cfg = (x25_B3_cfg_t)DEFAULT_X25_B3_CFG;
	if (pid->param[3]) {
		u_char	*p = pid->param[3];
		memcpy(&n_l3->B3cfg, &p[1], p[0]);
	}
	if (n_l3->B3cfg.modulo == 128)
		test_and_set_bit(X25_STATE_MOD128, &n_l3->state);
	if (n_l3->inst.pid.global == 1)
		test_and_set_bit(X25_STATE_ORGINATE, &n_l3->state);

	n_l3->l2l3m.fsm = &llfsm;
	n_l3->l2l3m.state = ST_LL_REL;
	n_l3->l2l3m.debug = debug;
	n_l3->l2l3m.userdata = n_l3;
	n_l3->l2l3m.userint = 0;
	n_l3->l2l3m.printdebug = l3m_debug;

	n_l3->x25r.debug = debug;
	n_l3->x25r.userdata = n_l3;
	n_l3->x25r.userint = 0;
	n_l3->x25r.printdebug = l3m_debug;
	mISDN_FsmInitTimer(&n_l3->x25r, &n_l3->TR);
	skb_queue_head_init(&n_l3->downq);

	list_add_tail(&n_l3->list, &obj->ilist);
	err = obj->ctrl(&n_l3->inst, MGR_NEWENTITY | REQUEST, NULL);
	if (err) {
		printk(KERN_WARNING "mISDN %s: MGR_NEWENTITY REQUEST failed err(%x)\n",
			__FUNCTION__, err);
	}
	err = obj->ctrl(st, MGR_REGLAYER | INDICATION, &n_l3->inst);
	if (err) {
		list_del(&n_l3->list);
		kfree(n_l3);
		n_l3 = NULL;
	} else {
		if (st->para.maxdatalen)
			n_l3->maxdatalen = st->para.maxdatalen;
		if (st->para.up_headerlen)
			n_l3->up_headerlen = st->para.up_headerlen;
		if (st->para.down_headerlen)
			n_l3->down_headerlen = st->para.down_headerlen;
		if (debug)
			printk(KERN_DEBUG "%s:mlen(%d) hup(%d) hdown(%d)\n", __FUNCTION__,
				n_l3->maxdatalen, n_l3->up_headerlen, n_l3->down_headerlen);
	}
	*l3_p = n_l3;
	return(err);
}

int
X25_add_header(x25_channel_t *l3c, x25_l3_t *l3, u_char pt, u_char *head, u_char flag)
{
	u_char	*p = head;

	if (test_bit(X25_STATE_MOD32768, &l3->state)) {
		*p++ = 0x30;
		*p = 0x30;
	} else if (test_bit(X25_STATE_MOD128, &l3->state))
		*p = 0x20;
	else
		*p = 0x10;
	switch (pt) {
		case X25_PTYPE_RESTART:
		case X25_PTYPE_RESTART_CNF:
		case X25_PTYPE_REGISTER:
		case X25_PTYPE_REGISTER_CNF:
		case X25_PTYPE_DIAGNOSTIC:
			p++;
			*p++ = 0;
			*p++ = pt;
			break;
		case X25_PTYPE_RESET:
		case X25_PTYPE_RESET_CNF:
		case X25_PTYPE_INTERRUPT:
		case X25_PTYPE_INTERRUPT_CNF:
			*p++ |= (((l3c->lchan) >> 8) & 0xf);
			*p++ = l3c->lchan & 0xff;
			*p++ = pt;
			break;
		case X25_PTYPE_CALL:
		case X25_PTYPE_CLEAR:
			if (test_bit(X25_STATE_DBIT, &l3c->state))
				*p |= X25_GFI_DBIT;
		case X25_PTYPE_CALL_CNF:
		case X25_PTYPE_CLEAR_CNF:
			if (test_bit(X25_STATE_ABIT, &l3c->state))
				*p |= X25_GFI_ABIT;
			*p++ |= (((l3c->lchan) >> 8) & 0xf);
			*p++ = l3c->lchan & 0xff;
			*p++ = pt;
			break;
		case X25_PTYPE_RR:
		case X25_PTYPE_RNR:
		case X25_PTYPE_REJ:
			if (*p == 0x10)
				pt |= (l3c->rps << 5);
			*p++ |= (((l3c->lchan) >> 8) & 0xf);
			*p++ = l3c->lchan & 0xff;
			*p++ = pt;
			if (test_bit(X25_STATE_MOD128, &l3->state))
				*p++ = l3c->rps << 1;
			else if (test_bit(X25_STATE_MOD32768, &l3->state)) {
				*p++ = (0x7f & l3c->rps) << 1;
				*p++ = l3c->rps >> 7;
			}
			break;
		case X25_PTYPE_DATA:
			if (*p == 0x10) {
				*p |= (flag & (X25_GFI_DBIT | X25_GFI_QBIT));
				*p++ |= (((l3c->lchan) >> 8) & 0xf);
				*p++ = l3c->lchan & 0xff;
				if (flag & X25_MBIT)
					pt |= X25_MBIT_MOD8;
				pt |= (l3c->rps << 5);
				pt |= (l3c->ps << 1);
				*p++ = pt;
				l3c->ps++;
				if (l3c->ps > 7)
					l3c->ps = 0;
			} else if (*p == 0x20) {
				*p |= (flag & (X25_GFI_DBIT | X25_GFI_QBIT));
				*p++ |= (((l3c->lchan) >> 8) & 0xf);
				*p++ = l3c->lchan & 0xff;
				*p++ = (l3c->ps << 1);
				*p = (flag & X25_MBIT) ? 1 : 0; 
				*p++ |= (l3c->rps << 1);
				l3c->ps++;
				if (l3c->ps > 0x7f)
					l3c->ps = 0;
			} else {
				*p |= (flag & (X25_GFI_DBIT | X25_GFI_QBIT));
				*p++ |= (((l3c->lchan) >> 8) & 0xf);
				*p++ = l3c->lchan & 0xff;
				*p++ = ((l3c->ps & 0x7f) << 1);
				*p++ = (l3c->ps >> 7);
				*p = (flag & X25_MBIT) ? 1 : 0;
				*p++ = ((l3c->rps & 0x7f) << 1);
				*p++ = (l3c->rps >> 7);
				l3c->ps++;
				if (l3c->ps > 0x7fff)
					l3c->ps = 0;
			}
			break;
		default:
			return(-EINVAL);
	}
	return(p - head);
}

int
X25sendL3frame(x25_channel_t *l3c, x25_l3_t *l3, u_char pt, int len, void *arg)
{
	struct sk_buff	*skb;
	int		ret;

	skb = alloc_stack_skb(len + X25_MINSIZE, l3->down_headerlen);
	if (!skb)
		return(-ENOMEM);
	ret = X25_add_header(l3c, l3, pt, skb->tail, 0);
	if (ret<0) {
		int_error();
		dev_kfree_skb(skb);
		return(ret);
	}
	skb_put(skb, ret);
	if (arg && len)
		memcpy(skb_put(skb, len), arg, len);

	mISDN_sethead(DL_DATA_REQ, X25_next_id(l3), skb);

	if (l3->l2l3m.state == ST_LL_ESTAB) {
		mISDNif_t	*down = &l3->inst.down;

		ret = down->func(down, skb);
		if (ret) {
			dev_kfree_skb(skb);
		}
	} else {
		skb_queue_tail(&l3->downq, skb);
		ret = 0;
	}
	return(ret);
}

int
X25_l3down(x25_l3_t *l3, u_int prim, u_int dinfo, struct sk_buff *skb)
{
	mISDNif_t	*down = &l3->inst.down;
	int		ret;

	if (!skb) {
		if (!(skb = alloc_stack_skb(0, l3->down_headerlen)))
			return(-ENOMEM);
	}
	mISDN_sethead(prim, dinfo, skb);
	ret = down->func(down, skb);
	if (ret) {
		dev_kfree_skb(skb);
	}
	return(0);
}

void
X25_send_diagnostic(x25_l3_t *l3, struct sk_buff *skb, int err, int channel)
{
	u_char	diagp[8], *p;
	u_int	i,l = 3;

	p = diagp;
	*p++ = err & 0xff;
	if (test_bit(X25_STATE_MOD32768, &l3->state)) {
		*p++ = 0x30;
		l++;
	}
	if (skb) {
		if (skb->len < l)
			l = skb->len;
		for (i = 0; i < l; i++)
			*p++ = skb->data[i];
	} else {
		if ((err & 0xf0) == 0x30) { /* Timer Expired */
			if (test_bit(X25_STATE_MOD32768, &l3->state))
				*p = 0x30;
			else if (test_bit(X25_STATE_MOD128, &l3->state))
				*p = 0x20;
			else
				*p = 0x10;
			if (err == 0x34)
				channel = 0; 
			*p |= ((channel >> 8) & 0x0f);
			p++;
			*p++ = channel & 0xff;
		}
	}
	X25sendL3frame(NULL, l3, X25_PTYPE_DIAGNOSTIC, p - diagp, diagp);
}

x25_channel_t *
X25_get_channel(x25_l3_t *l3, __u16 ch)
{
	x25_channel_t	*l3c;

	list_for_each_entry(l3c, &l3->channellist, list) {
		if (l3c->lchan == ch)
			return(l3c);
	}
	return(NULL);
}

x25_channel_t *
X25_get_channel4NCCI(x25_l3_t *l3, __u32 addr)
{
	x25_channel_t	*l3c;

	list_for_each_entry(l3c, &l3->channellist, list) {
		if ((l3c->ncci & 0xffff0000) == (addr & 0xffff0000))
			return(l3c);
	}
	return(NULL);
}

int
X25sendL4skb(x25_channel_t *l3c, x25_l3_t *l3, __u32 addr, int prim, int dinfo, struct sk_buff *skb)
{
	skb_push(skb, 4);
	if (l3c)
		capimsg_setu32(skb->data, 0, l3c->ncci);
	else
		capimsg_setu32(skb->data, 0, addr);
	return(if_newhead(&l3->inst.up, prim, dinfo, skb));		 
}

int
X25sendL4frame(x25_channel_t *l3c, x25_l3_t *l3, int prim, int flags, int len, void *arg)
{
	struct sk_buff	*skb;
	u_char		*p;
	int		ret;

	skb = alloc_stack_skb(len + X25_MINSIZE + 2, l3->up_headerlen);
	if (!skb)
		return(-ENOMEM);

	capimsg_setu32(skb_put(skb, 4), 0, l3c->ncci);
	switch(prim) {
		case CAPI_DISCONNECT_B3_IND:
			capimsg_setu16(skb_put(skb, 2), 0, flags & 0xffff);
		case CAPI_CONNECT_B3_IND:
		case CAPI_CONNECT_B3_ACTIVE_IND:
		case CAPI_RESET_B3_IND:
			if (len) {
				p = skb_put(skb, len + 4);
				*p++ = len +3;
				if (flags & 0x10000)
					*p++ = 1;
				else
					*p++ = 0;
				*p++ = l3c->lchan >> 8;
				*p++ = l3c->lchan & 0xff;
				memcpy(p, arg, len);
			} else {
				p = skb_put(skb, 1);
				*p = 0;
			}
			break;
		default:
			dev_kfree_skb(skb);
			return(-EINVAL);
	}
	ret = if_newhead(&l3->inst.up, prim, 0, skb);
	if (ret) {
		printk(KERN_WARNING "%s: up error %d\n", __FUNCTION__, ret);
		dev_kfree_skb(skb);
	}
	return(ret);
}

static int
confq_len(x25_channel_t *l3c)
{
	int		i,n = 0;
	x25_ConfQueue_t	*cq = l3c->confq;

	for (i = 0; i < l3c->lwin; i++)
		if (cq[i].PktId)
			n++;
	return(n);
}

static inline x25_ConfQueue_t *
get_free_confentry(x25_channel_t *l3c)
{
	int		i;
	x25_ConfQueue_t	*cq = l3c->confq;

	for (i = 0; i < l3c->lwin; i++) {
		if (!cq->PktId)
			break;
		cq++;
	}
	if (i == l3c->lwin)
		return(NULL);
	return(cq);
}

int
X25_invoke_sending(x25_channel_t *l3c)
{
	int		l,n = 0;
	x25_ConfQueue_t	*cq;
	struct sk_buff	*skb, *nskb;
	u_char		flg;

	if (!X25_cansend(l3c))
		return(0);
	cq = get_free_confentry(l3c);
	skb = skb_dequeue(&l3c->dataq);
	while(cq && skb) {
		mISDN_head_t    *hh = mISDN_HEAD_P(skb);

		cq->MsgId = hh->dinfo;
		hh++;
		cq->DataHandle = hh->prim;
		nskb = skb_clone(skb, GFP_ATOMIC);
		if (!nskb) {
			skb_queue_head(&l3c->dataq, skb);
			break;
		}
		cq->skb = skb;
		cq->PktId = 0x10000 | l3c->ps;
		flg = (hh->dinfo & CAPI_FLAG_DELIVERCONF) ? X25_GFI_DBIT : 0;
		if (hh->dinfo & CAPI_FLAG_QUALIFIER)
			flg |= X25_GFI_QBIT;
		if (hh->dinfo & CAPI_FLAG_MOREDATA)
			flg |= X25_MBIT;
		l = 3;
		if (test_bit(X25_STATE_MOD128, &l3c->state))
			l++;
		else if (test_bit(X25_STATE_MOD32768, &l3c->state))
			l += 4;
		skb_push(nskb, l);
		if (l != X25_add_header(l3c, l3c->l3, X25_PTYPE_DATA, nskb->data, flg))
			int_error();
		if (l3c->l3->l2l3m.state == ST_LL_ESTAB)
			X25_l3down(l3c->l3, DL_DATA_REQ, X25_next_id(l3c->l3), nskb);
		else {
			mISDN_sethead(DL_DATA_REQ, X25_next_id(l3c->l3), nskb);	
			skb_queue_tail(&l3c->l3->downq, nskb);
			break;
		}
		cq = get_free_confentry(l3c);
		skb = skb_dequeue(&l3c->dataq);
		n++;
	}
	return(n);
}

__u16
x25_data_b3_req(x25_channel_t *l3c, int dinfo, struct sk_buff *skb)
{
	__u16		size;
	mISDN_head_t	*hh = mISDN_HEAD_P(skb);

	if (!l3c)
		return(0x2002);
	if (skb->len < 10)
		return(0x2007);
	if ((confq_len(l3c) + skb_queue_len(&l3c->dataq)) > 7)
		return(CAPI_SENDQUEUEFULL);
	if ((l3c->x25p.state != ST_P4) && (l3c->x25d.state != ST_D1))
		return(0x2001);

	size =  CAPIMSG_U16(skb->data, 4);

	/* we save DataHandle and Flags in a area after normal mISDN_HEAD */ 
	hh++;
	hh->prim = CAPIMSG_U16(skb->data, 6);
	hh->dinfo = CAPIMSG_U16(skb->data, 8);
	/* the data begins behind the header, we don't use Data32/Data64 here */
	if ((skb->len - size) == 18)
		skb_pull(skb, 18);
	else if ((skb->len - size) == 10) // old format
		skb_pull(skb, 10);
	else
		return(0x2007);
	if (hh->dinfo & CAPI_FLAG_EXPEDITED) { // TODO Interrupt packet
	}

	skb_queue_tail(&l3c->dataq, skb);
	X25_invoke_sending(l3c);
	return(0);
}

int
x25_data_b3_resp(x25_channel_t *l3c, int dinfo, struct sk_buff *skb)
{
	int		i, m = 8;

	if (!l3c)
		return(-ENODEV);

	i = CAPIMSG_U16(skb->data, 0);
	dev_kfree_skb(skb);
	if (i >= CAPI_MAXDATAWINDOW) {
		int_error();
		return(-EINVAL);
	}
	if (l3c->recv_handles[i] == 0) {
		int_error();
		return(-EINVAL);
	}
	if (l3c->recv_handles[i] & CAPI_FLAG_DELIVERCONF) {
		if (test_bit(X25_STATE_MOD32768, &l3c->state))
			m = 32768;
		else if (test_bit(X25_STATE_MOD128, &l3c->state))
			m = 128;
		l3c->rps++;
		if (l3c->rps >= m)
			l3c->rps = 0;
		l3c->recv_handles[i] = 0;
		i = 0;
		if (X25_cansend(l3c) && skb_queue_len(&l3c->dataq))
			X25_invoke_sending(l3c);
		else {
			i = 1;
			X25sendL3frame(l3c, l3c->l3, X25_PTYPE_RR, 0, NULL);
		}
	} else {
		l3c->recv_handles[i] = 0;
		i = 0;
	}
	if (test_and_clear_bit(X25_STATE_DTE_RNR, &l3c->state)) {
		if (!i)
			X25sendL3frame(l3c, l3c->l3, X25_PTYPE_RR, 0, NULL);
	}	
	return(0);
}

int
X25_l3_init(void)
{
	llfsm.state_count = LL_STATE_COUNT;
	llfsm.event_count = LL_EVENT_COUNT;
	llfsm.strEvent = strLLEvent;
	llfsm.strState = strLLState;
	mISDN_FsmNew(&llfsm, LLFnList, LL_FN_COUNT);
	return(0);
}

void
X25_l3_cleanup(void)
{
	mISDN_FsmFree(&llfsm);
}

/* $Id: layer3.c,v 0.1 2001/02/11 22:46:19 kkeil Exp $
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
#define __NO_VERSION__
#include "hisax.h"
#include "hisaxl3.h"

const char *l3_revision = "$Revision: 0.1 $";

static
struct Fsm l3fsm = {NULL, 0, 0, NULL, NULL};

enum {
	ST_L3_LC_REL,
	ST_L3_LC_ESTAB_WAIT,
	ST_L3_LC_REL_DELAY, 
	ST_L3_LC_REL_WAIT,
	ST_L3_LC_ESTAB,
};

#define L3_STATE_COUNT (ST_L3_LC_ESTAB+1)

static char *strL3State[] =
{
	"ST_L3_LC_REL",
	"ST_L3_LC_ESTAB_WAIT",
	"ST_L3_LC_REL_DELAY",
	"ST_L3_LC_REL_WAIT",
	"ST_L3_LC_ESTAB",
};

enum {
	EV_ESTABLISH_REQ,
	EV_ESTABLISH_IND,
	EV_ESTABLISH_CNF,
	EV_RELEASE_REQ,
	EV_RELEASE_CNF,
	EV_RELEASE_IND,
	EV_TIMEOUT,
};

#define L3_EVENT_COUNT (EV_TIMEOUT+1)

static char *strL3Event[] =
{
	"EV_ESTABLISH_REQ",
	"EV_ESTABLISH_IND",
	"EV_ESTABLISH_CNF",
	"EV_RELEASE_REQ",
	"EV_RELEASE_CNF",
	"EV_RELEASE_IND",
	"EV_TIMEOUT",
};

static void
l3m_debug(struct FsmInst *fi, char *fmt, ...)
{
	layer3_t *l3 = fi->userdata;
	logdata_t log;

	va_start(log.args, fmt);
	log.fmt = fmt;
	log.head = l3->inst.id;
	l3->inst.obj->ctrl(&l3->inst, MGR_DEBUGDATA | REQUEST, &log);
	va_end(log.args);
}

u_char *
findie(u_char * p, int size, u_char ie, int wanted_set)
{
	int l, codeset, maincodeset;
	u_char *pend = p + size;

	/* skip protocol discriminator, callref and message type */
	p++;
	l = (*p++) & 0xf;
	p += l;
	p++;
	codeset = 0;
	maincodeset = 0;
	/* while there are bytes left... */
	while (p < pend) {
		if ((*p & 0xf0) == 0x90) {
			codeset = *p & 0x07;
			if (!(*p & 0x08))
				maincodeset = codeset;
		}
		if (codeset == wanted_set) {
			if (*p == ie) {
				/* improved length check (Werner Cornelius) */
				if (!(*p & 0x80)) {
					if ((pend - p) < 2)
						return(NULL);
					if (*(p+1) > (pend - (p+2)))
						return(NULL);
				}
				return (p);
			} else if ((*p > ie) && !(*p & 0x80))
				return (NULL);
		}
		if (!(*p & 0x80)) {
			p++;
			l = *p;
			p += l;
			codeset = maincodeset;
		}
		p++;
	}
	return (NULL);
}

int
getcallref(u_char * p)
{
	int l, cr = 0;

	p++;			/* prot discr */
	if (*p & 0xfe)		/* wrong callref BRI only 1 octet*/
		return(-2);
	l = 0xf & *p++;		/* callref length */
	if (!l)			/* dummy CallRef */
		return(-1);
	cr = *p++;
	return (cr);
}

static int OrigCallRef = 0;

int
newcallref(void)
{
	if (OrigCallRef == 127)
		OrigCallRef = 1;
	else
		OrigCallRef++;
	return (OrigCallRef);
}

void
newl3state(l3_process_t *pc, int state)
{
	if (pc->debug & L3_DEB_STATE)
		l3m_debug(&pc->l3->l3m, "newstate cr %d %d --> %d", 
			 pc->callref & 0x7F,
			 pc->state, state);
	pc->state = state;
}

static void
L3ExpireTimer(L3Timer_t *t)
{
	t->pc->l3->p_down(t->pc->l3, t->event, t->pc);
}

void
L3InitTimer(l3_process_t *pc, L3Timer_t *t)
{
	t->pc = pc;
	t->tl.function = (void *) L3ExpireTimer;
	t->tl.data = (long) t;
	init_timer(&t->tl);
}

void
L3DelTimer(L3Timer_t *t)
{
	del_timer(&t->tl);
}

int
L3AddTimer(L3Timer_t *t,
	   int millisec, int event)
{
	if (timer_pending(&t->tl)) {
		printk(KERN_WARNING "L3AddTimer: timer already active!\n");
		return -1;
	}
	init_timer(&t->tl);
	t->event = event;
	t->tl.expires = jiffies + (millisec * HZ) / 1000;
	add_timer(&t->tl);
	return 0;
}

void
StopAllL3Timer(l3_process_t *pc)
{
	L3DelTimer(&pc->timer);
}

struct sk_buff *
l3_alloc_skb(int len)
{
	struct sk_buff *skb;

	if (!(skb = alloc_skb(len + MAX_HEADER_LEN, GFP_ATOMIC))) {
		printk(KERN_WARNING "HiSax: No skb for D-channel\n");
		return (NULL);
	}
	skb_reserve(skb, MAX_HEADER_LEN);
	return (skb);
}
/*
static void
no_l3_proto(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;

	HiSax_putstatus(st->l1.hardware, "L3", "no D protocol");
	if (skb) {
		dev_kfree_skb(skb);
	}
}

static int
no_l3_proto_spec(struct PStack *st, isdn_ctrl *ic)
{
	printk(KERN_WARNING "HiSax: no specific protocol handler for proto %lu\n",ic->arg & 0xFF);
	return(-1);
}
*/

l3_process_t
*getl3proc(layer3_t *l3, int cr)
{
	l3_process_t *p = l3->proc;

	while (p)
		if (p->callref == cr)
			return (p);
		else
			p = p->next;
	return (NULL);
}

l3_process_t
*new_l3_process(layer3_t *l3, int cr)
{
	l3_process_t *p;

	if (!(p = kmalloc(sizeof(l3_process_t), GFP_ATOMIC))) {
		printk(KERN_ERR "HiSax can't get memory for cr %d\n", cr);
		return (NULL);
	}
	memset(p, 0, sizeof(l3_process_t));
	p->l3 = l3;
	p->debug = l3->debug;
	p->callref = cr;
	p->N303 = l3->N303;
	L3InitTimer(p, &p->timer);
	APPEND_TO_LIST(p, l3->proc);
	return (p);
};

void
release_l3_process(l3_process_t *p)
{
	layer3_t *l3;

	if (!p)
		return;
	l3 = p->l3;
	REMOVE_FROM_LISTBASE(p, l3->proc);
	StopAllL3Timer(p);
	if (!l3->proc)
		RELEASE();
};

static void
l3ml3p(layer3_t *l3, int pr)
{
	l3_process_t *p = l3->proc;
	l3_process_t *np;

	while (p) {
		/* p might be kfreed under us, so we need to save where we want to go on */
		np = p->next;
		l3->p_down(l3, pr, p);
		p = np;
	}
}

#if 0
setstack_l3dc(struct PStack *st, struct Channel *chanp)
{
	char tmp[64];

	st->l3.proc   = NULL;
	st->l3.global = NULL;
	skb_queue_head_init(&st->l3.squeue);
	st->l3.l3m.fsm = &l3fsm;
	st->l3.l3m.state = ST_L3_LC_REL;
	st->l3.l3m.debug = 1;
	st->l3.l3m.userdata = st;
	st->l3.l3m.userint = 0;
	st->l3.l3m.printdebug = l3m_debug;
        FsmInitTimer(&st->l3.l3m, &st->l3.l3m_timer);
	strcpy(st->l3.debug_id, "L3DC ");
	st->lli.l4l3_proto = no_l3_proto_spec;

#ifdef	CONFIG_HISAX_EURO
	if (st->protocol == ISDN_PTYPE_EURO) {
		setstack_dss1(st);
	} else
#endif
#ifdef  CONFIG_HISAX_NI1
	if (st->protocol == ISDN_PTYPE_NI1) {
		setstack_ni1(st);
	} else
#endif
#ifdef	CONFIG_HISAX_1TR6
	if (st->protocol == ISDN_PTYPE_1TR6) {
		setstack_1tr6(st);
	} else
#endif
	if (st->protocol == ISDN_PTYPE_LEASED) {
		st->lli.l4l3 = no_l3_proto;
		st->l2.l2l3 = no_l3_proto;
                st->l3.l3ml3 = no_l3_proto;
		printk(KERN_INFO "HiSax: Leased line mode\n");
	} else {
		st->lli.l4l3 = no_l3_proto;
		st->l2.l2l3 = no_l3_proto;
                st->l3.l3ml3 = no_l3_proto;
		sprintf(tmp, "protocol %s not supported",
			(st->protocol == ISDN_PTYPE_1TR6) ? "1tr6" :
			(st->protocol == ISDN_PTYPE_EURO) ? "euro" :
			(st->protocol == ISDN_PTYPE_NI1) ? "ni1" :
			"unknown");
		printk(KERN_WARNING "HiSax: %s\n", tmp);
		st->protocol = -1;
	}
}


void
isdnl3_trans(struct PStack *st, int pr, void *arg) {
	st->l3.l3l2(st, pr, arg);
}

void
releasestack_isdnl3(struct PStack *st)
{
	while (st->l3.proc)
		release_l3_process(st->l3.proc);
	if (st->l3.global) {
		StopAllL3Timer(st->l3.global);
		kfree(st->l3.global);
		st->l3.global = NULL;
	}
	FsmDelTimer(&st->l3.l3m_timer, 54);
	discard_queue(&st->l3.squeue);
}

void
setstack_l3bc(struct PStack *st, struct Channel *chanp)
{

	st->l3.proc   = NULL;
	st->l3.global = NULL;
	skb_queue_head_init(&st->l3.squeue);
	st->l3.l3m.fsm = &l3fsm;
	st->l3.l3m.state = ST_L3_LC_REL;
	st->l3.l3m.debug = 1;
	st->l3.l3m.userdata = st;
	st->l3.l3m.userint = 0;
	st->l3.l3m.printdebug = l3m_debug;
	strcpy(st->l3.debug_id, "L3BC ");
	st->lli.l4l3 = isdnl3_trans;
}
#endif

int
hisax_l3up(l3_process_t *l3p, u_int prim, void *arg) {
	layer3_t *l3;
	hisaxif_t *up;
	int err = -EINVAL;

	if (!l3p)
		return(-EINVAL);
	l3 = l3p->l3;
	up = &l3->inst.up;
	if (up->func)
		err = up->func(up, prim, l3->msgnr++, 0, arg);
	return(err);
}

static int
l3down(layer3_t *l3, u_int prim, u_int nr, int dtyp, void *arg) {
	hisaxif_t *down = &l3->inst.down;
	int err = -EINVAL;

	if (down->func) 
		err = down->func(down, prim, nr, dtyp, arg);
	else
		printk(KERN_WARNING "l3down: no down func prim %x\n", prim);
	return(err);
}

#define DREL_TIMER_VALUE 40000

static void
lc_activate(struct FsmInst *fi, int event, void *arg)
{
	layer3_t *l3 = fi->userdata;

	FsmChangeState(fi, ST_L3_LC_ESTAB_WAIT);
	l3down(l3, DL_ESTABLISH | REQUEST, l3->msgnr++, 0, NULL);
}

static void
lc_connect(struct FsmInst *fi, int event, void *arg)
{
	layer3_t *l3 = fi->userdata;
	struct sk_buff *skb = arg;
	int dequeued = 0;

	FsmChangeState(fi, ST_L3_LC_ESTAB);
	while ((skb = skb_dequeue(&l3->squeue))) {
		l3down(l3, DL_DATA | REQUEST, l3->msgnr++, DTYPE_SKB, skb);
		dequeued++;
	}
	if ((!l3->proc) &&  dequeued) {
		if (l3->debug)
			l3m_debug(fi, "lc_connect: release link");
		FsmEvent(&l3->l3m, EV_RELEASE_REQ, NULL);
	} else
		l3ml3p(l3, DL_ESTABLISH | INDICATION);
}

static void
lc_connected(struct FsmInst *fi, int event, void *arg)
{
	layer3_t *l3 = fi->userdata;
	struct sk_buff *skb = arg;
	int dequeued = 0;

	FsmDelTimer(&l3->l3m_timer, 51);
	FsmChangeState(fi, ST_L3_LC_ESTAB);
	while ((skb = skb_dequeue(&l3->squeue))) {
		l3down(l3, DL_DATA | REQUEST, l3->msgnr++, DTYPE_SKB, skb);
		dequeued++;
	}
	if ((!l3->proc) &&  dequeued) {
		if (l3->debug)
			l3m_debug(fi, "lc_connected: release link");
		FsmEvent(&l3->l3m, EV_RELEASE_REQ, NULL);
	} else
		l3ml3p(l3, DL_ESTABLISH | CONFIRM);
}

static void
lc_start_delay(struct FsmInst *fi, int event, void *arg)
{
	layer3_t *l3 = fi->userdata;

	FsmChangeState(fi, ST_L3_LC_REL_DELAY);
	FsmAddTimer(&l3->l3m_timer, DREL_TIMER_VALUE, EV_TIMEOUT, NULL, 50);
}

static void
lc_release_req(struct FsmInst *fi, int event, void *arg)
{
	layer3_t *l3 = fi->userdata;

	if (test_bit(FLG_L2BLOCK, &l3->Flag)) {
		if (l3->debug)
			l3m_debug(fi, "lc_release_req: l2 blocked");
		/* restart release timer */
		FsmAddTimer(&l3->l3m_timer, DREL_TIMER_VALUE, EV_TIMEOUT, NULL, 51);
	} else {
		FsmChangeState(fi, ST_L3_LC_REL_WAIT);
		l3down(l3, DL_RELEASE | REQUEST, l3->msgnr++, 0, NULL);
	}
}

static void
lc_release_ind(struct FsmInst *fi, int event, void *arg)
{
	layer3_t *l3 = fi->userdata;

	FsmDelTimer(&l3->l3m_timer, 52);
	FsmChangeState(fi, ST_L3_LC_REL);
	discard_queue(&l3->squeue);
	l3ml3p(l3, DL_RELEASE | INDICATION);
}

static void
lc_release_cnf(struct FsmInst *fi, int event, void *arg)
{
	layer3_t *l3 = fi->userdata;

	FsmChangeState(fi, ST_L3_LC_REL);
	discard_queue(&l3->squeue);
	l3ml3p(l3, DL_RELEASE | CONFIRM);
}


/* *INDENT-OFF* */
static struct FsmNode L3FnList[] HISAX_INITDATA =
{
	{ST_L3_LC_REL,		EV_ESTABLISH_REQ,	lc_activate},
	{ST_L3_LC_REL,		EV_ESTABLISH_IND,	lc_connect},
	{ST_L3_LC_REL,		EV_ESTABLISH_CNF,	lc_connect},
	{ST_L3_LC_ESTAB_WAIT,	EV_ESTABLISH_CNF,	lc_connected},
	{ST_L3_LC_ESTAB_WAIT,	EV_RELEASE_REQ,		lc_start_delay},
	{ST_L3_LC_ESTAB_WAIT,	EV_RELEASE_IND,		lc_release_ind},
	{ST_L3_LC_ESTAB,	EV_RELEASE_IND,		lc_release_ind},
	{ST_L3_LC_ESTAB,	EV_RELEASE_REQ,		lc_start_delay},
        {ST_L3_LC_REL_DELAY,    EV_RELEASE_IND,         lc_release_ind},
        {ST_L3_LC_REL_DELAY,    EV_ESTABLISH_REQ,       lc_connected},
        {ST_L3_LC_REL_DELAY,    EV_TIMEOUT,             lc_release_req},
	{ST_L3_LC_REL_WAIT,	EV_RELEASE_CNF,		lc_release_cnf},
	{ST_L3_LC_REL_WAIT,	EV_ESTABLISH_REQ,	lc_activate},
};
/* *INDENT-ON* */

#define L3_FN_COUNT (sizeof(L3FnList)/sizeof(struct FsmNode))

void
l3_msg(layer3_t *l3, int pr, void *arg)
{
	switch (pr) {
		case (DL_DATA | REQUEST):
			if (l3->l3m.state == ST_L3_LC_ESTAB) {
				l3down(l3, pr, l3->msgnr++, DTYPE_SKB, arg);
			} else {
				struct sk_buff *skb = arg;

				skb_queue_tail(&l3->squeue, skb);
				FsmEvent(&l3->l3m, EV_ESTABLISH_REQ, NULL); 
			}
			break;
		case (DL_ESTABLISH | REQUEST):
			FsmEvent(&l3->l3m, EV_ESTABLISH_REQ, NULL);
			break;
		case (DL_ESTABLISH | CONFIRM):
			FsmEvent(&l3->l3m, EV_ESTABLISH_CNF, NULL);
			break;
		case (DL_ESTABLISH | INDICATION):
			FsmEvent(&l3->l3m, EV_ESTABLISH_IND, NULL);
			break;
		case (DL_RELEASE | INDICATION):
			FsmEvent(&l3->l3m, EV_RELEASE_IND, NULL);
			break;
		case (DL_RELEASE | CONFIRM):
			FsmEvent(&l3->l3m, EV_RELEASE_CNF, NULL);
			break;
		case (DL_RELEASE | REQUEST):
			FsmEvent(&l3->l3m, EV_RELEASE_REQ, NULL);
			break;
	}
}

HISAX_INITFUNC(void
Isdnl3New(void))
{
	l3fsm.state_count = L3_STATE_COUNT;
	l3fsm.event_count = L3_EVENT_COUNT;
	l3fsm.strEvent = strL3Event;
	l3fsm.strState = strL3State;
	FsmNew(&l3fsm, L3FnList, L3_FN_COUNT);
}

void
Isdnl3Free(void)
{
	FsmFree(&l3fsm);
}
 
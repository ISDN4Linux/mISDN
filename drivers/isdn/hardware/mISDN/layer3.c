/* $Id: layer3.c,v 1.15 2004/06/17 12:31:12 keil Exp $
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
#include "layer3.h"
#include "helper.h"

const char *l3_revision = "$Revision: 1.15 $";

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
	log.head = l3->inst.name;
	l3->inst.obj->ctrl(&l3->inst, MGR_DEBUGDATA | REQUEST, &log);
	va_end(log.args);
}

void
l3_debug(layer3_t *l3, char *fmt, ...)
{
	logdata_t log;

	va_start(log.args, fmt);
	log.fmt = fmt;
	log.head = l3->inst.name;
	l3->inst.obj->ctrl(&l3->inst, MGR_DEBUGDATA | REQUEST, &log);
	va_end(log.args);
}

static int
l3_newid(layer3_t *l3)
{
	u_long	flags;
	int	id;

	spin_lock_irqsave(&l3->lock, flags);
	id = l3->next_id++;
	if (id == 0x7fff)
		l3->next_id = 1;
	spin_unlock_irqrestore(&l3->lock, flags);
	id |= (l3->entity << 16);
	return(id);
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
					p++; /* points to len */
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

int
newcallref(layer3_t *l3)
{
	int max = 127;

	if (test_bit(FLG_CRLEN2, &l3->Flag))
		max = 32767;

	if (l3->OrigCallRef >= max)
		l3->OrigCallRef = 1;
	else
		l3->OrigCallRef++;
	return (l3->OrigCallRef);
}

void
newl3state(l3_process_t *pc, int state)
{
	if (pc->l3->debug & L3_DEB_STATE)
		l3m_debug(&pc->l3->l3m, "newstate cr %d %d --> %d", 
			 pc->callref & 0x7F,
			 pc->state, state);
	pc->state = state;
}

static void
L3ExpireTimer(L3Timer_t *t)
{
	t->pc->l3->p_mgr(t->pc, t->event, NULL);
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
	if (pc->t303skb) {
		dev_kfree_skb(pc->t303skb);
		pc->t303skb = NULL;
	}
}

/*
static void
no_l3_proto(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;

	mISDN_putstatus(st->l1.hardware, "L3", "no D protocol");
	if (skb) {
		dev_kfree_skb(skb);
	}
}

static int
no_l3_proto_spec(struct PStack *st, isdn_ctrl *ic)
{
	printk(KERN_WARNING "mISDN: no specific protocol handler for proto %lu\n",ic->arg & 0xFF);
	return(-1);
}
*/

l3_process_t
*getl3proc(layer3_t *l3, int cr)
{
	l3_process_t *p;
	
	list_for_each_entry(p, &l3->plist, list)
		if (p->callref == cr)
			return (p);
	return (NULL);
}

l3_process_t
*getl3proc4id(layer3_t *l3, u_int id)
{
	l3_process_t *p;

	list_for_each_entry(p, &l3->plist, list)
		if (p->id == id)
			return (p);
	return (NULL);
}

l3_process_t
*new_l3_process(layer3_t *l3, int cr, int n303, u_int id)
{
	l3_process_t	*p = NULL;
	u_long		flags;

	if (id == MISDN_ID_ANY) {
		if (l3->entity == MISDN_ENTITY_NONE) {
			printk(KERN_WARNING "%s: no entity allocated for l3(%x)\n",
				__FUNCTION__, l3->id);
			return (NULL);
		}
		spin_lock_irqsave(&l3->lock, flags);
		if (l3->pid_cnt == 0x7FFF)
			l3->pid_cnt = 0;
		while(l3->pid_cnt <= 0x7FFF) {
			l3->pid_cnt++;
			id = l3->pid_cnt | (l3->entity << 16);
			p = getl3proc4id(l3, id);
			if (!p)
				break;
		}
		spin_unlock_irqrestore(&l3->lock, flags);
		if (p) {
			printk(KERN_WARNING "%s: no free process_id for l3(%x) entity(%x)\n",
				__FUNCTION__, l3->id, l3->entity);
			return (NULL);
		}
	} else {
		/* id from other entity */
		p = getl3proc4id(l3, id);
		if (p) {
			printk(KERN_WARNING "%s: process_id(%x) allready in use in l3(%x)\n",
				__FUNCTION__, id, l3->id);
			return (NULL);
		}
	}
	if (!(p = kmalloc(sizeof(l3_process_t), GFP_ATOMIC))) {
		printk(KERN_ERR "mISDN can't get memory for cr %d\n", cr);
		return (NULL);
	}
	memset(p, 0, sizeof(l3_process_t));
	p->l3 = l3;
	p->id = id;
	p->callref = cr;
	p->n303 = n303;
	L3InitTimer(p, &p->timer);
	list_add_tail(&p->list, &l3->plist);
	return (p);
};

void
release_l3_process(l3_process_t *p)
{
	layer3_t *l3;

	if (!p)
		return;
	l3 = p->l3;
	mISDN_l3up(p, CC_RELEASE_CR | INDICATION, NULL);
	list_del(&p->list);
	StopAllL3Timer(p);
	kfree(p);
	if (list_empty(&l3->plist) && !test_bit(FLG_PTP, &l3->Flag)) {
		if (l3->debug)
			l3_debug(l3, "release_l3_process: last process");
		if (!skb_queue_len(&l3->squeue)) {
			if (l3->debug)
				l3_debug(l3, "release_l3_process: release link");
			mISDN_FsmEvent(&l3->l3m, EV_RELEASE_REQ, NULL);
		} else {
			if (l3->debug)
				l3_debug(l3, "release_l3_process: not release link");
		}
	}
};

static void
l3ml3p(layer3_t *l3, int pr)
{
	l3_process_t *p, *np;

	list_for_each_entry_safe(p, np, &l3->plist, list) 
		l3->p_mgr(p, pr, NULL);
}

int
mISDN_l3up(l3_process_t *l3p, u_int prim, struct sk_buff *skb)
{
	layer3_t *l3;
	int err = -EINVAL;

	if (!l3p)
		return(-EINVAL);
	l3 = l3p->l3;
	if (!skb)
		err = if_link(&l3->inst.up, prim, l3p->id, 0, NULL, 0);
	else
		err = if_newhead(&l3->inst.up, prim, l3p->id, skb);
	return(err);
}

static int
l3down(layer3_t *l3, u_int prim, int dinfo, struct sk_buff *skb) {
	int err = -EINVAL;

	if (!skb)
		err = if_link(&l3->inst.down, prim, dinfo, 0, NULL, 0);
	else
		err = if_newhead(&l3->inst.down, prim, dinfo, skb);
	return(err);
}

#define DREL_TIMER_VALUE 40000

static void
lc_activate(struct FsmInst *fi, int event, void *arg)
{
	layer3_t *l3 = fi->userdata;

	mISDN_FsmChangeState(fi, ST_L3_LC_ESTAB_WAIT);
	l3down(l3, DL_ESTABLISH | REQUEST, 0, NULL);
}

static void
lc_connect(struct FsmInst *fi, int event, void *arg)
{
	layer3_t *l3 = fi->userdata;
	struct sk_buff *skb;
	int dequeued = 0;

	mISDN_FsmChangeState(fi, ST_L3_LC_ESTAB);
	while ((skb = skb_dequeue(&l3->squeue))) {
		if (l3down(l3, DL_DATA | REQUEST, l3_newid(l3), skb))
			dev_kfree_skb(skb);
		dequeued++;
	}
	if (list_empty(&l3->plist) &&  dequeued) {
		if (l3->debug)
			l3m_debug(fi, "lc_connect: release link");
		mISDN_FsmEvent(&l3->l3m, EV_RELEASE_REQ, NULL);
	} else
		l3ml3p(l3, DL_ESTABLISH | INDICATION);
}

static void
lc_connected(struct FsmInst *fi, int event, void *arg)
{
	layer3_t *l3 = fi->userdata;
	struct sk_buff *skb;
	int dequeued = 0;

	mISDN_FsmDelTimer(&l3->l3m_timer, 51);
	mISDN_FsmChangeState(fi, ST_L3_LC_ESTAB);
	while ((skb = skb_dequeue(&l3->squeue))) {
		if (l3down(l3, DL_DATA | REQUEST, l3_newid(l3), skb))
			dev_kfree_skb(skb);
		dequeued++;
	}
	if (list_empty(&l3->plist) &&  dequeued) {
		if (l3->debug)
			l3m_debug(fi, "lc_connected: release link");
		mISDN_FsmEvent(&l3->l3m, EV_RELEASE_REQ, NULL);
	} else
		l3ml3p(l3, DL_ESTABLISH | CONFIRM);
}

static void
lc_start_delay(struct FsmInst *fi, int event, void *arg)
{
	layer3_t *l3 = fi->userdata;

	mISDN_FsmChangeState(fi, ST_L3_LC_REL_DELAY);
	mISDN_FsmAddTimer(&l3->l3m_timer, DREL_TIMER_VALUE, EV_TIMEOUT, NULL, 50);
}

static void
lc_release_req(struct FsmInst *fi, int event, void *arg)
{
	layer3_t *l3 = fi->userdata;

	if (test_bit(FLG_L2BLOCK, &l3->Flag)) {
		if (l3->debug)
			l3m_debug(fi, "lc_release_req: l2 blocked");
		/* restart release timer */
		mISDN_FsmAddTimer(&l3->l3m_timer, DREL_TIMER_VALUE, EV_TIMEOUT, NULL, 51);
	} else {
		mISDN_FsmChangeState(fi, ST_L3_LC_REL_WAIT);
		l3down(l3, DL_RELEASE | REQUEST, 0, NULL);
	}
}

static void
lc_release_ind(struct FsmInst *fi, int event, void *arg)
{
	layer3_t *l3 = fi->userdata;

	mISDN_FsmDelTimer(&l3->l3m_timer, 52);
	mISDN_FsmChangeState(fi, ST_L3_LC_REL);
	discard_queue(&l3->squeue);
	l3ml3p(l3, DL_RELEASE | INDICATION);
}

static void
lc_release_cnf(struct FsmInst *fi, int event, void *arg)
{
	layer3_t *l3 = fi->userdata;

	mISDN_FsmChangeState(fi, ST_L3_LC_REL);
	discard_queue(&l3->squeue);
	l3ml3p(l3, DL_RELEASE | CONFIRM);
}


/* *INDENT-OFF* */
static struct FsmNode L3FnList[] =
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

int
l3_msg(layer3_t *l3, u_int pr, int dinfo, int len, void *arg)
{
	switch (pr) {
		case (DL_DATA | REQUEST):
			if (l3->l3m.state == ST_L3_LC_ESTAB) {
				return(l3down(l3, pr, l3_newid(l3), arg));
			} else {
				struct sk_buff *skb = arg;

//				printk(KERN_DEBUG "%s: queue skb %p len(%d)\n",
//					__FUNCTION__, skb, skb->len);
				skb_queue_tail(&l3->squeue, skb);
				mISDN_FsmEvent(&l3->l3m, EV_ESTABLISH_REQ, NULL); 
			}
			break;
		case (DL_ESTABLISH | REQUEST):
			mISDN_FsmEvent(&l3->l3m, EV_ESTABLISH_REQ, NULL);
			break;
		case (DL_ESTABLISH | CONFIRM):
			mISDN_FsmEvent(&l3->l3m, EV_ESTABLISH_CNF, NULL);
			break;
		case (DL_ESTABLISH | INDICATION):
			mISDN_FsmEvent(&l3->l3m, EV_ESTABLISH_IND, NULL);
			break;
		case (DL_RELEASE | INDICATION):
			mISDN_FsmEvent(&l3->l3m, EV_RELEASE_IND, NULL);
			break;
		case (DL_RELEASE | CONFIRM):
			mISDN_FsmEvent(&l3->l3m, EV_RELEASE_CNF, NULL);
			break;
		case (DL_RELEASE | REQUEST):
			mISDN_FsmEvent(&l3->l3m, EV_RELEASE_REQ, NULL);
			break;
	}
	return(0);
}

void
init_l3(layer3_t *l3)
{
	INIT_LIST_HEAD(&l3->plist);
	l3->global = NULL;
	l3->dummy = NULL;
	l3->entity = MISDN_ENTITY_NONE;
	l3->next_id = 1;
	spin_lock_init(&l3->lock);
	skb_queue_head_init(&l3->squeue);
	l3->l3m.fsm = &l3fsm;
	l3->l3m.state = ST_L3_LC_REL;
	l3->l3m.debug = l3->debug;
	l3->l3m.userdata = l3;
	l3->l3m.userint = 0;
	l3->l3m.printdebug = l3m_debug;
        mISDN_FsmInitTimer(&l3->l3m, &l3->l3m_timer);
}


void
release_l3(layer3_t *l3)
{
	l3_process_t *p, *np;

	if (l3->l3m.debug)
		printk(KERN_DEBUG "release_l3(%p) plist(%s) global(%p) dummy(%p)\n",
			l3, list_empty(&l3->plist) ? "no" : "yes", l3->global, l3->dummy);
	list_for_each_entry_safe(p, np, &l3->plist, list)
		release_l3_process(p);
	if (l3->global) {
		StopAllL3Timer(l3->global);
		kfree(l3->global);
		l3->global = NULL;
	}
	if (l3->dummy) {
		StopAllL3Timer(l3->dummy);
		kfree(l3->dummy);
		l3->dummy = NULL;
	}
	mISDN_FsmDelTimer(&l3->l3m_timer, 54);
	discard_queue(&l3->squeue);
}

void
mISDNl3New(void)
{
	l3fsm.state_count = L3_STATE_COUNT;
	l3fsm.event_count = L3_EVENT_COUNT;
	l3fsm.strEvent = strL3Event;
	l3fsm.strState = strL3State;
	mISDN_FsmNew(&l3fsm, L3FnList, L3_FN_COUNT);
}

void
mISDNl3Free(void)
{
	mISDN_FsmFree(&l3fsm);
}

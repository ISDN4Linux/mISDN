/* $Id: layer1.c,v 0.2 2001/02/11 22:57:23 kkeil Exp $
 *
 * hisax_l1.c     common low level stuff for I.430 layer1
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 *		This file is (c) under GNU PUBLIC LICENSE
 *		For changes and modifications please read
 *		../../../Documentation/isdn/HiSax.cert
 *
 */

const char *l1_revision = "$Revision: 0.2 $";

#include <linux/config.h>
#include <linux/module.h>
#include "hisax.h"
#include "hisaxl1.h"
#include "debug.h"

typedef struct _layer1 {
	struct _layer1	*prev;
	struct _layer1	*next;
	int Flags;
	struct FsmInst l1m;
	struct FsmTimer timer;
	int debug;
	int delay;
	u_int	last_nr;
	hisaxinstance_t	inst;
} layer1_t;

static layer1_t *l1list = NULL;
static int debug = 0;
static hisaxobject_t isdnl1;
static u_int msgnr = 1;

#define TIMER3_VALUE 7000

static
struct Fsm l1fsm_b =
{NULL, 0, 0, NULL, NULL};

static
struct Fsm l1fsm_s =
{NULL, 0, 0, NULL, NULL};

enum {
	ST_L1_F2,
	ST_L1_F3,
	ST_L1_F4,
	ST_L1_F5,
	ST_L1_F6,
	ST_L1_F7,
	ST_L1_F8,
};

#define L1S_STATE_COUNT (ST_L1_F8+1)

static char *strL1SState[] =
{
	"ST_L1_F2",
	"ST_L1_F3",
	"ST_L1_F4",
	"ST_L1_F5",
	"ST_L1_F6",
	"ST_L1_F7",
	"ST_L1_F8",
};

#ifdef HISAX_UINTERFACE
static
struct Fsm l1fsm_u =
{NULL, 0, 0, NULL, NULL};

enum {
	ST_L1_RESET,
	ST_L1_DEACT,
	ST_L1_SYNC2,
	ST_L1_TRANS,
};

#define L1U_STATE_COUNT (ST_L1_TRANS+1)

static char *strL1UState[] =
{
	"ST_L1_RESET",
	"ST_L1_DEACT",
	"ST_L1_SYNC2",
	"ST_L1_TRANS",
};
#endif

enum {
	ST_L1_NULL,
	ST_L1_WAIT_ACT,
	ST_L1_WAIT_DEACT,
	ST_L1_ACTIV,
};

#define L1B_STATE_COUNT (ST_L1_ACTIV+1)

static char *strL1BState[] =
{
	"ST_L1_NULL",
	"ST_L1_WAIT_ACT",
	"ST_L1_WAIT_DEACT",
	"ST_L1_ACTIV",
};

enum {
	EV_PH_ACTIVATE,
	EV_PH_DEACTIVATE,
	EV_RESET_IND,
	EV_DEACT_CNF,
	EV_DEACT_IND,
	EV_POWER_UP,
	EV_ANYSIG_IND, 
	EV_INFO2_IND,
	EV_INFO4_IND,
	EV_TIMER_DEACT,
	EV_TIMER_ACT,
	EV_TIMER3,
};

#define L1_EVENT_COUNT (EV_TIMER3 + 1)

static char *strL1Event[] =
{
	"EV_PH_ACTIVATE",
	"EV_PH_DEACTIVATE",
	"EV_RESET_IND",
	"EV_DEACT_CNF",
	"EV_DEACT_IND",
	"EV_POWER_UP",
	"EV_ANYSIG_IND", 
	"EV_INFO2_IND",
	"EV_INFO4_IND",
	"EV_TIMER_DEACT",
	"EV_TIMER_ACT",
	"EV_TIMER3",
};

static void
l1m_debug(struct FsmInst *fi, char *fmt, ...)
{
	layer1_t *l1 = fi->userdata;
	logdata_t log;

	va_start(log.args, fmt);
	log.fmt = fmt;
	log.head = l1->inst.id;
	l1->inst.obj->ctrl(&l1->inst, MGR_DEBUGDATA | REQUEST, &log);
	va_end(log.args);
}

static int
l1up(layer1_t *l1, u_int prim, u_int nr, int dtyp, void *arg) {
	int		err = -EINVAL;
	hisaxif_t	*upif = &l1->inst.up;

	if (upif) {
		err = upif->func(upif, prim, nr, dtyp, arg);
		if (err < 0) {
			printk(KERN_WARNING "HiSax: l1up err %d\n", err);
			return(err);
		}
	}
	return(err);	
}

static void
l1_reset(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_L1_F3);
}

static void
l1_deact_cnf(struct FsmInst *fi, int event, void *arg)
{
	layer1_t *l1 = fi->userdata;

	FsmChangeState(fi, ST_L1_F3);
	if (test_bit(FLG_L1_ACTIVATING, &l1->Flags))
		l1->inst.down.func(&l1->inst.down, PH_CONTROL | REQUEST,
			msgnr++, 4, (void *)HW_POWERUP);
}

static void
l1_deact_req_s(struct FsmInst *fi, int event, void *arg)
{
	layer1_t *l1 = fi->userdata;

	FsmChangeState(fi, ST_L1_F3);
	FsmRestartTimer(&l1->timer, 550, EV_TIMER_DEACT, NULL, 2);
	test_and_set_bit(FLG_L1_DEACTTIMER, &l1->Flags);
}

static void
l1_power_up_s(struct FsmInst *fi, int event, void *arg)
{
	layer1_t *l1 = fi->userdata;

	if (test_bit(FLG_L1_ACTIVATING, &l1->Flags)) {
		FsmChangeState(fi, ST_L1_F4);
		l1->inst.down.func(&l1->inst.down, PH_SIGNAL | REQUEST,
			msgnr++, 4, (void *)INFO3_P8);
		FsmRestartTimer(&l1->timer, TIMER3_VALUE, EV_TIMER3, NULL, 2);
		test_and_set_bit(FLG_L1_T3RUN, &l1->Flags);
	} else
		FsmChangeState(fi, ST_L1_F3);
}

static void
l1_go_F5(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_L1_F5);
}

static void
l1_go_F8(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_L1_F8);
}

static void
l1_info2_ind(struct FsmInst *fi, int event, void *arg)
{
	layer1_t *l1 = fi->userdata;

#ifdef HISAX_UINTERFACE
	if (test_bit(FLG_L1_UINT, &l1->Flags))
		FsmChangeState(fi, ST_L1_SYNC2);
	else
#endif
		FsmChangeState(fi, ST_L1_F6);
	l1->inst.down.func(&l1->inst.down, PH_SIGNAL | REQUEST, msgnr++,
		4, (void *)INFO3_P8);
}

static void
l1_info4_ind(struct FsmInst *fi, int event, void *arg)
{
	layer1_t *l1 = fi->userdata;

#ifdef HISAX_UINTERFACE
	if (test_bit(FLG_L1_UINT, &l1->Flags))
		FsmChangeState(fi, ST_L1_TRANS);
	else
#endif
		FsmChangeState(fi, ST_L1_F7);
	l1->inst.down.func(&l1->inst.down, PH_SIGNAL | REQUEST, msgnr++,
		4, (void *)INFO3_P8);
	if (test_and_clear_bit(FLG_L1_DEACTTIMER, &l1->Flags))
		FsmDelTimer(&l1->timer, 4);
	if (!test_bit(FLG_L1_ACTIVATED, &l1->Flags)) {
		if (test_and_clear_bit(FLG_L1_T3RUN, &l1->Flags))
			FsmDelTimer(&l1->timer, 3);
		FsmRestartTimer(&l1->timer, 110, EV_TIMER_ACT, NULL, 2);
		test_and_set_bit(FLG_L1_ACTTIMER, &l1->Flags);
	}
}

static void
l1_timer3(struct FsmInst *fi, int event, void *arg)
{
	layer1_t *l1 = fi->userdata;

	test_and_clear_bit(FLG_L1_T3RUN, &l1->Flags);	
	if (test_and_clear_bit(FLG_L1_ACTIVATING, &l1->Flags)) {
		if (test_and_clear_bit(FLG_L1_DBUSY, &l1->Flags))
			l1up(l1, PH_PAUSE | CONFIRM, msgnr++, 0, NULL);
		l1up(l1, PH_DEACTIVATE | INDICATION, msgnr++, 0, NULL);
	}
#ifdef HISAX_UINTERFACE
	if (!test_bit(FLG_L1_UINT, &l1->Flags))
#endif
	if (l1->l1m.state != ST_L1_F6) {
		FsmChangeState(fi, ST_L1_F3);
		l1->inst.down.func(&l1->inst.down, PH_CONTROL | REQUEST,
			msgnr++, 4, (void *)HW_POWERUP);
	}
}

static void
l1_timer_act(struct FsmInst *fi, int event, void *arg)
{
	layer1_t *l1 = fi->userdata;

	test_and_clear_bit(FLG_L1_ACTTIMER, &l1->Flags);
	test_and_set_bit(FLG_L1_ACTIVATED, &l1->Flags);
	if (test_and_clear_bit(FLG_L1_ACTIVATING, &l1->Flags))
		l1up(l1, PH_ACTIVATE | CONFIRM, l1->last_nr, 0, NULL);
	else
		l1up(l1, PH_ACTIVATE | INDICATION, msgnr++, 0, NULL);
}

static void
l1_timer_deact(struct FsmInst *fi, int event, void *arg)
{
	layer1_t *l1 = fi->userdata;

	test_and_clear_bit(FLG_L1_DEACTTIMER, &l1->Flags);
	test_and_clear_bit(FLG_L1_ACTIVATED, &l1->Flags);
	if (test_and_clear_bit(FLG_L1_DBUSY, &l1->Flags))
		l1up(l1, PH_PAUSE | CONFIRM, msgnr++, 0, NULL);
	l1up(l1, PH_DEACTIVATE | INDICATION, msgnr++, 0, NULL);
	l1->inst.down.func(&l1->inst.down, PH_CONTROL | REQUEST, msgnr++, 4,
		(void *)HW_DEACTIVATE);
}

static void
l1_activate_s(struct FsmInst *fi, int event, void *arg)
{
	layer1_t *l1 = fi->userdata;

	l1->inst.down.func(&l1->inst.down, PH_CONTROL | REQUEST, msgnr++, 4,
		(void *)HW_RESET);
}

static void
l1_activate_no(struct FsmInst *fi, int event, void *arg)
{
	layer1_t *l1 = fi->userdata;

	if ((!test_bit(FLG_L1_DEACTTIMER, &l1->Flags)) && (!test_bit(FLG_L1_T3RUN, &l1->Flags))) {
		test_and_clear_bit(FLG_L1_ACTIVATING, &l1->Flags);
		if (test_and_clear_bit(FLG_L1_DBUSY, &l1->Flags))
			l1up(l1, PH_PAUSE | CONFIRM, msgnr++, 0, NULL);
		l1up(l1, PH_DEACTIVATE | INDICATION, msgnr++, 0, NULL);
	}
}

static struct FsmNode L1SFnList[] HISAX_INITDATA =
{
	{ST_L1_F3, EV_PH_ACTIVATE, l1_activate_s},
	{ST_L1_F6, EV_PH_ACTIVATE, l1_activate_no},
	{ST_L1_F8, EV_PH_ACTIVATE, l1_activate_no},
	{ST_L1_F3, EV_RESET_IND, l1_reset},
	{ST_L1_F4, EV_RESET_IND, l1_reset},
	{ST_L1_F5, EV_RESET_IND, l1_reset},
	{ST_L1_F6, EV_RESET_IND, l1_reset},
	{ST_L1_F7, EV_RESET_IND, l1_reset},
	{ST_L1_F8, EV_RESET_IND, l1_reset},
	{ST_L1_F3, EV_DEACT_CNF, l1_deact_cnf},
	{ST_L1_F4, EV_DEACT_CNF, l1_deact_cnf},
	{ST_L1_F5, EV_DEACT_CNF, l1_deact_cnf},
	{ST_L1_F6, EV_DEACT_CNF, l1_deact_cnf},
	{ST_L1_F7, EV_DEACT_CNF, l1_deact_cnf},
	{ST_L1_F8, EV_DEACT_CNF, l1_deact_cnf},
	{ST_L1_F6, EV_DEACT_IND, l1_deact_req_s},
	{ST_L1_F7, EV_DEACT_IND, l1_deact_req_s},
	{ST_L1_F8, EV_DEACT_IND, l1_deact_req_s},
	{ST_L1_F3, EV_POWER_UP,  l1_power_up_s},
	{ST_L1_F4, EV_ANYSIG_IND,l1_go_F5},
	{ST_L1_F6, EV_ANYSIG_IND,l1_go_F8},
	{ST_L1_F7, EV_ANYSIG_IND,l1_go_F8},
	{ST_L1_F3, EV_INFO2_IND, l1_info2_ind},
	{ST_L1_F4, EV_INFO2_IND, l1_info2_ind},
	{ST_L1_F5, EV_INFO2_IND, l1_info2_ind},
	{ST_L1_F7, EV_INFO2_IND, l1_info2_ind},
	{ST_L1_F8, EV_INFO2_IND, l1_info2_ind},
	{ST_L1_F3, EV_INFO4_IND, l1_info4_ind},
	{ST_L1_F4, EV_INFO4_IND, l1_info4_ind},
	{ST_L1_F5, EV_INFO4_IND, l1_info4_ind},
	{ST_L1_F6, EV_INFO4_IND, l1_info4_ind},
	{ST_L1_F8, EV_INFO4_IND, l1_info4_ind},
	{ST_L1_F3, EV_TIMER3, l1_timer3},
	{ST_L1_F4, EV_TIMER3, l1_timer3},
	{ST_L1_F5, EV_TIMER3, l1_timer3},
	{ST_L1_F6, EV_TIMER3, l1_timer3},
	{ST_L1_F8, EV_TIMER3, l1_timer3},
	{ST_L1_F7, EV_TIMER_ACT, l1_timer_act},
	{ST_L1_F3, EV_TIMER_DEACT, l1_timer_deact},
	{ST_L1_F4, EV_TIMER_DEACT, l1_timer_deact},
	{ST_L1_F5, EV_TIMER_DEACT, l1_timer_deact},
	{ST_L1_F6, EV_TIMER_DEACT, l1_timer_deact},
	{ST_L1_F7, EV_TIMER_DEACT, l1_timer_deact},
	{ST_L1_F8, EV_TIMER_DEACT, l1_timer_deact},
};

#define L1S_FN_COUNT (sizeof(L1SFnList)/sizeof(struct FsmNode))

#ifdef HISAX_UINTERFACE
static void
l1_deact_req_u(struct FsmInst *fi, int event, void *arg)
{
	layer1_t *l1 = fi->userdata;

	FsmChangeState(fi, ST_L1_RESET);
	FsmRestartTimer(&l1->timer, 550, EV_TIMER_DEACT, NULL, 2);
	test_and_set_bit(FLG_L1_DEACTTIMER, &l1->Flags);
	l1->inst.down.func(&l1->inst.down, PH_CONTROL | REQUEST, msgnr++, 4,
		(void *)HW_POWERUP);
}

static void
l1_power_up_u(struct FsmInst *fi, int event, void *arg)
{
	layer1_t *l1 = fi->userdata;

	FsmRestartTimer(&l1->timer, TIMER3_VALUE, EV_TIMER3, NULL, 2);
	test_and_set_bit(FLG_L1_T3RUN, &l1->Flags);
}

static void
l1_info0_ind(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_L1_DEACT);
}

static void
l1_activate_u(struct FsmInst *fi, int event, void *arg)
{
	layer1_t *l1 = fi->userdata;

	l1->inst.down.func(&l1->inst.down, PH_SIGNAL | REQUEST, msgnr++, 4,
		(void *)INFO1);
}

static struct FsmNode L1UFnList[] HISAX_INITDATA =
{
	{ST_L1_RESET, EV_DEACT_IND, l1_deact_req_u},
	{ST_L1_DEACT, EV_DEACT_IND, l1_deact_req_u},
	{ST_L1_SYNC2, EV_DEACT_IND, l1_deact_req_u},
	{ST_L1_TRANS, EV_DEACT_IND, l1_deact_req_u},
	{ST_L1_DEACT, EV_PH_ACTIVATE, l1_activate_u},
	{ST_L1_DEACT, EV_POWER_UP, l1_power_up_u},
	{ST_L1_DEACT, EV_INFO2_IND, l1_info2_ind},
	{ST_L1_TRANS, EV_INFO2_IND, l1_info2_ind},
	{ST_L1_RESET, EV_DEACT_CNF, l1_info0_ind},
	{ST_L1_DEACT, EV_INFO4_IND, l1_info4_ind},
	{ST_L1_SYNC2, EV_INFO4_IND, l1_info4_ind},
	{ST_L1_RESET, EV_INFO4_IND, l1_info4_ind},
	{ST_L1_DEACT, EV_TIMER3, l1_timer3},
	{ST_L1_SYNC2, EV_TIMER3, l1_timer3},
	{ST_L1_TRANS, EV_TIMER_ACT, l1_timer_act},
	{ST_L1_DEACT, EV_TIMER_DEACT, l1_timer_deact},
	{ST_L1_SYNC2, EV_TIMER_DEACT, l1_timer_deact},
	{ST_L1_RESET, EV_TIMER_DEACT, l1_timer_deact},
};

#define L1U_FN_COUNT (sizeof(L1UFnList)/sizeof(struct FsmNode))

#endif

static void
l1b_activate(struct FsmInst *fi, int event, void *arg)
{
	layer1_t *l1 = fi->userdata;

	FsmChangeState(fi, ST_L1_WAIT_ACT);
	FsmRestartTimer(&l1->timer, l1->delay, EV_TIMER_ACT, NULL, 2);
}

static void
l1b_deactivate(struct FsmInst *fi, int event, void *arg)
{
	layer1_t *l1 = fi->userdata;

	FsmChangeState(fi, ST_L1_WAIT_DEACT);
	FsmRestartTimer(&l1->timer, 10, EV_TIMER_DEACT, NULL, 2);
}

static void
l1b_timer_act(struct FsmInst *fi, int event, void *arg)
{
	layer1_t *l1 = fi->userdata;

	FsmChangeState(fi, ST_L1_ACTIV);
	l1up(l1, PH_ACTIVATE | CONFIRM, l1->last_nr, 0, NULL);
}

static void
l1b_timer_deact(struct FsmInst *fi, int event, void *arg)
{
	layer1_t *l1 = fi->userdata;

	FsmChangeState(fi, ST_L1_NULL);
	l1up(l1, PH_DEACTIVATE | CONFIRM, l1->last_nr, 0, NULL);
}

static struct FsmNode L1BFnList[] HISAX_INITDATA =
{
	{ST_L1_NULL, EV_PH_ACTIVATE, l1b_activate},
	{ST_L1_WAIT_ACT, EV_TIMER_ACT, l1b_timer_act},
	{ST_L1_ACTIV, EV_PH_DEACTIVATE, l1b_deactivate},
	{ST_L1_WAIT_DEACT, EV_TIMER_DEACT, l1b_timer_deact},
};

#define L1B_FN_COUNT (sizeof(L1BFnList)/sizeof(struct FsmNode))

static int
l1from_up(hisaxif_t *hif, u_int prim, u_int nr, int dtyp, void *arg) {
	layer1_t *l1;

	if (!hif || !hif->fdata)
		return(-EINVAL);
	l1 = hif->fdata;
	switch(prim) {
		case (PH_DATA | REQUEST):
		case (PH_TESTLOOP | REQUEST):
			return(l1->inst.down.func(&l1->inst.down, prim, nr, dtyp, arg));
			break;
		case (PH_ACTIVATE | REQUEST):
			if (test_bit(FLG_L1_ACTIVATED, &l1->Flags))
				l1up(l1, PH_ACTIVATE | CONFIRM, nr, 0, NULL);
			else {
				test_and_set_bit(FLG_L1_ACTIVATING, &l1->Flags);
				l1->last_nr = nr;
				FsmEvent(&l1->l1m, EV_PH_ACTIVATE, arg);
			}
			break;
		default:
			if (l1->debug)
				hisaxdebug(l1->inst.st->id, NULL,
					"l1from_up msg %x unhandled", prim);
			return(-EINVAL);
			break;
	}
	return(0);
}

static int
l1from_down(hisaxif_t *hif, u_int prim, u_int nr, int dtyp, void *arg) {
	layer1_t *l1;
	u_int val = (u_int)arg;

	if (!hif || !hif->fdata)
		return(-EINVAL);
	l1 = hif->fdata;
	if (prim == PH_DATA_IND) {
		if (test_bit(FLG_L1_ACTTIMER, &l1->Flags))
			FsmEvent(&l1->l1m, EV_TIMER_ACT, NULL);	
		return(l1up(l1, prim, nr, dtyp, arg));
	} else if (prim == PH_DATA_CNF) {
		return(l1up(l1, prim, nr, dtyp, arg));
	} else if (prim == (PH_CONTROL | INDICATION)) {
		if (val == HW_RESET)
			FsmEvent(&l1->l1m, EV_RESET_IND, arg);
		else if (val == HW_DEACTIVATE)
			FsmEvent(&l1->l1m, EV_DEACT_IND, arg);
		else if (val == HW_POWERUP)
			FsmEvent(&l1->l1m, EV_POWER_UP, arg);
	} else if (prim == (PH_CONTROL | CONFIRM)) {
		if (val == HW_DEACTIVATE) 
			FsmEvent(&l1->l1m, EV_DEACT_CNF, arg);
	} else if (prim == (PH_SIGNAL | INDICATION)) {
		if (val == ANYSIGNAL)
			FsmEvent(&l1->l1m, EV_ANYSIG_IND, arg);
		else if (val == LOSTFRAMING)
			FsmEvent(&l1->l1m, EV_ANYSIG_IND, arg);
		else if (val == INFO2)
			FsmEvent(&l1->l1m, EV_INFO2_IND, arg);
		else if (val == INFO4_P8)
			FsmEvent(&l1->l1m, EV_INFO4_IND, arg);
		else if (val == INFO4_P10)
			FsmEvent(&l1->l1m, EV_INFO4_IND, arg);
		else {
			if (l1->debug)
				hisaxdebug(l1->inst.st->id, NULL,
					"l1from_down sig %x unhandled", val);
			return(-EINVAL);
		}
	} else {
		if (l1->debug)
			hisaxdebug(l1->inst.st->id, NULL,
				"l1from_down msg %x unhandled", prim);
		return(-EINVAL);
	}
	return(0);
}

static void
release_l1(layer1_t *l1) {
	hisaxinstance_t	*inst = &l1->inst;
	hisaxif_t hif;

	FsmDelTimer(&l1->timer, 0);
	memset(&hif, 0, sizeof(hisaxif_t));
	hif.fdata = l1;
	hif.func = l1from_up;
	hif.protocol = inst->up.protocol;
	hif.layer = inst->up.layer;
	isdnl1.ctrl(inst->st, MGR_DELIF | REQUEST, &hif);
	hif.fdata = l1;
	hif.func = l1from_down;
	hif.protocol = inst->down.protocol;
	hif.layer = inst->down.layer;
	isdnl1.ctrl(inst->st, MGR_DELIF | REQUEST, &hif);
	REMOVE_FROM_LISTBASE(l1, l1list);
	REMOVE_FROM_LIST(inst);
	if (inst->st)
		if (inst->st->inst[inst->layer] == inst)
			inst->st->inst[inst->layer] = inst->next;
	kfree(l1);
	isdnl1.refcnt--;
}

static layer1_t *
create_l1(hisaxstack_t *st, hisaxif_t *hif) {
	layer1_t *nl1;
	int lay, err;

	if (!hif)
		return(NULL);
	printk(KERN_DEBUG "create_l1 prot %x\n", hif->protocol);
	if (!st) {
		printk(KERN_ERR "create_l1 no stack\n");
		return(NULL);
	}
	if (!(nl1 = kmalloc(sizeof(layer1_t), GFP_ATOMIC))) {
		printk(KERN_ERR "kmalloc layer1_t failed\n");
		return(NULL);
	}
	memset(nl1, 0, sizeof(layer1_t));
	nl1->inst.protocol = hif->protocol;
	switch(nl1->inst.protocol) {
	    case ISDN_PID_L1_TE_S0:
	    	sprintf(nl1->inst.id, "l1TES0 %d", st->id);
		nl1->l1m.fsm = &l1fsm_s;
		nl1->l1m.state = ST_L1_F3;
		nl1->Flags = 0;
		break;
	    default:
		printk(KERN_ERR "layer1 create failed prt %x\n",nl1->inst.protocol);
		kfree(nl1);
		return(NULL);
	}
	nl1->debug = debug;
	nl1->l1m.debug = debug;
	nl1->l1m.userdata = nl1;
	nl1->l1m.userint = 0;
	nl1->l1m.printdebug = l1m_debug;
	FsmInitTimer(&nl1->l1m, &nl1->timer);
	nl1->inst.obj = &isdnl1;
	nl1->inst.layer = hif->layer;
	nl1->inst.data = nl1;
	APPEND_TO_LIST(nl1, l1list);
	isdnl1.ctrl(st, MGR_ADDLAYER | INDICATION, &nl1->inst);
	lay = nl1->inst.layer + 1;
	if ((lay<0) || (lay>MAX_LAYER)) {
		lay = 0;
		nl1->inst.up.protocol = ISDN_PID_NONE;
	} else
		nl1->inst.up.protocol = st->protocols[lay];
	nl1->inst.up.layer = lay;
	nl1->inst.up.stat = IF_DOWN;
	lay = nl1->inst.layer - 1;
	if ((lay<0) || (lay>MAX_LAYER)) {
		lay = 0;
		nl1->inst.down.protocol = ISDN_PID_NONE;
	} else
		nl1->inst.down.protocol = st->protocols[lay];
	nl1->inst.down.layer = lay;
	nl1->inst.down.stat = IF_UP;
	err = isdnl1.ctrl(st, MGR_ADDIF | REQUEST, &nl1->inst.down);
	if (err) {
		release_l1(nl1);
		printk(KERN_ERR "layer1 down interface request failed %d\n", err);
		return(NULL);
	}
	err = isdnl1.ctrl(st, MGR_ADDIF | REQUEST, &nl1->inst.up);
	if (err) {
		release_l1(nl1);
		printk(KERN_ERR "layer1 up interface request failed %d\n", err);
		return(NULL);
	}
	return(nl1);
}

static int
add_if(layer1_t *l1, hisaxif_t *hif) {
	int err;
	hisaxinstance_t *inst = &l1->inst;

	printk(KERN_DEBUG "layer1 add_if lay %d/%d prot %x\n", hif->layer,
		hif->stat, hif->protocol);
	hif->fdata = l1;
	if (IF_TYPE(hif) == IF_UP) {
		hif->func = l1from_up;
		if (inst->up.stat == IF_NOACTIV) {
			inst->up.stat = IF_DOWN;
			inst->up.protocol =
				inst->st->protocols[inst->up.layer];
			err = isdnl1.ctrl(inst->st, MGR_ADDIF | REQUEST, &inst->up);
			if (err)
				inst->up.stat = IF_NOACTIV;
		}
	} else if (IF_TYPE(hif) == IF_DOWN) {
		hif->func = l1from_down;
		if (inst->down.stat == IF_NOACTIV) {
			inst->down.stat = IF_UP;
			inst->down.protocol =
				inst->st->protocols[inst->down.layer];
			err = isdnl1.ctrl(inst->st, MGR_ADDIF | REQUEST, &inst->down);
			if (err)
				inst->down.stat = IF_NOACTIV;
		}
	} else
		return(-EINVAL);
	return(0);
}

static int
del_if(layer1_t *l1, hisaxif_t *hif) {
	int err;
	hisaxinstance_t *inst = &l1->inst;

	printk(KERN_DEBUG "layer1 del_if lay %d/%d %p/%p\n", hif->layer,
		hif->stat, hif->func, hif->fdata);
	if ((hif->func == inst->up.func) && (hif->fdata == inst->up.fdata)) {
		inst->up.stat = IF_NOACTIV;
		inst->up.protocol = ISDN_PID_NONE;
		err = isdnl1.ctrl(inst->st, MGR_ADDIF | REQUEST, &inst->up);
	} else if ((hif->func == inst->down.func) && (hif->fdata == inst->down.fdata)) {
		inst->down.stat = IF_NOACTIV;
		inst->down.protocol = ISDN_PID_NONE;
		err = isdnl1.ctrl(inst->st, MGR_ADDIF | REQUEST, &inst->down);
	} else {
		printk(KERN_DEBUG "layer1 del_if no if found\n");
		return(-EINVAL);
	}
	return(0);
}

static char MName[] = "ISDNL1";

static int L1Protocols[] = {	ISDN_PID_L1_TE_S0,
#ifdef HISAX_UINTERFACE
				ISDN_PID_L1_TE_U
#endif
			};
#define PROTOCOLCNT	(sizeof(L1Protocols)/sizeof(int))
 
#ifdef MODULE
MODULE_AUTHOR("Karsten Keil");
MODULE_PARM(debug, "1i");
#define Isdnl1Init init_module
#endif

static int
l1_manager(void *data, u_int prim, void *arg) {
	hisaxstack_t *st = data;
	layer1_t *l1l = l1list;

//	printk(KERN_DEBUG "l1_manager data:%p prim:%x arg:%p\n", data, prim, arg);
	if (!data)
		return(-EINVAL);
	while(l1l) {
		if (l1l->inst.st == st)
			break;
		l1l = l1l->next;
	}
	switch(prim) {
	    case MGR_ADDIF | REQUEST:
		if (!l1l)
			l1l = create_l1(st, arg);
		if (!l1l) {
			printk(KERN_WARNING "l1_manager create_l1 failed\n");
			return(-EINVAL);
		}
		return(add_if(l1l, arg));
		break;
	    case MGR_DELIF | REQUEST:
		if (!l1l) {
			printk(KERN_WARNING "l1_manager delif no instance\n");
			return(-EINVAL);
		}
		return(del_if(l1l, arg));
		break;
	    case MGR_RELEASE | INDICATION:
	    	if (l1l) {
			printk(KERN_DEBUG "release_l1 id %x\n", l1l->inst.st->id);
	    		release_l1(l1l);
	    	} else 
	    		printk(KERN_WARNING "l1_manager release no instance\n");
	    	break;
	    		
	    default:
		printk(KERN_WARNING "fritz_manager prim %x not handled\n", prim);
		return(-EINVAL);
	}
	return(0);
}

int Isdnl1Init(void)
{
	int err;

	isdnl1.name = MName;
	isdnl1.protocols = L1Protocols;
	isdnl1.protcnt = PROTOCOLCNT;
	isdnl1.own_ctrl = l1_manager;
	isdnl1.prev = NULL;
	isdnl1.next = NULL;
	isdnl1.layer = 1;
#ifdef HISAX_UINTERFACE
	l1fsm_u.state_count = L1U_STATE_COUNT;
	l1fsm_u.event_count = L1_EVENT_COUNT;
	l1fsm_u.strEvent = strL1Event;
	l1fsm_u.strState = strL1UState;
	FsmNew(&l1fsm_u, L1UFnList, L1U_FN_COUNT);
#endif
	l1fsm_s.state_count = L1S_STATE_COUNT;
	l1fsm_s.event_count = L1_EVENT_COUNT;
	l1fsm_s.strEvent = strL1Event;
	l1fsm_s.strState = strL1SState;
	FsmNew(&l1fsm_s, L1SFnList, L1S_FN_COUNT);
	l1fsm_b.state_count = L1B_STATE_COUNT;
	l1fsm_b.event_count = L1_EVENT_COUNT;
	l1fsm_b.strEvent = strL1Event;
	l1fsm_b.strState = strL1BState;
	FsmNew(&l1fsm_b, L1BFnList, L1B_FN_COUNT);
	if ((err = HiSax_register(&isdnl1))) {
		printk(KERN_ERR "Can't register %s error(%d)\n", MName, err);
#ifdef HISAX_UINTERFACE
		FsmFree(&l1fsm_u);
#endif
		FsmFree(&l1fsm_s);
		FsmFree(&l1fsm_b);
	}
	return(err);
}

#ifdef MODULE
void cleanup_module(void)
{
	int err;

	if ((err = HiSax_unregister(&isdnl1))) {
		printk(KERN_ERR "Can't unregister ISDN layer 1 error(%d)\n", err);
	}
	if(l1list) {
		printk(KERN_WARNING "hisaxl1 l1list not empty\n");
		while(l1list)
			release_l1(l1list);
	}
#ifdef HISAX_UINTERFACE
	FsmFree(&l1fsm_u);
#endif
	FsmFree(&l1fsm_s);
	FsmFree(&l1fsm_b);
}
#endif

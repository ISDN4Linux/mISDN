/* $Id: fsm.h,v 1.2 2004/01/30 23:46:37 keil Exp $
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#ifndef _MISDN_FSM_H
#define _MISDN_FSM_H

#include <linux/timer.h>

/* Statemachine */

struct FsmInst;

typedef void (* FSMFNPTR)(struct FsmInst *, int, void *);

struct Fsm {
	FSMFNPTR *jumpmatrix;
	int state_count, event_count;
	char **strEvent, **strState;
};

struct FsmInst {
	struct Fsm *fsm;
	int state;
	int debug;
	void *userdata;
	int userint;
	void (*printdebug) (struct FsmInst *, char *, ...);
};

struct FsmNode {
	int state, event;
	void (*routine) (struct FsmInst *, int, void *);
};

struct FsmTimer {
	struct FsmInst *fi;
	struct timer_list tl;
	int event;
	void *arg;
};

extern void mISDN_FsmNew(struct Fsm *, struct FsmNode *, int);
extern void mISDN_FsmFree(struct Fsm *);
extern int mISDN_FsmEvent(struct FsmInst *, int , void *);
extern void mISDN_FsmChangeState(struct FsmInst *, int);
extern void mISDN_FsmInitTimer(struct FsmInst *, struct FsmTimer *);
extern int mISDN_FsmAddTimer(struct FsmTimer *, int, int, void *, int);
extern void mISDN_FsmRestartTimer(struct FsmTimer *, int, int, void *, int);
extern void mISDN_FsmDelTimer(struct FsmTimer *, int);

#endif

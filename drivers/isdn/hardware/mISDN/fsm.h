/* $Id: fsm.h,v 1.0 2001/11/02 23:42:26 kkeil Exp $
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

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

extern void FsmNew(struct Fsm *, struct FsmNode *, int);
extern void FsmFree(struct Fsm *);
extern int FsmEvent(struct FsmInst *, int , void *);
extern void FsmChangeState(struct FsmInst *, int);
extern void FsmInitTimer(struct FsmInst *, struct FsmTimer *);
extern int FsmAddTimer(struct FsmTimer *, int, int, void *, int);
extern void FsmRestartTimer(struct FsmTimer *, int, int, void *, int);
extern void FsmDelTimer(struct FsmTimer *, int);

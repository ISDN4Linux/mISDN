/* $Id: layer3.h,v 0.5 2001/02/19 22:25:31 kkeil Exp $
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#include <linux/hisaxif.h>
#include <linux/skbuff.h>
#include "fsm.h"

#define SBIT(state) (1<<state)
#define ALL_STATES  0x03ffffff

#define PROTO_DIS_EURO	0x08

#define L3_DEB_WARN	0x01
#define L3_DEB_PROTERR	0x02
#define L3_DEB_STATE	0x04
#define L3_DEB_CHARGE	0x08
#define L3_DEB_CHECK	0x10
#define L3_DEB_SI	0x20

#define FLG_L2BLOCK	1
#define FLG_PTP		2

typedef struct _cause {
	u_char	len __attribute__ ((packed));
	u_char	loc __attribute__ ((packed));
	u_char	val __attribute__ ((packed));
	u_char	diag[28] __attribute__ ((packed));
	u_char	rec __attribute__ ((packed));
} cause_t;

typedef struct _channel {
	u_char	len __attribute__ ((packed));
	u_char	chan __attribute__ ((packed));
	u_char	spare[6] __attribute__ ((packed));
} channel_t;

typedef struct _ALERTING {
	u_char *BEARER;
	u_char *CHANNEL_ID;
	u_char *FACILITY;
	u_char *PROGRESS;
	u_char *DISPLAY;
	u_char *SIGNAL;
	u_char *HLC;
	u_char *USER_USER;
} ALERTING_t;

typedef struct _CALL_PROCEEDING {
	u_char *BEARER;
	u_char *CHANNEL_ID;
	u_char *FACILITY;
	u_char *PROGRESS;
	u_char *DISPLAY;
	u_char *HLC;
} CALL_PROCEEDING_t;

typedef struct _CONNECT {
	u_char *BEARER;
	u_char *CHANNEL_ID;
	u_char *FACILITY;
	u_char *PROGRESS;
	u_char *DISPLAY;
	u_char *DATE;
	u_char *SIGNAL;
	u_char *CONNECT_PN;
	u_char *CONNECT_SUB;
	u_char *LLC;
	u_char *HLC;
	u_char *USER_USER;
} CONNECT_t;

typedef struct _CONNECT_ACKNOWLEDGE {
	u_char *CHANNEL_ID;
	u_char *DISPLAY;
	u_char *SIGNAL;
} CONNECT_ACKNOWLEDGE_t;

typedef struct _DISCONNECT {
	u_char *CAUSE;
	u_char *FACILITY;
	u_char *PROGRESS;
	u_char *DISPLAY;
	u_char *SIGNAL;
	u_char *USER_USER;
} DISCONNECT_t;

typedef struct _INFORMATION {
	u_char *COMPLETE;
	u_char *DISPLAY;
	u_char *KEYPAD;
	u_char *SIGNAL;
	u_char *CALLED_PN;
} INFORMATION_t;

typedef struct _NOTIFY {
	u_char *BEARER;
	u_char *NOTIFY;
	u_char *DISPLAY;
} NOTIFY_t;

typedef struct _PROGRESS {
	u_char *BEARER;
	u_char *CAUSE;
	u_char *FACILITY;
	u_char *PROGRESS;
	u_char *DISPLAY;
	u_char *HLC;
	u_char *USER_USER;
} PROGRESS_t;

typedef struct _RELEASE {
	u_char *CAUSE;
	u_char *FACILITY;
	u_char *DISPLAY;
	u_char *SIGNAL;
	u_char *USER_USER;
} RELEASE_t;

typedef struct _RELEASE_COMPLETE {
	u_char *CAUSE;
	u_char *DISPLAY;
	u_char *SIGNAL;
	u_char *USER_USER;
} RELEASE_COMPLETE_t;

typedef struct _RESUME {
	u_char *CALL_ID;
	u_char *FACILITY;
} RESUME_t;

typedef struct _RESUME_ACKNOWLEDGE {
	u_char *CHANNEL_ID;
	u_char *FACILITY;
	u_char *DISPLAY;
} RESUME_ACKNOWLEDGE_t;

typedef struct _RESUME_REJECT {
	u_char *CAUSE;
	u_char *DISPLAY;
} RESUME_REJECT_t;

typedef struct _SETUP {
	u_char *COMPLETE;
	u_char *BEARER;
	u_char *CHANNEL_ID;
	u_char *FACILITY;
	u_char *PROGRESS;
	u_char *NET_FAC;
	u_char *DISPLAY;
	u_char *KEYPAD;
	u_char *SIGNAL;
	u_char *CALLING_PN;
	u_char *CALLING_SUB;
	u_char *CALLED_PN;
	u_char *CALLED_SUB;
	u_char *REDIR_NR;
	u_char *LLC;
	u_char *HLC;
	u_char *USER_USER;
} SETUP_t;

typedef struct _SETUP_ACKNOWLEDGE {
	u_char *CHANNEL_ID;
	u_char *FACILITY;
	u_char *PROGRESS;
	u_char *DISPLAY;
	u_char *SIGNAL;
} SETUP_ACKNOWLEDGE_t;

typedef struct _STATUS {
	u_char *CAUSE;
	u_char *CALL_STATE;
	u_char *DISPLAY;
} STATUS_t;

typedef struct _STATUS_ENQUIRY {
	u_char *DISPLAY;
} STATUS_ENQUIRY_t;

typedef struct _SUSPEND {
	u_char *CALL_ID;
	u_char *FACILITY;
} SUSPEND_t;

typedef struct _SUSPEND_ACKNOWLEDGE {
	u_char *FACILITY;
	u_char *DISPLAY;
} SUSPEND_ACKNOWLEDGE_t;

typedef struct _SUSPEND_REJECT {
	u_char *CAUSE;
	u_char *DISPLAY;
} SUSPEND_REJECT_t;

typedef struct _CONGESTION_CONTROL {
	u_char *CONGESTION;
	u_char *CAUSE;
	u_char *DISPLAY;
} CONGESTION_CONTROL_t;

typedef struct _USER_INFORMATION {
	u_char *MORE_DATA;
	u_char *USER_USER;
} USER_INFORMATION_t;

typedef struct _RESTART {
	u_char *CHANNEL_ID;
	u_char *DISPLAY;
	u_char *RESTART_IND;
} RESTART_t;

typedef struct _FACILITY {
	u_char *FACILITY;
	u_char *DISPLAY;
} FACILITY_t;

typedef struct _L3Timer {
	struct _l3_process	*pc;
	struct timer_list	tl;
	int			event;
} L3Timer_t;

typedef struct _l3_process {
	struct _l3_process	*prev;
	struct _l3_process	*next;
	struct _layer3		*l3;
	int			callref;
	int			state;
	L3Timer_t		timer;
	int			n303;
	u_int			id;
	int			bc;
	u_char			obuf[MAX_DFRAME_LEN];
	u_char			*op;
	int			err;
	union {
		ALERTING_t		ALERTING;
		CALL_PROCEEDING_t	CALL_PROCEEDING;
		CONGESTION_CONTROL_t	CONGESTION_CONTROL;
		CONNECT_t		CONNECT;
		CONNECT_ACKNOWLEDGE_t	CONNECT_ACKNOWLEDGE;
		DISCONNECT_t		DISCONNECT;
		FACILITY_t		FACILITY;
		INFORMATION_t		INFORMATION;
		NOTIFY_t		NOTIFY;
		PROGRESS_t		PROGRESS;
		RELEASE_t		RELEASE;
		RELEASE_COMPLETE_t	RELEASE_COMPLETE;
		RESTART_t		RESTART;
		RESUME_ACKNOWLEDGE_t	RESUME_ACKNOWLEDGE;
		RESUME_REJECT_t		RESUME_REJECT;
		SETUP_t			SETUP;
		SETUP_ACKNOWLEDGE_t	SETUP_ACKNOWLEDGE;
		STATUS_t		STATUS;
		STATUS_ENQUIRY_t	STATUS_ENQUIRY;
		SUSPEND_ACKNOWLEDGE_t	SUSPEND_ACKNOWLEDGE;
		SUSPEND_REJECT_t	SUSPEND_REJECT;
		USER_INFORMATION_t	USER_INFORMATION;
	} para;
} l3_process_t;

typedef struct _layer3 {
	struct _layer3	*prev;
	struct _layer3	*next;
	struct FsmInst	l3m;
	struct FsmTimer	l3m_timer;
	l3_process_t	*proc;
	l3_process_t	*global;
	l3_process_t	*dummy;
	int		(*p_mgr)(l3_process_t *, u_int, void *);
	u_int		id;
	int		debug;
	u_int		Flag;
	u_int		msgnr;
	hisaxinstance_t	inst;
	struct sk_buff_head squeue;
} layer3_t;

struct stateentry {
	int state;
	int primitive;
	void (*rout) (l3_process_t *, u_char, void *);
};

extern int l3_msg(layer3_t *, u_int, u_int, int, void *);
extern struct sk_buff *l3_alloc_skb(int);
extern void newl3state(l3_process_t *, int);
extern void L3InitTimer(l3_process_t *, L3Timer_t *);
extern void L3DelTimer(L3Timer_t *);
extern int L3AddTimer(L3Timer_t *, int, int);
extern void StopAllL3Timer(l3_process_t *);
extern void release_l3_process(l3_process_t *);
extern l3_process_t *new_l3_process(layer3_t *, int, int);
extern l3_process_t *getl3proc(layer3_t *, int);
extern u_char *findie(u_char *, int, u_char, int);
extern int hisax_l3up(l3_process_t *, u_int, void *);
extern int getcallref(u_char * p);
extern int newcallref(void);
extern void init_l3(layer3_t *);
extern void release_l3(layer3_t *);
extern void HiSaxl3New(void);
extern void HiSaxl3Free(void);
extern void l3_debug(layer3_t *, char *, ...);

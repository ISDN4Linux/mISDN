/* $Id: x25_l3.h,v 1.4 2004/06/17 12:31:12 keil Exp $
 *
 * Layer 3 X.25 defines
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */
#ifndef _L3_X25_H
#define _L3_X25_H
#include "m_capi.h"

typedef struct _x25_l3		x25_l3_t;
typedef struct _x25_channel	x25_channel_t;
typedef struct _x25_B3_cfg	x25_B3_cfg_t;
typedef struct _x25_ncpi	x25_ncpi_t;
typedef struct _x25_ConfQueue	x25_ConfQueue_t;

#define DEBUG_L3X25_WARN	0x0001
#define	DEBUG_L3X25_MGR		0x1000

struct _x25_B3_cfg {
	__u16	LIC;
	__u16	HIC;
	__u16	LTC;
	__u16	HTC;
	__u16	LOC;
	__u16	HOC;
	__u16	modulo;
	__u16	winsize;
};

#define DEFAULT_X25_B3_CFG	{0, 0, 1, 1, 0, 0, 8, 2}

struct _x25_ncpi {
	__u8	len		__attribute__((packed));
	__u8	Flags		__attribute__((packed));
	__u8	Group		__attribute__((packed));
	__u8	Channel		__attribute__((packed));
	__u8	Contens[4]	__attribute__((packed)); /* Note this can be less/more bytes in use */ 
};

struct _x25_ConfQueue { 
	__u32		PktId; 
	__u16		DataHandle;
	__u16		MsgId; 
	struct sk_buff	*skb;
};

struct _x25_l3 {
	struct list_head	list;
	mISDNinstance_t		inst;
	struct list_head	channellist;
	struct FsmInst		l2l3m;
	struct FsmInst		x25r;
	struct FsmTimer		TR;
	int			TRval;
	int			TRrep;
	int			entity;
	int			next_id;
	struct sk_buff_head	downq;
	int			down_headerlen;
	int			up_headerlen;
	int			maxdatalen;
	x25_B3_cfg_t		B3cfg;
	u_long			state;
	u_int			debug;
	spinlock_t		lock;
	u_char			cause[2];
};

struct _x25_channel {
	struct list_head	list;
	x25_l3_t		*l3;
	struct FsmInst		x25p;
	struct FsmInst		x25d;
	struct FsmTimer		TP;
	int			TPval;
	int			TPrep;
	struct FsmTimer		TD;
	int			TDval;
	int			TDrep;
	__u32			ncci;
	u_char			*ncpi_data;
	u_int			ncpi_len;
	u_long			state;
	u_int			debug;
	spinlock_t		lock;
	u_int			pr;
	u_int			ps;
	u_int			rps;
	u_int			lwin;
	u_int			rwin;
	u_int			datasize;
	struct sk_buff_head	dataq;
	x25_ConfQueue_t		*confq;
	u_int			recv_handles[CAPI_MAXDATAWINDOW];
	__u16			lchan;
	u_char			cause[2];
};

#define X25_CHANNEL_INCOMING	1
#define X25_CHANNEL_OUTGOING	2

#define X25_STATE_ORGINATE	0
#define X25_STATE_DCE		1
#define X25_STATE_DTEDTE	2
#define X25_STATE_PERMANENT	3
#define X25_STATE_MOD128	4
#define X25_STATE_MOD32768	5
#define X25_STATE_ABIT		6
#define X25_STATE_DBIT		7
#define X25_STATE_ESTABLISH	16
#define X25_STATE_DXE_INTSENT	17
#define X25_STATE_DTE_INTSENT   18
#define X25_STATE_DXE_RNR	19
#define X25_STATE_DTE_RNR	20

#define X25_MINSIZE		8

#define X25_GFI_ABIT		0x80
#define X25_GFI_DBIT		0x40
#define X25_GFI_QBIT		0x80

#define X25_MBIT		0x01
#define X25_MBIT_MOD8		0x10

#define CAPI_FLAG_QUALIFIER	0x01
#define CAPI_FLAG_MOREDATA	0x02
#define CAPI_FLAG_DELIVERCONF	0x04
#define CAPI_FLAG_EXPEDITED	0x08

#define X25_PTYPE_CALL		0x0b
#define X25_PTYPE_CALL_CNF	0x0f
#define X25_PTYPE_CLEAR		0x13
#define X25_PTYPE_CLEAR_CNF	0x17
#define X25_PTYPE_INTERRUPT	0x23
#define X25_PTYPE_INTERRUPT_CNF	0x27
#define X25_PTYPE_DATA		0x00
#define X25_PTYPE_RR		0x01
#define X25_PTYPE_RNR		0x05
#define X25_PTYPE_REJ		0x09
#define X25_PTYPE_RESET		0x1f
#define X25_PTYPE_RESET_CNF	0x1b
#define X25_PTYPE_RESTART	0xfb
#define X25_PTYPE_RESTART_CNF	0xff
#define X25_PTYPE_REGISTER	0xf3
#define X25_PTYPE_REGISTER_CNF	0xf7
#define X25_PTYPE_DIAGNOSTIC	0xf1
#define X25_PTYPE_NOTYPE	0x7F

#define	T10_VALUE		60000
#define T11_VALUE		180000
#define	T12_VALUE		60000
#define T13_VALUE		60000
#define	T20_VALUE		180000
#define T21_VALUE		200000
#define	T22_VALUE		180000
#define T23_VALUE		180000
#define T24_VALUE		60000
#define T25_VALUE		200000
#define T26_VALUE		180000
#define T27_VALUE		60000
#define T28_VALUE		300000


#define R20_VALUE		1
#define R22_VALUE		1
#define R23_VALUE		1
#define R25_VALUE		0
#define R27_VALUE		0
#define R28_VALUE		1

#define X25_ERRCODE_DISCARD	0x0100

/* LinkLayer (L2) maintained by L3 statemachine */
enum {
	EV_L3_ESTABLISH_REQ,
	EV_LL_ESTABLISH_IND,
	EV_LL_ESTABLISH_CNF,
	EV_L3_RELEASE_REQ,
	EV_LL_RELEASE_CNF,
	EV_LL_RELEASE_IND,
};
#define LL_EVENT_COUNT	(EV_LL_RELEASE_IND+1)

/* X.25 Restart state machine */
enum {
	ST_R0,
	ST_R1,
	ST_R2,
	ST_R3,
};
#define R_STATE_COUNT	(ST_R3+1)
extern char *X25strRState[];

enum {
	EV_LL_READY,
	EV_L3_RESTART_REQ,
 	EV_L2_RESTART,
	EV_L2_RESTART_CNF,
	EV_L3_RESTART_TOUT,
};
#define R_EVENT_COUNT	(EV_L3_RESTART_TOUT+1)
extern char *X25strREvent[];

/* X.25 connection state machine */
enum {
	ST_P0,
	ST_P1,
	ST_P2,
	ST_P3,
	ST_P4,
	ST_P5,
	ST_P6,
	ST_P7,
};
#define P_STATE_COUNT	(ST_P7+1)
extern char *X25strPState[];

enum {
	EV_L3_READY,
	EV_L3_OUTGOING_CALL,
	EV_L2_INCOMING_CALL,
	EV_L2_CALL_CNF,
	EV_L3_CALL_ACCEPT,
	EV_L3_CLEARING,
	EV_L2_CLEAR,
	EV_L2_CLEAR_CNF,
	EV_L2_INVALPKT,
	EV_L3_CALL_TOUT,
	EV_L3_CLEAR_TOUT,
};
#define P_EVENT_COUNT	(EV_L3_CLEAR_TOUT+1)
extern char *X25strPEvent[];

/* X.25 Flowcontrol state machine */
enum {
	ST_D0,
	ST_D1,
	ST_D2,
	ST_D3,
};
#define D_STATE_COUNT	(ST_D3+1)
extern char *X25strDState[];

enum {
	EV_L3_CONNECT,
 	EV_L3_RESETING,
	EV_L2_RESET,
	EV_L2_RESET_CNF,
	EV_L3_RESET_TOUT,
};
#define D_EVENT_COUNT	(EV_L3_RESET_TOUT+1)
extern char *X25strDEvent[];

extern x25_channel_t	*X25_get_channel(x25_l3_t *, __u16);
extern x25_channel_t	*X25_get_channel4NCCI(x25_l3_t *, __u32);
extern int		X25_reset_channel(x25_channel_t *, struct sk_buff *);
extern int		X25_restart(x25_l3_t *);
extern int		X25_get_header(x25_l3_t *, struct sk_buff *, u_char *, __u16 *, u_char *);
extern void		X25_release_channel(x25_channel_t *);
extern void		X25_release_l3(x25_l3_t *);
extern int		X25_realloc_ncpi_data(x25_channel_t *, int, u_char *);
extern int		new_x25_channel(x25_l3_t *, x25_channel_t **, __u16, int, u_char *);
extern int		new_x25_l3(x25_l3_t **, mISDNstack_t *, mISDN_pid_t *, mISDNobject_t *, int);
extern int		X25_next_id(x25_l3_t *);
extern int		X25_add_header(x25_channel_t *, x25_l3_t *, u_char , u_char *, u_char);
extern int		X25sendL3frame(x25_channel_t *, x25_l3_t *, u_char, int, void *);
extern int		X25sendL4frame(x25_channel_t *, x25_l3_t *, int, int, int, void *);
extern int		X25sendL4skb(x25_channel_t *, x25_l3_t *, __u32, int, int, struct sk_buff *);
extern void		X25_send_diagnostic(x25_l3_t *, struct sk_buff *, int, int);
extern int		X25_l3down(x25_l3_t *, u_int, u_int, struct sk_buff *);
extern int		X25_l3_init(void);
extern void		X25_l3_cleanup(void);
extern int		X25_get_and_test_pr(x25_channel_t *, u_char, struct sk_buff *);
extern int		X25_get_and_test_ps(x25_channel_t *, u_char, struct sk_buff *);
extern int		X25_cansend(x25_channel_t *);
extern __u16		x25_data_b3_req(x25_channel_t *, int, struct sk_buff *);
extern int		x25_data_b3_resp(x25_channel_t *, int, struct sk_buff *);
extern int		X25_invoke_sending(x25_channel_t *);
extern int		X25_receive_data(x25_channel_t *, int, int, struct sk_buff *);

#endif

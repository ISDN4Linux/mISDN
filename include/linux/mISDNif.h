/* $Id: mISDNif.h,v 0.24 2001/04/11 16:38:57 kkeil Exp $
 *
 */

#ifndef HISAXIF_H
#define HISAXIF_H

#include <stdarg.h>
#include <linux/types.h>
#include <linux/errno.h>

/* primitives for information exchange
 * generell format
 * <8 bit reserved>
 * <4 bit flags>
 * <4 bit layer>
 * <8 bit command>
 * <8 bit subcommand>
 *
 */

/* SUBCOMMANDS */
#define REQUEST		0x80
#define CONFIRM		0x81
#define INDICATION	0x82
#define RESPONSE	0x83

/* management */
#define MGR_GETOBJECT	0x0f0100
#define MGR_NEWOBJECT	0x0f0200
#define MGR_DELOBJECT	0x0f0300
#define MGR_GETSTACK	0x0f1100
#define MGR_NEWSTACK	0x0f1200
#define MGR_DELSTACK	0x0f1300
#define MGR_SETSTACK	0x0f1400
#define MGR_CLEARSTACK	0x0f1500
#define MGR_REGLAYER	0x0f1600
#define MGR_UNREGLAYER	0x0f1700
#define MGR_GETLAYER	0x0f2100
#define MGR_NEWLAYER	0x0f2200
#define MGR_DELLAYER	0x0f2300
#define MGR_GETIF	0x0f3100
#define MGR_CONNECT	0x0f3200
#define MGR_DISCONNECT	0x0f3300
#define MGR_SETIF	0x0f3400
#define MGR_ADDIF	0x0f3500
#define MGR_RELEASE	0x0f4500
#define MGR_CONTROL	0x0fe100
#define MGR_STATUS	0x0fe200
#define MGR_HASPROTOCOL 0x0fe300
#define MGR_LOADFIRM	0x0ff000
#define MGR_LOGDATA	0x0ff100
#define MGR_DEBUGDATA	0x0ff200

/* layer 1 <-> hardware */
#define PH_SIGNAL	0x000100
#define PH_CONTROL	0x000200
#define PH_STATUS	0x000300

/* PH_SIGNAL parameter */
#define INFO0		0x1000
#define INFO1		0x1100
#define INFO2		0x1200
#define INFO3_P8	0x1308
#define INFO3_P10	0x130a
#define INFO4_P8	0x1408
#define INFO4_P10	0x140a
#define LOSTFRAMING	0x1f00
#define ANYSIGNAL	0x1f01

/* PH_CONTROL parameter */
#define HW_RESET	0x0001 
#define HW_POWERDOWN	0x0100 
#define HW_POWERUP	0x0101
#define HW_DEACTIVATE	0x0200
#define HW_ACTIVATE	0x0201
#define HW_MOD_FRM	0x0400
#define HW_MOD_FRH	0x0401
#define HW_MOD_FTM	0x0402
#define HW_MOD_FTH	0x0403
#define HW_MOD_CONNECT	0x0410
#define HW_MOD_OK	0x0411
#define HW_MOD_NOCARR	0x0412
#define HW_MOD_FCERROR	0x0413
#define HW_D_BLOCKED	0xFF20 
#define HW_D_NOBLOCKED	0xFF21 
#define HW_TESTLOOP	0xFF00
#define HW_FIRM_START	0xFF10
#define HW_FIRM_DATA	0xFF11
#define HW_FIRM_END	0xFF12
/* TOUCH TONE IS 0x2010 - 0x201F := "0"..."9", "A","B","C","D","*","#" */
#define TOUCH_TONE_VAL	0x2010
#define TOUCH_TONE_MASK 0x000F


/* layer 1 */
#define PH_ACTIVATE	0x010100
#define PH_DEACTIVATE	0x010000
#define PH_DATA		0x110200
#define MPH_DEACTIVATE	0x011000
#define MPH_ACTIVATE	0x011100
#define MPH_INFORMATION	0x012000
 
/* layer 2 */
#define DL_ESTABLISH	0x020100
#define DL_RELEASE	0x020000
#define DL_DATA		0x120200
#define DL_UNITDATA	0x120300
#define MDL_UNITDATA	0x121200
#define MDL_ASSIGN	0x022100
#define MDL_REMOVE	0x022000
#define MDL_ERROR	0x023000
#define MDL_INFORMATION	0x024000
#define MDL_STATUS	0x028100
#define MDL_FINDTEI	0x028200

/* layer 3 */
#define CC_ALERTING		0x030100
#define CC_PROCEEDING		0x030200
#define CC_PROGRESS		0x030300
#define CC_SETUP		0x030500
#define CC_CONNECT		0x030700
#define CC_SETUP_ACKNOWLEDGE	0x030d00
#define CC_CONNECT_ACKNOWLEDGE	0x030f00
#define CC_USER_INFORMATION	0x032000
#define CC_SUSPEND_REJECT	0x032100
#define CC_RESUME_REJECT	0x032200
#define CC_SUSPEND		0x032500
#define CC_RESUME		0x032600
#define CC_SUSPEND_ACKNOWLEDGE	0x032d00
#define CC_RESUME_ACKNOWLEDGE	0x032e00
#define CC_DISCONNECT		0x034500
#define CC_RESTART		0x034600
#define CC_RELEASE		0x034d00
#define CC_RELEASE_COMPLETE	0x035a00
#define CC_FACILITY		0x036200
#define CC_NOTIFY		0x036e00
#define CC_STATUS_ENQUIRY	0x037500
#define CC_INFORMATION		0x037b00
#define CC_STATUS		0x037d00

#define CC_NEW_CR		0x03f000
#define CC_RELEASE_CR		0x03f100
#define CC_TIMEOUT		0x03ff00

#define CC_B3_DATA		0x138600

#define LAYER_MASK	0x0F0000
#define COMMAND_MASK	0x00FF00
#define SUBCOMMAND_MASK	0x0000FF
#define DATA_COMMAND	0x100000
#define CMD_IS_DATA(p)	(p & DATA_COMMAND)

/* short cuts layer 1 */
#define PH_ACTIVATE_REQ		(PH_ACTIVATE | REQUEST)
#define PH_ACTIVATE_IND		(PH_ACTIVATE | INDICATION)
#define PH_DEACTIVATE_IND	(PH_DEACTIVATE | INDICATION)
#define PH_DATA_REQ		(PH_DATA | REQUEST)
#define PH_DATA_IND		(PH_DATA | INDICATION)
#define PH_DATA_CNF		(PH_DATA | CONFIRM)
#define PH_DATA_RSP		(PH_DATA | RESPONSE)
#define MPH_ACTIVATE_REQ	(MPH_ACTIVATE | REQUEST)
#define MPH_DEACTIVATE_REQ	(MPH_DEACTIVATE | REQUEST)
#define MPH_INFORMATION_IND	(MPH_INFORMATION | INDICATION)

/* short cuts layer 2 */
#define DL_ESTABLISH_REQ	(DL_ESTABLISH | REQUEST)
#define DL_ESTABLISH_IND	(DL_ESTABLISH | INDICATION)
#define DL_ESTABLISH_CNF	(DL_ESTABLISH | CONFIRM)
#define DL_RELEASE_REQ		(DL_RELEASE | REQUEST)
#define DL_RELEASE_IND		(DL_RELEASE | INDICATION)
#define DL_RELEASE_CNF		(DL_RELEASE | CONFIRM)
#define DL_DATA_REQ		(DL_DATA | REQUEST)
#define DL_DATA_IND		(DL_DATA | INDICATION)
#define DL_UNITDATA_REQ		(DL_UNITDATA | REQUEST)
#define DL_UNITDATA_IND		(DL_UNITDATA | INDICATION)
#define MDL_ASSIGN_REQ		(MDL_ASSIGN | REQUEST)
#define MDL_ASSIGN_IND		(MDL_ASSIGN | INDICATION)
#define MDL_REMOVE_REQ		(MDL_REMOVE | REQUEST)
#define MDL_ERROR_IND		(MDL_ERROR | INDICATION)
#define MDL_ERROR_RSP		(MDL_ERROR | RESPONSE)
#define MDL_INFORMATION_IND	(MDL_INFORMATION | INDICATION)

/* protocol id */
#define ISDN_PID_NONE		0
#define ISDN_PID_ANY		0xffffffff
#define ISDN_PID_L0_TE_S0	0x00000001
#define ISDN_PID_L0_NT_S0	0x00000101
#define ISDN_PID_L0_TE_U	0x00000002
#define ISDN_PID_L0_NT_U	0x00000102
#define ISDN_PID_L1_TE_S0	0x01000001
#define ISDN_PID_L1_NT_S0	0x01000101
#define ISDN_PID_L1_TE_U	0x01000002
#define ISDN_PID_L1_NT_U	0x01000102
#define ISDN_PID_L1_B_64HDLC	0x41000001
#define ISDN_PID_L1_B_64TRANS	0x41000002
#define ISDN_PID_L1_B_TRANS_TTS	0x41100002
#define ISDN_PID_L1_B_TRANS_TTR	0x41200002
#define ISDN_PID_L1_B_TRANS_TT	0x41300002
#define ISDN_PID_L1_B_V32	0x41000100
#define ISDN_PID_L1_B_FAX	0x41000010
#define ISDN_PID_L2_LAPD	0x02000001
#define ISDN_PID_L2_B_X75SLP	0x42000001
#define ISDN_PID_L2_B_TRANS	0x42000002
#define ISDN_PID_L3_B_TRANS	0x43000001
#define ISDN_PID_L3_DSS1USER	0x03000001
#define ISDN_PID_L4_CAPI20	0x04000001
#define ISDN_PID_L4_B_CAPI20	0x44000001

#define ISDN_PID_BCHANNEL_BIT	0x40000000
#define ISDN_PID_LAYER_MASK	0x0f000000
#define ISDN_PID_LAYER(n)	(n<<24)

#define MAX_LAYER_NR	7
#define ISDN_LAYER(n)	(1<<n)
#define LAYER_OUTRANGE(layer)	((layer<0) || (layer>MAX_LAYER_NR))
#define HISAX_MAX_IDLEN	16

#define IADDR_BIT	0x10000000
#define IF_NOACTIV	0x00000000
#define IF_DOWN		0x01000000
#define IF_UP		0x02000000
#define IF_CHAIN	0x04000000
#define IF_HANDSHAKE	0x08000000
#define IF_TYPEMASK	0x07000000
#define IF_ADDRMASK	0x00FFFFFF
#define IF_IADDRMASK	0xF0FFFFFF
#define IF_LAYERMASK	0x00F00000
#define IF_TYPE(i)	((i)->stat & IF_TYPEMASK)

#define DINFO_SKB	-1

#define DUMMY_CR_FLAG	0x7FFFFF00
#define CONTROLER_MASK	0x000000FF


/* interface extentions */
#define EXT_LAYER_SPLIT
/* special packet type */
#define PACKET_NOACK	250

/* limits for buffers */

#define MAX_PHONE_DIGIT		31
#define MAX_DFRAME_LEN		260
#define MAX_DATA_SIZE		2048
#define MAX_DATA_MEM		2080
#define MAX_HEADER_LEN		4
#define UPLINK_HEADER_SPACE	22

/* structure for information exchange between layer/entity boundaries */

typedef struct _iframe {
	u_int	addr;
	u_int	prim;
	int	dinfo;
	int	len;
	union {
		u_char	b[4];
		void	*p;
		int	i;
	} data;
} iframe_t;


#define STATUS_INFO_L1	1
#define STATUS_INFO_L2	2

typedef struct _status_info {
	int	len;
	int	typ;
	u_char	info[120];
} status_info_t;

typedef struct _logdata {
	char    *head;
	char	*fmt;
	va_list args;	
} logdata_t;

typedef struct _moditem {
	char	*name;
	int	protocol;
} moditem_t;

typedef struct _hisax_pid {
	int	protocol[MAX_LAYER_NR +1];
	void	*param[MAX_LAYER_NR +1];
	__u16	global;
	int	layermask;
} hisax_pid_t;

typedef struct _layer_info {
	char		name[HISAX_MAX_IDLEN];
	int		object_id;
	int		extentions;
	int		id;
	int		st;
	hisax_pid_t	pid;
} layer_info_t;


typedef struct _interface_info {
	int		extentions;
	int		owner;
	int		peer;
	int		stat;
} interface_info_t;


/* l3 pointer arrays */

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
	u_char *FACILITY;
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



#ifdef __KERNEL__

typedef struct _hisaxobject {
	struct _hisaxobject	*prev;
	struct _hisaxobject	*next;
	char			*name;
	int			id;
	int			refcnt;
	hisax_pid_t		DPROTO;
	hisax_pid_t		BPROTO;
	int     (*own_ctrl)(void *, u_int, void *);
	int     (*ctrl)(void *, u_int, void *);
	void	*ilist;
} hisaxobject_t;

typedef struct _hisaxif {
	struct _hisaxif		*prev;
	struct _hisaxif		*next;
	int			extentions;
	int			stat;
	struct _hisaxstack	*st;
	struct _hisaxinstance	*owner;
	struct _hisaxinstance	*peer;
	int			(*func)(struct _hisaxif *, u_int, int, int, void *);
	void			*fdata;
} hisaxif_t;

typedef struct _hisaxinstance {
	struct _hisaxinstance	*prev;
	struct _hisaxinstance	*next;
	char			name[HISAX_MAX_IDLEN];
	int			extentions;
	u_int			id;
	hisax_pid_t		pid;
	struct _hisaxstack	*st;
	hisaxobject_t		*obj;
	void			*data;
	hisaxif_t		up;
	hisaxif_t		down;
	void			(*lock)(void *);
	void			(*unlock)(void *);
} hisaxinstance_t;

typedef struct _hisaxlayer {
	struct _hisaxlayer	*prev;
	struct _hisaxlayer	*next;
	hisaxinstance_t		*inst;
} hisaxlayer_t;

typedef struct _hisaxstack {
	struct _hisaxstack	*prev;
	struct _hisaxstack	*next;
	u_int			id;
	hisax_pid_t		pid;
	hisaxlayer_t		*lstack;
	hisaxinstance_t		*mgr;
	struct _hisaxstack	*child;
} hisaxstack_t;

/* common helper functions */
extern int bprotocol2pid(void *, hisax_pid_t *);
extern int get_protocol(hisaxstack_t *, int);
extern int HasProtocol(hisaxobject_t *, int);
extern int SetHandledPID(hisaxobject_t *, hisax_pid_t *);
extern void RemoveUsedPID(hisax_pid_t *, hisax_pid_t *);
extern int SetIF(hisaxinstance_t *, hisaxif_t *, u_int, void *, void *, void *);
extern int ConnectIF(hisaxinstance_t *, hisaxinstance_t *);
extern int DisConnectIF(hisaxinstance_t *, hisaxif_t *);

/* global exported functions */

extern int HiSax_register(hisaxobject_t *obj);
extern int HiSax_unregister(hisaxobject_t *obj);

#endif /* __KERNEL__ */
#endif /* HISAXIF_H */

/* $Id: mISDNif.h,v 1.32 2004/07/08 00:58:20 keil Exp $
 *
 */

#ifndef mISDNIF_H
#define mISDNIF_H

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
 
/*
 * ABI Version 32 bit
 *
 * <16 bit> Major version
 *		- changed if any interface become backwards incompatible
 *
 * <16 bit> Minor version
 *              - changed if any interface is extended but backwards compatible
 *
 */
#define	MISDN_MAJOR_VERSION	1
#define	MISDN_MINOR_VERSION	0
#define	MISDN_VERSION		((MISDN_MAJOR_VERSION<<16) | MISDN_MINOR_VERSION)

#define MISDN_REVISION		"$Revision: 1.32 $"
#define MISDN_DATE		"$Date: 2004/07/08 00:58:20 $"

/* SUBCOMMANDS */
#define REQUEST		0x80
#define CONFIRM		0x81
#define INDICATION	0x82
#define RESPONSE	0x83
#define SUB_ERROR	0xff

/* management */
#define MGR_FUNCTION	0x0f0000
#define MGR_GETOBJECT	0x0f0100
#define MGR_NEWOBJECT	0x0f0200
#define MGR_DELOBJECT	0x0f0300
#define MGR_NEWENTITY	0x0f0600
#define MGR_DELENTITY	0x0f0700
#define MGR_GETSTACK	0x0f1100
#define MGR_NEWSTACK	0x0f1200
#define MGR_DELSTACK	0x0f1300
#define MGR_SETSTACK	0x0f1400
#define MGR_CLEARSTACK	0x0f1500
#define MGR_REGLAYER	0x0f1600
#define MGR_UNREGLAYER	0x0f1700
#define MGR_SELCHANNEL	0x0f1800
#define MGR_SETSTACK_NW	0x0f1900
#define MGR_ADDSTPARA	0x0f1A00
#define MGR_CLRSTPARA	0x0f1B00
#define MGR_GETLAYER	0x0f2100
#define MGR_GETLAYERID	0x0f2200
#define MGR_NEWLAYER	0x0f2300
#define MGR_DELLAYER	0x0f2400
#define MGR_CLONELAYER	0x0f2500
#define MGR_GETIF	0x0f3100
#define MGR_CONNECT	0x0f3200
#define MGR_DISCONNECT	0x0f3300
#define MGR_SETIF	0x0f3400
#define MGR_ADDIF	0x0f3500
#define MGR_QUEUEIF	0x0f3600
#define MGR_CTRLREADY	0x0f4100
#define MGR_RELEASE	0x0f4500
#define MGR_GETDEVICE	0x0f5100
#define MGR_DELDEVICE	0x0f5200
#define MGR_SETDEVOPT	0x0f5300
#define MGR_GETDEVOPT	0x0f5400
#define MGR_INITTIMER	0x0f8100
#define MGR_ADDTIMER	0x0f8200
#define MGR_DELTIMER	0x0f8300
#define MGR_REMOVETIMER	0x0f8400
#define MGR_TIMER	0x0f8800
#define MGR_CONTROL	0x0fe100
#define MGR_STATUS	0x0fe200
#define MGR_HASPROTOCOL 0x0fe300
#define MGR_EVALSTACK	0x0fe400
#define MGR_GLOBALOPT	0x0fe500
#define MGR_LOADFIRM	0x0ff000
#define MGR_LOGDATA	0x0ff100
#define MGR_DEBUGDATA	0x0ff200
#define MGR_VERSION	0x0fff00

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
#define HW_MOD_FTS	0x0404
#define HW_MOD_CONNECT	0x0410
#define HW_MOD_OK	0x0411
#define HW_MOD_NOCARR	0x0412
#define HW_MOD_FCERROR	0x0413
#define HW_MOD_READY	0x0414
#define HW_MOD_LASTDATA	0x0415
#define HW_MOD_SILENCE	0x0416
#define HW_FEATURES	0x04ff
#define HW_HFC_COEFF	0x0500
#define HW_LOS		0x0501
#define HW_LOS_OFF	0x0502
#define HW_AIS		0x0503
#define HW_AIS_OFF	0x0504
#define HW_SLIP_TX	0x0505
#define HW_SLIP_RX	0x0506
#define HW_PCM_CONN	0x0580
#define HW_PCM_DISC	0x0581
#define HW_CONF_JOIN	0x0582
#define HW_CONF_SPLIT	0x0583
#define HW_RECEIVE_OFF	0x0584
#define HW_RECEIVE_ON	0x0585
#define HW_SPL_LOOP_ON	0x0586
#define HW_SPL_LOOP_OFF	0x0587
#define HW_TESTLOOP	0xFF00
#define HW_FIRM_START	0xFF10
#define HW_FIRM_DATA	0xFF11
#define HW_FIRM_END	0xFF12
#define HW_D_BLOCKED	0xFF20 
#define HW_D_NOBLOCKED	0xFF21 
#define HW_TESTRX_RAW	0xFF40
#define HW_TESTRX_HDLC	0xFF41
#define HW_TESTRX_OFF	0xFF4f
/* TOUCH TONE IS 0x20XX  XX "0"..."9", "A","B","C","D","*","#" */
#define DTMF_TONE_VAL	0x2000
#define DTMF_TONE_MASK	0x007F
#define DTMF_TONE_START	0x2100
#define DTMF_TONE_STOP	0x2200
#define CMX_CONF_JOIN	0x2300
#define CMX_CONF_SPLIT	0x2301
#define CMX_ECHO_ON	0x2302
#define CMX_ECHO_OFF	0x2303
#define CMX_RECEIVE_OFF	0x2304
#define CMX_RECEIVE_ON	0x2305
#define CMX_MIX_ON	0x2306
#define CMX_MIX_OFF	0x2307
#define TONE_PATT_ON	0x2310
#define TONE_PATT_OFF	0x2311
#define VOL_CHANGE_TX	0x2312
#define VOL_CHANGE_RX	0x2313
#define BF_ENABLE_KEY	0x2314
#define BF_DISABLE	0x2315
#define BF_ACCEPT	0x2316
#define BF_REJECT	0x2317
#define HW_POTS_ON		0x1001
#define HW_POTS_OFF		0x1002
#define HW_POTS_SETMICVOL	0x1100
#define HW_POTS_SETSPKVOL	0x1101
#define HW_POTS_GETMICVOL	0x1110
#define HW_POTS_GETSPKVOL	0x1111

/* TONE_PATT_ON parameter */
#define TONE_OFF			0x0000
#define TONE_GERMAN_DIALTONE		0x0001
#define TONE_GERMAN_OLDDIALTONE		0x0002
#define TONE_AMERICAN_DIALTONE		0x0003
#define TONE_GERMAN_DIALPBX		0x0004
#define TONE_GERMAN_OLDDIALPBX		0x0005
#define TONE_AMERICAN_DIALPBX		0x0006
#define TONE_GERMAN_RINGING		0x0007
#define TONE_GERMAN_OLDRINGING		0x0008
#define TONE_AMERICAN_RINGPBX		0x000b
#define TONE_GERMAN_RINGPBX		0x000c
#define TONE_GERMAN_OLDRINGPBX		0x000d
#define TONE_AMERICAN_RINGING		0x000e
#define TONE_GERMAN_BUSY		0x000f
#define TONE_GERMAN_OLDBUSY		0x0010
#define TONE_AMERICAN_BUSY		0x0011
#define TONE_GERMAN_HANGUP		0x0012
#define TONE_GERMAN_OLDHANGUP		0x0013
#define TONE_AMERICAN_HANGUP		0x0014
#define TONE_SPECIAL_INFO		0x0015
#define TONE_GERMAN_GASSENBESETZT	0x0016
#define TONE_GERMAN_AUFSCHALTTON	0x0016

#define GLOBALOPT_INTERNAL_CTRL		0x0001
#define GLOBALOPT_EXTERNAL_EQUIPMENT	0x0002
#define GLOBALOPT_HANDSET		0x0004
#define GLOBALOPT_DTMF			0x0008
#define GLOBALOPT_SUPPLEMENTARY_SERVICE	0x0010
#define GLOBALOPT_CHANNEL_ALLOCATION	0x0020
#define GLOBALOPT_PARAMETER_B_CHANNEL	0x0040
#define GLOBALOPT_LINE_INTERCONNECT	0x0080


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
#define CC_HOLD			0x032400
#define CC_SUSPEND		0x032500
#define CC_RESUME		0x032600
#define CC_HOLD_ACKNOWLEDGE	0x032800
#define CC_SUSPEND_ACKNOWLEDGE	0x032d00
#define CC_RESUME_ACKNOWLEDGE	0x032e00
#define CC_HOLD_REJECT		0x033000
#define CC_RETRIEVE		0x033100
#define CC_RETRIEVE_ACKNOWLEDGE	0x033300
#define CC_RETRIEVE_REJECT	0x033700
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
#define ISDN_PID_NONE			0
#define ISDN_PID_ANY			0xffffffff
#define ISDN_PID_L0_TE_S0		0x00000001
#define ISDN_PID_L0_NT_S0		0x00000100
#define ISDN_PID_L0_TE_U		0x00000002
#define ISDN_PID_L0_NT_U		0x00000200
#define ISDN_PID_L0_TE_UP2		0x00000004
#define ISDN_PID_L0_NT_UP2		0x00000400
#define ISDN_PID_L0_TE_E1		0x00000008
#define ISDN_PID_L0_NT_E1		0x00000800
#define ISDN_PID_L1_TE_S0		0x01000001
#define ISDN_PID_L1_NT_S0		0x01000100
#define ISDN_PID_L1_TE_U		0x01000002
#define ISDN_PID_L1_NT_U		0x01000200
#define ISDN_PID_L1_TE_UP2		0x01000004
#define ISDN_PID_L1_NT_UP2		0x01000400
#define ISDN_PID_L1_TE_E1		0x01000008
#define ISDN_PID_L1_NT_E1		0x01000800
/* Bit 15-0  reserved for CAPI defined protocols */
#define ISDN_PID_L1_B_64HDLC		0x41000001
#define ISDN_PID_L1_B_64TRANS		0x41000002
#define ISDN_PID_L1_B_V110_ASYNC	0x41000004
#define ISDN_PID_L1_B_V110_HDLC		0x41000008
#define ISDN_PID_L1_B_T30FAX		0x41000010
#define ISDN_PID_L1_B_64HDLC_INV	0x41000020
#define ISDN_PID_L1_B_56TRANS		0x41000040
#define ISDN_PID_L1_B_MODEM_ALL		0x41000080
#define ISDN_PID_L1_B_MODEM_ASYNC	0x41000100
#define ISDN_PID_L1_B_MODEM_HDLC	0x41000200
#define ISDN_PID_L2_LAPD		0x02000001
#define ISDN_PID_L2_LAPD_NET		0x02000002
/* Bit 15-0  reserved for CAPI defined protocols */
#define ISDN_PID_L2_B_X75SLP		0x42000001
#define ISDN_PID_L2_B_TRANS		0x42000002
#define ISDN_PID_L2_B_TRANSDTMF		0x42300002
#define ISDN_PID_L2_B_RAWDEV		0x42400002
#define ISDN_PID_L2_B_SDLC		0x42000004
#define ISDN_PID_L2_B_LAPD		0x42000008
#define ISDN_PID_L2_B_T30		0x42000010
#define ISDN_PID_L2_B_PPP		0x42000020
#define ISDN_PID_L2_B_TRANSNOERR	0x42000040
#define ISDN_PID_L2_B_MODEM		0x42000080
#define ISDN_PID_L2_B_X75SLPV42BIS	0x42000100
#define ISDN_PID_L2_B_V120ASYNC		0x42000200
#define ISDN_PID_L2_B_V120ASYNCV42BIS	0x42000400
#define ISDN_PID_L2_B_V120TRANS		0x42000800
#define ISDN_PID_L2_B_LAPDFREESAPI	0x42001000
#define ISDN_PID_L3_B_TRANS		0x43000001
#define ISDN_PID_L3_B_T90NL		0x43000002
#define ISDN_PID_L3_B_X25DTE		0x43000004
#define ISDN_PID_L3_B_X25DCE		0x43000008
#define ISDN_PID_L3_B_T30		0x43000010
#define ISDN_PID_L3_B_T30EXT		0x43000020
#define ISDN_PID_L3_B_MODEM		0x43000080
/* Bit 15-0  reserved for CAPI defined protocols */
#define ISDN_PID_L3_B_DSP		0x43010000
#define ISDN_PID_L3_DSS1USER		0x03000001
#define ISDN_PID_L3_DSS1NET		0x03000100
#define ISDN_PID_L4_CAPI20		0x04000001
#define ISDN_PID_L4_B_CAPI20		0x44000001

#define ISDN_PID_BCHANNEL_BIT		0x40000000
#define ISDN_PID_LAYER_MASK		0x0f000000
#define ISDN_PID_LAYER(n)		(n<<24)
#define ISDN_PID_FEATURE_MASK		0x00F00000
#define ISDN_PID_IDX_MAX		23

#define ISDN_PID_L2_DF_PTP		0x00100000
#define ISDN_PID_L2_DF_MULT_TEI		0x00200000
#define	ISDN_PID_L3_DF_PTP		0x00100000
#define ISDN_PID_L3_DF_EXTCID		0x00200000
#define ISDN_PID_L3_DF_CRLEN2		0x00400000

#define mISDN_CORE_DEVICE	0
#define mISDN_RAW_DEVICE	128

#define FLG_mISDNPORT_BUSY	1
#define FLG_mISDNPORT_ENABLED	2
#define FLG_mISDNPORT_BLOCK	3
#define FLG_mISDNPORT_OPEN	4
#define FLG_mISDNPORT_ONEFRAME	5


/*
 * A "ENTITY" is a instance which assign id's with a none local
 * scope. A id has a local part (a range of ids which the instance
 * maintains) and the global ENTITY ID.
 * A instance which wan't to assign IDs have to request a unique
 * ENTITY ID with MGR_NEWENTITY | REQUEST.
 * If the instance is deleted or don't need this feature anymore
 * it has to delete the ENTITY with MGR_DELENTITY | REQUEST.
 * ENTITY ID 0xFFFF is a special one.
 * One example for using this is the L3/L4 process ID.
 */

#define MISDN_MAX_ENTITY	2048
#define MISDN_ENTITY_NONE	0x0000ffff
#define MISDN_ID_ENTITYMASK	0xffff0000
#define MISDN_ID_LOCALMASK	0x0000FFFF
#define MISDN_ID_ANY		0xffffffff
#define MISDN_ID_NONE		0xfffffffe
#define MISDN_ID_DUMMY		0xffff0001
#define MISDN_ID_GLOBAL		0xffff0002

#define MAX_LAYER_NR	7
#define ISDN_LAYER(n)	(1<<n)
#define LAYER_OUTRANGE(layer)	((layer<0) || (layer>MAX_LAYER_NR))
#define mISDN_MAX_IDLEN	16

#define IF_NOACTIV	0x00000000
#define IF_DOWN		0x01000000
#define IF_UP		0x02000000
#define IF_CHAIN	0x04000000
#define IF_HANDSHAKE	0x08000000
#define IF_TYPEMASK	0x07000000
#define IF_ADDRMASK	0xF0FFFFFF
#define IF_IADDRMASK	0xF0FFFFFF
#define IF_CONTRMASK	0x000000FF
#define IF_CHILDMASK	0x1000FF00
#define IF_CLONEMASK	0x2000FF00
#define IF_INSTMASK	0x400F0000
#define IF_LAYERMASK	0x00F00000
#define IF_TYPE(i)	((i)->stat & IF_TYPEMASK)
#define CHILD_ID_INC	0x00000100
#define CHILD_ID_MAX	0x1000FF00
#define CLONE_ID_INC	0x00000100
#define CLONE_ID_MAX	0x2000FF00
#define INST_ID_INC	0x00010000
#define INST_ID_MAX	0x400F0000
#define FLG_CHILD_STACK	0x10000000
#define FLG_CLONE_STACK	0x20000000
#define FLG_INSTANCE	0x40000000

#define DUMMY_CR_FLAG	0x7FFFFF00
#define CONTROLER_MASK	0x000000FF

/* stack channel values */
#define CHANNEL_NUMBER	0x000000FF
#define CHANNEL_RXSLOT	0x0000FF00
#define CHANNEL_TXSLOT	0x00FF0000
#define CHANNEL_EXTINFO	0xFF000000
#define CHANNEL_NR_D	0x00000000
#define CHANNEL_NR_B1	0x00000001
#define CHANNEL_NR_B2	0x00000002
#define CHANNEL_EXT_PCM	0x01000000
#define CHANNEL_EXT_REV	0x02000000

/* interface extentions */
#define EXT_STACK_CLONE 0x00000001
#define EXT_INST_CLONE	0x00000100
#define EXT_INST_MGR	0x00000200
#define EXT_INST_MIDDLE	0x00000400
#define EXT_IF_CHAIN	0x00010000
#define EXT_IF_EXCLUSIV	0x00020000
#define EXT_IF_CREATE	0x00040000
#define EXT_IF_SPLIT	0x00080000


/* special packet type */
#define PACKET_NOACK	250

/* limits for buffers */

#define MAX_PHONE_DIGIT		31
#define MAX_DFRAME_LEN		260
#define MAX_DATA_SIZE		2048
#define MAX_DATA_MEM		2080
#define MAX_HEADER_LEN		4
#define	DEFAULT_PORT_QUEUELEN	256
#define PORT_SKB_RESERVE	L3_EXTRA_SIZE
#define PORT_SKB_MINIMUM	128

/* structure for information exchange between layer/entity boundaries */

#define STATUS_INFO_L1	1
#define STATUS_INFO_L2	2

typedef struct _mISDN_head {
	u_int	addr __attribute__((packed));
	u_int	prim __attribute__((packed));
	int	dinfo __attribute__((packed));
	int	len __attribute__((packed));
} mISDN_head_t;

#define mISDN_HEADER_LEN	sizeof(mISDN_head_t)

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

typedef struct _mISDN_pid {
	int	protocol[MAX_LAYER_NR +1];
	u_char	*param[MAX_LAYER_NR +1];
	__u16	global;
	int	layermask;
	int	maxplen;
	u_char	*pbuf;
} mISDN_pid_t;

typedef struct _mISDN_stPara {
	int	maxdatalen;
	int	up_headerlen;
	int	down_headerlen;
} mISDN_stPara_t;

typedef struct _stack_info {
	u_int		id;
	mISDN_pid_t	pid;
	mISDN_stPara_t	para;
	u_int		extentions;
	u_int		mgr;
	int		instcnt;
	int		inst[MAX_LAYER_NR +1];
	int		childcnt;
	u_int		child[2]; /* this is correct handled for PRI see get_stack_info() */
} stack_info_t;

typedef struct _layer_info {
	char		name[mISDN_MAX_IDLEN];
	int		object_id;
	int		extentions;
	u_int		id;
	u_int		st;
	mISDN_pid_t	pid;
} layer_info_t;


typedef struct _interface_info {
	int		extentions;
	u_int		owner;
	u_int		peer;
	int		stat;
} interface_info_t;

typedef struct _channel_info {
	u_int		channel;
	union {
		u_int	id;
		void	*p;
	} st;
} channel_info_t;

/* l3 pointer arrays */

typedef struct _Q931_info {
	u_char	type __attribute__((packed));
	u_char	crlen __attribute__((packed));
	u16	cr __attribute__((packed));
	u16	bearer_capability __attribute__((packed));
	u16	cause __attribute__((packed));
	u16	call_id __attribute__((packed));
	u16	call_state __attribute__((packed));
	u16	channel_id __attribute__((packed));
	u16	facility __attribute__((packed));
	u16	progress __attribute__((packed));
	u16	net_fac __attribute__((packed));
	u16	notify __attribute__((packed));
	u16	display __attribute__((packed));
	u16	date __attribute__((packed));
	u16	keypad __attribute__((packed));
	u16	signal __attribute__((packed));
	u16	info_rate __attribute__((packed));
	u16	end2end_transit __attribute__((packed));
	u16	transit_delay_sel __attribute__((packed));
	u16	pktl_bin_para __attribute__((packed));
	u16	pktl_window __attribute__((packed));
	u16	pkt_size __attribute__((packed));
	u16	closed_userg __attribute__((packed));
	u16	connected_nr __attribute__((packed));
	u16	connected_sub __attribute__((packed));
	u16	calling_nr __attribute__((packed));
	u16	calling_sub __attribute__((packed));
	u16	called_nr __attribute__((packed));
	u16	called_sub __attribute__((packed));
	u16	redirect_nr __attribute__((packed));
	u16	transit_net_sel __attribute__((packed));
	u16	restart_ind __attribute__((packed));
	u16	llc __attribute__((packed));
	u16	hlc __attribute__((packed));
	u16	useruser __attribute__((packed));
	u16	more_data __attribute__((packed));
	u16	sending_complete __attribute__((packed));
	u16	congestion_level __attribute__((packed));
	u16	fill1 __attribute__((packed));
} Q931_info_t;

#define L3_EXTRA_SIZE	sizeof(Q931_info_t)

#ifdef __KERNEL__
#include <linux/isdn_compat.h>
#include <linux/list.h>
#include <linux/skbuff.h>

typedef struct _mISDNobject	mISDNobject_t;
typedef struct _mISDNinstance	mISDNinstance_t;
typedef struct _mISDNlayer	mISDNlayer_t;
typedef struct _mISDNstack	mISDNstack_t;
typedef struct _mISDNport	mISDNport_t;
typedef struct _mISDNdevice	mISDNdevice_t;
typedef struct _mISDNif		mISDNif_t;
typedef int	(ctrl_func_t)(void *, u_int, void *);
typedef int	(if_func_t)(struct _mISDNif *, struct sk_buff *);
typedef int	(lock_func_t)(void *, int);
typedef void	(unlock_func_t)(void *);

#define mISDN_HEAD_P(s)		((mISDN_head_t *)&s->cb[0])
#define mISDN_HEAD_PRIM(s)	((mISDN_head_t *)&s->cb[0])->prim
#define mISDN_HEAD_DINFO(s)	((mISDN_head_t *)&s->cb[0])->dinfo

typedef struct _mISDN_headext {
	u_int	addr __attribute__((packed));
	u_int	prim __attribute__((packed));
	int	dinfo  __attribute__((packed));
	void	*data[3];
	union {
		ctrl_func_t	*ctrl;
		if_func_t	*iff;
		void		*func;
	} func;
} mISDN_headext_t;

#define mISDN_HEADEXT_P(s) ((mISDN_headext_t *)&s->cb[0])

/* Basic struct of a mISDN component */
struct _mISDNobject {
	struct list_head	list;
	char			*name;
	int			id;
	int			refcnt;
	mISDN_pid_t		DPROTO;
	mISDN_pid_t		BPROTO;
	ctrl_func_t		*own_ctrl;
	ctrl_func_t		*ctrl;
	struct list_head	ilist;
	struct module		*owner;
};

/* the interface between two mISDNinstances */
struct _mISDNif {
	if_func_t		*func;
	void			*fdata;
	mISDNif_t		*clone;
	mISDNif_t		*predecessor;
	int			extentions;
	int			stat;
	mISDNstack_t		*st;
	mISDNinstance_t		*owner;
	mISDNinstance_t		*peer;
};

/* a instance of a mISDNobject */
struct _mISDNinstance {
	struct list_head	list;
	char			name[mISDN_MAX_IDLEN];
	int			extentions;
	u_int			id;
	mISDN_pid_t		pid;
	mISDNstack_t		*st;
	mISDNobject_t		*obj;
	void			*data;
	mISDNif_t		up;
	mISDNif_t		down;
	lock_func_t		*lock;
	unlock_func_t		*unlock;
};

/* a list of parallel (horizontal) mISDNinstances in the same layer
 * normally here is only one instance per layer only if the information
 * will be splitted here are more instances */
struct _mISDNlayer {
	struct list_head	list;
	mISDNinstance_t		*inst;
};

/* the STACK; a (vertical) chain of layers */
 
struct _mISDNstack {
	struct list_head	list;
	u_int			id;
	u_int			extentions;
	mISDN_pid_t		pid;
	mISDN_stPara_t		para;
	struct list_head	layerlist;
	mISDNinstance_t		*mgr;
	struct list_head	childlist;
};

/* lowlevel read/write struct for the mISDNdevice */
struct _mISDNport {
	wait_queue_head_t	procq;
	spinlock_t		lock;
	mISDNif_t		pif;
	u_long			Flag;
	struct sk_buff_head	queue;
	u_int			maxqlen;
};

/* the user interface to handle /dev/mISDN */
struct _mISDNdevice {
	struct list_head	list;
	int			minor;
	struct semaphore	io_sema;
	int			open_mode;
	mISDNport_t		rport;
	mISDNport_t		wport;
	struct list_head	layerlist;
	struct list_head	stacklist;
	struct list_head	timerlist;
	struct list_head	entitylist;
};

/* common helper functions */
extern int	mISDN_bprotocol2pid(void *, mISDN_pid_t *);
extern int	mISDN_SetIF(mISDNinstance_t *, mISDNif_t *, u_int, void *, void *, void *);
extern int	mISDN_ConnectIF(mISDNinstance_t *, mISDNinstance_t *);
extern int	mISDN_DisConnectIF(mISDNinstance_t *, mISDNif_t *);

/* global register/unregister functions */

extern int	mISDN_register(mISDNobject_t *obj);
extern int	mISDN_unregister(mISDNobject_t *obj);

#endif /* __KERNEL__ */
#endif /* mISDNIF_H */

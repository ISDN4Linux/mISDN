/* $Id: mISDNif.h,v 0.1 2001/02/11 22:46:19 kkeil Exp $
 *
 */
#include <linux/types.h>

/* primitives for information exchange
 * generell format
 * <8 bit reserved>
 * <4 bit flags>
 * <4 bit layer>
 * <8 bit command>
 * <8 bit subcommand>
 *
 */

/* type */
#define REQUEST		0x80
#define CONFIRM		0x81
#define INDICATION	0x82
#define RESPONSE	0x83

/* management */
#define MGR_GETSTACK	0x0f0100
#define MGR_ADDSTACK	0x0f0200
#define MGR_DELSTACK	0x0f0300
#define MGR_GETLAYER	0x0f1100
#define MGR_ADDLAYER	0x0f1200
#define MGR_DELLAYER	0x0f1300
#define MGR_GETIF	0x0f2100
#define MGR_ADDIF	0x0f2200
#define MGR_DELIF	0x0f2300
#define MGR_SETIF	0x0f2400
#define MGR_RELEASE	0x0f0400
#define MGR_CONTROL	0x0fe100
#define MGR_STATUS	0x0fe200
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
#define ISDN_PID_L1_B_TRANS	0x01020001
#define ISDN_PID_L1_B_TRANS_TTS	0x01020011
#define ISDN_PID_L1_B_TRANS_TTR	0x01020021
#define ISDN_PID_L1_B_TRANS_TT	0x01020031
#define ISDN_PID_L1_B_V32	0x01020041
#define ISDN_PID_L1_B_FAX	0x01020051
#define ISDN_PID_L1_B_HDLC	0x01020002
#define ISDN_PID_L2_LAPD	0x02000001
#define ISDN_PID_L2_LAPB	0x02020001
#define ISDN_PID_L3_DSS1USER	0x03000001

#define ISDN_PID_BCHANNEL_BIT	0x00020000

#define MAX_LAYER	4
#define HISAX_MAX_IDLEN	16

#define IADDR_BIT	0x10000000
#define IF_NOACTIV	0x00000000
#define IF_DOWN		0x01000000
#define IF_UP		0x02000000
#define IF_LOG		0x04000000
#define IF_HANDSHAKE	0x08000000
#define IF_TYPEMASK	0x07000000
#define IF_ADDRMASK	0x00FFFFFF
#define IF_IADDRMASK	0xF0FFFFFF
#define IF_LAYERMASK	0x000F0000
#define IF_TYPE(i)	((i)->stat & IF_TYPEMASK)
#define DTYPE_SKB	-1

/* structure for information exchange between layer/entity boundaries */

typedef struct _iframe {
	u_int	addr;
	u_int	prim;
	u_int	nr;
	int	len;
	union {
		u_char	b[4];
		void	*p;
		int	i;
	} data;
} iframe_t;

typedef struct _logdata {
	char    *head;
	char	*fmt;
	va_list args;	
} logdata_t;

typedef struct _moditem {
	char	*name;
	int	layer;
	int	protocol;
} moditem_t;

typedef struct _bsetup {
	int	channel;
	int	flags;
	u_int	protocol[MAX_LAYER+1];
} bsetup_t;

typedef struct _hisaxobject {
	struct _hisaxobject *prev;
	struct _hisaxobject *next;
	char	*name;
	int	layer;
	int	refcnt;
	int     protcnt;
	int	*protocols;
	int     (*own_ctrl)(void *, u_int, void *);
	int     (*ctrl)(void *, u_int, void *);
} hisaxobject_t;

typedef struct _hisaxif {
	struct _hisaxif		*prev;
	struct _hisaxif		*next;
	int			layer;
	int			protocol;
	int			stat;
	struct _hisaxstack	*st;
	struct _hisaxinstance	*inst;
	int			(*func)(struct _hisaxif *, u_int, u_int, int, void *);
	void			*fdata;
} hisaxif_t;

typedef struct _hisaxinstance {
	struct _hisaxinstance	*prev;
	struct _hisaxinstance	*next;
	int			layer;
	int			protocol;
	char			id[HISAX_MAX_IDLEN];
	struct _hisaxstack	*st;
	hisaxobject_t		*obj;
	void			*data;
	hisaxif_t		up;
	hisaxif_t		down;
	void			(*lock)(void *);
	void			(*unlock)(void *);
} hisaxinstance_t;

typedef struct _hisaxstack {
	struct _hisaxstack	*prev;
	struct _hisaxstack	*next;
	int			id;
	int			protocols[MAX_LAYER+1];
	hisaxinstance_t		*inst[MAX_LAYER+1];
	hisaxinstance_t		*mgr;
	struct _hisaxstack	*child;
} hisaxstack_t;

int HiSax_register(hisaxobject_t *obj);
int HiSax_unregister(hisaxobject_t *obj);

/* $Id: isac.h,v 1.4 2004/01/26 22:21:30 keil Exp $
 *
 * isac.h   ISAC specific defines
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

/* privat isac data */

typedef struct isac_chip {
	u_char			*mon_tx;
	u_char			*mon_rx;
	int			mon_txp;
	int			mon_txc;
	int			mon_rxp;
	struct arcofi_msg	*arcofi_list;
	struct timer_list	arcofitimer;
	wait_queue_head_t	arcofi_wait;
	u_char			arcofi_bc;
	u_char			arcofi_state;
	u_char			mocr;
	u_char			adf2;
} isac_chip_t;

#define ISAC_TYPE_ISAC		0x0010
#define ISAC_TYPE_IPAC		0x0020
#define ISAC_TYPE_ISACSX	0x0040
#define ISAC_TYPE_IPACSX	0x0080
#define ISAC_TYPE_IOM1		0x0100
#define ISAC_TYPE_ARCOFI	0x1000


/* All Registers original Siemens Spec  */

#define ISAC_MASK 0x20
#define ISAC_ISTA 0x20
#define ISAC_STAR 0x21
#define ISAC_CMDR 0x21
#define ISAC_EXIR 0x24
#define ISAC_ADF2 0x39
#define ISAC_SPCR 0x30
#define ISAC_ADF1 0x38
#define ISAC_CIR0 0x31
#define ISAC_CIX0 0x31
#define ISAC_CIR1 0x33
#define ISAC_CIX1 0x33
#define ISAC_STCR 0x37
#define ISAC_MODE 0x22
#define ISAC_RSTA 0x27
#define ISAC_RBCL 0x25
#define ISAC_RBCH 0x2A
#define ISAC_TIMR 0x23
#define ISAC_SQXR 0x3b
#define ISAC_MOSR 0x3a
#define ISAC_MOCR 0x3a
#define ISAC_MOR0 0x32
#define ISAC_MOX0 0x32
#define ISAC_MOR1 0x34
#define ISAC_MOX1 0x34

#define ISAC_RBCH_XAC 0x80

#define ISAC_CMD_TIM	0x0
#define ISAC_CMD_RS	0x1
#define ISAC_CMD_SCZ	0x4
#define ISAC_CMD_SSZ	0x2
#define ISAC_CMD_AR8	0x8
#define ISAC_CMD_AR10	0x9
#define ISAC_CMD_ARL	0xA
#define ISAC_CMD_DUI	0xF

#define ISAC_IND_RS	0x1
#define ISAC_IND_PU	0x7
#define ISAC_IND_DR	0x0
#define ISAC_IND_SD	0x2
#define ISAC_IND_DIS	0x3
#define ISAC_IND_EI	0x6
#define ISAC_IND_RSY	0x4
#define ISAC_IND_ARD	0x8
#define ISAC_IND_TI	0xA
#define ISAC_IND_ATI	0xB
#define ISAC_IND_AI8	0xC
#define ISAC_IND_AI10	0xD
#define ISAC_IND_DID	0xF

/* the new ISACX */
#define ISACSX_MASK       0x60
#define ISACSX_ISTA       0x60
#define ISACSX_ISTA_ICD   0x01
#define ISACSX_ISTA_CIC   0x10

#define ISACSX_MASKD      0x20
#define ISACSX_ISTAD      0x20
#define ISACSX_ISTAD_XDU  0x04
#define ISACSX_ISTAD_XMR  0x08
#define ISACSX_ISTAD_XPR  0x10
#define ISACSX_ISTAD_RFO  0x20
#define ISACSX_ISTAD_RPF  0x40
#define ISACSX_ISTAD_RME  0x80

#define ISACSX_CMDRD      0x21
#define ISACSX_CMDRD_XRES 0x01
#define ISACSX_CMDRD_XME  0x02
#define ISACSX_CMDRD_XTF  0x08
#define ISACSX_CMDRD_RRES 0x40
#define ISACSX_CMDRD_RMC  0x80

#define ISACSX_MODED      0x22

#define ISACSX_RBCLD      0x26

#define ISACSX_RSTAD      0x28
#define ISACSX_RSTAD_RAB  0x10
#define ISACSX_RSTAD_CRC  0x20
#define ISACSX_RSTAD_RDO  0x40
#define ISACSX_RSTAD_VFR  0x80

#define ISACSX_CIR0       0x2e
#define ISACSX_CIR0_CIC0  0x08
#define ISACSX_CIX0       0x2e

#define ISACSX_TR_CONF0   0x30

#define ISACSX_TR_CONF2   0x32

/* interface for the isac module */

extern int mISDN_isac_init(dchannel_t *);
extern void mISDN_isac_free(dchannel_t *);

extern void mISDN_isac_interrupt(dchannel_t *, u_char);
extern void mISDN_clear_isac(dchannel_t *);
extern int mISDN_ISAC_l1hw(mISDNif_t *, struct sk_buff *);

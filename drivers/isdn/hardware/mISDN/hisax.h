/* $Id: hisax.h,v 0.2 2001/02/11 22:54:20 kkeil Exp $
 *
 *   Basic declarations, defines and prototypes
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/ioport.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/isdnif.h>
#include <linux/tty.h>
#include <linux/serial_reg.h>
#include <linux/hisaxif.h>
#include "helper.h"

#define CARD_RESET	0x00F0
#define CARD_INIT	0x00F2
#define CARD_RELEASE	0x00F3
#define CARD_TEST	0x00F4
#define CARD_AUX_IND	0x00F5

#define PH_TESTLOOP	0x0140
#define PH_PAUSE	0x0150

#define DL_FLUSH	0x0224
#define MDL_INFO_SETUP	0x02E0
#define MDL_INFO_CONN	0x02E4
#define MDL_INFO_REL	0x02E8

#define CC_SETUP	0x0300
#define CC_RESUME	0x0304
#define CC_MORE_INFO	0x0310
#define CC_IGNORE	0x0320
#define CC_REJECT	0x0324
#define CC_SETUP_COMPL	0x0330
#define CC_PROCEEDING	0x0340
#define CC_ALERTING	0x0344
#define CC_PROGRESS	0x0348
#define CC_CONNECT	0x0350
#define CC_CHARGE	0x0354
#define CC_NOTIFY	0x0358
#define CC_DISCONNECT	0x0360
#define CC_RELEASE	0x0368
#define CC_SUSPEND	0x0370
#define CC_PROCEED_SEND 0x0374
#define CC_REDIR        0x0378
#define CC_T302		0x0382
#define CC_T303		0x0383
#define CC_T304		0x0384
#define CC_T305		0x0385
#define CC_T308_1	0x0388
#define CC_T308_2	0x038A
#define CC_T309         0x0309
#define CC_T310		0x0390
#define CC_T313		0x0393
#define CC_T318		0x0398
#define CC_T319		0x0399
#define CC_TSPID	0x03A0
#define CC_NOSETUP_RSP	0x03E0
#define CC_SETUP_ERR	0x03E1
#define CC_SUSPEND_ERR	0x03E2
#define CC_RESUME_ERR	0x03E3
#define CC_CONNECT_ERR	0x03E4
#define CC_RELEASE_ERR	0x03E5
#define CC_RESTART	0x03F4
#define CC_TDSS1_IO     0x13F4    /* DSS1 IO user timer */
#define CC_TNI1_IO      0x13F5    /* NI1 IO user timer */

/* define maximum number of possible waiting incoming calls */
#define MAX_WAITING_CALLS 2


#ifdef __KERNEL__

#define MAX_DFRAME_LEN	260
#define MAX_DFRAME_LEN_L1	300
#define HSCX_BUFMAX	4096
#define MAX_DATA_SIZE	(HSCX_BUFMAX - 4)
#define MAX_DATA_MEM	(HSCX_BUFMAX + 64)
#define RAW_BUFMAX	(((HSCX_BUFMAX*6)/5) + 5)
#define MAX_HEADER_LEN	4
#define MAX_WINDOW	8
#define MAX_MON_FRAME	32
#define MAX_DLOG_SPACE	2048
#define MAX_BLOG_SPACE	256

/* #define I4L_IRQ_FLAG SA_INTERRUPT */
#define I4L_IRQ_FLAG    0

/*
 * Statemachine
 */

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

#define FLG_L1_ACTIVATING	1
#define FLG_L1_ACTIVATED	2
#define FLG_L1_DEACTTIMER	3
#define FLG_L1_ACTTIMER		4
#define FLG_L1_T3RUN		5
#define FLG_L1_PULL_REQ		6
#define FLG_L1_UINT		7

#define PACKET_NOACK	250

struct hdlc_stat_reg {
#ifdef __BIG_ENDIAN
	u_char fill __attribute__((packed));
	u_char mode __attribute__((packed));
	u_char xml  __attribute__((packed));
	u_char cmd  __attribute__((packed));
#else
	u_char cmd  __attribute__((packed));
	u_char xml  __attribute__((packed));
	u_char mode __attribute__((packed));
	u_char fill __attribute__((packed));
#endif
};

struct isar_reg {
	unsigned int Flags;
	volatile u_char bstat;
	volatile u_char iis;
	volatile u_char cmsb;
	volatile u_char clsb;
	volatile u_char par[8];
};

struct isar_hw {
	int dpath;
	int txcnt;
	int mml;
	u_char state;
	u_char cmd;
	u_char mod;
	u_char newcmd;
	u_char newmod;
	char try_mod;
	struct timer_list ftimer;
	u_char conmsg[16];
	struct isar_reg *reg;
};

struct hdlc_hw {
	union {
		u_int ctrl;
		struct hdlc_stat_reg sr;
	} ctrl;
	u_int stat;
	int count;              /* Current skb sent count */
};


#define BC_FLG_INIT	1
#define BC_FLG_ACTIV	2
#define BC_FLG_BUSY	3
#define BC_FLG_NOFRAME	4
#define BC_FLG_HALF	5
#define BC_FLG_EMPTY	6
#define BC_FLG_ORIG	7
#define BC_FLG_DLEETX	8
#define BC_FLG_LASTDLE	9
#define BC_FLG_FIRST	10
#define BC_FLG_LASTDATA	11
#define BC_FLG_NMD_DATA	12
#define BC_FLG_FTI_RUN	13
#define BC_FLG_LL_OK	14
#define BC_FLG_LL_CONN	15

typedef struct _bchannel_t {
	int channel;
	int protocol;
	int Flag;
	int debug;
	hisaxinstance_t inst;
	u_char (*BC_Read_Reg)(void *, int, u_char);
	void (*BC_Write_Reg)(void *, int, u_char, u_char);
	struct sk_buff	*next_skb;
	u_int		next_nr;
	u_char		*tx_buf;
	int		tx_idx;
	int             tx_len;
	u_char		*rx_buf;
	int		rx_idx;
	struct sk_buff_head rqueue;	/* B-Channel receive Queue */
	u_char *blog;
	u_char *conmsg;
	struct timer_list transbusy;
	struct tq_struct tqueue;
	int event;
	int err_crc;
	int err_tx;
	int err_rdo;
	int err_inv;
	union {
		struct hdlc_hw hdlc;
		struct isar_hw isar;
	} hw;
} bchannel_t;

struct avm_hw {
	unsigned int cfg_reg;
	unsigned int isac;
	unsigned int hscx[2];
	unsigned int isacfifo;
	unsigned int hscxfifo[2];
	unsigned int counter;
};

struct sedl_hw {
	unsigned int cfg_reg;
	unsigned int adr;
	unsigned int isac;
	unsigned int hscx;
	unsigned int reset_on;
	unsigned int reset_off;
	struct isar_reg isar;
	unsigned int chip;
	unsigned int bus;
};

struct arcofi_msg {
	struct arcofi_msg *next;
	u_char receive;
	u_char len;
	u_char msg[10];
};

struct isac_chip {
	int ph_state;
	u_char *mon_tx;
	u_char *mon_rx;
	int mon_txp;
	int mon_txc;
	int mon_rxp;
	struct arcofi_msg *arcofi_list;
	struct timer_list arcofitimer;
	struct wait_queue *arcofi_wait;
	u_char arcofi_bc;
	u_char arcofi_state;
	u_char mocr;
	u_char adf2;
};

#define HW_IOM1			0
#define HW_IPAC			1
#define HW_ISAR			2
#define HW_ARCOFI		3
#define FLG_TWO_DCHAN		4
#define FLG_TX_BUSY		5
#define FLG_TX_NEXT		6
#define FLG_L1_DBUSY		7
#define FLG_DBUSY_TIMER 	8
#define FLG_LOCK_ATOMIC 	9
#define FLG_ARCOFI_TIMER	10
#define FLG_ARCOFI_ERROR	11
#define FLG_HW_L1_UINT		12
#define FLG_HW_INIT		13

typedef struct _dchannel_t {
	hisaxinstance_t	inst;
	u_int		DFlags;
	u_char		(*readisac) (void *, u_char);
	void		(*writeisac) (void *, u_char, u_char);
	void		(*readisacfifo) (void *, u_char *, int);
	void		(*writeisacfifo) (void *, u_char *, int);
	char		*dlog;
	int		debug;
	u_int		reqnr;
	u_char		*rx_buf;
	int		rx_idx;
	struct sk_buff	*next_skb;
	u_int		next_nr;
	u_char		*tx_buf;
	int		tx_idx;
	int             tx_len;
	int		event;
	int		err_crc;
	int		err_tx;
	int		err_rx;
	union {
		struct isac_chip isac;
	} hw;
	struct sk_buff_head rqueue; /* D-channel receive queue */
	struct tq_struct tqueue;
	struct timer_list dbusytimer;
} dchannel_t;

#define  MON0_RX	1
#define  MON1_RX	2
#define  MON0_TX	4
#define  MON1_TX	8

#define	 HISAX_MAX_CARDS	8

#define  ISDN_CTYPE_16_0	1
#define  ISDN_CTYPE_8_0		2
#define  ISDN_CTYPE_16_3	3
#define  ISDN_CTYPE_PNP		4
#define  ISDN_CTYPE_A1		5
#define  ISDN_CTYPE_ELSA	6
#define  ISDN_CTYPE_ELSA_PNP	7
#define  ISDN_CTYPE_TELESPCMCIA	8
#define  ISDN_CTYPE_IX1MICROR2	9
#define  ISDN_CTYPE_ELSA_PCMCIA	10
#define  ISDN_CTYPE_DIEHLDIVA	11
#define  ISDN_CTYPE_ASUSCOM	12
#define  ISDN_CTYPE_TELEINT	13
#define  ISDN_CTYPE_TELES3C	14
#define  ISDN_CTYPE_SEDLBAUER	15
#define  ISDN_CTYPE_SPORTSTER	16
#define  ISDN_CTYPE_MIC		17
#define  ISDN_CTYPE_ELSA_PCI	18
#define  ISDN_CTYPE_COMPAQ_ISA	19
#define  ISDN_CTYPE_NETJET_S	20
#define  ISDN_CTYPE_TELESPCI	21
#define  ISDN_CTYPE_SEDLBAUER_PCMCIA	22
#define  ISDN_CTYPE_AMD7930	23
#define  ISDN_CTYPE_NICCY	24
#define  ISDN_CTYPE_S0BOX	25
#define  ISDN_CTYPE_A1_PCMCIA	26
#define  ISDN_CTYPE_FRITZPCI	27
#define  ISDN_CTYPE_SEDLBAUER_FAX     28
#define  ISDN_CTYPE_ISURF	29
#define  ISDN_CTYPE_ACERP10	30
#define  ISDN_CTYPE_HSTSAPHIR	31
#define	 ISDN_CTYPE_BKM_A4T	32
#define	 ISDN_CTYPE_SCT_QUADRO	33
#define  ISDN_CTYPE_GAZEL	34
#define  ISDN_CTYPE_HFC_PCI	35
#define  ISDN_CTYPE_W6692	36
#define  ISDN_CTYPE_HFC_SX      37
#define  ISDN_CTYPE_NETJET_U	38
#define  ISDN_CTYPE_HFC_SP_PCMCIA      39
#define  ISDN_CTYPE_COUNT	39


#ifdef ISDN_CHIP_ISAC
#undef ISDN_CHIP_ISAC
#endif

#ifndef __initfunc
#define __initfunc(__arginit) __arginit
#endif

#ifndef __initdata
#define __initdata
#endif

#define HISAX_INITFUNC(__arginit) __initfunc(__arginit)
#define HISAX_INITDATA __initdata

#ifdef	CONFIG_HISAX_FRITZPCI
#define  CARD_FRITZPCI 1
#ifndef ISDN_CHIP_ISAC
#define ISDN_CHIP_ISAC 1
#endif
#else
#define  CARD_FRITZPCI  0
#endif

#define TEI_PER_CARD 1

/* L1 Debug */
#define	L1_DEB_WARN		0x01
#define	L1_DEB_INTSTAT		0x02
#define	L1_DEB_ISAC		0x04
#define	L1_DEB_ISAC_FIFO	0x08
#define	L1_DEB_HSCX		0x10
#define	L1_DEB_HSCX_FIFO	0x20
#define	L1_DEB_LAPD	        0x40
#define	L1_DEB_IPAC	        0x80
#define	L1_DEB_RECEIVE_FRAME    0x100
#define L1_DEB_MONITOR		0x200
#define DEB_DLOG_HEX		0x400
#define DEB_DLOG_VERBOSE	0x800

struct IsdnCard {
	int typ;
	int protocol;		/* EDSS1, 1TR6 or NI1 */
	unsigned int para[4];
	void *card;
};

int init_dchannel(dchannel_t *);
int free_dchannel(dchannel_t *);
int init_bchannel(bchannel_t *);
int free_bchannel(bchannel_t *);
void FsmNew(struct Fsm *fsm, struct FsmNode *fnlist, int fncount);
void FsmFree(struct Fsm *fsm);
int FsmEvent(struct FsmInst *fi, int event, void *arg);
void FsmChangeState(struct FsmInst *fi, int newstate);
void FsmInitTimer(struct FsmInst *fi, struct FsmTimer *ft);
int FsmAddTimer(struct FsmTimer *ft, int millisec, int event,
	void *arg, int where);
void FsmRestartTimer(struct FsmTimer *ft, int millisec, int event,
	void *arg, int where);
void FsmDelTimer(struct FsmTimer *ft, int where);
int jiftime(char *s, long mark);

int QuickHex(char *txt, u_char * p, int cnt);

#endif	/* __KERNEL__ */

#define HZDELAY(jiffs) {int tout = jiffs; while (tout--) udelay(1000000/HZ);}

char *HiSax_getrev(const char *revision);
#ifdef __powerpc__
#include <linux/pci.h>
static inline int pci_enable_device(struct pci_dev *dev)
{
	u16 cmd;
	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	cmd |= PCI_COMMAND_MEMORY | PCI_COMMAND_IO | PCI_COMMAND_SERR;
	cmd &= ~PCI_COMMAND_FAST_BACK;
	pci_write_config_word(dev, PCI_COMMAND, cmd);
	return(0);
}
#else
#define pci_enable_device(dev)	!dev
#endif /* __powerpc__ */

/* $Id: hisax_hw.h,v 1.0 2001/11/02 23:42:26 kkeil Exp $
 *
 *   Basic declarations, defines for HiSax hardware drivers
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#include <linux/hisaxif.h>
#include <linux/tqueue.h>
#include <linux/smp.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <asm/io.h>
#include <linux/ioport.h>
#include <linux/skbuff.h>
#ifdef MEMDBG
#include "memdbg.h"
#endif

#define MAX_DFRAME_LEN_L1	300
#define MAX_MON_FRAME		32
#define MAX_DLOG_SPACE		2048
#define MAX_BLOG_SPACE		256

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
};


struct hfcB_hw {
	unsigned int *send;
	int f1;
	int f2;
};

#define BC_FLG_INIT	1
#define BC_FLG_ACTIV	2
// #define BC_FLG_BUSY	3
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
	hisaxdevice_t	*dev;
	u_char		(*BC_Read_Reg)(void *, int, u_char);
	void		(*BC_Write_Reg)(void *, int, u_char, u_char);
	struct sk_buff	*next_skb;
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
	wait_queue_head_t arcofi_wait;
	u_char arcofi_bc;
	u_char arcofi_state;
	u_char mocr;
	u_char adf2;
};

struct hfcpci_chip {
	int ph_state;
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
	u_char		*rx_buf;
	int		rx_idx;
	struct sk_buff	*next_skb;
	u_char		*tx_buf;
	int		tx_idx;
	int             tx_len;
	int		event;
	int		err_crc;
	int		err_tx;
	int		err_rx;
	union {
		struct isac_chip isac;
		struct hfcpci_chip hfcpci;
	} hw;
	struct sk_buff_head rqueue; /* D-channel receive queue */
	struct tq_struct tqueue;
	struct timer_list dbusytimer;
} dchannel_t;

#define  MON0_RX	1
#define  MON1_RX	2
#define  MON0_TX	4
#define  MON1_TX	8

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

int init_dchannel(dchannel_t *);
int free_dchannel(dchannel_t *);
int init_bchannel(bchannel_t *);
int free_bchannel(bchannel_t *);

void set_dchannel_pid(hisax_pid_t *, int, int);

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

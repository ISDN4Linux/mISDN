/* $Id: dchannel.h,v 1.1 2003/06/21 21:39:54 kkeil Exp $
 *
 *   Basic declarations for dchannel HW
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
	hisaxinstance_t		inst;
	u_int			DFlags;
	u_int			type;
	u_int			ph_state;
	u_char			(*read_reg) (void *, u_char);
	void			(*write_reg) (void *, u_char, u_char);
	void			(*read_fifo) (void *, u_char *, int);
	void			(*write_fifo) (void *, u_char *, int);
	char			*dlog;
	int			debug;
	struct sk_buff		*rx_skb;
	struct sk_buff		*next_skb;
	u_char			*tx_buf;
	int			tx_idx;
	int             	tx_len;
	int			err_crc;
	int			err_tx;
	int			err_rx;
	void			*hw;
	struct timer_list	dbusytimer;
	u_int			event;
	struct sk_buff_head	rqueue; /* D-channel receive queue */
	struct tq_struct	tqueue;
	void			(*hw_bh) (struct _dchannel_t *);
} dchannel_t;

#define  MON0_RX	1
#define  MON1_RX	2
#define  MON0_TX	4
#define  MON1_TX	8

extern int init_dchannel(dchannel_t *);
extern int free_dchannel(dchannel_t *);

void set_dchannel_pid(hisax_pid_t *, int, int);

static inline void
dchannel_sched_event(dchannel_t *dch, int event)
{
	test_and_set_bit(event, &dch->event);
	queue_task(&dch->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

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

/* $Id: avm_fritz.c,v 1.12 2003/07/21 12:44:45 kkeil Exp $
 *
 * fritz_pci.c    low level stuff for AVM Fritz!PCI and ISA PnP isdn cards
 *              Thanks to AVM, Berlin for informations
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/isapnp.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#include "dchannel.h"
#include "bchannel.h"
#include "isac.h"
#include "layer1.h"
#include "helper.h"
#include "debug.h"

#define SPIN_DEBUG
#define LOCK_STATISTIC
#include "hw_lock.h"

static const char *avm_fritz_rev = "$Revision: 1.12 $";

enum {
	AVM_FRITZ_PCI,
	AVM_FRITZ_PNP,
	AVM_FRITZ_PCIV2,
};

#ifndef PCI_VENDOR_ID_AVM
#define PCI_VENDOR_ID_AVM	0x1244
#endif
#ifndef PCI_DEVICE_ID_AVM_FRITZ
#define PCI_DEVICE_ID_AVM_FRITZ	0xa00
#endif
#ifndef PCI_DEVICE_ID_AVM_A1_V2
#define PCI_DEVICE_ID_AVM_A1_V2	0xe00
#endif

#define HDLC_FIFO		0x0
#define HDLC_STATUS		0x4
#define CHIP_WINDOW		0x10

#define CHIP_INDEX		0x4
#define AVM_HDLC_1		0x00
#define AVM_HDLC_2		0x01
#define AVM_ISAC_FIFO		0x02
#define AVM_ISAC_REG_LOW	0x04
#define AVM_ISAC_REG_HIGH	0x06

#define AVM_STATUS0_IRQ_ISAC	0x01
#define AVM_STATUS0_IRQ_HDLC	0x02
#define AVM_STATUS0_IRQ_TIMER	0x04
#define AVM_STATUS0_IRQ_MASK	0x07

#define AVM_STATUS0_RESET	0x01
#define AVM_STATUS0_DIS_TIMER	0x02
#define AVM_STATUS0_RES_TIMER	0x04
#define AVM_STATUS0_ENA_IRQ	0x08
#define AVM_STATUS0_TESTBIT	0x10

#define AVM_STATUS1_INT_SEL	0x0f
#define AVM_STATUS1_ENA_IOM	0x80

#define HDLC_MODE_ITF_FLG	0x01
#define HDLC_MODE_TRANS	0x02
#define HDLC_MODE_CCR_7	0x04
#define HDLC_MODE_CCR_16	0x08
#define HDLC_MODE_TESTLOOP	0x80

#define HDLC_INT_XPR		0x80
#define HDLC_INT_XDU		0x40
#define HDLC_INT_RPR		0x20
#define HDLC_INT_MASK		0xE0

#define HDLC_STAT_RME		0x01
#define HDLC_STAT_RDO		0x10
#define HDLC_STAT_CRCVFRRAB	0x0E
#define HDLC_STAT_CRCVFR	0x06
#define HDLC_STAT_RML_MASK	0x3f00

#define HDLC_CMD_XRS		0x80
#define HDLC_CMD_XME		0x01
#define HDLC_CMD_RRS		0x20
#define HDLC_CMD_XML_MASK	0x3f00

/* Fritz PCI v2.0 */

#define  AVM_HDLC_FIFO_1        0x10
#define  AVM_HDLC_FIFO_2        0x18

#define  AVM_HDLC_STATUS_1      0x14
#define  AVM_HDLC_STATUS_2      0x1c

#define  AVM_ISACSX_INDEX       0x04
#define  AVM_ISACSX_DATA        0x08

/* data struct */

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

typedef struct hdlc_hw {
	union {
		u_int ctrl;
		struct hdlc_stat_reg sr;
	} ctrl;
	u_int stat;
} hdlc_hw_t;


typedef struct _fritzpnppci {
	struct _fritzpnppci	*prev;
	struct _fritzpnppci	*next;
	struct pci_dev		*pdev;
	u_int			type;
	u_int			irq;
	u_int			addr;
	mISDN_HWlock_t		lock;
	isac_chip_t		isac;
	hdlc_hw_t		hdlc[2];
	dchannel_t		dch;
	bchannel_t		bch[2];
} fritzpnppci;


static int lock_dev(void *data, int nowait)
{
	register mISDN_HWlock_t	*lock = &((fritzpnppci *)data)->lock;

	return(lock_HW(lock, nowait));
} 

static void unlock_dev(void *data)
{
	register mISDN_HWlock_t *lock = &((fritzpnppci *)data)->lock;

	unlock_HW(lock);
}

/* Interface functions */

static u_char
ReadISAC(void *fc, u_char offset)
{
	register u_char idx = (offset > 0x2f) ? AVM_ISAC_REG_HIGH : AVM_ISAC_REG_LOW;
	register long addr = ((fritzpnppci *)fc)->addr;
	register u_char val;

	outb(idx, addr + CHIP_INDEX);
	val = inb(addr + CHIP_WINDOW + (offset & 0xf));
	return (val);
}

static void
WriteISAC(void *fc, u_char offset, u_char value)
{
	register u_char idx = (offset > 0x2f) ? AVM_ISAC_REG_HIGH : AVM_ISAC_REG_LOW;
	register long addr = ((fritzpnppci *)fc)->addr;

	outb(idx, addr + CHIP_INDEX);
	outb(value, addr + CHIP_WINDOW + (offset & 0xf));
}

static void
ReadISACfifo(void *fc, u_char * data, int size)
{
	register long addr = ((fritzpnppci *)fc)->addr;

	outb(AVM_ISAC_FIFO, addr + CHIP_INDEX);
	insb(addr + CHIP_WINDOW, data, size);
}

static void
WriteISACfifo(void *fc, u_char * data, int size)
{
	register long addr = ((fritzpnppci *)fc)->addr;

	outb(AVM_ISAC_FIFO, addr + CHIP_INDEX);
	outsb(addr + CHIP_WINDOW, data, size);
}

static unsigned char
fcpci2_read_isac(void *fc, unsigned char offset)
{
	register long addr = ((fritzpnppci *)fc)->addr;
	unsigned char val;

	outl(offset, addr + AVM_ISACSX_INDEX);
	val = inl(addr + AVM_ISACSX_DATA);
	return val;
}

static void
fcpci2_write_isac(void *fc, unsigned char offset, unsigned char value)
{
	register long addr = ((fritzpnppci *)fc)->addr;

	outl(offset, addr + AVM_ISACSX_INDEX);
	outl(value, addr + AVM_ISACSX_DATA);
}

static void
fcpci2_read_isac_fifo(void *fc, unsigned char * data, int size)
{
	register long addr = ((fritzpnppci *)fc)->addr;
	int i;

	outl(0, addr + AVM_ISACSX_INDEX);
	for (i = 0; i < size; i++)
		data[i] = inl(addr + AVM_ISACSX_DATA);
}

static void
fcpci2_write_isac_fifo(void *fc, unsigned char * data, int size)
{
	register long addr = ((fritzpnppci *)fc)->addr;
	int i;

	outl(0, addr + AVM_ISACSX_INDEX);
	for (i = 0; i < size; i++)
		outl(data[i], addr + AVM_ISACSX_DATA);
}

static inline
bchannel_t *Sel_BCS(fritzpnppci *fc, int channel)
{
	if (fc->bch[0].protocol && (fc->bch[0].channel == channel))
		return(&fc->bch[0]);
	else if (fc->bch[1].protocol && (fc->bch[1].channel == channel))
		return(&fc->bch[1]);
	else
		return(NULL);
}

static inline void
__write_ctrl_pnp(fritzpnppci *fc, hdlc_hw_t *hdlc, int channel, int which) {
	register u_char idx = channel ? AVM_HDLC_2 : AVM_HDLC_1;

	outb(idx, fc->addr + CHIP_INDEX);
	if (which & 4)
		outb(hdlc->ctrl.sr.mode, fc->addr + CHIP_WINDOW + HDLC_STATUS + 2);
	if (which & 2)
		outb(hdlc->ctrl.sr.xml, fc->addr + CHIP_WINDOW + HDLC_STATUS + 1);
	if (which & 1)
		outb(hdlc->ctrl.sr.cmd, fc->addr + CHIP_WINDOW + HDLC_STATUS);
}

static inline void
__write_ctrl_pci(fritzpnppci *fc, hdlc_hw_t *hdlc, int channel) {
	register u_int idx = channel ? AVM_HDLC_2 : AVM_HDLC_1;

	outl(idx, fc->addr + CHIP_INDEX);
	outl(hdlc->ctrl.ctrl, fc->addr + CHIP_WINDOW + HDLC_STATUS);
}

static inline void
__write_ctrl_pciv2(fritzpnppci *fc, hdlc_hw_t *hdlc, int channel) {
	outl(hdlc->ctrl.ctrl, fc->addr + channel ? AVM_HDLC_STATUS_2 : AVM_HDLC_STATUS_1);
}

void
write_ctrl(bchannel_t *bch, int which) {
	fritzpnppci	*fc = bch->inst.data;
	hdlc_hw_t	*hdlc = bch->hw;

	if (fc->dch.debug & L1_DEB_HSCX)
		debugprint(&bch->inst, "hdlc %c wr%x ctrl %x",
			'A' + bch->channel, which, hdlc->ctrl.ctrl);
	switch(fc->type) {
		case AVM_FRITZ_PCIV2:
			__write_ctrl_pciv2(fc, hdlc, bch->channel);
			break;
		case AVM_FRITZ_PCI:
			__write_ctrl_pci(fc, hdlc, bch->channel);
			break;
		case AVM_FRITZ_PNP:
			__write_ctrl_pnp(fc, hdlc, bch->channel, which);
			break;
	}
}


static inline u_int
__read_status_pnp(u_long addr, u_int channel)
{
	register u_int stat;

	outb(channel ? AVM_HDLC_2 : AVM_HDLC_1, addr + CHIP_INDEX);
	stat = inb(addr + CHIP_WINDOW + HDLC_STATUS);
	if (stat & HDLC_INT_RPR)
		stat |= (inb(addr + CHIP_WINDOW + HDLC_STATUS + 1)) << 8;
	return (stat);
}

static inline u_int
__read_status_pci(u_long addr, u_int channel)
{
	outl(channel ? AVM_HDLC_2 : AVM_HDLC_1, addr + CHIP_INDEX);
	return inl(addr + CHIP_WINDOW + HDLC_STATUS);
}

static inline u_int
__read_status_pciv2(u_long addr, u_int channel)
{
	return inl(addr + channel ? AVM_HDLC_STATUS_2 : AVM_HDLC_STATUS_1);
}


static u_int
read_status(fritzpnppci *fc, int channel)
{
	switch(fc->type) {
		case AVM_FRITZ_PCIV2:
			return(__read_status_pciv2(fc->addr, channel));
		case AVM_FRITZ_PCI:
			return(__read_status_pci(fc->addr, channel));
		case AVM_FRITZ_PNP:
			return(__read_status_pnp(fc->addr, channel));
	}
	/* dummy */
	return(0);
}

static int
modehdlc(bchannel_t *bch, int bc, int protocol)
{
	int		hdlc_ch = bch->channel;
	hdlc_hw_t	*hdlc = bch->hw;

	if (bch->debug & L1_DEB_HSCX)
		debugprint(&bch->inst, "hdlc %c protocol %x-->%x ch %d-->%d",
			'A' + hdlc_ch, bch->protocol, protocol, hdlc_ch, bc);
	hdlc->ctrl.ctrl = 0;
	switch (protocol) {
		case (-1): /* used for init */
			bch->protocol = -1;
			bch->channel = bc;
			bc = 0;
		case (ISDN_PID_NONE):
			if (bch->protocol == ISDN_PID_NONE)
				break;
			hdlc->ctrl.sr.cmd  = HDLC_CMD_XRS | HDLC_CMD_RRS;
			hdlc->ctrl.sr.mode = HDLC_MODE_TRANS;
			write_ctrl(bch, 5);
			bch->protocol = ISDN_PID_NONE;
			bch->channel = bc;
			break;
		case (ISDN_PID_L1_B_64TRANS):
			bch->protocol = protocol;
			bch->channel = bc;
			hdlc->ctrl.sr.cmd  = HDLC_CMD_XRS | HDLC_CMD_RRS;
			hdlc->ctrl.sr.mode = HDLC_MODE_TRANS;
			write_ctrl(bch, 5);
			hdlc->ctrl.sr.cmd = HDLC_CMD_XRS;
			write_ctrl(bch, 1);
			hdlc->ctrl.sr.cmd = 0;
			bch_sched_event(bch, B_XMTBUFREADY);
			break;
		case (ISDN_PID_L1_B_64HDLC):
			bch->protocol = protocol;
			bch->channel = bc;
			hdlc->ctrl.sr.cmd  = HDLC_CMD_XRS | HDLC_CMD_RRS;
			hdlc->ctrl.sr.mode = HDLC_MODE_ITF_FLG;
			write_ctrl(bch, 5);
			hdlc->ctrl.sr.cmd = HDLC_CMD_XRS;
			write_ctrl(bch, 1);
			hdlc->ctrl.sr.cmd = 0;
			bch_sched_event(bch, B_XMTBUFREADY);
			break;
		default:
			debugprint(&bch->inst, "prot not known %x", protocol);
			return(-ENOPROTOOPT);
	}
	return(0);
}

static void
hdlc_empty_fifo(bchannel_t *bch, int count)
{
	register u_int *ptr;
	u_char *p;
	u_char idx = bch->channel ? AVM_HDLC_2 : AVM_HDLC_1;
	int cnt=0;
	fritzpnppci *fc = bch->inst.data;

	if ((fc->dch.debug & L1_DEB_HSCX) && !(fc->dch.debug & L1_DEB_HSCX_FIFO))
		debugprint(&bch->inst, "hdlc_empty_fifo %d", count);
	if (bch->rx_idx + count > MAX_DATA_MEM) {
		if (fc->dch.debug & L1_DEB_WARN)
			debugprint(&bch->inst, "hdlc_empty_fifo: incoming packet too large");
		return;
	}
	ptr = (u_int *) p = bch->rx_buf + bch->rx_idx;
	bch->rx_idx += count;
	if (fc->type == AVM_FRITZ_PCIV2) {
		while (cnt < count) {
#ifdef __powerpc__
#ifdef CONFIG_APUS
			*ptr++ = in_le32((unsigned *)(fc->addr + bch->channel ? AVM_HDLC_FIFO_2 : AVM_HDLC_FIFO_1 +_IO_BASE));
#else
			*ptr++ = in_be32((unsigned *)(fc->addr + bch->channel ? AVM_HDLC_FIFO_2 : AVM_HDLC_FIFO_1 +_IO_BASE));
#endif /* CONFIG_APUS */
#else
			*ptr++ = inl(fc->addr + bch->channel ? AVM_HDLC_FIFO_2 : AVM_HDLC_FIFO_1);
#endif /* __powerpc__ */
			cnt += 4;
		}
	} else if (fc->type == AVM_FRITZ_PCI) {
		outl(idx, fc->addr + CHIP_INDEX);
		while (cnt < count) {
#ifdef __powerpc__
#ifdef CONFIG_APUS
			*ptr++ = in_le32((unsigned *)(fc->addr + CHIP_WINDOW +_IO_BASE));
#else
			*ptr++ = in_be32((unsigned *)(fc->addr + CHIP_WINDOW +_IO_BASE));
#endif /* CONFIG_APUS */
#else
			*ptr++ = inl(fc->addr + CHIP_WINDOW);
#endif /* __powerpc__ */
			cnt += 4;
		}
	} else {
		outb(idx, fc->addr + CHIP_INDEX);
		while (cnt < count) {
			*p++ = inb(fc->addr + CHIP_WINDOW);
			cnt++;
		}
	}
	if (fc->dch.debug & L1_DEB_HSCX_FIFO) {
		char *t = bch->blog;

		if (fc->type == AVM_FRITZ_PNP)
			p = (u_char *) ptr;
		t += sprintf(t, "hdlc_empty_fifo %c cnt %d",
			     bch->channel ? 'B' : 'A', count);
		QuickHex(t, p, count);
		debugprint(&bch->inst, bch->blog);
	}
}

#define HDLC_FIFO_SIZE	32

static void
hdlc_fill_fifo(bchannel_t *bch)
{
	fritzpnppci	*fc = bch->inst.data;
	hdlc_hw_t	*hdlc = bch->hw;
	int		count, cnt =0;
	u_char		*p;
	u_int		*ptr;

	if ((bch->debug & L1_DEB_HSCX) && !(bch->debug & L1_DEB_HSCX_FIFO))
		debugprint(&bch->inst, __FUNCTION__);
	count = bch->tx_len - bch->tx_idx;
	if (count <= 0)
		return;
	p = bch->tx_buf + bch->tx_idx;
	hdlc->ctrl.sr.cmd &= ~HDLC_CMD_XME;
	if (count > HDLC_FIFO_SIZE) {
		count = HDLC_FIFO_SIZE;
	} else {
		if (bch->protocol != ISDN_PID_L1_B_64TRANS)
			hdlc->ctrl.sr.cmd |= HDLC_CMD_XME;
	}
	if ((bch->debug & L1_DEB_HSCX) && !(bch->debug & L1_DEB_HSCX_FIFO))
		debugprint(&bch->inst, "%s: %d/%d", __FUNCTION__,
			count, bch->tx_idx);
	ptr = (u_int *) p;
	bch->tx_idx += count;
	hdlc->ctrl.sr.xml = ((count == HDLC_FIFO_SIZE) ? 0 : count);
	if (fc->type == AVM_FRITZ_PCIV2) {
		__write_ctrl_pciv2(fc, hdlc, bch->channel);
		while (cnt<count) {
#ifdef __powerpc__
#ifdef CONFIG_APUS
			out_le32((unsigned *)(fc->addr + bch->channel ? AVM_HDLC_FIFO_2 : AVM_HDLC_FIFO_1 +_IO_BASE), *ptr++);
#else
			out_be32((unsigned *)(fc->addr + bch->channel ? AVM_HDLC_FIFO_2 : AVM_HDLC_FIFO_1 +_IO_BASE), *ptr++);
#endif /* CONFIG_APUS */
#else
			outl(*ptr++, fc->addr + bch->channel ? AVM_HDLC_FIFO_2 : AVM_HDLC_FIFO_1);
#endif /* __powerpc__ */
			cnt += 4;
		}
	} else if (fc->type == AVM_FRITZ_PCI) {
		__write_ctrl_pci(fc, hdlc, bch->channel);
		while (cnt<count) {
#ifdef __powerpc__
#ifdef CONFIG_APUS
			out_le32((unsigned *)(fc->addr + CHIP_WINDOW +_IO_BASE), *ptr++);
#else
			out_be32((unsigned *)(fc->addr + CHIP_WINDOW +_IO_BASE), *ptr++);
#endif /* CONFIG_APUS */
#else
			outl(*ptr++, fc->addr + CHIP_WINDOW);
#endif /* __powerpc__ */
			cnt += 4;
		}
	} else {
		__write_ctrl_pnp(fc, hdlc, bch->channel, 3);
		while (cnt<count) {
			outb(*p++, fc->addr + CHIP_WINDOW);
			cnt++;
		}
	}
	if (bch->debug & L1_DEB_HSCX_FIFO) {
		char *t = bch->blog;

		if (fc->type == AVM_FRITZ_PNP)
			p = (u_char *) ptr;
		t += sprintf(t, "hdlc_fill_fifo %c cnt %d",
			     bch->channel ? 'B' : 'A', count);
		QuickHex(t, p, count);
		debugprint(&bch->inst, bch->blog);
	}
}

static void
HDLC_irq(bchannel_t *bch, u_int stat) {
	int		len;
	struct sk_buff	*skb;
	hdlc_hw_t	*hdlc = bch->hw;

	if (bch->debug & L1_DEB_HSCX)
		debugprint(&bch->inst, "ch%d stat %#x", bch->channel, stat);
	if (stat & HDLC_INT_RPR) {
		if (stat & HDLC_STAT_RDO) {
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "RDO");
			else
				debugprint(&bch->inst, "ch%d stat %#x", bch->channel, stat);
			hdlc->ctrl.sr.xml = 0;
			hdlc->ctrl.sr.cmd |= HDLC_CMD_RRS;
			write_ctrl(bch, 1);
			hdlc->ctrl.sr.cmd &= ~HDLC_CMD_RRS;
			write_ctrl(bch, 1);
			bch->rx_idx = 0;
		} else {
			if (!(len = (stat & HDLC_STAT_RML_MASK)>>8))
				len = 32;
			hdlc_empty_fifo(bch, len);
			if ((stat & HDLC_STAT_RME) || (bch->protocol == ISDN_PID_L1_B_64TRANS)) {
				if (((stat & HDLC_STAT_CRCVFRRAB)==HDLC_STAT_CRCVFR) ||
					(bch->protocol == ISDN_PID_L1_B_64TRANS)) {
					if (!(skb = dev_alloc_skb(bch->rx_idx)))
						printk(KERN_WARNING "HDLC: receive out of memory\n");
					else {
						memcpy(skb_put(skb, bch->rx_idx),
							bch->rx_buf, bch->rx_idx);
						skb_queue_tail(&bch->rqueue, skb);
					}
					bch->rx_idx = 0;
					bch_sched_event(bch, B_RCVBUFREADY);
				} else {
					if (bch->debug & L1_DEB_HSCX)
						debugprint(&bch->inst, "invalid frame");
					else
						debugprint(&bch->inst, "ch%d invalid frame %#x", bch->channel, stat);
					bch->rx_idx = 0;
				}
			}
		}
	}
	if (stat & HDLC_INT_XDU) {
		/* Here we lost an TX interrupt, so
		 * restart transmitting the whole frame.
		 */
		if (bch->tx_len) {
			bch->tx_idx = 0;
			if (bch->debug & L1_DEB_WARN)
				debugprint(&bch->inst, "ch%d XDU", bch->channel);
		} else if (bch->debug & L1_DEB_WARN)
			debugprint(&bch->inst, "ch%d XDU without data", bch->channel);
		hdlc->ctrl.sr.xml = 0;
		hdlc->ctrl.sr.cmd |= HDLC_CMD_XRS;
		write_ctrl(bch, 1);
		hdlc->ctrl.sr.cmd &= ~HDLC_CMD_XRS;
		write_ctrl(bch, 1);
		hdlc_fill_fifo(bch);
	} else if (stat & HDLC_INT_XPR) {
		if (bch->tx_idx < bch->tx_len) {
			hdlc_fill_fifo(bch);
		} else {
			bch->tx_idx = 0;
			if (test_and_clear_bit(BC_FLG_TX_NEXT, &bch->Flag)) {
				if (bch->next_skb) {
					bch->tx_len = bch->next_skb->len;
					memcpy(bch->tx_buf,
						bch->next_skb->data, bch->tx_len);
					hdlc_fill_fifo(bch);
					bch_sched_event(bch, B_XMTBUFREADY);
				} else {
					bch->tx_len = 0;
					printk(KERN_WARNING "hdlc tx irq TX_NEXT without skb\n");
					test_and_clear_bit(BC_FLG_TX_BUSY, &bch->Flag);
				}
			} else {
				bch->tx_len = 0;
				test_and_clear_bit(BC_FLG_TX_BUSY, &bch->Flag);
				bch_sched_event(bch, B_XMTBUFREADY);
			}
		}
	}
}

static inline void
HDLC_irq_main(fritzpnppci *fc)
{
	u_int stat;
	bchannel_t *bch;

	stat = read_status(fc, 0);
	if (stat & HDLC_INT_MASK) {
		if (!(bch = Sel_BCS(fc, 0))) {
			if (fc->bch[0].debug)
				debugprint(&fc->bch[0].inst, "hdlc spurious channel 0 IRQ");
		} else
			HDLC_irq(bch, stat);
	}
	stat = read_status(fc, 1);
	if (stat & HDLC_INT_MASK) {
		if (!(bch = Sel_BCS(fc, 1))) {
			if (fc->bch[1].debug)
				debugprint(&fc->bch[1].inst, "hdlc spurious channel 1 IRQ");
		} else
			HDLC_irq(bch, stat);
	}
}

static void
avm_fritz_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	fritzpnppci	*fc = dev_id;
	u_long		flags;
	u_char val;
	u_char sval;

	spin_lock_irqsave(&fc->lock.lock, flags);
#ifdef SPIN_DEBUG
	fc->lock.spin_adr = (void *)0x2001;
#endif
	sval = inb(fc->addr + 2);
	if ((sval & AVM_STATUS0_IRQ_MASK) == AVM_STATUS0_IRQ_MASK) {
		/* possible a shared  IRQ reqest */
#ifdef SPIN_DEBUG
		fc->lock.spin_adr = NULL;
#endif
		spin_unlock_irqrestore(&fc->lock.lock, flags);
		return;
	}
	if (test_and_set_bit(STATE_FLAG_BUSY, &fc->lock.state)) {
		printk(KERN_ERR "%s: STATE_FLAG_BUSY allready activ, should never happen state:%x\n",
			__FUNCTION__, fc->lock.state);
#ifdef SPIN_DEBUG
		printk(KERN_ERR "%s: previous lock:%p\n",
			__FUNCTION__, fc->lock.busy_adr);
#endif
#ifdef LOCK_STATISTIC
		fc->lock.irq_fail++;
#endif
	} else {
#ifdef LOCK_STATISTIC
		fc->lock.irq_ok++;
#endif
#ifdef SPIN_DEBUG
		fc->lock.busy_adr = avm_fritz_interrupt;
#endif
	}

	test_and_set_bit(STATE_FLAG_INIRQ, &fc->lock.state);
#ifdef SPIN_DEBUG
	fc->lock.spin_adr = NULL;
#endif
	spin_unlock_irqrestore(&fc->lock.lock, flags);
	if (!(sval & AVM_STATUS0_IRQ_ISAC)) {
		val = ReadISAC(fc, ISAC_ISTA);
		ISAC_interrupt(&fc->dch, val);
	}
	if (!(sval & AVM_STATUS0_IRQ_HDLC)) {
		HDLC_irq_main(fc);
	}
	if (fc->type == AVM_FRITZ_PNP) {
		WriteISAC(fc, ISAC_MASK, 0xFF);
		WriteISAC(fc, ISAC_MASK, 0x0);
	}
	spin_lock_irqsave(&fc->lock.lock, flags);
#ifdef SPIN_DEBUG
	fc->lock.spin_adr = (void *)0x2002;
#endif
	if (!test_and_clear_bit(STATE_FLAG_INIRQ, &fc->lock.state)) {
	}
	if (!test_and_clear_bit(STATE_FLAG_BUSY, &fc->lock.state)) {
		printk(KERN_ERR "%s: STATE_FLAG_BUSY not locked state(%x)\n",
			__FUNCTION__, fc->lock.state);
	}
#ifdef SPIN_DEBUG
	fc->lock.busy_adr = NULL;
	fc->lock.spin_adr = NULL;
#endif
	spin_unlock_irqrestore(&fc->lock.lock, flags);
}

static int
hdlc_down(mISDNif_t *hif, struct sk_buff *skb)
{
	bchannel_t	*bch;
	int		ret = -EINVAL;
	mISDN_head_t	*hh;

	if (!hif || !skb)
		return(ret);
	hh = mISDN_HEAD_P(skb);
	bch = hif->fdata;
	if ((hh->prim == PH_DATA_REQ) ||
		(hh->prim == (DL_DATA | REQUEST))) {
		if (bch->next_skb) {
			debugprint(&bch->inst, " l2l1 next_skb exist this shouldn't happen");
			return(-EBUSY);
		}
		bch->inst.lock(bch->inst.data, 0);
		if (test_and_set_bit(BC_FLG_TX_BUSY, &bch->Flag)) {
			test_and_set_bit(BC_FLG_TX_NEXT, &bch->Flag);
			bch->next_skb = skb;
			bch->inst.unlock(bch->inst.data);
			return(0);
		} else {
			bch->tx_len = skb->len;
			memcpy(bch->tx_buf, skb->data, bch->tx_len);
			bch->tx_idx = 0;
			hdlc_fill_fifo(bch);
			bch->inst.unlock(bch->inst.data);
			skb_trim(skb, 0);
			return(if_newhead(&bch->inst.up, hh->prim | CONFIRM,
				DINFO_SKB, skb));
		}
	} else if ((hh->prim == (PH_ACTIVATE | REQUEST)) ||
		(hh->prim == (DL_ESTABLISH  | REQUEST))) {
		if (test_and_set_bit(BC_FLG_ACTIV, &bch->Flag))
			ret = 0;
		else {
			bch->inst.lock(bch->inst.data,0);
			ret = modehdlc(bch, bch->channel,
				bch->inst.pid.protocol[1]);
			bch->inst.unlock(bch->inst.data);
		}
		skb_trim(skb, 0);
		return(if_newhead(&bch->inst.up, hh->prim | CONFIRM, ret, skb));
	} else if ((hh->prim == (PH_DEACTIVATE | REQUEST)) ||
		(hh->prim == (DL_RELEASE | REQUEST)) ||
		(hh->prim == (MGR_DISCONNECT | REQUEST))) {
		bch->inst.lock(bch->inst.data,0);
		if (test_and_clear_bit(BC_FLG_TX_NEXT, &bch->Flag)) {
			dev_kfree_skb(bch->next_skb);
			bch->next_skb = NULL;
		}
		test_and_clear_bit(BC_FLG_TX_BUSY, &bch->Flag);
		modehdlc(bch, bch->channel, 0);
		test_and_clear_bit(BC_FLG_ACTIV, &bch->Flag);
		bch->inst.unlock(bch->inst.data);
		skb_trim(skb, 0);
		if (hh->prim != (MGR_DISCONNECT | REQUEST))
			if (!if_newhead(&bch->inst.up, hh->prim | CONFIRM, 0, skb))
				return(0);
		ret = 0;
	} else {
		printk(KERN_WARNING "hdlc_down unknown prim(%x)\n", hh->prim);
		ret = -EINVAL;
	}
	if (!ret)
		dev_kfree_skb(skb);
	return(ret);
}

static void
inithdlc(fritzpnppci *fc)
{
	modehdlc(&fc->bch[0], 0, -1);
	modehdlc(&fc->bch[1], 1, -1);
}

void
clear_pending_hdlc_ints(fritzpnppci *fc)
{
	u_int val;

	val = read_status(fc, 0);
	debugprint(&fc->dch.inst, "HDLC 1 STA %x", val);
	val = read_status(fc, 1);
	debugprint(&fc->dch.inst, "HDLC 2 STA %x", val);
}

static void
reset_avmpcipnp(fritzpnppci *fc)
{
	long flags;

	printk(KERN_INFO "AVM PCI/PnP: reset\n");
	save_flags(flags);
	sti();
	outb(AVM_STATUS0_RESET | AVM_STATUS0_DIS_TIMER, fc->addr + 2);
	current->state = TASK_UNINTERRUPTIBLE;
	schedule_timeout((10*HZ)/1000); /* Timeout 10ms */
	outb(AVM_STATUS0_DIS_TIMER | AVM_STATUS0_RES_TIMER | AVM_STATUS0_ENA_IRQ, fc->addr + 2);
	outb(AVM_STATUS1_ENA_IOM | fc->irq, fc->addr + 3);
	current->state = TASK_UNINTERRUPTIBLE;
	schedule_timeout((10*HZ)/1000); /* Timeout 10ms */
	printk(KERN_INFO "AVM PCI/PnP: S1 %x\n", inb(fc->addr + 3));
	restore_flags(flags);
}

static int init_card(fritzpnppci *fc)
{
	int irq_cnt, cnt = 3;
	long flags;
	u_int shared = SA_SHIRQ;

	if (fc->type == AVM_FRITZ_PNP)
		shared = 0;
	save_flags(flags);
	irq_cnt = kstat_irqs(fc->irq);
	printk(KERN_INFO "AVM Fritz!PCI: IRQ %d count %d\n", fc->irq, irq_cnt);
	lock_dev(fc, 0);
	if (request_irq(fc->irq, avm_fritz_interrupt, SA_SHIRQ,
		"AVM Fritz!PCI", fc)) {
		printk(KERN_WARNING "mISDN: couldn't get interrupt %d\n",
			fc->irq);
		unlock_dev(fc);
		return(-EIO);
	}
	while (cnt) {
		int	ret;
		ISAC_clear_pending_ints(&fc->dch);
		if ((ret=ISAC_init(&fc->dch))) {
			printk(KERN_WARNING "mISDN: ISAC_init failed with %d\n", ret);
			break;
		}
		clear_pending_hdlc_ints(fc);
		inithdlc(fc);
		outb(AVM_STATUS0_DIS_TIMER | AVM_STATUS0_RES_TIMER,
			fc->addr + 2);
		WriteISAC(fc, ISAC_MASK, 0);
		outb(AVM_STATUS0_DIS_TIMER | AVM_STATUS0_RES_TIMER |
			AVM_STATUS0_ENA_IRQ, fc->addr + 2);
		/* RESET Receiver and Transmitter */
		WriteISAC(fc, ISAC_CMDR, 0x41);
		unlock_dev(fc);
		sti();
		/* Timeout 10ms */
		current->state = TASK_UNINTERRUPTIBLE;
		schedule_timeout((10*HZ)/1000);
		restore_flags(flags);
		printk(KERN_INFO "AVM Fritz!PCI: IRQ %d count %d\n",
			fc->irq, kstat_irqs(fc->irq));
		if (kstat_irqs(fc->irq) == irq_cnt) {
			printk(KERN_WARNING
			       "AVM Fritz!PCI: IRQ(%d) getting no interrupts during init %d\n",
			       fc->irq, 4 - cnt);
			if (cnt == 1) {
				return (-EIO);
			} else {
				reset_avmpcipnp(fc);
				cnt--;
			}
		} else {
			return(0);
		}
		lock_dev(fc, 0);
	}
	unlock_dev(fc);
	return(-EIO);
}

#define MAX_CARDS	4
#define MODULE_PARM_T	"1-4i"
static int fritz_cnt;
static u_int protocol[MAX_CARDS];
static int layermask[MAX_CARDS];

static mISDNobject_t	fritz;
static int debug;

#ifdef MODULE
MODULE_AUTHOR("Karsten Keil");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
MODULE_PARM(debug, "1i");
MODULE_PARM(io, MODULE_PARM_T);
MODULE_PARM(protocol, MODULE_PARM_T);
MODULE_PARM(irq, MODULE_PARM_T);
MODULE_PARM(layermask, MODULE_PARM_T);
#endif

static char FritzName[] = "Fritz!PCI";

static struct pci_device_id fcpci_ids[] __devinitdata = {
	{ PCI_VENDOR_ID_AVM, PCI_DEVICE_ID_AVM_A1   , PCI_ANY_ID, PCI_ANY_ID,
	  0, 0, (unsigned long) "Fritz!Card PCI" },
	{ PCI_VENDOR_ID_AVM, PCI_DEVICE_ID_AVM_A1_V2, PCI_ANY_ID, PCI_ANY_ID,
	  0, 0, (unsigned long) "Fritz!Card PCI v2" },
	{ }
};
MODULE_DEVICE_TABLE(pci, fcpci_ids);

static struct isapnp_device_id fcpnp_ids[] __devinitdata = {
	{ ISAPNP_VENDOR('A', 'V', 'M'), ISAPNP_FUNCTION(0x0900),
	  ISAPNP_VENDOR('A', 'V', 'M'), ISAPNP_FUNCTION(0x0900), 
	  (unsigned long) "Fritz!Card PnP" },
	{ }
};
MODULE_DEVICE_TABLE(isapnp, fcpnp_ids);

int
setup_fritz(fritzpnppci *fc)
{
	u_int val, ver;

	if (check_region((fc->addr), 32)) {
		printk(KERN_WARNING
		       "mISDN: %s config port %x-%x already in use\n",
		       "AVM Fritz!PCI",
		       fc->addr,
		       fc->addr + 31);
		return(-EIO);
	} else {
		request_region(fc->addr, 32,
			(fc->type == AVM_FRITZ_PCI) ? "avm PCI" : "avm PnP");
	}
	switch (fc->type) {
	    case AVM_FRITZ_PCI:
		val = inl(fc->addr);
		printk(KERN_INFO "AVM PCI: stat %#x\n", val);
		printk(KERN_INFO "AVM PCI: Class %X Rev %d\n",
			val & 0xff, (val>>8) & 0xff);
		outl(AVM_HDLC_1, fc->addr + CHIP_INDEX);
		ver = inl(fc->addr + CHIP_WINDOW + HDLC_STATUS) >> 24;
		printk(KERN_INFO "AVM PnP: HDLC version %x\n", ver & 0xf);
		fc->dch.read_reg = &ReadISAC;
		fc->dch.write_reg = &WriteISAC;
		fc->dch.read_fifo = &ReadISACfifo;
		fc->dch.write_fifo = &WriteISACfifo;
		fc->dch.type = ISAC_TYPE_ISAC;
		break;
	    case AVM_FRITZ_PCIV2:
		val = inl(fc->addr);
		printk(KERN_INFO "AVM PCI V2: stat %#x\n", val);
		printk(KERN_INFO "AVM PCI V2: Class %X Rev %d\n",
			val & 0xff, (val>>8) & 0xff);
		ver = inl(fc->addr + AVM_HDLC_STATUS_1) >> 24;
		printk(KERN_INFO "AVM PnP: HDLC version %x\n", ver & 0xf);
		fc->dch.read_reg = &fcpci2_read_isac;
		fc->dch.write_reg = &fcpci2_write_isac;
		fc->dch.read_fifo = &fcpci2_read_isac_fifo;
		fc->dch.write_fifo = &fcpci2_write_isac_fifo;
		fc->dch.type = ISAC_TYPE_ISACSX;
		break;
	    case AVM_FRITZ_PNP:
		val = inb(fc->addr);
		ver = inb(fc->addr + 1);
		printk(KERN_INFO "AVM PnP: Class %X Rev %d\n", val, ver);
		outb(AVM_HDLC_1, fc->addr + CHIP_INDEX);
		ver = inb(fc->addr + CHIP_WINDOW + 7);
		printk(KERN_INFO "AVM PnP: HDLC version %x\n", ver & 0xf);
		reset_avmpcipnp(fc);
		fc->dch.read_reg = &ReadISAC;
		fc->dch.write_reg = &WriteISAC;
		fc->dch.read_fifo = &ReadISACfifo;
		fc->dch.write_fifo = &WriteISACfifo;
		fc->dch.type = ISAC_TYPE_ISAC;
		break;
	    default:
	    	release_region(fc->addr, 32);
	  	printk(KERN_WARNING "AVM unknown type %d\n", fc->type);
	  	return(-ENODEV);
	}
	printk(KERN_INFO "mISDN: %s config irq:%d base:0x%X\n",
		(fc->type == AVM_FRITZ_PCI) ? "AVM Fritz!PCI" :
		(fc->type == AVM_FRITZ_PCIV2) ? "AVM Fritz!PCIv2" : "AVM Fritz!PnP",
		fc->irq, fc->addr);

	fc->dch.hw = &fc->isac;
	lock_dev(fc, 0);
#ifdef SPIN_DEBUG
	printk(KERN_ERR "spin_lock_adr=%p now(%p)\n", &fc->lock.busy_adr, fc->lock.busy_adr);
	printk(KERN_ERR "busy_lock_adr=%p now(%p)\n", &fc->lock.busy_adr, fc->lock.busy_adr);
#endif
	unlock_dev(fc);
	return(0);
}

static void
release_card(fritzpnppci *card)
{
#ifdef LOCK_STATISTIC
	printk(KERN_INFO "try_ok(%d) try_wait(%d) try_mult(%d) try_inirq(%d)\n",
		card->lock.try_ok, card->lock.try_wait, card->lock.try_mult, card->lock.try_inirq);
	printk(KERN_INFO "irq_ok(%d) irq_fail(%d)\n",
		card->lock.irq_ok, card->lock.irq_fail);
#endif
	lock_dev(card, 0);
	outb(0, card->addr + 2);
	free_irq(card->irq, card);
	modehdlc(&card->bch[0], 0, ISDN_PID_NONE);
	modehdlc(&card->bch[1], 1, ISDN_PID_NONE);
	ISAC_free(&card->dch);
	release_region(card->addr, 32);
	free_bchannel(&card->bch[1]);
	free_bchannel(&card->bch[0]);
	free_dchannel(&card->dch);
	REMOVE_FROM_LISTBASE(card, ((fritzpnppci *)fritz.ilist));
	unlock_dev(card);
	if (card->type == AVM_FRITZ_PNP) {
		if (card->pdev->deactivate)
			card->pdev->deactivate(card->pdev);
	} else
		pci_disable_device(card->pdev);
	pci_set_drvdata(card->pdev, NULL);
	kfree(card);
	fritz.refcnt--;
}

static int
fritz_manager(void *data, u_int prim, void *arg) {
	fritzpnppci	*card = fritz.ilist;
	mISDNinstance_t	*inst = data;
	struct sk_buff	*skb;
	int		channel = -1;

	if (!data) {
		printk(KERN_ERR "%s: no data prim %x arg %p\n",
			__FUNCTION__, prim, arg);
		return(-EINVAL);
	}
	while(card) {
		if (&card->dch.inst == inst) {
			channel = 2;
			break;
		}
		if (&card->bch[0].inst == inst) {
			channel = 0;
			break;
		}
		if (&card->bch[1].inst == inst) {
			inst = &card->bch[1].inst;
			channel = 1;
			break;
		}
		card = card->next;
	}
	if (channel<0) {
		printk(KERN_ERR "%s: no channel data %p prim %x arg %p\n",
			__FUNCTION__, data, prim, arg);
		return(-EINVAL);
	}

	switch(prim) {
	    case MGR_REGLAYER | CONFIRM:
		if (!card) {
			printk(KERN_WARNING "%s: no card found\n", __FUNCTION__);
			return(-ENODEV);
		}
		break;
	    case MGR_UNREGLAYER | REQUEST:
		if (!card) {
			printk(KERN_WARNING "%s: no card found\n",
				__FUNCTION__);
			return(-ENODEV);
		} else {
			if (channel == 2) {
				inst->down.fdata = &card->dch;
				if ((skb = create_link_skb(PH_CONTROL | REQUEST,
					HW_DEACTIVATE, 0, NULL, 0))) {
					if (ISAC_l1hw(&inst->down, skb))
						dev_kfree_skb(skb);
				}
			} else {
				inst->down.fdata = &card->bch[channel];
				if ((skb = create_link_skb(MGR_DISCONNECT | REQUEST,
					0, 0, NULL, 0))) {
					if (hdlc_down(&inst->down, skb))
						dev_kfree_skb(skb);
				}
			}
			fritz.ctrl(inst->up.peer, MGR_DISCONNECT | REQUEST,
				&inst->up);
			fritz.ctrl(inst, MGR_UNREGLAYER | REQUEST, NULL);
		}
		break;
	    case MGR_RELEASE | INDICATION:
		if (!card) {
			printk(KERN_WARNING "%s: no card found\n",
				__FUNCTION__);
			return(-ENODEV);
		} else {
			if (channel == 2) {
				release_card(card);
			} else {
				fritz.refcnt--;
			}
		}
		break;
	    case MGR_CONNECT | REQUEST:
		if (!card) {
			printk(KERN_WARNING "%s: connect request failed\n",
				__FUNCTION__);
			return(-ENODEV);
		}
		return(ConnectIF(inst, arg));
		break;
	    case MGR_SETIF | REQUEST:
	    case MGR_SETIF | INDICATION:
		if (!card) {
			printk(KERN_WARNING "%s: setif failed\n", __FUNCTION__);
			return(-ENODEV);
		}
		if (channel==2)
			return(SetIF(inst, arg, prim, ISAC_l1hw, NULL,
				&card->dch));
		else
			return(SetIF(inst, arg, prim, hdlc_down, NULL,
				&card->bch[channel]));
		break;
	    case MGR_DISCONNECT | REQUEST:
	    case MGR_DISCONNECT | INDICATION:
		if (!card) {
			printk(KERN_WARNING "%s: del interface request failed\n",
				__FUNCTION__);
			return(-ENODEV);
		}
		return(DisConnectIF(inst, arg));
		break;
	    case MGR_SETSTACK | CONFIRM:
		if (!card) {
			printk(KERN_WARNING "%s: setstack failed\n", __FUNCTION__);
			return(-ENODEV);
		}
		if ((channel!=2) && (inst->pid.global == 2)) {
			inst->down.fdata = &card->bch[channel];
			if ((skb = create_link_skb(PH_ACTIVATE | REQUEST,
				0, 0, NULL, 0))) {
				if (hdlc_down(&inst->down, skb))
					dev_kfree_skb(skb);
			}
			if (inst->pid.protocol[2] == ISDN_PID_L2_B_TRANS)
				if_link(&inst->up, DL_ESTABLISH | INDICATION,
					0, 0, NULL, 0);
			else
				if_link(&inst->up, PH_ACTIVATE | INDICATION,
					0, 0, NULL, 0);
		}
		break;
	    default:
		printk(KERN_WARNING "%s: prim %x not handled\n",
			__FUNCTION__, prim);
		return(-EINVAL);
	}
	return(0);
}

static int __devinit setup_instance(fritzpnppci *card)
{
	int		i, err;
	mISDN_pid_t	pid;
	
	pci_set_drvdata(card->pdev, card);
	APPEND_TO_LIST(card, ((fritzpnppci *)fritz.ilist));
	card->dch.debug = debug;
	card->dch.inst.obj = &fritz;
	lock_HW_init(&card->lock);
	card->dch.inst.lock = lock_dev;
	card->dch.inst.unlock = unlock_dev;
	card->dch.inst.data = card;
	card->dch.inst.pid.layermask = ISDN_LAYER(0);
	card->dch.inst.pid.protocol[0] = ISDN_PID_L0_TE_S0;
	card->dch.inst.up.owner = &card->dch.inst;
	card->dch.inst.down.owner = &card->dch.inst;
	fritz.ctrl(NULL, MGR_DISCONNECT | REQUEST, &card->dch.inst.down);
	sprintf(card->dch.inst.name, "Fritz%d", fritz_cnt+1);
	set_dchannel_pid(&pid, protocol[fritz_cnt], layermask[fritz_cnt]);
	init_dchannel(&card->dch);
	for (i=0; i<2; i++) {
		card->bch[i].channel = i;
		card->bch[i].inst.obj = &fritz;
		card->bch[i].inst.data = card;
		card->bch[i].inst.pid.layermask = ISDN_LAYER(0);
		card->bch[i].inst.up.owner = &card->bch[i].inst;
		card->bch[i].inst.down.owner = &card->bch[i].inst;
		fritz.ctrl(NULL, MGR_DISCONNECT | REQUEST, &card->bch[i].inst.down);
		card->bch[i].inst.lock = lock_dev;
		card->bch[i].inst.unlock = unlock_dev;
		card->bch[i].debug = debug;
		sprintf(card->bch[i].inst.name, "%s B%d", card->dch.inst.name, i+1);
		init_bchannel(&card->bch[i]);
		card->bch[i].hw = &card->hdlc[i];
	}
	printk(KERN_DEBUG "fritz card %p dch %p bch1 %p bch2 %p\n",
		card, &card->dch, &card->bch[0], &card->bch[1]);
	err = setup_fritz(card);
	if (err) {
		free_dchannel(&card->dch);
		free_bchannel(&card->bch[1]);
		free_bchannel(&card->bch[0]);
		REMOVE_FROM_LISTBASE(card, ((fritzpnppci *)fritz.ilist));
		kfree(card);
		return(err);
	}
	fritz_cnt++;
	err = fritz.ctrl(NULL, MGR_NEWSTACK | REQUEST, &card->dch.inst);
	if (err) {
		release_card(card);
		return(err);
	}
	for (i=0; i<2; i++) {
		err = fritz.ctrl(card->dch.inst.st, MGR_NEWSTACK | REQUEST, &card->bch[i].inst);
		if (err) {
			printk(KERN_ERR "MGR_ADDSTACK bchan error %d\n", err);
			fritz.ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
			return(err);
		}
	}
	err = fritz.ctrl(card->dch.inst.st, MGR_SETSTACK | REQUEST, &pid);
	if (err) {
		printk(KERN_ERR  "MGR_SETSTACK REQUEST dch err(%d)\n", err);
		fritz.ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
		return(err);
	}
	err = init_card(card);
	if (err) {
		fritz.ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
		return(err);
	}
	printk(KERN_INFO "fritz %d cards installed\n", fritz_cnt);
	return(0);
}

static int __devinit fritzpci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int		err = -ENOMEM;
	fritzpnppci	*card;

	if (!(card = kmalloc(sizeof(fritzpnppci), GFP_ATOMIC))) {
		printk(KERN_ERR "No kmem for fritzcard\n");
		return(err);
	}
	memset(card, 0, sizeof(fritzpnppci));
	if (pdev->device == PCI_DEVICE_ID_AVM_A1_V2)
		card->type = AVM_FRITZ_PCIV2;
	else
		card->type = AVM_FRITZ_PCI;
	card->pdev = pdev;
	err = pci_enable_device(pdev);
	if (err) {
		kfree(card);
		return(err);
	}

	printk(KERN_INFO "mISDN_fcpcipnp: found adapter %s at %s\n",
	       (char *) ent->driver_data, pdev->slot_name);

	card->addr = pci_resource_start(pdev, 1);
	card->irq = pdev->irq;
	err = setup_instance(card);
	return(err);
}

static int __devinit fritzpnp_probe(struct pci_dev *pdev, const struct isapnp_device_id *ent)
{
	int		err = -ENOMEM;
	fritzpnppci	*card;

	if (!(card = kmalloc(sizeof(fritzpnppci), GFP_ATOMIC))) {
		printk(KERN_ERR "No kmem for fritzcard\n");
		return(err);
	}
	memset(card, 0, sizeof(fritzpnppci));
	card->type = AVM_FRITZ_PNP;
	card->pdev = pdev;
	pdev->prepare(pdev);
	pdev->deactivate(pdev); // why?
	pdev->activate(pdev);
	card->addr = pdev->resource[0].start;
	card->irq = pdev->irq_resource[0].start;

	printk(KERN_INFO "mISDN_fcpcipnp: found adapter %s at IO %#x irq %d\n",
	       (char *) ent->driver_data, card->addr, card->irq);

	err = setup_instance(card);
	return(err);
}

static void __devexit fritz_remove(struct pci_dev *pdev)
{
	fritzpnppci	*card = pci_get_drvdata(pdev);

	if (card)
		fritz.ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
	else
		printk(KERN_WARNING "%s: drvdata allready removed\n", __FUNCTION__);
}

static struct pci_driver fcpci_driver = {
	name:     "fcpci",
	probe:    fritzpci_probe,
	remove:   __devexit_p(fritz_remove),
	id_table: fcpci_ids,
};

static struct isapnp_driver fcpnp_driver = {
	name:     "fcpnp",
	probe:    fritzpnp_probe,
	remove:   __devexit_p(fritz_remove),
	id_table: fcpnp_ids,
};

static int __init Fritz_init(void)
{
	int	err, pci_nr_found;
	char tmp[64];

	strcpy(tmp, avm_fritz_rev);
	printk(KERN_INFO "AVM Fritz PCI/PnP driver Rev. %s\n", mISDN_getrev(tmp));

	SET_MODULE_OWNER(&fritz);
	fritz.name = FritzName;
	fritz.own_ctrl = fritz_manager;
	fritz.DPROTO.protocol[0] = ISDN_PID_L0_TE_S0;
	fritz.BPROTO.protocol[1] = ISDN_PID_L1_B_64TRANS |
				    ISDN_PID_L1_B_64HDLC;
	fritz.BPROTO.protocol[2] = ISDN_PID_L2_B_TRANS;
	fritz.prev = NULL;
	fritz.next = NULL;
	if ((err = mISDN_register(&fritz))) {
		printk(KERN_ERR "Can't register Fritz PCI error(%d)\n", err);
		return(err);
	}
	err = pci_register_driver(&fcpci_driver);
	if (err < 0)
		goto out;
	pci_nr_found = err;

	err = isapnp_register_driver(&fcpnp_driver);
	if (err < 0)
		goto out_unregister_pci;

#if !defined(CONFIG_HOTPLUG) || defined(MODULE)
	if (pci_nr_found + err == 0) {
		err = -ENODEV;
		goto out_unregister_isapnp;
	}
#endif
	return 0;

#if !defined(CONFIG_HOTPLUG) || defined(MODULE)
 out_unregister_isapnp:
	isapnp_unregister_driver(&fcpnp_driver);
#endif
 out_unregister_pci:
	pci_unregister_driver(&fcpci_driver);
 out:
 	return err;
}

static void __exit Fritz_cleanup(void)
{
	int err;
	if ((err = mISDN_unregister(&fritz))) {
		printk(KERN_ERR "Can't unregister Fritz PCI error(%d)\n", err);
	}
	while(fritz.ilist) {
		printk(KERN_ERR "Fritz PCI card struct not empty refs %d\n",
			fritz.refcnt);
		release_card(fritz.ilist);
	}
	isapnp_unregister_driver(&fcpnp_driver);
	pci_unregister_driver(&fcpci_driver);
}

module_init(Fritz_init);
module_exit(Fritz_cleanup);

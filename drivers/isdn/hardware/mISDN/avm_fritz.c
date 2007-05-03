/* $Id: avm_fritz.c,v 1.43 2007/02/13 10:43:45 crich Exp $
 *
 * fritz_pci.c    low level stuff for AVM Fritz!PCI and ISA PnP isdn cards
 *              Thanks to AVM, Berlin for informations
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */
#include <linux/module.h>
#include <linux/pci.h>
#ifdef NEW_ISAPNP
#include <linux/pnp.h>
#else
#include <linux/isapnp.h>
#endif
#include <linux/delay.h>
#include "core.h"
#include "channel.h"
#include "isac.h"
#include "layer1.h"
#include "debug.h"


static const char *avm_fritz_rev = "$Revision: 1.43 $";

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
	u_char fill;
	u_char mode;
	u_char xml;
	u_char cmd;
#else
	u_char cmd;
	u_char xml;
	u_char mode;
	u_char fill;
#endif
} __attribute__((packed));

typedef struct hdlc_hw {
	union {
		u_int ctrl;
		struct hdlc_stat_reg sr;
	} ctrl;
	u_int stat;
} hdlc_hw_t;


typedef struct _fritzpnppci {
	struct list_head	list;
	union {
#if defined(CONFIG_PNP)
#ifdef NEW_ISAPNP
		struct pnp_dev	*pnp;
#else
		struct pci_dev	*pnp;
#endif
#endif
		struct pci_dev	*pci;
	}			dev;
	u_int			type;
	u_int			irq;
	u_int			irqcnt;
	u_int			addr;
	spinlock_t		lock;
	isac_chip_t		isac;
	hdlc_hw_t		hdlc[2];
	channel_t		dch;
	channel_t		bch[2];
	u_char			ctrlreg;
} fritzpnppci;


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
channel_t *Sel_BCS(fritzpnppci *fc, int channel)
{
	if (test_bit(FLG_ACTIVE, &fc->bch[0].Flags) && (fc->bch[0].channel == channel))
		return(&fc->bch[0]);
	else if (test_bit(FLG_ACTIVE, &fc->bch[1].Flags) && (fc->bch[1].channel == channel))
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
	outl(hdlc->ctrl.ctrl, fc->addr + (channel ? AVM_HDLC_STATUS_2 : AVM_HDLC_STATUS_1));
}

void
write_ctrl(channel_t *bch, int which) {
	fritzpnppci	*fc = bch->inst.privat;
	hdlc_hw_t	*hdlc = bch->hw;

	if (fc->dch.debug & L1_DEB_HSCX)
		mISDN_debugprint(&bch->inst, "hdlc %c wr%x ctrl %x",
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
	return inl(addr + (channel ? AVM_HDLC_STATUS_2 : AVM_HDLC_STATUS_1));
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

static void
enable_hwirq(fritzpnppci *fc)
{
	fc->ctrlreg |= AVM_STATUS0_ENA_IRQ;
	outb(fc->ctrlreg, fc->addr + 2);
}

static void
disable_hwirq(fritzpnppci *fc)
{
	fc->ctrlreg &= ~((u_char)AVM_STATUS0_ENA_IRQ);
	outb(fc->ctrlreg, fc->addr + 2);
}

static int
modehdlc(channel_t *bch, int bc, int protocol)
{
	hdlc_hw_t	*hdlc = bch->hw;

	if (bch->debug & L1_DEB_HSCX)
		mISDN_debugprint(&bch->inst, "hdlc %c protocol %x-->%x ch %d-->%d",
			'A' + bch->channel, bch->state, protocol, bch->channel, bc);
	if ((protocol != -1) && (bc != bch->channel))
		printk(KERN_WARNING "%s: fritzcard mismatch channel(%d/%d)\n", __FUNCTION__, bch->channel, bc);
	hdlc->ctrl.ctrl = 0;
	switch (protocol) {
		case (-1): /* used for init */
			bch->state = -1;
			bch->channel = bc;
		case (ISDN_PID_NONE):
			if (bch->state == ISDN_PID_NONE)
				break;
			hdlc->ctrl.sr.cmd  = HDLC_CMD_XRS | HDLC_CMD_RRS;
			hdlc->ctrl.sr.mode = HDLC_MODE_TRANS;
			write_ctrl(bch, 5);
			bch->state = ISDN_PID_NONE;
			test_and_clear_bit(FLG_HDLC, &bch->Flags);
			test_and_clear_bit(FLG_TRANSPARENT, &bch->Flags);
			break;
		case (ISDN_PID_L1_B_64TRANS):
			bch->state = protocol;
			hdlc->ctrl.sr.cmd  = HDLC_CMD_XRS | HDLC_CMD_RRS;
			hdlc->ctrl.sr.mode = HDLC_MODE_TRANS;
			write_ctrl(bch, 5);
			hdlc->ctrl.sr.cmd = HDLC_CMD_XRS;
			write_ctrl(bch, 1);
			hdlc->ctrl.sr.cmd = 0;
			test_and_set_bit(FLG_TRANSPARENT, &bch->Flags);
			break;
		case (ISDN_PID_L1_B_64HDLC):
			bch->state = protocol;
			hdlc->ctrl.sr.cmd  = HDLC_CMD_XRS | HDLC_CMD_RRS;
			hdlc->ctrl.sr.mode = HDLC_MODE_ITF_FLG;
			write_ctrl(bch, 5);
			hdlc->ctrl.sr.cmd = HDLC_CMD_XRS;
			write_ctrl(bch, 1);
			hdlc->ctrl.sr.cmd = 0;
			test_and_set_bit(FLG_HDLC, &bch->Flags);
			break;
		default:
			mISDN_debugprint(&bch->inst, "prot not known %x", protocol);
			return(-ENOPROTOOPT);
	}
	return(0);
}

static void
hdlc_empty_fifo(channel_t *bch, int count)
{
	register u_int *ptr;
	u_char *p;
	u_char idx = bch->channel ? AVM_HDLC_2 : AVM_HDLC_1;
	int cnt=0;
	fritzpnppci *fc = bch->inst.privat;

	if ((fc->dch.debug & L1_DEB_HSCX) && !(fc->dch.debug & L1_DEB_HSCX_FIFO))
		mISDN_debugprint(&bch->inst, "hdlc_empty_fifo %d", count);
	if (!bch->rx_skb) {
		if (!(bch->rx_skb = alloc_stack_skb(bch->maxlen, bch->up_headerlen))) {
			printk(KERN_WARNING "mISDN: B receive out of memory\n");
			return;
		}
	}
	if ((bch->rx_skb->len + count) > bch->maxlen) {
		if (bch->debug & L1_DEB_WARN)
			mISDN_debugprint(&bch->inst, "hdlc_empty_fifo overrun %d",
				bch->rx_skb->len + count);
		return;
	}
	p = skb_put(bch->rx_skb, count);
	ptr = (u_int *)p;
	if (fc->type == AVM_FRITZ_PCIV2) {
		while (cnt < count) {
#ifdef __powerpc__
#ifdef CONFIG_APUS
			*ptr++ = in_le32((unsigned *)(fc->addr + (bch->channel ? AVM_HDLC_FIFO_2 : AVM_HDLC_FIFO_1) +_IO_BASE));
#else
			*ptr++ = in_be32((unsigned *)(fc->addr + (bch->channel ? AVM_HDLC_FIFO_2 : AVM_HDLC_FIFO_1) +_IO_BASE));
#endif /* CONFIG_APUS */
#else
			*ptr++ = inl(fc->addr + (bch->channel ? AVM_HDLC_FIFO_2 : AVM_HDLC_FIFO_1));
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
		char *t = bch->log;

		if (fc->type == AVM_FRITZ_PNP)
			p = (u_char *) ptr;
		t += sprintf(t, "hdlc_empty_fifo %c cnt %d",
			     bch->channel ? 'B' : 'A', count);
		mISDN_QuickHex(t, p, count);
		mISDN_debugprint(&bch->inst, bch->log);
	}
}

#define HDLC_FIFO_SIZE	32

static void
hdlc_fill_fifo(channel_t *bch)
{
	fritzpnppci	*fc = bch->inst.privat;
	hdlc_hw_t	*hdlc = bch->hw;
	int		count, cnt =0;
	u_char		*p;
	u_int		*ptr;

	if ((bch->debug & L1_DEB_HSCX) && !(bch->debug & L1_DEB_HSCX_FIFO))
		mISDN_debugprint(&bch->inst, "%s", __FUNCTION__);
	if (!bch->tx_skb)
		return;
	count = bch->tx_skb->len - bch->tx_idx;
	if (count <= 0)
		return;
	p = bch->tx_skb->data + bch->tx_idx;
	hdlc->ctrl.sr.cmd &= ~HDLC_CMD_XME;
	if (count > HDLC_FIFO_SIZE) {
		count = HDLC_FIFO_SIZE;
	} else {
		if (test_bit(FLG_HDLC, &bch->Flags))
			hdlc->ctrl.sr.cmd |= HDLC_CMD_XME;
	}
	if ((bch->debug & L1_DEB_HSCX) && !(bch->debug & L1_DEB_HSCX_FIFO))
		mISDN_debugprint(&bch->inst, "%s: %d/%d", __FUNCTION__,
			count, bch->tx_idx);
	ptr = (u_int *) p;
	bch->tx_idx += count;
	hdlc->ctrl.sr.xml = ((count == HDLC_FIFO_SIZE) ? 0 : count);
	if (fc->type == AVM_FRITZ_PCIV2) {
		__write_ctrl_pciv2(fc, hdlc, bch->channel);
		while (cnt<count) {
#ifdef __powerpc__
#ifdef CONFIG_APUS
			out_le32((unsigned *)(fc->addr + (bch->channel ? AVM_HDLC_FIFO_2 : AVM_HDLC_FIFO_1) +_IO_BASE), *ptr++);
#else
			out_be32((unsigned *)(fc->addr + (bch->channel ? AVM_HDLC_FIFO_2 : AVM_HDLC_FIFO_1) +_IO_BASE), *ptr++);
#endif /* CONFIG_APUS */
#else
			outl(*ptr++, fc->addr + (bch->channel ? AVM_HDLC_FIFO_2 : AVM_HDLC_FIFO_1));
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
		char *t = bch->log;

		if (fc->type == AVM_FRITZ_PNP)
			p = (u_char *) ptr;
		t += sprintf(t, "hdlc_fill_fifo %c cnt %d",
			     bch->channel ? 'B' : 'A', count);
		mISDN_QuickHex(t, p, count);
		mISDN_debugprint(&bch->inst, bch->log);
	}
}

static void
HDLC_irq_xpr(channel_t *bch)
{
	if (bch->tx_skb && bch->tx_idx < bch->tx_skb->len)
		hdlc_fill_fifo(bch);
	else {
		if (bch->tx_skb)
			dev_kfree_skb(bch->tx_skb);
		bch->tx_idx = 0;
		if (test_bit(FLG_TX_NEXT, &bch->Flags)) {
			bch->tx_skb = bch->next_skb;
			if (bch->tx_skb) {
				mISDN_head_t	*hh = mISDN_HEAD_P(bch->tx_skb);

				bch->next_skb = NULL;
				test_and_clear_bit(FLG_TX_NEXT, &bch->Flags);
				queue_ch_frame(bch, CONFIRM, hh->dinfo, NULL);
				hdlc_fill_fifo(bch);
			} else {
				printk(KERN_WARNING "hdlc tx irq TX_NEXT without skb\n");
				test_and_clear_bit(FLG_TX_NEXT, &bch->Flags);
				test_and_clear_bit(FLG_TX_BUSY, &bch->Flags);
			}
		} else {
			bch->tx_skb = NULL;
			test_and_clear_bit(FLG_TX_BUSY, &bch->Flags);
		}
	}
}

static void
HDLC_irq(channel_t *bch, u_int stat)
{
	int		len;
	struct sk_buff	*skb;
	hdlc_hw_t	*hdlc = bch->hw;

	if (bch->debug & L1_DEB_HSCX)
		mISDN_debugprint(&bch->inst, "ch%d stat %#x", bch->channel, stat);
	if (stat & HDLC_INT_RPR) {
		if (stat & HDLC_STAT_RDO) {
			if (bch->debug & L1_DEB_HSCX)
				mISDN_debugprint(&bch->inst, "RDO");
			else
				mISDN_debugprint(&bch->inst, "ch%d stat %#x", bch->channel, stat);
			hdlc->ctrl.sr.xml = 0;
			hdlc->ctrl.sr.cmd |= HDLC_CMD_RRS;
			write_ctrl(bch, 1);
			hdlc->ctrl.sr.cmd &= ~HDLC_CMD_RRS;
			write_ctrl(bch, 1);
			if (bch->rx_skb)
				skb_trim(bch->rx_skb, 0);
		} else {
			if (!(len = (stat & HDLC_STAT_RML_MASK)>>8))
				len = 32;
			hdlc_empty_fifo(bch, len);
			if (!bch->rx_skb)
				goto handle_tx;
			if ((stat & HDLC_STAT_RME) || test_bit(FLG_TRANSPARENT, &bch->Flags)) {
				if (((stat & HDLC_STAT_CRCVFRRAB)==HDLC_STAT_CRCVFR) ||
					test_bit(FLG_TRANSPARENT, &bch->Flags)) {
					if (bch->rx_skb->len < MISDN_COPY_SIZE) {
						skb = alloc_stack_skb(bch->rx_skb->len, bch->up_headerlen);
						if (skb) {
							memcpy(skb_put(skb, bch->rx_skb->len),
								bch->rx_skb->data, bch->rx_skb->len);
							skb_trim(bch->rx_skb, 0);
						} else {
							skb = bch->rx_skb;
							bch->rx_skb = NULL;
						}
					} else {
						skb = bch->rx_skb;
						bch->rx_skb = NULL;
					}
					queue_ch_frame(bch, INDICATION, MISDN_ID_ANY, skb);
				} else {
					if (bch->debug & L1_DEB_HSCX)
						mISDN_debugprint(&bch->inst, "invalid frame");
					else
						mISDN_debugprint(&bch->inst, "ch%d invalid frame %#x", bch->channel, stat);
					skb_trim(bch->rx_skb, 0);
				}
			}
		}
	}
handle_tx:
	if (stat & HDLC_INT_XDU) {
		/* Here we lost an TX interrupt, so
		 * restart transmitting the whole frame on HDLC
		 * in transparent mode we send the next data
		 */
		if (bch->debug & L1_DEB_WARN) {
			if (bch->tx_skb)
				mISDN_debugprint(&bch->inst, "ch%d XDU tx_len(%d) tx_idx(%d) Flags(%lx)",
					bch->channel, bch->tx_skb->len, bch->tx_idx, bch->Flags);
			else
				mISDN_debugprint(&bch->inst, "ch%d XDU no tx_skb Flags(%lx)",
					bch->channel, bch->Flags);
		}
		if (bch->tx_skb && bch->tx_skb->len) {
			if (!test_bit(FLG_TRANSPARENT, &bch->Flags))
				bch->tx_idx = 0;
		}
		hdlc->ctrl.sr.xml = 0;
		hdlc->ctrl.sr.cmd |= HDLC_CMD_XRS;
		write_ctrl(bch, 1);
		hdlc->ctrl.sr.cmd &= ~HDLC_CMD_XRS;
		HDLC_irq_xpr(bch);
		return;
	} else if (stat & HDLC_INT_XPR)
		HDLC_irq_xpr(bch);
}

static inline void
HDLC_irq_main(fritzpnppci *fc)
{
	u_int stat;
	channel_t *bch;

	stat = read_status(fc, 0);
	if (stat & HDLC_INT_MASK) {
		if (!(bch = Sel_BCS(fc, 0))) {
			if (fc->bch[0].debug)
				mISDN_debugprint(&fc->bch[0].inst, "hdlc spurious channel 0 IRQ");
		} else
			HDLC_irq(bch, stat);
	}
	stat = read_status(fc, 1);
	if (stat & HDLC_INT_MASK) {
		if (!(bch = Sel_BCS(fc, 1))) {
			if (fc->bch[1].debug)
				mISDN_debugprint(&fc->bch[1].inst, "hdlc spurious channel 1 IRQ");
		} else
			HDLC_irq(bch, stat);
	}
}

static irqreturn_t
avm_fritz_interrupt(int intno, void *dev_id
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
, struct pt_regs *regs
#endif
)
{
	fritzpnppci	*fc = dev_id;
	u_char val;
	u_char sval;

	spin_lock(&fc->lock);
	sval = inb(fc->addr + 2);
	if (fc->dch.debug & L1_DEB_INTSTAT)
		mISDN_debugprint(&fc->dch.inst, "irq stat0 %x", sval);
	if ((sval & AVM_STATUS0_IRQ_MASK) == AVM_STATUS0_IRQ_MASK) {
		/* possible a shared  IRQ reqest */
		spin_unlock(&fc->lock);
		return IRQ_NONE;
	}
	fc->irqcnt++;

	if (!(sval & AVM_STATUS0_IRQ_ISAC)) {
		val = ReadISAC(fc, ISAC_ISTA);
		mISDN_isac_interrupt(&fc->dch, val);
	}
	if (!(sval & AVM_STATUS0_IRQ_HDLC)) {
		HDLC_irq_main(fc);
	}
	if (fc->type == AVM_FRITZ_PNP) {
		WriteISAC(fc, ISAC_MASK, 0xFF);
		WriteISAC(fc, ISAC_MASK, 0x0);
	}
	spin_unlock(&fc->lock);
	return IRQ_HANDLED;
}

static irqreturn_t
avm_fritzv2_interrupt(int intno, void *dev_id
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
, struct pt_regs *regs
#endif
)
{
	fritzpnppci	*fc = dev_id;
	u_char val;
	u_char sval;

	spin_lock(&fc->lock);
	sval = inb(fc->addr + 2);
	if (fc->dch.debug & L1_DEB_INTSTAT)
		mISDN_debugprint(&fc->dch.inst, "irq stat0 %x", sval);
	if (!(sval & AVM_STATUS0_IRQ_MASK)) {
		/* possible a shared  IRQ reqest */
		spin_unlock(&fc->lock);
		return IRQ_NONE;
	}
	fc->irqcnt++;

	if (sval & AVM_STATUS0_IRQ_HDLC) {
		HDLC_irq_main(fc);
	}
	if (sval & AVM_STATUS0_IRQ_ISAC) {
		val = fcpci2_read_isac(fc, ISACSX_ISTA);
		mISDN_isac_interrupt(&fc->dch, val);
	}
	if (sval & AVM_STATUS0_IRQ_TIMER) {
		if (fc->dch.debug & L1_DEB_INTSTAT)
			mISDN_debugprint(&fc->dch.inst, "Fc2 timer irq");
		outb(fc->ctrlreg | AVM_STATUS0_RES_TIMER, fc->addr + 2);
		udelay(1);
		outb(fc->ctrlreg, fc->addr + 2);
	}
	spin_unlock(&fc->lock);
	return IRQ_HANDLED;
}

static int
hdlc_down(mISDNinstance_t *inst, struct sk_buff *skb)
{
	channel_t	*bch;
	int		ret = 0;
	mISDN_head_t	*hh = mISDN_HEAD_P(skb);
	u_long		flags;

	bch = container_of(inst, channel_t, inst);
	if ((hh->prim == PH_DATA_REQ) || (hh->prim == DL_DATA_REQ)) {
		spin_lock_irqsave(inst->hwlock, flags);
		ret = channel_senddata(bch, hh->dinfo, skb);
		if (ret > 0) { /* direct TX */
			hdlc_fill_fifo(bch);
			ret = 0;
		}
		spin_unlock_irqrestore(inst->hwlock, flags);
		return(ret);
	} 
	if ((hh->prim == (PH_ACTIVATE | REQUEST)) ||
		(hh->prim == (DL_ESTABLISH  | REQUEST))) {
		if (!test_and_set_bit(FLG_ACTIVE, &bch->Flags)) {
			spin_lock_irqsave(inst->hwlock, flags);
			ret = modehdlc(bch, bch->channel,
				bch->inst.pid.protocol[1]);
			spin_unlock_irqrestore(inst->hwlock, flags);
		}
		skb_trim(skb, 0);
		return(mISDN_queueup_newhead(inst, 0, hh->prim | CONFIRM, ret, skb));
	} else if ((hh->prim == (PH_DEACTIVATE | REQUEST)) ||
		(hh->prim == (DL_RELEASE | REQUEST)) ||
		((hh->prim == (PH_CONTROL | REQUEST) && (hh->dinfo == HW_DEACTIVATE)))) {
		spin_lock_irqsave(inst->hwlock, flags);
		if (test_and_clear_bit(FLG_TX_NEXT, &bch->Flags)) {
			dev_kfree_skb(bch->next_skb);
			bch->next_skb = NULL;
		}
		if (bch->tx_skb) {
			dev_kfree_skb(bch->tx_skb);
			bch->tx_skb = NULL;
			bch->tx_idx = 0;
		}
		test_and_clear_bit(FLG_TX_BUSY, &bch->Flags);
		modehdlc(bch, bch->channel, 0);
		test_and_clear_bit(FLG_ACTIVE, &bch->Flags);
		spin_unlock_irqrestore(inst->hwlock, flags);
		skb_trim(skb, 0);
		if (hh->prim != (PH_CONTROL | REQUEST))
			if (!mISDN_queueup_newhead(inst, 0, hh->prim | CONFIRM, 0, skb))
				return(0);
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
	mISDN_debugprint(&fc->dch.inst, "HDLC 1 STA %x", val);
	val = read_status(fc, 1);
	mISDN_debugprint(&fc->dch.inst, "HDLC 2 STA %x", val);
}

static void
reset_avmpcipnp(fritzpnppci *fc)
{
	switch (fc->type) {
		case AVM_FRITZ_PNP:
		case AVM_FRITZ_PCI:
			fc->ctrlreg = AVM_STATUS0_RESET | AVM_STATUS0_DIS_TIMER;
			break;
		case AVM_FRITZ_PCIV2:
			fc->ctrlreg = AVM_STATUS0_RESET;
			break;
	}
	printk(KERN_INFO "AVM PCI/PnP: reset\n");
	disable_hwirq(fc);
	mdelay(5);
	switch (fc->type) {
		case AVM_FRITZ_PNP:
			fc->ctrlreg = AVM_STATUS0_DIS_TIMER | AVM_STATUS0_RES_TIMER;
			disable_hwirq(fc);
			outb(AVM_STATUS1_ENA_IOM | fc->irq, fc->addr + 3);
			break;
		case AVM_FRITZ_PCI:
			fc->ctrlreg = AVM_STATUS0_DIS_TIMER | AVM_STATUS0_RES_TIMER;
			disable_hwirq(fc);
			outb(AVM_STATUS1_ENA_IOM, fc->addr + 3);
			break;
		case AVM_FRITZ_PCIV2:
			fc->ctrlreg = 0;
			disable_hwirq(fc);
			break;
	}
	mdelay(1);
	printk(KERN_INFO "AVM PCI/PnP: S0/S1 %x/%x\n", inb(fc->addr + 2), inb(fc->addr + 3));
}

static int init_card(fritzpnppci *fc)
{
	int		cnt = 3;
	u_int		shared = SA_SHIRQ;
	u_long		flags;
	u_char		*id = "AVM Fritz!PCI";

	if (fc->type == AVM_FRITZ_PNP) {
		shared = 0;
		id = "AVM Fritz!PnP";
	}
	reset_avmpcipnp(fc); /* disable IRQ */
	if (fc->type == AVM_FRITZ_PCIV2) {
		if (request_irq(fc->irq, avm_fritzv2_interrupt, shared, id, fc)) {
			printk(KERN_WARNING "mISDN: couldn't get interrupt %d\n",
				fc->irq);
			return(-EIO);
		}
	} else {
		if (request_irq(fc->irq, avm_fritz_interrupt, shared, id, fc)) {
			printk(KERN_WARNING "mISDN: couldn't get interrupt %d\n",
				fc->irq);
			return(-EIO);
		}
	}
	while (cnt) {
		int	ret;

		spin_lock_irqsave(&fc->lock, flags);
		mISDN_clear_isac(&fc->dch);
		if ((ret=mISDN_isac_init(&fc->dch))) {
			printk(KERN_WARNING "mISDN: mISDN_isac_init failed with %d\n", ret);
			spin_unlock_irqrestore(&fc->lock, flags);
			break;
		}
		clear_pending_hdlc_ints(fc);
		inithdlc(fc);
		WriteISAC(fc, ISAC_MASK, 0);
		enable_hwirq(fc);
		/* RESET Receiver and Transmitter */
		WriteISAC(fc, ISAC_CMDR, 0x41);
		spin_unlock_irqrestore(&fc->lock, flags);
		/* Timeout 10ms */
		current->state = TASK_UNINTERRUPTIBLE;
		schedule_timeout((10*HZ)/1000);
		printk(KERN_INFO "AVM Fritz!PCI: IRQ %d count %d\n",
			fc->irq, fc->irqcnt);
		if (!fc->irqcnt) {
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
	}
	return(-EIO);
}

#define MAX_CARDS	4
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

#ifdef OLD_MODULE_PARAM
MODULE_PARM(debug, "1i");
#define MODULE_PARM_T   "1-4i"
MODULE_PARM(protocol, MODULE_PARM_T);
MODULE_PARM(layermask, MODULE_PARM_T);
#else
module_param(debug, uint, S_IRUGO | S_IWUSR);

#ifdef OLD_MODULE_PARAM_ARRAY
static int num_protocol=0,num_layermask=0;
module_param_array(protocol, uint, num_protocol, S_IRUGO | S_IWUSR);
module_param_array(layermask, uint, num_layermask, S_IRUGO | S_IWUSR);
#else
module_param_array(protocol, uint, NULL, S_IRUGO | S_IWUSR);
module_param_array(layermask, uint, NULL, S_IRUGO | S_IWUSR);
#endif
#endif

#endif

int
setup_fritz(fritzpnppci *fc)
{
	u_int	val, ver;

	if (!request_region(fc->addr, 32, (fc->type == AVM_FRITZ_PCI) ? "avm PCI" : "avm PnP")) {
		printk(KERN_WARNING
		       "mISDN: %s config port %x-%x already in use\n",
		       "AVM Fritz!PCI",
		       fc->addr,
		       fc->addr + 31);
		return(-EIO);
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
	return(0);
}

static void
release_card(fritzpnppci *card)
{
	u_long		flags;

	disable_hwirq(card);
	spin_lock_irqsave(&card->lock, flags);
	modehdlc(&card->bch[0], 0, ISDN_PID_NONE);
	modehdlc(&card->bch[1], 1, ISDN_PID_NONE);
	mISDN_isac_free(&card->dch);
	spin_unlock_irqrestore(&card->lock, flags);
	free_irq(card->irq, card);
	spin_lock_irqsave(&card->lock, flags);
	release_region(card->addr, 32);
	mISDN_freechannel(&card->bch[1]);
	mISDN_freechannel(&card->bch[0]);
	mISDN_freechannel(&card->dch);
	spin_unlock_irqrestore(&card->lock, flags);
	mISDN_ctrl(&card->dch.inst, MGR_UNREGLAYER | REQUEST, NULL);
	spin_lock_irqsave(&fritz.lock, flags);
	list_del(&card->list);
	spin_unlock_irqrestore(&fritz.lock, flags);
	if (card->type == AVM_FRITZ_PNP) {
#if defined(CONFIG_PNP)
		pnp_disable_dev(card->dev.pnp);
		pnp_set_drvdata(card->dev.pnp, NULL);
#endif
	} else {
		pci_disable_device(card->dev.pci);
		pci_set_drvdata(card->dev.pci, NULL);
	}
	kfree(card);
}

static int
fritz_manager(void *data, u_int prim, void *arg) {
	fritzpnppci	*card;
	mISDNinstance_t	*inst = data;
	struct sk_buff	*skb;
	u_long		flags;
	int		channel = -1;

	if (debug & 0x10000)
		printk(KERN_DEBUG "%s: data(%p) prim(%x) arg(%p)\n",
			__FUNCTION__, data, prim, arg);
	if (!data) {
		MGR_HASPROTOCOL_HANDLER(prim,arg,&fritz)
		printk(KERN_ERR "%s: no data prim %x arg %p\n",
			__FUNCTION__, prim, arg);
		return(-EINVAL);
	}
	spin_lock_irqsave(&fritz.lock, flags);
	list_for_each_entry(card, &fritz.ilist, list) {
		if (&card->dch.inst == inst) {
			channel = 2;
			break;
		}
		if (&card->bch[0].inst == inst) {
			channel = 0;
			break;
		}
		if (&card->bch[1].inst == inst) {
			channel = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&fritz.lock, flags);
	if (channel<0) {
		printk(KERN_WARNING "%s: no channel data %p prim %x arg %p\n",
			__FUNCTION__, data, prim, arg);
		return(-EINVAL);
	}

	switch(prim) {
	    case MGR_REGLAYER | CONFIRM:
		if (channel == 2)
			mISDN_setpara(&card->dch, &inst->st->para);
		else
			mISDN_setpara(&card->bch[channel], &inst->st->para);
		break;
	    case MGR_UNREGLAYER | REQUEST:
	    	if ((skb = create_link_skb(PH_CONTROL | REQUEST,
	    		HW_DEACTIVATE, 0, NULL, 0))) {
			if (channel == 2) {
				if (mISDN_ISAC_l1hw(inst, skb))
					dev_kfree_skb(skb);
			} else {
				if (hdlc_down(inst, skb))
					dev_kfree_skb(skb);
			}
		} else
			printk(KERN_WARNING "no SKB in %s MGR_UNREGLAYER | REQUEST\n", __FUNCTION__);
		mISDN_ctrl(inst, MGR_UNREGLAYER | REQUEST, NULL);
		break;
	    case MGR_CLRSTPARA | INDICATION:
		arg = NULL;
	    case MGR_ADDSTPARA | INDICATION:
		if (channel == 2)
			mISDN_setpara(&card->dch, arg);
		else
			mISDN_setpara(&card->bch[channel], arg);
		break;
	    case MGR_RELEASE | INDICATION:
		if (channel == 2) {
			release_card(card);
		} else {
			fritz.refcnt--;
		}
		break;
	    case MGR_SETSTACK | INDICATION:
		if ((channel!=2) && (inst->pid.global == 2)) {
			if ((skb = create_link_skb(PH_ACTIVATE | REQUEST,
				0, 0, NULL, 0))) {
				if (hdlc_down(inst, skb))
					dev_kfree_skb(skb);
			}
			if (inst->pid.protocol[2] == ISDN_PID_L2_B_TRANS)
				mISDN_queue_data(inst, FLG_MSG_UP, DL_ESTABLISH | INDICATION,
					0, 0, NULL, 0);
			else
				mISDN_queue_data(inst, FLG_MSG_UP, PH_ACTIVATE | INDICATION,
					0, 0, NULL, 0);
		}
		break;
	    PRIM_NOT_HANDLED(MGR_CTRLREADY | INDICATION);
	    PRIM_NOT_HANDLED(MGR_GLOBALOPT | REQUEST);
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
	u_long		flags;
	struct device	*dev;

	if (card->type == AVM_FRITZ_PNP) {
#if defined(CONFIG_PNP)
		dev = &card->dev.pnp->dev;
#else
		dev = NULL;
#endif
	} else {
		dev = &card->dev.pci->dev;
	}
	spin_lock_irqsave(&fritz.lock, flags);
	list_add_tail(&card->list, &fritz.ilist);
	spin_unlock_irqrestore(&fritz.lock, flags);
	card->dch.debug = debug;
	spin_lock_init(&card->lock);
	card->dch.inst.hwlock = &card->lock;
	card->dch.inst.class_dev.dev = dev;
	card->dch.inst.pid.layermask = ISDN_LAYER(0);
	card->dch.inst.pid.protocol[0] = ISDN_PID_L0_TE_S0;
	mISDN_init_instance(&card->dch.inst, &fritz, card, mISDN_ISAC_l1hw);
	sprintf(card->dch.inst.name, "Fritz%d", fritz_cnt+1);
	mISDN_set_dchannel_pid(&pid, protocol[fritz_cnt], layermask[fritz_cnt]);
	mISDN_initchannel(&card->dch, MSK_INIT_DCHANNEL, MAX_DFRAME_LEN_L1);
	for (i=0; i<2; i++) {
		card->bch[i].channel = i;
		mISDN_init_instance(&card->bch[i].inst, &fritz, card, hdlc_down);
		card->bch[i].inst.pid.layermask = ISDN_LAYER(0);
		card->bch[i].inst.hwlock = &card->lock;
		card->bch[i].inst.class_dev.dev = dev;
		card->bch[i].debug = debug;
		sprintf(card->bch[i].inst.name, "%s B%d", card->dch.inst.name, i+1);
		mISDN_initchannel(&card->bch[i], MSK_INIT_BCHANNEL, MAX_DATA_MEM);
		card->bch[i].hw = &card->hdlc[i];
	}
	printk(KERN_DEBUG "fritz card %p dch %p bch1 %p bch2 %p\n",
		card, &card->dch, &card->bch[0], &card->bch[1]);
	err = setup_fritz(card);
	if (err) {
		mISDN_freechannel(&card->dch);
		mISDN_freechannel(&card->bch[1]);
		mISDN_freechannel(&card->bch[0]);
		spin_lock_irqsave(&fritz.lock, flags);
		list_del(&card->list);
		spin_unlock_irqrestore(&fritz.lock, flags);
		kfree(card);
		return(err);
	}
	fritz_cnt++;
	err = mISDN_ctrl(NULL, MGR_NEWSTACK | REQUEST, &card->dch.inst);
	if (err) {
		release_card(card);
		return(err);
	}
	for (i=0; i<2; i++) {
		err = mISDN_ctrl(card->dch.inst.st, MGR_NEWSTACK | REQUEST, &card->bch[i].inst);
		if (err) {
			printk(KERN_ERR "MGR_ADDSTACK bchan error %d\n", err);
			mISDN_ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
			return(err);
		}
	}
	err = mISDN_ctrl(card->dch.inst.st, MGR_SETSTACK | REQUEST, &pid);
	if (err) {
		printk(KERN_ERR  "MGR_SETSTACK REQUEST dch err(%d)\n", err);
		mISDN_ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
		return(err);
	}
	err = init_card(card);
	if (err) {
		mISDN_ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
		return(err);
	}
	mISDN_ctrl(card->dch.inst.st, MGR_CTRLREADY | INDICATION, NULL);
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
	card->dev.pci = pdev;
	err = pci_enable_device(pdev);
	if (err) {
		kfree(card);
		return(err);
	}

	printk(KERN_INFO "mISDN_fcpcipnp: found adapter %s at %s\n",
	       (char *) ent->driver_data, pci_name(pdev));

	card->addr = pci_resource_start(pdev, 1);
	card->irq = pdev->irq;
	pci_set_drvdata(pdev, card);
	err = setup_instance(card);
	if (err)
		pci_set_drvdata(pdev, NULL);
	return(err);
}

#if defined(CONFIG_PNP)
#ifdef NEW_ISAPNP
static int __devinit fritzpnp_probe(struct pnp_dev *pdev, const struct pnp_device_id *dev_id)
#else
static int __devinit fritzpnp_probe(struct pci_dev *pdev, const struct isapnp_device_id *dev_id)
#endif
{
	int		err;
	fritzpnppci	*card;

	if (!pdev)
		return(-ENODEV);

	if (!(card = kmalloc(sizeof(fritzpnppci), GFP_ATOMIC))) {
		printk(KERN_ERR "No kmem for fritzcard\n");
		return(-ENOMEM);
	}
	memset(card, 0, sizeof(fritzpnppci));
	card->type = AVM_FRITZ_PNP;
	card->dev.pnp = pdev;
	pnp_disable_dev(pdev);
	err = pnp_activate_dev(pdev);
	if (err<0) {
		printk(KERN_WARNING "%s: pnp_activate_dev(%s) ret(%d)\n", __FUNCTION__,
			(char *)dev_id->driver_data, err);
		kfree(card);
		return(err);
	}
	card->addr = pnp_port_start(pdev, 0);
	card->irq = pnp_irq(pdev, 0);

	printk(KERN_INFO "mISDN_fcpcipnp: found adapter %s at IO %#x irq %d\n",
	       (char *)dev_id->driver_data, card->addr, card->irq);

	pnp_set_drvdata(pdev, card);
	err = setup_instance(card);
	if (err)
		pnp_set_drvdata(pdev, NULL);
	return(err);
}
#endif /* CONFIG_PNP */

static void __devexit fritz_remove_pci(struct pci_dev *pdev)
{
	fritzpnppci	*card = pci_get_drvdata(pdev);

	if (card)
		mISDN_ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
	else
		if (debug)
			printk(KERN_WARNING "%s: drvdata allready removed\n", __FUNCTION__);
}

#if defined(CONFIG_PNP)
#ifdef NEW_ISAPNP
static void __devexit fritz_remove_pnp(struct pnp_dev *pdev)
#else
static void __devexit fritz_remove_pnp(struct pci_dev *pdev)
#endif
{
	fritzpnppci	*card = pnp_get_drvdata(pdev);

	if (card)
		mISDN_ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
	else
		if (debug)
			printk(KERN_WARNING "%s: drvdata allready removed\n", __FUNCTION__);
}
#endif /* CONFIG_PNP */

static struct pci_device_id fcpci_ids[] __devinitdata = {
	{ PCI_VENDOR_ID_AVM, PCI_DEVICE_ID_AVM_A1   , PCI_ANY_ID, PCI_ANY_ID,
	  0, 0, (unsigned long) "Fritz!Card PCI" },
	{ PCI_VENDOR_ID_AVM, PCI_DEVICE_ID_AVM_A1_V2, PCI_ANY_ID, PCI_ANY_ID,
	  0, 0, (unsigned long) "Fritz!Card PCI v2" },
	{ }
};
MODULE_DEVICE_TABLE(pci, fcpci_ids);

static struct pci_driver fcpci_driver = {
	name:     "fcpci",
	probe:    fritzpci_probe,
	remove:   __devexit_p(fritz_remove_pci),
	id_table: fcpci_ids,
};

#if defined(CONFIG_PNP)
#ifdef NEW_ISAPNP
static struct pnp_device_id fcpnp_ids[] __devinitdata = {
	{ 
		.id		= "AVM0900",
		.driver_data	= (unsigned long) "Fritz!Card PnP",
	},
};

static struct pnp_driver fcpnp_driver = {
#else
static struct isapnp_device_id fcpnp_ids[] __devinitdata = {
	{ ISAPNP_VENDOR('A', 'V', 'M'), ISAPNP_FUNCTION(0x0900),
	  ISAPNP_VENDOR('A', 'V', 'M'), ISAPNP_FUNCTION(0x0900), 
	  (unsigned long) "Fritz!Card PnP" },
	{ }
};
MODULE_DEVICE_TABLE(isapnp, fcpnp_ids);

static struct isapnp_driver fcpnp_driver = {
#endif
	name:     "fcpnp",
	probe:    fritzpnp_probe,
	remove:   __devexit_p(fritz_remove_pnp),
	id_table: fcpnp_ids,
};
#endif /* CONFIG_PNP */

static char FritzName[] = "AVM Fritz";

static int __init Fritz_init(void)
{
	int	err;
#ifdef OLD_PCI_REGISTER_DRIVER
	int	pci_nr_found;
#endif

	printk(KERN_INFO "AVM Fritz PCI/PnP driver Rev. %s\n", mISDN_getrev(avm_fritz_rev));
#ifdef MODULE
	fritz.owner = THIS_MODULE;
#endif
	spin_lock_init(&fritz.lock);
	INIT_LIST_HEAD(&fritz.ilist);
	fritz.name = FritzName;
	fritz.own_ctrl = fritz_manager;
	fritz.DPROTO.protocol[0] = ISDN_PID_L0_TE_S0;
	fritz.BPROTO.protocol[1] = ISDN_PID_L1_B_64TRANS |
				    ISDN_PID_L1_B_64HDLC;
	fritz.BPROTO.protocol[2] = ISDN_PID_L2_B_TRANS;
	if ((err = mISDN_register(&fritz))) {
		printk(KERN_ERR "Can't register Fritz PCI error(%d)\n", err);
		return(err);
	}
	err = pci_register_driver(&fcpci_driver);
	if (err < 0)
		goto out;
#ifdef OLD_PCI_REGISTER_DRIVER
	pci_nr_found = err;
#endif
#if defined(CONFIG_PNP)
	err = pnp_register_driver(&fcpnp_driver);
	if (err < 0)
		goto out_unregister_pci;
#endif
#if !defined(CONFIG_HOTPLUG) || defined(MODULE)
#ifdef OLD_PCI_REGISTER_DRIVER
	if (pci_nr_found + err == 0) {
		err = -ENODEV;
		goto out_unregister_isapnp;
	}
#endif
#endif
	
	mISDN_module_register(THIS_MODULE);

	return 0;

#if !defined(CONFIG_HOTPLUG) || defined(MODULE)
#ifdef OLD_PCI_REGISTER_DRIVER
 out_unregister_isapnp:
#if defined(CONFIG_PNP)
	pnp_unregister_driver(&fcpnp_driver);
#endif
#endif
#endif
#if defined(CONFIG_PNP)
 out_unregister_pci:
#endif
	pci_unregister_driver(&fcpci_driver);
 out:
 	return err;
}

static void __exit Fritz_cleanup(void)
{
	fritzpnppci *card, *next;
	int err;

	mISDN_module_unregister(THIS_MODULE);
	
	if ((err = mISDN_unregister(&fritz))) {
		printk(KERN_ERR "Can't unregister Fritz PCI error(%d)\n", err);
	}
	list_for_each_entry_safe(card, next, &fritz.ilist, list) {
		if (debug)
			printk(KERN_ERR "Fritz PCI card struct not empty refs %d\n",
				   fritz.refcnt);
		release_card(card);
	}
#if defined(CONFIG_PNP)
	pnp_unregister_driver(&fcpnp_driver);
#endif
	pci_unregister_driver(&fcpci_driver);
}

module_init(Fritz_init);
module_exit(Fritz_cleanup);

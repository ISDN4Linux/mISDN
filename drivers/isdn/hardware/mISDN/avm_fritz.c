/* $Id: avm_fritz.c,v 1.3 2002/09/16 23:49:38 kkeil Exp $
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
#include <linux/kernel_stat.h>
#include "hisax_hw.h"
#include "isac.h"
#include "hisaxl1.h"
#include "helper.h"
#include "debug.h"

static const char *avm_pci_rev = "$Revision: 1.3 $";

#define ISDN_CTYPE_FRITZPCI 1

#define AVM_FRITZ_PCI		1
#define AVM_FRITZ_PNP		2

#ifndef PCI_VENDOR_ID_AVM
#define PCI_VENDOR_ID_AVM	0x1244
#endif
#ifndef PCI_DEVICE_ID_AVM_FRITZ
#define PCI_DEVICE_ID_AVM_FRITZ	0xa00
#endif

#define HDLC_FIFO		0x0
#define HDLC_STATUS		0x4
#define CHIP_WINDOW		0x10

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

/* data struct */

typedef struct _fritzpnppci {
	struct _fritzpnppci	*prev;
	struct _fritzpnppci	*next;
	u_char			subtyp;
	u_int			irq;
	u_int			addr;
	spinlock_t		devlock;
	u_long			flags;
#ifdef SPIN_DEBUG
	void			*lock_adr;
#endif
	dchannel_t		dch;
	bchannel_t		bch[2];
} fritzpnppci;


static void lock_dev(void *data)
{
	register u_long	flags;
	register fritzpnppci *card = data;

	spin_lock_irqsave(&card->devlock, flags);
	card->flags = flags;
#ifdef SPIN_DEBUG
	card->lock_adr = __builtin_return_address(0);
#endif
} 

static void unlock_dev(void *data)
{
	register fritzpnppci *card = data;

	spin_unlock_irqrestore(&card->devlock, card->flags);
#ifdef SPIN_DEBUG
	card->lock_adr = NULL;
#endif
}

/* Interface functions */

static u_char
ReadISAC(void *fc, u_char offset)
{
	register u_char idx = (offset > 0x2f) ? AVM_ISAC_REG_HIGH : AVM_ISAC_REG_LOW;
	register long addr = ((fritzpnppci *)fc)->addr;
	register u_char val;

	outb(idx, addr + HDLC_STATUS);
	val = inb(addr + CHIP_WINDOW + (offset & 0xf));
	return (val);
}

static void
WriteISAC(void *fc, u_char offset, u_char value)
{
	register u_char idx = (offset > 0x2f) ? AVM_ISAC_REG_HIGH : AVM_ISAC_REG_LOW;
	register long addr = ((fritzpnppci *)fc)->addr;

	outb(idx, addr + HDLC_STATUS);
	outb(value, addr + CHIP_WINDOW + (offset & 0xf));
}

static void
ReadISACfifo(void *fc, u_char * data, int size)
{
	register long addr = ((fritzpnppci *)fc)->addr;

	outb(AVM_ISAC_FIFO, addr + HDLC_STATUS);
	insb(addr + CHIP_WINDOW, data, size);
}

static void
WriteISACfifo(void *fc, u_char * data, int size)
{
	register long addr = ((fritzpnppci *)fc)->addr;

	outb(AVM_ISAC_FIFO, addr + HDLC_STATUS);
	outsb(addr + CHIP_WINDOW, data, size);
}

static inline u_int
ReadHDLCPCI(void *fc, int chan, u_char offset)
{
	register u_int idx = chan ? AVM_HDLC_2 : AVM_HDLC_1;
	register u_int val;
	register long addr = ((fritzpnppci *)fc)->addr;

	outl(idx, addr + HDLC_STATUS);
	val = inl(addr + CHIP_WINDOW + offset);
	return (val);
}

static inline void
WriteHDLCPCI(void *fc, int chan, u_char offset, u_int value)
{
	register u_int idx = chan ? AVM_HDLC_2 : AVM_HDLC_1;
	register long addr = ((fritzpnppci *)fc)->addr;

	outl(idx, addr + HDLC_STATUS);
	outl(value, addr + CHIP_WINDOW + offset);
}

static inline u_char
ReadHDLCPnP(void *fc, int chan, u_char offset)
{
	register u_char idx = chan ? AVM_HDLC_2 : AVM_HDLC_1;
	register u_char val;
	register long addr = ((fritzpnppci *)fc)->addr;

	outb(idx, addr + HDLC_STATUS);
	val = inb(addr + CHIP_WINDOW + offset);
	return (val);
}

static inline void
WriteHDLCPnP(void *fc, int chan, u_char offset, u_char value)
{
	register u_char idx = chan ? AVM_HDLC_2 : AVM_HDLC_1;
	register long addr = ((fritzpnppci *)fc)->addr;

	outb(idx, addr + HDLC_STATUS);
	outb(value, addr + CHIP_WINDOW + offset);
}

static u_char
ReadHDLC_s(void *fc, int chan, u_char offset)
{
	return(0xff & ReadHDLCPCI(fc, chan, offset));
}

static void
WriteHDLC_s(void *fc, int chan, u_char offset, u_char value)
{
	WriteHDLCPCI(fc, chan, offset, value);
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

void inline
hdlc_sched_event(bchannel_t *bch, int event)
{
	bch->event |= 1 << event;
	queue_task(&bch->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

void
write_ctrl(bchannel_t *bch, int which) {
	fritzpnppci *fc = bch->inst.data;

	if (fc->dch.debug & L1_DEB_HSCX)
		debugprint(&bch->inst, "hdlc %c wr%x ctrl %x",
			'A' + bch->channel, which, bch->hw.hdlc.ctrl.ctrl);
	if (fc->subtyp == AVM_FRITZ_PCI) {
		WriteHDLCPCI(fc, bch->channel, HDLC_STATUS, bch->hw.hdlc.ctrl.ctrl);
	} else {
		if (which & 4)
			WriteHDLCPnP(fc, bch->channel, HDLC_STATUS + 2,
				bch->hw.hdlc.ctrl.sr.mode);
		if (which & 2)
			WriteHDLCPnP(fc, bch->channel, HDLC_STATUS + 1,
				bch->hw.hdlc.ctrl.sr.xml);
		if (which & 1)
			WriteHDLCPnP(fc, bch->channel, HDLC_STATUS,
				bch->hw.hdlc.ctrl.sr.cmd);
	}
}

static int
modehdlc(bchannel_t *bch, int bc, int protocol)
{
	int hdlc = bch->channel;

	if (bch->debug & L1_DEB_HSCX)
		debugprint(&bch->inst, "hdlc %c protocol %x-->%x ch %d-->%d",
			'A' + hdlc, bch->protocol, protocol, hdlc, bc);
	bch->hw.hdlc.ctrl.ctrl = 0;
	switch (protocol) {
		case (-1): /* used for init */
			bch->protocol = -1;
			bch->channel = bc;
			bc = 0;
		case (ISDN_PID_NONE):
			if (bch->protocol == ISDN_PID_NONE)
				break;
			bch->hw.hdlc.ctrl.sr.cmd  = HDLC_CMD_XRS | HDLC_CMD_RRS;
			bch->hw.hdlc.ctrl.sr.mode = HDLC_MODE_TRANS;
			write_ctrl(bch, 5);
			bch->protocol = ISDN_PID_NONE;
			bch->channel = bc;
			break;
		case (ISDN_PID_L1_B_64TRANS):
			bch->protocol = protocol;
			bch->channel = bc;
			bch->hw.hdlc.ctrl.sr.cmd  = HDLC_CMD_XRS | HDLC_CMD_RRS;
			bch->hw.hdlc.ctrl.sr.mode = HDLC_MODE_TRANS;
			write_ctrl(bch, 5);
			bch->hw.hdlc.ctrl.sr.cmd = HDLC_CMD_XRS;
			write_ctrl(bch, 1);
			bch->hw.hdlc.ctrl.sr.cmd = 0;
			hdlc_sched_event(bch, B_XMTBUFREADY);
			break;
		case (ISDN_PID_L1_B_64HDLC):
			bch->protocol = protocol;
			bch->channel = bc;
			bch->hw.hdlc.ctrl.sr.cmd  = HDLC_CMD_XRS | HDLC_CMD_RRS;
			bch->hw.hdlc.ctrl.sr.mode = HDLC_MODE_ITF_FLG;
			write_ctrl(bch, 5);
			bch->hw.hdlc.ctrl.sr.cmd = HDLC_CMD_XRS;
			write_ctrl(bch, 1);
			bch->hw.hdlc.ctrl.sr.cmd = 0;
			hdlc_sched_event(bch, B_XMTBUFREADY);
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
	if (fc->subtyp == AVM_FRITZ_PCI) {
		outl(idx, fc->addr + HDLC_STATUS);
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
		outb(idx, fc->addr + HDLC_STATUS);
		while (cnt < count) {
			*p++ = inb(fc->addr + CHIP_WINDOW);
			cnt++;
		}
	}
	if (fc->dch.debug & L1_DEB_HSCX_FIFO) {
		char *t = bch->blog;

		if (fc->subtyp == AVM_FRITZ_PNP)
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
	fritzpnppci *fc = bch->inst.data;
	int count, cnt =0;
	u_char *p;
	u_int *ptr;

	if ((bch->debug & L1_DEB_HSCX) && !(bch->debug & L1_DEB_HSCX_FIFO))
		debugprint(&bch->inst, __FUNCTION__);
	count = bch->tx_len - bch->tx_idx;
	if (count <= 0)
		return;
	p = bch->tx_buf + bch->tx_idx;
	bch->hw.hdlc.ctrl.sr.cmd &= ~HDLC_CMD_XME;
	if (count > HDLC_FIFO_SIZE) {
		count = HDLC_FIFO_SIZE;
	} else {
		if (bch->protocol != ISDN_PID_L1_B_64TRANS)
			bch->hw.hdlc.ctrl.sr.cmd |= HDLC_CMD_XME;
	}
	if ((bch->debug & L1_DEB_HSCX) && !(bch->debug & L1_DEB_HSCX_FIFO))
		debugprint(&bch->inst, "%s: %d/%d", __FUNCTION__,
			count, bch->tx_idx);
	ptr = (u_int *) p;
	bch->tx_idx += count;
	bch->hw.hdlc.ctrl.sr.xml = ((count == HDLC_FIFO_SIZE) ? 0 : count);
	write_ctrl(bch, 3);  /* sets the correct index too */
	if (fc->subtyp == AVM_FRITZ_PCI) {
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
		while (cnt<count) {
			outb(*p++, fc->addr + CHIP_WINDOW);
			cnt++;
		}
	}
	if (bch->debug & L1_DEB_HSCX_FIFO) {
		char *t = bch->blog;

		if (fc->subtyp == AVM_FRITZ_PNP)
			p = (u_char *) ptr;
		t += sprintf(t, "hdlc_fill_fifo %c cnt %d",
			     bch->channel ? 'B' : 'A', count);
		QuickHex(t, p, count);
		debugprint(&bch->inst, bch->blog);
	}
}

static void
HDLC_irq(bchannel_t *bch, u_int stat) {
	int len;
	struct sk_buff *skb;

	if (bch->debug & L1_DEB_HSCX)
		debugprint(&bch->inst, "ch%d stat %#x", bch->channel, stat);
	if (stat & HDLC_INT_RPR) {
		if (stat & HDLC_STAT_RDO) {
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "RDO");
			else
				debugprint(&bch->inst, "ch%d stat %#x", bch->channel, stat);
			bch->hw.hdlc.ctrl.sr.xml = 0;
			bch->hw.hdlc.ctrl.sr.cmd |= HDLC_CMD_RRS;
			write_ctrl(bch, 1);
			bch->hw.hdlc.ctrl.sr.cmd &= ~HDLC_CMD_RRS;
			write_ctrl(bch, 1);
			bch->rx_idx = 0;
		} else {
			if (!(len = (stat & HDLC_STAT_RML_MASK)>>8))
				len = 32;
			hdlc_empty_fifo(bch, len);
			if ((stat & HDLC_STAT_RME) || (bch->protocol == ISDN_PID_L1_B_64TRANS)) {
				if (((stat & HDLC_STAT_CRCVFRRAB)==HDLC_STAT_CRCVFR) ||
					(bch->protocol == ISDN_PID_L1_B_64TRANS)) {
					if (!(skb = alloc_uplink_skb(bch->rx_idx)))
						printk(KERN_WARNING "HDLC: receive out of memory\n");
					else {
						memcpy(skb_put(skb, bch->rx_idx),
							bch->rx_buf, bch->rx_idx);
						skb_queue_tail(&bch->rqueue, skb);
					}
					bch->rx_idx = 0;
					hdlc_sched_event(bch, B_RCVBUFREADY);
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
		bch->hw.hdlc.ctrl.sr.xml = 0;
		bch->hw.hdlc.ctrl.sr.cmd |= HDLC_CMD_XRS;
		write_ctrl(bch, 1);
		bch->hw.hdlc.ctrl.sr.cmd &= ~HDLC_CMD_XRS;
		write_ctrl(bch, 1);
		hdlc_fill_fifo(bch);
	} else if (stat & HDLC_INT_XPR) {
		if (bch->tx_idx < bch->tx_len) {
			hdlc_fill_fifo(bch);
		} else {
			bch->tx_idx = 0;
			if (test_and_clear_bit(FLG_TX_NEXT, &bch->Flag)) {
				if (bch->next_skb) {
					bch->tx_len = bch->next_skb->len;
					memcpy(bch->tx_buf,
						bch->next_skb->data, bch->tx_len);
					hdlc_fill_fifo(bch);
					hdlc_sched_event(bch, B_XMTBUFREADY);
				} else {
					printk(KERN_WARNING "hdlc tx irq TX_NEXT without skb\n");
					test_and_clear_bit(FLG_TX_BUSY, &bch->Flag);
				}
			} else {
				test_and_clear_bit(FLG_TX_BUSY, &bch->Flag);
				hdlc_sched_event(bch, B_XMTBUFREADY);
			}
		}
	}
}

static inline void
HDLC_irq_main(fritzpnppci *fc)
{
	u_int stat;
	bchannel_t *bch;

	if (fc->subtyp == AVM_FRITZ_PCI) {
		stat = ReadHDLCPCI(fc, 0, HDLC_STATUS);
	} else {
		stat = ReadHDLCPnP(fc, 0, HDLC_STATUS);
		if (stat & HDLC_INT_RPR)
			stat |= (ReadHDLCPnP(fc, 0, HDLC_STATUS+1))<<8;
	}
	if (stat & HDLC_INT_MASK) {
		if (!(bch = Sel_BCS(fc, 0))) {
			if (fc->bch[0].debug)
				debugprint(&fc->bch[0].inst, "hdlc spurious channel 0 IRQ");
		} else
			HDLC_irq(bch, stat);
	}
	if (fc->subtyp == AVM_FRITZ_PCI) {
		stat = ReadHDLCPCI(fc, 1, HDLC_STATUS);
	} else {
		stat = ReadHDLCPnP(fc, 1, HDLC_STATUS);
		if (stat & HDLC_INT_RPR)
			stat |= (ReadHDLCPnP(fc, 1, HDLC_STATUS+1))<<8;
	}
	if (stat & HDLC_INT_MASK) {
		if (!(bch = Sel_BCS(fc, 1))) {
			if (fc->bch[1].debug)
				debugprint(&fc->bch[1].inst, "hdlc spurious channel 1 IRQ");
		} else
			HDLC_irq(bch, stat);
	}
}

static void
avm_pcipnp_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	fritzpnppci *fc = dev_id;
	u_char val;
	u_char sval;

	if (!fc) {
		printk(KERN_WARNING "AVM PCI: Spurious interrupt!\n");
		return;
	}
	lock_dev(fc);
	sval = inb(fc->addr + 2);
	if ((sval & AVM_STATUS0_IRQ_MASK) == AVM_STATUS0_IRQ_MASK) {
		/* possible a shared  IRQ reqest */
		unlock_dev(fc);
		return;
	}
	if (!(sval & AVM_STATUS0_IRQ_ISAC)) {
		val = ReadISAC(fc, ISAC_ISTA);
		isac_interrupt(&fc->dch, val);
	}
	if (!(sval & AVM_STATUS0_IRQ_HDLC)) {
		HDLC_irq_main(fc);
	}
	WriteISAC(fc, ISAC_MASK, 0xFF);
	WriteISAC(fc, ISAC_MASK, 0x0);
	unlock_dev(fc);
}

static int
hdlc_down(hisaxif_t *hif, struct sk_buff *skb)
{
	bchannel_t	*bch;
	int		ret = -EINVAL;
	hisax_head_t	*hh;

	if (!hif || !skb)
		return(ret);
	hh = HISAX_HEAD_P(skb);
	bch = hif->fdata;
	if ((hh->prim == PH_DATA_REQ) ||
		(hh->prim == (DL_DATA | REQUEST))) {
		if (bch->next_skb) {
			debugprint(&bch->inst, " l2l1 next_skb exist this shouldn't happen");
			return(-EBUSY);
		}
		bch->inst.lock(bch->inst.data);
		if (test_and_set_bit(FLG_TX_BUSY, &bch->Flag)) {
			test_and_set_bit(FLG_TX_NEXT, &bch->Flag);
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
			bch->inst.lock(bch->inst.data);
			ret = modehdlc(bch, bch->channel,
				bch->inst.pid.protocol[1]);
			bch->inst.unlock(bch->inst.data);
		}
		skb_trim(skb, 0);
		return(if_newhead(&bch->inst.up, hh->prim | CONFIRM, ret, skb));
	} else if ((hh->prim == (PH_DEACTIVATE | REQUEST)) ||
		(hh->prim == (DL_RELEASE | REQUEST)) ||
		(hh->prim == (MGR_DISCONNECT | REQUEST))) {
		bch->inst.lock(bch->inst.data);
		if (test_and_clear_bit(FLG_TX_NEXT, &bch->Flag)) {
			dev_kfree_skb(bch->next_skb);
			bch->next_skb = NULL;
		}
		test_and_clear_bit(FLG_TX_BUSY, &bch->Flag);
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
hdlc_bh(bchannel_t *bch)
{
	struct sk_buff	*skb;
	u_int 		pr;
	int		ret;

	if (!bch)
		return;
	if (!bch->inst.up.func) {
		printk(KERN_WARNING "HiSax: hdlc_bh without up.func\n");
		return;
	}
	if (test_and_clear_bit(B_XMTBUFREADY, &bch->event)) {
		skb = bch->next_skb;
		if (skb) {
			bch->next_skb = NULL;
			if (bch->inst.pid.protocol[2] == ISDN_PID_L2_B_TRANS)
				pr = DL_DATA | CONFIRM;
			else
				pr = PH_DATA | CONFIRM;
			if (if_newhead(&bch->inst.up, pr, DINFO_SKB, skb))
				dev_kfree_skb(skb);
		}
	}
	if (test_and_clear_bit(B_RCVBUFREADY, &bch->event)) {
		while ((skb = skb_dequeue(&bch->rqueue))) {
			if (bch->inst.pid.protocol[2] == ISDN_PID_L2_B_TRANS)
				pr = DL_DATA | INDICATION;
			else
				pr = PH_DATA | INDICATION;
			ret = if_newhead(&bch->inst.up, pr, DINFO_SKB, skb);
			if (ret < 0) {
				printk(KERN_WARNING "hdlc_bh deliver err %d\n",
					ret);
				dev_kfree_skb(skb);
			}
		}
	}
}

static void
inithdlc(fritzpnppci *fc)
{
	fc->bch[0].tqueue.routine = (void *) (void *) hdlc_bh;
	fc->bch[1].tqueue.routine = (void *) (void *) hdlc_bh;
	modehdlc(&fc->bch[0], 0, -1);
	modehdlc(&fc->bch[1], 1, -1);
}

void
clear_pending_hdlc_ints(fritzpnppci *fc)
{
	u_int val;

	if (fc->subtyp == AVM_FRITZ_PCI) {
		val = ReadHDLCPCI(fc, 0, HDLC_STATUS);
		debugprint(&fc->dch.inst, "HDLC 1 STA %x", val);
		val = ReadHDLCPCI(fc, 1, HDLC_STATUS);
		debugprint(&fc->dch.inst, "HDLC 2 STA %x", val);
	} else {
		val = ReadHDLCPnP(fc, 0, HDLC_STATUS);
		debugprint(&fc->dch.inst, "HDLC 1 STA %x", val);
		val = ReadHDLCPnP(fc, 0, HDLC_STATUS + 1);
		debugprint(&fc->dch.inst, "HDLC 1 RML %x", val);
		val = ReadHDLCPnP(fc, 0, HDLC_STATUS + 2);
		debugprint(&fc->dch.inst, "HDLC 1 MODE %x", val);
		val = ReadHDLCPnP(fc, 0, HDLC_STATUS + 3);
		debugprint(&fc->dch.inst, "HDLC 1 VIN %x", val);
		val = ReadHDLCPnP(fc, 1, HDLC_STATUS);
		debugprint(&fc->dch.inst, "HDLC 2 STA %x", val);
		val = ReadHDLCPnP(fc, 1, HDLC_STATUS + 1);
		debugprint(&fc->dch.inst, "HDLC 2 RML %x", val);
		val = ReadHDLCPnP(fc, 1, HDLC_STATUS + 2);
		debugprint(&fc->dch.inst, "HDLC 2 MODE %x", val);
		val = ReadHDLCPnP(fc, 1, HDLC_STATUS + 3);
		debugprint(&fc->dch.inst, "HDLC 2 VIN %x", val);
	}
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

	if (fc->subtyp == AVM_FRITZ_PNP)
		shared = 0;
	save_flags(flags);
	irq_cnt = kstat_irqs(fc->irq);
	printk(KERN_INFO "AVM Fritz!PCI: IRQ %d count %d\n", fc->irq, irq_cnt);
	lock_dev(fc);
	if (request_irq(fc->irq, avm_pcipnp_interrupt, SA_SHIRQ,
		"AVM Fritz!PCI", fc)) {
		printk(KERN_WARNING "HiSax: couldn't get interrupt %d\n",
			fc->irq);
		unlock_dev(fc);
		return(-EIO);
	}
	while (cnt) {
		clear_pending_isac_ints(&fc->dch);
		init_isac(&fc->dch);
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
		lock_dev(fc);
	}
	unlock_dev(fc);
	return(-EIO);
}

#define MAX_CARDS	4
#define MODULE_PARM_T	"1-4i"
static int fritz_cnt;
static u_int protocol[MAX_CARDS];
static u_int io[MAX_CARDS];
static u_int irq[MAX_CARDS];
static int layermask[MAX_CARDS];
static int cfg_idx;

static hisaxobject_t	fritz;
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
#define Fritz_init init_module
#endif

static char FritzName[] = "Fritz!PCI";

static	struct pci_dev *dev_avm = NULL;
static	int pci_finished_lookup;

int
setup_fritz(fritzpnppci *fc, u_int io_cfg, u_int irq_cfg)
{
	u_int val, ver;
	char tmp[64];

	strcpy(tmp, avm_pci_rev);
	printk(KERN_INFO "HiSax: AVM Fritz PCI/PnP driver Rev. %s\n", HiSax_getrev(tmp));
	fc->subtyp = 0;
#if CONFIG_PCI
	if (pci_present() && !pci_finished_lookup) {
		while ((dev_avm = pci_find_device(PCI_VENDOR_ID_AVM,
			PCI_DEVICE_ID_AVM_FRITZ,  dev_avm))) {
			fc->irq = dev_avm->irq;
			if (!fc->irq) {
				printk(KERN_ERR "FritzPCI: No IRQ for PCI card found\n");
				continue;;
			}
			if (pci_enable_device(dev_avm))
				continue;
			fc->addr = pci_resource_start_io(dev_avm,1);
			if (!fc->addr) {
				printk(KERN_ERR "FritzPCI: No IO-Adr for PCI card found\n");
				continue;
			}
			fc->subtyp = AVM_FRITZ_PCI;
			break;
		}
	 	if (!dev_avm) {
	 		pci_finished_lookup = 1;
			printk(KERN_INFO "Fritz: No more PCI cards found\n");
		}
	}
#else
	printk(KERN_WARNING "FritzPCI: NO_PCI_BIOS\n");
	return (-ENODEV);
#endif /* CONFIG_PCI */
	if (!fc->subtyp) { /* OK no PCI found now check for an ISA card */	
		if ((!io_cfg) || (!irq_cfg)) {
			if (!fritz_cnt)
				printk(KERN_WARNING
					"Fritz: No io/irq for ISA card\n");
			return(1);
		}
		fc->addr = io_cfg;
		fc->irq = irq_cfg;
		fc->subtyp = AVM_FRITZ_PNP;
	}
	if (check_region((fc->addr), 32)) {
		printk(KERN_WARNING
		       "HiSax: %s config port %x-%x already in use\n",
		       "AVM Fritz!PCI",
		       fc->addr,
		       fc->addr + 31);
		return(-EIO);
	} else {
		request_region(fc->addr, 32,
			(fc->subtyp == AVM_FRITZ_PCI) ? "avm PCI" : "avm PnP");
	}
	switch (fc->subtyp) {
	    case AVM_FRITZ_PCI:
		val = inl(fc->addr);
		printk(KERN_INFO "AVM PCI: stat %#x\n", val);
		printk(KERN_INFO "AVM PCI: Class %X Rev %d\n",
			val & 0xff, (val>>8) & 0xff);
		fc->bch[0].BC_Read_Reg = &ReadHDLC_s;
		fc->bch[0].BC_Write_Reg = &WriteHDLC_s;
		fc->bch[1].BC_Read_Reg = &ReadHDLC_s;
		fc->bch[1].BC_Write_Reg = &WriteHDLC_s;
		break;
	    case AVM_FRITZ_PNP:
		val = inb(fc->addr);
		ver = inb(fc->addr + 1);
		printk(KERN_INFO "AVM PnP: Class %X Rev %d\n", val, ver);
		reset_avmpcipnp(fc);
		fc->bch[0].BC_Read_Reg = &ReadHDLCPnP;
		fc->bch[0].BC_Write_Reg = &WriteHDLCPnP;
		fc->bch[1].BC_Read_Reg = &ReadHDLCPnP;
		fc->bch[1].BC_Write_Reg = &WriteHDLCPnP;
		break;
	    default:
	  	printk(KERN_WARNING "AVM unknown subtype %d\n", fc->subtyp);
	  	return(-ENODEV);
	}
	printk(KERN_INFO "HiSax: %s config irq:%d base:0x%X\n",
		(fc->subtyp == AVM_FRITZ_PCI) ? "AVM Fritz!PCI" : "AVM Fritz!PnP",
		fc->irq, fc->addr);

	fc->dch.readisac = &ReadISAC;
	fc->dch.writeisac = &WriteISAC;
	fc->dch.readisacfifo = &ReadISACfifo;
	fc->dch.writeisacfifo = &WriteISACfifo;
	lock_dev(fc);
#ifdef SPIN_DEBUG
	printk(KERN_ERR "lock_adr=%p now(%p)\n", &fc->lock_adr, fc->lock_adr);
#endif
	ISACVersion(&fc->dch, (fc->subtyp == AVM_FRITZ_PCI) ? "AVM PCI:" : "AVM PnP:");
	unlock_dev(fc);
	return(0);
}

static void
release_card(fritzpnppci *card) {

	lock_dev(card);
	outb(0, card->addr + 2);
	free_irq(card->irq, card);
	modehdlc(&card->bch[0], 0, ISDN_PID_NONE);
	modehdlc(&card->bch[1], 1, ISDN_PID_NONE);
	free_isac(&card->dch);
	release_region(card->addr, 32);
	free_bchannel(&card->bch[1]);
	free_bchannel(&card->bch[0]);
	free_dchannel(&card->dch);
	REMOVE_FROM_LISTBASE(card, ((fritzpnppci *)fritz.ilist));
	unlock_dev(card);
	kfree(card);
	fritz.refcnt--;
}

static int
fritz_manager(void *data, u_int prim, void *arg) {
	fritzpnppci	*card = fritz.ilist;
	hisaxinstance_t	*inst = data;
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

int
Fritz_init(void)
{
	int err,i;
	fritzpnppci *card;
	hisax_pid_t pid;

	fritz.name = FritzName;
	fritz.own_ctrl = fritz_manager;
	fritz.DPROTO.protocol[0] = ISDN_PID_L0_TE_S0;
	fritz.BPROTO.protocol[1] = ISDN_PID_L1_B_64TRANS |
				    ISDN_PID_L1_B_64HDLC;
	fritz.BPROTO.protocol[2] = ISDN_PID_L2_B_TRANS;
	fritz.prev = NULL;
	fritz.next = NULL;
	if ((err = HiSax_register(&fritz))) {
		printk(KERN_ERR "Can't register Fritz PCI error(%d)\n", err);
		return(err);
	}
	while (fritz_cnt < MAX_CARDS) {
		if (!(card = kmalloc(sizeof(fritzpnppci), GFP_ATOMIC))) {
			printk(KERN_ERR "No kmem for fritzcard\n");
			HiSax_unregister(&fritz);
			return(-ENOMEM);
		}
		memset(card, 0, sizeof(fritzpnppci));
		APPEND_TO_LIST(card, ((fritzpnppci *)fritz.ilist));
		card->dch.debug = debug;
		card->dch.inst.obj = &fritz;
		spin_lock_init(&card->devlock);
		card->dch.inst.lock = lock_dev;
		card->dch.inst.unlock = unlock_dev;
		card->dch.inst.data = card;
		card->dch.inst.pid.layermask = ISDN_LAYER(0);
		card->dch.inst.pid.protocol[0] = ISDN_PID_L0_TE_S0;
		card->dch.inst.up.owner = &card->dch.inst;
		card->dch.inst.down.owner = &card->dch.inst;
		fritz.ctrl(NULL, MGR_DISCONNECT | REQUEST,
			&card->dch.inst.down);
		sprintf(card->dch.inst.name, "Fritz%d", fritz_cnt+1);
		set_dchannel_pid(&pid, protocol[fritz_cnt],
			layermask[fritz_cnt]);
		init_dchannel(&card->dch);
		for (i=0; i<2; i++) {
			card->bch[i].channel = i;
			card->bch[i].inst.obj = &fritz;
			card->bch[i].inst.data = card;
			card->bch[i].inst.pid.layermask = ISDN_LAYER(0);
			card->bch[i].inst.up.owner = &card->bch[i].inst;
			card->bch[i].inst.down.owner = &card->bch[i].inst;
			fritz.ctrl(NULL, MGR_DISCONNECT | REQUEST,
				&card->bch[i].inst.down);
			card->bch[i].inst.lock = lock_dev;
			card->bch[i].inst.unlock = unlock_dev;
			card->bch[i].debug = debug;
			sprintf(card->bch[i].inst.name, "%s B%d",
				card->dch.inst.name, i+1);
			init_bchannel(&card->bch[i]);
		}
		printk(KERN_DEBUG "fritz card %p dch %p bch1 %p bch2 %p\n",
			card, &card->dch, &card->bch[0], &card->bch[1]);
		if (setup_fritz(card, io[cfg_idx], irq[cfg_idx])) {
			err = 0;
			free_dchannel(&card->dch);
			free_bchannel(&card->bch[1]);
			free_bchannel(&card->bch[0]);
			REMOVE_FROM_LISTBASE(card, ((fritzpnppci *)fritz.ilist));
			kfree(card);
			if (!fritz_cnt) {
				HiSax_unregister(&fritz);
				err = -ENODEV;
			} else
				printk(KERN_INFO "fritz %d cards installed\n",
					fritz_cnt);
			return(err);
		}
		if (card->subtyp == AVM_FRITZ_PNP)
			cfg_idx++;
		fritz_cnt++;
		if ((err = fritz.ctrl(NULL, MGR_NEWSTACK | REQUEST, &card->dch.inst))) {
			printk(KERN_ERR  "MGR_ADDSTACK REQUEST dch err(%d)\n", err);
			release_card(card);
			if (!fritz_cnt)
				HiSax_unregister(&fritz);
			else
				err = 0;
			return(err);
		}
		for (i=0; i<2; i++) {
			if ((err = fritz.ctrl(card->dch.inst.st, MGR_NEWSTACK | REQUEST,
				&card->bch[i].inst))) {
				printk(KERN_ERR "MGR_ADDSTACK bchan error %d\n", err);
				fritz.ctrl(card->dch.inst.st,
					MGR_DELSTACK | REQUEST, NULL);
				if (!fritz_cnt)
					HiSax_unregister(&fritz);
				else
					err = 0;
				return(err);
			}
		}
		if ((err = fritz.ctrl(card->dch.inst.st, MGR_SETSTACK | REQUEST, &pid))) {
			printk(KERN_ERR  "MGR_SETSTACK REQUEST dch err(%d)\n", err);
			fritz.ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
			if (!fritz_cnt)
				HiSax_unregister(&fritz);
			else
				err = 0;
			return(err);
		}
		if ((err = init_card(card))) {
			fritz.ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
			if (!fritz_cnt)
				HiSax_unregister(&fritz);
			else
				err = 0;
			return(err);
		}
	}
	printk(KERN_INFO "fritz %d cards installed\n", fritz_cnt);
	return(0);
}

#ifdef MODULE
int
cleanup_module(void)
{
	int err;
	if ((err = HiSax_unregister(&fritz))) {
		printk(KERN_ERR "Can't unregister Fritz PCI error(%d)\n", err);
		return(err);
	}
	while(fritz.ilist) {
		printk(KERN_ERR "Fritz PCI card struct not empty refs %d\n",
			fritz.refcnt);
		release_card(fritz.ilist);
	}
	return(0);
}
#endif

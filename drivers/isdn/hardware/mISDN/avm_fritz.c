/* $Id: avm_fritz.c,v 0.6 2001/03/03 08:07:29 kkeil Exp $
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

static const char *avm_pci_rev = "$Revision: 0.6 $";

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
	register long flags;

	save_flags(flags);
	cli();
	outb(idx, addr + HDLC_STATUS);
	val = inb(addr + CHIP_WINDOW + (offset & 0xf));
	restore_flags(flags);
	return (val);
}

static void
WriteISAC(void *fc, u_char offset, u_char value)
{
	register u_char idx = (offset > 0x2f) ? AVM_ISAC_REG_HIGH : AVM_ISAC_REG_LOW;
	register long addr = ((fritzpnppci *)fc)->addr;
	register long flags;

	save_flags(flags);
	cli();
	outb(idx, addr + HDLC_STATUS);
	outb(value, addr + CHIP_WINDOW + (offset & 0xf));
	restore_flags(flags);
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
	register long flags;

	save_flags(flags);
	cli();
	outl(idx, addr + HDLC_STATUS);
	val = inl(addr + CHIP_WINDOW + offset);
	restore_flags(flags);
	return (val);
}

static inline void
WriteHDLCPCI(void *fc, int chan, u_char offset, u_int value)
{
	register u_int idx = chan ? AVM_HDLC_2 : AVM_HDLC_1;
	register long addr = ((fritzpnppci *)fc)->addr;
	register long flags;

	save_flags(flags);
	cli();
	outl(idx, addr + HDLC_STATUS);
	outl(value, addr + CHIP_WINDOW + offset);
	restore_flags(flags);
}

static inline u_char
ReadHDLCPnP(void *fc, int chan, u_char offset)
{
	register u_char idx = chan ? AVM_HDLC_2 : AVM_HDLC_1;
	register u_char val;
	register long addr = ((fritzpnppci *)fc)->addr;
	register long flags;

	save_flags(flags);
	cli();
	outb(idx, addr + HDLC_STATUS);
	val = inb(addr + CHIP_WINDOW + offset);
	restore_flags(flags);
	return (val);
}

static inline void
WriteHDLCPnP(void *fc, int chan, u_char offset, u_char value)
{
	register u_char idx = chan ? AVM_HDLC_2 : AVM_HDLC_1;
	register long addr = ((fritzpnppci *)fc)->addr;
	register long flags;

	save_flags(flags);
	cli();
	outb(idx, addr + HDLC_STATUS);
	outb(value, addr + CHIP_WINDOW + offset);
	restore_flags(flags);
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

void
modehdlc(bchannel_t *bch, int protocol, int bc)
{
	fritzpnppci *fc = bch->inst.data;
	int hdlc = bch->channel;

	if (fc->dch.debug & L1_DEB_HSCX)
		debugprint(&bch->inst, "hdlc %c protocol %d --> %d ichan %d --> %d",
			'A' + hdlc, bch->protocol, protocol, hdlc, bc);
	bch->hw.hdlc.ctrl.ctrl = 0;
	switch (protocol) {
		case (-1): /* used for init */
			bch->protocol = 1;
			bch->channel = bc;
			bc = 0;
		case (ISDN_PID_NONE):
			if (bch->protocol == ISDN_PID_NONE)
				return;
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
	}
}

static inline void
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

static inline void
hdlc_fill_fifo(bchannel_t *bch)
{
	fritzpnppci *fc = bch->inst.data;
	int count, cnt =0;
	int fifo_size = 32;
	u_char *p;
	u_int *ptr;

	if ((fc->dch.debug & L1_DEB_HSCX) && !(fc->dch.debug & L1_DEB_HSCX_FIFO))
		debugprint(&bch->inst, "hdlc_fill_fifo");
	if (!bch->tx_buf)
		return;
	count = bch->tx_len - bch->tx_idx;
	if (count <= 0)
		return;

	bch->hw.hdlc.ctrl.sr.cmd &= ~HDLC_CMD_XME;
	if (count > fifo_size) {
		count = fifo_size;
	} else {
		if (bch->protocol != ISDN_PID_L1_B_64TRANS)
			bch->hw.hdlc.ctrl.sr.cmd |= HDLC_CMD_XME;
	}
	if ((fc->dch.debug & L1_DEB_HSCX) && !(fc->dch.debug & L1_DEB_HSCX_FIFO))
		debugprint(&bch->inst, "hdlc_fill_fifo %d/%ld", count, bch->tx_idx);
	ptr = (u_int *) p = bch->tx_buf + bch->tx_idx;
	bch->tx_idx += count;
	bch->hw.hdlc.count += count;
	bch->hw.hdlc.ctrl.sr.xml = ((count == fifo_size) ? 0 : count);
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
	if (fc->dch.debug & L1_DEB_HSCX_FIFO) {
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
fill_hdlc(bchannel_t *bch)
{
	long flags;
	save_flags(flags);
	cli();
	hdlc_fill_fifo(bch);
	restore_flags(flags);
}

static inline void
HDLC_irq(bchannel_t *bch, u_int stat) {
	int len;
	struct sk_buff *skb;
	fritzpnppci *fc = bch->inst.data;

	if (fc->dch.debug & L1_DEB_HSCX)
		debugprint(&bch->inst, "ch%d stat %#x", bch->channel, stat);
	if (stat & HDLC_INT_RPR) {
		if (stat & HDLC_STAT_RDO) {
			if (fc->dch.debug & L1_DEB_HSCX)
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
					if (!(skb = dev_alloc_skb(bch->rx_idx)))
						printk(KERN_WARNING "HDLC: receive out of memory\n");
					else {
						memcpy(skb_put(skb, bch->rx_idx),
							bch->rx_buf, bch->rx_idx);
						skb_queue_tail(&bch->rqueue, skb);
					}
					bch->rx_idx = 0;
					hdlc_sched_event(bch, B_RCVBUFREADY);
				} else {
					if (fc->dch.debug & L1_DEB_HSCX)
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
			bch->hw.hdlc.count = 0;
			bch->tx_idx = 0;
//			hdlc_sched_event(bch, B_XMTBUFREADY);
			if (fc->dch.debug & L1_DEB_WARN)
				debugprint(&bch->inst, "ch%d XDU", bch->channel);
		} else if (fc->dch.debug & L1_DEB_WARN)
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
#if 0
			if (bch->st->lli.l1writewakeup &&
				(PACKET_NOACK != bch->tx_skb->pkt_type))
				bch->st->lli.l1writewakeup(bch->st, bch->hw.hdlc.count);
			dev_kfree_skb(bch->tx_skb);
			bch->hw.hdlc.count = 0;
			bch->tx_skb = NULL;
#endif
			test_and_clear_bit(BC_FLG_BUSY, &bch->Flag);
			hdlc_sched_event(bch, B_XMTBUFREADY);
		}
	}
}

inline void
HDLC_irq_main(fritzpnppci *fc)
{
	u_int stat;
	long  flags;
	bchannel_t *bch;

	save_flags(flags);
	cli();
	if (fc->subtyp == AVM_FRITZ_PCI) {
		stat = ReadHDLCPCI(fc, 0, HDLC_STATUS);
	} else {
		stat = ReadHDLCPnP(fc, 0, HDLC_STATUS);
		if (stat & HDLC_INT_RPR)
			stat |= (ReadHDLCPnP(fc, 0, HDLC_STATUS+1))<<8;
	}
	if (stat & HDLC_INT_MASK) {
		if (!(bch = Sel_BCS(fc, 0))) {
			if (fc->dch.debug)
				debugprint(&bch->inst, "hdlc spurious channel 0 IRQ");
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
			if (fc->dch.debug)
				debugprint(&bch->inst, "hdlc spurious channel 1 IRQ");
		} else
			HDLC_irq(bch, stat);
	}
	restore_flags(flags);
}

static int
hdlc_down(hisaxif_t *hif, u_int prim, u_int nr, int len, void *arg)
{
	bchannel_t	*bch;
	fritzpnppci	*fc;
	struct sk_buff	*skb = arg;
	long flags;

	if (!hif || !hif->fdata)
		return(-EINVAL);
	bch = hif->fdata;
	fc = bch->inst.data;
	switch (prim) {
		case (PH_DATA | REQUEST):
			save_flags(flags);
			cli();
			if (bch->next_skb) {
				restore_flags(flags);
			} else {
				bch->next_skb = skb;
				test_and_set_bit(BC_FLG_BUSY, &bch->Flag);
				bch->hw.hdlc.count = 0;
				restore_flags(flags);
				fill_hdlc(bch);
			}
			break;
#if 0
		case (PH_PULL | REQUEST):
			if (!bch->tx_skb) {
				test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
				st->l1.l1l2(st, PH_PULL | CONFIRM, NULL);
			} else
				test_and_set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			break;
#endif
		case (PH_ACTIVATE | REQUEST):
			test_and_set_bit(BC_FLG_ACTIV, &bch->Flag);
			modehdlc(bch, len, (int)arg);
//			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | REQUEST):
//			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | CONFIRM):
			test_and_clear_bit(BC_FLG_ACTIV, &bch->Flag);
			test_and_clear_bit(BC_FLG_BUSY, &bch->Flag);
			modehdlc(bch, 0, bch->channel);
			bch->inst.up.func(&bch->inst.up,
				PH_DEACTIVATE | CONFIRM, nr, 0, NULL);
			break;
	}
	return(0);
}

void
close_hdlcstate(bchannel_t *bch)
{
	modehdlc(bch, 0, 0);
	if (test_and_clear_bit(BC_FLG_INIT, &bch->Flag)) {
		if (bch->rx_buf) {
			kfree(bch->rx_buf);
			bch->rx_buf = NULL;
		}
		if (bch->blog) {
			kfree(bch->blog);
			bch->blog = NULL;
		}
		discard_queue(&bch->rqueue);
		if (bch->next_skb) {
			dev_kfree_skb(bch->next_skb);
			bch->next_skb = NULL;
			test_and_clear_bit(BC_FLG_BUSY, &bch->Flag);
		}
	}
}

int
open_hdlcstate(fritzpnppci *fc, bchannel_t *bch)
{
	if (!test_and_set_bit(BC_FLG_INIT, &bch->Flag)) {
		if (!(bch->rx_buf = kmalloc(MAX_DATA_MEM, GFP_ATOMIC))) {
			printk(KERN_WARNING
			       "HiSax: No memory for hdlc.rx_buf\n");
			return (1);
		}
		if (!(bch->blog = kmalloc(MAX_BLOG_SPACE, GFP_ATOMIC))) {
			printk(KERN_WARNING
				"HiSax: No memory for bch->blog\n");
			test_and_clear_bit(BC_FLG_INIT, &bch->Flag);
			kfree(bch->rx_buf);
			bch->rx_buf = NULL;
			return (2);
		}
		skb_queue_head_init(&bch->rqueue);
	}
	bch->next_skb = NULL;
	test_and_clear_bit(BC_FLG_BUSY, &bch->Flag);
	bch->event = 0;
	bch->rx_idx = 0;
	return (0);
}

int
setstack_hdlc(fritzpnppci *fc, bchannel_t *bch)
{
	bch->inst.data = fc;
	if (open_hdlcstate(fc, bch))
		return (-1);
	bch->inst.up.fdata = bch;
	bch->inst.up.func = hdlc_down;
	return (0);
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
inithdlc(fritzpnppci *fc)
{
	modehdlc(fc->bch, -1, 0);
	modehdlc(fc->bch + 1, -1, 1);
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
	sval = inb(fc->addr + 2);
	if ((sval & AVM_STATUS0_IRQ_MASK) == AVM_STATUS0_IRQ_MASK)
		/* possible a shared  IRQ reqest */
		return;
	if (!(sval & AVM_STATUS0_IRQ_ISAC)) {
		val = ReadISAC(fc, ISAC_ISTA);
		isac_interrupt(&fc->dch, val);
	}
	if (!(sval & AVM_STATUS0_IRQ_HDLC)) {
		HDLC_irq_main(fc);
	}
	WriteISAC(fc, ISAC_MASK, 0xFF);
	WriteISAC(fc, ISAC_MASK, 0x0);
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

void
BChannel_bh(bchannel_t *bch)
{
}

void
init_bch(fritzpnppci *fc,
	     int bc)
{
	bchannel_t *bch = fc->bch + bc;

	bch->inst.data = fc;
	bch->channel = bc;
	bch->tqueue.next = 0;
	bch->tqueue.sync = 0;
	bch->tqueue.routine = (void *) (void *) BChannel_bh;
	bch->tqueue.data = bch;
	bch->Flag = 0;
}

#define MAX_CARDS	4
#define MODULE_PARM_T	"1-4i"
static int fritz_cnt;
static u_int act_protocol;
static u_int protocol[MAX_CARDS];
static u_int io[MAX_CARDS];
static u_int irq[MAX_CARDS];
static int cfg_idx;

static fritzpnppci	*cardlist = NULL;
static hisaxobject_t	fritz;
static int debug;

#ifdef MODULE
MODULE_AUTHOR("Karsten Keil");
MODULE_PARM(debug, "1i");
MODULE_PARM(io, MODULE_PARM_T);
MODULE_PARM(protocol, MODULE_PARM_T);
MODULE_PARM(irq, MODULE_PARM_T);
#define Fritz_init init_module
#endif

static char FritzName[] = "Fritz!PCI";

static int FritzProtocols[] = {	ISDN_PID_L0_TE_S0,
				ISDN_PID_L1_B_64TRANS,
				ISDN_PID_L1_B_64HDLC,
				ISDN_PID_L2_B_TRANS,
				ISDN_PID_L3_B_TRANS,
			};
#define FRITZPCNT	(sizeof(FritzProtocols)/sizeof(int))
 
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
			fc->addr = dev_avm->base_address[ 1] & PCI_BASE_ADDRESS_IO_MASK;
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

static int
dummy_down(hisaxif_t *hif,  u_int prim, u_int nr, int dtyp, void *arg) {
	fritzpnppci *card;

	if (!hif || !hif->fdata)
		return(-EINVAL);
	card = hif->fdata;
	printk(KERN_WARNING "dummy fritz down id %d prim %x\n", card->dch.inst.st->id, prim);
	return(-EINVAL);
}


static int
add_if(hisaxinstance_t *inst, int channel, hisaxif_t *hif) {
	int err, lay;
	fritzpnppci *card = inst->data;

	if (!hif)
		return(-EINVAL);
	if (IF_TYPE(hif) != IF_UP)
		return(-EINVAL);
	lay = layermask2layer(hif->layermask);
	if (lay < 0) {
		int_errtxt("lm %x", hif->layermask);
		return(-EINVAL);
	}
	switch(hif->protocol) {
	    case ISDN_PID_L1_B_64TRANS:
	    case ISDN_PID_L1_B_64HDLC:
		printk(KERN_DEBUG "fritz_add_if ch%d p:%x\n", channel, hif->protocol);
	    	if ((channel<0) || (channel>1))
	    		return(-EINVAL);
		hif->fdata = &card->bch[channel];
		hif->func = hdlc_down;
		inst->pid.protocol[lay] = hif->protocol;
		if (inst->up.stat == IF_NOACTIV) {
			printk(KERN_DEBUG "fritz_add_if set upif\n");
			inst->up.stat = IF_DOWN;
			inst->up.layermask = get_up_layer(inst->layermask);
			inst->up.protocol = get_protocol(inst->st, inst->up.layermask);
			err = fritz.ctrl(inst->st, MGR_ADDIF | REQUEST, &inst->up);
			if (err) {
				printk(KERN_WARNING "fritz_add_if up no if\n");
				inst->up.stat = IF_NOACTIV;
			}
		}
		break;
	    case ISDN_PID_L0_TE_S0:
	    	if (inst != &card->dch.inst)
	    		return(-EINVAL);
		hif->fdata = &card->dch;
		hif->func = ISAC_l1hw;
		if (inst->up.stat == IF_NOACTIV) {
			inst->up.stat = IF_DOWN;
			inst->up.layermask = get_up_layer(inst->layermask);
			inst->up.protocol = get_protocol(inst->st, inst->up.layermask);
			err = fritz.ctrl(inst->st, MGR_ADDIF | REQUEST, &inst->up);
			if (err)
				inst->up.stat = IF_NOACTIV;
		}
		break;
	    default:
		printk(KERN_WARNING "fritz_add_if: protocol %x not supported\n",
			hif->protocol);
		return(-EPROTONOSUPPORT);
	}
	return(0);
}

static int
del_if(hisaxinstance_t *inst, int channel, hisaxif_t *hif) {
	int err;
	fritzpnppci *card = inst->data;

	printk(KERN_DEBUG "fritz del_if lay %x/%x %p/%p\n", hif->layermask,
		hif->stat, hif->func, hif->fdata);
	if ((hif->func == inst->up.func) && (hif->fdata == inst->up.fdata)) {
		if (channel==2)
			DelIF(inst, &inst->up, ISAC_l1hw, &card->dch);
		else
			DelIF(inst, &inst->up, hdlc_down, &card->bch[channel]);
		inst->up.stat = IF_NOACTIV;
		inst->up.protocol = ISDN_PID_NONE;
		err = fritz.ctrl(inst->st, MGR_ADDIF | REQUEST, &inst->up);
	} else {
		printk(KERN_DEBUG "fritz del_if no if found\n");
		return(-EINVAL);
	}
	return(0);
}

static void
release_card(fritzpnppci *card) {

	lock_dev(card);
	outb(0, card->addr + 2);
	free_irq(card->irq, card);
	close_hdlcstate(card->bch + 1);
	close_hdlcstate(card->bch);
	free_isac(&card->dch);
	release_region(card->addr, 32);
	free_bchannel(&card->bch[1]);
	free_bchannel(&card->bch[0]);
	free_dchannel(&card->dch);
	REMOVE_FROM_LISTBASE(card, cardlist);
	unlock_dev(card);
	kfree(card);
	fritz.refcnt--;
}

static int
set_stack(hisaxstack_t *st, hisaxinstance_t *inst, int chan, hisax_pid_t *pid) {
	int err,layer = 0;

#if 0
	if (st->inst[0] || st->inst[1] || st->inst[2]) {
		return(-EBUSY);	
	}
#endif
	if (!HasProtocol(inst, pid->protocol[1])) {
		return(-EPROTONOSUPPORT);
	} else
		layer = ISDN_LAYER(1);
	if (HasProtocol(inst, pid->protocol[2])) {
		layer |= ISDN_LAYER(2);
		if (HasProtocol(inst, protocol[3]))
			layer |= ISDN_LAYER(3);
	}
	inst->layermask = layer;
	inst->pid.protocol[1] = pid->protocol[1];
	if ((err = fritz.ctrl(st, MGR_ADDLAYER | REQUEST, inst))) {
		printk(KERN_WARNING "set_stack MGR_ADDLAYER err(%d)\n", err);
		return(err);
	}
	inst->up.layermask = get_up_layer(layer);
	inst->up.protocol = get_protocol(st, inst->up.layermask);
	inst->up.inst = inst;
	inst->up.stat = IF_DOWN;
	if ((err = fritz.ctrl(st, MGR_ADDIF | REQUEST, &inst->up))) {
		printk(KERN_WARNING "set_stack MGR_ADDIF err(%d)\n", err);
		return(err);
	}
	return(0);	                                    
}

static int
fritz_manager(void *data, u_int prim, void *arg) {
	fritzpnppci *card = cardlist;
	hisaxinstance_t *inst=NULL;
	int channel = -1;
	hisaxstack_t *st = data;

	if (!data) {
		printk(KERN_ERR "fritz_manager no data prim %x arg %p\n",
			prim, arg);
		return(-EINVAL);
	}
	while(card) {
		if (card->dch.inst.st == st) {
			inst = &card->dch.inst;
			channel = 2;
			break;
		}
		if (card->bch[0].inst.st == st) {
			inst = &card->bch[0].inst;
			channel = 0;
			break;
		}
		if (card->bch[1].inst.st == st) {
			inst = &card->bch[1].inst;
			channel = 1;
			break;
		}
		card = card->next;
	}
	if (channel<0) {
		printk(KERN_ERR "fritz_manager no channel data %p prim %x arg %p\n",
			data, prim, arg);
		return(-EINVAL);
	}

	switch(prim) {
	    case MGR_ADDLAYER | CONFIRM:
		if (!card) {
			printk(KERN_WARNING "fritz_manager no card found\n");
			return(-ENODEV);
		}
		if (channel == 2) {
#if 0
			card->dch.inst.st->protocols[0] = ISDN_PID_L0_TE_S0;
			card->dch.inst.st->protocols[1] = ISDN_PID_L1_TE_S0;
			card->dch.inst.st->protocols[2] = ISDN_PID_L2_LAPD;
			if (act_protocol == 2) {
				card->dch.inst.st->protocols[3] = ISDN_PID_L3_DSS1USER;
				card->dch.inst.st->protocols[4] = ISDN_PID_CAPI20;
			} else {
				card->dch.inst.st->protocols[3] = ISDN_PID_NONE;
				card->dch.inst.st->protocols[4] = ISDN_PID_NONE;
			}
#endif
		} else {
			break;
		}
		break;
	    case MGR_DELLAYER | REQUEST:
		if (!card) {
			printk(KERN_WARNING "fritz_manager del layer request failed\n");
			return(-ENODEV);
		}
		if (channel==2)
			DelIF(inst, &inst->up, ISAC_l1hw, &card->dch);
		else
			DelIF(inst, &inst->up, hdlc_down, &card->bch[channel]);
		fritz.ctrl(st, MGR_DELLAYER | REQUEST, inst);
		break;
	    case MGR_RELEASE | INDICATION:
		if (!card) {
			printk(KERN_WARNING "fritz_manager no card found\n");
			return(-ENODEV);
		} else {
			if (channel == 2) {
				release_card(card);
			} else {
				fritz.refcnt--;
			}
		}
		break;
	    case MGR_ADDIF | REQUEST:
		if (!card) {
			printk(KERN_WARNING "fritz_manager interface request failed\n");
			return(-ENODEV);
		}
		return(add_if(inst, channel, arg));
		break;
	    case MGR_DELIF | REQUEST:
		if (!card) {
			printk(KERN_WARNING "fritz_manager del interface request failed\n");
			return(-ENODEV);
		}
		return(del_if(inst, channel, arg));
		break;
	    case MGR_SETSTACK | REQUEST:
		if (!card) {
			printk(KERN_WARNING "fritz_manager del interface request failed\n");
			return(-ENODEV);
		} else {
			return(set_stack(st, inst, channel, arg));
		}
		break;
	    default:
		printk(KERN_WARNING "fritz_manager prim %x not handled\n", prim);
		return(-EINVAL);
	}
	return(0);
}

int
Fritz_init(void)
{
	int err,i;
	fritzpnppci *card;

	fritz.name = FritzName;
	fritz.own_ctrl = fritz_manager;
	fritz.layermask = ISDN_LAYER(0);
	fritz.protocols = FritzProtocols;
	fritz.protcnt = FRITZPCNT;
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
		APPEND_TO_LIST(card, cardlist);
		card->dch.debug = debug;
		card->dch.inst.obj = &fritz;
		spin_lock_init(&card->devlock);
		card->dch.inst.lock = lock_dev;
		card->dch.inst.unlock = unlock_dev;
		card->dch.inst.data = card;
		card->dch.inst.layermask = ISDN_LAYER(0);
		card->dch.inst.pid.protocol[0] = ISDN_PID_L0_TE_S0;
		card->dch.inst.up.inst = &card->dch.inst;
		act_protocol = protocol[fritz_cnt];
		sprintf(card->dch.inst.id, "Fritz%d", fritz_cnt+1);
		init_dchannel(&card->dch);
		for (i=0; i<2; i++) {
			card->bch[i].inst.obj = &fritz;
			card->bch[i].inst.data = card;
			card->bch[i].inst.layermask = ISDN_LAYER(0);
			card->bch[i].inst.up.layermask = ISDN_LAYER(1);
			card->bch[i].inst.up.inst = &card->bch[i].inst;
			card->bch[i].inst.lock = lock_dev;
			card->bch[i].inst.unlock = unlock_dev;
			card->bch[i].debug = debug;
			sprintf(card->bch[i].inst.id, "%s B%d", card->dch.inst.id, i+1);
			init_bchannel(&card->bch[i]);
		}
		printk(KERN_DEBUG "fritz card %p dch %p bch1 %p bch2 %p\n",
			card, &card->dch, &card->bch[0], &card->bch[1]);
		if (setup_fritz(card, io[cfg_idx], irq[cfg_idx])) {
			err = 0;
			free_dchannel(&card->dch);
			free_bchannel(&card->bch[1]);
			free_bchannel(&card->bch[0]);
			REMOVE_FROM_LISTBASE(card, cardlist);
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
		if ((err = fritz.ctrl(NULL, MGR_ADDSTACK | REQUEST, &card->dch.inst))) {
			printk(KERN_ERR  "MGR_ADDSTACK REQUEST dch err(%d)\n", err);
			release_card(card);
			if (!fritz_cnt)
				HiSax_unregister(&fritz);
			else
				err = 0;
			return(err);
		}
		if ((err = fritz.ctrl(card->dch.inst.st, MGR_ADDLAYER | REQUEST, &card->dch.inst))) {
			printk(KERN_ERR  "MGR_ADDLAYER REQUEST dch err(%d)\n", err);
 			fritz.ctrl(card->dch.inst.st,
				MGR_DELSTACK | REQUEST, NULL);
			if (!fritz_cnt)
				HiSax_unregister(&fritz);
			else
				err = 0;
			return(err);
		}
		card->dch.inst.up.layermask = get_up_layer(card->dch.inst.layermask);
		card->dch.inst.up.stat = IF_DOWN;
		card->dch.inst.up.protocol = get_protocol(card->dch.inst.st,
			card->dch.inst.up.layermask);
		card->dch.inst.down.func = dummy_down;
		card->dch.inst.down.fdata = card;
		if ((err = fritz.ctrl(card->dch.inst.st, MGR_ADDIF | REQUEST,
			&card->dch.inst.up))) {
			printk(KERN_ERR  "MGR_ADDIF REQUEST dch err(%d)\n", err);
 			fritz.ctrl(card->dch.inst.st,
				MGR_DELSTACK | REQUEST, NULL);
			if (!fritz_cnt)
				HiSax_unregister(&fritz);
			else
				err = 0;
			return(err);
		}
		for (i=0; i<2; i++) {
			if ((err = fritz.ctrl(card->dch.inst.st, MGR_ADDSTACK | REQUEST,
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
	while(cardlist) {
		printk(KERN_ERR "Fritz PCI card struct not empty refs %d\n",
			fritz.refcnt);
		release_card(cardlist);
	}
	return(0);
}
#endif

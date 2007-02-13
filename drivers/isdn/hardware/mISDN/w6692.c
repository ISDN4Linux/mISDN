/* $Id: w6692.c,v 1.23 2007/02/13 10:43:45 crich Exp $

 * w6692.c     low level driver for CCD's hfc-pci based cards
 *
 * Author      Karsten Keil <kkeil@suse.de>
 *             based on the w6692 I4L driver from Petr Novak <petr.novak@i.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#include "core.h"
#include "channel.h"
#include "layer1.h"
#include "helper.h"
#include "debug.h"
#include "w6692.h"

#include <linux/isdn_compat.h>

extern const char *CardType[];

const char *w6692_rev = "$Revision: 1.23 $";

#define DBUSY_TIMER_VALUE	80

enum {
	W6692_ASUS,
	W6692_WINBOND,
	W6692_USR
};

/* private data in the PCI devices list */
typedef struct _w6692_map{
	u_int	subtype;
	char	*name;
} w6692_map_t;

static w6692_map_t w6692_map[] =
{
	{W6692_ASUS, "Dynalink/AsusCom IS64PH"},
	{W6692_WINBOND, "Winbond W6692"},
	{W6692_USR, "USR W6692"}
};

#ifndef PCI_VENDOR_ID_USR2
#define PCI_VENDOR_ID_USR2	0x16ec
#define PCI_DEVICE_ID_USR2_6692	0x3409
#endif

typedef struct _w6692_bc {
	struct timer_list	timer;
	u_char			b_mode;
} w6692_bc;

typedef struct _w6692pci {
	struct list_head	list;
	struct pci_dev		*pdev;
	u_int			subtype;
	u_int			irq;
	u_int			irqcnt;
	u_int			addr;
	int			pots;
	int			led;
	spinlock_t		lock;
	u_char			imask;
	u_char			pctl;
	u_char			xaddr;
	u_char			xdata;
	w6692_bc		wbc[2];
	channel_t		dch;
	channel_t		bch[2];
} w6692pci;

#define W_LED1_ON	1
#define W_LED1_S0STATUS	2

static __inline__ u_char
ReadW6692(w6692pci *card, u_char offset)
{
	return (inb(card->addr + offset));
}

static __inline__ void
WriteW6692(w6692pci *card, u_char offset, u_char value)
{
	outb(value, card->addr + offset);
}

static __inline__ u_char
ReadW6692B(w6692pci *card, int bchan, u_char offset)
{
	return (inb(card->addr + (bchan ? 0x40 : 0) + offset));
}

static __inline__ void
WriteW6692B(w6692pci *card, int bchan, u_char offset, u_char value)
{
	outb(value, card->addr + (bchan ? 0x40 : 0) + offset);
}

static void
enable_hwirq(w6692pci *card)
{
	WriteW6692(card, W_IMASK, card->imask);
}

static void
disable_hwirq(w6692pci *card)
{
	WriteW6692(card, W_IMASK, 0xff);
}

static char *W6692Ver[] __initdata =
{"W6692 V00", "W6692 V01", "W6692 V10",
 "W6692 V11"};

static void
W6692Version(w6692pci *card, char *s)
{
	int val;

	val = ReadW6692(card, W_D_RBCH);
	printk(KERN_INFO "%s Winbond W6692 version (%x): %s\n", s, val, W6692Ver[(val >> 6) & 3]);
}

static void
w6692_led_handler(w6692pci *card, int on)
{
	if (!card->led || card->subtype == W6692_USR)
		return;
	if (on) {
		card->xdata &= 0xfb;	/*  LED ON */
		WriteW6692(card, W_XDATA, card->xdata);
	} else {
		card->xdata |= 0x04;	/*  LED OFF */
		WriteW6692(card, W_XDATA, card->xdata);
	}
}

static void
ph_command(w6692pci *card, u_char command)
{
	if (card->dch.debug & L1_DEB_ISAC)
		mISDN_debugprint(&card->dch.inst, "ph_command %x", command);
	WriteW6692(card, W_CIX, command);
}

static void
W6692_new_ph(channel_t *dch)
{
	u_int		prim = PH_SIGNAL | INDICATION;
	u_int		para = 0;

	switch (dch->state) {
		case W_L1CMD_RST:
			ph_command(dch->hw, W_L1CMD_DRC);
			mISDN_queue_data(&dch->inst, FLG_MSG_UP, PH_CONTROL | INDICATION, HW_RESET, 0, NULL, 0);
			/* fall trough */
		case W_L1IND_CD:
			prim = PH_CONTROL | CONFIRM;
			para = HW_DEACTIVATE;
			test_and_clear_bit(FLG_ACTIVE, &dch->Flags);
			break;
		case W_L1IND_DRD:
			prim = PH_CONTROL | INDICATION;
			para = HW_DEACTIVATE;
			test_and_clear_bit(FLG_ACTIVE, &dch->Flags);
			break;
		case W_L1IND_CE:
			prim = PH_CONTROL | INDICATION;
			para = HW_POWERUP;
			break;
		case W_L1IND_LD:
			para = ANYSIGNAL;
			break;
		case W_L1IND_ARD:
			para = INFO2;
			break;
		case W_L1IND_AI8:
			para = INFO4_P8;
			test_and_set_bit(FLG_ACTIVE, &dch->Flags);
			break;
		case W_L1IND_AI10:
			para = INFO4_P10;
			test_and_set_bit(FLG_ACTIVE, &dch->Flags);
			break;
		default:
			return;
	}
	mISDN_queue_data(&dch->inst, FLG_MSG_UP, prim, para, 0, NULL, 0);
}

static void
W6692_empty_Dfifo(w6692pci *card, int count)
{
	channel_t	*dch = &card->dch;
	u_char		*ptr;

	if ((dch->debug & L1_DEB_ISAC) && !(dch->debug & L1_DEB_ISAC_FIFO))
		mISDN_debugprint(&dch->inst, "empty_Dfifo");

	if (!dch->rx_skb) {
		if (!(dch->rx_skb = alloc_stack_skb(dch->maxlen, dch->up_headerlen))) {
			printk(KERN_WARNING "mISDN: D receive out of memory\n");
			WriteW6692(card, W_D_CMDR, W_D_CMDR_RACK);
			return;
		}
	}
	if ((dch->rx_skb->len + count) >= dch->maxlen) {
		if (dch->debug & L1_DEB_WARN)
			mISDN_debugprint(&dch->inst, "empty_Dfifo overrun %d",
				dch->rx_skb->len + count);
		WriteW6692(card, W_D_CMDR, W_D_CMDR_RACK);
		return;
	}
	ptr = skb_put(dch->rx_skb, count);
	insb(card->addr + W_D_RFIFO, ptr, count);
	WriteW6692(card, W_D_CMDR, W_D_CMDR_RACK);
	if (dch->debug & L1_DEB_ISAC_FIFO) {
		char *t = dch->log;

		t += sprintf(t, "empty_Dfifo cnt %d", count);
		mISDN_QuickHex(t, ptr, count);
		mISDN_debugprint(&dch->inst, dch->log);
	}
}

static void
W6692_fill_Dfifo(w6692pci *card)
{
	channel_t	*dch = &card->dch;
	int		count;
	u_char		*ptr;
	u_char		cmd = W_D_CMDR_XMS;

	if ((dch->debug & L1_DEB_ISAC) && !(dch->debug & L1_DEB_ISAC_FIFO))
		mISDN_debugprint(&dch->inst, "fill_Dfifo");

	if (!dch->tx_skb)
		return;
	count = dch->tx_skb->len - dch->tx_idx;
	if (count <= 0)
		return;
	if (count > 32)
		count = 32;
	else
		cmd |= W_D_CMDR_XME;
	ptr = dch->tx_skb->data + dch->tx_idx;
	dch->tx_idx += count;
	outsb(card->addr + W_D_XFIFO, ptr, count);
	WriteW6692(card, W_D_CMDR, cmd);
	if (test_and_set_bit(FLG_BUSY_TIMER, &dch->Flags)) {
		mISDN_debugprint(&dch->inst, "fill_Dfifo dbusytimer running");
		del_timer(&dch->timer);
	}
	init_timer(&dch->timer);
	dch->timer.expires = jiffies + ((DBUSY_TIMER_VALUE * HZ)/1000);
	add_timer(&dch->timer);
	if (dch->debug & L1_DEB_ISAC_FIFO) {
		char *t = dch->log;

		t += sprintf(t, "fill_Dfifo cnt %d", count);
		mISDN_QuickHex(t, ptr, count);
		mISDN_debugprint(&dch->inst, dch->log);
	}
}

static void
d_retransmit(w6692pci *card)
{
	channel_t *dch = &card->dch;

	if (test_and_clear_bit(FLG_BUSY_TIMER, &dch->Flags))
		del_timer(&dch->timer);
#ifdef FIXME
	if (test_and_clear_bit(FLG_L1_BUSY, &dch->Flags))
		dchannel_sched_event(dch, D_CLEARBUSY);
#endif
	if (test_bit(FLG_TX_BUSY, &dch->Flags)) {
		/* Restart frame */
		dch->tx_idx = 0;
		W6692_fill_Dfifo(card);
	} else if (dch->tx_skb) { /* should not happen */
		int_error();
		test_and_set_bit(FLG_TX_BUSY, &dch->Flags);
		dch->tx_idx = 0;
		W6692_fill_Dfifo(card);
	} else {
		printk(KERN_WARNING "mISDN: w6692 XDU no TX_BUSY\n");
		mISDN_debugprint(&dch->inst, "XDU no TX_BUSY");
		if (test_bit(FLG_TX_NEXT, &dch->Flags)) {
			dch->tx_skb = dch->next_skb;
			if (dch->tx_skb) {
				mISDN_head_t	*hh = mISDN_HEAD_P(dch->tx_skb);

				dch->next_skb = NULL;
				test_and_clear_bit(FLG_TX_NEXT, &dch->Flags);
				dch->tx_idx = 0;
				queue_ch_frame(dch, CONFIRM, hh->dinfo, NULL);
				W6692_fill_Dfifo(card);
			} else {
				printk(KERN_WARNING "w6692 xdu irq TX_NEXT without skb\n");
				test_and_clear_bit(FLG_TX_NEXT, &dch->Flags);
			}
		}
	}
}

static void
handle_rxD(w6692pci *card) {
	u_char	stat;
	int	count;

	stat = ReadW6692(card, W_D_RSTA);
	if (stat & (W_D_RSTA_RDOV | W_D_RSTA_CRCE | W_D_RSTA_RMB)) {
		if (stat & W_D_RSTA_RDOV) {
			if (card->dch.debug & L1_DEB_WARN)
				mISDN_debugprint(&card->dch.inst, "D-channel RDOV");
#ifdef ERROR_STATISTIC
			card->dch.err_rx++;
#endif
		}
		if (stat & W_D_RSTA_CRCE) {
			if (card->dch.debug & L1_DEB_WARN)
				mISDN_debugprint(&card->dch.inst, "D-channel CRC error");
#ifdef ERROR_STATISTIC
			card->dch.err_crc++;
#endif
		}
		if (stat & W_D_RSTA_RMB) {
			if (card->dch.debug & L1_DEB_WARN)
				mISDN_debugprint(&card->dch.inst, "D-channel ABORT");
#ifdef ERROR_STATISTIC
			card->dch.err_rx++;
#endif
		}
		if (card->dch.rx_skb)
			dev_kfree_skb(card->dch.rx_skb);
		WriteW6692(card, W_D_CMDR, W_D_CMDR_RACK | W_D_CMDR_RRST);
	} else {
		count = ReadW6692(card, W_D_RBCL) & (W_D_FIFO_THRESH - 1);
		if (count == 0)
			count = W_D_FIFO_THRESH;
		W6692_empty_Dfifo(card, count);
		if (card->dch.rx_skb)
			mISDN_queueup_newhead(&card->dch.inst, 0, PH_DATA_IND,
				MISDN_ID_ANY, card->dch.rx_skb);
	}
	card->dch.rx_skb = NULL;
}

static void
handle_txD(w6692pci *card) {
	register channel_t	*dch = &card->dch;

	if (test_and_clear_bit(FLG_BUSY_TIMER, &dch->Flags))
		del_timer(&dch->timer);
#ifdef FIXME
	if (test_and_clear_bit(FLG_L1_BUSY, &dch->Flags))
		dchannel_sched_event(dch, D_CLEARBUSY);
#endif
	if (dch->tx_skb && dch->tx_idx < dch->tx_skb->len) {
		W6692_fill_Dfifo(card);
	} else {
		if (dch->tx_skb)
			dev_kfree_skb(dch->tx_skb);
		if (test_bit(FLG_TX_NEXT, &dch->Flags)) {
			dch->tx_skb = dch->next_skb;
			if (dch->tx_skb) {
				mISDN_head_t	*hh = mISDN_HEAD_P(dch->tx_skb);

				dch->next_skb = NULL;
				test_and_clear_bit(FLG_TX_NEXT, &dch->Flags);
				dch->tx_idx = 0;
				queue_ch_frame(dch, CONFIRM, hh->dinfo, NULL);
				W6692_fill_Dfifo(card);
			} else {
				printk(KERN_WARNING "w6692 txD irq TX_NEXT without skb\n");
				test_and_clear_bit(FLG_TX_NEXT, &dch->Flags);
				test_and_clear_bit(FLG_TX_BUSY, &dch->Flags);
			}
		} else {
			dch->tx_skb = NULL;
			test_and_clear_bit(FLG_TX_BUSY, &dch->Flags);
		}
	}
}

static void
handle_statusD(w6692pci *card)
{
	register channel_t	*dch = &card->dch;
	u_char			exval, v1, cir;

	exval = ReadW6692(card, W_D_EXIR);
	
	if (card->dch.debug & L1_DEB_ISAC)
		mISDN_debugprint(&card->dch.inst, "D_EXIR %02x", exval);
	if (exval & (W_D_EXI_XDUN | W_D_EXI_XCOL)) {	/* Transmit underrun/collision */
		if (card->dch.debug & L1_DEB_WARN)
			mISDN_debugprint(&card->dch.inst, "D-channel underrun/collision");
#ifdef ERROR_STATISTIC
		dch->err_tx++;
#endif
		d_retransmit(card);
	}
	if (exval & W_D_EXI_RDOV) {	/* RDOV */
		if (card->dch.debug & L1_DEB_WARN)
			mISDN_debugprint(&card->dch.inst, "D-channel RDOV");
		WriteW6692(card, W_D_CMDR, W_D_CMDR_RRST);
	}
	if (exval & W_D_EXI_TIN2)	/* TIN2 - never */
		if (card->dch.debug & L1_DEB_WARN)
			mISDN_debugprint(&card->dch.inst, "spurious TIN2 interrupt");
	if (exval & W_D_EXI_MOC) {	/* MOC - not supported */
		v1 = ReadW6692(card, W_MOSR);
		if (card->dch.debug & L1_DEB_ISAC) {
			mISDN_debugprint(&card->dch.inst, "spurious MOC interrupt");
			mISDN_debugprint(&card->dch.inst, "MOSR %02x", v1);
		}
	}
	if (exval & W_D_EXI_ISC) {	/* ISC - Level1 change */
		cir = ReadW6692(card, W_CIR);
		if (card->dch.debug & L1_DEB_ISAC)
			mISDN_debugprint(&card->dch.inst, "ISC CIR=0x%02X", cir);
		if (cir & W_CIR_ICC) {
			v1 = cir & W_CIR_COD_MASK;
			if (card->dch.debug & L1_DEB_ISAC)
				mISDN_debugprint(&card->dch.inst, "ph_state_change %x -> %x",
					dch->state, v1);
			dch->state = v1;
			if (card->led & W_LED1_S0STATUS) {
				switch (v1) {
					case W_L1IND_AI8:
					case W_L1IND_AI10:
						w6692_led_handler(card, 1);
						break;
					default:
						w6692_led_handler(card, 0);
						break;
				}
			}
			W6692_new_ph(dch);
		}
		if (cir & W_CIR_SCC) {
			v1 = ReadW6692(card, W_SQR);
			if (card->dch.debug & L1_DEB_ISAC)
				mISDN_debugprint(&card->dch.inst, "SCC SQR=0x%02X", v1);
		}
	}
	if (exval & W_D_EXI_WEXP) {
		if (card->dch.debug & L1_DEB_WARN)
			mISDN_debugprint(&card->dch.inst, "spurious WEXP interrupt!");
	}
	if (exval & W_D_EXI_TEXP) {
		if (card->dch.debug & L1_DEB_WARN)
			mISDN_debugprint(&card->dch.inst, "spurious TEXP interrupt!");
	}
}

static void
W6692_empty_Bfifo(channel_t *bch, int count)
{
	u_char		*ptr;
	w6692pci	*card = bch->inst.privat;

	if ((bch->debug & L1_DEB_HSCX) && !(bch->debug & L1_DEB_HSCX_FIFO))
		mISDN_debugprint(&bch->inst, "empty_Bfifo %d", count);
	if (unlikely(bch->state == ISDN_PID_NONE)) {
		if (bch->debug & L1_DEB_WARN)
			mISDN_debugprint(&bch->inst, "empty_Bfifo ISDN_PID_NONE");
		WriteW6692B(card, bch->channel, W_B_CMDR, W_B_CMDR_RACK | W_B_CMDR_RACT);
		if (bch->rx_skb)
			skb_trim(bch->rx_skb, 0);
		return;
	}
	if (!bch->rx_skb) {
		bch->rx_skb = alloc_stack_skb(bch->maxlen, bch->up_headerlen);
		if (unlikely(!bch->rx_skb)) {
			printk(KERN_WARNING "mISDN: B receive out of memory\n");
			WriteW6692B(card, bch->channel, W_B_CMDR, W_B_CMDR_RACK | W_B_CMDR_RACT);
			return;
		}
	}
	if (bch->rx_skb->len + count > bch->maxlen) {
		if (bch->debug & L1_DEB_WARN)
			mISDN_debugprint(&bch->inst, "empty_Bfifo incoming packet too large");
		WriteW6692B(card, bch->channel, W_B_CMDR, W_B_CMDR_RACK | W_B_CMDR_RACT);
		skb_trim(bch->rx_skb, 0);
		return;
	}
	ptr = skb_put(bch->rx_skb, count);
	insb(card->addr + W_B_RFIFO + (bch->channel ? 0x40 : 0), ptr, count);
	WriteW6692B(card, bch->channel, W_B_CMDR, W_B_CMDR_RACK | W_B_CMDR_RACT);

	if (bch->debug & L1_DEB_HSCX_FIFO) {
		char *t = bch->log;

		t += sprintf(t, "empty_Bfifo B%d cnt %d", bch->channel, count);
		mISDN_QuickHex(t, ptr, count);
		mISDN_debugprint(&bch->inst, bch->log);
	}
}

static void
W6692_fill_Bfifo(channel_t *bch)
{
	w6692pci	*card = bch->inst.privat;
	int		count;
	u_char		*ptr, cmd = W_B_CMDR_RACT | W_B_CMDR_XMS;

	if ((bch->debug & L1_DEB_HSCX) && !(bch->debug & L1_DEB_HSCX_FIFO))
		mISDN_debugprint(&bch->inst, "%s", __FUNCTION__);
	if (!bch->tx_skb)
		return;
	count = bch->tx_skb->len - bch->tx_idx;
	if (count <= 0)
		return;
	ptr = bch->tx_skb->data + bch->tx_idx;
	if (count > W_B_FIFO_THRESH) {
		count = W_B_FIFO_THRESH;
	} else {
		if (test_bit(FLG_HDLC, &bch->Flags))
			cmd |= W_B_CMDR_XME;
	}
	if ((bch->debug & L1_DEB_HSCX) && !(bch->debug & L1_DEB_HSCX_FIFO))
		mISDN_debugprint(&bch->inst, "%s: %d/%d", __FUNCTION__,
			count, bch->tx_idx);
	bch->tx_idx += count;
	outsb(card->addr + W_B_XFIFO + (bch->channel ? 0x40 : 0), ptr, count);
	WriteW6692B(card, bch->channel, W_B_CMDR, cmd);
	if (bch->debug & L1_DEB_HSCX_FIFO) {
		char *t = bch->log;

		t += sprintf(t, "fill_Bfifo  B%d cnt %d",
			     bch->channel, count);
		mISDN_QuickHex(t, ptr, count);
		mISDN_debugprint(&bch->inst, bch->log);
	}
}

static int
setvolume(channel_t *bch, int mic, struct sk_buff *skb)
{
	w6692pci	*card = bch->inst.privat;
	u16		*vol = (u16 *)skb->data;
	u_char		val;

	if ((card->pots == 0) || !test_bit(FLG_TRANSPARENT, &bch->Flags))
		return(-ENODEV);
	if (skb->len < 2)
		return(-EINVAL);
	if (*vol > 7)
		return(-EINVAL);
	val = *vol & 7;
	val = 7 - val;
	if (mic) {
		val <<= 3;
		card->xaddr &= 0xc7;
	} else {
		card->xaddr &= 0xf8;
	}
	card->xaddr |= val;
	WriteW6692(card, W_XADDR, card->xaddr);
	return(0);
}

static int
enable_pots(channel_t *bch)
{
	w6692_bc	*bhw  = bch->hw;
	w6692pci	*card = bch->inst.privat;

	if ((card->pots == 0) || !test_bit(FLG_TRANSPARENT, &bch->Flags))
		return(-ENODEV);
	
	bhw->b_mode |= W_B_MODE_EPCM | W_B_MODE_BSW0;
	WriteW6692B(card, bch->channel, W_B_MODE, bhw->b_mode);
	WriteW6692B(card, bch->channel, W_B_CMDR, W_B_CMDR_RRST | W_B_CMDR_XRST);
	card->pctl |= (bch->channel ? W_PCTL_PCX : 0);
	WriteW6692(card, W_PCTL, card->pctl);
	return(0);
}

static int
disable_pots(channel_t *bch)
{
	w6692_bc	*bhw  = bch->hw;
	w6692pci	*card = bch->inst.privat;

	if (card->pots == 0)
		return(-ENODEV);
	bhw->b_mode &= ~(W_B_MODE_EPCM | W_B_MODE_BSW0);
	WriteW6692B(card, bch->channel, W_B_MODE, bhw->b_mode);
	WriteW6692B(card, bch->channel, W_B_CMDR, W_B_CMDR_RRST | W_B_CMDR_RACT | W_B_CMDR_XRST);
	return(0);
}

static int
mode_w6692(channel_t *bch, int bc, int protocol)
{
	w6692pci	*card = bch->inst.privat;
	w6692_bc	*bhw  = bch->hw;
	

	if (bch->debug & L1_DEB_HSCX)
		mISDN_debugprint(&bch->inst, "B%d protocol %x-->%x ch %d-->%d",
			bch->channel, bch->state, protocol, bch->channel, bc);
	switch (protocol) {
		case (-1): /* used for init */
			bch->state = -1;
			bch->channel = bc;
		case (ISDN_PID_NONE):
			if (bch->state == ISDN_PID_NONE)
				break;
			bch->state = ISDN_PID_NONE;
			if (card->pots && (bhw->b_mode & W_B_MODE_EPCM))
				disable_pots(bch);
			bhw->b_mode = 0;
			bch->tx_idx = 0;
			if (bch->next_skb) {
				dev_kfree_skb(bch->next_skb);
				bch->next_skb = NULL;
			}
			if (bch->tx_skb) {
				dev_kfree_skb(bch->tx_skb);
				bch->tx_skb = NULL;
			}
			if (bch->rx_skb) {
				dev_kfree_skb(bch->rx_skb);
				bch->rx_skb = NULL;
			}
			WriteW6692B(card, bch->channel, W_B_MODE, bhw->b_mode);
			WriteW6692B(card, bch->channel, W_B_CMDR, W_B_CMDR_RRST | W_B_CMDR_XRST);
			test_and_clear_bit(FLG_HDLC, &bch->Flags);
			test_and_clear_bit(FLG_TRANSPARENT, &bch->Flags);
			break;
		case (ISDN_PID_L1_B_64TRANS):
			bch->state = protocol;
			bhw->b_mode = W_B_MODE_MMS;
			WriteW6692B(card, bch->channel, W_B_MODE, bhw->b_mode);
			WriteW6692B(card, bch->channel, W_B_EXIM, 0);
			WriteW6692B(card, bch->channel, W_B_CMDR, W_B_CMDR_RRST | W_B_CMDR_RACT | W_B_CMDR_XRST);
			test_and_set_bit(FLG_TRANSPARENT, &bch->Flags);
			break;
		case (ISDN_PID_L1_B_64HDLC):
			bch->state = protocol;
			bhw->b_mode = W_B_MODE_ITF;
			WriteW6692B(card, bch->channel, W_B_MODE, bhw->b_mode);
			WriteW6692B(card, bch->channel, W_B_ADM1, 0xff);
			WriteW6692B(card, bch->channel, W_B_ADM2, 0xff);
			WriteW6692B(card, bch->channel, W_B_EXIM, 0);
			WriteW6692B(card, bch->channel, W_B_CMDR, W_B_CMDR_RRST | W_B_CMDR_RACT | W_B_CMDR_XRST);
			test_and_set_bit(FLG_HDLC, &bch->Flags);
			break;
		default:
			mISDN_debugprint(&bch->inst, "prot not known %x", protocol);
			return(-ENOPROTOOPT);
	}
	return(0);
}

static void
send_next(channel_t *bch)
{
	if (test_bit(FLG_ACTIVE, &bch->Flags))
		return;
	if (bch->tx_skb && bch->tx_idx < bch->tx_skb->len)
		W6692_fill_Bfifo(bch);
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
				W6692_fill_Bfifo(bch);
			} else {
				test_and_clear_bit(FLG_TX_NEXT, &bch->Flags);
				printk(KERN_WARNING "W6692 tx irq TX_NEXT without skb\n");
				test_and_clear_bit(FLG_TX_BUSY, &bch->Flags);
			}
		} else {
			bch->tx_skb = NULL;
			test_and_clear_bit(FLG_TX_BUSY, &bch->Flags);
		}
	}
}

static void
W6692B_interrupt(w6692pci *card, int ch)
{
	channel_t	*bch = &card->bch[ch];
	int		count;
	u_char		stat, star = 0;
	struct sk_buff	*skb;

	stat = ReadW6692B(card, ch, W_B_EXIR);
	if (bch->debug & L1_DEB_HSCX)
		mISDN_debugprint(&bch->inst, "ch%d stat %#x", bch->channel, stat);

	if (stat & W_B_EXI_RME) {
		star = ReadW6692B(card, ch, W_B_STAR);
		if (star & (W_B_STAR_RDOV | W_B_STAR_CRCE | W_B_STAR_RMB)) {
			if ((star & W_B_STAR_RDOV) && test_bit(FLG_ACTIVE, &bch->Flags)) {
				if (bch->debug & L1_DEB_WARN)
					mISDN_debugprint(&bch->inst, "B%d RDOV protocol=%x",
						ch +1, bch->state);
#ifdef ERROR_STATISTIC
				bch->err_rdo++;
#endif
			}
			if (test_bit(FLG_HDLC, &bch->Flags)) {
				if (star & W_B_STAR_CRCE) {
					if (bch->debug & L1_DEB_WARN)
						mISDN_debugprint(&bch->inst,
							"B%d CRC error", ch+1);
#ifdef ERROR_STATISTIC
					bch->err_crc++;
#endif
				}
				if (star & W_B_STAR_RMB) {
					if (bch->debug & L1_DEB_WARN)
						mISDN_debugprint(&bch->inst,
							"B%d message abort", ch+1);
#ifdef ERROR_STATISTIC
					bch->err_inv++;
#endif
				}
			}
			WriteW6692B(card, ch, W_B_CMDR, W_B_CMDR_RACK | W_B_CMDR_RRST | W_B_CMDR_RACT);
			if (bch->rx_skb)
				skb_trim(bch->rx_skb, 0);
		} else {
			count = ReadW6692B(card, ch, W_B_RBCL) & (W_B_FIFO_THRESH - 1);
			if (count == 0)
				count = W_B_FIFO_THRESH;
			W6692_empty_Bfifo(bch, count);
			if (bch->rx_skb && bch->rx_skb->len > 0) {
				if (bch->debug & L1_DEB_HSCX)
					mISDN_debugprint(&bch->inst, "Bchan Frame %d", bch->rx_skb->len);
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
			}
		}
	}
	if (stat & W_B_EXI_RMR) {
		if (!(stat & W_B_EXI_RME))
			star = ReadW6692B(card, ch, W_B_STAR);
		if (star & W_B_STAR_RDOV) {
			if (bch->debug & L1_DEB_WARN)
				mISDN_debugprint(&bch->inst, "B%d RDOV protocol=%x",
					ch +1, bch->state);
#ifdef ERROR_STATISTIC
			bch->err_rdo++;
#endif
			WriteW6692B(card, ch, W_B_CMDR, W_B_CMDR_RACK | W_B_CMDR_RRST | W_B_CMDR_RACT);
		} else {
			W6692_empty_Bfifo(bch, W_B_FIFO_THRESH);
			if (test_bit(FLG_TRANSPARENT, &bch->Flags) &&
				bch->rx_skb && (bch->rx_skb->len > 0)) {
				/* receive audio data */
				if (bch->debug & L1_DEB_HSCX)
					mISDN_debugprint(&bch->inst,
						"Bchan Frame %d", bch->rx_skb->len);
				queue_ch_frame(bch, INDICATION, MISDN_ID_ANY, bch->rx_skb);
				bch->rx_skb = NULL;
			}
		}
	}
	if (stat & W_B_EXI_RDOV) {
		if (!(star & W_B_STAR_RDOV)) { /* only if it is not handled yet */
			if (bch->debug & L1_DEB_WARN)
				mISDN_debugprint(&bch->inst, "B%d RDOV IRQ protocol=%x",
					ch +1, bch->state);
#ifdef ERROR_STATISTIC
			bch->err_rdo++;
#endif
			WriteW6692B(card, ch, W_B_CMDR, W_B_CMDR_RACK | W_B_CMDR_RRST | W_B_CMDR_RACT);
		}
	}
	if (stat & W_B_EXI_XFR) {
		if (!(stat & (W_B_EXI_RME | W_B_EXI_RMR))) {
			star = ReadW6692B(card, ch, W_B_STAR);
			if (bch->debug & L1_DEB_HSCX)
				mISDN_debugprint(&bch->inst, "B%d star %02x", ch +1, star);
		}
		if (star & W_B_STAR_XDOW) {
			if (bch->debug & L1_DEB_WARN)
				mISDN_debugprint(&bch->inst, "B%d XDOW protocol=%x",
					ch +1, bch->state);
#ifdef ERROR_STATISTIC
			bch->err_xdu++;
#endif
			WriteW6692B(card, ch, W_B_CMDR, W_B_CMDR_XRST | W_B_CMDR_RACT);
			/* resend */
			if (bch->tx_skb) {
				if (!test_bit(FLG_TRANSPARENT, &bch->Flags))
					bch->tx_idx = 0;
			}
		}
		send_next(bch);
		if (stat & W_B_EXI_XDUN)
			return; /* handle XDOW only once */
	}
	if (stat & W_B_EXI_XDUN) {
		if (bch->debug & L1_DEB_WARN)
			mISDN_debugprint(&bch->inst, "B%d XDUN protocol=%x",
				ch +1, bch->state);
#ifdef ERROR_STATISTIC
		bch->err_xdu++;
#endif
		WriteW6692B(card, ch, W_B_CMDR, W_B_CMDR_XRST | W_B_CMDR_RACT);
		/* resend */
		if (bch->tx_skb) {
			if (!test_bit(FLG_TRANSPARENT, &bch->Flags))
				bch->tx_idx = 0;
		}
		send_next(bch);
	}
}

static irqreturn_t
w6692_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	w6692pci	*card = dev_id;
	u_char		ista;

	spin_lock(&card->lock);
	ista = ReadW6692(card, W_ISTA);
	if ((ista | card->imask) == card->imask) {
		/* possible a shared  IRQ reqest */
		spin_unlock(&card->lock);
		return IRQ_NONE;
	}
	card->irqcnt++;
/* Begin IRQ handler */
	if (card->dch.debug & L1_DEB_ISAC)
		mISDN_debugprint(&card->dch.inst, "ista %02x", ista);
	ista &= ~card->imask;
	if (ista & W_INT_B1_EXI)
		W6692B_interrupt(card, 0);
	if (ista & W_INT_B2_EXI)
		W6692B_interrupt(card, 1);
	if (ista & W_INT_D_RME)
		handle_rxD(card);
	if (ista & W_INT_D_RMR)
		W6692_empty_Dfifo(card, W_D_FIFO_THRESH);
	if (ista & W_INT_D_XFR)
		handle_txD(card);
	if (ista & W_INT_D_EXI)
		handle_statusD(card);
	if (ista & (W_INT_XINT0 | W_INT_XINT1)) /* XINT0/1 - never */
		mISDN_debugprint(&card->dch.inst, "W6692 spurious XINT!");
/* End IRQ Handler */
	spin_unlock(&card->lock);
	return IRQ_HANDLED;
}

static void
dbusy_timer_handler(channel_t *dch)
{
	w6692pci	*card = dch->hw;
	int		rbch, star;
	u_long		flags;

	if (test_bit(FLG_BUSY_TIMER, &dch->Flags)) {
		spin_lock_irqsave(dch->inst.hwlock, flags);
		rbch = ReadW6692(card, W_D_RBCH);
		star = ReadW6692(card, W_D_STAR);
		if (dch->debug) 
			mISDN_debugprint(&dch->inst, "D-Channel Busy RBCH %02x STAR %02x",
				rbch, star);
		if (star & W_D_STAR_XBZ) {	/* D-Channel Busy */
			test_and_set_bit(FLG_L1_BUSY, &dch->Flags);
		} else {
			/* discard frame; reset transceiver */
			test_and_clear_bit(FLG_BUSY_TIMER, &dch->Flags);
			if (dch->tx_idx) {
				dch->tx_idx = 0;
			} else {
				printk(KERN_WARNING "mISDN: W6692 D-Channel Busy no tx_idx\n");
				mISDN_debugprint(&dch->inst, "D-Channel Busy no tx_idx");
			}
			/* Transmitter reset */
			WriteW6692(card, W_D_CMDR, W_D_CMDR_XRST);	/* Transmitter reset */
		}
		spin_unlock_irqrestore(dch->inst.hwlock, flags);
	}
}

void initW6692(w6692pci *card)
{
	u_char	val;

	card->dch.timer.function = (void *) dbusy_timer_handler;
	card->dch.timer.data = (u_long) &card->dch;
	init_timer(&card->dch.timer);
	mode_w6692(&card->bch[0], 0, -1);
	mode_w6692(&card->bch[1], 1, -1);
	WriteW6692(card, W_D_CTL, 0x00);
	disable_hwirq(card);
	WriteW6692(card, W_D_SAM, 0xff);
	WriteW6692(card, W_D_TAM, 0xff);
	WriteW6692(card, W_D_MODE, W_D_MODE_RACT);
	card->dch.state = W_L1CMD_RST;
	ph_command(card, W_L1CMD_RST);
	ph_command(card, W_L1CMD_ECK);
	/* enable all IRQ but extern */
	card->imask = 0x18;
	WriteW6692(card, W_D_EXIM, 0x00);
	WriteW6692B(card, 0, W_B_EXIM, 0);
	WriteW6692B(card, 1, W_B_EXIM, 0);
	/* Reset D-chan receiver and transmitter */
	WriteW6692(card, W_D_CMDR, W_D_CMDR_RRST | W_D_CMDR_XRST);
	/* Reset B-chan receiver and transmitter */
	WriteW6692B(card, 0, W_B_CMDR, W_B_CMDR_RRST | W_B_CMDR_XRST);
	WriteW6692B(card, 1, W_B_CMDR, W_B_CMDR_RRST | W_B_CMDR_XRST);
	/* enable peripheral */
	if (card->subtype == W6692_USR) {
		/* seems that USR implemented some power control features
		 * Pin 79 is connected to the oscilator circuit so we
		 * have to handle it here
		 */
		card->pctl = 0x80;
		card->xdata = 0;
		WriteW6692(card, W_PCTL, card->pctl);
		WriteW6692(card, W_XDATA, card->xdata);
	} else {
		card->pctl = W_PCTL_OE5 | W_PCTL_OE4 | W_PCTL_OE2 | W_PCTL_OE1 | W_PCTL_OE0;
		if (card->pots) {
			card->xaddr = 0x00; /* all sw off */
			card->xdata |= 0x06;  /*  POWER UP/ LED OFF / ALAW */
			WriteW6692(card, W_PCTL, card->pctl);
			WriteW6692(card, W_XADDR, card->xaddr);
			WriteW6692(card, W_XDATA, card->xdata);
			val = ReadW6692(card, W_XADDR);
			if (card->dch.debug & L1_DEB_ISAC)
				mISDN_debugprint(&card->dch.inst, "W_XADDR: %02x", val);
			if (card->led & W_LED1_ON)
				w6692_led_handler(card, 1);
			else
				w6692_led_handler(card, 0);
		}
	}
}

static void reset_w6692(w6692pci *card)
{
	WriteW6692(card, W_D_CTL, W_D_CTL_SRST);
	mdelay(10);
	WriteW6692(card, W_D_CTL, 0);
}

static int init_card(w6692pci *card)
{
	int	cnt = 3;
	u_long	flags;

	spin_lock_irqsave(&card->lock, flags);
	disable_hwirq(card);
	spin_unlock_irqrestore(&card->lock, flags);
	if (request_irq(card->irq, w6692_interrupt, SA_SHIRQ, "w6692", card)) {
		printk(KERN_WARNING "mISDN: couldn't get interrupt %d\n", card->irq);
		return(-EIO);
	}
	spin_lock_irqsave(&card->lock, flags);
	while (cnt) {
		initW6692(card);
		enable_hwirq(card);
		spin_unlock_irqrestore(&card->lock, flags);
		/* Timeout 10ms */
		current->state = TASK_UNINTERRUPTIBLE;
		schedule_timeout((10*HZ)/1000);
		printk(KERN_INFO "w6692: IRQ %d count %d\n",
			card->irq, card->irqcnt);
		if (!card->irqcnt) {
			printk(KERN_WARNING
			       "w6692: IRQ(%d) getting no interrupts during init %d\n",
			       card->irq, 4 - cnt);
			if (cnt == 1) {
				return (-EIO);
			} else {
				spin_lock_irqsave(&card->lock, flags);
				reset_w6692(card);
				cnt--;
			}
		} else {
			return(0);
		}
	}
	spin_unlock_irqrestore(&card->lock, flags);
	return(-EIO);
}

#define MAX_CARDS	4
static int w6692_cnt;
static u_int protocol[MAX_CARDS];
static int layermask[MAX_CARDS];

static mISDNobject_t	w6692;
static int debug;
static int pots[MAX_CARDS];
static int led[MAX_CARDS];


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
static int num_led=0, num_pots=0, num_protocol=0, num_layermask=0; 
module_param_array(led, uint, num_led, S_IRUGO | S_IWUSR);
module_param_array(pots, uint, num_pots, S_IRUGO | S_IWUSR);
module_param_array(protocol, uint, num_protocol, S_IRUGO | S_IWUSR);
module_param_array(layermask, uint, num_layermask, S_IRUGO | S_IWUSR);
#else 
module_param_array(led, uint, NULL, S_IRUGO | S_IWUSR);
module_param_array(pots, uint, NULL, S_IRUGO | S_IWUSR);
module_param_array(protocol, uint, NULL, S_IRUGO | S_IWUSR);
module_param_array(layermask, uint, NULL, S_IRUGO | S_IWUSR);
#endif
#endif
#endif

/******************************/
/* Layer2 -> Layer 1 Transfer */
/******************************/
static int
w6692_bmsg(channel_t *bch, struct sk_buff *skb)
{
	int		ret = 0;
	mISDN_head_t	*hh = mISDN_HEAD_P(skb);
	u_long		flags;

	if ((hh->prim == (PH_ACTIVATE | REQUEST)) ||
		(hh->prim == (DL_ESTABLISH  | REQUEST))) {
		if (!test_and_set_bit(FLG_ACTIVE, &bch->Flags)) {
			spin_lock_irqsave(bch->inst.hwlock, flags);
			ret = mode_w6692(bch, bch->channel, bch->inst.pid.protocol[1]);
			spin_unlock_irqrestore(bch->inst.hwlock, flags);
		}
#ifdef FIXME
		if (bch->inst.pid.protocol[2] == ISDN_PID_L2_B_RAWDEV)
			if (bch->dev)
				if_link(&bch->dev->rport.pif,
					hh->prim | CONFIRM, 0, 0, NULL, 0);
#endif
		skb_trim(skb, 0);
		return(mISDN_queueup_newhead(&bch->inst, 0,
			hh->prim | CONFIRM, ret, skb));
	} else if ((hh->prim == (PH_DEACTIVATE | REQUEST)) ||
		(hh->prim == (DL_RELEASE | REQUEST)) ||
		((hh->prim == (PH_CONTROL | REQUEST) &&
		(hh->dinfo == HW_DEACTIVATE)))) {
		spin_lock_irqsave(bch->inst.hwlock, flags);
		if (test_and_clear_bit(FLG_TX_NEXT, &bch->Flags)) {
			dev_kfree_skb(bch->next_skb);
			bch->next_skb = NULL;
		}
		if (bch->tx_skb) {
			dev_kfree_skb(bch->tx_skb);
			bch->tx_skb = NULL;
		}
		if (bch->rx_skb) {
			dev_kfree_skb(bch->rx_skb);
			bch->rx_skb = NULL;
		}
		test_and_clear_bit(FLG_TX_BUSY, &bch->Flags);
		mode_w6692(bch, bch->channel, ISDN_PID_NONE);
		test_and_clear_bit(FLG_ACTIVE, &bch->Flags);
		spin_unlock_irqrestore(bch->inst.hwlock, flags);
		skb_trim(skb, 0);
		if (hh->prim != (PH_CONTROL | REQUEST)) {
#ifdef FIXME
			if (bch->inst.pid.protocol[2] == ISDN_PID_L2_B_RAWDEV)
				if (bch->dev)
					if_link(&bch->dev->rport.pif,
						hh->prim | CONFIRM, 0, 0, NULL, 0);
#endif
			if (!mISDN_queueup_newhead(&bch->inst, 0,
				hh->prim | CONFIRM, 0, skb))
				return(0);
		}
	} else if (hh->prim == (PH_CONTROL | REQUEST)) {
		spin_lock_irqsave(bch->inst.hwlock, flags);
		if (hh->dinfo == HW_POTS_ON) {
			ret = enable_pots(bch);
		} else if (hh->dinfo == HW_POTS_OFF) {
			ret = disable_pots(bch);
		} else if (hh->dinfo == HW_POTS_SETMICVOL) {
			ret = setvolume(bch, 1, skb);
		} else if (hh->dinfo == HW_POTS_SETSPKVOL) {
			ret = setvolume(bch, 0, skb);
		} else
			ret = -EINVAL;
		spin_unlock_irqrestore(bch->inst.hwlock, flags);
	} else
		ret = -EAGAIN;
	if (!ret)
		dev_kfree_skb(skb);
	return(ret);
}

static int
w6692_dmsg(channel_t *dch, struct sk_buff *skb)
{
	int		ret = 0;
	mISDN_head_t	*hh = mISDN_HEAD_P(skb);
	u_long		flags;

	if (hh->prim == (PH_SIGNAL | REQUEST)) {
		spin_lock_irqsave(dch->inst.hwlock, flags);
		if (hh->dinfo == INFO3_P8)
			ph_command(dch->hw, W_L1CMD_AR8);
		else if (hh->dinfo == INFO3_P10)
			ph_command(dch->hw, W_L1CMD_AR10);
		else
			ret = -EINVAL;
		spin_unlock_irqrestore(dch->inst.hwlock, flags);
	} else if (hh->prim == (PH_CONTROL | REQUEST)) {
		spin_lock_irqsave(dch->inst.hwlock, flags);
		if (hh->dinfo == HW_RESET) {
			if (dch->state != W_L1IND_DRD)
				ph_command(dch->hw, W_L1CMD_RST);
			ph_command(dch->hw, W_L1CMD_ECK);
		} else if (hh->dinfo == HW_POWERUP) {
			ph_command(dch->hw, W_L1CMD_ECK);
		} else if (hh->dinfo == HW_DEACTIVATE) {
			if (dch->next_skb) {
				dev_kfree_skb(dch->next_skb);
				dch->next_skb = NULL;
			}
			if (dch->tx_skb) {
				dev_kfree_skb(dch->tx_skb);
				dch->tx_skb = NULL;
			}
			if (dch->rx_skb) {
				dev_kfree_skb(dch->rx_skb);
				dch->rx_skb = NULL;
			}
			test_and_clear_bit(FLG_ACTIVE, &dch->Flags);
			test_and_clear_bit(FLG_TX_NEXT, &dch->Flags);
			test_and_clear_bit(FLG_TX_BUSY, &dch->Flags);
			if (test_and_clear_bit(FLG_BUSY_TIMER, &dch->Flags))
				del_timer(&dch->timer);
#ifdef FIXME
			if (test_and_clear_bit(FLG_L1_BUSY, &dch->Flags))
				dchannel_sched_event(dch, D_CLEARBUSY);
#endif
		} else if ((hh->dinfo & HW_TESTLOOP) == HW_TESTLOOP) {
			u_char	val = 0;

			if (1 & hh->dinfo)
				val |= 0x0c;
			if (2 & hh->dinfo)
				val |= 0x3;
			/* !!! not implemented yet */
		} else {
			if (dch->debug & L1_DEB_WARN)
				mISDN_debugprint(&dch->inst, "w6692_dmsg unknown ctrl %x",
					hh->dinfo);
			ret = -EINVAL;
		}
		spin_unlock_irqrestore(dch->inst.hwlock, flags);
	} else
		ret = -EAGAIN;
	if (!ret)
		dev_kfree_skb(skb);
	return(ret);
}

static int
w6692_l2l1(mISDNinstance_t *inst, struct sk_buff *skb)
{
	channel_t	*chan = container_of(inst, channel_t, inst);
	int		ret = 0;
	mISDN_head_t	*hh = mISDN_HEAD_P(skb);
	u_long		flags;

	if ((hh->prim == PH_DATA_REQ) || (hh->prim == DL_DATA_REQ)) {
		spin_lock_irqsave(inst->hwlock, flags);
		ret = channel_senddata(chan, hh->dinfo, skb);
		if (ret > 0) { /* direct TX */
			if (test_bit(FLG_BCHANNEL, &chan->Flags))
				W6692_fill_Dfifo(chan->hw);
			else if (test_bit(FLG_DCHANNEL, &chan->Flags))
				W6692_fill_Bfifo(chan);
			else
				int_error();
			ret = 0;
		}
		spin_unlock_irqrestore(inst->hwlock, flags);
		return(ret);
	} 
	if (test_bit(FLG_DCHANNEL, &chan->Flags)) {
		ret = w6692_dmsg(chan, skb);
		if (ret != -EAGAIN)
			return(ret);
		ret = -EINVAL;
	}
	if (test_bit(FLG_BCHANNEL, &chan->Flags)) {
		ret = w6692_bmsg(chan, skb);
		if (ret != -EAGAIN)
			return(ret);
		ret = -EINVAL;
	}
	if (!ret)
		dev_kfree_skb(skb);
	return(ret);
}

int __init 
setup_w6692(w6692pci *card)
{
	u_int	val;

	if (!request_region(card->addr, 256, "w6692")) {
		printk(KERN_WARNING
		       "mISDN: %s config port %x-%x already in use\n",
		       "w6692",
		       card->addr,
		       card->addr + 255);
		return(-EIO);
	}
	W6692Version(card, "W6692:");
	val = ReadW6692(card, W_ISTA);
	if (debug)
		printk(KERN_DEBUG "W6692 ISTA=0x%X\n", val);
	val = ReadW6692(card, W_IMASK);
	if (debug)
		printk(KERN_DEBUG "W6692 IMASK=0x%X\n", val);
	val = ReadW6692(card, W_D_EXIR);
	if (debug)
		printk(KERN_DEBUG "W6692 D_EXIR=0x%X\n", val);
	val = ReadW6692(card, W_D_EXIM);
	if (debug)
		printk(KERN_DEBUG "W6692 D_EXIM=0x%X\n", val);
	val = ReadW6692(card, W_D_RSTA);
	if (debug)
		printk(KERN_DEBUG "W6692 D_RSTA=0x%X\n", val);
	return (0);
}

static void
release_card(w6692pci *card)
{
	u_long	flags;

	spin_lock_irqsave(&card->lock, flags);
	disable_hwirq(card);
	spin_unlock_irqrestore(&card->lock, flags);
	free_irq(card->irq, card);
	spin_lock_irqsave(&card->lock, flags);
	mode_w6692(&card->bch[0], 0, ISDN_PID_NONE);
	mode_w6692(&card->bch[1], 1, ISDN_PID_NONE);
	if (card->led || card->subtype == W6692_USR) {
		card->xdata |= 0x04;	/*  LED OFF */
		WriteW6692(card, W_XDATA, card->xdata);
	}
	release_region(card->addr, 256);
	mISDN_freechannel(&card->bch[1]);
	mISDN_freechannel(&card->bch[0]);
	mISDN_freechannel(&card->dch);
	spin_unlock_irqrestore(&card->lock, flags);
	mISDN_ctrl(&card->dch.inst, MGR_UNREGLAYER | REQUEST, NULL);
	spin_lock_irqsave(&w6692.lock, flags);
	list_del(&card->list);
	spin_unlock_irqrestore(&w6692.lock, flags);
	pci_disable_device(card->pdev);
	pci_set_drvdata(card->pdev, NULL);
	kfree(card);
}

static int
w6692_manager(void *data, u_int prim, void *arg) {
	w6692pci	*card;
	mISDNinstance_t	*inst = data;
	struct sk_buff	*skb;
	int		channel = -1;
	u_long		flags;

	if (debug & 0x10000)
		printk(KERN_DEBUG "%s: data(%p) prim(%x) arg(%p)\n",
			__FUNCTION__, data, prim, arg);
	if (!data) {
		MGR_HASPROTOCOL_HANDLER(prim,arg,&w6692)
		printk(KERN_ERR "%s: no data prim %x arg %p\n",
			__FUNCTION__, prim, arg);
		return(-EINVAL);
	}
	spin_lock_irqsave(&w6692.lock, flags);
	list_for_each_entry(card, &w6692.ilist, list) {
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
	spin_unlock_irqrestore(&w6692.lock, flags);
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
			if (w6692_l2l1(inst, skb))
				dev_kfree_skb(skb);
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
			w6692.refcnt--;
		}
		break;
	    case MGR_SETSTACK | INDICATION:
		if ((channel!=2) && (inst->pid.global == 2)) {
			if ((skb = create_link_skb(PH_ACTIVATE | REQUEST,
				0, 0, NULL, 0))) {
				if (w6692_l2l1(inst, skb))
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
	    case MGR_GLOBALOPT | REQUEST:
		if (arg) {
			/* FIXME: detect cards with HEADSET */
			u_int *gopt = arg;
			*gopt =	GLOBALOPT_INTERNAL_CTRL |
				GLOBALOPT_EXTERNAL_EQUIPMENT |
				GLOBALOPT_HANDSET;
		} else
			return(-EINVAL);
		break;
	    case MGR_SELCHANNEL | REQUEST:
		/* no special procedure */
		return(-EINVAL);
	    PRIM_NOT_HANDLED(MGR_CTRLREADY | INDICATION);
	    default:
		printk(KERN_WARNING "%s: prim %x not handled\n",
			__FUNCTION__, prim);
		return(-EINVAL);
	}
	return(0);
}

static int setup_instance(w6692pci *card)
{
	int		i, err;
	mISDN_pid_t	pid;
	u_long		flags;
	
	spin_lock_irqsave(&w6692.lock, flags);
	list_add_tail(&card->list, &w6692.ilist);
	spin_unlock_irqrestore(&w6692.lock, flags);
	card->dch.debug = debug;
	spin_lock_init(&card->lock);
	card->dch.inst.hwlock = &card->lock;
	card->dch.inst.class_dev.dev = &card->pdev->dev;
	card->dch.inst.pid.layermask = ISDN_LAYER(0);
	card->dch.inst.pid.protocol[0] = ISDN_PID_L0_TE_S0;
	mISDN_init_instance(&card->dch.inst, &w6692, card, w6692_l2l1);
	sprintf(card->dch.inst.name, "W6692_%d", w6692_cnt+1);
	mISDN_set_dchannel_pid(&pid, protocol[w6692_cnt], layermask[w6692_cnt]);
	mISDN_initchannel(&card->dch, MSK_INIT_DCHANNEL, MAX_DFRAME_LEN_L1);
	card->dch.hw = card;
	for (i=0; i<2; i++) {
		card->bch[i].channel = i;
		mISDN_init_instance(&card->bch[i].inst, &w6692, card, w6692_l2l1);
		card->bch[i].inst.pid.layermask = ISDN_LAYER(0);
		card->bch[i].inst.hwlock = &card->lock;
		card->bch[i].inst.class_dev.dev = &card->pdev->dev;
		card->bch[i].debug = debug;
		sprintf(card->bch[i].inst.name, "%s B%d", card->dch.inst.name, i+1);
		mISDN_initchannel(&card->bch[i], MSK_INIT_BCHANNEL, MAX_DATA_MEM);
		card->bch[i].hw = &card->wbc[i];
	}
	if (debug)
		printk(KERN_DEBUG "w6692 card %p dch %p bch1 %p bch2 %p\n",
			card, &card->dch, &card->bch[0], &card->bch[1]);
	err = setup_w6692(card);
	if (err) {
		mISDN_freechannel(&card->dch);
		mISDN_freechannel(&card->bch[1]);
		mISDN_freechannel(&card->bch[0]);
		list_del(&card->list);
		kfree(card);
		return(err);
	}
	card->pots = pots[w6692_cnt];
	card->led = led[w6692_cnt];
	w6692_cnt++;
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
	printk(KERN_INFO "w6692 %d cards installed\n", w6692_cnt);
	return(0);
}

static int __devinit w6692_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int		err = -ENOMEM;
	w6692pci	*card;
	w6692_map_t	*m = (w6692_map_t *)ent->driver_data;	

	if (!(card = kmalloc(sizeof(w6692pci), GFP_ATOMIC))) {
		printk(KERN_ERR "No kmem for w6692 card\n");
		return(err);
	}
	memset(card, 0, sizeof(w6692pci));
	card->pdev = pdev;
	card->subtype = m->subtype;
	err = pci_enable_device(pdev);
	if (err) {
		kfree(card);
		return(err);
	}

	printk(KERN_INFO "mISDN_w6692: found adapter %s at %s\n",
	       m->name, pci_name(pdev));

	card->addr = pci_resource_start(pdev, 1);
	card->irq = pdev->irq;
	pci_set_drvdata(pdev, card);
	err = setup_instance(card);
	return(err);
}

static void __devexit w6692_remove_pci(struct pci_dev *pdev)
{
	w6692pci	*card = pci_get_drvdata(pdev);

	if (card)
		mISDN_ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
	else
		printk(KERN_WARNING "%s: drvdata allready removed\n", __FUNCTION__);
}

static struct pci_device_id w6692_ids[] = {
	{ PCI_VENDOR_ID_DYNALINK, PCI_DEVICE_ID_DYNALINK_IS64PH, PCI_ANY_ID, PCI_ANY_ID,
	  0, 0, (unsigned long) &w6692_map[0] },
	{ PCI_VENDOR_ID_WINBOND2, PCI_DEVICE_ID_WINBOND2_6692, PCI_VENDOR_ID_USR2, PCI_DEVICE_ID_USR2_6692,
	  0, 0, (unsigned long) &w6692_map[2] },
	{ PCI_VENDOR_ID_WINBOND2, PCI_DEVICE_ID_WINBOND2_6692, PCI_ANY_ID, PCI_ANY_ID,
	  0, 0, (unsigned long) &w6692_map[1] },
	{ }
};
MODULE_DEVICE_TABLE(pci, w6692_ids);

static struct pci_driver w6692_driver = {
	name:     "w6692",
	probe:    w6692_probe,
	remove:   __devexit_p(w6692_remove_pci),
	id_table: w6692_ids,
};


static char W6692Name[] = "W6692";

static int __init w6692_init(void)
{
	int	err;

	printk(KERN_INFO "Winbond W6692 PCI driver Rev. %s\n", mISDN_getrev(w6692_rev));

#ifdef MODULE
	w6692.owner = THIS_MODULE;
#endif
	spin_lock_init(&w6692.lock);
	INIT_LIST_HEAD(&w6692.ilist);
	w6692.name = W6692Name;
	w6692.own_ctrl = w6692_manager;
	w6692.DPROTO.protocol[0] = ISDN_PID_L0_TE_S0;
	w6692.BPROTO.protocol[1] = ISDN_PID_L1_B_64TRANS |
				    ISDN_PID_L1_B_64HDLC;
	w6692.BPROTO.protocol[2] = ISDN_PID_L2_B_TRANS;
	if ((err = mISDN_register(&w6692))) {
		printk(KERN_ERR "Can't register Winbond W6692 PCI error(%d)\n", err);
		return(err);
	}
	err = pci_register_driver(&w6692_driver);
	if (err < 0)
		goto out;

#ifdef OLD_PCI_REGISTER_DRIVER
	if (err == 0) {
		err = -ENODEV;
		pci_unregister_driver(&w6692_driver);
		goto out;
	}
#endif

	mISDN_module_register(THIS_MODULE);

	return 0;

 out:
 	mISDN_unregister(&w6692);
 	return err;
}

static void __exit w6692_cleanup(void)
{
	int		err;
	w6692pci	*card, *next;

	mISDN_module_unregister(THIS_MODULE);

	if ((err = mISDN_unregister(&w6692))) {
		printk(KERN_ERR "Can't unregister Winbond W6692 PCI error(%d)\n", err);
	}
	list_for_each_entry_safe(card, next, &w6692.ilist, list) {
		printk(KERN_ERR "Winbond W6692 PCI card struct not empty refs %d\n",
			w6692.refcnt);
		release_card(card);
	}
	pci_unregister_driver(&w6692_driver);
}

module_init(w6692_init);
module_exit(w6692_cleanup);

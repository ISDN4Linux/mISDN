/* $Id: w6692.c,v 1.12 2004/06/17 12:31:12 keil Exp $

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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#include "dchannel.h"
#include "bchannel.h"
#include "layer1.h"
#include "helper.h"
#include "debug.h"
#include "w6692.h"

#include <linux/isdn_compat.h>

#define SPIN_DEBUG
#define LOCK_STATISTIC
#include "hw_lock.h"

extern const char *CardType[];

const char *w6692_rev = "$Revision: 1.12 $";

#define DBUSY_TIMER_VALUE	80

typedef struct _w6692_bc {
	struct timer_list	timer;
	u_char			b_mode;
} w6692_bc;

typedef struct _w6692pci {
	struct list_head	list;
	void			*pdev;
	u_int			irq;
	u_int			irqcnt;
	u_int			addr;
	int			pots;
	int			led;
	mISDN_HWlock_t		lock;
	u_char			imask;
	u_char			pctl;
	u_char			xaddr;
	u_char			xdata;
	w6692_bc		wbc[2];
	dchannel_t		dch;
	bchannel_t		bch[2];
} w6692pci;

#define W_LED1_ON	1
#define W_LED1_S0STATUS	2

static int lock_dev(void *data, int nowait)
{
	register mISDN_HWlock_t	*lock = &((w6692pci *)data)->lock;

	return(lock_HW(lock, nowait));
} 

static void unlock_dev(void *data)
{
	register mISDN_HWlock_t *lock = &((w6692pci *)data)->lock;

	unlock_HW(lock);
}

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
	if (!card->led)
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
W6692_new_ph(dchannel_t *dch)
{
	u_int		prim = PH_SIGNAL | INDICATION;
	u_int		para = 0;
	mISDNif_t	*upif = &dch->inst.up;

	if (dch->debug)
		printk(KERN_DEBUG "%s: event %lx\n", __FUNCTION__, dch->event);
	if (!test_and_clear_bit(D_L1STATECHANGE, &dch->event))
		return;
	switch (dch->ph_state) {
		case W_L1CMD_RST:
			dch->inst.lock(dch->inst.data, 0);
			ph_command(dch->hw, W_L1CMD_DRC);
			dch->inst.unlock(dch->inst.data);
			prim = PH_CONTROL | INDICATION;
			para = HW_RESET;
			while(upif) {
				if_link(upif, prim, para, 0, NULL, 0);
				upif = upif->clone;
			}
			upif = &dch->inst.up;
			/* fall trough */
		case W_L1IND_CD:
			prim = PH_CONTROL | CONFIRM;
			para = HW_DEACTIVATE;
			break;
		case W_L1IND_DRD:
			prim = PH_CONTROL | INDICATION;
			para = HW_DEACTIVATE;
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
			break;
		case W_L1IND_AI10:
			para = INFO4_P10;
			break;
		default:
			return;
	}
	while(upif) {
		if_link(upif, prim, para, 0, NULL, 0);
		upif = upif->clone;
	}
}

static void
W6692_empty_Dfifo(w6692pci *card, int count)
{
	dchannel_t	*dch = &card->dch;
	u_char		*ptr;

	if ((dch->debug & L1_DEB_ISAC) && !(dch->debug & L1_DEB_ISAC_FIFO))
		mISDN_debugprint(&dch->inst, "empty_Dfifo");

	if (!dch->rx_skb) {
		if (!(dch->rx_skb = alloc_stack_skb(MAX_DFRAME_LEN_L1, dch->up_headerlen))) {
			printk(KERN_WARNING "mISDN: D receive out of memory\n");
			WriteW6692(card, W_D_CMDR, W_D_CMDR_RACK);
			return;
		}
	}
	if ((dch->rx_skb->len + count) >= MAX_DFRAME_LEN_L1) {
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
		char *t = dch->dlog;

		t += sprintf(t, "empty_Dfifo cnt %d", count);
		mISDN_QuickHex(t, ptr, count);
		mISDN_debugprint(&dch->inst, dch->dlog);
	}
}

static void
W6692_fill_Dfifo(w6692pci *card)
{
	dchannel_t	*dch = &card->dch;
	int		count;
	u_char		*ptr;
	u_char		cmd = W_D_CMDR_XMS;

	if ((dch->debug & L1_DEB_ISAC) && !(dch->debug & L1_DEB_ISAC_FIFO))
		mISDN_debugprint(&dch->inst, "fill_Dfifo");

	count = dch->tx_len - dch->tx_idx;
	if (count <= 0)
		return;
	if (count > 32) {
		count = 32;
	} else
		cmd |= W_D_CMDR_XME;
	ptr = dch->tx_buf + dch->tx_idx;
	dch->tx_idx += count;
	outsb(card->addr + W_D_XFIFO, ptr, count);
	WriteW6692(card, W_D_CMDR, cmd);
	if (test_and_set_bit(FLG_DBUSY_TIMER, &dch->DFlags)) {
		mISDN_debugprint(&dch->inst, "fill_Dfifo dbusytimer running");
		del_timer(&dch->dbusytimer);
	}
	init_timer(&dch->dbusytimer);
	dch->dbusytimer.expires = jiffies + ((DBUSY_TIMER_VALUE * HZ)/1000);
	add_timer(&dch->dbusytimer);
	if (dch->debug & L1_DEB_ISAC_FIFO) {
		char *t = dch->dlog;

		t += sprintf(t, "fill_Dfifo cnt %d", count);
		mISDN_QuickHex(t, ptr, count);
		mISDN_debugprint(&dch->inst, dch->dlog);
	}
}

static void
d_retransmit(w6692pci *card)
{
	dchannel_t *dch = &card->dch;

	if (test_and_clear_bit(FLG_DBUSY_TIMER, &dch->DFlags))
		del_timer(&dch->dbusytimer);
	if (test_and_clear_bit(FLG_L1_DBUSY, &dch->DFlags))
		dchannel_sched_event(dch, D_CLEARBUSY);
	if (test_bit(FLG_TX_BUSY, &dch->DFlags)) {
		/* Restart frame */
		dch->tx_idx = 0;
		W6692_fill_Dfifo(card);
	} else {
		printk(KERN_WARNING "mISDN: w6692 XDU no TX_BUSY\n");
		mISDN_debugprint(&dch->inst, "XDU no TX_BUSY");
		if (test_and_clear_bit(FLG_TX_NEXT, &dch->DFlags)) {
			if (dch->next_skb) {
				dch->tx_len = dch->next_skb->len;
				memcpy(dch->tx_buf,
					dch->next_skb->data,
					dch->tx_len);
				dch->tx_idx = 0;
				W6692_fill_Dfifo(card);
				dchannel_sched_event(dch, D_XMTBUFREADY);
			} else {
				printk(KERN_WARNING "w6692 xdu irq TX_NEXT without skb\n");
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
		if (card->dch.rx_skb) {
			skb_queue_tail(&card->dch.rqueue, card->dch.rx_skb);
		}
	}
	card->dch.rx_skb = NULL;
	dchannel_sched_event(&card->dch, D_RCVBUFREADY);
}

static void
handle_txD(w6692pci *card) {
	register dchannel_t	*dch = &card->dch;

	if (test_and_clear_bit(FLG_DBUSY_TIMER, &dch->DFlags))
		del_timer(&dch->dbusytimer);
	if (test_and_clear_bit(FLG_L1_DBUSY, &dch->DFlags))
		dchannel_sched_event(dch, D_CLEARBUSY);
	if (dch->tx_idx < dch->tx_len) {
		W6692_fill_Dfifo(card);
	} else {
		if (test_and_clear_bit(FLG_TX_NEXT, &dch->DFlags)) {
			if (dch->next_skb) {
				dch->tx_len = dch->next_skb->len;
				memcpy(dch->tx_buf,
					dch->next_skb->data, dch->tx_len);
				dch->tx_idx = 0;
				W6692_fill_Dfifo(card);
				dchannel_sched_event(dch, D_XMTBUFREADY);
			} else {
				printk(KERN_WARNING "w6692 txD irq TX_NEXT without skb\n");
				test_and_clear_bit(FLG_TX_BUSY, &dch->DFlags);
			}
		} else
			test_and_clear_bit(FLG_TX_BUSY, &dch->DFlags);
	}
}

static void
handle_statusD(w6692pci *card) {
	register dchannel_t	*dch = &card->dch;
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
					dch->ph_state, v1);
			dch->ph_state = v1;
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
			dchannel_sched_event(dch, D_L1STATECHANGE);
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
W6692_empty_Bfifo(bchannel_t *bch, int count)
{
	u_char		*ptr;
	w6692pci	*card = bch->inst.data;

	if ((bch->debug & L1_DEB_HSCX) && !(bch->debug & L1_DEB_HSCX_FIFO))
		mISDN_debugprint(&bch->inst, "empty_Bfifo %d", count);
	if (bch->protocol == ISDN_PID_NONE) {
		if (bch->debug & L1_DEB_WARN)
			mISDN_debugprint(&bch->inst, "empty_Bfifo ISDN_PID_NONE");
		WriteW6692B(card, bch->channel, W_B_CMDR, W_B_CMDR_RACK | W_B_CMDR_RACT);
		bch->rx_idx = 0;
		return;
	}
	if (bch->rx_idx + count > MAX_DATA_MEM) {
		if (bch->debug & L1_DEB_WARN)
			mISDN_debugprint(&bch->inst, "empty_Bfifo incoming packet too large");
		WriteW6692B(card, bch->channel, W_B_CMDR, W_B_CMDR_RACK | W_B_CMDR_RACT);
		bch->rx_idx = 0;
		return;
	}
	ptr = bch->rx_buf + bch->rx_idx;
	bch->rx_idx += count;
	insb(card->addr + W_B_RFIFO + (bch->channel ? 0x40 : 0), ptr, count);
	WriteW6692B(card, bch->channel, W_B_CMDR, W_B_CMDR_RACK | W_B_CMDR_RACT);

	if (bch->debug & L1_DEB_HSCX_FIFO) {
		char *t = bch->blog;

		t += sprintf(t, "empty_Bfifo B%d cnt %d", bch->channel, count);
		mISDN_QuickHex(t, ptr, count);
		mISDN_debugprint(&bch->inst, bch->blog);
	}
}

static void
W6692_fill_Bfifo(bchannel_t *bch)
{
	w6692pci	*card = bch->inst.data;
	int		count;
	u_char		*ptr, cmd = W_B_CMDR_RACT | W_B_CMDR_XMS;

	if ((bch->debug & L1_DEB_HSCX) && !(bch->debug & L1_DEB_HSCX_FIFO))
		mISDN_debugprint(&bch->inst, "%s", __FUNCTION__);
	count = bch->tx_len - bch->tx_idx;
	if (count <= 0)
		return;
	ptr = bch->tx_buf + bch->tx_idx;
	if (count > W_B_FIFO_THRESH) {
		count = W_B_FIFO_THRESH;
	} else {
		if (bch->protocol != ISDN_PID_L1_B_64TRANS)
			cmd |= W_B_CMDR_XME;
	}
	if ((bch->debug & L1_DEB_HSCX) && !(bch->debug & L1_DEB_HSCX_FIFO))
		mISDN_debugprint(&bch->inst, "%s: %d/%d", __FUNCTION__,
			count, bch->tx_idx);
	bch->tx_idx += count;
	outsb(card->addr + W_B_XFIFO + (bch->channel ? 0x40 : 0), ptr, count);
	WriteW6692B(card, bch->channel, W_B_CMDR, cmd);
	if (bch->debug & L1_DEB_HSCX_FIFO) {
		char *t = bch->blog;

		t += sprintf(t, "fill_Bfifo  B%d cnt %d",
			     bch->channel, count);
		mISDN_QuickHex(t, ptr, count);
		mISDN_debugprint(&bch->inst, bch->blog);
	}
}

static int
setvolume(bchannel_t *bch, int mic, struct sk_buff *skb)
{
	w6692pci	*card = bch->inst.data;
	u16		*vol = (u16 *)skb->data;
	u_char		val;

	if ((card->pots == 0) || (bch->protocol != ISDN_PID_L1_B_64TRANS))
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
enable_pots(bchannel_t *bch)
{
	w6692_bc	*bhw  = bch->hw;
	w6692pci	*card = bch->inst.data;

	if ((card->pots == 0) || (bch->protocol != ISDN_PID_L1_B_64TRANS))
		return(-ENODEV);
	
	bhw->b_mode |= W_B_MODE_EPCM | W_B_MODE_BSW0;
	WriteW6692B(card, bch->channel, W_B_MODE, bhw->b_mode);
	WriteW6692B(card, bch->channel, W_B_CMDR, W_B_CMDR_RRST | W_B_CMDR_XRST);
	card->pctl |= (bch->channel ? W_PCTL_PCX : 0);
	WriteW6692(card, W_PCTL, card->pctl);
	return(0);
}

static int
disable_pots(bchannel_t *bch)
{
	w6692_bc	*bhw  = bch->hw;
	w6692pci	*card = bch->inst.data;

	if (card->pots == 0)
		return(-ENODEV);
	bhw->b_mode &= ~(W_B_MODE_EPCM | W_B_MODE_BSW0);
	WriteW6692B(card, bch->channel, W_B_MODE, bhw->b_mode);
	WriteW6692B(card, bch->channel, W_B_CMDR, W_B_CMDR_RRST | W_B_CMDR_RACT | W_B_CMDR_XRST);
	return(0);
}

static int
mode_w6692(bchannel_t *bch, int bc, int protocol)
{
	w6692pci	*card = bch->inst.data;
	w6692_bc	*bhw  = bch->hw;
	

	if (bch->debug & L1_DEB_HSCX)
		mISDN_debugprint(&bch->inst, "B%d protocol %x-->%x ch %d-->%d",
			bch->channel, bch->protocol, protocol, bch->channel, bc);
	switch (protocol) {
		case (-1): /* used for init */
			bch->protocol = -1;
			bch->channel = bc;
		case (ISDN_PID_NONE):
			if (bch->protocol == ISDN_PID_NONE)
				break;
			bch->protocol = ISDN_PID_NONE;
			if (card->pots && (bhw->b_mode & W_B_MODE_EPCM))
				disable_pots(bch);
			bhw->b_mode = 0;
			bch->tx_len = 0;
			bch->tx_idx = 0;
			bch->rx_idx = 0;
			if (bch->next_skb) {
				dev_kfree_skb(bch->next_skb);
				bch->next_skb = NULL;
			}
			discard_queue(&bch->rqueue);
			WriteW6692B(card, bch->channel, W_B_MODE, bhw->b_mode);
			WriteW6692B(card, bch->channel, W_B_CMDR, W_B_CMDR_RRST | W_B_CMDR_XRST);
			break;
		case (ISDN_PID_L1_B_64TRANS):
			bch->protocol = protocol;
			bhw->b_mode = W_B_MODE_MMS;
			WriteW6692B(card, bch->channel, W_B_MODE, bhw->b_mode);
			WriteW6692B(card, bch->channel, W_B_EXIM, 0);
			WriteW6692B(card, bch->channel, W_B_CMDR, W_B_CMDR_RRST | W_B_CMDR_RACT | W_B_CMDR_XRST);
			bch_sched_event(bch, B_XMTBUFREADY);
			break;
		case (ISDN_PID_L1_B_64HDLC):
			bch->protocol = protocol;
			bhw->b_mode = W_B_MODE_ITF;
			WriteW6692B(card, bch->channel, W_B_MODE, bhw->b_mode);
			WriteW6692B(card, bch->channel, W_B_ADM1, 0xff);
			WriteW6692B(card, bch->channel, W_B_ADM2, 0xff);
			WriteW6692B(card, bch->channel, W_B_EXIM, 0);
			WriteW6692B(card, bch->channel, W_B_CMDR, W_B_CMDR_RRST | W_B_CMDR_RACT | W_B_CMDR_XRST);
			bch_sched_event(bch, B_XMTBUFREADY);
			break;
		default:
			mISDN_debugprint(&bch->inst, "prot not known %x", protocol);
			return(-ENOPROTOOPT);
	}
	return(0);
}

static void
send_next(bchannel_t *bch)
{
	if (bch->protocol == ISDN_PID_NONE)
		return;
	if (bch->tx_idx < bch->tx_len)
		W6692_fill_Bfifo(bch);
	else {
		bch->tx_idx = 0;
		if (test_and_clear_bit(BC_FLG_TX_NEXT, &bch->Flag)) {
			if (bch->next_skb) {
				bch->tx_len = bch->next_skb->len;
				memcpy(bch->tx_buf, bch->next_skb->data, bch->tx_len);
				W6692_fill_Bfifo(bch);
			} else {
				bch->tx_len = 0;
				printk(KERN_WARNING "W6692 tx irq TX_NEXT without skb\n");
				test_and_clear_bit(BC_FLG_TX_BUSY, &bch->Flag);
			}
		} else {
			bch->tx_len = 0;
			test_and_clear_bit(BC_FLG_TX_BUSY, &bch->Flag);
		}
		bch_sched_event(bch, B_XMTBUFREADY);
	}
}

static void
W6692B_interrupt(w6692pci *card, int ch)
{
	bchannel_t	*bch = &card->bch[ch];
	int		count;
	u_char		stat, star = 0;
	struct sk_buff	*skb;

	stat = ReadW6692B(card, ch, W_B_EXIR);
	if (bch->debug & L1_DEB_HSCX)
		mISDN_debugprint(&bch->inst, "ch%d stat %#x", bch->channel, stat);

	if (stat & W_B_EXI_RME) {
		star = ReadW6692B(card, ch, W_B_STAR);
		if (star & (W_B_STAR_RDOV | W_B_STAR_CRCE | W_B_STAR_RMB)) {
			if ((star & W_B_STAR_RDOV) && (bch->protocol != ISDN_PID_NONE)) {
				if (bch->debug & L1_DEB_WARN)
					mISDN_debugprint(&bch->inst, "B%d RDOV protocol=%x",
						ch +1, bch->protocol);
#ifdef ERROR_STATISTIC
				bch->err_rdo++;
#endif
			}
			if ((star & W_B_STAR_CRCE) && (bch->protocol == ISDN_PID_L1_B_64HDLC)) {
				if (bch->debug & L1_DEB_WARN)
					mISDN_debugprint(&bch->inst, "B%d CRC error", ch +1);
#ifdef ERROR_STATISTIC
				bch->err_crc++;
#endif
			}
			if ((star & W_B_STAR_RMB) && (bch->protocol == ISDN_PID_L1_B_64HDLC)) {
				if (bch->debug & L1_DEB_WARN)
					mISDN_debugprint(&bch->inst, "B%d message abort", ch +1);
#ifdef ERROR_STATISTIC
				bch->err_inv++;
#endif
			}
			WriteW6692B(card, ch, W_B_CMDR, W_B_CMDR_RACK | W_B_CMDR_RRST | W_B_CMDR_RACT);
		} else {
			count = ReadW6692B(card, ch, W_B_RBCL) & (W_B_FIFO_THRESH - 1);
			if (count == 0)
				count = W_B_FIFO_THRESH;
			W6692_empty_Bfifo(bch, count);
			if (bch->rx_idx > 0) {
				if (bch->debug & L1_DEB_HSCX)
					mISDN_debugprint(&bch->inst, "Bchan Frame %d", bch->rx_idx);
				if (!(skb = alloc_stack_skb(bch->rx_idx, bch->up_headerlen)))
					printk(KERN_WARNING "Bchan receive out of memory\n");
				else {
					memcpy(skb_put(skb, bch->rx_idx), bch->rx_buf, bch->rx_idx);
					skb_queue_tail(&bch->rqueue, skb);
				}
				bch_sched_event(bch, B_RCVBUFREADY);
			}
		}
		bch->rx_idx = 0;
	}
	if (stat & W_B_EXI_RMR) {
		if (!(stat & W_B_EXI_RME))
			star = ReadW6692B(card, ch, W_B_STAR);
		if (star & W_B_STAR_RDOV) {
			if (bch->debug & L1_DEB_WARN)
				mISDN_debugprint(&bch->inst, "B%d RDOV protocol=%x",
					ch +1, bch->protocol);
#ifdef ERROR_STATISTIC
			bch->err_rdo++;
#endif
			WriteW6692B(card, ch, W_B_CMDR, W_B_CMDR_RACK | W_B_CMDR_RRST | W_B_CMDR_RACT);
		} else {
			W6692_empty_Bfifo(bch, W_B_FIFO_THRESH);
			if ((bch->protocol == ISDN_PID_L1_B_64TRANS) && (bch->rx_idx > 0)) {
				/* receive audio data */
				if (bch->debug & L1_DEB_HSCX)
					mISDN_debugprint(&bch->inst, "Bchan Frame %d", bch->rx_idx);
				if (!(skb = alloc_stack_skb(bch->rx_idx, bch->up_headerlen)))
					printk(KERN_WARNING "Bchan receive out of memory\n");
				else {
					memcpy(skb_put(skb, bch->rx_idx), bch->rx_buf, bch->rx_idx);
					skb_queue_tail(&bch->rqueue, skb);
				}
				bch_sched_event(bch, B_RCVBUFREADY);
				bch->rx_idx = 0;
			}
		}
	}
	if (stat & W_B_EXI_RDOV) {
		if (!(star & W_B_STAR_RDOV)) { /* only if it is not handled yet */
			if (bch->debug & L1_DEB_WARN)
				mISDN_debugprint(&bch->inst, "B%d RDOV IRQ protocol=%x",
					ch +1, bch->protocol);
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
					ch +1, bch->protocol);
#ifdef ERROR_STATISTIC
			bch->err_xdu++;
#endif
			WriteW6692B(card, ch, W_B_CMDR, W_B_CMDR_XRST | W_B_CMDR_RACT);
			/* resend */
			if (bch->tx_len) {
				if (bch->protocol != ISDN_PID_L1_B_64TRANS)
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
				ch +1, bch->protocol);
#ifdef ERROR_STATISTIC
		bch->err_xdu++;
#endif
		WriteW6692B(card, ch, W_B_CMDR, W_B_CMDR_XRST | W_B_CMDR_RACT);
		/* resend */
		if (bch->tx_len) {
			if (bch->protocol != ISDN_PID_L1_B_64TRANS)
				bch->tx_idx = 0;
		}
		send_next(bch);
	}
}

static irqreturn_t
w6692_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	w6692pci	*card = dev_id;
	u_long		flags;
	u_char		ista;

	spin_lock_irqsave(&card->lock.lock, flags);
#ifdef SPIN_DEBUG
	card->lock.spin_adr = (void *)0x2001;
#endif
	ista = ReadW6692(card, W_ISTA);
	if ((ista | card->imask) == card->imask) {
		/* possible a shared  IRQ reqest */
#ifdef SPIN_DEBUG
		card->lock.spin_adr = NULL;
#endif
		spin_unlock_irqrestore(&card->lock.lock, flags);
		return IRQ_NONE;
	}
	card->irqcnt++;
	if (test_and_set_bit(STATE_FLAG_BUSY, &card->lock.state)) {
		printk(KERN_ERR "%s: STATE_FLAG_BUSY allready activ, should never happen state:%lx\n",
			__FUNCTION__, card->lock.state);
#ifdef SPIN_DEBUG
		printk(KERN_ERR "%s: previous lock:%p\n",
			__FUNCTION__, card->lock.busy_adr);
#endif
#ifdef LOCK_STATISTIC
		card->lock.irq_fail++;
#endif
	} else {
#ifdef LOCK_STATISTIC
		card->lock.irq_ok++;
#endif
#ifdef SPIN_DEBUG
		card->lock.busy_adr = w6692_interrupt;
#endif
	}

	test_and_set_bit(STATE_FLAG_INIRQ, &card->lock.state);
#ifdef SPIN_DEBUG
	card->lock.spin_adr = NULL;
#endif
	spin_unlock_irqrestore(&card->lock.lock, flags);
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
	spin_lock_irqsave(&card->lock.lock, flags);
#ifdef SPIN_DEBUG
	card->lock.spin_adr = (void *)0x2002;
#endif
	if (!test_and_clear_bit(STATE_FLAG_INIRQ, &card->lock.state)) {
	}
	if (!test_and_clear_bit(STATE_FLAG_BUSY, &card->lock.state)) {
		printk(KERN_ERR "%s: STATE_FLAG_BUSY not locked state(%lx)\n",
			__FUNCTION__, card->lock.state);
	}
#ifdef SPIN_DEBUG
	card->lock.busy_adr = NULL;
	card->lock.spin_adr = NULL;
#endif
	spin_unlock_irqrestore(&card->lock.lock, flags);
	return IRQ_HANDLED;
}

static void
dbusy_timer_handler(dchannel_t *dch)
{
	w6692pci	*card = dch->hw;
	int	rbch, star;

	if (test_bit(FLG_DBUSY_TIMER, &dch->DFlags)) {
		if (dch->inst.lock(dch->inst.data, 1)) {
			dch->dbusytimer.expires = jiffies + 1;
			add_timer(&dch->dbusytimer);
			return;
		}
		rbch = ReadW6692(card, W_D_RBCH);
		star = ReadW6692(card, W_D_STAR);
		if (dch->debug) 
			mISDN_debugprint(&dch->inst, "D-Channel Busy RBCH %02x STAR %02x",
				rbch, star);
		if (star & W_D_STAR_XBZ) {	/* D-Channel Busy */
			test_and_set_bit(FLG_L1_DBUSY, &dch->DFlags);
		} else {
			/* discard frame; reset transceiver */
			test_and_clear_bit(FLG_DBUSY_TIMER, &dch->DFlags);
			if (dch->tx_idx) {
				dch->tx_idx = 0;
			} else {
				printk(KERN_WARNING "mISDN: W6692 D-Channel Busy no tx_idx\n");
				mISDN_debugprint(&dch->inst, "D-Channel Busy no tx_idx");
			}
			/* Transmitter reset */
			WriteW6692(card, W_D_CMDR, W_D_CMDR_XRST);	/* Transmitter reset */
		}
		dch->inst.unlock(dch->inst.data);
	}
}

void initW6692(w6692pci *card)
{
	u_char	val;

	card->dch.hw_bh = W6692_new_ph;
	card->dch.dbusytimer.function = (void *) dbusy_timer_handler;
	card->dch.dbusytimer.data = (u_long) &card->dch;
	init_timer(&card->dch.dbusytimer);
	mode_w6692(&card->bch[0], 0, -1);
	mode_w6692(&card->bch[1], 1, -1);
	WriteW6692(card, W_D_CTL, 0x00);
	WriteW6692(card, W_IMASK, 0xff);
	WriteW6692(card, W_D_SAM, 0xff);
	WriteW6692(card, W_D_TAM, 0xff);
	WriteW6692(card, W_D_MODE, W_D_MODE_RACT);
	card->dch.ph_state = W_L1CMD_RST;
	ph_command(card, W_L1CMD_RST);
	ph_command(card, W_L1CMD_ECK);
	/* Reenable all IRQ */
	card->imask = 0x18;
	WriteW6692(card, W_IMASK, card->imask);
	WriteW6692(card, W_D_EXIM, 0x00);
	WriteW6692B(card, 0, W_B_EXIM, 0);
	WriteW6692B(card, 1, W_B_EXIM, 0);
	/* Reset D-chan receiver and transmitter */
	WriteW6692(card, W_D_CMDR, W_D_CMDR_RRST | W_D_CMDR_XRST);
	/* Reset B-chan receiver and transmitter */
	WriteW6692B(card, 0, W_B_CMDR, W_B_CMDR_RRST | W_B_CMDR_XRST);
	WriteW6692B(card, 1, W_B_CMDR, W_B_CMDR_RRST | W_B_CMDR_XRST);
	/* enable peripheral */
	card->pctl = W_PCTL_OE5 | W_PCTL_OE4 | W_PCTL_OE2 | W_PCTL_OE1 | W_PCTL_OE0;
	if (card->pots) {
		card->xaddr = 0x00; /* all sw off */
		card->xdata = 0x06;  /*  LED OFF / POWER UP / ALAW */
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

static void reset_w6692(w6692pci *card)
{
	WriteW6692(card, W_D_CTL, W_D_CTL_SRST);
	mdelay(10);
	WriteW6692(card, W_D_CTL, 0);
}

static int init_card(w6692pci *card)
{
	int		cnt = 3;

	lock_dev(card, 0);
	if (request_irq(card->irq, w6692_interrupt, SA_SHIRQ,
		"w6692", card)) {
		printk(KERN_WARNING "mISDN: couldn't get interrupt %d\n",
			card->irq);
		unlock_dev(card);
		return(-EIO);
	}
	while (cnt) {
		initW6692(card);
		/* RESET Receiver and Transmitter */
		unlock_dev(card);
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
				reset_w6692(card);
				cnt--;
			}
		} else {
			return(0);
		}
		lock_dev(card, 0);
	}
	unlock_dev(card);
	return(-EIO);
}

#define MAX_CARDS	4
#define MODULE_PARM_T	"1-4i"
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
MODULE_PARM(debug, "1i");
MODULE_PARM(led, MODULE_PARM_T);
MODULE_PARM(pots, MODULE_PARM_T);
MODULE_PARM(protocol, MODULE_PARM_T);
MODULE_PARM(layermask, MODULE_PARM_T);
#endif

/******************************/
/* Layer2 -> Layer 1 Transfer */
/******************************/
static int
w6692_l2l1B(mISDNif_t *hif, struct sk_buff *skb)
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
			printk(KERN_WARNING "%s: next_skb exist ERROR\n",
				__FUNCTION__);
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
			W6692_fill_Bfifo(bch);
			bch->inst.unlock(bch->inst.data);
			if ((bch->inst.pid.protocol[2] == ISDN_PID_L2_B_RAWDEV)
				&& bch->dev)
				hif = &bch->dev->rport.pif;
			else
				hif = &bch->inst.up;
			skb_trim(skb, 0);
			return(if_newhead(hif, hh->prim | CONFIRM,
				hh->dinfo, skb));
		}
	} else if ((hh->prim == (PH_ACTIVATE | REQUEST)) ||
		(hh->prim == (DL_ESTABLISH  | REQUEST))) {
		if (test_and_set_bit(BC_FLG_ACTIV, &bch->Flag))
			ret = 0;
		else {
			bch->inst.lock(bch->inst.data, 0);
			ret = mode_w6692(bch, bch->channel, bch->inst.pid.protocol[1]);
			bch->inst.unlock(bch->inst.data);
		}
		if (bch->inst.pid.protocol[2] == ISDN_PID_L2_B_RAWDEV)
			if (bch->dev)
				if_link(&bch->dev->rport.pif,
					hh->prim | CONFIRM, 0, 0, NULL, 0);
		skb_trim(skb, 0);
		return(if_newhead(&bch->inst.up, hh->prim | CONFIRM, ret, skb));
	} else if ((hh->prim == (PH_DEACTIVATE | REQUEST)) ||
		(hh->prim == (DL_RELEASE | REQUEST)) ||
		(hh->prim == (MGR_DISCONNECT | REQUEST))) {
		bch->inst.lock(bch->inst.data, 0);
		if (test_and_clear_bit(BC_FLG_TX_NEXT, &bch->Flag)) {
			dev_kfree_skb(bch->next_skb);
			bch->next_skb = NULL;
		}
		test_and_clear_bit(BC_FLG_TX_BUSY, &bch->Flag);
		mode_w6692(bch, bch->channel, ISDN_PID_NONE);
		test_and_clear_bit(BC_FLG_ACTIV, &bch->Flag);
		bch->inst.unlock(bch->inst.data);
		skb_trim(skb, 0);
		if (hh->prim != (MGR_DISCONNECT | REQUEST)) {
			if (bch->inst.pid.protocol[2] == ISDN_PID_L2_B_RAWDEV)
				if (bch->dev)
					if_link(&bch->dev->rport.pif,
						hh->prim | CONFIRM, 0, 0, NULL, 0);
			if (!if_newhead(&bch->inst.up, hh->prim | CONFIRM, 0, skb))
				return(0);
		}
		ret = 0;
	} else if (hh->prim == (PH_CONTROL | REQUEST)) {
		ret = 0;
		bch->inst.lock(bch->inst.data, 0);
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
		bch->inst.unlock(bch->inst.data);
	} else {
		printk(KERN_WARNING "%s: unknown prim(%x)\n",
			__FUNCTION__, hh->prim);
	}
	if (!ret)
		dev_kfree_skb(skb);
	return(ret);
}

static int
w6692_l1hwD(mISDNif_t *hif, struct sk_buff *skb)
{
	dchannel_t	*dch;
	int		ret = -EINVAL;
	mISDN_head_t	*hh;

	if (!hif || !skb)
		return(ret);
	hh = mISDN_HEAD_P(skb);
	dch = hif->fdata;
	ret = 0;
	if (hh->prim == PH_DATA_REQ) {
		if (dch->next_skb) {
			mISDN_debugprint(&dch->inst, "w6692 l2l1 next_skb exist this shouldn't happen");
			return(-EBUSY);
		}
		dch->inst.lock(dch->inst.data,0);
		if (test_and_set_bit(FLG_TX_BUSY, &dch->DFlags)) {
			test_and_set_bit(FLG_TX_NEXT, &dch->DFlags);
			dch->next_skb = skb;
			dch->inst.unlock(dch->inst.data);
			return(0);
		} else {
			dch->tx_len = skb->len;
			memcpy(dch->tx_buf, skb->data, dch->tx_len);
			dch->tx_idx = 0;
			W6692_fill_Dfifo(dch->hw);
			dch->inst.unlock(dch->inst.data);
			return(if_newhead(&dch->inst.up, PH_DATA_CNF,
				hh->dinfo, skb));
		}
	} else if (hh->prim == (PH_SIGNAL | REQUEST)) {
		dch->inst.lock(dch->inst.data,0);
		if (hh->dinfo == INFO3_P8)
			ph_command(dch->hw, W_L1CMD_AR8);
		else if (hh->dinfo == INFO3_P10)
			ph_command(dch->hw, W_L1CMD_AR10);
		else
			ret = -EINVAL;
		dch->inst.unlock(dch->inst.data);
	} else if (hh->prim == (PH_CONTROL | REQUEST)) {
		dch->inst.lock(dch->inst.data,0);
		if (hh->dinfo == HW_RESET) {
			if (dch->ph_state != W_L1IND_DRD)
				ph_command(dch->hw, W_L1CMD_RST);
			ph_command(dch->hw, W_L1CMD_ECK);
		} else if (hh->dinfo == HW_POWERUP) {
			ph_command(dch->hw, W_L1CMD_ECK);
		} else if (hh->dinfo == HW_DEACTIVATE) {
			discard_queue(&dch->rqueue);
			if (dch->next_skb) {
				dev_kfree_skb(dch->next_skb);
				dch->next_skb = NULL;
			}
			test_and_clear_bit(FLG_TX_NEXT, &dch->DFlags);
			test_and_clear_bit(FLG_TX_BUSY, &dch->DFlags);
			if (test_and_clear_bit(FLG_DBUSY_TIMER, &dch->DFlags))
				del_timer(&dch->dbusytimer);
			if (test_and_clear_bit(FLG_L1_DBUSY, &dch->DFlags))
				dchannel_sched_event(dch, D_CLEARBUSY);
		} else if ((hh->dinfo & HW_TESTLOOP) == HW_TESTLOOP) {
			u_char	val = 0;

			if (1 & hh->dinfo)
				val |= 0x0c;
			if (2 & hh->dinfo)
				val |= 0x3;
			/* !!! not implemented yet */
		} else {
			if (dch->debug & L1_DEB_WARN)
				mISDN_debugprint(&dch->inst, "w6692_l1hw unknown ctrl %x",
					hh->dinfo);
			ret = -EINVAL;
		}
		dch->inst.unlock(dch->inst.data);
	} else {
		if (dch->debug & L1_DEB_WARN)
			mISDN_debugprint(&dch->inst, "w6692_l1hw unknown prim %x",
				hh->prim);
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
#ifdef LOCK_STATISTIC
	printk(KERN_INFO "try_ok(%d) try_wait(%d) try_mult(%d) try_inirq(%d)\n",
		card->lock.try_ok, card->lock.try_wait, card->lock.try_mult, card->lock.try_inirq);
	printk(KERN_INFO "irq_ok(%d) irq_fail(%d)\n",
		card->lock.irq_ok, card->lock.irq_fail);
#endif
	lock_dev(card, 0);
	/* disable all IRQ */
	WriteW6692(card, W_IMASK, 0xff);
	free_irq(card->irq, card);
	mode_w6692(&card->bch[0], 0, ISDN_PID_NONE);
	mode_w6692(&card->bch[1], 1, ISDN_PID_NONE);
	if (card->led) {
		card->xdata |= 0x04;	/*  LED OFF */
		WriteW6692(card, W_XDATA, card->xdata);
	}
	release_region(card->addr, 256);
	mISDN_free_bch(&card->bch[1]);
	mISDN_free_bch(&card->bch[0]);
	mISDN_free_dch(&card->dch);
	w6692.ctrl(card->dch.inst.up.peer, MGR_DISCONNECT | REQUEST, &card->dch.inst.up);
	w6692.ctrl(&card->dch.inst, MGR_UNREGLAYER | REQUEST, NULL);
	list_del(&card->list);
	unlock_dev(card);
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

	if (debug & 0x10000)
		printk(KERN_DEBUG "%s: data(%p) prim(%x) arg(%p)\n",
			__FUNCTION__, data, prim, arg);
	if (!data) {
		MGR_HASPROTOCOL_HANDLER(prim,arg,&w6692)
		printk(KERN_ERR "%s: no data prim %x arg %p\n",
			__FUNCTION__, prim, arg);
		return(-EINVAL);
	}
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
	if (channel<0) {
		printk(KERN_WARNING "%s: no channel data %p prim %x arg %p\n",
			__FUNCTION__, data, prim, arg);
		return(-EINVAL);
	}

	switch(prim) {
	    case MGR_REGLAYER | CONFIRM:
		if (channel == 2)
			dch_set_para(&card->dch, &inst->st->para);
		else
			bch_set_para(&card->bch[channel], &inst->st->para);
		break;
	    case MGR_UNREGLAYER | REQUEST:
		if (channel == 2) {
			inst->down.fdata = &card->dch;
			if ((skb = create_link_skb(PH_CONTROL | REQUEST,
				HW_DEACTIVATE, 0, NULL, 0))) {
				if (w6692_l1hwD(&inst->down, skb))
					dev_kfree_skb(skb);
			}
		} else {
			inst->down.fdata = &card->bch[channel];
			if ((skb = create_link_skb(MGR_DISCONNECT | REQUEST,
				0, 0, NULL, 0))) {
				if (w6692_l2l1B(&inst->down, skb))
					dev_kfree_skb(skb);
			}
		}
		w6692.ctrl(inst->up.peer, MGR_DISCONNECT | REQUEST, &inst->up);
		w6692.ctrl(inst, MGR_UNREGLAYER | REQUEST, NULL);
		break;
	    case MGR_CLRSTPARA | INDICATION:
		arg = NULL;
	    case MGR_ADDSTPARA | INDICATION:
		if (channel == 2)
			dch_set_para(&card->dch, arg);
		else
			bch_set_para(&card->bch[channel], arg);
		break;
	    case MGR_RELEASE | INDICATION:
		if (channel == 2) {
			release_card(card);
		} else {
			w6692.refcnt--;
		}
		break;
	    case MGR_CONNECT | REQUEST:
		return(mISDN_ConnectIF(inst, arg));
	    case MGR_SETIF | REQUEST:
	    case MGR_SETIF | INDICATION:
		if (channel==2)
			return(mISDN_SetIF(inst, arg, prim, w6692_l1hwD, NULL,
				&card->dch));
		else
			return(mISDN_SetIF(inst, arg, prim, w6692_l2l1B, NULL,
				&card->bch[channel]));
		break;
	    case MGR_DISCONNECT | REQUEST:
	    case MGR_DISCONNECT | INDICATION:
		return(mISDN_DisConnectIF(inst, arg));
	    case MGR_SETSTACK | CONFIRM:
		if ((channel!=2) && (inst->pid.global == 2)) {
			inst->down.fdata = &card->bch[channel];
			if ((skb = create_link_skb(PH_ACTIVATE | REQUEST,
				0, 0, NULL, 0))) {
				if (w6692_l2l1B(&inst->down, skb))
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

static int __devinit setup_instance(w6692pci *card)
{
	int		i, err;
	mISDN_pid_t	pid;
	
	list_add_tail(&card->list, &w6692.ilist);
	card->dch.debug = debug;
	lock_HW_init(&card->lock);
	card->dch.inst.lock = lock_dev;
	card->dch.inst.unlock = unlock_dev;
	card->dch.inst.pid.layermask = ISDN_LAYER(0);
	card->dch.inst.pid.protocol[0] = ISDN_PID_L0_TE_S0;
	mISDN_init_instance(&card->dch.inst, &w6692, card);
	sprintf(card->dch.inst.name, "W6692_%d", w6692_cnt+1);
	mISDN_set_dchannel_pid(&pid, protocol[w6692_cnt], layermask[w6692_cnt]);
	mISDN_init_dch(&card->dch);
	card->dch.hw = card;
	for (i=0; i<2; i++) {
		card->bch[i].channel = i;
		mISDN_init_instance(&card->bch[i].inst, &w6692, card);
		card->bch[i].inst.pid.layermask = ISDN_LAYER(0);
		card->bch[i].inst.lock = lock_dev;
		card->bch[i].inst.unlock = unlock_dev;
		card->bch[i].debug = debug;
		sprintf(card->bch[i].inst.name, "%s B%d", card->dch.inst.name, i+1);
		mISDN_init_bch(&card->bch[i]);
		card->bch[i].hw = &card->wbc[i];
	}
	if (debug)
		printk(KERN_DEBUG "w6692 card %p dch %p bch1 %p bch2 %p\n",
			card, &card->dch, &card->bch[0], &card->bch[1]);
	err = setup_w6692(card);
	if (err) {
		mISDN_free_dch(&card->dch);
		mISDN_free_bch(&card->bch[1]);
		mISDN_free_bch(&card->bch[0]);
		list_del(&card->list);
		kfree(card);
		return(err);
	}
	card->pots = pots[w6692_cnt];
	card->led = led[w6692_cnt];
	w6692_cnt++;
	err = w6692.ctrl(NULL, MGR_NEWSTACK | REQUEST, &card->dch.inst);
	if (err) {
		release_card(card);
		return(err);
	}
	for (i=0; i<2; i++) {
		err = w6692.ctrl(card->dch.inst.st, MGR_NEWSTACK | REQUEST, &card->bch[i].inst);
		if (err) {
			printk(KERN_ERR "MGR_ADDSTACK bchan error %d\n", err);
			w6692.ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
			return(err);
		}
	}
	err = w6692.ctrl(card->dch.inst.st, MGR_SETSTACK | REQUEST, &pid);
	if (err) {
		printk(KERN_ERR  "MGR_SETSTACK REQUEST dch err(%d)\n", err);
		w6692.ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
		return(err);
	}
	err = init_card(card);
	if (err) {
		w6692.ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
		return(err);
	}
	w6692.ctrl(card->dch.inst.st, MGR_CTRLREADY | INDICATION, NULL);
	printk(KERN_INFO "w6692 %d cards installed\n", w6692_cnt);
	return(0);
}

static int __devinit w6692_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int		err = -ENOMEM;
	w6692pci	*card;

	if (!(card = kmalloc(sizeof(w6692pci), GFP_ATOMIC))) {
		printk(KERN_ERR "No kmem for w6692 card\n");
		return(err);
	}
	memset(card, 0, sizeof(w6692pci));
	card->pdev = pdev;
	err = pci_enable_device(pdev);
	if (err) {
		kfree(card);
		return(err);
	}

	printk(KERN_INFO "mISDN_w6692: found adapter %s at %s\n",
	       (char *) ent->driver_data, pdev->slot_name);

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
		w6692.ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
	else
		printk(KERN_WARNING "%s: drvdata allready removed\n", __FUNCTION__);
}

/* table entry in the PCI devices list */
typedef struct {
	int vendor_id;
	int device_id;
	char *vendor_name;
	char *card_name;
} PCI_ENTRY;

static const PCI_ENTRY id_list[] =
{
	{PCI_VENDOR_ID_DYNALINK, PCI_DEVICE_ID_DYNALINK_IS64PH, "Dynalink/AsusCom", "IS64PH"},
	{PCI_VENDOR_ID_WINBOND2, PCI_DEVICE_ID_WINBOND2_6692, "Winbond", "W6692"},
	{0, 0, NULL, NULL}
};

static struct pci_device_id w6692_ids[] __devinitdata = {
	{ PCI_VENDOR_ID_DYNALINK, PCI_DEVICE_ID_DYNALINK_IS64PH, PCI_ANY_ID, PCI_ANY_ID,
	  0, 0, (unsigned long) "Dynalink/AsusCom IS64PH" },
	{ PCI_VENDOR_ID_WINBOND2, PCI_DEVICE_ID_WINBOND2_6692, PCI_ANY_ID, PCI_ANY_ID,
	  0, 0, (unsigned long) "Winbond W6692" },
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

	if (err == 0) {
		err = -ENODEV;
		pci_unregister_driver(&w6692_driver);
		goto out;
	}
	return 0;

 out:
 	mISDN_unregister(&w6692);
 	return err;
}

static void __exit w6692_cleanup(void)
{
	int		err;
	w6692pci	*card, *next;

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

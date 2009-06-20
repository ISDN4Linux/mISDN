/*
 * isac.c   ISAC specific routines
 *
 * Author       Karsten Keil <keil@isdn4linux.de>
 *
 * Copyright 2009  by Karsten Keil <keil@isdn4linux.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
#include <linux/mISDNhw.h>
#include "isac.h"


#define DBUSY_TIMER_VALUE	80
#define ARCOFI_USE		1

#define ISAC_REV		"2.0"

MODULE_AUTHOR("Karsten Keil");
MODULE_VERSION(ISAC_REV);
MODULE_LICENSE("GPL v2");

static u32 *debug;

static inline void
ph_command(struct isac_hw *isac, u8 command)
{
	pr_debug("%s: ph_command %x\n", isac->name, command);
	if (isac->type & ISAC_TYPE_ISACSX)
		isac->write_reg(isac->dch.hw, ISACSX_CIX0,
			(command << 4) | 0xE);
	else
		isac->write_reg(isac->dch.hw, ISAC_CIX0,
			(command << 2) | 3);
}

static void
isac_ph_state_change(struct isac_hw *isac)
{
	switch (isac->state) {
	case (ISAC_IND_RS):
	case (ISAC_IND_EI):
		ph_command(isac, ISAC_CMD_DUI);
	}
	schedule_event(&isac->dch, FLG_PHCHANGE);
}

static void
isac_ph_state_bh(struct dchannel *dch)
{
	struct isac_hw *isac = container_of(dch, struct isac_hw, dch);

	switch (isac->state) {
	case ISAC_IND_RS:
	case ISAC_IND_EI:
		dch->state = 0;
		l1_event(dch->l1, HW_RESET_IND);
		break;
	case ISAC_IND_DID:
		dch->state = 3;
		l1_event(dch->l1, HW_DEACT_CNF);
		break;
	case ISAC_IND_DR:
		dch->state = 3;
		l1_event(dch->l1, HW_DEACT_IND);
		break;
	case ISAC_IND_PU:
		dch->state = 4;
		l1_event(dch->l1, HW_POWERUP_IND);
		break;
	case ISAC_IND_RSY:
		if (dch->state <= 5) {
			dch->state = 5;
			l1_event(dch->l1, ANYSIGNAL);
		} else {
			dch->state = 8;
			l1_event(dch->l1, LOSTFRAMING);
		}
		break;
	case ISAC_IND_ARD:
		dch->state = 6;
		l1_event(dch->l1, INFO2);
		break;
	case ISAC_IND_AI8:
		dch->state = 7;
		l1_event(dch->l1, INFO4_P8);
		break;
	case ISAC_IND_AI10:
		dch->state = 7;
		l1_event(dch->l1, INFO4_P10);
		break;
	}
	pr_debug("%s: TE newstate %x\n", isac->name, dch->state);
}

void
isac_empty_fifo(struct isac_hw *isac, int count)
{
	u8 *ptr;

	pr_debug("%s: %s  %d\n", isac->name, __func__, count);

	if (!isac->dch.rx_skb) {
		isac->dch.rx_skb = mI_alloc_skb(isac->dch.maxlen, GFP_ATOMIC);
		if (!isac->dch.rx_skb) {
			pr_info("%s: D receive out of memory\n", isac->name);
			isac->write_reg(isac->dch.hw, ISAC_CMDR, 0x80);
			return;
		}
	}
	if ((isac->dch.rx_skb->len + count) >= isac->dch.maxlen) {
		pr_debug("%s: %s overrun %d\n", isac->name, __func__,
			    isac->dch.rx_skb->len + count);
		isac->write_reg(isac->dch.hw, ISAC_CMDR, 0x80);
		return;
	}
	ptr = skb_put(isac->dch.rx_skb, count);
	isac->read_fifo(isac->dch.hw, ptr, count);
	isac->write_reg(isac->dch.hw, ISAC_CMDR, 0x80);
	if (*debug & DEBUG_HW_DFIFO) {
		char	pfx[MISDN_MAX_IDLEN + 16];

		snprintf(pfx, MISDN_MAX_IDLEN + 15, "D-recv %s %d ",
			isac->name, count);
		print_hex_dump_bytes(pfx, DUMP_PREFIX_OFFSET, ptr, count);
	}
}

static void
isac_fill_fifo(struct isac_hw *isac)
{
	int count, more;
	u8 *ptr;

	if (!isac->dch.tx_skb)
		return;
	count = isac->dch.tx_skb->len - isac->dch.tx_idx;
	if (count <= 0)
		return;

	more = 0;
	if (count > 32) {
		more = !0;
		count = 32;
	}
	pr_debug("%s: %s  %d\n", isac->name, __func__, count);
	ptr = isac->dch.tx_skb->data + isac->dch.tx_idx;
	isac->dch.tx_idx += count;
	isac->write_fifo(isac->dch.hw, ptr, count);
	isac->write_reg(isac->dch.hw, ISAC_CMDR, more ? 0x8 : 0xa);
	if (test_and_set_bit(FLG_BUSY_TIMER, &isac->dch.Flags)) {
		pr_debug("%s: %s dbusytimer running\n", isac->name, __func__);
		del_timer(&isac->dch.timer);
	}
	init_timer(&isac->dch.timer);
	isac->dch.timer.expires = jiffies + ((DBUSY_TIMER_VALUE * HZ)/1000);
	add_timer(&isac->dch.timer);
	if (*debug & DEBUG_HW_DFIFO) {
		char	pfx[MISDN_MAX_IDLEN + 16];

		snprintf(pfx, MISDN_MAX_IDLEN + 15, "D-send %s %d ",
			isac->name, count);
		print_hex_dump_bytes(pfx, DUMP_PREFIX_OFFSET, ptr, count);
	}
}

static void
isac_rme_irq(struct isac_hw *isac)
{
	u8 val, count;

	val = isac->read_reg(isac->dch.hw, ISAC_RSTA);
	if ((val & 0x70) != 0x20) {
		if (val & 0x40) {
			pr_debug("%s: ISAC RDO\n", isac->name);
#ifdef ERROR_STATISTIC
			isac->dch.err_rx++;
#endif
		}
		if (!(val & 0x20)) {
			pr_debug("%s: ISAC CRC error\n", isac->name);
#ifdef ERROR_STATISTIC
			isac->dch.err_crc++;
#endif
		}
		isac->write_reg(isac->dch.hw, ISAC_CMDR, 0x80);
		if (isac->dch.rx_skb)
			dev_kfree_skb(isac->dch.rx_skb);
		isac->dch.rx_skb = NULL;
	} else {
		count = isac->read_reg(isac->dch.hw, ISAC_RBCL) & 0x1f;
		if (count == 0)
			count = 32;
		isac_empty_fifo(isac, count);
		recv_Dchannel(&isac->dch);
	}
}

static void
isac_xpr_irq(struct isac_hw *isac)
{
	if (test_and_clear_bit(FLG_BUSY_TIMER, &isac->dch.Flags))
		del_timer(&isac->dch.timer);
	if (isac->dch.tx_skb && isac->dch.tx_idx < isac->dch.tx_skb->len) {
		isac_fill_fifo(isac);
	} else {
		if (isac->dch.tx_skb)
			dev_kfree_skb(isac->dch.tx_skb);
		if (get_next_dframe(&isac->dch))
			isac_fill_fifo(isac);
	}
}

static void
isac_retransmit(struct isac_hw *isac)
{
	if (test_and_clear_bit(FLG_BUSY_TIMER, &isac->dch.Flags))
		del_timer(&isac->dch.timer);
	if (test_bit(FLG_TX_BUSY, &isac->dch.Flags)) {
		/* Restart frame */
		isac->dch.tx_idx = 0;
		isac_fill_fifo(isac);
	} else if (isac->dch.tx_skb) { /* should not happen */
		pr_info("%s: tx_skb exist but not busy\n", isac->name);
		test_and_set_bit(FLG_TX_BUSY, &isac->dch.Flags);
		isac->dch.tx_idx = 0;
		isac_fill_fifo(isac);
	} else {
		pr_info("%s: ISAC XDU no TX_BUSY\n", isac->name);
		if (get_next_dframe(&isac->dch))
			isac_fill_fifo(isac);
	}
}

static void
isac_mos_irq(struct isac_hw *isac)
{
	u8 val;
	int ret;

	val = isac->read_reg(isac->dch.hw, ISAC_MOSR);
	pr_debug("%s: ISAC MOSR %02x\n", isac->name, val);
#if ARCOFI_USE
	if (val & 0x08) {
		if (!isac->mon_rx) {
			isac->mon_rx = kmalloc(MAX_MON_FRAME, GFP_ATOMIC);
			if (!isac->mon_rx) {
				pr_info("%s: ISAC MON RX out of memory!\n",
					isac->name);
				isac->mocr &= 0xf0;
				isac->mocr |= 0x0a;
				isac->write_reg(isac->dch.hw, ISAC_MOCR,
					isac->mocr);
				goto afterMONR0;
			} else
				isac->mon_rxp = 0;
		}
		if (isac->mon_rxp >= MAX_MON_FRAME) {
			isac->mocr &= 0xf0;
			isac->mocr |= 0x0a;
			isac->write_reg(isac->dch.hw, ISAC_MOCR, isac->mocr);
			isac->mon_rxp = 0;
			pr_debug("%s: ISAC MON RX overflow!\n", isac->name);
			goto afterMONR0;
		}
		isac->mon_rx[isac->mon_rxp++] = isac->read_reg(isac->dch.hw,
						ISAC_MOR0);
		pr_debug("%s: ISAC MOR0 %02x\n", isac->name,
			isac->mon_rx[isac->mon_rxp - 1]);
		if (isac->mon_rxp == 1) {
			isac->mocr |= 0x04;
			isac->write_reg(isac->dch.hw, ISAC_MOCR, isac->mocr);
		}
	}
afterMONR0:
	if (val & 0x80) {
		if (!isac->mon_rx) {
			isac->mon_rx = kmalloc(MAX_MON_FRAME, GFP_ATOMIC);
			if (!isac->mon_rx) {
				pr_info("%s: ISAC MON RX out of memory!\n",
					isac->name);
				isac->mocr &= 0x0f;
				isac->mocr |= 0xa0;
				isac->write_reg(isac->dch.hw, ISAC_MOCR,
					isac->mocr);
				goto afterMONR1;
			} else
				isac->mon_rxp = 0;
		}
		if (isac->mon_rxp >= MAX_MON_FRAME) {
			isac->mocr &= 0x0f;
			isac->mocr |= 0xa0;
			isac->write_reg(isac->dch.hw, ISAC_MOCR, isac->mocr);
			isac->mon_rxp = 0;
			pr_debug("%s: ISAC MON RX overflow!\n", isac->name);
			goto afterMONR1;
		}
		isac->mon_rx[isac->mon_rxp++] = isac->read_reg(isac->dch.hw,
						ISAC_MOR1);
		pr_debug("%s: ISAC MOR1 %02x\n", isac->name,
			isac->mon_rx[isac->mon_rxp - 1]);
		isac->mocr |= 0x40;
		isac->write_reg(isac->dch.hw, ISAC_MOCR, isac->mocr);
	}
afterMONR1:
	if (val & 0x04) {
		isac->mocr &= 0xf0;
		isac->write_reg(isac->dch.hw, ISAC_MOCR, isac->mocr);
		isac->mocr |= 0x0a;
		isac->write_reg(isac->dch.hw, ISAC_MOCR, isac->mocr);
		if (isac->monitor) {
			ret = isac->monitor(isac->dch.hw, MONITOR_RX_0,
				isac->mon_rx, isac->mon_rxp);
			if (ret)
				kfree(isac->mon_rx);
		} else {
			pr_info("%s: MONITOR 0 received %d but no user\n",
				isac->name, isac->mon_rxp);
			kfree(isac->mon_rx);
		}
		isac->mon_rx = NULL;
		isac->mon_rxp = 0;
	}
	if (val & 0x40) {
		isac->mocr &= 0x0f;
		isac->write_reg(isac->dch.hw, ISAC_MOCR, isac->mocr);
		isac->mocr |= 0xa0;
		isac->write_reg(isac->dch.hw, ISAC_MOCR, isac->mocr);
		if (isac->monitor) {
			ret = isac->monitor(isac->dch.hw, MONITOR_RX_1,
				isac->mon_rx, isac->mon_rxp);
			if (ret)
				kfree(isac->mon_rx);
		} else {
			pr_info("%s: MONITOR 1 received %d but no user\n",
				isac->name, isac->mon_rxp);
			kfree(isac->mon_rx);
		}
		isac->mon_rx = NULL;
		isac->mon_rxp = 0;
	}
	if (val & 0x02) {
		if ((!isac->mon_tx) || (isac->mon_txc &&
			(isac->mon_txp >= isac->mon_txc) && !(val & 0x08))) {
			isac->mocr &= 0xf0;
			isac->write_reg(isac->dch.hw, ISAC_MOCR, isac->mocr);
			isac->mocr |= 0x0a;
			isac->write_reg(isac->dch.hw, ISAC_MOCR, isac->mocr);
			if (isac->mon_txc && (isac->mon_txp >= isac->mon_txc)) {
				if (isac->monitor)
					ret = isac->monitor(isac->dch.hw,
						MONITOR_TX_0, NULL, 0);
			}
			kfree(isac->mon_tx);
			isac->mon_tx = NULL;
			isac->mon_txc = 0;
			isac->mon_txp = 0;
			goto AfterMOX0;
		}
		if (isac->mon_txc && (isac->mon_txp >= isac->mon_txc)) {
			if (isac->monitor)
				ret = isac->monitor(isac->dch.hw,
					MONITOR_TX_0, NULL, 0);
			kfree(isac->mon_tx);
			isac->mon_tx = NULL;
			isac->mon_txc = 0;
			isac->mon_txp = 0;
			goto AfterMOX0;
		}
		isac->write_reg(isac->dch.hw, ISAC_MOX0,
		isac->mon_tx[isac->mon_txp++]);
		pr_debug("%s: ISAC %02x -> MOX0\n", isac->name,
			isac->mon_tx[isac->mon_txp - 1]);
	}
AfterMOX0:
	if (val & 0x20) {
		if ((!isac->mon_tx) || (isac->mon_txc &&
			(isac->mon_txp >= isac->mon_txc) && !(val & 0x80))) {
			isac->mocr &= 0x0f;
			isac->write_reg(isac->dch.hw, ISAC_MOCR, isac->mocr);
			isac->mocr |= 0xa0;
			isac->write_reg(isac->dch.hw, ISAC_MOCR, isac->mocr);
			if (isac->mon_txc && (isac->mon_txp >= isac->mon_txc)) {
				if (isac->monitor)
					ret = isac->monitor(isac->dch.hw,
						MONITOR_TX_1, NULL, 0);
			}
			kfree(isac->mon_tx);
			isac->mon_tx = NULL;
			isac->mon_txc = 0;
			isac->mon_txp = 0;
			goto AfterMOX1;
		}
		if (isac->mon_txc && (isac->mon_txp >= isac->mon_txc)) {
			if (isac->monitor)
				ret = isac->monitor(isac->dch.hw,
					MONITOR_TX_1, NULL, 0);
			kfree(isac->mon_tx);
			isac->mon_tx = NULL;
			isac->mon_txc = 0;
			isac->mon_txp = 0;
			goto AfterMOX1;
		}
		isac->write_reg(isac->dch.hw, ISAC_MOX1,
		isac->mon_tx[isac->mon_txp++]);
		pr_debug("%s: ISAC %02x -> MOX1\n", isac->name,
			isac->mon_tx[isac->mon_txp - 1]);
	}
AfterMOX1:
	val = 0; /* dummy to avoid warning */
#endif
}

static void
isac_cisq_irq(struct isac_hw *isac) {
	u8 val;

	val = isac->read_reg(isac->dch.hw, ISAC_CIR0);
	pr_debug("%s: ISAC CIR0 %02X\n", isac->name, val);
	if (val & 2) {
		pr_debug("%s: ph_state change %x->%x\n", isac->name,
			isac->state, (val >> 2) & 0xf);
		isac->state = (val >> 2) & 0xf;
		isac_ph_state_change(isac);
	}
	if (val & 1) {
		val = isac->read_reg(isac->dch.hw, ISAC_CIR1);
		pr_debug("%s: ISAC CIR1 %02X\n", isac->name, val);
	}
}

static void
isacsx_cic_irq(struct isac_hw *isac)
{
	u8 val;

	val = isac->read_reg(isac->dch.hw, ISACSX_CIR0);
	pr_debug("%s: ISACSX CIR0 %02X\n", isac->name, val);
	if (val & ISACSX_CIR0_CIC0) {
		pr_debug("%s: ph_state change %x->%x\n", isac->name,
			isac->state, val >> 4);
		isac->state = val >> 4;
		isac_ph_state_change(isac);
	}
}

static void
isacsx_rme_irq(struct isac_hw *isac)
{
	int count;
	u8 val;

	val = isac->read_reg(isac->dch.hw, ISACSX_RSTAD);
	if ((val & (ISACSX_RSTAD_VFR |
		    ISACSX_RSTAD_RDO |
		    ISACSX_RSTAD_CRC |
		    ISACSX_RSTAD_RAB))
	    != (ISACSX_RSTAD_VFR | ISACSX_RSTAD_CRC)) {
		pr_debug("%s: RSTAD %#x, dropped\n", isac->name, val);
#ifdef ERROR_STATISTIC
		if (val & ISACSX_RSTAD_CRC)
			isac->dch.err_rx++;
		else
			isac->dch.err_crc++;
#endif
		isac->write_reg(isac->dch.hw, ISACSX_CMDRD,
		    ISACSX_CMDRD_RMC);
		if (isac->dch.rx_skb)
			dev_kfree_skb(isac->dch.rx_skb);
		isac->dch.rx_skb = NULL;
	} else {
		count = isac->read_reg(isac->dch.hw, ISACSX_RBCLD) & 0x1f;
		if (count == 0)
			count = 32;
		isac_empty_fifo(isac, count);
		if (isac->dch.rx_skb) {
			skb_trim(isac->dch.rx_skb, isac->dch.rx_skb->len - 1);
			pr_debug("%s: dchannel received %d\n", isac->name,
				isac->dch.rx_skb->len);
			recv_Dchannel(&isac->dch);
		}
	}
}

static void
isac_interrupt(struct isac_hw *isac, u8 val)
{
	pr_debug("%s: ISAC interrupt %02x\n", isac->name, val);
	if (isac->type & ISAC_TYPE_ISACSX) {
		if (val & ISACSX_ISTA_CIC)
			isacsx_cic_irq(isac);
		if (val & ISACSX_ISTA_ICD) {
			val = isac->read_reg(isac->dch.hw, ISACSX_ISTAD);
			pr_debug("%s: ISTAD %02x\n", isac->name, val);
			if (val & ISACSX_ISTAD_XDU) {
				pr_debug("%s: ISAC XDU\n", isac->name);
#ifdef ERROR_STATISTIC
				isac->dch.err_tx++;
#endif
				isac_retransmit(isac);
			}
			if (val & ISACSX_ISTAD_XMR) {
				pr_debug("%s: ISAC XMR\n", isac->name);
#ifdef ERROR_STATISTIC
				isac->dch.err_tx++;
#endif
				isac_retransmit(isac);
			}
			if (val & ISACSX_ISTAD_XPR)
				isac_xpr_irq(isac);
			if (val & ISACSX_ISTAD_RFO) {
				pr_debug("%s: ISAC RFO\n", isac->name);
				isac->write_reg(isac->dch.hw, ISACSX_CMDRD,
					ISACSX_CMDRD_RMC);
			}
			if (val & ISACSX_ISTAD_RME)
				isacsx_rme_irq(isac);
			if (val & ISACSX_ISTAD_RPF)
				isac_empty_fifo(isac, 0x20);
		}
	} else {
		if (val & 0x80)	/* RME */
			isac_rme_irq(isac);
		if (val & 0x40)	/* RPF */
			isac_empty_fifo(isac, 32);
		if (val & 0x10)	/* XPR */
			isac_xpr_irq(isac);
		if (val & 0x04)	/* CISQ */
			isac_cisq_irq(isac);
		if (val & 0x20)	/* RSC - never */
			pr_debug("%s: ISAC RSC interrupt\n", isac->name);
		if (val & 0x02)	/* SIN - never */
			pr_debug("%s: ISAC SIN interrupt\n", isac->name);
		if (val & 0x01) {	/* EXI */
			val = isac->read_reg(isac->dch.hw, ISAC_EXIR);
			pr_debug("%s: ISAC EXIR %02x\n", isac->name, val);
			if (val & 0x80)	/* XMR */
				pr_debug("%s: ISAC XMR\n", isac->name);
			if (val & 0x40) { /* XDU */
				pr_debug("%s: ISAC XDU\n", isac->name);
#ifdef ERROR_STATISTIC
				isac->dch.err_tx++;
#endif
				isac_retransmit(isac);
			}
			if (val & 0x04)	/* MOS */
				isac_mos_irq(isac);
		}
	}
}

static int
isac_l1hw(struct mISDNchannel *ch, struct sk_buff *skb)
{
	struct mISDNdevice	*dev = container_of(ch, struct mISDNdevice, D);
	struct dchannel		*dch = container_of(dev, struct dchannel, dev);
	struct isac_hw		*isac = container_of(dch, struct isac_hw, dch);
	int			ret = -EINVAL;
	struct mISDNhead	*hh = mISDN_HEAD_P(skb);
	u32			id;
	u_long			flags;

	switch (hh->prim) {
	case PH_DATA_REQ:
		spin_lock_irqsave(isac->hwlock, flags);
		ret = dchannel_senddata(dch, skb);
		if (ret > 0) { /* direct TX */
			id = hh->id; /* skb can be freed */
			isac_fill_fifo(isac);
			ret = 0;
			spin_unlock_irqrestore(isac->hwlock, flags);
			queue_ch_frame(ch, PH_DATA_CNF, id, NULL);
		} else
			spin_unlock_irqrestore(isac->hwlock, flags);
		return ret;
	case PH_ACTIVATE_REQ:
		ret = l1_event(dch->l1, hh->prim);
		break;
	case PH_DEACTIVATE_REQ:
		test_and_clear_bit(FLG_L2_ACTIVATED, &dch->Flags);
		ret = l1_event(dch->l1, hh->prim);
		break;
	}

	if (!ret)
		dev_kfree_skb(skb);
	return ret;
}

static int
isac_ctrl(struct isac_hw *isac, u32 cmd, u_long para)
{
	u8 tl = 0;
	u_long flags;

	switch (cmd) {
	case HW_TESTLOOP:
		spin_lock_irqsave(isac->hwlock, flags);
		if (!(isac->type & ISAC_TYPE_ISACSX)) {
			/* TODO: implement for ISAC_TYPE_ISACSX */
			if (para & 1) /* B1 */
				tl |= 0x0c;
			else if (para & 2) /* B2 */
				tl |= 0x3;
			if (ISAC_USE_IOM1 & isac->type) {
				/* IOM 1 Mode */
				if (!tl) {
					isac->write_reg(isac->dch.hw,
						ISAC_SPCR, 0xa);
					isac->write_reg(isac->dch.hw,
						ISAC_ADF1, 0x2);
				} else {
					isac->write_reg(isac->dch.hw,
						ISAC_SPCR, tl);
					isac->write_reg(isac->dch.hw,
						ISAC_ADF1, 0xa);
				}
			} else {
				/* IOM 2 Mode */
				isac->write_reg(isac->dch.hw,
					ISAC_SPCR, tl);
				if (tl)
					isac->write_reg(isac->dch.hw,
						ISAC_ADF1, 0x8);
				else
					isac->write_reg(isac->dch.hw,
						ISAC_ADF1, 0x0);
			}
		}
		spin_unlock_irqrestore(isac->hwlock, flags);
		break;
	default:
		pr_debug("%s: %s unknown command %x %lx\n", isac->name,
			__func__, cmd, para);
		return -1;
	}
	return 0;
}

static int
isac_l1cmd(struct dchannel *dch, u32 cmd)
{
	struct isac_hw *isac = container_of(dch, struct isac_hw, dch);
	u_long flags;

	switch (cmd) {
	case INFO3_P8:
		spin_lock_irqsave(isac->hwlock, flags);
		ph_command(isac, ISAC_CMD_AR8);
		spin_unlock_irqrestore(isac->hwlock, flags);
		break;
	case INFO3_P10:
		spin_lock_irqsave(isac->hwlock, flags);
		ph_command(isac, ISAC_CMD_AR10);
		spin_unlock_irqrestore(isac->hwlock, flags);
		break;
	case HW_RESET_REQ:
		spin_lock_irqsave(isac->hwlock, flags);
		if ((isac->state == ISAC_IND_EI) ||
		    (isac->state == ISAC_IND_DR) ||
		    (isac->state == ISAC_IND_RS))
			ph_command(isac, ISAC_CMD_TIM);
		else
			ph_command(isac, ISAC_CMD_RS);
		spin_unlock_irqrestore(isac->hwlock, flags);
		break;
	case HW_DEACT_REQ:
		skb_queue_purge(&dch->squeue);
		if (dch->tx_skb) {
			dev_kfree_skb(dch->tx_skb);
			dch->tx_skb = NULL;
		}
		dch->tx_idx = 0;
		if (dch->rx_skb) {
			dev_kfree_skb(dch->rx_skb);
			dch->rx_skb = NULL;
		}
		test_and_clear_bit(FLG_TX_BUSY, &dch->Flags);
		if (test_and_clear_bit(FLG_BUSY_TIMER, &dch->Flags))
			del_timer(&dch->timer);
		break;
	case HW_POWERUP_REQ:
		spin_lock_irqsave(isac->hwlock, flags);
		ph_command(isac, ISAC_CMD_TIM);
		spin_unlock_irqrestore(isac->hwlock, flags);
		break;
	case PH_ACTIVATE_IND:
		test_and_set_bit(FLG_ACTIVE, &dch->Flags);
		_queue_data(&dch->dev.D, cmd, MISDN_ID_ANY, 0, NULL,
			GFP_ATOMIC);
		break;
	case PH_DEACTIVATE_IND:
		test_and_clear_bit(FLG_ACTIVE, &dch->Flags);
		_queue_data(&dch->dev.D, cmd, MISDN_ID_ANY, 0, NULL,
			GFP_ATOMIC);
		break;
	default:
		pr_debug("%s: %s unknown command %x\n", isac->name,
			__func__, cmd);
		return -1;
	}
	return 0;
}

static void
isac_release(struct isac_hw *isac)
{
	if (isac->dch.timer.function != NULL) {
		del_timer(&isac->dch.timer);
		isac->dch.timer.function = NULL;
	}
	kfree(isac->mon_rx);
	isac->mon_rx = NULL;
	kfree(isac->mon_tx);
	isac->mon_tx = NULL;
	if (isac->dch.l1)
		l1_event(isac->dch.l1, CLOSE_CHANNEL);
	mISDN_freedchannel(&isac->dch);
}

static void
dbusy_timer_handler(struct isac_hw *isac)
{
	int rbch, star;
	u_long flags;

	if (test_bit(FLG_BUSY_TIMER, &isac->dch.Flags)) {
		spin_lock_irqsave(isac->hwlock, flags);
		rbch = isac->read_reg(isac->dch.hw, ISAC_RBCH);
		star = isac->read_reg(isac->dch.hw, ISAC_STAR);
		pr_debug("%s: D-Channel Busy RBCH %02x STAR %02x\n",
			isac->name, rbch, star);
		if (rbch & ISAC_RBCH_XAC) /* D-Channel Busy */
			test_and_set_bit(FLG_L1_BUSY, &isac->dch.Flags);
		else {
			/* discard frame; reset transceiver */
			test_and_clear_bit(FLG_BUSY_TIMER, &isac->dch.Flags);
			if (isac->dch.tx_idx)
				isac->dch.tx_idx = 0;
			else
				pr_info("%s: ISAC D-Channel Busy no tx_idx\n",
					isac->name);
			/* Transmitter reset */
			isac->write_reg(isac->dch.hw, ISAC_CMDR, 0x01);
		}
		spin_unlock_irqrestore(isac->hwlock, flags);
	}
}

static int
open_dchannel(struct isac_hw *isac, struct channel_req *rq)
{
	pr_debug("%s: %s dev(%d) open from %p\n", isac->name, __func__,
		isac->dch.dev.id, __builtin_return_address(1));
	if (rq->protocol != ISDN_P_TE_S0)
		return -EINVAL;
	if (rq->adr.channel == 1)
		/* E-Channel not supported */
		return -EINVAL;
	rq->ch = &isac->dch.dev.D;
	rq->ch->protocol = rq->protocol;
	if (isac->dch.state == 7)
		_queue_data(rq->ch, PH_ACTIVATE_IND, MISDN_ID_ANY,
		    0, NULL, GFP_KERNEL);
	return 0;
}

static char *ISACVer[] =
{"2086/2186 V1.1", "2085 B1", "2085 B2",
 "2085 V2.3"};

static int
isac_init(struct isac_hw *isac)
{
	u8 val;
	int err = 0;

	if (!isac->dch.l1) {
		err = create_l1(&isac->dch, isac_l1cmd);
		if (err)
			return err;
	}
	isac->mon_tx = NULL;
	isac->mon_rx = NULL;
	isac->dch.timer.function = (void *) dbusy_timer_handler;
	isac->dch.timer.data = (long)isac;
	init_timer(&isac->dch.timer);
	isac->mocr = 0xaa;
	if (isac->type & ISAC_TYPE_ISACSX) {
		/* clear LDD */
		isac->write_reg(isac->dch.hw, ISACSX_TR_CONF0, 0x00);
		/* enable transmitter */
		isac->write_reg(isac->dch.hw, ISACSX_TR_CONF2, 0x00);
		/* transparent mode 0, RAC, stop/go */
		isac->write_reg(isac->dch.hw, ISACSX_MODED, 0xc9);
		/* all HDLC IRQ unmasked */
		isac->write_reg(isac->dch.hw, ISACSX_MASKD, 0x03);
		/* unmask ICD, CID IRQs */
		isac->write_reg(isac->dch.hw, ISACSX_MASK,
		    ~(ISACSX_ISTA_ICD | ISACSX_ISTA_CIC));
		pr_notice("%s: %s ISACSX\n", isac->name, __func__);
		isac_ph_state_change(isac);
		ph_command(isac, ISAC_CMD_RS);
	} else { /* old isac */
		isac->write_reg(isac->dch.hw, ISAC_MASK, 0xff);
		val = isac->read_reg(isac->dch.hw, ISAC_RBCH);
		pr_notice("%s: %s ISAC version (%x): %s\n", isac->name,
			__func__, val, ISACVer[(val >> 5) & 3]);
		isac->type |= ((val >> 5) & 3);
		if (ISAC_USE_IOM1 & isac->type) {
			/* IOM 1 Mode */
			isac->write_reg(isac->dch.hw, ISAC_ADF2, 0x0);
			isac->write_reg(isac->dch.hw, ISAC_SPCR, 0xa);
			isac->write_reg(isac->dch.hw, ISAC_ADF1, 0x2);
			isac->write_reg(isac->dch.hw, ISAC_STCR, 0x70);
			isac->write_reg(isac->dch.hw, ISAC_MODE, 0xc9);
		} else {
			/* IOM 2 Mode */
			if (!isac->adf2)
				isac->adf2 = 0x80;
			isac->write_reg(isac->dch.hw, ISAC_ADF2, isac->adf2);
			isac->write_reg(isac->dch.hw, ISAC_SQXR, 0x2f);
			isac->write_reg(isac->dch.hw, ISAC_SPCR, 0x00);
			isac->write_reg(isac->dch.hw, ISAC_STCR, 0x70);
			isac->write_reg(isac->dch.hw, ISAC_MODE, 0xc9);
			isac->write_reg(isac->dch.hw, ISAC_TIMR, 0x00);
			isac->write_reg(isac->dch.hw, ISAC_ADF1, 0x00);
		}
		isac_ph_state_change(isac);
		ph_command(isac, ISAC_CMD_RS);
		isac->write_reg(isac->dch.hw, ISAC_MASK, 0x0);
	}
	return err;
}

void
isac_clear(struct isac_hw *isac)
{
	u8 val, eval;

	/* Disable all IRQ */
	isac->write_reg(isac->dch.hw, ISAC_MASK, 0xFF);
	val = isac->read_reg(isac->dch.hw, ISAC_STAR);
	pr_debug("%s: ISAC STAR %x\n", isac->name, val);
	val = isac->read_reg(isac->dch.hw, ISAC_MODE);
	pr_debug("%s: ISAC MODE %x\n", isac->name, val);
	val = isac->read_reg(isac->dch.hw, ISAC_ADF2);
	pr_debug("%s: ISAC ADF2 %x\n", isac->name, val);
	val = isac->read_reg(isac->dch.hw, ISAC_ISTA);
	pr_debug("%s: ISAC ISTA %x\n", isac->name, val);
	if (val & 0x01) {
		eval = isac->read_reg(isac->dch.hw, ISAC_EXIR);
		pr_debug("%s: ISAC EXIR %x\n", isac->name, eval);
	}
	val = isac->read_reg(isac->dch.hw, ISAC_CIR0);
	pr_debug("%s: ISAC CIR0 %x\n", isac->name, val);
	isac->state = (val >> 2) & 0xf;
}

int
mISDN_isac_init(struct isac_hw *isac, void *hw, u32 *deb)
{
	if (!debug) /* only the first driver set debug */
		debug = deb;
	mISDN_initdchannel(&isac->dch, MAX_DFRAME_LEN_L1, isac_ph_state_bh);
	isac->dch.hw = hw;
	isac->dch.debug = *deb;
	isac->dch.dev.D.send = isac_l1hw;
	isac->init = isac_init;
	isac->clear = isac_clear;
	isac->interrupt = isac_interrupt;
	isac->release = isac_release;
	isac->ctrl = isac_ctrl;
	isac->open = open_dchannel;
	return 0;
}
EXPORT_SYMBOL(mISDN_isac_init);

static int __init
isac_mod_init(void)
{
	pr_notice("mISDN ISAC module version %s\n", ISAC_REV);
	return 0;
}

static void __exit
isac_mod_cleanup(void)
{
	pr_notice("mISDN ISAC module unloaded\n");
}
module_init(isac_mod_init);
module_exit(isac_mod_cleanup);

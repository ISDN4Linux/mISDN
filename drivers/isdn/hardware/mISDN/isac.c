/* $Id: isac.c,v 1.0 2001/11/02 23:42:27 kkeil Exp $
 *
 * isac.c   ISAC specific routines
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 *		This file is (c) under GNU PUBLIC LICENSE
 *		For changes and modifications please read
 *		../../../Documentation/isdn/HiSax.cert
 */

#define __NO_VERSION__
#include "hisax_hw.h"
#include "isac.h"
#include "arcofi.h"
#include "hisaxl1.h"
#include "helper.h"
#include "debug.h"

#define DBUSY_TIMER_VALUE 80
#define ARCOFI_USE 1

static char *ISACVer[] =
{"2086/2186 V1.1", "2085 B1", "2085 B2",
 "2085 V2.3"};

void
ISACVersion(dchannel_t *dch, char *s)
{
	int val;

	val = dch->readisac(dch->inst.data, ISAC_RBCH);
	printk(KERN_INFO "%s ISAC version (%x): %s\n", s, val, ISACVer[(val >> 5) & 3]);
}

static void
ph_command(dchannel_t *dch, unsigned int command)
{
	if (dch->debug & L1_DEB_ISAC)
		debugprint(&dch->inst, "ph_command %x", command);
	dch->writeisac(dch->inst.data, ISAC_CIX0, (command << 2) | 3);
}

static void
isac_new_ph(dchannel_t *dch)
{
	u_int		prim = PH_SIGNAL | INDICATION;
	u_int		para = 0;
	hisaxif_t	*upif = &dch->inst.up;

	switch (dch->hw.isac.ph_state) {
		case (ISAC_IND_RS):
		case (ISAC_IND_EI):
			dch->inst.lock(dch->inst.data);
			ph_command(dch, ISAC_CMD_DUI);
			dch->inst.unlock(dch->inst.data);
			prim = PH_CONTROL | INDICATION;
			para = HW_RESET;
			break;
		case (ISAC_IND_DID):
			prim = PH_CONTROL | CONFIRM;
			para = HW_DEACTIVATE;
			break;
		case (ISAC_IND_DR):
			prim = PH_CONTROL | INDICATION;
			para = HW_DEACTIVATE;
			break;
		case (ISAC_IND_PU):
			prim = PH_CONTROL | INDICATION;
			para = HW_POWERUP;
			break;
		case (ISAC_IND_RSY):
			para = ANYSIGNAL;
			break;
		case (ISAC_IND_ARD):
			para = INFO2;
			break;
		case (ISAC_IND_AI8):
			para = INFO4_P8;
			break;
		case (ISAC_IND_AI10):
			para = INFO4_P10;
			break;
		default:
			return;
	}
	while(upif) {
		if_link(upif, prim, para, 0, NULL, 0);
		upif = upif->next;
	}
}

static void
isac_rcv(dchannel_t *dch)
{
	struct sk_buff	*skb;
	int		err;

	while ((skb = skb_dequeue(&dch->rqueue))) {
		err = if_addhead(&dch->inst.up, PH_DATA_IND, DINFO_SKB, skb);
		if (err < 0) {
			printk(KERN_WARNING "HiSax: isac deliver err %d\n", err);
			dev_kfree_skb(skb);
		}
	}
}

static void
isac_bh(dchannel_t *dch)
{
	if (!dch)
		return;
	printk(KERN_DEBUG __FUNCTION__": event %x\n", dch->event);
#if 0
	if (test_and_clear_bit(D_CLEARBUSY, &dch->event)) {
		if (dch->debug)
			debugprint(&dch->inst, "D-Channel Busy cleared");
		stptr = dch->stlist;
		while (stptr != NULL) {
			stptr->l1.l1l2(stptr, PH_PAUSE | CONFIRM, NULL);
			stptr = stptr->next;
		}
	}
#endif
	if (test_and_clear_bit(D_L1STATECHANGE, &dch->event))
		isac_new_ph(dch);		
	if (test_and_clear_bit(D_XMTBUFREADY, &dch->event)) {
		struct sk_buff *skb = dch->next_skb;

		if (skb) {
			dch->next_skb = NULL;
			skb_trim(skb, 0);
			if (skb_headroom(skb) < HISAX_HEAD_SIZE) {
				int_errtxt("skb %p %d/%d\n",
					skb, skb_headroom(skb),
					skb_tailroom(skb));
				skb_reserve(skb, HISAX_HEAD_SIZE);
			}
			if (if_addhead(&dch->inst.up, PH_DATA_CNF, DINFO_SKB,
				skb))
				dev_kfree_skb(skb);
		}
	}
	if (test_and_clear_bit(D_RCVBUFREADY, &dch->event))
		isac_rcv(dch);
#if ARCOFI_USE
	if (!test_bit(HW_ARCOFI, &dch->DFlags))
		return;
	if (test_and_clear_bit(D_RX_MON1, &dch->event))
		arcofi_fsm(dch, ARCOFI_RX_END, NULL);
	if (test_and_clear_bit(D_TX_MON1, &dch->event))
		arcofi_fsm(dch, ARCOFI_TX_END, NULL);
#endif
}

void
isac_empty_fifo(dchannel_t *dch, int count)
{
	u_char *ptr;

	if ((dch->debug & L1_DEB_ISAC) && !(dch->debug & L1_DEB_ISAC_FIFO))
		debugprint(&dch->inst, "isac_empty_fifo");

	if ((dch->rx_idx + count) >= MAX_DFRAME_LEN_L1) {
		if (dch->debug & L1_DEB_WARN)
			debugprint(&dch->inst, "isac_empty_fifo overrun %d",
				dch->rx_idx + count);
		dch->writeisac(dch->inst.data, ISAC_CMDR, 0x80);
		dch->rx_idx = 0;
		return;
	}
	ptr = dch->rx_buf + dch->rx_idx;
	dch->rx_idx += count;
	dch->readisacfifo(dch->inst.data, ptr, count);
	dch->writeisac(dch->inst.data, ISAC_CMDR, 0x80);
	if (dch->debug & L1_DEB_ISAC_FIFO) {
		char *t = dch->dlog;

		t += sprintf(t, "isac_empty_fifo cnt %d", count);
		QuickHex(t, ptr, count);
		debugprint(&dch->inst, dch->dlog);
	}
}

static void
isac_fill_fifo(dchannel_t *dch)
{
	int count, more;
	u_char *ptr;

	if ((dch->debug & L1_DEB_ISAC) && !(dch->debug & L1_DEB_ISAC_FIFO))
		debugprint(&dch->inst, "isac_fill_fifo");

	count = dch->tx_len - dch->tx_idx;
	if (count <= 0)
		return;

	more = 0;
	if (count > 32) {
		more = !0;
		count = 32;
	}
	ptr = dch->tx_buf + dch->tx_idx;
	dch->tx_idx += count;
	dch->writeisacfifo(dch->inst.data, ptr, count);
	dch->writeisac(dch->inst.data, ISAC_CMDR, more ? 0x8 : 0xa);
	if (test_and_set_bit(FLG_DBUSY_TIMER, &dch->DFlags)) {
		debugprint(&dch->inst, "isac_fill_fifo dbusytimer running");
		del_timer(&dch->dbusytimer);
	}
	init_timer(&dch->dbusytimer);
	dch->dbusytimer.expires = jiffies + ((DBUSY_TIMER_VALUE * HZ)/1000);
	add_timer(&dch->dbusytimer);
	if (dch->debug & L1_DEB_ISAC_FIFO) {
		char *t = dch->dlog;

		t += sprintf(t, "isac_fill_fifo cnt %d", count);
		QuickHex(t, ptr, count);
		debugprint(&dch->inst, dch->dlog);
	}
}

void
isac_sched_event(dchannel_t *dch, int event)
{
	test_and_set_bit(event, &dch->event);
	queue_task(&dch->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

void
isac_interrupt(dchannel_t *dch, u_char val)
{
	u_char exval, v1;
	struct sk_buff *skb;
	unsigned int count;

	if (dch->debug & L1_DEB_ISAC)
		debugprint(&dch->inst, "ISAC interrupt %x", val);
	if (val & 0x80) {	/* RME */
		exval = dch->readisac(dch->inst.data, ISAC_RSTA);
		if ((exval & 0x70) != 0x20) {
			if (exval & 0x40) {
				if (dch->debug & L1_DEB_WARN)
					debugprint(&dch->inst, "ISAC RDO");
#ifdef ERROR_STATISTIC
				dch->err_rx++;
#endif
			}
			if (!(exval & 0x20)) {
				if (dch->debug & L1_DEB_WARN)
					debugprint(&dch->inst, "ISAC CRC error");
#ifdef ERROR_STATISTIC
				dch->err_crc++;
#endif
			}
			dch->writeisac(dch->inst.data, ISAC_CMDR, 0x80);
		} else {
			count = dch->readisac(dch->inst.data, ISAC_RBCL) & 0x1f;
			if (count == 0)
				count = 32;
			isac_empty_fifo(dch, count);
			if ((count = dch->rx_idx) > 0) {
				dch->rx_idx = 0;
				if (!(skb = alloc_uplink_skb(count)))
					printk(KERN_WARNING "HiSax: D receive out of memory\n");
				else {
					memcpy(skb_put(skb, count), dch->rx_buf, count);
					skb_queue_tail(&dch->rqueue, skb);
				}
			}
		}
		dch->rx_idx = 0;
		isac_sched_event(dch, D_RCVBUFREADY);
	}
	if (val & 0x40) {	/* RPF */
		isac_empty_fifo(dch, 32);
	}
	if (val & 0x20) {	/* RSC */
		/* never */
		if (dch->debug & L1_DEB_WARN)
			debugprint(&dch->inst, "ISAC RSC interrupt");
	}
	if (val & 0x10) {	/* XPR */
		if (test_and_clear_bit(FLG_DBUSY_TIMER, &dch->DFlags))
			del_timer(&dch->dbusytimer);
		if (test_and_clear_bit(FLG_L1_DBUSY, &dch->DFlags))
			isac_sched_event(dch, D_CLEARBUSY);
		if (dch->tx_idx < dch->tx_len) {
			isac_fill_fifo(dch);
		} else {
			if (test_and_clear_bit(FLG_TX_NEXT, &dch->DFlags)) {
				if (dch->next_skb) {
					dch->tx_len = dch->next_skb->len;
					memcpy(dch->tx_buf,
						dch->next_skb->data, dch->tx_len);
					dch->tx_idx = 0;
					isac_fill_fifo(dch);
					isac_sched_event(dch, D_XMTBUFREADY);
				} else {
					printk(KERN_WARNING "isac tx irq TX_NEXT without skb\n");
					test_and_clear_bit(FLG_TX_BUSY, &dch->DFlags);
				}
			} else
				test_and_clear_bit(FLG_TX_BUSY, &dch->DFlags);
		}
	}
	if (val & 0x04) {	/* CISQ */
		exval = dch->readisac(dch->inst.data, ISAC_CIR0);
		if (dch->debug & L1_DEB_ISAC)
			debugprint(&dch->inst, "ISAC CIR0 %02X", exval );
		if (exval & 2) {
			dch->hw.isac.ph_state = (exval >> 2) & 0xf;
			if (dch->debug & L1_DEB_ISAC)
				debugprint(&dch->inst, "ph_state change %x", dch->hw.isac.ph_state);
			isac_sched_event(dch, D_L1STATECHANGE);
		}
		if (exval & 1) {
			exval = dch->readisac(dch->inst.data, ISAC_CIR1);
			if (dch->debug & L1_DEB_ISAC)
				debugprint(&dch->inst, "ISAC CIR1 %02X", exval );
		}
	}
	if (val & 0x02) {	/* SIN */
		/* never */
		if (dch->debug & L1_DEB_WARN)
			debugprint(&dch->inst, "ISAC SIN interrupt");
	}
	if (val & 0x01) {	/* EXI */
		exval = dch->readisac(dch->inst.data, ISAC_EXIR);
		if (dch->debug & L1_DEB_WARN)
			debugprint(&dch->inst, "ISAC EXIR %02x", exval);
		if (exval & 0x80) {  /* XMR */
			debugprint(&dch->inst, "ISAC XMR");
			printk(KERN_WARNING "HiSax: ISAC XMR\n");
		}
		if (exval & 0x40) {  /* XDU */
			debugprint(&dch->inst, "ISAC XDU");
			printk(KERN_WARNING "HiSax: ISAC XDU\n");
#ifdef ERROR_STATISTIC
			dch->err_tx++;
#endif
			if (test_and_clear_bit(FLG_DBUSY_TIMER, &dch->DFlags))
				del_timer(&dch->dbusytimer);
			if (test_and_clear_bit(FLG_L1_DBUSY, &dch->DFlags))
				isac_sched_event(dch, D_CLEARBUSY);
			if (test_bit(FLG_TX_BUSY, &dch->DFlags)) {
				/* Restart frame */
				dch->tx_idx = 0;
				isac_fill_fifo(dch);
			} else {
				printk(KERN_WARNING "HiSax: ISAC XDU no TX_BUSY\n");
				debugprint(&dch->inst, "ISAC XDU no TX_BUSY");
				if (test_and_clear_bit(FLG_TX_NEXT, &dch->DFlags)) {
					if (dch->next_skb) {
						dch->tx_len = dch->next_skb->len;
						memcpy(dch->tx_buf,
							dch->next_skb->data,
							dch->tx_len);
						dch->tx_idx = 0;
						isac_fill_fifo(dch);
						isac_sched_event(dch, D_XMTBUFREADY);
					} else {
						printk(KERN_WARNING "isac xdu irq TX_NEXT without skb\n");
					}
				}
			}
		}
		if (exval & 0x04) {  /* MOS */
			v1 = dch->readisac(dch->inst.data, ISAC_MOSR);
			if (dch->debug & L1_DEB_MONITOR)
				debugprint(&dch->inst, "ISAC MOSR %02x", v1);
#if ARCOFI_USE
			if (v1 & 0x08) {
				if (!dch->hw.isac.mon_rx) {
					if (!(dch->hw.isac.mon_rx = kmalloc(MAX_MON_FRAME, GFP_ATOMIC))) {
						if (dch->debug & L1_DEB_WARN)
							debugprint(&dch->inst, "ISAC MON RX out of memory!");
						dch->hw.isac.mocr &= 0xf0;
						dch->hw.isac.mocr |= 0x0a;
						dch->writeisac(dch->inst.data, ISAC_MOCR, dch->hw.isac.mocr);
						goto afterMONR0;
					} else
						dch->hw.isac.mon_rxp = 0;
				}
				if (dch->hw.isac.mon_rxp >= MAX_MON_FRAME) {
					dch->hw.isac.mocr &= 0xf0;
					dch->hw.isac.mocr |= 0x0a;
					dch->writeisac(dch->inst.data, ISAC_MOCR, dch->hw.isac.mocr);
					dch->hw.isac.mon_rxp = 0;
					if (dch->debug & L1_DEB_WARN)
						debugprint(&dch->inst, "ISAC MON RX overflow!");
					goto afterMONR0;
				}
				dch->hw.isac.mon_rx[dch->hw.isac.mon_rxp++] = dch->readisac(dch->inst.data, ISAC_MOR0);
				if (dch->debug & L1_DEB_MONITOR)
					debugprint(&dch->inst, "ISAC MOR0 %02x", dch->hw.isac.mon_rx[dch->hw.isac.mon_rxp -1]);
				if (dch->hw.isac.mon_rxp == 1) {
					dch->hw.isac.mocr |= 0x04;
					dch->writeisac(dch->inst.data, ISAC_MOCR, dch->hw.isac.mocr);
				}
			}
		      afterMONR0:
			if (v1 & 0x80) {
				if (!dch->hw.isac.mon_rx) {
					if (!(dch->hw.isac.mon_rx = kmalloc(MAX_MON_FRAME, GFP_ATOMIC))) {
						if (dch->debug & L1_DEB_WARN)
							debugprint(&dch->inst, "ISAC MON RX out of memory!");
						dch->hw.isac.mocr &= 0x0f;
						dch->hw.isac.mocr |= 0xa0;
						dch->writeisac(dch->inst.data, ISAC_MOCR, dch->hw.isac.mocr);
						goto afterMONR1;
					} else
						dch->hw.isac.mon_rxp = 0;
				}
				if (dch->hw.isac.mon_rxp >= MAX_MON_FRAME) {
					dch->hw.isac.mocr &= 0x0f;
					dch->hw.isac.mocr |= 0xa0;
					dch->writeisac(dch->inst.data, ISAC_MOCR, dch->hw.isac.mocr);
					dch->hw.isac.mon_rxp = 0;
					if (dch->debug & L1_DEB_WARN)
						debugprint(&dch->inst, "ISAC MON RX overflow!");
					goto afterMONR1;
				}
				dch->hw.isac.mon_rx[dch->hw.isac.mon_rxp++] = dch->readisac(dch->inst.data, ISAC_MOR1);
				if (dch->debug & L1_DEB_MONITOR)
					debugprint(&dch->inst, "ISAC MOR1 %02x", dch->hw.isac.mon_rx[dch->hw.isac.mon_rxp -1]);
				dch->hw.isac.mocr |= 0x40;
				dch->writeisac(dch->inst.data, ISAC_MOCR, dch->hw.isac.mocr);
			}
		      afterMONR1:
			if (v1 & 0x04) {
				dch->hw.isac.mocr &= 0xf0;
				dch->writeisac(dch->inst.data, ISAC_MOCR, dch->hw.isac.mocr);
				dch->hw.isac.mocr |= 0x0a;
				dch->writeisac(dch->inst.data, ISAC_MOCR, dch->hw.isac.mocr);
				isac_sched_event(dch, D_RX_MON0);
			}
			if (v1 & 0x40) {
				dch->hw.isac.mocr &= 0x0f;
				dch->writeisac(dch->inst.data, ISAC_MOCR, dch->hw.isac.mocr);
				dch->hw.isac.mocr |= 0xa0;
				dch->writeisac(dch->inst.data, ISAC_MOCR, dch->hw.isac.mocr);
				isac_sched_event(dch, D_RX_MON1);
			}
			if (v1 & 0x02) {
				if ((!dch->hw.isac.mon_tx) || (dch->hw.isac.mon_txc && 
					(dch->hw.isac.mon_txp >= dch->hw.isac.mon_txc) && 
					!(v1 & 0x08))) {
					dch->hw.isac.mocr &= 0xf0;
					dch->writeisac(dch->inst.data, ISAC_MOCR, dch->hw.isac.mocr);
					dch->hw.isac.mocr |= 0x0a;
					dch->writeisac(dch->inst.data, ISAC_MOCR, dch->hw.isac.mocr);
					if (dch->hw.isac.mon_txc &&
						(dch->hw.isac.mon_txp >= dch->hw.isac.mon_txc))
						isac_sched_event(dch, D_TX_MON0);
					goto AfterMOX0;
				}
				if (dch->hw.isac.mon_txc && (dch->hw.isac.mon_txp >= dch->hw.isac.mon_txc)) {
					isac_sched_event(dch, D_TX_MON0);
					goto AfterMOX0;
				}
				dch->writeisac(dch->inst.data, ISAC_MOX0,
					dch->hw.isac.mon_tx[dch->hw.isac.mon_txp++]);
				if (dch->debug & L1_DEB_MONITOR)
					debugprint(&dch->inst, "ISAC %02x -> MOX0", dch->hw.isac.mon_tx[dch->hw.isac.mon_txp -1]);
			}
		      AfterMOX0:
			if (v1 & 0x20) {
				if ((!dch->hw.isac.mon_tx) || (dch->hw.isac.mon_txc && 
					(dch->hw.isac.mon_txp >= dch->hw.isac.mon_txc) && 
					!(v1 & 0x80))) {
					dch->hw.isac.mocr &= 0x0f;
					dch->writeisac(dch->inst.data, ISAC_MOCR, dch->hw.isac.mocr);
					dch->hw.isac.mocr |= 0xa0;
					dch->writeisac(dch->inst.data, ISAC_MOCR, dch->hw.isac.mocr);
					if (dch->hw.isac.mon_txc &&
						(dch->hw.isac.mon_txp >= dch->hw.isac.mon_txc))
						isac_sched_event(dch, D_TX_MON1);
					goto AfterMOX1;
				}
				if (dch->hw.isac.mon_txc && (dch->hw.isac.mon_txp >= dch->hw.isac.mon_txc)) {
					isac_sched_event(dch, D_TX_MON1);
					goto AfterMOX1;
				}
				dch->writeisac(dch->inst.data, ISAC_MOX1,
					dch->hw.isac.mon_tx[dch->hw.isac.mon_txp++]);
				if (dch->debug & L1_DEB_MONITOR)
					debugprint(&dch->inst, "ISAC %02x -> MOX1", dch->hw.isac.mon_tx[dch->hw.isac.mon_txp -1]);
			}
		      AfterMOX1:
#endif
		}
	}
}

int
ISAC_l1hw(hisaxif_t *hif, struct sk_buff *skb)
{
	dchannel_t	*dch;
	u_char		tl;
	int ret = -EINVAL;
	hisax_head_t	*hh;

	if (!hif || !skb)
		return(ret);
	hh = (hisax_head_t *)skb->data;
	if (skb->len < HISAX_FRAME_MIN)
		return(ret);
	dch = hif->fdata;
	ret = 0;
	if (hh->prim == PH_DATA_REQ) {
		if (dch->next_skb) {
			debugprint(&dch->inst, " l2l1 next_skb exist this shouldn't happen");
			return(-EBUSY);
		}
		skb_pull(skb, HISAX_HEAD_SIZE);
		dch->inst.lock(dch->inst.data);
		if (test_and_set_bit(FLG_TX_BUSY, &dch->DFlags)) {
			test_and_set_bit(FLG_TX_NEXT, &dch->DFlags);
			dch->next_skb = skb;
			dch->inst.unlock(dch->inst.data);
			return(0);
		} else {
			dch->tx_len = skb->len;
			memcpy(dch->tx_buf, skb->data, dch->tx_len);
			dch->tx_idx = 0;
			isac_fill_fifo(dch);
			dch->inst.unlock(dch->inst.data);
			return(if_addhead(&dch->inst.up, PH_DATA_CNF,
				DINFO_SKB, skb));
		}
	} else if (hh->prim == (PH_SIGNAL | REQUEST)) {
		dch->inst.lock(dch->inst.data);
		if (hh->dinfo == INFO3_P8)
			ph_command(dch, ISAC_CMD_AR8);
		else if (hh->dinfo == INFO3_P10)
			ph_command(dch, ISAC_CMD_AR10);
		else
			ret = -EINVAL;
		dch->inst.unlock(dch->inst.data);
	} else if (hh->prim == (PH_CONTROL | REQUEST)) {
		dch->inst.lock(dch->inst.data);
		if (hh->dinfo == HW_RESET) {
			if ((dch->hw.isac.ph_state == ISAC_IND_EI) ||
				(dch->hw.isac.ph_state == ISAC_IND_DR) ||
				(dch->hw.isac.ph_state == ISAC_IND_RS))
			        ph_command(dch, ISAC_CMD_TIM);
			else
				ph_command(dch, ISAC_CMD_RS);
		} else if (hh->dinfo == HW_POWERUP) {
			ph_command(dch, ISAC_CMD_TIM);
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
				isac_sched_event(dch, D_CLEARBUSY);
		} else if ((hh->dinfo & HW_TESTLOOP) == HW_TESTLOOP) {
			tl = 0;
			if (1 & hh->dinfo)
				tl |= 0x0c;
			if (2 & hh->dinfo)
				tl |= 0x3;
			if (test_bit(HW_IOM1, &dch->DFlags)) {
				/* IOM 1 Mode */
				if (!tl) {
					dch->writeisac(dch->inst.data, ISAC_SPCR, 0xa);
					dch->writeisac(dch->inst.data, ISAC_ADF1, 0x2);
				} else {
					dch->writeisac(dch->inst.data, ISAC_SPCR, tl);
					dch->writeisac(dch->inst.data, ISAC_ADF1, 0xa);
				}
			} else {
				/* IOM 2 Mode */
				dch->writeisac(dch->inst.data, ISAC_SPCR, tl);
				if (tl)
					dch->writeisac(dch->inst.data, ISAC_ADF1, 0x8);
				else
					dch->writeisac(dch->inst.data, ISAC_ADF1, 0x0);
			}
		} else {
			if (dch->debug & L1_DEB_WARN)
				debugprint(&dch->inst, "isac_l1hw unknown ctrl %x",
					hh->dinfo);
			ret = -EINVAL;
		}
		dch->inst.unlock(dch->inst.data);
	} else {
		if (dch->debug & L1_DEB_WARN)
			debugprint(&dch->inst, "isac_l1hw unknown prim %x",
				hh->prim);
		ret = -EINVAL;
	}
	if (!ret)
		dev_kfree_skb(skb);
	return(ret);
}

void 
free_isac(dchannel_t *dch) {
	if (dch->hw.isac.mon_rx) {
		kfree(dch->hw.isac.mon_rx);
		dch->hw.isac.mon_rx = NULL;
	}
	if (dch->hw.isac.mon_tx) {
		kfree(dch->hw.isac.mon_tx);
		dch->hw.isac.mon_tx = NULL;
	}
	if (dch->dbusytimer.function != NULL) {
		del_timer(&dch->dbusytimer);
		dch->dbusytimer.function = NULL;
	}
}

static void
dbusy_timer_handler(dchannel_t *dch)
{
	int	rbch, star;

	if (test_bit(FLG_DBUSY_TIMER, &dch->DFlags)) {
		dch->inst.lock(dch->inst.data);
		rbch = dch->readisac(dch->inst.data, ISAC_RBCH);
		star = dch->readisac(dch->inst.data, ISAC_STAR);
		if (dch->debug) 
			debugprint(&dch->inst, "D-Channel Busy RBCH %02x STAR %02x",
				rbch, star);
		if (rbch & ISAC_RBCH_XAC) { /* D-Channel Busy */
			test_and_set_bit(FLG_L1_DBUSY, &dch->DFlags);
#if 0
			stptr = dch->stlist;
			while (stptr != NULL) {
				stptr->l1.l1l2(stptr, PH_PAUSE | INDICATION, NULL);
				stptr = stptr->next;
			}
#endif
		} else {
			/* discard frame; reset transceiver */
			test_and_clear_bit(FLG_DBUSY_TIMER, &dch->DFlags);
			if (dch->tx_idx) {
				dch->tx_idx = 0;
			} else {
				printk(KERN_WARNING "HiSax: ISAC D-Channel Busy no tx_idx\n");
				debugprint(&dch->inst, "D-Channel Busy no tx_idx");
			}
			/* Transmitter reset */
			dch->writeisac(dch->inst.data, ISAC_CMDR, 0x01);
		}
		dch->inst.unlock(dch->inst.data);
	}
}

void
init_isac(dchannel_t *dch)
{
  	dch->writeisac(dch->inst.data, ISAC_MASK, 0xff);
	dch->tqueue.routine = (void *) (void *) isac_bh;
	dch->hw.isac.mon_tx = NULL;
	dch->hw.isac.mon_rx = NULL;
	dch->dbusytimer.function = (void *) dbusy_timer_handler;
	dch->dbusytimer.data = (long) dch;
	init_timer(&dch->dbusytimer);
  	dch->hw.isac.mocr = 0xaa;
	if (test_bit(HW_IOM1, &dch->DFlags)) {
		/* IOM 1 Mode */
		dch->writeisac(dch->inst.data, ISAC_ADF2, 0x0);
		dch->writeisac(dch->inst.data, ISAC_SPCR, 0xa);
		dch->writeisac(dch->inst.data, ISAC_ADF1, 0x2);
		dch->writeisac(dch->inst.data, ISAC_STCR, 0x70);
		dch->writeisac(dch->inst.data, ISAC_MODE, 0xc9);
	} else {
		/* IOM 2 Mode */
		if (!dch->hw.isac.adf2)
			dch->hw.isac.adf2 = 0x80;
		dch->writeisac(dch->inst.data, ISAC_ADF2, dch->hw.isac.adf2);
		dch->writeisac(dch->inst.data, ISAC_SQXR, 0x2f);
		dch->writeisac(dch->inst.data, ISAC_SPCR, 0x00);
		dch->writeisac(dch->inst.data, ISAC_STCR, 0x70);
		dch->writeisac(dch->inst.data, ISAC_MODE, 0xc9);
		dch->writeisac(dch->inst.data, ISAC_TIMR, 0x00);
		dch->writeisac(dch->inst.data, ISAC_ADF1, 0x00);
	}
	isac_sched_event(dch, D_L1STATECHANGE);
	ph_command(dch, ISAC_CMD_RS);
	dch->writeisac(dch->inst.data, ISAC_MASK, 0x0);
}

void
clear_pending_isac_ints(dchannel_t *dch)
{
	int val, eval;

	/* Disable all IRQ */
	dch->writeisac(dch->inst.data, ISAC_MASK, 0xFF);
	val = dch->readisac(dch->inst.data, ISAC_STAR);
	debugprint(&dch->inst, "ISAC STAR %x", val);
	val = dch->readisac(dch->inst.data, ISAC_MODE);
	debugprint(&dch->inst, "ISAC MODE %x", val);
	val = dch->readisac(dch->inst.data, ISAC_ADF2);
	debugprint(&dch->inst, "ISAC ADF2 %x", val);
	val = dch->readisac(dch->inst.data, ISAC_ISTA);
	debugprint(&dch->inst, "ISAC ISTA %x", val);
	if (val & 0x01) {
		eval = dch->readisac(dch->inst.data, ISAC_EXIR);
		debugprint(&dch->inst, "ISAC EXIR %x", eval);
	}
	val = dch->readisac(dch->inst.data, ISAC_CIR0);
	debugprint(&dch->inst, "ISAC CIR0 %x", val);
	dch->hw.isac.ph_state = (val >> 2) & 0xf;
}

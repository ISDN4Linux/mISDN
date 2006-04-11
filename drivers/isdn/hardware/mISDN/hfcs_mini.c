/* $Id: hfcs_mini.c,v 1.7 2006/04/11 13:13:30 crich Exp $
 *
 * mISDN driver for Colognechip HFC-S mini Evaluation Card
 *
 * Authors : Martin Bachem, Joerg Ciesielski
 * Contact : info@colognechip.com
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
 *******************************************************************************
 *
 * MODULE PARAMETERS:
 * (NOTE: layermask and protocol must be given for all ports,
 *  not for the number of cards.)
 *
 * - protocol=<p1>[,p2,p3...]
 *   Values:
 *      <bit  3 -  0>  D-channel protocol id
 *      <bit  4 -  4>  Flags for special features
 *      <bit 31 -  5>  Spare (set to 0)
 *
 *        D-channel protocol ids
 *        - 1       1TR6 (not released yet)
 *        - 2       DSS1
 *
 *        Feature Flags
 *        <bit 4>   0x0010  Net side stack (NT mode)
 *        <bit 5>   0x0020  PCI mode (0=master, 1=slave)
 *        <bit 6>   0x0040  not in use
 *        <bit 7>   0x0080  B channel loop (for layer1 tests)
 *
 * - layermask=<l1>[,l2,l3...] (32bit):
 *        mask of layers to be used for D-channel stack
 *
 * - debug:
 *        enable debugging (see hfcs_mini.h for debug options)
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <asm/timex.h>
#include "layer1.h"
#include "debug.h"
#include "hfcs_mini.h"
#include "hfcsmcc.h"

#if HFCBRIDGE == BRIDGE_HFCPCI
#include <linux/pci.h>
#endif

static const char hfcsmini_rev[] = "$Revision: 1.7 $";

#define MAX_CARDS	8
static int card_cnt;
static u_int protocol[MAX_CARDS];
static int layermask[MAX_CARDS];

static mISDNobject_t hw_mISDNObj;
static int debug = 0;


#ifdef MODULE
MODULE_LICENSE("GPL");
module_param(debug, uint, S_IRUGO | S_IWUSR);

#ifdef OLD_MODULE_PARAM_ARRAY
static int num_protocol=0, num_layermask=0;
module_param_array(protocol, uint, num_protocol, S_IRUGO | S_IWUSR);
module_param_array(layermask, uint, num_layermask, S_IRUGO | S_IWUSR);
#else
module_param_array(protocol, uint, NULL, S_IRUGO | S_IWUSR);
module_param_array(layermask, uint, NULL, S_IRUGO | S_IWUSR);
#endif

#endif

#if HFCBRIDGE == BRIDGE_HFCPCI

static inline void
hfcsmini_sel_reg(hfcsmini_hw * hw, __u8 reg_addr)
{
	outb(6, hw->iobase + 3); /* A0 = 1, reset = 1 */
	outb(reg_addr, hw->iobase + 1); /* write register number */
	outb(4, hw->iobase + 3); /* A0 = 0, reset = 1 */
}


static inline __u8
read_hfcsmini(hfcsmini_hw * hw, __u8 reg_addr)
{
	register u_char ret;
	
#ifdef SPIN_LOCK_HFCSMINI_REGISTER
	spin_lock_irq(&hw->rlock);
#endif
	hfcsmini_sel_reg(hw, reg_addr);
	ret = inb(hw->iobase + 1);
#ifdef SPIN_LOCK_HFCSMINI_REGISTER	
	spin_unlock_irq(&hw->rlock);
#endif	
	return(ret);
}


/* read register in already spin-locked irq context */
static inline __u8
read_hfcsmini_irq(hfcsmini_hw * hw, __u8 reg_addr)
{
	register u_char ret;
	hfcsmini_sel_reg(hw, reg_addr);
	ret = inb(hw->iobase + 1);
	return(ret);
}


static inline __u8  
read_hfcsmini_stable(hfcsmini_hw * hw, __u8 reg_addr)
{
	register u_char in1, in2; 

#ifdef SPIN_LOCK_HFCSMINI_REGISTER
	spin_lock_irq(&hw->rlock);
#endif
	hfcsmini_sel_reg(hw, reg_addr);

	in1 = inb(hw->iobase + 1);
	// loop until 2 equal accesses
	while((in2=inb(hw->iobase + 1))!=in1) in1=in2;
	
#ifdef SPIN_LOCK_HFCSMINI_REGISTER	
	spin_unlock_irq(&hw->rlock);
#endif	
	return(in1);
}


static inline void
write_hfcsmini(hfcsmini_hw * hw, __u8 reg_addr, __u8 value)
{
#ifdef SPIN_LOCK_HFCSMINI_REGISTER
	spin_lock_irq(&hw->rlock);
#endif
	hfcsmini_sel_reg(hw, reg_addr);
	outb(value, hw->iobase + 1);
#ifdef SPIN_LOCK_HFCSMINI_REGISTER	
	spin_unlock_irq(&hw->rlock);
#endif	
}

#endif


static void
hfcsmini_ph_command(channel_t * dch, u_char command)
{
	hfcsmini_hw *hw = dch->hw;
	
	if (dch->debug)
		mISDN_debugprint(&dch->inst,
				 "%s command(%i) channel(%i)",
				 __FUNCTION__, command, dch->channel);

	switch (command) {
		case HFC_L1_ACTIVATE_TE:
			if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES)) {
				mISDN_debugprint(&dch->inst,
						 "HFC_L1_ACTIVATE_TE channel(%i) command(%i)",
						 dch->channel, command);
			}

			write_hfcsmini(hw, R_ST_WR_STA, (M_ST_LD_STA | (M1_ST_SET_STA*4)));
			udelay(125); /* to be sure INFO1 signals are sent */
			write_hfcsmini(hw, R_ST_WR_STA, (M1_ST_SET_STA * 4));
			break;
			
		case HFC_L1_FORCE_DEACTIVATE_TE:
			write_hfcsmini(hw, R_ST_WR_STA, (M_ST_LD_STA | (M1_ST_SET_STA*3)));
			udelay(7); /* wait at least 5,21 us */
			write_hfcsmini(hw, R_ST_WR_STA, (M1_ST_SET_STA*3));
			break;
			

		case HFC_L1_ACTIVATE_NT:
			if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
				mISDN_debugprint(&dch->inst,
					 "HFC_L1_ACTIVATE_NT channel(%i)");

			write_hfcsmini(hw, R_ST_WR_STA, (M1_ST_ACT | M_SET_G2_G3));
			break;

		case HFC_L1_DEACTIVATE_NT:
			if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
				mISDN_debugprint(&dch->inst,
						 "HFC_L1_DEACTIVATE_NT channel(%i)");

			write_hfcsmini(hw, R_ST_WR_STA, (M1_ST_ACT * 2));
			break;
			
		case HFC_L1_TESTLOOP_B1:
			break;
			
		case HFC_L1_TESTLOOP_B2:
			break;

	}
}


/*********************************/
/* S0 state change event handler */
/*********************************/
static void
s0_new_state(channel_t * dch)
{
	u_int prim = PH_SIGNAL | INDICATION;
	u_int para = 0;
	hfcsmini_hw *hw = dch->hw;

	if (hw->portmode & PORT_MODE_TE) {
		if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
			mISDN_debugprint(&dch->inst,
					 "%s: TE %d",
					 __FUNCTION__, dch->state);

		switch (dch->state) {
			case (0):
				prim = PH_CONTROL | INDICATION;
				para = HW_RESET;
				break;
			case (3):
				prim = PH_CONTROL | INDICATION;
				para = HW_DEACTIVATE;
				break;
			case (6):
				para = INFO2;
				break;
			case (7):
				para = INFO4_P8;
				break;
			case (5):
			case (8):
				para = ANYSIGNAL;
				break;
			default:
				return;
		}
		if (dch->state== 7)
			test_and_set_bit(FLG_ACTIVE, &dch->Flags);
		else
			test_and_clear_bit(FLG_ACTIVE, &dch->Flags);
	} // PORT_MODE_TE

	if (hw->portmode & PORT_MODE_NT) {
		if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
			mISDN_debugprint(&dch->inst,
					 "%s: NT %d",
					 __FUNCTION__, dch->state);

		switch (dch->state) {
			case (1):
				hw->nt_timer = 0;
				hw->portmode &= ~NT_TIMER;
				prim = PH_DEACTIVATE | INDICATION;
				test_and_clear_bit(FLG_ACTIVE, &dch->Flags);
				para = 0;
				break;
			case (2):
				if (hw->nt_timer < 0) {
					hw->nt_timer = 0;
					hw->portmode &= ~NT_TIMER;
					hfcsmini_ph_command(dch,
							HFC_L1_DEACTIVATE_NT);
				} else {
					hw->nt_timer = NT_T1_COUNT;
					hw->portmode |= NT_TIMER;
					write_hfcsmini(hw, R_ST_WR_STA, M_SET_G2_G3);
				}
				return;
			case (3):
				hw->nt_timer = 0;
				hw->portmode &= ~NT_TIMER;
				prim = PH_ACTIVATE | INDICATION;
				test_and_set_bit(FLG_ACTIVE, &dch->Flags);
				para = 0;
				break;
			case (4):
				hw->nt_timer = 0;
				hw->portmode &= ~NT_TIMER;
				return;
			default:
				break;
		}
	} // PORT_MODE_NT
	mISDN_queue_data(&dch->inst, FLG_MSG_UP, prim, para, 0, NULL, 0);
}

/*************************************/
/* Layer 1 D-channel hardware access */
/*************************************/
static int
handle_dmsg(channel_t *dch, struct sk_buff *skb)
{
	int		ret = 0;
	mISDN_head_t	*hh = mISDN_HEAD_P(skb);
	hfcsmini_hw	*hw = dch->hw;
	u_long		flags;
		
	if (hh->prim == (PH_SIGNAL | REQUEST)) {
		ret = -EINVAL;
	} else if (hh->prim == (PH_CONTROL | REQUEST)) {
		spin_lock_irqsave(&hw->rlock, flags);
		if (hh->dinfo == HW_RESET) {
			if (dch->state != 0)
				hfcsmini_ph_command(dch, HFC_L1_ACTIVATE_TE);
			spin_unlock_irqrestore(&hw->rlock, flags);
			skb_trim(skb, 0);
			return(mISDN_queueup_newhead(&dch->inst, 0, PH_CONTROL | INDICATION,HW_POWERUP, skb));
		} else if (hh->dinfo == HW_DEACTIVATE) {
			if (dch->next_skb) {
				dev_kfree_skb(dch->next_skb);
				dch->next_skb = NULL;
			}
			test_and_clear_bit(FLG_TX_NEXT, &dch->Flags);
			test_and_clear_bit(FLG_TX_BUSY, &dch->Flags);
#ifdef FIXME
			if (test_and_clear_bit(FLG_L1_DBUSY, &dch->Flags))
				dchannel_sched_event(dch, D_CLEARBUSY);
#endif
		} else if ((hh->dinfo & HW_TESTLOOP) == HW_TESTLOOP) {
			if (1 & hh->dinfo)
				hfcsmini_ph_command(dch, HFC_L1_TESTLOOP_B1);
				
			if (2 & hh->dinfo)
				hfcsmini_ph_command(dch, HFC_L1_TESTLOOP_B2);
				
		} else if (hh->dinfo == HW_POWERUP) {
			hfcsmini_ph_command(dch, HFC_L1_FORCE_DEACTIVATE_TE);
		} else {
			if (dch->debug & L1_DEB_WARN)
				mISDN_debugprint(&dch->inst,
					"hfcsmini_l1hw unknown ctrl %x",
					hh->dinfo);
			ret = -EINVAL;
		}
		spin_unlock_irqrestore(&hw->rlock, flags);
	} else if (hh->prim == (PH_ACTIVATE | REQUEST)) {
		spin_lock_irqsave(&hw->rlock, flags);
		if (hw->portmode & PORT_MODE_NT) {
			hfcsmini_ph_command(dch, HFC_L1_ACTIVATE_NT);
		} else {
			if (dch->debug & L1_DEB_WARN)
				mISDN_debugprint(&dch->inst,
					"%s: PH_ACTIVATE none NT mode",
					__FUNCTION__);
			ret = -EINVAL;
		}
		spin_unlock_irqrestore(&hw->rlock, flags);
	} else if (hh->prim == (PH_DEACTIVATE | REQUEST)) {
		spin_lock_irqsave(&hw->rlock, flags);
		if (hw->portmode & PORT_MODE_NT) {
			hfcsmini_ph_command(dch, HFC_L1_DEACTIVATE_NT);
			if (test_and_clear_bit(FLG_TX_NEXT, &dch->Flags)) {
				dev_kfree_skb(dch->next_skb);
				dch->next_skb = NULL;
			}
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
			test_and_clear_bit(FLG_ACTIVE, &dch->Flags);
		} else {
			if (dch->debug & L1_DEB_WARN)
				mISDN_debugprint(&dch->inst,
					"%s: PH_DEACTIVATE none NT mode",
					__FUNCTION__);
			ret = -EINVAL;
		}
		spin_unlock_irqrestore(&hw->rlock, flags);
	} else if ((hh->prim & MISDN_CMD_MASK) == MGR_SHORTSTATUS) {
		u_int temp = hh->dinfo & SSTATUS_ALL; // remove SSTATUS_BROADCAST_BIT
		if ((hw->portmode & PORT_MODE_NT) &&
			(temp == SSTATUS_ALL || temp == SSTATUS_L1)) {
			if (hh->dinfo & SSTATUS_BROADCAST_BIT)
				temp = dch->inst.id | MSG_BROADCAST;
			else
				temp = hh->addr | FLG_MSG_TARGET;
			skb_trim(skb, 0);
			hh->dinfo = test_bit(FLG_ACTIVE, &dch->Flags) ?
				SSTATUS_L1_ACTIVATED : SSTATUS_L1_DEACTIVATED;
			hh->prim = MGR_SHORTSTATUS | CONFIRM;
			return(mISDN_queue_message(&dch->inst, temp, skb));
		}
		ret = -EOPNOTSUPP;
	} else {
		printk(KERN_WARNING "%s %s: unknown prim(%x)\n",
		       hw->card_name, __FUNCTION__, hh->prim);
		ret = -EAGAIN;
	}
	if (!ret)
		dev_kfree_skb(skb);
	return (ret);
}

/*************************************/
/* Layer 1 B-channel hardware access */
/*************************************/
static int
handle_bmsg(channel_t *bch, struct sk_buff *skb)
{
	hfcsmini_hw 	*hw = bch->hw;
	int		ret = 0;
	mISDN_head_t	*hh = mISDN_HEAD_P(skb);
	u_long		flags;

	if ((hh->prim == (PH_ACTIVATE | REQUEST)) ||
		(hh->prim == (DL_ESTABLISH | REQUEST))) {
		if (!test_and_set_bit(FLG_ACTIVE, &bch->Flags)) {
			spin_lock_irqsave(&hw->rlock, flags);
			ret = setup_channel(hw, bch->channel,
				bch->inst.pid.protocol[1]);
			if (bch->inst.pid.protocol[2] == ISDN_PID_L2_B_TRANS)
				test_and_set_bit(FLG_L2DATA, &bch->Flags);
			spin_unlock_irqrestore(&hw->rlock, flags);
		}
#ifdef FIXME
		if (bch->inst.pid.protocol[2] == ISDN_PID_L2_B_RAWDEV)
			if (bch->dev)
				if_link(&bch->dev->rport.pif,
					hh->prim | CONFIRM, 0, 0, NULL, 0);
#endif
		skb_trim(skb, 0);
		return(mISDN_queueup_newhead(&bch->inst, 0, hh->prim | CONFIRM, ret, skb));
	} else if ((hh->prim == (PH_DEACTIVATE | REQUEST)) ||
		(hh->prim == (DL_RELEASE | REQUEST)) ||
		((hh->prim == (PH_CONTROL | REQUEST) && (hh->dinfo == HW_DEACTIVATE)))) {
		   	
		spin_lock_irqsave(&hw->rlock, flags);
		if (test_and_clear_bit(FLG_TX_NEXT, &bch->Flags)) {
			dev_kfree_skb(bch->next_skb);
			bch->next_skb = NULL;
		}
		if (bch->tx_skb) {
			dev_kfree_skb(bch->tx_skb);
			bch->tx_skb = NULL;
		}
		bch->tx_idx = 0;
		if (bch->rx_skb) {
			dev_kfree_skb(bch->rx_skb);
			bch->rx_skb = NULL;
		}
		test_and_clear_bit(FLG_L2DATA, &bch->Flags);
		test_and_clear_bit(FLG_TX_BUSY, &bch->Flags);
		setup_channel(hw, bch->channel, ISDN_PID_NONE);
		test_and_clear_bit(FLG_ACTIVE, &bch->Flags);
		spin_unlock_irqrestore(&hw->rlock, flags);
		skb_trim(skb, 0);
		if (hh->prim != (PH_CONTROL | REQUEST)) {
#ifdef FIXME
			if (bch->inst.pid.protocol[2] == ISDN_PID_L2_B_RAWDEV)
				if (bch->dev)
					if_link(&bch->dev->rport.pif,
						hh->prim | CONFIRM, 0, 0, NULL, 0);
#endif
			if (!mISDN_queueup_newhead(&bch->inst, 0, hh->prim | CONFIRM, 0, skb))
				return(0);
		}
	} else if (hh->prim == (PH_CONTROL | REQUEST)) {
		// do not handle PH_CONTROL | REQUEST ??
	} else {
		printk(KERN_WARNING "%s %s: unknown prim(%x)\n",
			hw->card_name, __FUNCTION__, hh->prim);
		ret = -EAGAIN;
	}
	if (!ret)
		dev_kfree_skb(skb);
	return (ret);
}

/******************************/
/* Layer2 -> Layer 1 Transfer */
/******************************/
static int
hfcsmini_l2l1(mISDNinstance_t *inst, struct sk_buff *skb)
{
	channel_t	*chan = container_of(inst, channel_t, inst);
	hfcsmini_hw	*hw = chan->hw;
	int		ret = 0;
	mISDN_head_t	*hh = mISDN_HEAD_P(skb);
	u_long		flags;

	if ((hh->prim == PH_DATA_REQ) || (hh->prim == DL_DATA_REQ)) {
		spin_lock_irqsave(inst->hwlock, flags);
		ret = channel_senddata(chan, hh->dinfo, skb);
		if (ret > 0) { /* direct TX */
			tasklet_schedule(&hw->tasklet);
			ret = 0;
		}
		spin_unlock_irqrestore(inst->hwlock, flags);
		return(ret);
	} 
	if (test_bit(FLG_DCHANNEL, &chan->Flags)) {
		ret = handle_dmsg(chan, skb);
		if (ret != -EAGAIN)
			return(ret);
		ret = -EINVAL;
	}
	if (test_bit(FLG_BCHANNEL, &chan->Flags)) {
		ret = handle_bmsg(chan, skb);
		if (ret != -EAGAIN)
			return(ret);
		ret = -EINVAL;
	}
	if (!ret)
		dev_kfree_skb(skb);
	return(ret);
}


static int
hfcsmini_manager(void *data, u_int prim, void *arg)
{
	hfcsmini_hw *hw = NULL;
	mISDNinstance_t *inst = data;
	struct sk_buff *skb;
	int channel = -1;
	int i;
	channel_t *chan = NULL;
	u_long flags;

	if (!data) {
		MGR_HASPROTOCOL_HANDLER(prim, arg, &hw_mISDNObj)
		    printk(KERN_ERR "%s %s: no data prim %x arg %p\n",
			   hw->card_name, __FUNCTION__, prim, arg);
		return (-EINVAL);
	}
	
	spin_lock_irqsave(&hw_mISDNObj.lock, flags);

	/* find channel and card */
	list_for_each_entry(hw, &hw_mISDNObj.ilist, list) {
		i = 0;
		while (i < MAX_CHAN) {
			if (hw->chan[i].Flags &&
				&hw->chan[i].inst == inst) {
				channel = i;
				chan = &hw->chan[i];
				break;
			}
			i++;
		}
		if (channel >= 0)
			break;
	}
	spin_unlock_irqrestore(&hw_mISDNObj.lock, flags);
	
	if (channel < 0) {
		printk(KERN_ERR
		       "%s: no card/channel found  data %p prim %x arg %p\n",
		       __FUNCTION__, data, prim, arg);
		return (-EINVAL);
	}

	switch (prim) {
		case MGR_REGLAYER | CONFIRM:
			mISDN_setpara(chan, &inst->st->para);
			break;
		case MGR_UNREGLAYER | REQUEST:
			if ((skb = create_link_skb(PH_CONTROL | REQUEST,
				HW_DEACTIVATE, 0, NULL, 0))) {
				if (hfcsmini_l2l1(inst, skb))
					dev_kfree_skb(skb);
			} else
				printk(KERN_WARNING "no SKB in %s MGR_UNREGLAYER | REQUEST\n", __FUNCTION__);
			mISDN_ctrl(inst, MGR_UNREGLAYER | REQUEST, NULL);
			break;
		case MGR_CLRSTPARA | INDICATION:
			arg = NULL;
		case MGR_ADDSTPARA | INDICATION:
			mISDN_setpara(chan, arg);
			break;
		case MGR_RELEASE | INDICATION:
			if (channel == 2) {
				release_card(hw);
			} else {
				hw_mISDNObj.refcnt--;
			}
			break;
		case MGR_SETSTACK | INDICATION:
			if ((channel != 2) && (inst->pid.global == 2)) {
				if ((skb = create_link_skb(PH_ACTIVATE | REQUEST,
					0, 0, NULL, 0))) {
					if (hfcsmini_l2l1(inst, skb))
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
				// FIXME: detect cards with HEADSET
				u_int *gopt = arg;
				*gopt = GLOBALOPT_INTERNAL_CTRL |
				    GLOBALOPT_EXTERNAL_EQUIPMENT |
				    GLOBALOPT_HANDSET;
			} else
				return (-EINVAL);
			break;
		case MGR_SELCHANNEL | REQUEST:
			// no special procedure
			return (-EINVAL);
			PRIM_NOT_HANDLED(MGR_CTRLREADY | INDICATION);
		default:
			printk(KERN_WARNING "%s %s: prim %x not handled\n",
			       hw->card_name, __FUNCTION__, prim);
			return (-EINVAL);
	}
	return (0);
}


/***********************************/
/* check if new buffer for channel */
/* is waitinng is transmitt queue  */
/***********************************/
int
next_tx_frame(hfcsmini_hw * hw, __u8 channel)
{
	channel_t *ch = &hw->chan[channel];

	if (ch->tx_skb)
		dev_kfree_skb(ch->tx_skb);
	if (test_and_clear_bit(FLG_TX_NEXT, &ch->Flags)) {
		ch->tx_skb = ch->next_skb;
		if (ch->tx_skb) {
			mISDN_head_t *hh = mISDN_HEAD_P(ch->tx_skb);
			ch->next_skb = NULL;
			test_and_clear_bit(FLG_TX_NEXT, &ch->Flags);
			ch->tx_idx = 0;
			queue_ch_frame(ch, CONFIRM, hh->dinfo, NULL);
			return (1);
		} else {
			printk(KERN_WARNING
			       "%s channel(%i) TX_NEXT without skb\n",
			       hw->card_name, channel);
			test_and_clear_bit(FLG_TX_NEXT, &ch->Flags);
		}
	} else
		ch->tx_skb = NULL;
	test_and_clear_bit(FLG_TX_BUSY, &ch->Flags);
	return (0);
}

static inline void
hfcsmini_waitbusy(hfcsmini_hw *hw)
{
	while (read_hfcsmini(hw, R_STATUS) & M_BUSY);
}


static inline void
hfcsmini_selfifo(hfcsmini_hw *hw, __u8 fifo)
{
	write_hfcsmini(hw, R_FIFO, fifo);
	hfcsmini_waitbusy(hw);
}


static inline void
hfcsmini_inc_f(hfcsmini_hw *hw)
{
	write_hfcsmini(hw, A_INC_RES_FIFO, M_INC_F);
	hfcsmini_waitbusy(hw);
}


static inline void
hfcsmini_resetfifo(hfcsmini_hw *hw)
{
	write_hfcsmini(hw, A_INC_RES_FIFO, M_RES_FIFO);
	hfcsmini_waitbusy(hw);
}


/**************************/
/* fill fifo with TX data */
/**************************/
void
hfcsmini_write_fifo(hfcsmini_hw *hw, __u8 channel)
{
	__u8		fcnt, tcnt, i;
	__u8		free;
	__u8		f1, f2;
	__u8		fstat;
	__u8		*data;
	int		remain;
	channel_t	*ch = &hw->chan[channel];

send_buffer:
	if (!ch->tx_skb)
		return;
	remain = ch->tx_skb->len - ch->tx_idx;
	if (remain <= 0)
		return;
	hfcsmini_selfifo(hw, (channel * 2));
	free = (hw->max_z - (read_hfcsmini_stable(hw, A_USAGE)));
	tcnt = (free >= remain) ? remain : free;

	fstat = read_hfcsmini(hw, R_ST_RD_STA);
	f1 = read_hfcsmini_stable(hw, A_F1);
	f2 = read_hfcsmini(hw, A_F2);
	fcnt = 0x07 - ((f1 - f2) & 0x07);	/* free frame count in tx fifo */

	if (debug & DEBUG_HFC_FIFO) {
		mISDN_debugprint(&ch->inst,
			"%s channel(%i) len(%i) idx(%i) f1(%i) "
			"f2(%i) fcnt(%i) tcnt(%i) free(%i) fstat(%i)",
			__FUNCTION__, channel, ch->tx_skb->len, ch->tx_idx,
			f1, f2, fcnt, tcnt, free, fstat);
	}

	if (free && fcnt && tcnt) {
		data = ch->tx_skb->data + ch->tx_idx;
		ch->tx_idx += tcnt;

		if (debug & DEBUG_HFC_FIFO) {
			printk(KERN_DEBUG "%s channel(%i) writing: ",
				hw->card_name, channel);
		}
		i = tcnt;
		/* write data to Fifo */
		while (i--) {
			if (debug & DEBUG_HFC_FIFO)
				printk("%02x ", *data);
			write_hfcsmini(hw, A_FIFO_DATA, *data++);
		}
		if (debug & DEBUG_HFC_FIFO)
			printk("\n");
			
		if (ch->tx_idx == ch->tx_skb->len) {
			if (test_bit(FLG_HDLC, &ch->Flags)) {
				/* terminate frame */
				hfcsmini_inc_f(hw);
			} else {
				hfcsmini_selfifo(hw, (channel * 2));
			}
			if (debug & DEBUG_HFC_BTRACE)
				mISDN_debugprint(&ch->inst,
					"TX frame channel(%i) completed",
					channel);
			if (next_tx_frame(hw, channel)) {
				if (debug & DEBUG_HFC_BTRACE)
					mISDN_debugprint(&ch->inst,
						"channel(%i) has next_tx_frame",
						channel);
				if ((free - tcnt) > 8) {
					if (debug & DEBUG_HFC_BTRACE)
						mISDN_debugprint(&ch->inst,
							"channel(%i) continue B-TX immediatetly",
							channel);
					goto send_buffer;
				}
			}
		} else {
			/* tx buffer not complete, but fifo filled to maximum */
			hfcsmini_selfifo(hw, (channel * 2));
		}
	}
}


/****************************/
/* read RX data out of fifo */
/****************************/
void
hfcsmini_read_fifo(hfcsmini_hw *hw, __u8 channel)
{
	__u8	f1 = 0, f2 = 0, z1, z2;
	__u8	fstat = 0;
	int	i;
	int	rcnt;		/* read rcnt bytes out of fifo */
	__u8	*data;		/* new data pointer */
	struct sk_buff	*skb;	/* data buffer for upper layer */
	channel_t	*ch = &hw->chan[channel];

receive_buffer:
	hfcsmini_selfifo(hw, (channel * 2) + 1);
	if (test_bit(FLG_HDLC, &ch->Flags)) {
		/* hdlc rcnt */
		f1 = read_hfcsmini_stable(hw, A_F1);
		f2 = read_hfcsmini(hw, A_F2);
		z1 = read_hfcsmini_stable(hw, A_Z1);
		z2 = read_hfcsmini(hw, A_Z2);
		fstat = read_hfcsmini(hw, R_ST_RD_STA);
		rcnt = (z1 - z2) & hw->max_z;
		if (f1 != f2)
			rcnt++;
	} else {
		/* transparent rcnt */
		rcnt = read_hfcsmini_stable(hw, A_USAGE) - 1;
		f1=f2=z1=z2=0;
	}
	if (debug & DEBUG_HFC_FIFO) {
		if (ch->rx_skb)
			i = ch->rx_skb->len;
		else
			i = 0;
		mISDN_debugprint(&ch->inst, "reading %i bytes channel(%i) "
			"irq_cnt(%i) fstat(%i) idx(%i) f1(%i) f2(%i) z1(%i) z2(%i)",
			rcnt, channel, hw->irq_cnt, fstat, i, f1, f2, z1, z2);
	}
	if (rcnt > 0) {
		if (!ch->rx_skb) {
			ch->rx_skb = alloc_stack_skb(ch->maxlen + 3, ch->up_headerlen);
			if (!ch->rx_skb) {
				printk(KERN_DEBUG "%s: No mem for rx_skb\n", __FUNCTION__);
				return;
			}
		}
		data = skb_put(ch->rx_skb, rcnt);
		/* read data from FIFO*/
		while (rcnt--)
			*data++ = read_hfcsmini(hw, A_FIFO_DATA);
	} else
		return;


	if (test_bit(FLG_HDLC, &ch->Flags)) {
		if (f1 != f2) {
			hfcsmini_inc_f(hw);
			/* check minimum frame size */
			if (ch->rx_skb->len < 4) {
				if (debug & DEBUG_HFC_FIFO_ERR)
					mISDN_debugprint(&ch->inst,
						"%s: frame in channel(%i) < minimum size",
						__FUNCTION__, channel);
				goto read_exit;
			}
			/* check crc */
			if (ch->rx_skb->data[ch->rx_skb->len - 1]) {
				if (debug & DEBUG_HFC_FIFO_ERR)
					mISDN_debugprint(&ch->inst,
						"%s: channel(%i) CRC-error",
						__FUNCTION__, channel);
				goto read_exit;	
			}
			/* remove cksum */
			skb_trim(ch->rx_skb, ch->rx_skb->len - 3);

			if (ch->rx_skb->len < MISDN_COPY_SIZE) {
				skb = alloc_stack_skb(ch->rx_skb->len, ch->up_headerlen);
				if (skb) {
					memcpy(skb_put(skb, ch->rx_skb->len),
						ch->rx_skb->data, ch->rx_skb->len);
					skb_trim(ch->rx_skb, 0);
				} else {
					skb = ch->rx_skb;
					ch->rx_skb = NULL;
				}
			} else {
				skb = ch->rx_skb;
				ch->rx_skb = NULL;
			}
			if ((ch->debug) && (debug & DEBUG_HFC_DTRACE)) {
				mISDN_debugprint(&ch->inst,
					"channel(%i) new RX len(%i): ",
					channel, skb->len);
				i = 0;
				printk("  ");
				while (i < skb->len)
					printk("%02x ", skb->data[i++]);
				printk("\n");
			}
			queue_ch_frame(ch, INDICATION, MISDN_ID_ANY, skb);
read_exit:
			if (ch->rx_skb)
				skb_trim(ch->rx_skb, 0);
			if (read_hfcsmini_stable(hw, A_USAGE) > 8) {
				if (debug & DEBUG_HFC_FIFO)
					mISDN_debugprint(&ch->inst,
						"%s: channel(%i) continue hfcsmini_read_fifo",
						__FUNCTION__, channel);
				goto receive_buffer;
			}
			return;
		} else {
			hfcsmini_selfifo(hw, (channel * 2) + 1);
		}
	} else { /* transparent data */
		hfcsmini_selfifo(hw, (channel * 2) + 1);
		if (ch->rx_skb->len >= 128) {
			/* deliver transparent data to layer2 */
			queue_ch_frame(ch, INDICATION, MISDN_ID_ANY, ch->rx_skb);
			ch->rx_skb = NULL;
		}
	}
}


/*************************************/
/* bottom half handler for interrupt */
/*************************************/
static void
hfcsmini_bh_handler(unsigned long ul_hw)
{
	hfcsmini_hw *hw = (hfcsmini_hw *) ul_hw;
	reg_r_st_rd_sta state;
	int i;

	/* Timer Int */	
	if (hw->misc_irq.bit.v_ti_irq) {
		hw->misc_irq.bit.v_ti_irq = 0;
		/* add Fifo-Fill info into int_s1 bitfield */
		hw->fifo_irq.reg |= ((read_hfcsmini(hw, R_FILL) ^ FIFO_MASK_TX) & hw->fifomask);
		/* Handle TX Fifos */
		for (i = 0; i < hw->max_fifo; i++) {
			if ((1 << (i * 2)) & (hw->fifo_irq.reg)) {
				hw->fifo_irq.reg &= ~(1 << (i * 2));
				if (test_bit(FLG_TX_BUSY, &hw->chan[i].Flags))
					hfcsmini_write_fifo(hw, i);
			}
		}
		/* handle NT Timer */
		if ((hw->portmode & PORT_MODE_NT) && (hw->portmode & NT_TIMER))
			if ((--hw->nt_timer) < 0)
				s0_new_state(&hw->chan[2]);
	}
	/* Handle RX Fifos */
	for (i = 0; i < hw->max_fifo; i++) {
		if ((1 << (i * 2 + 1)) & (hw->fifo_irq.reg)) {
			hw->fifo_irq.reg &= ~(1 << (i * 2 + 1));
			hfcsmini_read_fifo(hw, i);
		}
	}
	/* state machine IRQ */	
	if (hw->misc_irq.bit.v_st_irq) {
		hw->misc_irq.bit.v_st_irq = 0;
		state.reg = read_hfcsmini(hw, R_ST_RD_STA);
		/*
		mISDN_debugprint(&dch->inst,
			"new_l1_state(0x%02x)", state.bit.v_st_sta);
		*/
		if (state.bit.v_st_sta != hw->chan[2].state) {
			hw->chan[2].state = state.bit.v_st_sta;
			s0_new_state(&hw->chan[2]);
		}		
	}
	return;
}


/*********************/
/* Interrupt handler */
/*********************/
static irqreturn_t
hfcsmini_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	__u8 fifo_irq, misc_irq;
	hfcsmini_hw *hw = dev_id;

	spin_lock(&hw->rlock);
	
	if (!(hw->misc_irqmsk.bit.v_irq_en)) {
		if (!(hw->testirq))
			printk(KERN_INFO
			       "%s %s GLOBAL INTERRUPT DISABLED\n",
			       hw->card_name, __FUNCTION__);		
		spin_unlock(&hw->rlock);
		return IRQ_NONE;
	} 

	fifo_irq = read_hfcsmini_irq(hw, R_FIFO_IRQ) & hw->fifo_irqmsk.reg;
	misc_irq = read_hfcsmini_irq(hw, R_MISC_IRQ) & hw->misc_irqmsk.reg;
	
	if (!fifo_irq && !misc_irq) {
		spin_unlock(&hw->rlock);
		return IRQ_NONE; /* other hardware interrupted */
	}
	
	hw->irq_cnt++;
	
	hw->fifo_irq.reg |= fifo_irq;
	hw->misc_irq.reg |= misc_irq;
	
	/* queue bottom half */
	if (!(hw->testirq)) {
		tasklet_schedule(&hw->tasklet);
	}
	
	spin_unlock(&hw->rlock);
	
	return IRQ_HANDLED;
}


/*************************************/
/* free memory for all used channels */
/*************************************/
void
release_channels(hfcsmini_hw * hw)
{
	int i = 0;

	while (i < MAX_CHAN) {
		if (hw->chan[i].Flags) {
			if (debug & DEBUG_HFC_INIT)
				printk(KERN_DEBUG "%s %s: free channel %d\n",
					hw->card_name, __FUNCTION__, i);
			mISDN_freechannel(&hw->chan[i]);
			mISDN_ctrl(&hw->chan[i].inst, MGR_UNREGLAYER | REQUEST, NULL);
		}
		i++;
	}
}


/******************************************/
/* Setup Fifo using HDLC_PAR and CON_HDLC */
/******************************************/
void setup_fifo(hfcsmini_hw * hw, int fifo, __u8 hdlcreg, __u8 con_reg, __u8 irq_enable, __u8 enable)
{       
	
	if (enable)
		/* mark fifo to be 'in use' */
		hw->fifomask |= (1 << fifo);
	else
		hw->fifomask &= ~(1 << fifo);

	if (irq_enable)
		hw->fifo_irqmsk.reg |= (1 << fifo);
	else
		hw->fifo_irqmsk.reg &= ~(1 << fifo);
		
	write_hfcsmini(hw, R_FIFO_IRQMSK, hw->fifo_irqmsk.reg);
	
	hfcsmini_selfifo(hw, fifo);
	write_hfcsmini(hw, A_HDLC_PAR, hdlcreg); 
	write_hfcsmini(hw, A_CON_HDLC, con_reg); 
	hfcsmini_resetfifo(hw);
}


/*************************************************/
/* Setup ST interface, enable/disable B-Channels */
/*************************************************/
void
setup_st(hfcsmini_hw * hw, __u8 bc, __u8 enable)
{
	if (!((bc == 0) || (bc == 1))) {
		printk(KERN_INFO "%s %s: ERROR: bc(%i) unvalid!\n",
		       hw->card_name, __FUNCTION__, bc);
		return;
	}

	if (bc) {
		hw->st_ctrl0.bit.v_b2_en = (enable?1:0);
		hw->st_ctrl2.bit.v_b2_rx_en = (enable?1:0);
	} else {
		hw->st_ctrl0.bit.v_b1_en = (enable?1:0);
		hw->st_ctrl2.bit.v_b1_rx_en = (enable?1:0);
	}

	write_hfcsmini(hw, R_ST_CTRL0, hw->st_ctrl0.reg);
	write_hfcsmini(hw, R_ST_CTRL2, hw->st_ctrl2.reg);
	
	if (debug & DEBUG_HFC_MODE) {
		printk(KERN_INFO
		       "%s %s: bc(%i) %s, R_ST_CTRL0(0x%02x) R_ST_CTRL2(0x%02x)\n",
		       hw->card_name, __FUNCTION__, bc, enable?"enable":"disable",
		       hw->st_ctrl0.reg, hw->st_ctrl2.reg);
	}
}


/*********************************************/
/* (dis-) connect D/B-Channel using protocol */
/*********************************************/
int
setup_channel(hfcsmini_hw *hw, __u8 channel, int protocol)
{
	if (test_bit(FLG_BCHANNEL, &hw->chan[channel].Flags)) {
		if (debug & DEBUG_HFC_MODE)
			mISDN_debugprint(&hw->chan[channel].inst,
				"channel(%i) protocol %x-->%x",
				channel, hw->chan[channel].state, protocol);
		switch (protocol) {
			case (-1):	/* used for init */
				hw->chan[channel].state = -1;
				hw->chan[channel].channel = channel;
				/* fall trough */
			case (ISDN_PID_NONE):
				if (debug & DEBUG_HFC_MODE)
					mISDN_debugprint(&hw->chan[channel].inst,
						"ISDN_PID_NONE");
				if (hw->chan[channel].state == ISDN_PID_NONE)
					return (0);	/* already in idle state */
				hw->chan[channel].state = ISDN_PID_NONE;
				/* B-TX */
				setup_fifo(hw, (channel << 1), 0, 0,
					FIFO_IRQ_OFF, FIFO_DISABLE); 
				/* B-RX */                   
				setup_fifo(hw, (channel << 1) + 1, 0, 0,
					FIFO_IRQ_OFF, FIFO_DISABLE);
				setup_st(hw, channel, 0);
				test_and_clear_bit(FLG_HDLC, &hw->chan[channel].Flags);
				test_and_clear_bit(FLG_TRANSPARENT, &hw->chan[channel].Flags);
				break;
			case (ISDN_PID_L1_B_64TRANS):
				if (debug & DEBUG_HFC_MODE)
					mISDN_debugprint(&hw->chan[channel].inst,
						"ISDN_PID_L1_B_64TRANS");
				/* B-TX */
				setup_fifo(hw, (channel << 1), HDLC_PAR_BCH,
					CON_HDLC_B_TRANS, FIFO_IRQ_OFF,
					FIFO_ENABLE); 
				/* B-RX */
				setup_fifo(hw, (channel << 1) + 1, HDLC_PAR_BCH,
					CON_HDLC_B_TRANS, FIFO_IRQ_OFF,
					FIFO_ENABLE);
				setup_st(hw, channel, 1);
				hw->chan[channel].state = ISDN_PID_L1_B_64TRANS;
				test_and_set_bit(FLG_TRANSPARENT, &hw->chan[channel].Flags);
				break;
			case (ISDN_PID_L1_B_64HDLC):
				if (debug & DEBUG_HFC_MODE)
					mISDN_debugprint(&hw->chan[channel].inst,
						"ISDN_PID_L1_B_64HDLC");
				/* B-TX */
				setup_fifo(hw, (channel << 1), HDLC_PAR_BCH,
					CON_HDLC_B_HDLC, FIFO_IRQ_OFF,
					FIFO_ENABLE); 
				/* B-RX */
				setup_fifo(hw, (channel << 1) + 1, HDLC_PAR_BCH,
					CON_HDLC_B_HDLC, FIFO_IRQ_ON,
					FIFO_ENABLE);
				setup_st(hw, channel, 1);
				hw->chan[channel].state = ISDN_PID_L1_B_64HDLC;
				test_and_set_bit(FLG_HDLC, &hw->chan[channel].Flags);
				break;
			default:
				mISDN_debugprint(&hw->chan[channel].inst,
					"prot not known %x", protocol);
				return (-ENOPROTOOPT);
		}
		return (0);
	}

	if (test_bit(FLG_DCHANNEL, &hw->chan[channel].Flags)) {
		if (debug & DEBUG_HFC_MODE)
			mISDN_debugprint(&hw->chan[channel].inst,
				"D channel(%i) protocol(%i)",channel, protocol);
		
		/* init the D-channel fifos */
		/* D-TX */
		setup_fifo(hw, (channel << 1), HDLC_PAR_DCH,
			CON_HDLC_D_HDLC, FIFO_IRQ_OFF, FIFO_ENABLE); 
		/* D-RX */
		setup_fifo(hw, (channel << 1) + 1, HDLC_PAR_DCH,
			CON_HDLC_D_HDLC, FIFO_IRQ_ON, FIFO_DISABLE);
		return (0);
	}
	printk(KERN_INFO "%s %s ERROR: channel(%i) is NEITHER B nor D !!!\n",
		hw->card_name, __FUNCTION__, channel);
	return (-1);
}


/*****************************************************/
/* register ISDN stack for one HFC-S mini instance   */
/*   - register all ports and channels               */
/*   - set param_idx                                 */
/*                                                   */
/*  channel mapping in mISDN in hw->chan[]           */
/*    0=B1,  1=B2,  2=D,  3=PCM                      */
/*****************************************************/
int
init_mISDN_channels(hfcsmini_hw * hw)
{
	int err;
	int ch;
	int b;
	mISDN_pid_t pid;
	u_long flags;

	/* clear PCM */
	memset(&hw->chan[3], 0, sizeof(channel_t));
	/* init D channels */
	ch = 2;
	if (debug & DEBUG_HFC_INIT)
		printk(KERN_INFO
		       "%s %s: Registering D-channel, card(%d) protocol(%x)\n",
		       hw->card_name, __FUNCTION__, hw->cardnum,
		       hw->dpid);

	memset(&hw->chan[ch], 0, sizeof(channel_t));
	hw->chan[ch].channel = ch;
	hw->chan[ch].debug = debug;
	hw->chan[ch].inst.obj = &hw_mISDNObj;
	hw->chan[ch].inst.hwlock = &hw->mlock;
	hw->chan[ch].inst.class_dev.dev = &hw->pdev->dev;
	mISDN_init_instance(&hw->chan[ch].inst, &hw_mISDNObj, hw, hfcsmini_l2l1);
	
	hw->chan[ch].inst.pid.layermask = ISDN_LAYER(0);
	sprintf(hw->chan[ch].inst.name, "%s", hw->card_name);
	err = mISDN_initchannel(&hw->chan[ch], MSK_INIT_DCHANNEL, MAX_DFRAME_LEN_L1);
	if (err)
		goto free_channels;
	hw->chan[ch].hw = hw;

	/* init B channels */
	for (b = 0; b < 2; b++) {
		if (debug & DEBUG_HFC_INIT)
			printk(KERN_DEBUG
			       "%s %s: Registering B-channel, card(%d) "
			       "ch(%d)\n", hw->card_name,
			       __FUNCTION__, hw->cardnum, b);

		memset(&hw->chan[b], 0, sizeof(channel_t));
		hw->chan[b].channel = b;
		hw->chan[b].debug = debug;
		mISDN_init_instance(&hw->chan[b].inst, &hw_mISDNObj, hw, hfcsmini_l2l1);
		hw->chan[b].inst.pid.layermask = ISDN_LAYER(0);
		hw->chan[b].inst.hwlock = &hw->mlock;
		hw->chan[b].inst.class_dev.dev = &hw->pdev->dev;
		
		sprintf(hw->chan[b].inst.name, "%s B%d",
			hw->chan[ch].inst.name, b + 1);
		if (mISDN_initchannel(&hw->chan[b], MSK_INIT_BCHANNEL, MAX_DATA_MEM)) {
			err = -ENOMEM;
			goto free_channels;
		}
		hw->chan[b].hw = hw;
	}

	mISDN_set_dchannel_pid(&pid, hw->dpid, layermask[hw->param_idx]);

	/* set protocol for NT/TE */
	if (hw->portmode & PORT_MODE_NT) {
		/* NT-mode */
		hw->portmode |= NT_TIMER;
		hw->nt_timer = 0;

		hw->chan[ch].inst.pid.protocol[0] = ISDN_PID_L0_NT_S0;
		hw->chan[ch].inst.pid.protocol[1] = ISDN_PID_L1_NT_S0;
		pid.protocol[0] = ISDN_PID_L0_NT_S0;
		pid.protocol[1] = ISDN_PID_L1_NT_S0;
		hw->chan[ch].inst.pid.layermask |= ISDN_LAYER(1);
		pid.layermask |= ISDN_LAYER(1);
		if (layermask[hw->param_idx] & ISDN_LAYER(2))
			pid.protocol[2] = ISDN_PID_L2_LAPD_NET;
	} else {
		/* TE-mode */
		hw->portmode |= PORT_MODE_TE;
		hw->chan[ch].inst.pid.protocol[0] = ISDN_PID_L0_TE_S0;
		pid.protocol[0] = ISDN_PID_L0_TE_S0;
	}

	if (debug & DEBUG_HFC_INIT)
		printk(KERN_INFO
		       "%s %s: registering Stack\n",
		       hw->card_name, __FUNCTION__);

	/* register stack */
	err = mISDN_ctrl(NULL, MGR_NEWSTACK | REQUEST, &hw->chan[ch].inst);
	if (err) {
		printk(KERN_ERR "%s %s: MGR_NEWSTACK | REQUEST  err(%d)\n",
		       hw->card_name, __FUNCTION__, err);
		goto free_channels;
	}

	hw->chan[ch].state = 0;
	for (b = 0; b < 2; b++) {
		err = mISDN_ctrl(hw->chan[ch].inst.st, MGR_NEWSTACK | REQUEST, &hw->chan[b].inst);
		if (err) {
			printk(KERN_ERR
			       "%s %s: MGR_ADDSTACK bchan error %d\n",
			       hw->card_name, __FUNCTION__, err);
			goto free_stack;
		}
	}

	err = mISDN_ctrl(hw->chan[ch].inst.st, MGR_SETSTACK | REQUEST, &pid);

	if (err) {
		printk(KERN_ERR
		       "%s %s: MGR_SETSTACK REQUEST dch err(%d)\n",
		       hw->card_name, __FUNCTION__, err);
		mISDN_ctrl(hw->chan[ch].inst.st, MGR_DELSTACK | REQUEST, NULL);
		goto free_stack;
	}

	setup_channel(hw, hw->chan[ch].channel, -1);
	for (b = 0; b < 2; b++) {
		setup_channel(hw, b, -1);
	}

	/* delay some time */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((100 * HZ) / 1000);	/* Timeout 100ms */

	mISDN_ctrl(hw->chan[ch].inst.st, MGR_CTRLREADY | INDICATION, NULL);
	return (0);

      free_stack:
	mISDN_ctrl(hw->chan[ch].inst.st, MGR_DELSTACK | REQUEST, NULL);
      free_channels:
      	spin_lock_irqsave(&hw_mISDNObj.lock, flags);
	release_channels(hw);
	list_del(&hw->list);
	spin_unlock_irqrestore(&hw_mISDNObj.lock, flags);

	return (err);
}


/********************************/
/* parse module paramaters like */
/* NE/TE and S0/Up port mode    */
/********************************/
void
parse_module_params(hfcsmini_hw * hw)
{

	/* D-Channel protocol: (2=DSS1) */
	hw->dpid = (protocol[hw->param_idx] & 0x0F);
	if (hw->dpid == 0) {
		printk(KERN_INFO
		       "%s %s: WARNING: wrong value for protocol[%i], "
		       "assuming 0x02 (DSS1)...\n",
		       hw->card_name, __FUNCTION__,
		       hw->param_idx);
		hw->dpid = 0x02;
	}

	/* Line Interface TE or NT */
	if (protocol[hw->param_idx] & 0x10)
		hw->portmode |= PORT_MODE_NT;
	else
		hw->portmode |= PORT_MODE_TE;

	/* Line Interface in S0 or Up mode */
	if (!(protocol[hw->param_idx] & 0x40))
		hw->portmode |= PORT_MODE_BUS_MASTER;

		
	/* link B-channel loop */
	if (protocol[hw->param_idx] & 0x80)
		hw->portmode |= PORT_MODE_LOOP;
	

	if (debug & DEBUG_HFC_INIT)
		printk ("%s %s: protocol[%i]=0x%02x, dpid=%d,%s bus-mode:%s %s\n",
		        hw->card_name, __FUNCTION__, hw->param_idx,
		        protocol[hw->param_idx],
		        hw->dpid,
		        (hw->portmode & PORT_MODE_TE)?"TE":"NT",
		        (hw->portmode & PORT_MODE_BUS_MASTER)?"MASTER":"SLAVE",
		        (hw->portmode & PORT_MODE_LOOP)?"B-LOOP":""
		        );
}


/*****************************************/
/* initialise the HFC-S mini ISDN Chip   */
/* return 0 on success.                  */
/*****************************************/
int
init_hfcsmini(hfcsmini_hw * hw)
{
	int err = 0;
	reg_r_fifo_thres threshold;

#if HFCBRIDGE == BRIDGE_HFCPCI
	err = init_pci_bridge(hw);
	if (err)
		return(-ENODEV);
#endif

	hw->chip_id.reg = read_hfcsmini(hw, R_CHIP_ID);

	if (debug & DEBUG_HFC_INIT)
		printk(KERN_INFO "%s %s ChipID: 0x%x\n", hw->card_name,
		       __FUNCTION__, hw->chip_id.bit.v_chip_id);

	switch (hw->chip_id.bit.v_chip_id) {
		case CHIP_ID_HFCSMINI:
			hw->max_fifo = 4;
			hw->ti.reg   = 5; /* 8 ms timer interval */
			hw->max_z    = 0x7F;
			break;
		default:
			err = -ENODEV;
	}

	if (err) {
		if (debug & DEBUG_HFC_INIT)
			printk(KERN_ERR "%s %s: unkown Chip ID 0x%x\n",
			       hw->card_name, __FUNCTION__, hw->chip_id.bit.v_chip_id);
		return (err);
	}
	
	/* reset card */
	write_hfcsmini(hw, R_CIRM, M_SRES); /* Reset On */
	udelay(10);
	write_hfcsmini(hw, R_CIRM, 0); /* Reset Off */
	
	/* wait until fifo controller init sequence is finished */
	hfcsmini_waitbusy(hw);

	/* reset D-Channel S/T controller */
	write_hfcsmini(hw, R_ST_CTRL1, M_D_RES);

	if (hw->portmode & PORT_MODE_TE) {
		/* TE mode */
		hw->st_ctrl0.reg = 0;
		write_hfcsmini(hw, R_ST_CLK_DLY, (M1_ST_CLK_DLY* 0xF));
		write_hfcsmini(hw, R_ST_CTRL1, 0);
	} else {
		/* NT mode */
		hw->st_ctrl0.reg = 4;
		write_hfcsmini(hw, R_ST_CLK_DLY, ((M1_ST_SMPL * 0x6) | (M1_ST_CLK_DLY*0xC)));
		write_hfcsmini(hw, R_ST_CTRL1, M_E_IGNO);
	}
	
	hw->st_ctrl2.reg = 0;
	write_hfcsmini(hw, R_ST_CTRL0, hw->st_ctrl0.reg);
	write_hfcsmini(hw, R_ST_CTRL2, hw->st_ctrl2.reg);

	/* HFC Master/Slave Mode */
	if (hw->portmode & PORT_MODE_BUS_MASTER)
		hw->pcm_md0.bit.v_pcm_md = 1;
	else
		hw->pcm_md0.bit.v_pcm_md = 0;

	write_hfcsmini(hw, R_PCM_MD0, hw->pcm_md0.reg);
	write_hfcsmini(hw, R_PCM_MD1, 0);
	write_hfcsmini(hw, R_PCM_MD2, 0);

	/* setup threshold register */
	threshold.bit.v_thres_tx = (HFCSMINI_TX_THRESHOLD / 8);
	threshold.bit.v_thres_rx = (HFCSMINI_RX_THRESHOLD / 8);
	write_hfcsmini(hw, R_FIFO_THRES, threshold.reg);

	/* test timer irq */
	enable_interrupts(hw);
	mdelay(((1 << hw->ti.reg)+1)*2);
	hw->testirq = 0;
	
	if (hw->irq_cnt) {
		printk(KERN_INFO
		       "%s %s: test IRQ OK, irq_cnt %i\n",
		       hw->card_name, __FUNCTION__, hw->irq_cnt);
		disable_interrupts(hw);
		return (0);
	} else {
		if (debug & DEBUG_HFC_INIT)
			printk(KERN_INFO
			       "%s %s: ERROR getting IRQ (irq_cnt %i)\n",
			       hw->card_name, __FUNCTION__, hw->irq_cnt);
		disable_interrupts(hw);
		free_irq(hw->irq, hw);
		return (-EIO);
	}
}


/*****************************************************/
/* disable all interrupts by disabling M_GLOB_IRQ_EN */
/*****************************************************/
void
disable_interrupts(hfcsmini_hw * hw)
{
	u_long flags;
	if (debug & DEBUG_HFC_IRQ)
		printk(KERN_INFO "%s %s\n", hw->card_name, __FUNCTION__);

	spin_lock_irqsave(&hw->mlock, flags);
	hw->fifo_irqmsk.reg = 0;
	hw->misc_irqmsk.reg = 0;	
	write_hfcsmini(hw, R_FIFO_IRQMSK, hw->fifo_irqmsk.reg);
	write_hfcsmini(hw, R_MISC_IRQMSK, hw->misc_irqmsk.reg);
	spin_unlock_irqrestore(&hw->mlock, flags);
}


/******************************************/
/* start interrupt and set interrupt mask */
/******************************************/
void
enable_interrupts(hfcsmini_hw * hw)
{
	u_long flags;
	
	if (debug & DEBUG_HFC_IRQ)
		printk(KERN_INFO "%s %s\n", hw->card_name, __FUNCTION__);

	spin_lock_irqsave(&hw->mlock, flags);
	
	hw->fifo_irq.reg = 0;
	hw->misc_irq.reg = 0;
	
	write_hfcsmini(hw, R_TI, hw->ti.reg); 

	/* D-RX and D-TX interrupts enable */
	hw->fifo_irqmsk.bit.v_fifo2_tx_irqmsk = 1;
	hw->fifo_irqmsk.bit.v_fifo2_rx_irqmsk = 1;	

	/* clear pending ints */
	if (read_hfcsmini(hw, R_FIFO_IRQ));
	if (read_hfcsmini(hw, R_MISC_IRQ));
	
	/* Finally enable IRQ output */
	hw->misc_irqmsk.bit.v_st_irqmsk = 1; /* enable L1-state change irq */
	hw->misc_irqmsk.bit.v_ti_irqmsk = 1; /* enable timer irq */
	hw->misc_irqmsk.bit.v_irq_en    = 1; /* IRQ global enable */
	
	write_hfcsmini(hw, R_MISC_IRQMSK, hw->misc_irqmsk.reg);
	
	spin_unlock_irqrestore(&hw->mlock, flags);
	
	return;
}


/**************************************/
/* initialise the HFC-S mini hardware */
/* return 0 on success.               */
/**************************************/
static int __devinit
setup_instance(hfcsmini_hw * hw)
{
	int		err;
	hfcsmini_hw *previous_hw;
	u_long		flags;
	

	if (debug & DEBUG_HFC_INIT)
		printk(KERN_WARNING "%s %s\n",
		       hw->card_name, __FUNCTION__);

	spin_lock_init(&hw->mlock);
	spin_lock_init(&hw->rlock);
	tasklet_init(&hw->tasklet, hfcsmini_bh_handler, (unsigned long) hw);
	
	/* search previous instances to index protocol[] array */
	list_for_each_entry(previous_hw, &hw_mISDNObj.ilist, list)
		hw->param_idx++;

	/* add this instance to hardware list */
	spin_lock_irqsave(&hw_mISDNObj.lock, flags);
	list_add_tail(&hw->list, &hw_mISDNObj.ilist);
	spin_unlock_irqrestore(&hw_mISDNObj.lock, flags);

	/* init interrupt engine */
	hw->testirq = 1;
	if (debug & DEBUG_HFC_INIT)
		printk(KERN_WARNING "%s %s: requesting IRQ %d\n",
		       hw->card_name, __FUNCTION__, hw->irq);
		       
	if (request_irq(hw->irq, hfcsmini_interrupt, SA_SHIRQ, "HFC-S mini", hw)) {
		printk(KERN_WARNING "%s %s: couldn't get interrupt %d\n",
		       hw->card_name, __FUNCTION__, hw->irq);
		       
		hw->irq = 0;
		err = -EIO;
		goto out;
	}

	parse_module_params(hw);
	
	err = init_hfcsmini(hw);
	if (err)
		goto out;

	/* register all channels at ISDN procol stack */
	err = init_mISDN_channels(hw);
	if (err)
		goto out;
		
	/* delay some time to have mISDN initialazed complete */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((100 * HZ) / 1000);	/* Timeout 100ms */
	
	/* Clear already pending ints */
	if (read_hfcsmini(hw, R_FIFO_IRQ));
	
	enable_interrupts(hw);
	
	/* enable state machine */
	write_hfcsmini(hw, R_ST_RD_STA, 0x0);
	
	return(0);

      out:
	return (err);
}


#if HFCBRIDGE == BRIDGE_HFCPCI

/***********************/
/* PCI Bridge ID List  */
/***********************/
static struct pci_device_id hfcsmini_ids[] = {
	{.vendor = PCI_VENDOR_ID_CCD,
	 .device = 0xA001,
	 .subvendor = PCI_VENDOR_ID_CCD,
	 .subdevice = 0xFFFF,
	 .driver_data =
	 (unsigned long) &((hfcsmini_param) {0xFF, "HFC-S mini Evaluation Board"}),
	 },
	{}
};

/******************************/
/* initialise the PCI Bridge  */
/* return 0 on success.       */
/******************************/
int
init_pci_bridge(hfcsmini_hw * hw)
{
	outb(0x58, hw->iobase + 4); /* ID-register of bridge */
	if ((inb(hw->iobase) & 0xf0) != 0x30) {
		printk(KERN_INFO "%s %s: chip ID for PCI bridge invalid\n",
		       hw->card_name, __FUNCTION__);
		release_region(hw->iobase, 8);
		return(-EIO);
	}

	outb(0x60, hw->iobase + 4); /* CIRM register of bridge */
	outb(0x07, hw->iobase); /* 15 PCI clocks aux access */

	/* reset sequence */
	outb(2, hw->iobase + 3); /* A0 = 1, reset = 0 (active) */
	udelay(10);
	outb(6, hw->iobase + 3); /* A0 = 1, reset = 1 (inactive) */
	outb(0, hw->iobase + 1); /* write dummy register number */

	/* wait until reset sequence finished, can be redefined after schematic review */
	mdelay(300);

	return (0);
}

/************************/
/* release single card  */
/************************/
static void
release_card(hfcsmini_hw * hw)
{
	u_long	flags;

	disable_interrupts(hw);
	free_irq(hw->irq, hw);

	/* wait for pending tasklet to finish */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((100 * HZ) / 1000);	/* Timeout 100ms */
	
	spin_lock_irqsave(&hw_mISDNObj.lock, flags);
	release_channels(hw);
	list_del(&hw->list);
	spin_unlock_irqrestore(&hw_mISDNObj.lock, flags);
	
	kfree(hw);
}

/*****************************************/
/* PCI hotplug interface: probe new card */
/*****************************************/
static int __devinit
hfcsmini_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	hfcsmini_param *driver_data = (hfcsmini_param *) ent->driver_data;
	hfcsmini_hw *hw;

	int err = -ENOMEM;

	if (!(hw = kmalloc(sizeof(hfcsmini_hw), GFP_ATOMIC))) {
		printk(KERN_ERR "%s %s: No kmem for HFC-S mini card\n",
		       hw->card_name, __FUNCTION__);
		return (err);
	}
	memset(hw, 0, sizeof(hfcsmini_hw));

	hw->pdev = pdev;
	err = pci_enable_device(pdev);

	if (err)
		goto out;

	hw->cardnum = card_cnt;
	sprintf(hw->card_name, "%s_%d", DRIVER_NAME, hw->cardnum);
	printk(KERN_INFO "%s %s: adapter '%s' found on PCI bus %02x dev %02x\n",
	       hw->card_name, __FUNCTION__, driver_data->device_name,
	       pdev->bus->number, pdev->devfn);

	hw->driver_data = *driver_data;
	hw->irq = pdev->irq;
	
 	hw->iobase = (u_int) get_pcibase(pdev, 0);
	if (!hw->iobase) {
		printk(KERN_WARNING "%s no IO for PCI card found\n",
		       hw->card_name);
		return(-EIO);
	}
	
	if (!request_region(hw->iobase, 8, "hfcmulti")) {
		printk(KERN_WARNING "%s failed to request "
		                    "address space at 0x%04x\n",
		                    hw->card_name,
		                    hw->iobase);
	}
	
        printk(KERN_INFO "%s defined at IOBASE 0x%#x IRQ %d HZ %d\n",
	       hw->card_name, 
	       (u_int) hw->iobase,
	       hw->irq,
	       HZ);

	/* enable IO */
	pci_write_config_word(pdev, PCI_COMMAND, 0x01);
	
	pci_set_drvdata(pdev, hw);
	err = setup_instance(hw);
	
	if (!err) {
		card_cnt++;
		return (0);
	} else {
		goto out;
	}

      out:
	kfree(hw);
	return (err);
};


/**************************************/
/* PCI hotplug interface: remove card */
/**************************************/
static void __devexit
hfcsmini_pci_remove(struct pci_dev *pdev)
{
	hfcsmini_hw *hw = pci_get_drvdata(pdev);
	printk(KERN_INFO "%s %s: removing card\n", hw->card_name,
	       __FUNCTION__);
	release_card(hw);
	card_cnt--;
	pci_disable_device(pdev);
	return;
};


/*****************************/
/* Module PCI driver exports */
/*****************************/
static struct pci_driver hfcsmini_driver = {
	name:DRIVER_NAME,
	probe:hfcsmini_pci_probe,
	remove:__devexit_p(hfcsmini_pci_remove),
	id_table:hfcsmini_ids,
};

MODULE_DEVICE_TABLE(pci, hfcsmini_ids);

#endif

/***************/
/* Module init */
/***************/
static int __init
hfcsmini_init(void)
{
	int err;

	printk(KERN_INFO "HFC-S mini: %s driver Rev. %s (debug=%i)\n",
	       __FUNCTION__, mISDN_getrev(hfcsmini_rev), debug);

#ifdef MODULE
	hw_mISDNObj.owner = THIS_MODULE;
#endif

	INIT_LIST_HEAD(&hw_mISDNObj.ilist);
	spin_lock_init(&hw_mISDNObj.lock);
	hw_mISDNObj.name = DRIVER_NAME;
	hw_mISDNObj.own_ctrl = hfcsmini_manager;

	hw_mISDNObj.DPROTO.protocol[0] = ISDN_PID_L0_TE_S0 |
	    ISDN_PID_L0_NT_S0;
	hw_mISDNObj.DPROTO.protocol[1] = ISDN_PID_L1_NT_S0;
	hw_mISDNObj.BPROTO.protocol[1] = ISDN_PID_L1_B_64TRANS |
	    ISDN_PID_L1_B_64HDLC;
	hw_mISDNObj.BPROTO.protocol[2] = ISDN_PID_L2_B_TRANS |
	    ISDN_PID_L2_B_RAWDEV;

	card_cnt = 0;

	if ((err = mISDN_register(&hw_mISDNObj))) {
		printk(KERN_ERR "HFC-S mini: can't register HFC-S mini, error(%d)\n",
		       err);
		goto out;
	}

#if HFCBRIDGE == BRIDGE_HFCPCI
	err = pci_register_driver(&hfcsmini_driver);
	if (err < 0) {
		goto out;
	}
#if !defined(CONFIG_HOTPLUG)
	if (err == 0) {
		err = -ENODEV;
		pci_unregister_driver(&hfcsmini_driver);
		goto out;
	}
#endif
#endif

	printk(KERN_INFO "HFC-S mini: %d cards installed\n", card_cnt);
	return 0;

      out:
	return (err);
}


static void __exit
hfcsmini_cleanup(void)
{
	int err;

#if HFCBRIDGE == BRIDGE_HFCPCI
	pci_unregister_driver(&hfcsmini_driver);
#endif

	if ((err = mISDN_unregister(&hw_mISDNObj))) {
		printk(KERN_ERR "HFC-S mini: can't unregister HFC-S mini, error(%d)\n",
		       err);
	}
	printk(KERN_INFO "%s: driver removed\n", __FUNCTION__);
}

module_init(hfcsmini_init);
module_exit(hfcsmini_cleanup);

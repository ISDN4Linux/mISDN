/* $Id: xhfc_su.c,v 1.2 2006/03/06 12:58:31 keil Exp $
 *
 * mISDN driver for CologneChip AG's XHFC
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
 *        <bit 5>   0x0020  Line Interface Mode (0=S0, 1=Up)
 *        <bit 6>   0x0040  st line polarity (1=exchanged)
 *        <bit 7>   0x0080  B channel loop (for layer1 tests)
 *
 * - layermask=<l1>[,l2,l3...] (32bit):
 *        mask of layers to be used for D-channel stack
 *
 * - debug:
 *        enable debugging (see xhfc_su.h for debug options)
 *
 */
 
#include <linux/mISDNif.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <asm/timex.h>
#include "helper.h"
#include "debug.h"
#include "xhfc_su.h"
#include "xhfc24succ.h"

#if BRIDGE == BRIDGE_PCI2PI
#include "xhfc_pci2pi.h"
#endif

static const char xhfc_rev[] = "$Revision: 1.2 $";

#define MAX_CARDS	8
static int card_cnt;
static u_int protocol[MAX_CARDS * MAX_PORT];
static int layermask[MAX_CARDS * MAX_PORT];

#ifdef MODULE
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
#define MODULE_PARM_T	"1-8i"
MODULE_PARM(debug, "1i");
MODULE_PARM(protocol, MODULE_PARM_T);
MODULE_PARM(layermask, MODULE_PARM_T);
#endif

static mISDNobject_t hw_mISDNObj;
static int debug = 0;

static void release_card(xhfc_hw * hw);



/****************************************************/
/* Physical S/U commands to control Line Interface  */
/****************************************************/
static void
xhfc_ph_command(channel_t * dch, u_char command)
{
	xhfc_hw *hw = dch->hw;
	xhfc_port_t *port = hw->chan[dch->channel].port;

	if (dch->debug)
		mISDN_debugprint(&dch->inst,
				 "%s command(%i) channel(%i) port(%i)",
				 __FUNCTION__, command, dch->channel,
				 port->idx);

	switch (command) {
		case HFC_L1_ACTIVATE_TE:
			if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES)) {
				mISDN_debugprint(&dch->inst,
						 "HFC_L1_ACTIVATE_TE channel(%i))",
						 dch->channel);
			}

			write_xhfc(hw, R_SU_SEL, port->idx);
			write_xhfc(hw, A_SU_WR_STA, STA_ACTIVATE);
			break;
			
		case HFC_L1_FORCE_DEACTIVATE_TE:
			if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
				mISDN_debugprint(&dch->inst,
						 "HFC_L1_FORCE_DEACTIVATE_TE channel(%i)",
						 dch->channel);

			write_xhfc(hw, R_SU_SEL, port->idx);
			write_xhfc(hw, A_SU_WR_STA, STA_DEACTIVATE);
			break;

		case HFC_L1_ACTIVATE_NT:
			if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
				mISDN_debugprint(&dch->inst,
					 "HFC_L1_ACTIVATE_NT channel(%i)",
					 dch->channel);

			write_xhfc(hw, R_SU_SEL, port->idx);
			write_xhfc(hw, A_SU_WR_STA,
				   STA_ACTIVATE | M_SU_SET_G2_G3);
			break;

		case HFC_L1_DEACTIVATE_NT:
			if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
				mISDN_debugprint(&dch->inst,
						 "HFC_L1_DEACTIVATE_NT channel(%i)",
						 dch->channel);

			write_xhfc(hw, R_SU_SEL, port->idx);
			write_xhfc(hw, A_SU_WR_STA, STA_DEACTIVATE);
			break;

		case HFC_L1_TESTLOOP_B1:
			setup_fifo(hw, port->idx*8,   0xC6, 0, 0, 0);	/* connect B1-SU RX with PCM TX */
			setup_fifo(hw, port->idx*8+1, 0xC6, 0, 0, 0);	/* connect B1-SU TX with PCM RX */

			write_xhfc(hw, R_SLOT, port->idx*8);		/* PCM timeslot B1 TX */
			write_xhfc(hw, A_SL_CFG, port->idx*8 + 0x80); 	/* enable B1 TX timeslot on STIO1 */

			write_xhfc(hw, R_SLOT, port->idx*8+1);		/* PCM timeslot B1 RX */
			write_xhfc(hw, A_SL_CFG, port->idx*8+1 + 0xC0); /* enable B1 RX timeslot on STIO1*/

			setup_su(hw, port->idx, 0, 1);
			break;

		case HFC_L1_TESTLOOP_B2:
			setup_fifo(hw, port->idx*8+2, 0xC6, 0, 0, 0);	/* connect B2-SU RX with PCM TX */
			setup_fifo(hw, port->idx*8+3, 0xC6, 0, 0, 0);	/* connect B2-SU TX with PCM RX */

			write_xhfc(hw, R_SLOT, port->idx*8+2);		/* PCM timeslot B2 TX */
			write_xhfc(hw, A_SL_CFG, port->idx*8+2 + 0x80); /* enable B2 TX timeslot on STIO1 */

			write_xhfc(hw, R_SLOT, port->idx*8+3);		/* PCM timeslot B2 RX */
			write_xhfc(hw, A_SL_CFG, port->idx*8+3 + 0xC0); /* enable B2 RX timeslot on STIO1*/

			setup_su(hw, port->idx, 1, 1);
			break;
	}
}


static void
l1_timer_start_t3(channel_t * dch)
{
	xhfc_hw * hw = dch->hw;
	xhfc_port_t * port = hw->chan[dch->channel].port;

	if (!timer_pending(&port->t3_timer)) {
		if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
			mISDN_debugprint(&dch->inst,
				 "%s channel(%i) state(F%i)",
				 __FUNCTION__, dch->channel, dch->state);

		port->t3_timer.expires = jiffies + (XHFC_TIMER_T3 * HZ) / 1000;
		add_timer(&port->t3_timer);
	}
}

static void
l1_timer_stop_t3(channel_t * dch)
{
	xhfc_hw * hw = dch->hw;
	xhfc_port_t * port = hw->chan[dch->channel].port;

	clear_bit(HFC_L1_ACTIVATING, &port->l1_flags);
	if (timer_pending(&port->t3_timer)) {
		if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
			mISDN_debugprint(&dch->inst,
				 "%s channel(%i) state(F%i)",
				 __FUNCTION__, dch->channel, dch->state);
					 		
		del_timer(&port->t3_timer);
	}
}

/***********************************/
/* called when timer t3 expires    */
/* -> activation failed            */
/*    force clean L1 deactivation  */
/***********************************/
static void
l1_timer_expire_t3(channel_t * dch)
{
	xhfc_hw * hw = dch->hw;
	xhfc_port_t * port = hw->chan[dch->channel].port;

	if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
		mISDN_debugprint(&dch->inst,
			 "%s channel(%i) state(F%i), "
			 "l1->l2 (PH_DEACTIVATE | INDICATION)",
			 __FUNCTION__, dch->channel, dch->state);	

	clear_bit(HFC_L1_ACTIVATING, &port->l1_flags),
	xhfc_ph_command(dch, HFC_L1_FORCE_DEACTIVATE_TE);
	
	mISDN_queue_data(&dch->inst, FLG_MSG_UP,
		(PH_DEACTIVATE | INDICATION),
		0, 0, NULL, 0);
	mISDN_queue_data(&dch->inst, dch->inst.id | MSG_BROADCAST,
		MGR_SHORTSTATUS | INDICATION, SSTATUS_L1_DEACTIVATED,
		0, NULL, 0);
}

static void
l1_timer_start_t4(channel_t * dch)
{
	xhfc_hw * hw = dch->hw;
	xhfc_port_t * port = hw->chan[dch->channel].port;

	set_bit(HFC_L1_DEACTTIMER, &port->l1_flags);

	if (!timer_pending(&port->t4_timer)) {
		if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
			mISDN_debugprint(&dch->inst,
				 "%s channel(%i) state(F%i)",
				 __FUNCTION__, dch->channel, dch->state);

		port->t4_timer.expires =
		    jiffies + (XHFC_TIMER_T4 * HZ) / 1000;
		add_timer(&port->t4_timer);
	}
}

static void
l1_timer_stop_t4(channel_t * dch)
{
	xhfc_hw * hw = dch->hw;
	xhfc_port_t * port = hw->chan[dch->channel].port;

	clear_bit(HFC_L1_DEACTTIMER, &port->l1_flags);
	if (timer_pending(&port->t4_timer)) {
		if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
			mISDN_debugprint(&dch->inst,
				 "%s channel(%i) state(F%i)", 
				 __FUNCTION__, dch->channel, dch->state);
		del_timer(&port->t4_timer);
	}
}

/*****************************************************/
/* called when timer t4 expires                      */
/* send (PH_DEACTIVATE | INDICATION) to upper layer  */
/*****************************************************/
static void
l1_timer_expire_t4(channel_t * dch)
{
	xhfc_hw * hw = dch->hw;
	xhfc_port_t * port = hw->chan[dch->channel].port;
	
	if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
		mISDN_debugprint(&dch->inst,
			 "%s channel(%i) state(F%i), "
			 "l1->l2 (PH_DEACTIVATE | INDICATION)",
			 __FUNCTION__, dch->channel, dch->state);

	clear_bit(HFC_L1_DEACTTIMER, &port->l1_flags);
	mISDN_queue_data(&dch->inst, FLG_MSG_UP,
		(PH_DEACTIVATE | INDICATION), 0, 0, NULL, 0);
	mISDN_queue_data(&dch->inst, dch->inst.id | MSG_BROADCAST,
		MGR_SHORTSTATUS | INDICATION, SSTATUS_L1_DEACTIVATED,
		0, NULL, 0);	
}

/*********************************/
/* Line Interface State handler  */
/*********************************/
static void
su_new_state(channel_t * dch)
{
	xhfc_hw * hw = dch->hw;
	xhfc_port_t * port = hw->chan[dch->channel].port;
	
	u_int prim = 0;

	if (port->mode & PORT_MODE_TE) {
		if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
			mISDN_debugprint(&dch->inst, "%s: TE F%d",
				__FUNCTION__, dch->state);

		if ((dch->state <= 3) || (dch->state >= 7))
			l1_timer_stop_t3(dch);

		switch (dch->state) {
			case (3):
				if (test_and_clear_bit(HFC_L1_ACTIVATED, &port->l1_flags))
					l1_timer_start_t4(dch);
				return;

			case (7):
				if (timer_pending(&port->t4_timer))
					l1_timer_stop_t4(dch);
					
				if (test_and_clear_bit(HFC_L1_ACTIVATING, &port->l1_flags)) {
					if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
						mISDN_debugprint(&dch->inst,
							 "l1->l2 (PH_ACTIVATE | CONFIRM)");
							 
					set_bit(HFC_L1_ACTIVATED, &port->l1_flags);
					prim = PH_ACTIVATE | CONFIRM;
				} else {
					if (!(test_and_set_bit(HFC_L1_ACTIVATED, &port->l1_flags))) {
						if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
							mISDN_debugprint(&dch->inst,
								 "l1->l2 (PH_ACTIVATE | INDICATION)");
						prim = PH_ACTIVATE | INDICATION;
					} else {
						// L1 was already activated (e.g. F8->F7)
						return;
					}
				}
				mISDN_queue_data(&dch->inst, dch->inst.id | MSG_BROADCAST,
					MGR_SHORTSTATUS | INDICATION, SSTATUS_L1_ACTIVATED,
					0, NULL, 0);
				break;

			case (8):
				l1_timer_stop_t4(dch);
				return;

			/*
			case (0):
			case (1):
			case (2):
			case (4):
			case (5):
			case (6):
			*/
			default:
				return;
		}
		
	} else if (port->mode & PORT_MODE_NT) {
		
		if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
			mISDN_debugprint(&dch->inst, "%s: NT G%d",
				__FUNCTION__, dch->state);

		switch (dch->state) {
			
			case (1):
				test_and_set_bit(FLG_ACTIVE, &dch->Flags);
				port->nt_timer = 0;
				port->mode &= ~NT_TIMER;
				prim = (PH_DEACTIVATE | INDICATION);
				if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
					mISDN_debugprint(&dch->inst,
						 "l1->l2 (PH_DEACTIVATE | INDICATION)");
				break;
			case (2):
				if (port->nt_timer < 0) {
					port->nt_timer = 0;
					port->mode &= ~NT_TIMER;
					xhfc_ph_command(dch, HFC_L1_DEACTIVATE_NT);
				} else {
					port->nt_timer = NT_T1_COUNT;
					port->mode |= NT_TIMER;

					write_xhfc(hw, R_SU_SEL, port->idx);
					write_xhfc(hw, A_SU_WR_STA, M_SU_SET_G2_G3);
				}
				return;
			case (3):
				test_and_set_bit(FLG_ACTIVE, &dch->Flags);
				port->nt_timer = 0;
				port->mode &= ~NT_TIMER;
				prim = (PH_ACTIVATE | INDICATION);
				
				if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
					mISDN_debugprint(&dch->inst,
						 "l1->l2 (PH_ACTIVATE | INDICATION)");				
				break;
			case (4):
				port->nt_timer = 0;
				port->mode &= ~NT_TIMER;
				return;
			default:
				break;
		}
		mISDN_queue_data(&dch->inst, dch->inst.id | MSG_BROADCAST,
			MGR_SHORTSTATUS | INDICATION,  test_bit(FLG_ACTIVE, &dch->Flags) ?
			SSTATUS_L1_ACTIVATED : SSTATUS_L1_DEACTIVATED,
			0, NULL, 0);
	}

	mISDN_queue_data(&dch->inst, FLG_MSG_UP, prim, 0, 0, NULL, 0);
}

/*************************************/
/* Layer 1 D-channel hardware access */
/*************************************/
static int
handle_dmsg(channel_t *dch, struct sk_buff *skb)
{
	xhfc_hw * hw = dch->hw;
	xhfc_port_t * port = hw->chan[dch->channel].port;
	
	int		ret = 0;
	mISDN_head_t	*hh = mISDN_HEAD_P(skb);	

	switch (hh->prim) {
		case (PH_ACTIVATE | REQUEST):
			if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
				mISDN_debugprint(&dch->inst,
					 "l2->l1 (PH_ACTIVATE | REQUEST)");				
			
			if (port->mode & PORT_MODE_TE) {
				if (test_bit(HFC_L1_ACTIVATED, &port->l1_flags)) {
					
					if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
						mISDN_debugprint(&dch->inst,
							 "l1->l2 (PH_ACTIVATE | CONFIRM)");
					
					mISDN_queue_data(&dch->inst, FLG_MSG_UP,
					                 PH_ACTIVATE | CONFIRM,
					                 0, 0, NULL, 0);
				} else {
					test_and_set_bit(HFC_L1_ACTIVATING, &port->l1_flags);

					xhfc_ph_command(dch, HFC_L1_ACTIVATE_TE);
					l1_timer_start_t3(dch);
				}
			} else {
				xhfc_ph_command(dch, HFC_L1_ACTIVATE_NT);
			}
			break;
			
		case (PH_DEACTIVATE | REQUEST):
			if (port->mode & PORT_MODE_TE) {
				// no deact request in TE mode !
				ret = -EINVAL;
			} else {
				xhfc_ph_command(dch, HFC_L1_DEACTIVATE_NT);
			}
			break;

		case (MDL_FINDTEI | REQUEST):
			return(mISDN_queue_up(&dch->inst, 0, skb));
			break;
	}
	
	return(ret);
}

/*************************************/
/* Layer 1 B-channel hardware access */
/*************************************/
static int
handle_bmsg(channel_t *bch, struct sk_buff *skb)
{
	xhfc_hw 	*hw = bch->hw;
	int		ret = 0;
	mISDN_head_t	*hh = mISDN_HEAD_P(skb);
	u_long		flags;
	
	if ((hh->prim == (PH_ACTIVATE | REQUEST)) ||
		   (hh->prim == (DL_ESTABLISH | REQUEST))) {
		if (!test_and_set_bit(FLG_ACTIVE, &bch->Flags)) {
			spin_lock_irqsave(&hw->lock, flags);
			if (bch->inst.pid.protocol[2] == ISDN_PID_L2_B_TRANS)
				test_and_set_bit(FLG_L2DATA, &bch->Flags);			
			ret =
			    setup_channel(hw, bch->channel,
					  bch->inst.pid.protocol[1]);
			spin_unlock_irqrestore(&hw->lock, flags);
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
		   	
		spin_lock_irqsave(&hw->lock, flags);
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
		spin_unlock_irqrestore(&hw->lock, flags);
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

	} else {
		printk(KERN_WARNING "%s %s: unknown prim(%x)\n",
		       hw->card_name, __FUNCTION__, hh->prim);
	}
	if (!ret)
		dev_kfree_skb(skb);
	return (ret);
}

/***********************************************/
/* handle Layer2 -> Layer 1 D-Channel messages */
/***********************************************/
static int
xhfc_l2l1(mISDNinstance_t *inst, struct sk_buff *skb)
{
	channel_t	*chan = container_of(inst, channel_t, inst);
	mISDN_head_t	*hh = mISDN_HEAD_P(skb);
	xhfc_hw		*hw = inst->privat;
	int		ret = 0;
	u_long		flags;
	
	if ((hh->prim == PH_DATA_REQ) || (hh->prim == DL_DATA_REQ)) {
		spin_lock_irqsave(inst->hwlock, flags);
		ret = channel_senddata(chan, hh->dinfo, skb);
		if (ret > 0) { /* direct TX */
			tasklet_schedule(&hw->tasklet);
			// printk ("PH_DATA_REQ: %i bytes in channel(%i)\n", ret, chan->channel);
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
xhfc_manager(void *data, u_int prim, void *arg)
{
	xhfc_hw *hw = NULL;
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
			if (hw->chan[i].ch.Flags &&
				&hw->chan[i].ch.inst == inst) {
				channel = i;
				chan = &hw->chan[i].ch;
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
				if (xhfc_l2l1(inst, skb))
					dev_kfree_skb(skb);
			} else
				printk(KERN_WARNING "no SKB in %s MGR_UNREGLAYER | REQUEST\n", __FUNCTION__);
			hw_mISDNObj.ctrl(inst, MGR_UNREGLAYER | REQUEST, NULL);
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
					if (xhfc_l2l1(inst, skb))
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
next_tx_frame(xhfc_hw * hw, __u8 channel)
{
	channel_t *ch = &hw->chan[channel].ch;

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
xhfc_waitbusy(xhfc_hw * hw)
{
	while (read_xhfc(hw, R_STATUS) & M_BUSY);
}

static inline void
xhfc_selfifo(xhfc_hw * hw, __u8 fifo)
{
	write_xhfc(hw, R_FIFO, fifo);
	xhfc_waitbusy(hw);
}

static inline void
xhfc_inc_f(xhfc_hw * hw)
{
	write_xhfc(hw, A_INC_RES_FIFO, M_INC_F);
	xhfc_waitbusy(hw);
}

static inline void
xhfc_resetfifo(xhfc_hw * hw)
{
	write_xhfc(hw, A_INC_RES_FIFO, M_RES_FIFO | M_RES_FIFO_ERR);
	xhfc_waitbusy(hw);
}

/**************************/
/* fill fifo with TX data */
/**************************/
void
xhfc_write_fifo(xhfc_hw * hw, __u8 channel)
{
	__u8		fcnt, tcnt, i;
	__u8		free;
	__u8		f1, f2;
	__u8		fstat;
	__u8		*data;
	int		remain;
	channel_t	*ch = &hw->chan[channel].ch;


      send_buffer:
	if (!ch->tx_skb)
		return;
	remain = ch->tx_skb->len - ch->tx_idx;
	if (remain <= 0)
		return;      
      
	xhfc_selfifo(hw, (channel * 2));

	fstat = read_xhfc(hw, A_FIFO_STA);
	free = (hw->max_z - (read_xhfc(hw, A_USAGE)));
	tcnt = (free >= remain) ? remain : free;

	f1 = read_xhfc(hw, A_F1);
	f2 = read_xhfc(hw, A_F2);

	fcnt = 0x07 - ((f1 - f2) & 0x07);	/* free frame count in tx fifo */

	if (debug & DEBUG_HFC_FIFO) {
		mISDN_debugprint(&ch->inst,
				 "%s channel(%i) len(%i) idx(%i) f1(%i) f2(%i) fcnt(%i) tcnt(%i) free(%i) fstat(%i)",
				 __FUNCTION__, channel, ch->tx_skb->len, ch->tx_idx,
				 f1, f2, fcnt, tcnt, free, fstat);
	}

	/* check for fifo underrun during frame transmission */
	fstat = read_xhfc(hw, A_FIFO_STA);
	if (fstat & M_FIFO_ERR) {
		if (debug & DEBUG_HFC_FIFO_ERR) {
			mISDN_debugprint(&ch->inst,
					 "%s transmit fifo channel(%i) underrun idx(%i), A_FIFO_STA(0x%02x)",
					 __FUNCTION__, channel,
					 ch->tx_idx, fstat);
		}

		write_xhfc(hw, A_INC_RES_FIFO, M_RES_FIFO_ERR);

		/* restart frame transmission */
		if ((test_bit(FLG_HDLC, &ch->Flags)) && ch->tx_idx) {
			ch->tx_idx = 0;
			goto send_buffer;
		}
	}

	if (free && fcnt && tcnt) {
		data = ch->tx_skb->data + ch->tx_idx;
		ch->tx_idx += tcnt;

		if (debug & DEBUG_HFC_FIFO) {
			printk("%s channel(%i) writing: ",
			       hw->card_name, channel);

			i=0;
			while (i<tcnt)
				printk("%02x ", *(data+(i++)));
			printk ("\n");
		}
		
		/* write data to FIFO */
		i=0;
		while (i<tcnt) {
			if ((tcnt-i) >= 4) {
				write32_xhfc(hw, A_FIFO_DATA, *((__u32 *) (data+i)));
				i += 4;
			} else {
				write_xhfc(hw, A_FIFO_DATA, *(data+i));
				i++;
			}
		}

		if (ch->tx_idx == ch->tx_skb->len) {
			if (test_bit(FLG_HDLC, &ch->Flags)) {
				/* terminate frame */
				xhfc_inc_f(hw);
			} else {
				xhfc_selfifo(hw, (channel * 2));
			}

			/* check for fifo underrun during frame transmission */
			fstat = read_xhfc(hw, A_FIFO_STA);
			if (fstat & M_FIFO_ERR) {
				if (debug & DEBUG_HFC_FIFO_ERR) {
					mISDN_debugprint(&ch->inst,
							 "%s transmit fifo channel(%i) underrun "
							 "during transmission, A_FIFO_STA(0x%02x)\n",
							 __FUNCTION__,
							 channel,
							 fstat);
				}
				write_xhfc(hw, A_INC_RES_FIFO, M_RES_FIFO_ERR);

				if (test_bit(FLG_HDLC, &ch->Flags)) {
					// restart frame transmission
					ch->tx_idx = 0;
					goto send_buffer;
				}
			}
			
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
			xhfc_selfifo(hw, (channel * 2));
		}
	} 
}

/****************************/
/* read RX data out of fifo */
/****************************/
void
xhfc_read_fifo(xhfc_hw * hw, __u8 channel)
{
	__u8	f1=0, f2=0, z1=0, z2=0;
	__u8	fstat = 0;
	int	i;
	int	rcnt;		/* read rcnt bytes out of fifo */
	__u8	*data;		/* new data pointer */
	struct sk_buff	*skb;	/* data buffer for upper layer */
	channel_t	*ch = &hw->chan[channel].ch;

      receive_buffer:

	xhfc_selfifo(hw, (channel * 2) + 1);

	fstat = read_xhfc(hw, A_FIFO_STA);
	if (fstat & M_FIFO_ERR) {
		if (debug & DEBUG_HFC_FIFO_ERR)
			mISDN_debugprint(&ch->inst,
					 "RX fifo overflow channel(%i), "
					 "A_FIFO_STA(0x%02x) f0cnt(%i)",
					 channel, fstat, hw->f0_akku);
		write_xhfc(hw, A_INC_RES_FIFO, M_RES_FIFO_ERR);
	}

	if (test_bit(FLG_HDLC, &ch->Flags)) {
		/* hdlc rcnt */
		f1 = read_xhfc(hw, A_F1);
		f2 = read_xhfc(hw, A_F2);
		z1 = read_xhfc(hw, A_Z1);
		z2 = read_xhfc(hw, A_Z2);

		rcnt = (z1 - z2) & hw->max_z;
		if (f1 != f2)
			rcnt++;

	} else {
		/* transparent rcnt */
		rcnt = read_xhfc(hw, A_USAGE) - 1;
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
		
		/* read data from FIFO */
		i=0;
		while (i<rcnt) {
			if ((rcnt-i) >= 4) {
				*((__u32 *) (data+i)) = read32_xhfc(hw, A_FIFO_DATA);
				i += 4;
			} else {
				*(data+i) = read_xhfc(hw, A_FIFO_DATA);
				i++;
			}
		}		
	} else
		return;
		

	if (test_bit(FLG_HDLC, &ch->Flags)) {
		if (f1 != f2) {
			xhfc_inc_f(hw);

			/* check minimum frame size */
			if (ch->rx_skb->len < 4) {
				if (debug & DEBUG_HFC_FIFO_ERR)
					mISDN_debugprint(&ch->inst,
							 "%s: frame in channel(%i) < minimum size",
							 __FUNCTION__,
							 channel);
				goto read_exit;
			}
			
			/* check crc */
			if (ch->rx_skb->data[ch->rx_skb->len - 1]) {
				if (debug & DEBUG_HFC_FIFO_ERR)
					mISDN_debugprint(&ch->inst,
							 "%s: channel(%i) CRC-error",
							 __FUNCTION__,
							 channel);
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
			
			queue_ch_frame(ch, INDICATION, MISDN_ID_ANY, skb);

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

		      read_exit:
			if (ch->rx_skb)
				skb_trim(ch->rx_skb, 0);
			if (read_xhfc(hw, A_USAGE) > 8) {
				if (debug & DEBUG_HFC_FIFO)
					mISDN_debugprint(&ch->inst,
							 "%s: channel(%i) continue xhfc_read_fifo",
							 __FUNCTION__,
							 channel);
				goto receive_buffer;
			}
			return;


		} else {
			xhfc_selfifo(hw, (channel * 2) + 1);
		}
	} else {
		xhfc_selfifo(hw, (channel * 2) + 1);
		if (ch->rx_skb->len >= TRANSP_PACKET_SIZE) {
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
xhfc_bh_handler(unsigned long ul_hw)
{
	xhfc_hw 	*hw = (xhfc_hw *) ul_hw;
	int		i;
	reg_a_su_rd_sta	su_state;
	channel_t	*dch;

	/* timer interrupt */
	if (hw->misc_irq.bit.v_ti_irq) {
		hw->misc_irq.bit.v_ti_irq = 0;

		/* Handle tx Fifos */
		for (i = 0; i < hw->max_fifo; i++) {
			if ((1 << (i * 2)) & (hw->fifo_irqmsk)) {
				hw->fifo_irq &= ~(1 << (i * 2));
				if (test_bit(FLG_TX_BUSY, &hw->chan[i].ch.Flags))
					xhfc_write_fifo(hw, i);
			}
		}
		
		/* handle NT Timer */
		for (i = 0; i < hw->num_ports; i++) {
			if ((hw->port[i].mode & PORT_MODE_NT)
			    && (hw->port[i].mode & NT_TIMER)) {
				if ((--hw->port[i].nt_timer) < 0)
					su_new_state(&hw->chan[(i << 2) + 2].ch);
			}
		}
	}

	/* set fifo_irq when RX data over treshold */
	for (i = 0; i < hw->num_ports; i++) {
		hw->fifo_irq |= read_xhfc(hw, R_FILL_BL0 + i) << (i * 8);
	}

	/* Handle rx Fifos */
	if ((hw->fifo_irq & hw->fifo_irqmsk) & FIFO_MASK_RX) {
		for (i = 0; i < hw->max_fifo; i++) {
			if ((hw->fifo_irq & (1 << (i * 2 + 1)))
			    & (hw->fifo_irqmsk)) {

				hw->fifo_irq &= ~(1 << (i * 2 + 1));
				xhfc_read_fifo(hw, i);
			}
		}
	}

	/* su interrupt */
	if (hw->su_irq.reg & hw->su_irqmsk.reg) {
		hw->su_irq.reg = 0;
		for (i = 0; i < hw->num_ports; i++) {
			write_xhfc(hw, R_SU_SEL, i);
			su_state.reg = read_xhfc(hw, A_SU_RD_STA);
			
			dch = &hw->chan[(i << 2) + 2].ch;
			if (su_state.bit.v_su_sta != dch->state) {
				dch->state = su_state.bit.v_su_sta;
				su_new_state(dch);
			}
		}
	}
}

/*********************/
/* Interrupt handler */
/*********************/
static irqreturn_t
xhfc_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	xhfc_hw *hw = dev_id;
	__u8 i;
	__u32 f0_cnt;

	if (!(hw->irq_ctrl.bit.v_glob_irq_en)
	    && (read_xhfc(hw, R_IRQ_OVIEW))) {
		if (debug & DEBUG_HFC_IRQ)
			printk(KERN_INFO
			       "%s %s NOT M_GLOB_IRQ_EN or R_IRQ_OVIEW \n",
			       hw->card_name, __FUNCTION__);
		return IRQ_NONE;
	}

	hw->misc_irq.reg |= read_xhfc(hw, R_MISC_IRQ);
	hw->su_irq.reg |= read_xhfc(hw, R_SU_IRQ);

	/* get fifo IRQ states in bundle */
	for (i = 0; i < 4; i++) {
		hw->fifo_irq |=
		    (read_xhfc(hw, R_FIFO_BL0_IRQ + i) << (i * 8));
	}

	/* call bottom half at events
	 *   - Timer Interrupt (or other misc_irq sources)
	 *   - SU State change
	 *   - Fifo FrameEnd interrupts (only at rx fifos enabled)
	 */
	if (!((hw->misc_irq.reg & hw->misc_irqmsk.reg)
	      || (hw->su_irq.reg & hw->su_irqmsk.reg)
	      || (hw->fifo_irq & hw->fifo_irqmsk)))
		return IRQ_NONE;

	/* queue bottom half */
	if (!(hw->testirq))
		tasklet_schedule(&hw->tasklet);

	/* count irqs */
	hw->irq_cnt++;

#ifdef USE_F0_COUNTER	
	/* akkumulate f0 counter diffs */
	f0_cnt = read_xhfc(hw, R_F0_CNTL);
	f0_cnt += read_xhfc(hw, R_F0_CNTH) << 8;
	hw->f0_akku += (f0_cnt - hw->f0_cnt);
	if ((f0_cnt - hw->f0_cnt) < 0)
		hw->f0_akku += 0xFFFF;
	hw->f0_cnt = f0_cnt;
#endif
	
	return IRQ_HANDLED;
}

/*****************************************************/
/* disable all interrupts by disabling M_GLOB_IRQ_EN */
/*****************************************************/
void
disable_interrupts(xhfc_hw * hw)
{
	if (debug & DEBUG_HFC_IRQ)
		printk(KERN_INFO "%s %s\n", hw->card_name, __FUNCTION__);
	hw->irq_ctrl.bit.v_glob_irq_en = 0;
	write_xhfc(hw, R_IRQ_CTRL, hw->irq_ctrl.reg);
}

/******************************************/
/* start interrupt and set interrupt mask */
/******************************************/
void
enable_interrupts(xhfc_hw * hw)
{
	if (debug & DEBUG_HFC_IRQ)
		printk(KERN_INFO "%s %s\n", hw->card_name, __FUNCTION__);

	write_xhfc(hw, R_SU_IRQMSK, hw->su_irqmsk.reg);

	/* use defined timer interval */
	write_xhfc(hw, R_TI_WD, hw->ti_wd.reg);
	hw->misc_irqmsk.bit.v_ti_irqmsk = 1;
	write_xhfc(hw, R_MISC_IRQMSK, hw->misc_irqmsk.reg);

	/* clear all pending interrupts bits */
	read_xhfc(hw, R_MISC_IRQ);
	read_xhfc(hw, R_SU_IRQ);
	read_xhfc(hw, R_FIFO_BL0_IRQ);
	read_xhfc(hw, R_FIFO_BL1_IRQ);
	read_xhfc(hw, R_FIFO_BL2_IRQ);
	read_xhfc(hw, R_FIFO_BL3_IRQ);

	/* enable global interrupts */
	hw->irq_ctrl.bit.v_glob_irq_en = 1;
	hw->irq_ctrl.bit.v_fifo_irq_en = 1;
	write_xhfc(hw, R_IRQ_CTRL, hw->irq_ctrl.reg);
}

/***********************************/
/* initialise the XHFC ISDN Chip   */
/* return 0 on success.            */
/***********************************/
int
init_xhfc(xhfc_hw * hw)
{
	int err = 0;
	int timeout = 0x2000;

	hw->chip_id = read_xhfc(hw, R_CHIP_ID);

	if (debug & DEBUG_HFC_INIT)
		printk(KERN_INFO "%s %s ChipID: 0x%x\n", hw->card_name,
		       __FUNCTION__, hw->chip_id);

	switch (hw->chip_id) {
		case CHIP_ID_1SU:
			hw->num_ports = 1;
			hw->max_fifo = 4;
			hw->max_z = 0xFF;
			hw->ti_wd.bit.v_ev_ts = 0x6;	/* timer irq interval 16 ms */
			write_xhfc(hw, R_FIFO_MD, M1_FIFO_MD * 2);
			hw->su_irqmsk.bit.v_su0_irqmsk = 1;
			break;
		case CHIP_ID_2SU:
			hw->num_ports = 2;
			hw->max_fifo = 8;
			hw->max_z = 0x7F;
			hw->ti_wd.bit.v_ev_ts = 0x5;	/* timer irq interval 8 ms */
			write_xhfc(hw, R_FIFO_MD, M1_FIFO_MD * 1);
			hw->su_irqmsk.bit.v_su0_irqmsk = 1;
			hw->su_irqmsk.bit.v_su1_irqmsk = 1;
			break;
		case CHIP_ID_2S4U:
		case CHIP_ID_4SU:
			hw->num_ports = 4;
			hw->max_fifo = 16;
			hw->max_z = 0x3F;
			hw->ti_wd.bit.v_ev_ts = 0x4;	/* timer irq interval 4 ms */
			write_xhfc(hw, R_FIFO_MD, M1_FIFO_MD * 0);
			hw->su_irqmsk.bit.v_su0_irqmsk = 1;
			hw->su_irqmsk.bit.v_su1_irqmsk = 1;
			hw->su_irqmsk.bit.v_su2_irqmsk = 1;
			hw->su_irqmsk.bit.v_su3_irqmsk = 1;
			break;
		default:
			err = -ENODEV;
	}

	if (err) {
		if (debug & DEBUG_HFC_INIT)
			printk(KERN_ERR "%s %s: unkown Chip ID 0x%x\n",
			       hw->card_name, __FUNCTION__, hw->chip_id);
		return (err);
	}
	
	/* software reset to enable R_FIFO_MD setting */
	write_xhfc(hw, R_CIRM, M_SRES);
	udelay(5);
	write_xhfc(hw, R_CIRM, 0);
	
	/* amplitude */
	write_xhfc(hw, R_PWM_MD, 0x80);
	write_xhfc(hw, R_PWM1, 0x18);

	write_xhfc(hw, R_FIFO_THRES, 0x11);

	while ((read_xhfc(hw, R_STATUS) & (M_BUSY | M_PCM_INIT))
	       && (timeout))
		timeout--;

	if (!(timeout)) {
		if (debug & DEBUG_HFC_INIT)
			printk(KERN_ERR
			       "%s %s: initialization sequence could not finish\n",
			       hw->card_name, __FUNCTION__);
		return (-ENODEV);
	}

	/* set PCM master mode */
	hw->pcm_md0.bit.v_pcm_md = 1;
	write_xhfc(hw, R_PCM_MD0, hw->pcm_md0.reg);

	/* set pll adjust */
	hw->pcm_md0.bit.v_pcm_idx = IDX_PCM_MD1;
	hw->pcm_md1.bit.v_pll_adj = 3;
	write_xhfc(hw, R_PCM_MD0, hw->pcm_md0.reg);
	write_xhfc(hw, R_PCM_MD1, hw->pcm_md1.reg);

	enable_interrupts(hw);

	mdelay(1 << hw->ti_wd.bit.v_ev_ts);
	if (hw->irq_cnt > 2) {
		disable_interrupts(hw);
		hw->testirq = 0;
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

/*************************************/
/* free memory for all used channels */
/*************************************/
void
release_channels(xhfc_hw * hw)
{
	int i = 0;

	while (i < MAX_CHAN) {
		if (hw->chan[i].ch.Flags) {
			if (debug & DEBUG_HFC_INIT)
				printk(KERN_DEBUG "%s %s: free channel %d\n",
					hw->card_name, __FUNCTION__, i);
			mISDN_freechannel(&hw->chan[i].ch);
			hw_mISDNObj.ctrl(&hw->chan[i].ch.inst, MGR_UNREGLAYER | REQUEST, NULL);
		}
		i++;
	}
}

/*********************************************/
/* setup port (line interface) with SU_CRTLx */
/*********************************************/
void
init_su(xhfc_hw * hw, __u8 pt)
{
	xhfc_port_t *port = &hw->port[pt];

	if (debug & DEBUG_HFC_MODE)
		printk(KERN_INFO "%s %s port(%i)\n", hw->card_name,
		       __FUNCTION__, pt);

	write_xhfc(hw, R_SU_SEL, pt);

	if (port->mode & PORT_MODE_NT)
		port->su_ctrl0.bit.v_su_md = 1;

	if (port->mode & PORT_MODE_EXCH_POL) 
		port->su_ctrl2.reg = M_SU_EXCHG;

	if (port->mode & PORT_MODE_UP) {
		port->st_ctrl3.bit.v_st_sel = 1;
		write_xhfc(hw, A_MS_TX, 0x0F);
		port->su_ctrl0.bit.v_st_sq_en = 1;
	}

	if (debug & DEBUG_HFC_MODE)
		printk(KERN_INFO "%s %s su_ctrl0(0x%02x) "
		       "su_ctrl1(0x%02x) "
		       "su_ctrl2(0x%02x) "
		       "st_ctrl3(0x%02x)\n",
		       hw->card_name, __FUNCTION__,
		       port->su_ctrl0.reg,
		       port->su_ctrl1.reg,
		       port->su_ctrl2.reg,
		       port->st_ctrl3.reg);

	write_xhfc(hw, A_ST_CTRL3, port->st_ctrl3.reg);
	write_xhfc(hw, A_SU_CTRL0, port->su_ctrl0.reg);
	write_xhfc(hw, A_SU_CTRL1, port->su_ctrl1.reg);
	write_xhfc(hw, A_SU_CTRL2, port->su_ctrl2.reg);
	
	if (port->mode & PORT_MODE_TE)
		write_xhfc(hw, A_SU_CLK_DLY, CLK_DLY_TE);
	else
		write_xhfc(hw, A_SU_CLK_DLY, CLK_DLY_NT);

	write_xhfc(hw, A_SU_WR_STA, 0);
}

/*********************************************************/
/* Setup Fifo using A_CON_HDLC, A_SUBCH_CFG, A_FIFO_CTRL */
/*********************************************************/
void
setup_fifo(xhfc_hw * hw, __u8 fifo, __u8 conhdlc, __u8 subcfg,
	   __u8 fifoctrl, __u8 enable)
{
	xhfc_selfifo(hw, fifo);
	write_xhfc(hw, A_CON_HDLC, conhdlc);
	write_xhfc(hw, A_SUBCH_CFG, subcfg);
	write_xhfc(hw, A_FIFO_CTRL, fifoctrl);

	if (enable)
		hw->fifo_irqmsk |= (1 << fifo);
	else
		hw->fifo_irqmsk &= ~(1 << fifo);

	xhfc_resetfifo(hw);
	xhfc_selfifo(hw, fifo);

	if (debug & DEBUG_HFC_MODE) {
		printk(KERN_INFO
		       "%s %s: fifo(%i) conhdlc(0x%02x) subcfg(0x%02x) fifoctrl(0x%02x)\n",
		       hw->card_name, __FUNCTION__, fifo, sread_xhfc(hw,
								     A_CON_HDLC),
		       sread_xhfc(hw, A_SUBCH_CFG), sread_xhfc(hw,
							       A_FIFO_CTRL)
		    );
	}
}

/**************************************************/
/* Setup S/U interface, enable/disable B-Channels */
/**************************************************/
void
setup_su(xhfc_hw * hw, __u8 pt, __u8 bc, __u8 enable)
{
	xhfc_port_t *port = &hw->port[pt];

	if (!((bc == 0) || (bc == 1))) {
		printk(KERN_INFO "%s %s: pt(%i) ERROR: bc(%i) unvalid!\n",
		       hw->card_name, __FUNCTION__, pt, bc);
		return;
	}

	if (debug & DEBUG_HFC_MODE)
		printk(KERN_INFO "%s %s %s pt(%i) bc(%i)\n",
		       hw->card_name, __FUNCTION__,
		       (enable) ? ("enable") : ("disable"), pt, bc);

	if (bc) {
		port->su_ctrl2.bit.v_b2_rx_en = (enable?1:0);
		port->su_ctrl0.bit.v_b2_tx_en = (enable?1:0);
	} else {
		port->su_ctrl2.bit.v_b1_rx_en = (enable?1:0);
		port->su_ctrl0.bit.v_b1_tx_en = (enable?1:0);
	}

	if (hw->port[pt].mode & PORT_MODE_NT)
		hw->port[pt].su_ctrl0.bit.v_su_md = 1;

	write_xhfc(hw, R_SU_SEL, pt);
	write_xhfc(hw, A_SU_CTRL0, hw->port[pt].su_ctrl0.reg);
	write_xhfc(hw, A_SU_CTRL2, hw->port[pt].su_ctrl2.reg);
}

/*********************************************/
/* (dis-) connect D/B-Channel using protocol */
/*********************************************/
int
setup_channel(xhfc_hw * hw, __u8 channel, int protocol)
{
	xhfc_port_t *port = hw->chan[channel].port;
	
	if (test_bit(FLG_BCHANNEL, &hw->chan[channel].ch.Flags)) {
		if (debug & DEBUG_HFC_MODE)
			mISDN_debugprint(&hw->chan[channel].ch.inst,
					 "channel(%i) protocol %x-->%x",
					 channel,
					 hw->chan[channel].ch.state,
					 protocol);

		switch (protocol) {
			case (-1):	/* used for init */
				hw->chan[channel].ch.state = -1;
				hw->chan[channel].ch.channel = channel;
				/* fall trough */
			case (ISDN_PID_NONE):
				if (debug & DEBUG_HFC_MODE)
					mISDN_debugprint(&hw->
							 chan[channel].ch.inst,
							 "ISDN_PID_NONE");
				if (hw->chan[channel].ch.state == ISDN_PID_NONE)
					return (0);	/* already in idle state */
				hw->chan[channel].ch.state = ISDN_PID_NONE;

				setup_fifo(hw, (channel << 1),     4, 0, 0, 0);	/* B-TX fifo */
				setup_fifo(hw, (channel << 1) + 1, 4, 0, 0, 0);	/* B-RX fifo */

				setup_su(hw, port->idx, (channel % 4) ? 1 : 0, 0);
				
				test_and_clear_bit(FLG_HDLC, &hw->chan[channel].ch.Flags);
				test_and_clear_bit(FLG_TRANSPARENT, &hw->chan[channel].ch.Flags);

				break;

			case (ISDN_PID_L1_B_64TRANS):
				if (debug & DEBUG_HFC_MODE)
					mISDN_debugprint(&hw->chan[channel].ch.inst,
							 "ISDN_PID_L1_B_64TRANS");
				setup_fifo(hw, (channel << 1), 6, 0, 0, 1);	/* B-TX Fifo */
				setup_fifo(hw, (channel << 1) + 1, 6, 0, 0, 1);	/* B-RX Fifo */

				setup_su(hw, port->idx, (channel % 4) ? 1 : 0, 1);

				hw->chan[channel].ch.state = ISDN_PID_L1_B_64TRANS;
				test_and_set_bit(FLG_TRANSPARENT, &hw->chan[channel].ch.Flags);

				break;

			case (ISDN_PID_L1_B_64HDLC):
				if (debug & DEBUG_HFC_MODE)
					mISDN_debugprint(&hw->chan[channel].ch.inst,
							 "ISDN_PID_L1_B_64HDLC");
				setup_fifo(hw, (channel << 1), 4, 0, M_FR_ABO, 1);	// TX Fifo
				setup_fifo(hw, (channel << 1) + 1, 4, 0, M_FR_ABO | M_FIFO_IRQMSK, 1);	// RX Fifo

				setup_su(hw, port->idx, (channel % 4) ? 1 : 0, 1);

				hw->chan[channel].ch.state = ISDN_PID_L1_B_64HDLC;
				test_and_set_bit(FLG_HDLC, &hw->chan[channel].ch.Flags);

				break;
			default:
				mISDN_debugprint(&hw->chan[channel].ch.inst,
					"prot not known %x",
					protocol);
				return (-ENOPROTOOPT);
		}
		return (0);
	}
	else if (test_bit(FLG_DCHANNEL, &hw->chan[channel].ch.Flags)) {
		if (debug & DEBUG_HFC_MODE)
			mISDN_debugprint(&hw->chan[channel].ch.inst,
					 "D channel(%i) protocol(%i)",
					 channel, protocol);

		setup_fifo(hw, (channel << 1), 5, 2, M_FR_ABO, 1);	/* D TX fifo */
		setup_fifo(hw, (channel << 1) + 1, 5, 2, M_FR_ABO | M_FIFO_IRQMSK, 1);	/* D RX fifo */

		return (0);
	}

	printk(KERN_INFO
	       "%s %s ERROR: channel(%i) is NEITHER B nor D !!!\n",
	       hw->card_name, __FUNCTION__, channel);

	return (-1);
}

/*****************************************************/
/* register ISDN stack for one XHFC card             */
/*   - register all ports and channels               */
/*   - set param_idx                                 */
/*                                                   */
/*  channel mapping in mISDN in hw->chan[MAX_CHAN]:  */
/*    1st line interf:  0=B1,  1=B2,  2=D,  3=PCM    */
/*    2nd line interf:  4=B1,  5=B2,  6=D,  7=PCM    */
/*    3rd line interf:  8=B1,  9=B2, 10=D, 11=PCM    */
/*    4th line interf; 12=B1, 13=B2, 14=D, 15=PCM    */
/*****************************************************/
int
init_mISDN_channels(xhfc_hw * hw)
{
	int err;
	int pt;		/* ST/U port index */
	int ch_idx;	/* channel index */
	int b;
	channel_t *ch;
	mISDN_pid_t pid;
	u_long flags;
	
	for (pt = 0; pt < hw->num_ports; pt++) {
		/* init D channels */
		ch_idx = (pt << 2) + 2;
		if (debug & DEBUG_HFC_INIT)
			printk(KERN_INFO
			       "%s %s: Registering D-channel, card(%d) "
			       "ch(%d) port(%d) protocol(%x)\n",
			       hw->card_name, __FUNCTION__, hw->cardnum,
			       ch_idx, pt, hw->port[pt].dpid);

		hw->port[pt].idx = pt;
		hw->chan[ch_idx].port = &hw->port[pt];
		ch = &hw->chan[ch_idx].ch;
		
		memset(ch, 0, sizeof(channel_t));
		ch->channel = ch_idx;
		ch->debug = debug;
		ch->inst.obj = &hw_mISDNObj;
		ch->inst.hwlock = &hw->lock;
		ch->inst.class_dev.dev = &hw->pdev->dev;
		mISDN_init_instance(&ch->inst, &hw_mISDNObj, hw, xhfc_l2l1);
		ch->inst.pid.layermask = ISDN_LAYER(0);
		sprintf(ch->inst.name, "%s/%d", hw->card_name, pt);
		err = mISDN_initchannel(ch, MSK_INIT_DCHANNEL, MAX_DFRAME_LEN_L1);
		if (err)
			goto free_channels;
		ch->hw = hw;
		
		/* init t3 timer */
		init_timer(&hw->port[pt].t3_timer);
		hw->port[pt].t3_timer.data = (long) ch;
		hw->port[pt].t3_timer.function = (void *) l1_timer_expire_t3;

		/* init t4 timer */
		init_timer(&hw->port[pt].t4_timer);
		hw->port[pt].t4_timer.data = (long) ch;
		hw->port[pt].t4_timer.function = (void *) l1_timer_expire_t4;

		/* init B channels */
		for (b = 0; b < 2; b++) {
			ch_idx = (pt << 2) + b;
			if (debug & DEBUG_HFC_INIT)
				printk(KERN_DEBUG
				       "%s %s: Registering B-channel, card(%d) "
				       "ch(%d) port(%d)\n", hw->card_name,
				       __FUNCTION__, hw->cardnum, ch_idx, pt);

			hw->chan[ch_idx].port = &hw->port[pt];
			ch = &hw->chan[ch_idx].ch;
			
			memset(ch, 0, sizeof(channel_t));
			ch->channel = ch_idx;
			ch->debug = debug;
			mISDN_init_instance(&ch->inst, &hw_mISDNObj, hw, xhfc_l2l1);
			ch->inst.pid.layermask = ISDN_LAYER(0);
			ch->inst.hwlock = &hw->lock;
			ch->inst.class_dev.dev = &hw->pdev->dev;			
			
			sprintf(ch->inst.name, "%s/%d B%d",
				hw->card_name, pt, b + 1);

			if (mISDN_initchannel(ch, MSK_INIT_BCHANNEL, MAX_DATA_MEM)) {
				err = -ENOMEM;
				goto free_channels;
			}
			ch->hw = hw;
		}
		
		/* clear PCM */
		memset(&hw->chan[(pt << 2) + 3], 0, sizeof(channel_t));

		mISDN_set_dchannel_pid(&pid, hw->port[pt].dpid,
				       layermask[hw->param_idx + pt]);

		/* register D Channel */
		ch = &hw->chan[(pt << 2) + 2].ch;
		
		/* set protocol for NT/TE */
		if (hw->port[pt].mode & PORT_MODE_NT) {
			/* NT-mode */
			hw->port[hw->param_idx + pt].mode |= NT_TIMER;
			hw->port[hw->param_idx + pt].nt_timer = 0;

			ch->inst.pid.protocol[0] = ISDN_PID_L0_NT_S0;
			ch->inst.pid.protocol[1] = ISDN_PID_L1_NT_S0;
			pid.protocol[0] = ISDN_PID_L0_NT_S0;
			pid.protocol[1] = ISDN_PID_L1_NT_S0;
			ch->inst.pid.layermask |= ISDN_LAYER(1);
			pid.layermask |= ISDN_LAYER(1);
			if (layermask[hw->param_idx + pt] & ISDN_LAYER(2))
				pid.protocol[2] = ISDN_PID_L2_LAPD_NET;
		} else {
			/* TE-mode */
			hw->port[hw->param_idx + pt].mode |= PORT_MODE_TE;
			ch->inst.pid.protocol[0] = ISDN_PID_L0_TE_S0;
			ch->inst.pid.protocol[1] = ISDN_PID_L1_TE_S0;
			pid.protocol[0] = ISDN_PID_L0_TE_S0;
			pid.protocol[1] = ISDN_PID_L1_TE_S0;
		}

		if (debug & DEBUG_HFC_INIT)
			printk(KERN_INFO
			       "%s %s: registering Stack for Port %i\n",
			       hw->card_name, __FUNCTION__, pt);

		/* register stack */
		err = hw_mISDNObj.ctrl(NULL, MGR_NEWSTACK | REQUEST, &ch->inst);
		if (err) {
			printk(KERN_ERR
			       "%s %s: MGR_NEWSTACK | REQUEST  err(%d)\n",
			       hw->card_name, __FUNCTION__, err);
			goto free_channels;
		}
		ch->state = 0;

		/* attach two BChannels to this DChannel (ch) */
		for (b = 0; b < 2; b++) {
			err = hw_mISDNObj.ctrl(ch->inst.st,
				MGR_NEWSTACK | REQUEST,
				&hw->chan[(pt << 2) + b].ch.inst);
			if (err) {
				printk(KERN_ERR
				       "%s %s: MGR_ADDSTACK bchan error %d\n",
				       hw->card_name, __FUNCTION__, err);
				goto free_stack;
			}
		}

		err = hw_mISDNObj.ctrl(ch->inst.st, MGR_SETSTACK | REQUEST, &pid);

		if (err) {
			printk(KERN_ERR
			       "%s %s: MGR_SETSTACK REQUEST dch err(%d)\n",
			       hw->card_name, __FUNCTION__, err);
			hw_mISDNObj.ctrl(ch->inst.st,
					 MGR_DELSTACK | REQUEST, NULL);
			goto free_stack;
		}

		/* initial setup of each channel */
		setup_channel(hw, ch->channel, -1);
		for (b = 0; b < 2; b++)
			setup_channel(hw, (pt << 2) + b, -1);

		/* delay some time */
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout((100 * HZ) / 1000);	/* Timeout 100ms */

		hw_mISDNObj.ctrl(ch->inst.st, MGR_CTRLREADY | INDICATION, NULL);
	}
	return (0);

      free_stack:
	hw_mISDNObj.ctrl(ch->inst.st, MGR_DELSTACK | REQUEST, NULL);
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
parse_module_params(xhfc_hw * hw)
{
	__u8 pt;

	/* parse module parameters */
	for (pt = 0; pt < hw->num_ports; pt++) {
		/* D-Channel protocol: (2=DSS1) */
		hw->port[pt].dpid = (protocol[hw->param_idx + pt] & 0x0F);
		if (hw->port[pt].dpid == 0) {
			printk(KERN_INFO
			       "%s %s: WARNING: wrong value for protocol[%i], "
			       "assuming 0x02 (DSS1)...\n",
			       hw->card_name, __FUNCTION__,
			       hw->param_idx + pt);
			hw->port[pt].dpid = 0x02;
		}

		/* Line Interface TE or NT */
		if (protocol[hw->param_idx + pt] & 0x10)
			hw->port[pt].mode |= PORT_MODE_NT;
		else
			hw->port[pt].mode |= PORT_MODE_TE;

		/* Line Interface in S0 or Up mode */
		if (protocol[hw->param_idx + pt] & 0x20)
			hw->port[pt].mode |= PORT_MODE_UP;
		else
			hw->port[pt].mode |= PORT_MODE_S0;

		/* st line polarity */
		if (protocol[hw->param_idx + pt] & 0x40)
			hw->port[pt].mode |= PORT_MODE_EXCH_POL;
			
		/* link B-channel loop */
		if (protocol[hw->param_idx + pt] & 0x80)
			hw->port[pt].mode |= PORT_MODE_LOOP;
		

		if (debug & DEBUG_HFC_INIT)
			printk ("%s %s: protocol[%i]=0x%02x, dpid=%d, mode:%s,%s %s %s\n",
			        hw->card_name, __FUNCTION__, hw->param_idx+pt,
			        protocol[hw->param_idx + pt],
			        hw->port[pt].dpid,
			        (hw->port[pt].mode & PORT_MODE_TE)?"TE":"NT",
			        (hw->port[pt].mode & PORT_MODE_S0)?"S0":"Up",
			        (hw->port[pt].mode & PORT_MODE_EXCH_POL)?"SU_EXCH":"",
			        (hw->port[pt].mode & PORT_MODE_LOOP)?"B-LOOP":""
			        );
	}
}

/********************************/
/* initialise the XHFC hardware */
/* return 0 on success.         */
/********************************/
static int __devinit
setup_instance(xhfc_hw * hw)
{
	int err;
	int pt;
	xhfc_hw *previous_hw;
	u_long flags;

#if BRIDGE == BRIDGE_PCI2PI
	err = init_pci_bridge(hw);
	if (err)
		goto out;
#endif

	if (debug & DEBUG_HFC_INIT)
		printk(KERN_WARNING "%s %s: requesting IRQ %d\n",
		       hw->card_name, __FUNCTION__, hw->irq);

	spin_lock_init(&hw->lock);
	tasklet_init(&hw->tasklet, xhfc_bh_handler, (unsigned long) hw);

	/* search previous instances to index protocol[] array */
	list_for_each_entry(previous_hw, &hw_mISDNObj.ilist, list) {
		hw->param_idx += previous_hw->num_ports;
	}

	spin_lock_irqsave(&hw_mISDNObj.lock, flags);
	/* add this instance to hardware list */
	list_add_tail(&hw->list, &hw_mISDNObj.ilist);
	spin_unlock_irqrestore(&hw_mISDNObj.lock, flags);

	/* init interrupt engine */
	hw->testirq = 1;
	if (request_irq(hw->irq, xhfc_interrupt, SA_SHIRQ, "XHFC", hw)) {
		printk(KERN_WARNING "%s %s: couldn't get interrupt %d\n",
		       hw->card_name, __FUNCTION__, hw->irq);
		hw->irq = 0;
		err = -EIO;
		goto out;
	}

	err = init_xhfc(hw);
	if (err)
		goto out;

	parse_module_params(hw);

	/* init line interfaces (ports) */
	for (pt = 0; pt < hw->num_ports; pt++) {
		init_su(hw, pt);
	}

	/* register all channels at ISDN procol stack */
	err = init_mISDN_channels(hw);
	if (err)
		goto out;

	/* delay some time */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((100 * HZ) / 1000);	/* Timeout 100ms */

	enable_interrupts(hw);

	/* force initial layer1 statechanges */
	hw->su_irq.reg = hw->su_irqmsk.reg;
	
	
	/* init B cbannel loops if desired */
	for (pt = 0; pt < hw->num_ports; pt++) {
		if (hw->port[pt].mode & PORT_MODE_LOOP) {
		        if (debug & DEBUG_HFC_INIT)
	        	        printk(KERN_INFO "%s %s init B-channel loop in port(%i)\n",
	        	               hw->card_name, __FUNCTION__, pt);
	        	               
			xhfc_ph_command(&hw->chan[(pt << 2) + 2].ch, HFC_L1_TESTLOOP_B1);
			xhfc_ph_command(&hw->chan[(pt << 2) + 2].ch, HFC_L1_TESTLOOP_B2);
		}
	}

	return (0);

      out:
	return (err);
}

/************************/
/* release single card  */
/************************/
static void
release_card(xhfc_hw * hw)
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

#if BRIDGE == BRIDGE_PCI2PI

/*****************************************/
/* PCI hotplug interface: probe new card */
/*****************************************/
static int __devinit
xhfc_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	xhfc_param *driver_data = (xhfc_param *) ent->driver_data;
	xhfc_hw *hw;

	int err = -ENOMEM;


	if (!(hw = kmalloc(sizeof(xhfc_hw), GFP_ATOMIC))) {
		printk(KERN_ERR "%s %s: No kmem for XHFC card\n",
		       hw->card_name, __FUNCTION__);
		return (err);
	}
	memset(hw, 0, sizeof(xhfc_hw));

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

	hw->hw_membase = (u_char *) pci_resource_start(pdev, 1);
	hw->membase = ioremap((ulong) hw->hw_membase, 4096);

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
xhfc_pci_remove(struct pci_dev *pdev)
{
	xhfc_hw *hw = pci_get_drvdata(pdev);
	printk(KERN_INFO "%s %s: removing card\n", hw->card_name,
	       __FUNCTION__);
	release_card(hw);
	card_cnt--;
	pci_disable_device(pdev);
	return;
};


static struct pci_device_id xhfc_ids[] = {
	{.vendor = PCI_VENDOR_ID_CCD,
	 .device = 0xA003,
	 .subvendor = 0x1397,
	 .subdevice = 0xA003,
	 .driver_data =
	 (unsigned long) &((xhfc_param) {CHIP_ID_4SU, "XHFC Evaluation Board"}),
	 },
	{}
};

/***************/
/* Module init */
/***************/
static struct pci_driver xhfc_driver = {
      name:DRIVER_NAME,
      probe:xhfc_pci_probe,
      remove:__devexit_p(xhfc_pci_remove),
      id_table:xhfc_ids,
};


MODULE_DEVICE_TABLE(pci, xhfc_ids);

#endif // BRIDGE_PCI2PI

/***************/
/* Module init */
/***************/
static int __init
xhfc_init(void)
{
	int err;

	printk(KERN_INFO "XHFC: %s driver Rev. %s (debug=%i)\n",
	       __FUNCTION__, mISDN_getrev(xhfc_rev), debug);

#ifdef MODULE
	hw_mISDNObj.owner = THIS_MODULE;
#endif

	INIT_LIST_HEAD(&hw_mISDNObj.ilist);
	spin_lock_init(&hw_mISDNObj.lock);
	hw_mISDNObj.name = DRIVER_NAME;
	hw_mISDNObj.own_ctrl = xhfc_manager;

	hw_mISDNObj.DPROTO.protocol[0] = ISDN_PID_L0_TE_S0 | ISDN_PID_L0_NT_S0;
	hw_mISDNObj.DPROTO.protocol[1] = ISDN_PID_L1_TE_S0 | ISDN_PID_L1_NT_S0;
	hw_mISDNObj.BPROTO.protocol[1] = ISDN_PID_L1_B_64TRANS | ISDN_PID_L1_B_64HDLC;
	hw_mISDNObj.BPROTO.protocol[2] = ISDN_PID_L2_B_TRANS | ISDN_PID_L2_B_RAWDEV;
	card_cnt = 0;

	if ((err = mISDN_register(&hw_mISDNObj))) {
		printk(KERN_ERR "XHFC: can't register xhfc error(%d)\n",
		       err);
		goto out;
	}
	
#if BRIDGE == BRIDGE_PCI2PI
	err = pci_register_driver(&xhfc_driver);
	if (err < 0) {
		goto out;
	}
#endif

	printk(KERN_INFO "XHFC: %d cards installed\n", card_cnt);

#if !defined(CONFIG_HOTPLUG)
	if (err == 0) {
		err = -ENODEV;
		pci_unregister_driver(&xhfc_driver);
		goto out;
	}
#endif

	return 0;

      out:
	return (err);
}

static void __exit
xhfc_cleanup(void)
{
	int err;

#if BRIDGE == BRIDGE_PCI2PI
	pci_unregister_driver(&xhfc_driver);
#endif

	if ((err = mISDN_unregister(&hw_mISDNObj))) {
		printk(KERN_ERR "XHFC: can't unregister xhfc, error(%d)\n",
		       err);
	}
	printk(KERN_INFO "%s: driver removed\n", __FUNCTION__);
}

module_init(xhfc_init);
module_exit(xhfc_cleanup);

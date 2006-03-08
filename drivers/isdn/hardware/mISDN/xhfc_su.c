/* $Id: xhfc_su.c,v 1.5 2006/03/08 14:07:14 mbachem Exp $
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

static const char xhfc_rev[] = "$Revision: 1.5 $";

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


/* static function prototypes */
static void release_card(xhfc_pi * pi);
static void setup_fifo(xhfc_t * xhfc, __u8 fifo, __u8 conhdlc, __u8 subcfg,
                       __u8 fifoctrl, __u8 enable);
static void setup_su(xhfc_t * xhfc, __u8 pt, __u8 bc, __u8 enable);
static int  setup_channel(xhfc_t * xhfc, __u8 channel, int protocol);


/****************************************************/
/* Physical S/U commands to control Line Interface  */
/****************************************************/
static void
xhfc_ph_command(channel_t * dch, u_char command)
{
	xhfc_t *xhfc = dch->hw;
	xhfc_port_t *port = xhfc->chan[dch->channel].port;

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

			write_xhfc(xhfc, R_SU_SEL, port->idx);
			write_xhfc(xhfc, A_SU_WR_STA, STA_ACTIVATE);
			break;
			
		case HFC_L1_FORCE_DEACTIVATE_TE:
			if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
				mISDN_debugprint(&dch->inst,
						 "HFC_L1_FORCE_DEACTIVATE_TE channel(%i)",
						 dch->channel);

			write_xhfc(xhfc, R_SU_SEL, port->idx);
			write_xhfc(xhfc, A_SU_WR_STA, STA_DEACTIVATE);
			break;

		case HFC_L1_ACTIVATE_NT:
			if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
				mISDN_debugprint(&dch->inst,
					 "HFC_L1_ACTIVATE_NT channel(%i)",
					 dch->channel);

			write_xhfc(xhfc, R_SU_SEL, port->idx);
			write_xhfc(xhfc, A_SU_WR_STA,
				   STA_ACTIVATE | M_SU_SET_G2_G3);
			break;

		case HFC_L1_DEACTIVATE_NT:
			if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
				mISDN_debugprint(&dch->inst,
						 "HFC_L1_DEACTIVATE_NT channel(%i)",
						 dch->channel);

			write_xhfc(xhfc, R_SU_SEL, port->idx);
			write_xhfc(xhfc, A_SU_WR_STA, STA_DEACTIVATE);
			break;

		case HFC_L1_TESTLOOP_B1:
			setup_fifo(xhfc, port->idx*8,   0xC6, 0, 0, 0);	/* connect B1-SU RX with PCM TX */
			setup_fifo(xhfc, port->idx*8+1, 0xC6, 0, 0, 0);	/* connect B1-SU TX with PCM RX */

			write_xhfc(xhfc, R_SLOT, port->idx*8);		/* PCM timeslot B1 TX */
			write_xhfc(xhfc, A_SL_CFG, port->idx*8 + 0x80); 	/* enable B1 TX timeslot on STIO1 */

			write_xhfc(xhfc, R_SLOT, port->idx*8+1);		/* PCM timeslot B1 RX */
			write_xhfc(xhfc, A_SL_CFG, port->idx*8+1 + 0xC0); /* enable B1 RX timeslot on STIO1*/

			setup_su(xhfc, port->idx, 0, 1);
			break;

		case HFC_L1_TESTLOOP_B2:
			setup_fifo(xhfc, port->idx*8+2, 0xC6, 0, 0, 0);	/* connect B2-SU RX with PCM TX */
			setup_fifo(xhfc, port->idx*8+3, 0xC6, 0, 0, 0);	/* connect B2-SU TX with PCM RX */

			write_xhfc(xhfc, R_SLOT, port->idx*8+2);		/* PCM timeslot B2 TX */
			write_xhfc(xhfc, A_SL_CFG, port->idx*8+2 + 0x80); /* enable B2 TX timeslot on STIO1 */

			write_xhfc(xhfc, R_SLOT, port->idx*8+3);		/* PCM timeslot B2 RX */
			write_xhfc(xhfc, A_SL_CFG, port->idx*8+3 + 0xC0); /* enable B2 RX timeslot on STIO1*/

			setup_su(xhfc, port->idx, 1, 1);
			break;
	}
}


static void
l1_timer_start_t3(channel_t * dch)
{
	xhfc_t * xhfc = dch->hw;
	xhfc_port_t * port = xhfc->chan[dch->channel].port;

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
	xhfc_t * xhfc = dch->hw;
	xhfc_port_t * port = xhfc->chan[dch->channel].port;

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
	xhfc_t * xhfc = dch->hw;
	xhfc_port_t * port = xhfc->chan[dch->channel].port;

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
	xhfc_t * xhfc = dch->hw;
	xhfc_port_t * port = xhfc->chan[dch->channel].port;

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
	xhfc_t * xhfc = dch->hw;
	xhfc_port_t * port = xhfc->chan[dch->channel].port;

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
	xhfc_t * xhfc = dch->hw;
	xhfc_port_t * port = xhfc->chan[dch->channel].port;
	
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
	xhfc_t * xhfc = dch->hw;
	xhfc_port_t * port = xhfc->chan[dch->channel].port;
	
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
			default:
				return;
		}
		
	} else if (port->mode & PORT_MODE_NT) {
		
		if ((dch->debug) & (debug & DEBUG_HFC_S0_STATES))
			mISDN_debugprint(&dch->inst, "%s: NT G%d",
				__FUNCTION__, dch->state);

		switch (dch->state) {
			
			case (1):
				clear_bit(FLG_ACTIVE, &dch->Flags);
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

					write_xhfc(xhfc, R_SU_SEL, port->idx);
					write_xhfc(xhfc, A_SU_WR_STA, M_SU_SET_G2_G3);
				}
				return;
			case (3):
				set_bit(FLG_ACTIVE, &dch->Flags);
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
	xhfc_t * xhfc = dch->hw;
	xhfc_port_t * port = xhfc->chan[dch->channel].port;
	
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
	xhfc_t 	*xhfc = bch->hw;
	int		ret = 0;
	mISDN_head_t	*hh = mISDN_HEAD_P(skb);
	u_long		flags;
	
	if ((hh->prim == (PH_ACTIVATE | REQUEST)) ||
		   (hh->prim == (DL_ESTABLISH | REQUEST))) {
		if (!test_and_set_bit(FLG_ACTIVE, &bch->Flags)) {
			spin_lock_irqsave(&xhfc->lock, flags);
			if (bch->inst.pid.protocol[2] == ISDN_PID_L2_B_TRANS)
				test_and_set_bit(FLG_L2DATA, &bch->Flags);			
			ret =
			    setup_channel(xhfc, bch->channel,
					  bch->inst.pid.protocol[1]);
			spin_unlock_irqrestore(&xhfc->lock, flags);
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
		   	
		spin_lock_irqsave(&xhfc->lock, flags);
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
		setup_channel(xhfc, bch->channel, ISDN_PID_NONE);
		test_and_clear_bit(FLG_ACTIVE, &bch->Flags);
		spin_unlock_irqrestore(&xhfc->lock, flags);
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
		       xhfc->chip_name, __FUNCTION__, hh->prim);
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
	xhfc_t		*xhfc = inst->privat;
	int		ret = 0;
	u_long		flags;
	
	if ((hh->prim == PH_DATA_REQ) || (hh->prim == DL_DATA_REQ)) {
		spin_lock_irqsave(inst->hwlock, flags);
		ret = channel_senddata(chan, hh->dinfo, skb);
		if (ret > 0) { /* direct TX */
			tasklet_schedule(&xhfc->tasklet);
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
	xhfc_t *xhfc = NULL;
	mISDNinstance_t *inst = data;
	struct sk_buff *skb;
	int channel = -1;
	int i;
	channel_t *chan = NULL;
	u_long flags;

	if (!data) {
		MGR_HASPROTOCOL_HANDLER(prim, arg, &hw_mISDNObj)
		    printk(KERN_ERR "%s %s: no data prim %x arg %p\n",
			   xhfc->chip_name, __FUNCTION__, prim, arg);
		return (-EINVAL);
	}
	
	spin_lock_irqsave(&hw_mISDNObj.lock, flags);

	/* find channel and card */
	list_for_each_entry(xhfc, &hw_mISDNObj.ilist, list) {
		i = 0;
		while (i < MAX_CHAN) {
			if (xhfc->chan[i].ch.Flags &&
				&xhfc->chan[i].ch.inst == inst) {
				channel = i;
				chan = &xhfc->chan[i].ch;
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
				// release_card(xhfc);
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
			       xhfc->chip_name, __FUNCTION__, prim);
			return (-EINVAL);
	}
	return (0);
}

/***********************************/
/* check if new buffer for channel */
/* is waitinng is transmitt queue  */
/***********************************/
static int
next_tx_frame(xhfc_t * xhfc, __u8 channel)
{
	channel_t *ch = &xhfc->chan[channel].ch;

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
			       xhfc->chip_name, channel);
			test_and_clear_bit(FLG_TX_NEXT, &ch->Flags);
		}
	} else
		ch->tx_skb = NULL;
	test_and_clear_bit(FLG_TX_BUSY, &ch->Flags);
	return (0);
}

static inline void
xhfc_waitbusy(xhfc_t * xhfc)
{
	while (read_xhfc(xhfc, R_STATUS) & M_BUSY);
}

static inline void
xhfc_selfifo(xhfc_t * xhfc, __u8 fifo)
{
	write_xhfc(xhfc, R_FIFO, fifo);
	xhfc_waitbusy(xhfc);
}

static inline void
xhfc_inc_f(xhfc_t * xhfc)
{
	write_xhfc(xhfc, A_INC_RES_FIFO, M_INC_F);
	xhfc_waitbusy(xhfc);
}

static inline void
xhfc_resetfifo(xhfc_t * xhfc)
{
	write_xhfc(xhfc, A_INC_RES_FIFO, M_RES_FIFO | M_RES_FIFO_ERR);
	xhfc_waitbusy(xhfc);
}

/**************************/
/* fill fifo with TX data */
/**************************/
static void
xhfc_write_fifo(xhfc_t * xhfc, __u8 channel)
{
	__u8		fcnt, tcnt, i;
	__u8		free;
	__u8		f1, f2;
	__u8		fstat;
	__u8		*data;
	int		remain;
	channel_t	*ch = &xhfc->chan[channel].ch;


      send_buffer:
	if (!ch->tx_skb)
		return;
	remain = ch->tx_skb->len - ch->tx_idx;
	if (remain <= 0)
		return;      
      
	xhfc_selfifo(xhfc, (channel * 2));

	fstat = read_xhfc(xhfc, A_FIFO_STA);
	free = (xhfc->max_z - (read_xhfc(xhfc, A_USAGE)));
	tcnt = (free >= remain) ? remain : free;

	f1 = read_xhfc(xhfc, A_F1);
	f2 = read_xhfc(xhfc, A_F2);

	fcnt = 0x07 - ((f1 - f2) & 0x07);	/* free frame count in tx fifo */

	if (debug & DEBUG_HFC_FIFO) {
		mISDN_debugprint(&ch->inst,
				 "%s channel(%i) len(%i) idx(%i) f1(%i) f2(%i) fcnt(%i) tcnt(%i) free(%i) fstat(%i)",
				 __FUNCTION__, channel, ch->tx_skb->len, ch->tx_idx,
				 f1, f2, fcnt, tcnt, free, fstat);
	}

	/* check for fifo underrun during frame transmission */
	fstat = read_xhfc(xhfc, A_FIFO_STA);
	if (fstat & M_FIFO_ERR) {
		if (debug & DEBUG_HFC_FIFO_ERR) {
			mISDN_debugprint(&ch->inst,
					 "%s transmit fifo channel(%i) underrun idx(%i), A_FIFO_STA(0x%02x)",
					 __FUNCTION__, channel,
					 ch->tx_idx, fstat);
		}

		write_xhfc(xhfc, A_INC_RES_FIFO, M_RES_FIFO_ERR);

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
			       xhfc->chip_name, channel);

			i=0;
			while (i<tcnt)
				printk("%02x ", *(data+(i++)));
			printk ("\n");
		}
		
		/* write data to FIFO */
		i=0;
		while (i<tcnt) {
			if ((tcnt-i) >= 4) {
				write32_xhfc(xhfc, A_FIFO_DATA, *((__u32 *) (data+i)));
				i += 4;
			} else {
				write_xhfc(xhfc, A_FIFO_DATA, *(data+i));
				i++;
			}
		}

		if (ch->tx_idx == ch->tx_skb->len) {
			if (test_bit(FLG_HDLC, &ch->Flags)) {
				/* terminate frame */
				xhfc_inc_f(xhfc);
			} else {
				xhfc_selfifo(xhfc, (channel * 2));
			}

			/* check for fifo underrun during frame transmission */
			fstat = read_xhfc(xhfc, A_FIFO_STA);
			if (fstat & M_FIFO_ERR) {
				if (debug & DEBUG_HFC_FIFO_ERR) {
					mISDN_debugprint(&ch->inst,
							 "%s transmit fifo channel(%i) underrun "
							 "during transmission, A_FIFO_STA(0x%02x)\n",
							 __FUNCTION__,
							 channel,
							 fstat);
				}
				write_xhfc(xhfc, A_INC_RES_FIFO, M_RES_FIFO_ERR);

				if (test_bit(FLG_HDLC, &ch->Flags)) {
					// restart frame transmission
					ch->tx_idx = 0;
					goto send_buffer;
				}
			}
			
			if (next_tx_frame(xhfc, channel)) {
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
			xhfc_selfifo(xhfc, (channel * 2));
		}
	} 
}

/****************************/
/* read RX data out of fifo */
/****************************/
static void
xhfc_read_fifo(xhfc_t * xhfc, __u8 channel)
{
	__u8	f1=0, f2=0, z1=0, z2=0;
	__u8	fstat = 0;
	int	i;
	int	rcnt;		/* read rcnt bytes out of fifo */
	__u8	*data;		/* new data pointer */
	struct sk_buff	*skb;	/* data buffer for upper layer */
	channel_t	*ch = &xhfc->chan[channel].ch;

      receive_buffer:

	xhfc_selfifo(xhfc, (channel * 2) + 1);

	fstat = read_xhfc(xhfc, A_FIFO_STA);
	if (fstat & M_FIFO_ERR) {
		if (debug & DEBUG_HFC_FIFO_ERR)
			mISDN_debugprint(&ch->inst,
					 "RX fifo overflow channel(%i), "
					 "A_FIFO_STA(0x%02x) f0cnt(%i)",
					 channel, fstat, xhfc->f0_akku);
		write_xhfc(xhfc, A_INC_RES_FIFO, M_RES_FIFO_ERR);
	}

	if (test_bit(FLG_HDLC, &ch->Flags)) {
		/* hdlc rcnt */
		f1 = read_xhfc(xhfc, A_F1);
		f2 = read_xhfc(xhfc, A_F2);
		z1 = read_xhfc(xhfc, A_Z1);
		z2 = read_xhfc(xhfc, A_Z2);

		rcnt = (z1 - z2) & xhfc->max_z;
		if (f1 != f2)
			rcnt++;

	} else {
		/* transparent rcnt */
		rcnt = read_xhfc(xhfc, A_USAGE) - 1;
	}

	if (debug & DEBUG_HFC_FIFO) {
		if (ch->rx_skb)
			i = ch->rx_skb->len;
		else
			i = 0;
		mISDN_debugprint(&ch->inst, "reading %i bytes channel(%i) "
			"irq_cnt(%i) fstat(%i) idx(%i) f1(%i) f2(%i) z1(%i) z2(%i)",
			rcnt, channel, xhfc->irq_cnt, fstat, i, f1, f2, z1, z2);
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
				*((__u32 *) (data+i)) = read32_xhfc(xhfc, A_FIFO_DATA);
				i += 4;
			} else {
				*(data+i) = read_xhfc(xhfc, A_FIFO_DATA);
				i++;
			}
		}		
	} else
		return;
		

	if (test_bit(FLG_HDLC, &ch->Flags)) {
		if (f1 != f2) {
			xhfc_inc_f(xhfc);

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
			if (read_xhfc(xhfc, A_USAGE) > 8) {
				if (debug & DEBUG_HFC_FIFO)
					mISDN_debugprint(&ch->inst,
							 "%s: channel(%i) continue xhfc_read_fifo",
							 __FUNCTION__,
							 channel);
				goto receive_buffer;
			}
			return;


		} else {
			xhfc_selfifo(xhfc, (channel * 2) + 1);
		}
	} else {
		xhfc_selfifo(xhfc, (channel * 2) + 1);
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
	xhfc_t 	*xhfc = (xhfc_t *) ul_hw;
	int		i;
	reg_a_su_rd_sta	su_state;
	channel_t	*dch;

	/* timer interrupt */
	if (xhfc->misc_irq.bit.v_ti_irq) {
		xhfc->misc_irq.bit.v_ti_irq = 0;

		/* Handle tx Fifos */
		for (i = 0; i < xhfc->max_fifo; i++) {
			if ((1 << (i * 2)) & (xhfc->fifo_irqmsk)) {
				xhfc->fifo_irq &= ~(1 << (i * 2));
				if (test_bit(FLG_TX_BUSY, &xhfc->chan[i].ch.Flags))
					xhfc_write_fifo(xhfc, i);
			}
		}
		
		/* handle NT Timer */
		for (i = 0; i < xhfc->num_ports; i++) {
			if ((xhfc->port[i].mode & PORT_MODE_NT)
			    && (xhfc->port[i].mode & NT_TIMER)) {
				if ((--xhfc->port[i].nt_timer) < 0)
					su_new_state(&xhfc->chan[(i << 2) + 2].ch);
			}
		}
	}

	/* set fifo_irq when RX data over treshold */
	for (i = 0; i < xhfc->num_ports; i++) {
		xhfc->fifo_irq |= read_xhfc(xhfc, R_FILL_BL0 + i) << (i * 8);
	}

	/* Handle rx Fifos */
	if ((xhfc->fifo_irq & xhfc->fifo_irqmsk) & FIFO_MASK_RX) {
		for (i = 0; i < xhfc->max_fifo; i++) {
			if ((xhfc->fifo_irq & (1 << (i * 2 + 1)))
			    & (xhfc->fifo_irqmsk)) {

				xhfc->fifo_irq &= ~(1 << (i * 2 + 1));
				xhfc_read_fifo(xhfc, i);
			}
		}
	}

	/* su interrupt */
	if (xhfc->su_irq.reg & xhfc->su_irqmsk.reg) {
		xhfc->su_irq.reg = 0;
		for (i = 0; i < xhfc->num_ports; i++) {
			write_xhfc(xhfc, R_SU_SEL, i);
			su_state.reg = read_xhfc(xhfc, A_SU_RD_STA);
			
			dch = &xhfc->chan[(i << 2) + 2].ch;
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
	xhfc_pi *pi = dev_id;
	xhfc_t * xhfc = NULL;
	__u8 i;
	__u32 f0_cnt;
	__u32 xhfc_irqs;

	xhfc_irqs = 0;
	for (i=0; i<pi->driver_data.num_xhfcs; i++) {
		xhfc = &pi->xhfc[i];
		if (xhfc->irq_ctrl.bit.v_glob_irq_en && (read_xhfc(xhfc, R_IRQ_OVIEW)))
		    	xhfc_irqs |= (1 << i);
	}
	if (!xhfc_irqs) {
		if (debug & DEBUG_HFC_IRQ)
			printk(KERN_INFO
			       "%s %s NOT M_GLOB_IRQ_EN or R_IRQ_OVIEW \n",
			       xhfc->chip_name, __FUNCTION__);
		return IRQ_NONE;
	}

	xhfc_irqs = 0;
	for (i=0; i<pi->driver_data.num_xhfcs; i++) {
		xhfc = &pi->xhfc[i];

		xhfc->misc_irq.reg |= read_xhfc(xhfc, R_MISC_IRQ);
		xhfc->su_irq.reg |= read_xhfc(xhfc, R_SU_IRQ);

		/* get fifo IRQ states in bundle */
		for (i = 0; i < 4; i++) {
			xhfc->fifo_irq |=
			    (read_xhfc(xhfc, R_FIFO_BL0_IRQ + i) << (i * 8));
		}

		/* call bottom half at events
		 *   - Timer Interrupt (or other misc_irq sources)
		 *   - SU State change
		 *   - Fifo FrameEnd interrupts (only at rx fifos enabled)
		 */
		if ((xhfc->misc_irq.reg & xhfc->misc_irqmsk.reg)
		      || (xhfc->su_irq.reg & xhfc->su_irqmsk.reg)
		      || (xhfc->fifo_irq & xhfc->fifo_irqmsk)) {
		      	
		      	
			xhfc_irqs |= (1 << i);
	
			/* queue bottom half */
			if (!(xhfc->testirq))
				tasklet_schedule(&xhfc->tasklet);

			/* count irqs */
			xhfc->irq_cnt++;

#ifdef USE_F0_COUNTER	
			/* akkumulate f0 counter diffs */
			f0_cnt = read_xhfc(xhfc, R_F0_CNTL);
			f0_cnt += read_xhfc(xhfc, R_F0_CNTH) << 8;
			xhfc->f0_akku += (f0_cnt - xhfc->f0_cnt);
			if ((f0_cnt - xhfc->f0_cnt) < 0)
				xhfc->f0_akku += 0xFFFF;
			xhfc->f0_cnt = f0_cnt;
		}
#endif
	}
	
	return ((xhfc_irqs)?IRQ_HANDLED:IRQ_NONE);
}

/*****************************************************/
/* disable all interrupts by disabling M_GLOB_IRQ_EN */
/*****************************************************/
static void
disable_interrupts(xhfc_t * xhfc)
{
	if (debug & DEBUG_HFC_IRQ)
		printk(KERN_INFO "%s %s\n", xhfc->chip_name, __FUNCTION__);
	xhfc->irq_ctrl.bit.v_glob_irq_en = 0;
	write_xhfc(xhfc, R_IRQ_CTRL, xhfc->irq_ctrl.reg);
}

/******************************************/
/* start interrupt and set interrupt mask */
/******************************************/
static void
enable_interrupts(xhfc_t * xhfc)
{
	if (debug & DEBUG_HFC_IRQ)
		printk(KERN_INFO "%s %s\n", xhfc->chip_name, __FUNCTION__);

	write_xhfc(xhfc, R_SU_IRQMSK, xhfc->su_irqmsk.reg);

	/* use defined timer interval */
	write_xhfc(xhfc, R_TI_WD, xhfc->ti_wd.reg);
	xhfc->misc_irqmsk.bit.v_ti_irqmsk = 1;
	write_xhfc(xhfc, R_MISC_IRQMSK, xhfc->misc_irqmsk.reg);

	/* clear all pending interrupts bits */
	read_xhfc(xhfc, R_MISC_IRQ);
	read_xhfc(xhfc, R_SU_IRQ);
	read_xhfc(xhfc, R_FIFO_BL0_IRQ);
	read_xhfc(xhfc, R_FIFO_BL1_IRQ);
	read_xhfc(xhfc, R_FIFO_BL2_IRQ);
	read_xhfc(xhfc, R_FIFO_BL3_IRQ);

	/* enable global interrupts */
	xhfc->irq_ctrl.bit.v_glob_irq_en = 1;
	xhfc->irq_ctrl.bit.v_fifo_irq_en = 1;
	write_xhfc(xhfc, R_IRQ_CTRL, xhfc->irq_ctrl.reg);
}

/***********************************/
/* initialise the XHFC ISDN Chip   */
/* return 0 on success.            */
/***********************************/
static int
init_xhfc(xhfc_t * xhfc)
{
	int err = 0;
	int timeout = 0x2000;
	__u8 chip_id;

	chip_id = read_xhfc(xhfc, R_CHIP_ID);
	switch (chip_id) {
		case CHIP_ID_1SU:
			xhfc->num_ports = 1;
			xhfc->max_fifo = 4;
			xhfc->max_z = 0xFF;
			xhfc->ti_wd.bit.v_ev_ts = 0x6;	/* timer irq interval 16 ms */
			write_xhfc(xhfc, R_FIFO_MD, M1_FIFO_MD * 2);
			xhfc->su_irqmsk.bit.v_su0_irqmsk = 1;
			sprintf(xhfc->chip_name, "%s_PI%d_%i",
			        CHIP_NAME_1SU,
			        xhfc->pi->cardnum,
			        xhfc->chipidx);
			break;

		case CHIP_ID_2SU:
			xhfc->num_ports = 2;
			xhfc->max_fifo = 8;
			xhfc->max_z = 0x7F;
			xhfc->ti_wd.bit.v_ev_ts = 0x5;	/* timer irq interval 8 ms */
			write_xhfc(xhfc, R_FIFO_MD, M1_FIFO_MD * 1);
			xhfc->su_irqmsk.bit.v_su0_irqmsk = 1;
			xhfc->su_irqmsk.bit.v_su1_irqmsk = 1;
			sprintf(xhfc->chip_name, "%s_PI%d_%i",
			        CHIP_NAME_2SU,
			        xhfc->pi->cardnum,
			        xhfc->chipidx);
			break;

		case CHIP_ID_2S4U:
			xhfc->num_ports = 4;
			xhfc->max_fifo = 16;
			xhfc->max_z = 0x3F;
			xhfc->ti_wd.bit.v_ev_ts = 0x4;	/* timer irq interval 4 ms */
			write_xhfc(xhfc, R_FIFO_MD, M1_FIFO_MD * 0);
			xhfc->su_irqmsk.bit.v_su0_irqmsk = 1;
			xhfc->su_irqmsk.bit.v_su1_irqmsk = 1;
			xhfc->su_irqmsk.bit.v_su2_irqmsk = 1;
			xhfc->su_irqmsk.bit.v_su3_irqmsk = 1;		
			sprintf(xhfc->chip_name, "%s_PI%d_%i",
			        CHIP_NAME_2S4U,
			        xhfc->pi->cardnum,
			        xhfc->chipidx);

		case CHIP_ID_4SU:
			xhfc->num_ports = 4;
			xhfc->max_fifo = 16;
			xhfc->max_z = 0x3F;
			xhfc->ti_wd.bit.v_ev_ts = 0x4;	/* timer irq interval 4 ms */
			write_xhfc(xhfc, R_FIFO_MD, M1_FIFO_MD * 0);
			xhfc->su_irqmsk.bit.v_su0_irqmsk = 1;
			xhfc->su_irqmsk.bit.v_su1_irqmsk = 1;
			xhfc->su_irqmsk.bit.v_su2_irqmsk = 1;
			xhfc->su_irqmsk.bit.v_su3_irqmsk = 1;
			sprintf(xhfc->chip_name, "%s_PI%d_%i",
			        CHIP_NAME_4SU,
			        xhfc->pi->cardnum,
			        xhfc->chipidx);
			break;
		default:
			err = -ENODEV;
	}

	if (err) {
		if (debug & DEBUG_HFC_INIT)
			printk(KERN_ERR "%s %s: unkown Chip ID 0x%x\n",
			       xhfc->chip_name, __FUNCTION__, chip_id);
		return (err);
	} else {
		if (debug & DEBUG_HFC_INIT)
			printk(KERN_INFO "%s ChipID: 0x%x\n",
			       xhfc->chip_name, chip_id);
	}
	
	/* software reset to enable R_FIFO_MD setting */
	write_xhfc(xhfc, R_CIRM, M_SRES);
	udelay(5);
	write_xhfc(xhfc, R_CIRM, 0);
	
	/* amplitude */
	write_xhfc(xhfc, R_PWM_MD, 0x80);
	write_xhfc(xhfc, R_PWM1, 0x18);

	write_xhfc(xhfc, R_FIFO_THRES, 0x11);

	while ((read_xhfc(xhfc, R_STATUS) & (M_BUSY | M_PCM_INIT))
	       && (timeout))
		timeout--;

	if (!(timeout)) {
		if (debug & DEBUG_HFC_INIT)
			printk(KERN_ERR
			       "%s %s: initialization sequence could not finish\n",
			       xhfc->chip_name, __FUNCTION__);
		return (-ENODEV);
	}

	/* set PCM master mode */
	xhfc->pcm_md0.bit.v_pcm_md = 1;
	write_xhfc(xhfc, R_PCM_MD0, xhfc->pcm_md0.reg);

	/* set pll adjust */
	xhfc->pcm_md0.bit.v_pcm_idx = IDX_PCM_MD1;
	xhfc->pcm_md1.bit.v_pll_adj = 3;
	write_xhfc(xhfc, R_PCM_MD0, xhfc->pcm_md0.reg);
	write_xhfc(xhfc, R_PCM_MD1, xhfc->pcm_md1.reg);


	/* perfom short irq test */
	xhfc->testirq=1;
	enable_interrupts(xhfc);
	mdelay(1 << xhfc->ti_wd.bit.v_ev_ts);
	disable_interrupts(xhfc);
	
	if (xhfc->irq_cnt > 2) {
		xhfc->testirq = 0;
		return (0);
	} else {
		if (debug & DEBUG_HFC_INIT)
			printk(KERN_INFO
			       "%s %s: ERROR getting IRQ (irq_cnt %i)\n",
			       xhfc->chip_name, __FUNCTION__, xhfc->irq_cnt);
		return (-EIO);
	}
}

/*************************************/
/* free memory for all used channels */
/*************************************/
static void
release_channels(xhfc_t * xhfc)
{
	int i = 0;

	while (i < MAX_CHAN) {
		if (xhfc->chan[i].ch.Flags) {
			if (debug & DEBUG_HFC_INIT)
				printk(KERN_DEBUG "%s %s: free channel %d\n",
					xhfc->chip_name, __FUNCTION__, i);
			mISDN_freechannel(&xhfc->chan[i].ch);
			hw_mISDNObj.ctrl(&xhfc->chan[i].ch.inst, MGR_UNREGLAYER | REQUEST, NULL);
		}
		i++;
	}
	
	if (xhfc->chan)
		kfree(xhfc->chan);
	if (xhfc->port)
		kfree(xhfc->port);
}

/*********************************************/
/* setup port (line interface) with SU_CRTLx */
/*********************************************/
static void
init_su(xhfc_t * xhfc, __u8 pt)
{
	xhfc_port_t *port = &xhfc->port[pt];

	if (debug & DEBUG_HFC_MODE)
		printk(KERN_INFO "%s %s port(%i)\n", xhfc->chip_name,
		       __FUNCTION__, pt);

	write_xhfc(xhfc, R_SU_SEL, pt);

	if (port->mode & PORT_MODE_NT)
		port->su_ctrl0.bit.v_su_md = 1;

	if (port->mode & PORT_MODE_EXCH_POL) 
		port->su_ctrl2.reg = M_SU_EXCHG;

	if (port->mode & PORT_MODE_UP) {
		port->st_ctrl3.bit.v_st_sel = 1;
		write_xhfc(xhfc, A_MS_TX, 0x0F);
		port->su_ctrl0.bit.v_st_sq_en = 1;
	}

	if (debug & DEBUG_HFC_MODE)
		printk(KERN_INFO "%s %s su_ctrl0(0x%02x) "
		       "su_ctrl1(0x%02x) "
		       "su_ctrl2(0x%02x) "
		       "st_ctrl3(0x%02x)\n",
		       xhfc->chip_name, __FUNCTION__,
		       port->su_ctrl0.reg,
		       port->su_ctrl1.reg,
		       port->su_ctrl2.reg,
		       port->st_ctrl3.reg);

	write_xhfc(xhfc, A_ST_CTRL3, port->st_ctrl3.reg);
	write_xhfc(xhfc, A_SU_CTRL0, port->su_ctrl0.reg);
	write_xhfc(xhfc, A_SU_CTRL1, port->su_ctrl1.reg);
	write_xhfc(xhfc, A_SU_CTRL2, port->su_ctrl2.reg);
	
	if (port->mode & PORT_MODE_TE)
		write_xhfc(xhfc, A_SU_CLK_DLY, CLK_DLY_TE);
	else
		write_xhfc(xhfc, A_SU_CLK_DLY, CLK_DLY_NT);

	write_xhfc(xhfc, A_SU_WR_STA, 0);
}

/*********************************************************/
/* Setup Fifo using A_CON_HDLC, A_SUBCH_CFG, A_FIFO_CTRL */
/*********************************************************/
static void
setup_fifo(xhfc_t * xhfc, __u8 fifo, __u8 conhdlc, __u8 subcfg,
	   __u8 fifoctrl, __u8 enable)
{
	xhfc_selfifo(xhfc, fifo);
	write_xhfc(xhfc, A_CON_HDLC, conhdlc);
	write_xhfc(xhfc, A_SUBCH_CFG, subcfg);
	write_xhfc(xhfc, A_FIFO_CTRL, fifoctrl);

	if (enable)
		xhfc->fifo_irqmsk |= (1 << fifo);
	else
		xhfc->fifo_irqmsk &= ~(1 << fifo);

	xhfc_resetfifo(xhfc);
	xhfc_selfifo(xhfc, fifo);

	if (debug & DEBUG_HFC_MODE) {
		printk(KERN_INFO
		       "%s %s: fifo(%i) conhdlc(0x%02x) "
		       "subcfg(0x%02x) fifoctrl(0x%02x)\n",
		       xhfc->chip_name, __FUNCTION__, fifo,
		       sread_xhfc(xhfc, A_CON_HDLC),
		       sread_xhfc(xhfc, A_SUBCH_CFG),
		       sread_xhfc(xhfc,  A_FIFO_CTRL)
		    );
	}
}

/**************************************************/
/* Setup S/U interface, enable/disable B-Channels */
/**************************************************/
static void
setup_su(xhfc_t * xhfc, __u8 pt, __u8 bc, __u8 enable)
{
	xhfc_port_t *port = &xhfc->port[pt];

	if (!((bc == 0) || (bc == 1))) {
		printk(KERN_INFO "%s %s: pt(%i) ERROR: bc(%i) unvalid!\n",
		       xhfc->chip_name, __FUNCTION__, pt, bc);
		return;
	}

	if (debug & DEBUG_HFC_MODE)
		printk(KERN_INFO "%s %s %s pt(%i) bc(%i)\n",
		       xhfc->chip_name, __FUNCTION__,
		       (enable) ? ("enable") : ("disable"), pt, bc);

	if (bc) {
		port->su_ctrl2.bit.v_b2_rx_en = (enable?1:0);
		port->su_ctrl0.bit.v_b2_tx_en = (enable?1:0);
	} else {
		port->su_ctrl2.bit.v_b1_rx_en = (enable?1:0);
		port->su_ctrl0.bit.v_b1_tx_en = (enable?1:0);
	}

	if (xhfc->port[pt].mode & PORT_MODE_NT)
		xhfc->port[pt].su_ctrl0.bit.v_su_md = 1;

	write_xhfc(xhfc, R_SU_SEL, pt);
	write_xhfc(xhfc, A_SU_CTRL0, xhfc->port[pt].su_ctrl0.reg);
	write_xhfc(xhfc, A_SU_CTRL2, xhfc->port[pt].su_ctrl2.reg);
}

/*********************************************/
/* (dis-) connect D/B-Channel using protocol */
/*********************************************/
static int
setup_channel(xhfc_t * xhfc, __u8 channel, int protocol)
{
	xhfc_port_t *port = xhfc->chan[channel].port;
	
	if (test_bit(FLG_BCHANNEL, &xhfc->chan[channel].ch.Flags)) {
		if (debug & DEBUG_HFC_MODE)
			mISDN_debugprint(&xhfc->chan[channel].ch.inst,
					 "channel(%i) protocol %x-->%x",
					 channel,
					 xhfc->chan[channel].ch.state,
					 protocol);

		switch (protocol) {
			case (-1):	/* used for init */
				xhfc->chan[channel].ch.state = -1;
				xhfc->chan[channel].ch.channel = channel;
				/* fall trough */
			case (ISDN_PID_NONE):
				if (debug & DEBUG_HFC_MODE)
					mISDN_debugprint(&xhfc->
							 chan[channel].ch.inst,
							 "ISDN_PID_NONE");
				if (xhfc->chan[channel].ch.state == ISDN_PID_NONE)
					return (0);	/* already in idle state */
				xhfc->chan[channel].ch.state = ISDN_PID_NONE;

				setup_fifo(xhfc, (channel << 1),     4, 0, 0, 0);	/* B-TX fifo */
				setup_fifo(xhfc, (channel << 1) + 1, 4, 0, 0, 0);	/* B-RX fifo */

				setup_su(xhfc, port->idx, (channel % 4) ? 1 : 0, 0);
				
				test_and_clear_bit(FLG_HDLC, &xhfc->chan[channel].ch.Flags);
				test_and_clear_bit(FLG_TRANSPARENT, &xhfc->chan[channel].ch.Flags);

				break;

			case (ISDN_PID_L1_B_64TRANS):
				if (debug & DEBUG_HFC_MODE)
					mISDN_debugprint(&xhfc->chan[channel].ch.inst,
							 "ISDN_PID_L1_B_64TRANS");
				setup_fifo(xhfc, (channel << 1), 6, 0, 0, 1);	/* B-TX Fifo */
				setup_fifo(xhfc, (channel << 1) + 1, 6, 0, 0, 1);	/* B-RX Fifo */

				setup_su(xhfc, port->idx, (channel % 4) ? 1 : 0, 1);

				xhfc->chan[channel].ch.state = ISDN_PID_L1_B_64TRANS;
				test_and_set_bit(FLG_TRANSPARENT, &xhfc->chan[channel].ch.Flags);

				break;

			case (ISDN_PID_L1_B_64HDLC):
				if (debug & DEBUG_HFC_MODE)
					mISDN_debugprint(&xhfc->chan[channel].ch.inst,
							 "ISDN_PID_L1_B_64HDLC");
				setup_fifo(xhfc, (channel << 1), 4, 0, M_FR_ABO, 1);	// TX Fifo
				setup_fifo(xhfc, (channel << 1) + 1, 4, 0, M_FR_ABO | M_FIFO_IRQMSK, 1);	// RX Fifo

				setup_su(xhfc, port->idx, (channel % 4) ? 1 : 0, 1);

				xhfc->chan[channel].ch.state = ISDN_PID_L1_B_64HDLC;
				test_and_set_bit(FLG_HDLC, &xhfc->chan[channel].ch.Flags);

				break;
			default:
				mISDN_debugprint(&xhfc->chan[channel].ch.inst,
					"prot not known %x",
					protocol);
				return (-ENOPROTOOPT);
		}
		return (0);
	}
	else if (test_bit(FLG_DCHANNEL, &xhfc->chan[channel].ch.Flags)) {
		if (debug & DEBUG_HFC_MODE)
			mISDN_debugprint(&xhfc->chan[channel].ch.inst,
					 "channel(%i) protocol(%i)",
					 channel, protocol);

		setup_fifo(xhfc, (channel << 1), 5, 2, M_FR_ABO, 1);	/* D TX fifo */
		setup_fifo(xhfc, (channel << 1) + 1, 5, 2, M_FR_ABO | M_FIFO_IRQMSK, 1);	/* D RX fifo */

		return (0);
	}

	printk(KERN_INFO
	       "%s %s ERROR: channel(%i) is NEITHER B nor D !!!\n",
	       xhfc->chip_name, __FUNCTION__, channel);

	return (-1);
}

/*******************************************************/
/* register ISDN stack for one XHFC card               */
/*   - register all ports and channels                 */
/*   - set param_idx                                   */
/*                                                     */
/*  channel mapping in mISDN in xhfc->chan[MAX_CHAN]:  */
/*    1st line interf:  0=B1,  1=B2,  2=D,  3=PCM      */
/*    2nd line interf:  4=B1,  5=B2,  6=D,  7=PCM      */
/*    3rd line interf:  8=B1,  9=B2, 10=D, 11=PCM      */
/*    4th line interf; 12=B1, 13=B2, 14=D, 15=PCM      */
/*******************************************************/
static int
init_mISDN_channels(xhfc_t * xhfc)
{
	int err;
	int pt;		/* ST/U port index */
	int ch_idx;	/* channel index */
	int b;
	channel_t *ch;
	mISDN_pid_t pid;
	u_long flags;
	
	for (pt = 0; pt < xhfc->num_ports; pt++) {
		/* init D channels */
		ch_idx = (pt << 2) + 2;
		if (debug & DEBUG_HFC_INIT)
			printk(KERN_INFO
			       "%s %s: Registering D-channel, card(%d) "
			       "ch(%d) port(%d) protocol(%x)\n",
			       xhfc->chip_name, __FUNCTION__, xhfc->chipnum,
			       ch_idx, pt, xhfc->port[pt].dpid);

		xhfc->port[pt].idx = pt;
		xhfc->chan[ch_idx].port = &xhfc->port[pt];
		ch = &xhfc->chan[ch_idx].ch;
		
		memset(ch, 0, sizeof(channel_t));
		ch->channel = ch_idx;
		ch->debug = debug;
		ch->inst.obj = &hw_mISDNObj;
		ch->inst.hwlock = &xhfc->lock;
		ch->inst.class_dev.dev = &xhfc->pi->pdev->dev;
		mISDN_init_instance(&ch->inst, &hw_mISDNObj, xhfc, xhfc_l2l1);
		ch->inst.pid.layermask = ISDN_LAYER(0);
		sprintf(ch->inst.name, "%s_%d_D", xhfc->chip_name, pt);
		err = mISDN_initchannel(ch, MSK_INIT_DCHANNEL, MAX_DFRAME_LEN_L1);
		if (err)
			goto free_channels;
		ch->hw = xhfc;
		
		/* init t3 timer */
		init_timer(&xhfc->port[pt].t3_timer);
		xhfc->port[pt].t3_timer.data = (long) ch;
		xhfc->port[pt].t3_timer.function = (void *) l1_timer_expire_t3;

		/* init t4 timer */
		init_timer(&xhfc->port[pt].t4_timer);
		xhfc->port[pt].t4_timer.data = (long) ch;
		xhfc->port[pt].t4_timer.function = (void *) l1_timer_expire_t4;

		/* init B channels */
		for (b = 0; b < 2; b++) {
			ch_idx = (pt << 2) + b;
			if (debug & DEBUG_HFC_INIT)
				printk(KERN_DEBUG
				       "%s %s: Registering B-channel, card(%d) "
				       "ch(%d) port(%d)\n", xhfc->chip_name,
				       __FUNCTION__, xhfc->chipnum, ch_idx, pt);

			xhfc->chan[ch_idx].port = &xhfc->port[pt];
			ch = &xhfc->chan[ch_idx].ch;
			
			memset(ch, 0, sizeof(channel_t));
			ch->channel = ch_idx;
			ch->debug = debug;
			mISDN_init_instance(&ch->inst, &hw_mISDNObj, xhfc, xhfc_l2l1);
			ch->inst.pid.layermask = ISDN_LAYER(0);
			ch->inst.hwlock = &xhfc->lock;
			ch->inst.class_dev.dev = &xhfc->pi->pdev->dev;			
			
			sprintf(ch->inst.name, "%s_%d_B%d",
				xhfc->chip_name, pt, b + 1);

			if (mISDN_initchannel(ch, MSK_INIT_BCHANNEL, MAX_DATA_MEM)) {
				err = -ENOMEM;
				goto free_channels;
			}
			ch->hw = xhfc;
		}
		
		/* clear PCM */
		memset(&xhfc->chan[(pt << 2) + 3], 0, sizeof(channel_t));

		mISDN_set_dchannel_pid(&pid, xhfc->port[pt].dpid,
				       layermask[xhfc->param_idx + pt]);

		/* register D Channel */
		ch = &xhfc->chan[(pt << 2) + 2].ch;
		
		/* set protocol for NT/TE */
		if (xhfc->port[pt].mode & PORT_MODE_NT) {
			/* NT-mode */
			xhfc->port[xhfc->param_idx + pt].mode |= NT_TIMER;
			xhfc->port[xhfc->param_idx + pt].nt_timer = 0;

			ch->inst.pid.protocol[0] = ISDN_PID_L0_NT_S0;
			ch->inst.pid.protocol[1] = ISDN_PID_L1_NT_S0;
			pid.protocol[0] = ISDN_PID_L0_NT_S0;
			pid.protocol[1] = ISDN_PID_L1_NT_S0;
			ch->inst.pid.layermask |= ISDN_LAYER(1);
			pid.layermask |= ISDN_LAYER(1);
			if (layermask[xhfc->param_idx + pt] & ISDN_LAYER(2))
				pid.protocol[2] = ISDN_PID_L2_LAPD_NET;
		} else {
			/* TE-mode */
			xhfc->port[xhfc->param_idx + pt].mode |= PORT_MODE_TE;
			ch->inst.pid.protocol[0] = ISDN_PID_L0_TE_S0;
			ch->inst.pid.protocol[1] = ISDN_PID_L1_TE_S0;
			pid.protocol[0] = ISDN_PID_L0_TE_S0;
			pid.protocol[1] = ISDN_PID_L1_TE_S0;
		}

		if (debug & DEBUG_HFC_INIT)
			printk(KERN_INFO
			       "%s %s: registering Stack for Port %i\n",
			       xhfc->chip_name, __FUNCTION__, pt);

		/* register stack */
		err = hw_mISDNObj.ctrl(NULL, MGR_NEWSTACK | REQUEST, &ch->inst);
		if (err) {
			printk(KERN_ERR
			       "%s %s: MGR_NEWSTACK | REQUEST  err(%d)\n",
			       xhfc->chip_name, __FUNCTION__, err);
			goto free_channels;
		}
		ch->state = 0;

		/* attach two BChannels to this DChannel (ch) */
		for (b = 0; b < 2; b++) {
			err = hw_mISDNObj.ctrl(ch->inst.st,
				MGR_NEWSTACK | REQUEST,
				&xhfc->chan[(pt << 2) + b].ch.inst);
			if (err) {
				printk(KERN_ERR
				       "%s %s: MGR_ADDSTACK bchan error %d\n",
				       xhfc->chip_name, __FUNCTION__, err);
				goto free_stack;
			}
		}

		err = hw_mISDNObj.ctrl(ch->inst.st, MGR_SETSTACK | REQUEST, &pid);

		if (err) {
			printk(KERN_ERR
			       "%s %s: MGR_SETSTACK REQUEST dch err(%d)\n",
			       xhfc->chip_name, __FUNCTION__, err);
			hw_mISDNObj.ctrl(ch->inst.st,
					 MGR_DELSTACK | REQUEST, NULL);
			goto free_stack;
		}

		/* initial setup of each channel */
		setup_channel(xhfc, ch->channel, -1);
		for (b = 0; b < 2; b++)
			setup_channel(xhfc, (pt << 2) + b, -1);

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
	release_channels(xhfc);
	list_del(&xhfc->list);
	spin_unlock_irqrestore(&hw_mISDNObj.lock, flags);

	return (err);
}

/********************************/
/* parse module paramaters like */
/* NE/TE and S0/Up port mode    */
/********************************/
static void
parse_module_params(xhfc_t * xhfc)
{
	__u8 pt;

	/* parse module parameters */
	for (pt = 0; pt < xhfc->num_ports; pt++) {
		/* D-Channel protocol: (2=DSS1) */
		xhfc->port[pt].dpid = (protocol[xhfc->param_idx + pt] & 0x0F);
		if (xhfc->port[pt].dpid == 0) {
			printk(KERN_INFO
			       "%s %s: WARNING: wrong value for protocol[%i], "
			       "assuming 0x02 (DSS1)...\n",
			       xhfc->chip_name, __FUNCTION__,
			       xhfc->param_idx + pt);
			xhfc->port[pt].dpid = 0x02;
		}

		/* Line Interface TE or NT */
		if (protocol[xhfc->param_idx + pt] & 0x10)
			xhfc->port[pt].mode |= PORT_MODE_NT;
		else
			xhfc->port[pt].mode |= PORT_MODE_TE;

		/* Line Interface in S0 or Up mode */
		if (protocol[xhfc->param_idx + pt] & 0x20)
			xhfc->port[pt].mode |= PORT_MODE_UP;
		else
			xhfc->port[pt].mode |= PORT_MODE_S0;

		/* st line polarity */
		if (protocol[xhfc->param_idx + pt] & 0x40)
			xhfc->port[pt].mode |= PORT_MODE_EXCH_POL;
			
		/* link B-channel loop */
		if (protocol[xhfc->param_idx + pt] & 0x80)
			xhfc->port[pt].mode |= PORT_MODE_LOOP;
		

		if (debug & DEBUG_HFC_INIT)
			printk ("%s %s: protocol[%i]=0x%02x, dpid=%d, mode:%s,%s %s %s\n",
			        xhfc->chip_name, __FUNCTION__, xhfc->param_idx+pt,
			        protocol[xhfc->param_idx + pt],
			        xhfc->port[pt].dpid,
			        (xhfc->port[pt].mode & PORT_MODE_TE)?"TE":"NT",
			        (xhfc->port[pt].mode & PORT_MODE_S0)?"S0":"Up",
			        (xhfc->port[pt].mode & PORT_MODE_EXCH_POL)?"SU_EXCH":"",
			        (xhfc->port[pt].mode & PORT_MODE_LOOP)?"B-LOOP":""
			        );
	}
}

/********************************/
/* initialise the XHFC hardware */
/* return 0 on success.         */
/********************************/
static int __devinit
setup_instance(xhfc_t * xhfc)
{
	int err;
	int pt;
	xhfc_t *previous_hw;
	xhfc_port_t * port = NULL;
	xhfc_chan_t * chan = NULL;
	u_long flags;


	spin_lock_init(&xhfc->lock);
	tasklet_init(&xhfc->tasklet, xhfc_bh_handler, (unsigned long) xhfc);

	/* search previous instances to index protocol[] array */
	list_for_each_entry(previous_hw, &hw_mISDNObj.ilist, list) {
		xhfc->param_idx += previous_hw->num_ports;
	}

	spin_lock_irqsave(&hw_mISDNObj.lock, flags);
	/* add this instance to hardware list */
	list_add_tail(&xhfc->list, &hw_mISDNObj.ilist);
	spin_unlock_irqrestore(&hw_mISDNObj.lock, flags);

	err = init_xhfc(xhfc);
	if (err)
		goto out;

	/* alloc mem for all ports and channels */
	err = -ENOMEM;
	port = kmalloc(sizeof(xhfc_port_t) * xhfc->num_ports, GFP_KERNEL);
	if (port) {
		xhfc->port = port;
		memset(xhfc->port, 0, sizeof(xhfc_port_t) * xhfc->num_ports);
		chan = kmalloc(sizeof(xhfc_chan_t) * xhfc->num_ports * CHAN_PER_PORT, GFP_KERNEL);
		if (chan) {
			xhfc->chan = chan;
			memset(xhfc->chan, 0, sizeof(xhfc_chan_t) * xhfc->num_ports * CHAN_PER_PORT);
			err = 0;
		} else {
			printk(KERN_ERR "%s %s: No kmem for xhfc_chan_t*%i \n",
			       xhfc->chip_name, __FUNCTION__, xhfc->num_ports * CHAN_PER_PORT);	
			goto out;
		}
	} else {
		printk(KERN_ERR "%s %s: No kmem for xhfc_port_t*%i \n",
		       xhfc->chip_name, __FUNCTION__, xhfc->num_ports);	
		goto out;	
	}
	
	parse_module_params(xhfc);

	/* init line interfaces (ports) */
	for (pt = 0; pt < xhfc->num_ports; pt++)
		init_su(xhfc, pt);

	/* register all channels at ISDN procol stack */
	err = init_mISDN_channels(xhfc);
	if (err)
		goto out;

	/* delay some time */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((100 * HZ) / 1000);	/* Timeout 100ms */

	enable_interrupts(xhfc);

	/* force initial layer1 statechanges */
	xhfc->su_irq.reg = xhfc->su_irqmsk.reg;

	/* init B cbannel loops if desired */
	for (pt = 0; pt < xhfc->num_ports; pt++) {
		if (xhfc->port[pt].mode & PORT_MODE_LOOP) {
		        if (debug & DEBUG_HFC_INIT)
	        	        printk(KERN_INFO "%s %s init B-channel loop in port(%i)\n",
	        	               xhfc->chip_name, __FUNCTION__, pt);
	        	               
			xhfc_ph_command(&xhfc->chan[(pt << 2) + 2].ch, HFC_L1_TESTLOOP_B1);
			xhfc_ph_command(&xhfc->chan[(pt << 2) + 2].ch, HFC_L1_TESTLOOP_B2);
		}
	}

	return (0);

      out:
	if (xhfc->chan)
		kfree(xhfc->chan);
	if (xhfc->port)
		kfree(xhfc->port);
	return (err);
}

/************************/
/* release single card  */
/************************/
static void
release_card(xhfc_pi * pi)
{
	u_long	flags;
	__u8 i;

	for (i=0; i<pi->driver_data.num_xhfcs; i++)
		disable_interrupts(&pi->xhfc[i]);

	free_irq(pi->irq, pi);

	/* wait for pending tasklet to finish */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((100 * HZ) / 1000);	/* Timeout 100ms */

	spin_lock_irqsave(&hw_mISDNObj.lock, flags);
	for (i=0; i<pi->driver_data.num_xhfcs; i++) {
		release_channels(&pi->xhfc[i]);
		list_del(&pi->xhfc[i].list);
	}
	spin_unlock_irqrestore(&hw_mISDNObj.lock, flags);

	kfree(pi->xhfc);
	kfree(pi);
}

#if BRIDGE == BRIDGE_PCI2PI

/*****************************************/
/* PCI hotplug interface: probe new card */
/*****************************************/
static int __devinit
xhfc_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	pi_params * driver_data = (pi_params *) ent->driver_data;
	xhfc_pi * pi = NULL;
	__u8 i;

	int err = -ENOMEM;

	/* alloc mem for ProcessorInterface xhfc_pi */
	if (!(pi = kmalloc(sizeof(xhfc_pi), GFP_KERNEL))) {
		printk(KERN_ERR "%s %s: No kmem for XHFC card\n",
		       pi->card_name, __FUNCTION__);
		goto out;
	}
	memset(pi, 0, sizeof(xhfc_pi));

	pi->cardnum = card_cnt;

	sprintf(pi->card_name, "%s_PI%d", DRIVER_NAME, pi->cardnum);
	printk(KERN_INFO "%s %s: adapter '%s' found on PCI bus %02x dev %02x, using %i XHFC controllers\n",
	       pi->card_name, __FUNCTION__, driver_data->device_name,
	       pdev->bus->number, pdev->devfn, driver_data->num_xhfcs);

	/* alloc mem for all XHFCs (xhfc_t) */
	if (!(pi->xhfc = kmalloc(sizeof(xhfc_t) * driver_data->num_xhfcs, GFP_KERNEL))) {
		printk(KERN_ERR "%s %s: No kmem for sizeof(xhfc_t)*%i \n",
		       pi->card_name, __FUNCTION__, driver_data->num_xhfcs);
		goto out;
	}
	memset(pi->xhfc, 0, sizeof(xhfc_t) * driver_data->num_xhfcs);

	pi->pdev = pdev;
	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR "%s %s: error with pci_enable_device\n",
		       pi->card_name, __FUNCTION__);
		goto out;
	}

	if (driver_data->num_xhfcs > PCI2PI_MAX_XHFC) {
		printk(KERN_ERR "%s %s: max number og adressable XHFCs aceeded\n",
		       pi->card_name, __FUNCTION__);
		goto out;
	}

	pi->driver_data = *driver_data;
	pi->irq = pdev->irq;
	pi->hw_membase = (u_char *) pci_resource_start(pdev, 1);
	pi->membase = ioremap((ulong) pi->hw_membase, 4096);
	pci_set_drvdata(pdev, pi);

	err = init_pci_bridge(pi);
	if (err) {
		printk(KERN_ERR "%s %s: init_pci_bridge failed!\n",
		       pi->card_name, __FUNCTION__);
		goto out;
	}

	/* init interrupt engine */
	if (request_irq(pi->irq, xhfc_interrupt, SA_SHIRQ, "XHFC", pi)) {
		printk(KERN_WARNING "%s %s: couldn't get interrupt %d\n",
		       pi->card_name, __FUNCTION__, pi->irq);
		pi->irq = 0;
		err = -EIO;
		goto out;
	}

	err = 0;
	for (i=0; i<pi->driver_data.num_xhfcs; i++) {
		pi->xhfc[i].pi = pi;
		pi->xhfc[i].chipidx = i;
		err |= setup_instance(&pi->xhfc[i]);
	}

	if (!err) {
		card_cnt++;
		return (0);
	} else {
		goto out;
	}

      out:
	if (pi->xhfc)
		kfree(pi->xhfc);
	if (pi)
		kfree(pi);
	return (err);
};

/**************************************/
/* PCI hotplug interface: remove card */
/**************************************/
static void __devexit
xhfc_pci_remove(struct pci_dev *pdev)
{
	xhfc_pi *pi = pci_get_drvdata(pdev);
	printk(KERN_INFO "%s %s: removing card\n", pi->card_name,
	       __FUNCTION__);
	release_card(pi);
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
	 (unsigned long) &((pi_params) {1, "XHFC Evaluation Board"}),
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

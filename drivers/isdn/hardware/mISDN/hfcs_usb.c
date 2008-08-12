/* hfcs_usb.c
 * mISDN driver for Colognechip HFC-S USB chip
 *
 * Copyright 2001 by Peter Sprenger (sprenger@moving-bytes.de)
 * Copyright 2008 by Martin Bachem (info@bachem-it.com)
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
#include <linux/delay.h>
#include <linux/usb.h>
#include <linux/pci.h>
#include <linux/mISDNhw.h>
#include <linux/isdn_compat.h>
#include "hfcs_usb.h"

const char *hfcsusb_rev = "Revision: 2.1 ALPHA (socket), 2008-08-12";

static int debug = 0;
static int poll = 128;

static LIST_HEAD(HFClist);
static rwlock_t HFClock = RW_LOCK_UNLOCKED;


#ifdef MODULE
MODULE_AUTHOR("Martin Bachem");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
module_param(debug, uint, 0);
#endif


#define MAX_CARDS 8

static int hfcsusb_cnt;

/* some function prototypes */
static void hfcsusb_ph_command(hfcsusb_t * hw, u_char command);
static void release_hw(hfcsusb_t * hw);
static void reset_hfcsusb(hfcsusb_t * hw);
static void setPortMode(hfcsusb_t * hw);
static void hfcsusb_start_endpoint(hfcsusb_t * hw, int channel);
static void hfcsusb_stop_endpoint(hfcsusb_t * hw, int channel);
static int  hfcsusb_setup_bch(struct bchannel *bch, int protocol);
static void deactivate_bchannel(struct bchannel *bch);

/* start next background transfer for control channel */
static void
ctrl_start_transfer(hfcsusb_t * hw)
{
	if (debug & DBG_HFC_CALL_TRACE)
		printk (KERN_INFO DRIVER_NAME ": %s\n",
		        __FUNCTION__);

	if (hw->ctrl_cnt) {
		hw->ctrl_urb->pipe = hw->ctrl_out_pipe;
		hw->ctrl_urb->setup_packet =
		    (u_char *) & hw->ctrl_write;
		hw->ctrl_urb->transfer_buffer = NULL;
		hw->ctrl_urb->transfer_buffer_length = 0;
		hw->ctrl_write.wIndex =
		    cpu_to_le16(hw->ctrl_buff[hw->ctrl_out_idx].hfcs_reg);
		hw->ctrl_write.wValue =
		    cpu_to_le16(hw->ctrl_buff[hw->ctrl_out_idx].reg_val);

		usb_submit_urb(hw->ctrl_urb, GFP_ATOMIC);	/* start transfer */
	}
}

/*
 * queue a control transfer request to write HFC-S USB
 * chip register using CTRL resuest queue
 */
static int
queued_Write_hfc(hfcsusb_t * hw, __u8 reg, __u8 val)
{
	ctrl_buft *buf;

	if (debug & DBG_HFC_CALL_TRACE)
		printk (KERN_INFO DRIVER_NAME ": %s\n",
		        __FUNCTION__);

	spin_lock(&hw->ctrl_lock);
	if (hw->ctrl_cnt >= HFC_CTRL_BUFSIZE)
		return (1);
	buf = &hw->ctrl_buff[hw->ctrl_in_idx];
	buf->hfcs_reg = reg;
	buf->reg_val = val;
	if (++hw->ctrl_in_idx >= HFC_CTRL_BUFSIZE)
		hw->ctrl_in_idx = 0;
	if (++hw->ctrl_cnt == 1)
		ctrl_start_transfer(hw);
	spin_unlock(&hw->ctrl_lock);

	return (0);
}

/* control completion routine handling background control cmds */
static void
ctrl_complete(struct urb *urb)
{
	hfcsusb_t *hw = (hfcsusb_t *) urb->context;
	ctrl_buft *buf;

	if (debug & DBG_HFC_CALL_TRACE)
		printk (KERN_INFO DRIVER_NAME ": %s\n",
		        __FUNCTION__);

	urb->dev = hw->dev;
	if (hw->ctrl_cnt) {
		buf = &hw->ctrl_buff[hw->ctrl_out_idx];
		hw->ctrl_cnt--;	/* decrement actual count */
		if (++hw->ctrl_out_idx >= HFC_CTRL_BUFSIZE)
			hw->ctrl_out_idx = 0;	/* pointer wrap */

		ctrl_start_transfer(hw);	/* start next transfer */
	}
}

/* handle LED bits   */
static void
set_led_bit(hfcsusb_t * hw, signed short led_bits, int set_on)
{
	if (debug & DBG_HFC_CALL_TRACE)
		printk (KERN_INFO DRIVER_NAME ": %s\n",
		        __FUNCTION__);

	if (set_on) {
		if (led_bits < 0)
			hw->led_state &= ~abs(led_bits);
		else
			hw->led_state |= led_bits;
	} else {
		if (led_bits < 0)
			hw->led_state |= abs(led_bits);
		else
			hw->led_state &= ~led_bits;
	}
}

/* handle LED requests  */
static void
handle_led(hfcsusb_t * hw, int event)
{
	hfcsusb_vdata *driver_info =
	    (hfcsusb_vdata *) hfcsusb_idtab[hw->vend_idx].driver_info;
	__u8 tmpled;

	if (debug & DBG_HFC_CALL_TRACE)
		printk (KERN_INFO DRIVER_NAME ": %s\n",
		        __FUNCTION__);

	if (driver_info->led_scheme == LED_OFF) {
		return;
	}
	tmpled = hw->led_state;

	switch (event) {
		case LED_POWER_ON:
			set_led_bit(hw, driver_info->led_bits[0], 1);
			set_led_bit(hw, driver_info->led_bits[1], 0);
			set_led_bit(hw, driver_info->led_bits[2], 0);
			set_led_bit(hw, driver_info->led_bits[3], 0);
			break;
		case LED_POWER_OFF:
			set_led_bit(hw, driver_info->led_bits[0], 0);
			set_led_bit(hw, driver_info->led_bits[1], 0);
			set_led_bit(hw, driver_info->led_bits[2], 0);
			set_led_bit(hw, driver_info->led_bits[3], 0);
			break;
		case LED_S0_ON:
			set_led_bit(hw, driver_info->led_bits[1], 1);
			break;
		case LED_S0_OFF:
			set_led_bit(hw, driver_info->led_bits[1], 0);
			break;
		case LED_B1_ON:
			set_led_bit(hw, driver_info->led_bits[2], 1);
			break;
		case LED_B1_OFF:
			set_led_bit(hw, driver_info->led_bits[2], 0);
			break;
		case LED_B2_ON:
			set_led_bit(hw, driver_info->led_bits[3], 1);
			break;
		case LED_B2_OFF:
			set_led_bit(hw, driver_info->led_bits[3], 0);
			break;
	}

	if (hw->led_state != tmpled)
		write_usb(hw, HFCUSB_P_DATA, hw->led_state);

}


/*
 * Layer2 -> Layer 1 Bchannel data
 */
static int
hfcusb_l2l1B(struct mISDNchannel *ch, struct sk_buff *skb)
{
	struct bchannel		*bch = container_of(ch, struct bchannel, ch);
	hfcsusb_t		*hw = bch->hw;
	int			ret = -EINVAL;
	struct mISDNhead	*hh = mISDN_HEAD_P(skb);
	u_long			flags;

	if (debug & DBG_HFC_CALL_TRACE)
		printk (KERN_INFO DRIVER_NAME ": %s\n",
		        __FUNCTION__);

	switch (hh->prim) {
	case PH_DATA_REQ:
		spin_lock_irqsave(&hw->lock, flags);
		ret = bchannel_senddata(bch, skb);
		if (ret > 0) {
			spin_unlock_irqrestore(&hw->lock, flags);
			queue_ch_frame(ch, PH_DATA_CNF, hh->id, NULL);
			ret = 0;
		} else
			spin_unlock_irqrestore(&hw->lock, flags);
		return ret;
	case PH_ACTIVATE_REQ:
		spin_lock_irqsave(&hw->lock, flags);
		if (!test_and_set_bit(FLG_ACTIVE, &bch->Flags)) {
			hfcsusb_start_endpoint(hw, bch->nr);
			ret = hfcsusb_setup_bch(bch, ch->protocol);
		} else
			ret = 0;
		spin_unlock_irqrestore(&hw->lock, flags);
		if (!ret)
			_queue_data(ch, PH_ACTIVATE_IND, MISDN_ID_ANY, 0, NULL, GFP_KERNEL);
		break;
	case PH_DEACTIVATE_REQ:
		deactivate_bchannel(bch);
		_queue_data(ch, PH_DEACTIVATE_IND, MISDN_ID_ANY, 0, NULL, GFP_KERNEL);
		ret = 0;
		break;
	}
	if (!ret)
		dev_kfree_skb(skb);
	return ret;
}

/*
 * Layer2 -> Layer 1 Dchannel data
 */
static int
hfcusb_l2l1D(struct mISDNchannel *ch, struct sk_buff *skb)
{
	struct mISDNdevice	*dev = container_of(ch, struct mISDNdevice, D);
	struct dchannel		*dch = container_of(dev, struct dchannel, dev);
	struct mISDNhead	*hh = mISDN_HEAD_P(skb);
	hfcsusb_t		*hw = dch->hw;
	int			ret = -EINVAL;

	if (debug & DBG_HFC_CALL_TRACE)
		printk (KERN_INFO DRIVER_NAME ": %s\n",
		        __FUNCTION__);

	switch (hh->prim) {
		case PH_DATA_REQ:
			if (debug & DBG_HFC_CALL_TRACE)
				printk (KERN_INFO DRIVER_NAME ": %s: PH_DATA_REQ\n",
		        		__FUNCTION__);

			ret = dchannel_senddata(dch, skb);
			if (ret > 0) {
				ret = 0;
				queue_ch_frame(ch, PH_DATA_CNF, hh->id, NULL);
			}
			break;

		case PH_ACTIVATE_REQ:
			if (debug & DBG_HFC_CALL_TRACE)
				printk (KERN_INFO DRIVER_NAME ": %s: PH_ACTIVATE_REQ\n",
		        		__FUNCTION__);
			hfcsusb_ph_command(hw, HFC_L1_ACTIVATE_TE);
			break;

		case PH_DEACTIVATE_REQ:
			if (debug & DBG_HFC_CALL_TRACE)
				printk (KERN_INFO DRIVER_NAME ": %s: PH_DEACTIVATE_REQ\n",
		        		__FUNCTION__);
			break;
	}

	// TODO mbachem
	return ret;
}

/*
 * Layer 1 callback function
 */
static int
hfc_l1callback(struct dchannel *dch, u_int cmd)
{
	if (debug & DBG_HFC_CALL_TRACE)
		printk (KERN_INFO DRIVER_NAME ": %s cmd 0x%x\n",
		        __FUNCTION__, cmd);

	// TODO mbachem
	switch (cmd) {
		case INFO3_P8:
		case INFO3_P10:
			printk(KERN_INFO "INFO3_P8\n");
			break;
		case HW_RESET_REQ:
			printk(KERN_INFO "HW_RESET_REQ\n");
			break;
		case HW_DEACT_REQ:
			printk(KERN_INFO "HW_DEACT_REQ\n");
			break;
		case HW_POWERUP_REQ:
			printk(KERN_INFO "HW_POWERUP_REQ\n");
			break;
		case PH_ACTIVATE_IND:
			printk(KERN_INFO "PH_ACTIVATE_IND\n");
			test_and_clear_bit(FLG_ACTIVE, &dch->Flags);
			_queue_data(&dch->dev.D, cmd, MISDN_ID_ANY, 0, NULL,
			            GFP_ATOMIC);
			break;
		case PH_DEACTIVATE_IND:
			printk(KERN_INFO "PH_DEACTIVATE_IND\n");

			break;
		default:
			if (dch->debug & DEBUG_HW)
				printk(KERN_INFO "%s: unknown command %x\n",
				__func__, cmd);
			return -1;
	}
	return 0;
}

static int
open_dchannel(hfcsusb_t *hw, struct mISDNchannel *ch,
    struct channel_req *rq)
{
	int err = 0;

	if (debug & DEBUG_HW_OPEN)
		printk(KERN_INFO DRIVER_NAME ": %s: dev(%d) open from %p\n",
		       __FUNCTION__, hw->dch.dev.id,
		       __builtin_return_address(0));
	if (rq->protocol == ISDN_P_NONE)
		return -EINVAL;

	hfcsusb_start_endpoint(hw, HFC_CHAN_D);
	if (!hw->initdone) {
		if (rq->protocol == ISDN_P_TE_S0) {
			hw->portmode = ISDN_P_TE_S0;
			err = create_l1(&hw->dch, hfc_l1callback);
			if (err)
				return err;
		} else {
			hw->portmode = ISDN_P_NT_S0;
		}
		setPortMode(hw);
		ch->protocol = rq->protocol;
		hw->initdone = 1;
	} else {
		if (rq->protocol != ch->protocol)
			return -EPROTONOSUPPORT;
	}

	if (((ch->protocol == ISDN_P_NT_S0) && (hw->dch.state == 3)) ||
	    ((ch->protocol == ISDN_P_TE_S0) && (hw->dch.state == 7))) {
		_queue_data(ch, PH_ACTIVATE_IND, MISDN_ID_ANY,
		    0, NULL, GFP_KERNEL);
	}
	rq->ch = ch;
	if (!try_module_get(THIS_MODULE))
		printk(KERN_WARNING DRIVER_NAME ": %s: cannot get module\n", __FUNCTION__);
	return 0;
}

static int
open_bchannel(hfcsusb_t *hw, struct channel_req *rq)
{
	struct bchannel		*bch;

	if (rq->adr.channel > 2)
		return -EINVAL;
	if (rq->protocol == ISDN_P_NONE)
		return -EINVAL;

	if (debug & DBG_HFC_CALL_TRACE)
		printk (KERN_INFO DRIVER_NAME ": %s B%i\n",
			__FUNCTION__, rq->adr.channel);

	bch = &hw->bch[rq->adr.channel - 1];
	if (test_and_set_bit(FLG_OPEN, &bch->Flags))
		return -EBUSY; /* b-channel can be only open once */
	test_and_clear_bit(FLG_FILLEMPTY, &bch->Flags);
	bch->ch.protocol = rq->protocol;
	rq->ch = &bch->ch; /* TODO: E-channel */

	// start USB endpoint for bchannel
	if (rq->adr.channel  == 1)
		hfcsusb_start_endpoint(hw, HFC_CHAN_B1);
	else
		hfcsusb_start_endpoint(hw, HFC_CHAN_B2);

	if (!try_module_get(THIS_MODULE))
		printk(KERN_WARNING "%s:cannot get module\n", __func__);
	return 0;
}

static int
channel_ctrl(hfcsusb_t *hw, struct mISDN_ctrl_req *cq)
{
	int ret = 0;

	if (debug & DBG_HFC_CALL_TRACE)
		printk (KERN_INFO DRIVER_NAME ": %s op(0x%x) channel(0x%x)\n",
		        __FUNCTION__, (cq->op), (cq->channel));

	switch(cq->op) {
		case MISDN_CTRL_GETOP:
			cq->op = MISDN_CTRL_LOOP | MISDN_CTRL_CONNECT |
			         MISDN_CTRL_DISCONNECT;
			break;
		default:
			printk(KERN_WARNING DRIVER_NAME "%i: %s: unknown Op %x\n",
			       hw->tn, __FUNCTION__, cq->op);
			ret= -EINVAL;
			break;
	}
	return ret;
}

/*
 * device control function
 */
static int
hfc_dctrl(struct mISDNchannel *ch, u_int cmd, void *arg)
{
	struct mISDNdevice	*dev = container_of(ch, struct mISDNdevice, D);
	struct dchannel		*dch = container_of(dev, struct dchannel, dev);
	hfcsusb_t		*hw = dch->hw;
	struct channel_req	*rq;
	int			err = 0;

	if (dch->debug & DEBUG_HW)
		printk(KERN_INFO DRIVER_NAME ": %s: cmd:%x %p\n",
		       __FUNCTION__, cmd, arg);
	switch (cmd) {
		case OPEN_CHANNEL:
			rq = arg;
			if (rq->adr.channel == 0)
				err = open_dchannel(hw, ch, rq);
			else
				err = open_bchannel(hw, rq);
			break;
		case CLOSE_CHANNEL:
			if (debug & DEBUG_HW_OPEN)
				printk(KERN_INFO DRIVER_NAME ": %s: dev(%d) close from %p\n",
				    __FUNCTION__, hw->dch.dev.id,
				    __builtin_return_address(0));
			hfcsusb_stop_endpoint(hw, HFC_CHAN_D);
			module_put(THIS_MODULE);
			break;
		case CONTROL_CHANNEL:
			err = channel_ctrl(hw, arg);
			break;
		default:
			if (dch->debug & DEBUG_HW)
				printk(KERN_INFO DRIVER_NAME ": %s: unknown command %x\n",
				    __FUNCTION__, cmd);
			return -EINVAL;
	}
	return err;
}

/* S0 TE state change event handler */
static void
ph_state_te(struct dchannel * dch)
{
	hfcsusb_t * hw = dch->hw;

	if (debug & DEBUG_HW)
		printk (KERN_INFO DRIVER_NAME ": %s: TE F%d\n",
				__FUNCTION__, dch->state);
	switch (dch->state) {
		case 0:
			l1_event(dch->l1, HW_RESET_IND);
			break;
		case 3:
			l1_event(dch->l1, HW_DEACT_IND);
			break;
		case 5:
		case 8:
			l1_event(dch->l1, ANYSIGNAL);
			break;
		case 6:
			l1_event(dch->l1, INFO2);
			break;
		case 7:
			l1_event(dch->l1, INFO4_P8);
			break;
	}
	if (dch->state == 7)
		handle_led(hw, LED_S0_ON);
	else
		handle_led(hw, LED_S0_OFF);
}

/* S0 TE state change event handler */
/*
static void
ph_state_nt(struct dchannel * dch) {
	if (debug & DEBUG_HW)
		printk (KERN_INFO DRIVER_NAME ": %s: NT G%d\n",
		 __FUNCTION__, dch->state);
}
*/

static void
ph_state(struct dchannel *dch)
{
	hfcsusb_t *hw = dch->hw;

	if (debug & DBG_HFC_CALL_TRACE)
		printk (KERN_INFO DRIVER_NAME ": %s\n",
		        __FUNCTION__);

	if (hw->portmode == ISDN_P_NT_S0) {
		// TODO mb
	} else
		ph_state_te(dch);
}

/*
 * disable/enable BChannel for desired protocoll
 */
static int
hfcsusb_setup_bch(struct bchannel *bch, int protocol)
{
	__u8 conhdlc, sctrl, sctrl_r;	/* conatainer for new register vals */

	hfcsusb_t *hw = bch->hw;

	if (debug & DEBUG_HW)
		printk(KERN_INFO DRIVER_NAME ": %s: protocol %x-->%x B%d\n",
				 __FUNCTION__, bch->state, protocol,
				 bch->nr);

	/* setup val for CON_HDLC */
	conhdlc = 0;
	if (protocol > ISDN_P_NONE)
		conhdlc = 8;	/* enable FIFO */

	switch (protocol) {
		case (-1):	/* used for init */
			bch->state = -1;
			/* fall trough */
		case (ISDN_P_NONE):
			if (bch->state == ISDN_P_NONE)
				return (0);	/* already in idle state */
			bch->state = ISDN_P_NONE;
			test_and_clear_bit(FLG_HDLC, &bch->Flags);
			test_and_clear_bit(FLG_TRANSPARENT, &bch->Flags);
			break;
		case (ISDN_P_B_RAW):
			conhdlc |= 2;
			bch->state = protocol;
			set_bit(FLG_TRANSPARENT, &bch->Flags);
			break;
		case (ISDN_P_B_HDLC):
			bch->state = protocol;
			set_bit(FLG_HDLC, &bch->Flags);
			break;
		default:
			if (debug & DEBUG_HW)
				printk(KERN_INFO DRIVER_NAME ": %s: prot not known %x\n",
					__FUNCTION__, protocol);
			return (-ENOPROTOOPT);
	}

	if (protocol >= ISDN_P_NONE) {
		/*
		printk ("HFCS-USB: %s: HFCUSB_FIFO(0x%x) HFCUSB_CON_HDLC(0x%x)\n",
			__FUNCTION__, (bch->channel)?2:0, conhdlc);
		*/

		/* set FIFO to transmit register */
		queued_Write_hfc(hw, HFCUSB_FIFO, (bch->nr==1)?0:2);
		queued_Write_hfc(hw, HFCUSB_CON_HDLC, conhdlc);

		/* reset fifo */
		queued_Write_hfc(hw, HFCUSB_INC_RES_F, 2);

		/*
		printk ("HFCS-USB: %s: HFCUSB_FIFO(0x%x) HFCUSB_CON_HDLC(0x%x)\n",
			__FUNCTION__, (bch->channel)?2:0, conhdlc);
		*/

		/* set FIFO to receive register */
		queued_Write_hfc(hw, HFCUSB_FIFO, (bch->nr==1)?1:3);
		queued_Write_hfc(hw, HFCUSB_CON_HDLC, conhdlc);

		/* reset fifo */
		queued_Write_hfc(hw, HFCUSB_INC_RES_F, 2);

		sctrl = 0x40 + ((hw->portmode & ISDN_P_TE_S0)?0x00:0x04);
		sctrl_r = 0x0;

		if (hw->bch[0].state) {
			sctrl |= ((hw->bch[0].nr)?2:1);
			sctrl_r |= ((hw->bch[0].nr)?2:1);
		}

		if (hw->bch[1].state) {
			sctrl |= ((hw->bch[1].nr)?2:1);
			sctrl_r |= ((hw->bch[1].nr)?2:1);
		}

		/*
		printk ("HFCS-USB: %s: HFCUSB_SCTRL(0x%x) HFCUSB_SCTRL_R(0x%x)\n",
			__FUNCTION__, sctrl, sctrl_r);
		*/

		queued_Write_hfc(hw, HFCUSB_SCTRL, sctrl);
		queued_Write_hfc(hw, HFCUSB_SCTRL_R, sctrl_r);

		if (protocol > ISDN_P_NONE) {
			handle_led(hw, (bch->nr==1)?LED_B1_ON:LED_B2_ON);
		} else {
			handle_led(hw, (bch->nr==1)?LED_B1_OFF:LED_B2_OFF);
		}
	}
	return (0);
}

static void
hfcsusb_ph_command(hfcsusb_t * hw, u_char command)
{
	if (debug & DEBUG_HW)
		printk(KERN_INFO DRIVER_NAME ": %s: %x\n",
		       __FUNCTION__, command);

	switch (command) {
		case HFC_L1_ACTIVATE_TE:
			/* force sending sending INFO1 */
			queued_Write_hfc(hw, HFCUSB_STATES, 0x14);
			/* start l1 activation */
			queued_Write_hfc(hw, HFCUSB_STATES, 0x04);
			break;

		case HFC_L1_FORCE_DEACTIVATE_TE:
			queued_Write_hfc(hw, HFCUSB_STATES, 0x10);
			queued_Write_hfc(hw, HFCUSB_STATES, 0x03);
			break;

		case HFC_L1_ACTIVATE_NT:
			if (hw->dch.state == 3) {
				// TODO mbachem: signal PH_ACTIVATE | INDICATION to mISDN
			} else {
				queued_Write_hfc(hw, HFCUSB_STATES,
				                       HFCUSB_ACTIVATE
				                       | HFCUSB_DO_ACTION
				                       | HFCUSB_NT_G2_G3);
			}
			break;

		case HFC_L1_DEACTIVATE_NT:
			queued_Write_hfc(hw, HFCUSB_STATES,
			 		       HFCUSB_DO_ACTION);
			break;
	}
}

/*
 * Layer 1 B-channel hardware access
 */
static int
channel_bctrl(struct bchannel *bch, struct mISDN_ctrl_req *cq)
{
	int	ret = 0;

	switch (cq->op) {
		case MISDN_CTRL_GETOP:
			cq->op = MISDN_CTRL_FILL_EMPTY;
			break;
			case MISDN_CTRL_FILL_EMPTY: /* fill fifo, if empty */
				test_and_set_bit(FLG_FILLEMPTY, &bch->Flags);
				if (debug & DEBUG_HW_OPEN)
					printk(KERN_DEBUG "%s: FILL_EMPTY request (nr=%d "
							"off=%d)\n", __func__, bch->nr, !!cq->p1);
				break;
		default:
			printk(KERN_WARNING "%s: unknown Op %x\n", __func__, cq->op);
			ret = -EINVAL;
			break;
	}
	return ret;
}

/* collect data from incoming interrupt or isochron USB data */
static void
hfcsusb_rx_frame(usb_fifo * fifo, __u8 * data, unsigned int len, int finish)
{
	struct sk_buff	*rx_skb; /* data buffer for upper layer */
	int		maxlen;
	int		fifon = fifo->fifonum;
	int		i;
	int		hdlc;

	if (debug & DBG_HFC_CALL_TRACE)
		printk (KERN_INFO DRIVER_NAME ": %s: fifo(%i) len(%i) dch(%p) bch(%p)\n",
		        __FUNCTION__, fifon, len, fifo->dch, fifo->bch);

	if (!len)
		return;

	if (fifo->dch) {
		rx_skb = fifo->dch->rx_skb;
		maxlen = fifo->dch->maxlen;
		hdlc = 1;
	}
	else if (fifo->bch) {
		rx_skb = fifo->bch->rx_skb;
		maxlen = fifo->bch->maxlen;
		hdlc = test_bit(FLG_HDLC, &fifo->bch->Flags);
	}
	else {
		printk(KERN_INFO DRIVER_NAME ": %s: neither B-CH not D-CH\n", __FUNCTION__);
		return;
	}

	if (!rx_skb) {
		printk(KERN_INFO DRIVER_NAME ": %s: alloc new rx_skb\n", __FUNCTION__);
		rx_skb = mI_alloc_skb(maxlen, GFP_ATOMIC);
		if (rx_skb) {
			if (fifo->dch)
				fifo->dch->rx_skb = rx_skb;
			else
				fifo->bch->rx_skb = rx_skb;
			skb_trim(rx_skb, 0);
		} else {
			printk(KERN_INFO DRIVER_NAME ": %s: No mem for rx_skb\n", __FUNCTION__);
			return;
		}
	}

	if (fifo->dch) {
		/* D-Channel SKB range check */
		if ((rx_skb->len + len) >= MAX_DFRAME_LEN_L1) {
			printk(KERN_INFO DRIVER_NAME ": %s: sbk mem exceeded for fifo(%d) HFCUSB_D_RX\n",
			       __FUNCTION__, fifon);
			skb_trim(rx_skb, 0);
			return;
		}
	} else {
		/* B-Channel SKB range check */
		if ((rx_skb->len + len) >= (MAX_BCH_SIZE + 3)) {
			printk(KERN_INFO DRIVER_NAME ": %s: sbk mem exceeded for fifo(%d) HFCUSB_B_RX\n",
			       __FUNCTION__, fifon);
			skb_trim(rx_skb, 0);
			return;
		}
	}

	// printk (KERN_INFO "skb_put: len(%d) new_len(%d)\n", rx_skb->len, len);
	memcpy(skb_put(rx_skb, len), data, len);

	if (hdlc) {
		/* we have a complete hdlc packet */
		if (finish) {
			if ((rx_skb->len > 3) &&
			   (!(rx_skb->data[rx_skb->len - 1]))) {

				if (debug & DBG_HFC_FIFO_VERBOSE) {
					printk(KERN_INFO DRIVER_NAME ": %s: fifon(%i) new RX len(%i): ",
						__FUNCTION__, fifon, rx_skb->len);
					i = 0;
					while (i < rx_skb->len)
						printk("%02x ", rx_skb->data[i++]);
					printk("\n");
				}

				/* remove CRC & status */
				skb_trim(rx_skb, rx_skb->len - 3);

				if (fifo->dch)
					recv_Dchannel(fifo->dch);
				else
					recv_Bchannel(fifo->bch);

			} else {
				if (debug & DBG_HFC_FIFO_VERBOSE) {
					printk (KERN_INFO DRIVER_NAME ": CRC or minlen ERROR fifon(%i) RX len(%i): ",
					         fifon, rx_skb->len);
					i = 0;
					while (i < rx_skb->len)
						printk("%02x ", rx_skb->data[i++]);
					printk("\n");
				}
				skb_trim(rx_skb, 0);
			}
		}
	} else {
		if (finish || rx_skb->len >= poll) {
			/* deliver transparent data to layer2 */
			// TODO mbachem: ask jolly for transparent flow control
			recv_Bchannel(fifo->bch);
		}
	}
}

void
fill_isoc_urb(struct urb *urb, struct usb_device *dev, unsigned int pipe,
	      void *buf, int num_packets, int packet_size, int interval,
	      usb_complete_t complete, void *context)
{
	int k;

	usb_fill_bulk_urb(urb, dev, pipe, buf, packet_size * num_packets, complete, context);

	urb->number_of_packets = num_packets;
	urb->transfer_flags = URB_ISO_ASAP;
	urb->actual_length = 0;
	urb->interval = interval;

	for (k = 0; k < num_packets; k++) {
		urb->iso_frame_desc[k].offset = packet_size * k;
		urb->iso_frame_desc[k].length = packet_size;
		urb->iso_frame_desc[k].actual_length = 0;
	}
}

#ifdef ISO_FRAME_START_DEBUG
static void
debug_frame_starts(iso_urb_struct *context_iso_urb, __u8 fifon)
{
	__u8 k;

	printk (KERN_INFO DRIVER_NAME ": ISO_FRAME_START_DEBUG fifo(%d) urb(%d) (%d) - \n",
	        fifon, context_iso_urb->indx,
	        context_iso_urb->iso_frm_strt_pos);
	for (k=0; k<ISO_FRAME_START_RING_COUNT; k++)
		printk(" %d", context_iso_urb->start_frames[k]);
	printk ("\n");
}

static void
rem_frame_starts(struct urb *urb, iso_urb_struct *context_iso_urb, __u8 fifon)
{
	context_iso_urb->start_frames[context_iso_urb->iso_frm_strt_pos]
		= urb->start_frame;
	context_iso_urb->iso_frm_strt_pos++;
	if (context_iso_urb->iso_frm_strt_pos >= ISO_FRAME_START_RING_COUNT)
		context_iso_urb->iso_frm_strt_pos = 0;
}
#endif

/* receive completion routine for all ISO tx fifos   */
static void
rx_iso_complete(struct urb *urb)
{
	iso_urb_struct *context_iso_urb = (iso_urb_struct *) urb->context;
	usb_fifo *fifo = context_iso_urb->owner_fifo;
	hfcsusb_t *hw = fifo->hw;
	int k, len, errcode, offset, num_isoc_packets, fifon, maxlen,
	    status, iso_status, i;
	__u8 *buf;
	static __u8 eof[8];
	__u8 s0_state;
#ifdef ISO_FRAME_START_DEBUG
	__u8 do_strt_frm_dbg;
#endif
	fifon = fifo->fifonum;
	status = urb->status;
	s0_state = 0;

#ifdef ISO_FRAME_START_DEBUG
	do_strt_frm_dbg = 0;
	rem_frame_starts(urb, context_iso_urb, fifon);
#endif

	/*
	 * ISO transfer only partially completed,
	 * look at individual frame status for details
	 */
	if (status == -EXDEV) {
#ifdef ISO_FRAME_START_DEBUG
		do_strt_frm_dbg = 1;
#endif
		if (debug & DBG_HFC_FIFO_VERBOSE)
			printk(KERN_INFO
				DRIVER_NAME ": rx_iso_complete with -EXDEV "
				"urb->status %d, fifonum %d\n",
				status, fifon);

		// clear status, so go on with ISO transfers
		status = 0;
	}

	if (fifo->active && !status) {
		num_isoc_packets = iso_packets[fifon];
		maxlen = fifo->usb_packet_maxlen;

		for (k = 0; k < num_isoc_packets; ++k)
		{
			len = urb->iso_frame_desc[k].actual_length;
			offset = urb->iso_frame_desc[k].offset;
			buf = context_iso_urb->buffer + offset;
			iso_status = urb->iso_frame_desc[k].status;

			if ((iso_status && !hw->disc_flag) &&
			    (debug & DBG_HFC_FIFO_VERBOSE)) {
				printk(KERN_INFO
				       DRIVER_NAME ": rx_iso_complete "
				       "ISO packet %i, status: %i\n",
				       k, iso_status);
#ifdef ISO_FRAME_START_DEBUG
				do_strt_frm_dbg = 1;
#endif
			}

			/* USB data log for every D ISO in */
			if ((fifon == HFCUSB_D_RX) && (debug & DBG_HFC_USB_VERBOSE)) {
				printk (KERN_INFO DRIVER_NAME ": D RX ISO %d (%d/%d) len(%d) ",
				        urb->start_frame, k, num_isoc_packets-1,
				        len);
				for (i=0; i<len; i++)
					printk ("%x ", buf[i]);
				printk ("\n");
			}

			if (!iso_status) {
				if (fifo->last_urblen != maxlen) {
					/*
					 * save fifo fill-level threshold bits to
					 * use them later in TX ISO URB completions
					 */
					hw->threshold_mask = buf[1];

					if (fifon == HFCUSB_D_RX)
						s0_state = (buf[0] >> 4);

					eof[fifon] = buf[0] & 1;
					if (len > 2)
						hfcsusb_rx_frame(fifo, buf + 2,
								 len - 2,
								 (len<maxlen) ?
								 eof[fifon]:0);
				} else {
					hfcsusb_rx_frame(fifo, buf, len,
							 (len<maxlen) ?
							 eof[fifon]:0);
				}
				fifo->last_urblen = len;
			}
		}

		/* signal S0 layer1 state change */
		if ((s0_state) && (hw->initdone) &&
		    (s0_state != hw->dch.state)) {
			hw->dch.state = s0_state;
			schedule_event(&hw->dch, FLG_PHCHANGE);
		}

		fill_isoc_urb(urb, fifo->hw->dev, fifo->pipe,
			      context_iso_urb->buffer, num_isoc_packets,
			      fifo->usb_packet_maxlen, fifo->intervall,
			      (usb_complete_t)rx_iso_complete, urb->context);
		errcode = usb_submit_urb(urb, GFP_ATOMIC);
		if (errcode < 0) {
			if (debug & DBG_HFC_URB_ERROR)
				printk(KERN_INFO
				       DRIVER_NAME ": error submitting ISO URB: %d\n",
				       errcode);

#ifdef HFCUSB_ISO_RESURRECTION
			printk(KERN_INFO DRIVER_NAME ": setting ISO resurrection "
			       "timer, fifo(%i) urb(%i)\n",
			       fifon, context_iso_urb->indx);

			if (timer_pending(&context_iso_urb->timer))
				del_timer(&context_iso_urb->timer);

			context_iso_urb->iso_res_cnt = 0;
			/* wait 1s for hcd hopfuly gets friendly again */
			context_iso_urb->timer.expires =
				jiffies + HZ;
			add_timer(&context_iso_urb->timer);
#endif
		}
	} else {
		if (status && !hw->disc_flag) {
			if (debug & DBG_HFC_URB_INFO)
				printk(KERN_INFO
				       DRIVER_NAME ": rx_iso_complete : "
				       "urb->status %d, fifonum %d\n",
				       status, fifon);
		}
	}

#ifdef ISO_FRAME_START_DEBUG
	if (do_strt_frm_dbg)
		debug_frame_starts(context_iso_urb, fifon);
#endif
}

/* receive completion routine for all interrupt rx fifos */
static void
rx_int_complete(struct urb *urb)
{
	int len, status, i;
	__u8 *buf, maxlen, fifon;
	usb_fifo *fifo = (usb_fifo *) urb->context;
	hfcsusb_t *hw = fifo->hw;
	static __u8 eof[8];

	urb->dev = hw->dev;	/* security init */

	fifon = fifo->fifonum;
	if ((!fifo->active) || (urb->status)) {
		if (debug & DBG_HFC_URB_ERROR)
			printk(KERN_INFO
			       DRIVER_NAME ": RX-Fifo %i is going down (%i)\n", fifon,
			       urb->status);

		fifo->urb->interval = 0; /* cancel automatic rescheduling */
		return;
	}
	len = urb->actual_length;
	buf = fifo->buffer;
	maxlen = fifo->usb_packet_maxlen;

	/* USB data log for every D INT in */
	if ((fifon == HFCUSB_D_RX) && (debug & DBG_HFC_USB_VERBOSE)) {
		printk (KERN_INFO DRIVER_NAME ": D RX INT len(%d) ", len);
		for (i=0; i<len; i++)
			printk ("%02x ", buf[i]);
		printk ("\n");
	}

	if (fifo->last_urblen != fifo->usb_packet_maxlen) {
		/* the threshold mask is in the 2nd status byte */
		hw->threshold_mask = buf[1];

		/* signal S0 layer1 state change */
		if (hw->initdone && ((buf[0] >> 4) != hw->dch.state)) {
			hw->dch.state = (buf[0] >> 4);
			schedule_event(&hw->dch, FLG_PHCHANGE);
		}

		eof[fifon] = buf[0] & 1;
		/* if we have more than the 2 status bytes -> collect data */
		if (len > 2)
			hfcsusb_rx_frame(fifo, buf + 2,
					 urb->actual_length - 2,
					 (len < maxlen) ? eof[fifon] : 0);
	} else {
		hfcsusb_rx_frame(fifo, buf, urb->actual_length,
				 (len < maxlen) ? eof[fifon] : 0);
	}
	fifo->last_urblen = urb->actual_length;

	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status) {
		if (debug & DBG_HFC_URB_ERROR)
			printk(KERN_INFO
			       DRIVER_NAME ": error resubmitting URB at rx_int_complete...\n");
	}
}

/* transmit completion routine for all ISO tx fifos */
static void
tx_iso_complete(struct urb *urb)
{
	iso_urb_struct *context_iso_urb = (iso_urb_struct *) urb->context;
	usb_fifo *fifo = context_iso_urb->owner_fifo;
	hfcsusb_t *hw = fifo->hw;
	struct sk_buff *tx_skb;
	int k, tx_offset, num_isoc_packets, sink, remain, current_len,
	    errcode, hdlc, i;
	int *tx_idx;
	int frame_complete, fifon, status;
	__u8 threshbit;
#ifdef ISO_FRAME_START_DEBUG
	__u8 do_strt_frm_dbg;
#endif

	if (fifo->dch) {
		tx_skb = fifo->dch->tx_skb;
		tx_idx = &fifo->dch->tx_idx;
		hdlc = 1;
	}
	else if (fifo->bch) {
		tx_skb = fifo->bch->tx_skb;
		tx_idx = &fifo->bch->tx_idx;
		hdlc = test_bit(FLG_HDLC, &fifo->bch->Flags);
	}
	else {
		printk(KERN_INFO DRIVER_NAME ": %s: neither BCH not DCH -> exit\n", __FUNCTION__);
		return;
	}

	fifon = fifo->fifonum;
	status = urb->status;

	tx_offset = 0;

#ifdef ISO_FRAME_START_DEBUG
	do_strt_frm_dbg = 0;
	rem_frame_starts(urb, context_iso_urb, fifon);
#endif

	/*
	 * ISO transfer only partially completed,
	 * look at individual frame status for details
	 */
	if (status == -EXDEV) {
#ifdef ISO_FRAME_START_DEBUG
		do_strt_frm_dbg = 1;
#endif
		if (debug & DBG_HFC_URB_ERROR)
			printk(KERN_INFO
				DRIVER_NAME ": tx_iso_complete with -EXDEV (%i) "
				"fifon (%d)\n",
				status, fifon);

		// clear status, so go on with ISO transfers
		status = 0;
	}

	if (fifo->active && !status) {
		/* is FifoFull-threshold set for our channel? */
		threshbit = (hw->threshold_mask & (1 << fifon));
		num_isoc_packets = iso_packets[fifon];

		/* predict dataflow to avoid fifo overflow */
		if (fifon >= HFCUSB_D_TX) {
			sink = (threshbit) ? SINK_DMIN : SINK_DMAX;
		} else {
			sink = (threshbit) ? SINK_MIN : SINK_MAX;
		}
		fill_isoc_urb(urb, fifo->hw->dev, fifo->pipe,
			      context_iso_urb->buffer, num_isoc_packets,
			      fifo->usb_packet_maxlen, fifo->intervall,
			      (usb_complete_t)tx_iso_complete, urb->context);
		memset(context_iso_urb->buffer, 0,
		       sizeof(context_iso_urb->buffer));
		frame_complete = 0;

		for (k = 0; k < num_isoc_packets; ++k)
		{
			/* analyze tx success of previous ISO packets */
			if (debug & DBG_HFC_URB_ERROR) {
				errcode = urb->iso_frame_desc[k].status;
				if (errcode) {
					printk(KERN_INFO
					       DRIVER_NAME ": tx_iso_complete "
					       "ISO packet %i, status: %i\n",
					       k, errcode);
#ifdef ISO_FRAME_START_DEBUG
					do_strt_frm_dbg = 1;
#endif
				}
			}

			/* Generate next ISO Packets */
			if (tx_skb) {
				remain = tx_skb->len - *tx_idx;
			} else {
				remain = 0;
			}

			if (remain > 0) {
				/* we lower data margin every msec */
				fifo->bit_line -= sink;
				current_len = (0 - fifo->bit_line) / 8;

				/* maximum 15 byte for every ISO packet makes our life easier */
				if (current_len > 14)
					current_len = 14;
				current_len = (remain <= current_len) ? remain : current_len;

				/* how much bit do we put on the line? */
				fifo->bit_line += current_len * 8;

				context_iso_urb->buffer[tx_offset] = 0;
				if (current_len == remain) {
					if (hdlc) {
						/* here frame completion */
						context_iso_urb->buffer[tx_offset] = 1;
						/* add 2 byte flags and 16bit CRC at end of ISDN frame */
						fifo->bit_line += 32;
					}
					frame_complete = 1;
				}

				/* copy tx data to iso-urb buffer */
				memcpy(context_iso_urb->buffer + tx_offset + 1,
				       (tx_skb->data + *tx_idx), current_len);
				*tx_idx += current_len;

				/* define packet delimeters within the URB buffer */
				urb->iso_frame_desc[k].offset = tx_offset;
				urb->iso_frame_desc[k].length = current_len + 1;

				/* USB data log for every D ISO out */
				if ((fifon == HFCUSB_D_RX) && (debug & DBG_HFC_USB_VERBOSE)) {
					printk (KERN_INFO DRIVER_NAME ": D TX ISO (%d/%d) offset(%d) len(%d) ",
					        k, num_isoc_packets-1,
					        urb->iso_frame_desc[k].offset,
					        urb->iso_frame_desc[k].length);

					for (i=urb->iso_frame_desc[k].offset;
					     i<(urb->iso_frame_desc[k].offset + urb->iso_frame_desc[k].length);
					     i++)
						printk ("%x ", context_iso_urb->buffer[i]);

					printk (" skb->len(%i) tx-idx(%d)\n", tx_skb->len, *tx_idx);
				}

				tx_offset += (current_len + 1);
			} else {
				urb->iso_frame_desc[k].offset = tx_offset++;

				urb->iso_frame_desc[k].length = 1;
				fifo->bit_line -= sink;	/* we lower data margin every msec */

				if (fifo->bit_line < BITLINE_INF) {
					fifo->bit_line = BITLINE_INF;
				}
			}

			if (frame_complete) {
				frame_complete = 0;

				if (debug & DBG_HFC_FIFO_VERBOSE) {
					printk(KERN_INFO DRIVER_NAME ": %s: fifon(%i) new TX len(%i): ",
					       __FUNCTION__, fifon, tx_skb->len);
					i = 0;
					while (i < tx_skb->len)
						printk("%02x ", tx_skb->data[i++]);
					printk("\n");
				}

				dev_kfree_skb(tx_skb);
				tx_skb = NULL;
				if ((fifo->dch) && (get_next_dframe(fifo->dch))) {
					tx_skb = fifo->dch->tx_skb;
				}
				else if ((fifo->bch) && (get_next_bframe(fifo->bch))) {
					tx_skb = fifo->bch->tx_skb;
					hdlc = test_bit(FLG_HDLC, &fifo->bch->Flags);
				}
			}
		}
		errcode = usb_submit_urb(urb, GFP_ATOMIC);
		if (errcode < 0) {
			if (debug & DBG_HFC_URB_ERROR)
				printk(KERN_INFO
				       DRIVER_NAME ": error submitting ISO URB: %d \n",
				       errcode);
#ifdef HFCUSB_ISO_RESURRECTION
			printk(KERN_INFO DRIVER_NAME ": setting ISO resurrection "
			       "timer, fifo(%i) urb(%i)\n",
			       fifon, context_iso_urb->indx);

			if (timer_pending(&context_iso_urb->timer))
				del_timer(&context_iso_urb->timer);

			context_iso_urb->iso_res_cnt = 0;
			/* wait 1s for hcd hopfuly gets friendly again */
			context_iso_urb->timer.expires =
				jiffies + HZ;
			add_timer(&context_iso_urb->timer);
#endif
		}

		/*
		 * abuse DChannel tx iso completion to trigger NT mode state changes
		 * tx_iso_complete is assumed to be called every fifo->intervall ms
		 */
		if ((fifon == HFCUSB_D_TX) && (hw->portmode & ISDN_P_NT_S0)
		    && (hw->portmode & NT_ACTIVATION_TIMER)) {
			if ((--hw->nt_timer) < 0)
				schedule_event(&hw->dch, FLG_PHCHANGE);
		}

	} else {
		if (status && !hw->disc_flag) {
			if (debug & DBG_HFC_URB_ERROR)
				printk(KERN_INFO
				       DRIVER_NAME ": tx_iso_complete : urb->status %s (%i), fifonum=%d\n",
				       symbolic(urb_errlist, status), status,
				       fifon);
		}
	}

#ifdef ISO_FRAME_START_DEBUG
	if (do_strt_frm_dbg)
		debug_frame_starts(context_iso_urb, fifon);
#endif
}

#ifdef HFCUSB_ISO_RESURRECTION
/* try to restart ISO URB a few times */
static void
iso_resurrection(iso_urb_struct *iso_urb)
{
	int err, k, num_k;

	printk (DRIVER_NAME ": %s fifon(%i) urb(%i)\n",
	        __FUNCTION__,
	        iso_urb->owner_fifo->fifonum,
	        iso_urb->indx);

	usb_kill_urb(iso_urb->purb);

	/* reinit URB's payload container */
	num_k = iso_packets[iso_urb->owner_fifo->fifonum];
	memset(iso_urb->buffer, 0, ISO_BUFFER_SIZE);
	for (k = 0; k < num_k; k++) {
		iso_urb->purb->iso_frame_desc[k].offset = k;
		iso_urb->purb->iso_frame_desc[k].length = 1;
	}

	err = usb_submit_urb(iso_urb->purb, GFP_KERNEL);

	printk (DRIVER_NAME ": %s %i %i\n",
	        __FUNCTION__, iso_urb->iso_res_cnt, err);
	if (err < 0) {
		/* resurrection failed */
		if (iso_urb->iso_res_cnt++ < ISO_MAX_RES_CNT) {
			/* wait for hcd hopfuly gets friendly again */
			iso_urb->timer.expires =
				jiffies + ((iso_urb->iso_res_cnt << 1) * HZ);
			/* I'll be back */
			add_timer(&iso_urb->timer);
		}
	} else {
		/* resurrection done */
		iso_urb->iso_res_cnt=0;
	}
}
#endif

/*
 * allocs urbs and start isoc transfer with two pending urbs to avoid
 * gaps in the transfer chain
 */
static int
start_isoc_chain(usb_fifo * fifo, int num_packets_per_urb,
		 usb_complete_t complete, int packet_size)
{
	int i, k, errcode;

	if (debug)
		printk(KERN_INFO DRIVER_NAME ": starting ISO-chain for Fifo %i\n",
		       fifo->fifonum);

	/* allocate Memory for Iso out Urbs */
	for (i = 0; i < 2; i++) {
		if (!(fifo->iso[i].purb)) {
			fifo->iso[i].purb =
			    usb_alloc_urb(num_packets_per_urb, GFP_KERNEL);
			if (!(fifo->iso[i].purb)) {
				printk(KERN_INFO DRIVER_NAME
				       ": alloc urb for fifo %i failed!!!",
				       fifo->fifonum);
			}
			fifo->iso[i].owner_fifo = (struct usb_fifo *) fifo;
			fifo->iso[i].indx = i;
#ifdef ISO_FRAME_START_DEBUG
			fifo->iso[i].iso_frm_strt_pos = 0;
#endif

#ifdef HFCUSB_ISO_RESURRECTION
			init_timer(&fifo->iso[i].timer);
			fifo->iso[i].timer.data = (long) fifo->iso + i;
			fifo->iso[i].timer.function = (void *) iso_resurrection;
#endif

			/* Init the first iso */
			if (ISO_BUFFER_SIZE >=
			    (fifo->usb_packet_maxlen *
			     num_packets_per_urb)) {
				fill_isoc_urb(fifo->iso[i].purb,
					      fifo->hw->dev, fifo->pipe,
					      fifo->iso[i].buffer,
					      num_packets_per_urb,
					      fifo->usb_packet_maxlen,
					      fifo->intervall, complete,
					      &fifo->iso[i]);
				memset(fifo->iso[i].buffer, 0,
				       sizeof(fifo->iso[i].buffer));
				/* defining packet delimeters in fifo->buffer */
				for (k = 0; k < num_packets_per_urb; k++) {
					fifo->iso[i].purb->
					    iso_frame_desc[k].offset =
					    k * packet_size;
					fifo->iso[i].purb->
					    iso_frame_desc[k].length =
					    packet_size;
				}
			} else {
				printk(KERN_INFO DRIVER_NAME
				       ": ISO Buffer size to small!\n");
			}
		}
		fifo->bit_line = BITLINE_INF;

		errcode = usb_submit_urb(fifo->iso[i].purb, GFP_KERNEL);
		fifo->active = (errcode >= 0) ? 1 : 0;
		if (errcode < 0) {
			printk(KERN_INFO DRIVER_NAME ": %s  URB nr:%d\n",
			       symbolic(urb_errlist, errcode), i);
		};
	}
	return (fifo->active);
}

/* stops running iso chain and frees their pending urbs */
static void
stop_isoc_chain(usb_fifo * fifo)
{
	int i;

	for (i = 0; i < 2; i++) {
		if (fifo->iso[i].purb) {
			if (debug)
				printk(KERN_INFO DRIVER_NAME
				       ": %s for fifo %i.%i\n",
				       __FUNCTION__, fifo->fifonum, i);
			usb_kill_urb(fifo->iso[i].purb);
			usb_free_urb(fifo->iso[i].purb);
			fifo->iso[i].purb = NULL;
		}
	}
	if (fifo->urb) {
		usb_kill_urb(fifo->urb);
		usb_free_urb(fifo->urb);
		fifo->urb = NULL;
	}
	fifo->active = 0;
}

/* start the interrupt transfer for the given fifo */
static void
start_int_fifo(usb_fifo * fifo)
{
	int errcode;

	if (debug)
		printk(KERN_INFO DRIVER_NAME ": starting intr IN fifo:%d\n",
		       fifo->fifonum);

	if (!fifo->urb) {
		fifo->urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!fifo->urb)
			return;
	}
	usb_fill_int_urb(fifo->urb, fifo->hw->dev, fifo->pipe,
			 fifo->buffer, fifo->usb_packet_maxlen,
			 (usb_complete_t)rx_int_complete, fifo, fifo->intervall);
	fifo->active = 1;	/* must be marked active */
	errcode = usb_submit_urb(fifo->urb, GFP_KERNEL);
	if (errcode) {
		printk(KERN_INFO DRIVER_NAME
		       ": submit URB error(start_int_info): status:%i\n",
		       errcode);
		fifo->active = 0;
	}
}

static void
setPortMode(hfcsusb_t * hw)
{
	if (debug & DEBUG_HW)
		printk (KERN_INFO DRIVER_NAME ": %s %s\n", __FUNCTION__,
		        (hw->portmode & ISDN_P_TE_S0)?"TE":"NT");

	if (hw->portmode & ISDN_P_TE_S0) {
		write_usb(hw, HFCUSB_SCTRL, 0x40);
		write_usb(hw, HFCUSB_SCTRL_E, 0x00);
		write_usb(hw, HFCUSB_CLKDEL, CLKDEL_TE);
		write_usb(hw, HFCUSB_STATES, 3 | 0x10);
		write_usb(hw, HFCUSB_STATES, 3);
	} else {
		write_usb(hw, HFCUSB_SCTRL, 0x44);
		write_usb(hw, HFCUSB_SCTRL_E, 0x09);
		write_usb(hw, HFCUSB_CLKDEL, CLKDEL_NT);
		write_usb(hw, HFCUSB_STATES, 1 | 0x10);
		write_usb(hw, HFCUSB_STATES, 1);
	}
}

static void
reset_hfcsusb(hfcsusb_t * hw)
{
	usb_fifo *fifo;
	int i;

	if (debug & DEBUG_HW)
		printk (KERN_INFO DRIVER_NAME ": %s\n", __FUNCTION__);

	/* do Chip reset */
	write_usb(hw, HFCUSB_CIRM, 8);

	/* aux = output, reset off */
	write_usb(hw, HFCUSB_CIRM, 0x10);

	/* set USB_SIZE to match the the wMaxPacketSize for INT or BULK transfers */
	write_usb(hw, HFCUSB_USB_SIZE,
		  (hw->packet_size /
		   8) | ((hw->packet_size / 8) << 4));

	/* set USB_SIZE_I to match the the wMaxPacketSize for ISO transfers */
	write_usb(hw, HFCUSB_USB_SIZE_I, hw->iso_packet_size);

	/* enable PCM/GCI master mode */
	write_usb(hw, HFCUSB_MST_MODE1, 0);	/* set default values */
	write_usb(hw, HFCUSB_MST_MODE0, 1);	/* enable master mode */

	/* init the fifos */
	write_usb(hw, HFCUSB_F_THRES,
		  (HFCUSB_TX_THRESHOLD /
		   8) | ((HFCUSB_RX_THRESHOLD / 8) << 4));

	fifo = hw->fifos;
	for (i = 0; i < HFCUSB_NUM_FIFOS; i++) {
		write_usb(hw, HFCUSB_FIFO, i);	/* select the desired fifo */
		fifo[i].max_size =
		    (i <= HFCUSB_B2_RX) ? MAX_BCH_SIZE : MAX_DFRAME_LEN;
		fifo[i].last_urblen = 0;

		/* set 2 bit for D- & E-channel */
		write_usb(hw, HFCUSB_HDLC_PAR,
			  ((i <= HFCUSB_B2_RX) ? 0 : 2));

		/* enable all fifos */
		if (i == HFCUSB_D_TX) {
			write_usb(hw, HFCUSB_CON_HDLC,
			          (hw->portmode & ISDN_P_NT_S0) ? 0x08 : 0x09);
		} else {
			write_usb(hw, HFCUSB_CON_HDLC, 0x08);
		}
		write_usb(hw, HFCUSB_INC_RES_F, 2);	/* reset the fifo */
	}

	write_usb(hw, HFCUSB_SCTRL_R, 0);	/* disable both B receivers */
	handle_led(hw, LED_POWER_ON);
	// setPortMode(hw);
}

/* start USB data pipes dependand on device's endpoint configuration */
static void
hfcsusb_start_endpoint(hfcsusb_t * hw, int channel)
{
	/* quick check if endpoint already running */
	if ((channel == HFC_CHAN_D) && (hw->fifos[HFCUSB_D_RX].active))
		return;
	if ((channel == HFC_CHAN_B1) && (hw->fifos[HFCUSB_B1_RX].active))
		return;
	if ((channel == HFC_CHAN_B2) && (hw->fifos[HFCUSB_B2_RX].active))
		return;

	/* start rx endpoints using USB INT IN method */
	if (hw->cfg_used == CNF_3INT3ISO || hw->cfg_used == CNF_4INT3ISO) {
		switch(channel) {
			case HFC_CHAN_D:
				start_int_fifo(hw->fifos + HFCUSB_D_RX);
				break;
			case HFC_CHAN_B1:
				start_int_fifo(hw->fifos + HFCUSB_B1_RX);
				break;
			case HFC_CHAN_B2:
				start_int_fifo(hw->fifos + HFCUSB_B2_RX);
				break;
		}
	}

	/* start rx endpoints using USB ISO IN method */
	if (hw->cfg_used == CNF_3ISO3ISO || hw->cfg_used == CNF_4ISO3ISO) {
		switch(channel) {
			case HFC_CHAN_D:
				start_isoc_chain(hw->fifos + HFCUSB_D_RX,
				                 ISOC_PACKETS_D,
				                 (usb_complete_t)rx_iso_complete,
				                 16);
				break;
			case HFC_CHAN_B1:
				start_isoc_chain(hw->fifos + HFCUSB_B1_RX,
				                 ISOC_PACKETS_B,
				                 (usb_complete_t)rx_iso_complete,
				                 16);
				break;
			case HFC_CHAN_B2:
				start_isoc_chain(hw->fifos + HFCUSB_B2_RX,
				                 ISOC_PACKETS_B,
				                 (usb_complete_t)rx_iso_complete,
				                 16);
				break;
		}
	}

	/* start tx endpoints using USB ISO OUT method */
	switch(channel) {
		case HFC_CHAN_D:
			start_isoc_chain(hw->fifos + HFCUSB_D_TX,
			                 ISOC_PACKETS_B,
			                 (usb_complete_t)tx_iso_complete,
			                 1);
			break;
		case HFC_CHAN_B1:
			start_isoc_chain(hw->fifos + HFCUSB_B1_TX,
			                 ISOC_PACKETS_D,
			                 (usb_complete_t)tx_iso_complete,
			                 1);
			break;
		case HFC_CHAN_B2:
			start_isoc_chain(hw->fifos + HFCUSB_B2_TX,
			                 ISOC_PACKETS_B,
			                 (usb_complete_t)tx_iso_complete,
			                 1);
			break;
	}
}

/* stop USB data pipes dependand on device's endpoint configuration */
static void
hfcsusb_stop_endpoint(hfcsusb_t * hw, int channel)
{
	/* rx endpoints using USB INT IN method */
	if (hw->cfg_used == CNF_3INT3ISO || hw->cfg_used == CNF_4INT3ISO) {
		switch(channel) {
			case HFC_CHAN_D:
				usb_kill_urb(hw->fifos[HFCUSB_D_RX].urb);
				usb_free_urb(hw->fifos[HFCUSB_D_RX].urb);
				hw->fifos[HFCUSB_D_RX].urb = NULL;
				hw->fifos[HFCUSB_D_RX].active = 0;
				break;
			case HFC_CHAN_B1:
				usb_kill_urb(hw->fifos[HFCUSB_B1_RX].urb);
				usb_free_urb(hw->fifos[HFCUSB_B1_RX].urb);
				hw->fifos[HFCUSB_B1_RX].urb = NULL;
				hw->fifos[HFCUSB_B1_RX].active = 0;
				break;
			case HFC_CHAN_B2:
				usb_kill_urb(hw->fifos[HFCUSB_B2_RX].urb);
				usb_free_urb(hw->fifos[HFCUSB_B2_RX].urb);
				hw->fifos[HFCUSB_B2_RX].urb = NULL;
				hw->fifos[HFCUSB_B2_RX].active = 0;
				break;
		}
	}

	/* rx endpoints using USB ISO IN method */
	if (hw->cfg_used == CNF_3ISO3ISO || hw->cfg_used == CNF_4ISO3ISO) {
		switch(channel) {
			case HFC_CHAN_D:
				stop_isoc_chain(hw->fifos + HFCUSB_D_RX);
				break;
			case HFC_CHAN_B1:
				stop_isoc_chain(hw->fifos + HFCUSB_B1_RX);
				break;
			case HFC_CHAN_B2:
				stop_isoc_chain(hw->fifos + HFCUSB_B2_RX);
				break;
		}
	}

	/* tx endpoints using USB ISO OUT method */
	switch(channel) {
		case HFC_CHAN_D:
			stop_isoc_chain(hw->fifos + HFCUSB_D_TX);
			break;
		case HFC_CHAN_B1:
			stop_isoc_chain(hw->fifos + HFCUSB_B1_TX);
			break;
		case HFC_CHAN_B2:
			stop_isoc_chain(hw->fifos + HFCUSB_B2_TX);
			break;
	}
}


/* Hardware Initialization */
int
setup_hfcsusb(hfcsusb_t * hw)
{
	int i, err;
	u_char b;

	if (debug & DBG_HFC_CALL_TRACE)
		printk (KERN_INFO DRIVER_NAME ": %s\n",
		        __FUNCTION__);

	/* check the chip id */
	if (read_usb(hw, HFCUSB_CHIP_ID, &b) != 1) {
		printk(KERN_INFO DRIVER_NAME ": cannot read chip id\n");
		return (1);
	}
	if (b != HFCUSB_CHIPID) {
		printk(KERN_INFO DRIVER_NAME ": Invalid chip id 0x%02x\n", b);
		return (1);
	}

	/* first set the needed config, interface and alternate */
	err = usb_set_interface(hw->dev, hw->if_used, hw->alt_used);

	hw->disc_flag = 0;
	hw->led_state = 0;

	/* init the background machinery for control requests */
	hw->ctrl_read.bRequestType = 0xc0;
	hw->ctrl_read.bRequest = 1;
	hw->ctrl_read.wLength = cpu_to_le16(1);
	hw->ctrl_write.bRequestType = 0x40;
	hw->ctrl_write.bRequest = 0;
	hw->ctrl_write.wLength = 0;
	usb_fill_control_urb(hw->ctrl_urb,
			     hw->dev,
			     hw->ctrl_out_pipe,
			     (u_char *) & hw->ctrl_write,
			     NULL, 0, (usb_complete_t)ctrl_complete, hw);

	reset_hfcsusb(hw);

	/* Init All Fifos */
	for (i = 0; i < HFCUSB_NUM_FIFOS; i++) {
		hw->fifos[i].iso[0].purb = NULL;
		hw->fifos[i].iso[1].purb = NULL;
		hw->fifos[i].active = 0;
	}

#ifdef TEST_LEDS
	__u8 t;

	// LED test toggle sequence: USB, S0, B1, B2
	for (t=LED_POWER_ON; t<=LED_B2_OFF; t++) {
		handle_led(hw, t);
		set_current_state(TASK_UNINTERRUPTIBLE);
		printk (KERN_INFO DRIVER_NAME 
		        ": TEST_LED (%d) P_DATA: 0x%02x\n",
		        t, hw->led_state);
		schedule_timeout(2 * HZ);
	}
#endif

	return (0);
}

static void
release_hw(hfcsusb_t * hw)
{
	int	i;

	if (debug & DEBUG_HW)
		printk (KERN_INFO DRIVER_NAME ": %s\n", __FUNCTION__);

	/* tell all fifos to terminate */
	for (i = 0; i < HFCUSB_NUM_FIFOS; i++) {
		if (hw->fifos[i].usb_transfer_mode == USB_ISOC) {
			if (hw->fifos[i].active > 0) {
				stop_isoc_chain(&hw->fifos[i]);
			}
		} else {
			if (hw->fifos[i].active > 0) {
				hw->fifos[i].active = 0;
			}
			if (hw->fifos[i].urb) {
				usb_kill_urb(hw->fifos[i].urb);
				usb_free_urb(hw->fifos[i].urb);
				hw->fifos[i].urb = NULL;
			}
		}
		hw->fifos[i].active = 0;
	}

#if 0
	spin_lock_irqsave(&hw->lock, flags);
	// TODO mbachem
	// mode_hfcpci(&hw->bch[0], 1, ISDN_P_NONE);
	// mode_hfcpci(&hw->bch[1], 2, ISDN_P_NONE);
	if (hw->dch.timer.function != NULL) {
		del_timer(&hw->dch.timer);
		hw->dch.timer.function = NULL;
	}
	spin_unlock_irqrestore(&hw->lock, flags);

	// TODO mbachem
	/*
	if (hw->hw.nt_mode == 0) // TE Mode
		l1_event(hw->dch.l1, CLOSE_CHANNEL);
	*/
#endif
	mISDN_unregister_device(&hw->dch.dev);
	mISDN_freebchannel(&hw->bch[1]);
	mISDN_freebchannel(&hw->bch[0]);
	mISDN_freedchannel(&hw->dch);

	/* wait for all URBS to terminate */
	if (hw->ctrl_urb) {
		usb_kill_urb(hw->ctrl_urb);
		usb_free_urb(hw->ctrl_urb);
		hw->ctrl_urb = NULL;
	}
	hfcsusb_cnt--;
	if (hw->intf)
		usb_set_intfdata(hw->intf, NULL);

	list_del(&hw->list);
	kfree(hw);
}

static void
deactivate_bchannel(struct bchannel *bch)
{
	hfcsusb_t	*hw = bch->hw;

	if (bch->debug & DEBUG_HW)
		printk(KERN_INFO DRIVER_NAME
		       ": %s: bch->nr(%i)\n", __func__, bch->nr);

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

	hfcsusb_setup_bch(bch, ISDN_P_NONE);
	hfcsusb_stop_endpoint(hw, bch->nr);

	test_and_clear_bit(FLG_ACTIVE, &bch->Flags);
	test_and_clear_bit(FLG_TX_BUSY, &bch->Flags);
}

/*
 * Layer 1 B-channel hardware access
 */
static int
hfc_bctrl(struct mISDNchannel *ch, u_int cmd, void *arg)
{
	struct bchannel	*bch = container_of(ch, struct bchannel, ch);
	int		ret = -EINVAL;

	if (bch->debug & DEBUG_HW)
		printk(KERN_DEBUG "%s: cmd:%x %p\n", __func__, cmd, arg);

	switch (cmd) {
		case HW_TESTRX_RAW:
		case HW_TESTRX_HDLC:
		case HW_TESTRX_OFF:
			ret = -EINVAL;
			break;

		case CLOSE_CHANNEL:
			test_and_clear_bit(FLG_OPEN, &bch->Flags);
			if (test_bit(FLG_ACTIVE, &bch->Flags))
				deactivate_bchannel(bch);
			ch->protocol = ISDN_P_NONE;
			ch->peer = NULL;
			module_put(THIS_MODULE);
			ret = 0;
			break;
		case CONTROL_CHANNEL:
			ret = channel_bctrl(bch, arg);
			break;
		default:
			printk(KERN_WARNING "%s: unknown prim(%x)\n",
			       __func__, cmd);
	}
	return ret;
}

static int
setup_instance(hfcsusb_t * hw)
{
	u_long	flags;
	int	err, i;
	char	name[MISDN_MAX_IDLEN];

	if (debug & DBG_HFC_CALL_TRACE)
		printk (KERN_INFO DRIVER_NAME ": %s\n",
		        __FUNCTION__);

	spin_lock_init(&hw->ctrl_lock);
	spin_lock_init(&hw->lock);

	hw->dch.debug = debug & 0xFFFF;
	spin_lock_init(&hw->lock);
	mISDN_initdchannel(&hw->dch, MAX_DFRAME_LEN_L1, ph_state);
	hw->dch.hw = hw;
	hw->dch.dev.Dprotocols = (1 << ISDN_P_TE_S0) | (1 << ISDN_P_NT_S0);
	hw->dch.dev.Bprotocols = (1 << (ISDN_P_B_RAW & ISDN_P_B_MASK)) |
	    (1 << (ISDN_P_B_HDLC & ISDN_P_B_MASK));
	hw->dch.dev.D.send = hfcusb_l2l1D;
	hw->dch.dev.D.ctrl = hfc_dctrl;
	hw->dch.dev.nrbchan = 2;
	for (i=0; i<2; i++) {
		hw->bch[i].nr = i + 1;
		test_and_set_bit(i + 1, (u_long *)hw->dch.dev.channelmap);
		hw->bch[i].debug = debug;
		mISDN_initbchannel(&hw->bch[i], MAX_DATA_MEM);
		hw->bch[i].hw = hw;
		hw->bch[i].ch.send = hfcusb_l2l1B;
		hw->bch[i].ch.ctrl = hfc_bctrl;
		hw->bch[i].ch.nr = i + 1;
		list_add(&hw->bch[i].ch.list, &hw->dch.dev.bchannels);
	}

	hw->fifos[HFCUSB_B1_TX].bch = &hw->bch[0];
	hw->fifos[HFCUSB_B1_RX].bch = &hw->bch[0];
	hw->fifos[HFCUSB_B2_TX].bch = &hw->bch[1];
	hw->fifos[HFCUSB_B2_RX].bch = &hw->bch[1];
	hw->fifos[HFCUSB_D_TX].dch = &hw->dch;
	hw->fifos[HFCUSB_D_RX].dch = &hw->dch;

	err = setup_hfcsusb(hw);
	if (err)
		goto out;

	snprintf(name, MISDN_MAX_IDLEN - 1, "hfcs-usb.%d", hfcsusb_cnt + 1);
	hw->tn = hfcsusb_cnt;
	err = mISDN_register_device(&hw->dch.dev, name);
	if (err)
		goto out;

	hfcsusb_cnt++;
	write_lock_irqsave(&HFClock, flags);
	list_add_tail(&hw->list, &HFClist);
	write_unlock_irqrestore(&HFClock, flags);
	return (0);

out:
	mISDN_freebchannel(&hw->bch[1]);
	mISDN_freebchannel(&hw->bch[0]);
	mISDN_freedchannel(&hw->dch);
	kfree(hw);
	return err;
}

/* function called to probe a new plugged device */
static int
hfcsusb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	hfcsusb_t			*hw;
	struct usb_device		*dev = interface_to_usbdev(intf);
	struct usb_host_interface	*iface = intf->cur_altsetting;
	struct usb_host_interface	*iface_used = NULL;
	struct usb_host_endpoint	*ep;
	hfcsusb_vdata			*driver_info;
	int 				ifnum = iface->desc.bInterfaceNumber,
					i, idx, alt_idx, probe_alt_setting,
					vend_idx, cfg_used, *vcf,
					attr, cfg_found, ep_addr,
					cmptbl[16], small_match, iso_packet_size,
 					packet_size, alt_used = 0;
	

	vend_idx = 0xffff;
	for (i = 0; hfcsusb_idtab[i].idVendor; i++) {
		if ((le16_to_cpu(dev->descriptor.idVendor) == hfcsusb_idtab[i].idVendor)
		    && (le16_to_cpu(dev->descriptor.idProduct) == hfcsusb_idtab[i].idProduct)) {
			vend_idx = i;
			continue;
		}
	}

	printk(KERN_INFO DRIVER_NAME
	       ": probing interface(%d) actalt(%d) minor(%d) vend_idx(%d)\n",
	       ifnum, iface->desc.bAlternateSetting, intf->minor, vend_idx);

	if (vend_idx == 0xffff) {
		printk(KERN_WARNING DRIVER_NAME
		       ": no valid vendor found in USB descriptor\n");
		return (-EIO);
	}
	/* if vendor and product ID is OK, start probing alternate settings */
	alt_idx = 0;
	small_match = 0xffff;

	/* default settings */
	iso_packet_size = 16;
	packet_size = 64;

	while (alt_idx < intf->num_altsetting) {
		iface = intf->altsetting + alt_idx;
		probe_alt_setting = iface->desc.bAlternateSetting;
		cfg_used = 0;

		/* check for config EOL element */
		while (validconf[cfg_used][0]) {
			cfg_found = 1;
			vcf = validconf[cfg_used];
			/* first endpoint descriptor */
			ep = iface->endpoint;
			memcpy(cmptbl, vcf, 16 * sizeof(int));

			/* check for all endpoints in this alternate setting */
			for (i = 0; i < iface->desc.bNumEndpoints; i++) {
				ep_addr = ep->desc.bEndpointAddress;
				/* get endpoint base */
				idx = ((ep_addr & 0x7f) - 1) * 2;
				if (ep_addr & 0x80)
					idx++;
				attr = ep->desc.bmAttributes;
				if (cmptbl[idx] == EP_NUL) {
					cfg_found = 0;
				}
				if (attr == USB_ENDPOINT_XFER_INT
					&& cmptbl[idx] == EP_INT)
					cmptbl[idx] = EP_NUL;
				if (attr == USB_ENDPOINT_XFER_BULK
					&& cmptbl[idx] == EP_BLK)
					cmptbl[idx] = EP_NUL;
				if (attr == USB_ENDPOINT_XFER_ISOC
					&& cmptbl[idx] == EP_ISO)
					cmptbl[idx] = EP_NUL;

				/* check if all INT endpoints match minimum interval */
				if (attr == USB_ENDPOINT_XFER_INT &&
					ep->desc.bInterval < vcf[17]) {
					cfg_found = 0;
				}
				ep++;
			}
			for (i = 0; i < 16; i++) {
				/* all entries must be EP_NOP or EP_NUL for a valid config */
				if (cmptbl[i] != EP_NOP && cmptbl[i] != EP_NUL)
					cfg_found = 0;
			}
			if (cfg_found) {
				if (cfg_used < small_match) {
					small_match = cfg_used;
					alt_used = probe_alt_setting;
					iface_used = iface;
				}
			}
			cfg_used++;
		}
		alt_idx++;
	}	/* (alt_idx < intf->num_altsetting) */

	/* not found a valid USB Ta Endpoint config */
	if (small_match == 0xffff) {
		printk(KERN_WARNING DRIVER_NAME
		       ": no valid endpoint found in USB descriptor\n");
		return (-EIO);
	}
	iface = iface_used;
	hw = kzalloc(sizeof(hfcsusb_t), GFP_KERNEL);
	if (!hw)
		return (-ENOMEM);	/* got no mem */

	ep = iface->endpoint;
	vcf = validconf[small_match];

	for (i = 0; i < iface->desc.bNumEndpoints; i++) {
		usb_fifo	*f;

		ep_addr = ep->desc.bEndpointAddress;
		/* get endpoint base */
		idx = ((ep_addr & 0x7f) - 1) * 2;
		if (ep_addr & 0x80)
			idx++;
		f = &hw->fifos[idx & 7];

		/* init Endpoints */
		if (vcf[idx] == EP_NOP || vcf[idx] == EP_NUL) {
			ep++;
			continue;
		}
		switch (ep->desc.bmAttributes) {
			case USB_ENDPOINT_XFER_INT:
				f->pipe = usb_rcvintpipe(dev,
					ep->desc.bEndpointAddress);
				f->usb_transfer_mode = USB_INT;
				packet_size = le16_to_cpu(ep->desc.wMaxPacketSize);
				break;
			case USB_ENDPOINT_XFER_BULK:
				if (ep_addr & 0x80)
					f->pipe = usb_rcvbulkpipe(dev,
						ep->desc.bEndpointAddress);
				else
					f->pipe = usb_sndbulkpipe(dev,
						ep->desc.bEndpointAddress);
				f->usb_transfer_mode = USB_BULK;
				packet_size = le16_to_cpu(ep->desc.wMaxPacketSize);
				break;
			case USB_ENDPOINT_XFER_ISOC:
				if (ep_addr & 0x80)
					f->pipe = usb_rcvisocpipe(dev,
						ep->desc.bEndpointAddress);
				else
					f->pipe = usb_sndisocpipe(dev,
						ep->desc.bEndpointAddress);
				f->usb_transfer_mode = USB_ISOC;
				iso_packet_size = le16_to_cpu(ep->desc.wMaxPacketSize);
				break;
			default:
				f->pipe = 0;
		}	/* switch attribute */

		if (f->pipe) {
			f->fifonum = idx & 7;
			f->hw = hw;
			f->usb_packet_maxlen = le16_to_cpu(ep->desc.wMaxPacketSize);
			f->intervall = ep->desc.bInterval;
		}
		ep++;
	}
	hw->dev = dev;	/* save device */
	hw->if_used = ifnum;	/* save used interface */
	hw->alt_used = alt_used;	/* and alternate config */
	hw->ctrl_paksize = dev->descriptor.bMaxPacketSize0;	/* control size */
	hw->cfg_used = vcf[16];	/* store used config */
	hw->vend_idx = vend_idx;	/* store found vendor */
	hw->packet_size = packet_size;
	hw->iso_packet_size = iso_packet_size;

	/* create the control pipes needed for register access */
	hw->ctrl_in_pipe = usb_rcvctrlpipe(hw->dev, 0);
	hw->ctrl_out_pipe = usb_sndctrlpipe(hw->dev, 0);
	hw->ctrl_urb = usb_alloc_urb(0, GFP_KERNEL);

	driver_info = (hfcsusb_vdata *) hfcsusb_idtab[vend_idx].driver_info;
	printk(KERN_INFO DRIVER_NAME ": detected \"%s\" (%s, if=%d alt=%d)\n",
		driver_info->vend_name, conf_str[small_match],
		ifnum, alt_used);

	hw->intf = intf;
	if (setup_instance(hw)) {
		return (-EIO);
	}
	return (0);
}

/* function called when an active device is removed */
static void
hfcsusb_disconnect(struct usb_interface *intf)
{
	hfcsusb_t *hw = usb_get_intfdata(intf);

	printk(KERN_INFO DRIVER_NAME ": device disconnect\n");
	if (!hw) {
		if (debug & 0x10000)
			printk(KERN_INFO DRIVER_NAME "%i: %s : NO CONTEXT!\n",
			       hw->tn, __FUNCTION__);
		return;
	}
	if (debug & 0x10000)
		printk(KERN_INFO DRIVER_NAME ": %s\n", __FUNCTION__);
	hw->disc_flag = 1;

	usb_set_intfdata(intf, NULL);
}

static struct usb_driver hfcsusb_drv = {
	.name = DRIVER_NAME,
	.id_table = hfcsusb_idtab,
	.probe = hfcsusb_probe,
	.disconnect = hfcsusb_disconnect,
};

static int __init
hfcsusb_init(void)
{
	printk(KERN_INFO DRIVER_NAME " driver Rev. %s (debug=0x%x)\n", hfcsusb_rev, debug);

	if (usb_register(&hfcsusb_drv)) {
		printk(KERN_INFO DRIVER_NAME
		       ": Unable to register hfcsusb module at usb stack\n");
		return -ENODEV;
	}

	return 0;
}

static void __exit
hfcsusb_cleanup(void)
{
	u_long flags;
	hfcsusb_t *hw, *next;

	if (debug & 0x10000)
		printk(KERN_INFO DRIVER_NAME ": %s\n", __FUNCTION__);

	write_lock_irqsave(&HFClock, flags);
	list_for_each_entry_safe(hw, next, &HFClist, list) {
		handle_led(hw, LED_POWER_OFF);
	}
	list_for_each_entry_safe(hw, next, &HFClist, list) {
		release_hw(hw);
	}
	write_unlock_irqrestore(&HFClock, flags);

	/* unregister Hardware */
	usb_deregister(&hfcsusb_drv);	/* release our driver */
}

module_init(hfcsusb_init);
module_exit(hfcsusb_cleanup);

/* hwskel.c
 * basic skeleton mISDN layer1 driver
 *
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
 *
 * module params
 *   numports=<n>
 *      create <n> virtual mISDN layer1 devices
 *   debug=<n>, default=0, with n=0xHHHHGGGG
 *      H - l1 driver flags described in hfcs_usb.h
 *      G - common mISDN debug flags described at mISDNhw.h
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mISDNhw.h>
#include "hwskel.h"

const char *hwskel_rev = "Revision: 0.1.3 (socket), 2008-11-04";

static unsigned int debug = 0;
static unsigned int interfaces = 1;

static int hwskel_cnt;
static LIST_HEAD(hwskel_list);
static rwlock_t hwskel_lock = RW_LOCK_UNLOCKED;
struct hwskel * hw;

#ifdef MODULE
MODULE_AUTHOR("Martin Bachem");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
module_param(debug, uint, S_IRUGO | S_IWUSR);
module_param(interfaces, uint, 0);
#endif


/*
 * send full D/B channel status information
 * as MPH_INFORMATION_IND
 */
static void hwskel_ph_info(struct port * p) {
        struct ph_info * phi;
        struct dchannel * dch = &p->dch;
        int i;

        phi = kzalloc(sizeof(struct ph_info) +
                dch->dev.nrbchan * sizeof(struct ph_info_ch),
                GFP_ATOMIC);

        phi->dch.ch.protocol = p->protocol;
        phi->dch.ch.Flags = dch->Flags;
        phi->dch.state = dch->state;
        phi->dch.num_bch = dch->dev.nrbchan;
        for (i=0; i<dch->dev.nrbchan; i++) {
                phi->bch[i].protocol = p->bch[i].ch.protocol;
                phi->bch[i].Flags = p->bch[i].Flags;
        }
        _queue_data(&dch->dev.D,
                MPH_INFORMATION_IND, MISDN_ID_ANY,
                sizeof(struct ph_info_dch) +
                        dch->dev.nrbchan * sizeof(struct ph_info_ch),
                        phi, GFP_ATOMIC);
}

/*
 * Layer 1 callback function
 */
static int
hwskel_l1callback(struct dchannel *dch, u_int cmd)
{
	struct port *p = dch->hw;

	if (debug & DEBUG_HW)
		printk (KERN_DEBUG "%s: %s cmd 0x%x\n",
			p->name, __func__, cmd);

	switch (cmd) {
		case INFO3_P8:
		case INFO3_P10:
		case HW_RESET_REQ:
		case HW_POWERUP_REQ:
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
			if (dch->debug & DEBUG_HW)
				printk(KERN_DEBUG "%s: %s: unknown cmd %x\n",
				       p->name, __func__, cmd);
			return -1;
	}
	hwskel_ph_info(p);
	return 0;
}

/*
 * disable/enable BChannel for desired protocoll
 */
static int
hwskel_setup_bch(struct bchannel *bch, int protocol)
{
	struct port *p = bch->hw;

	if (debug)
		printk(KERN_DEBUG "%s: %s: protocol %x-->%x B%d\n",
			p->name, __func__, bch->state, protocol,
			bch->nr);

	switch (protocol) {
		case (-1):	/* used for init */
			bch->state = -1;
			/* fall trough */
		case (ISDN_P_NONE):
			if (bch->state == ISDN_P_NONE)
				return (0); /* already in idle state */
			bch->state = ISDN_P_NONE;
			clear_bit(FLG_HDLC, &bch->Flags);
			clear_bit(FLG_TRANSPARENT, &bch->Flags);
			break;
		case (ISDN_P_B_RAW):
			bch->state = protocol;
			set_bit(FLG_TRANSPARENT, &bch->Flags);
			break;
		case (ISDN_P_B_HDLC):
			bch->state = protocol;
			set_bit(FLG_HDLC, &bch->Flags);
			break;
		default:
			if (debug & DEBUG_HW)
				printk(KERN_DEBUG "%s: %s: prot not known %x\n",
					p->name, __func__, protocol);
			return (-ENOPROTOOPT);
	}
	hwskel_ph_info(p);
	return (0);
}

static void
deactivate_bchannel(struct bchannel *bch)
{
	struct port *p = bch->hw;
	u_long flags;

	if (bch->debug)
		printk(KERN_DEBUG "%s: %s: bch->nr(%i)\n",
		       p->name, __func__, bch->nr);

	spin_lock_irqsave(&p->lock, flags);
	mISDN_clear_bchannel(bch);
	spin_unlock_irqrestore(&p->lock, flags);

	hwskel_setup_bch(bch, ISDN_P_NONE);
}

/*
 * Layer2 -> Layer 1 Bchannel data
 */
static int
hwskel_l2l1B(struct mISDNchannel *ch, struct sk_buff *skb) {
	struct bchannel		*bch = container_of(ch, struct bchannel, ch);
	struct port 		*p = bch->hw;
	int			ret = -EINVAL;
	struct mISDNhead	*hh = mISDN_HEAD_P(skb);
	u_long			flags;

	if (debug & DEBUG_HW)
		printk (KERN_DEBUG "%s: %s\n", p->name, __func__);

	switch (hh->prim) {
		case PH_DATA_REQ:
			spin_lock_irqsave(&p->lock, flags);
			ret = bchannel_senddata(bch, skb);
			spin_unlock_irqrestore(&p->lock, flags);
			if (ret > 0) {
				ret = 0;
				queue_ch_frame(ch, PH_DATA_CNF, hh->id, NULL);

				// l1 virtual bchannel loop
				bch->rx_skb = skb_copy(skb, GFP_ATOMIC);
				if (bch->rx_skb) {
					recv_Bchannel(bch, MISDN_ID_ANY);
				} else {
					if (debug & DEBUG_HW)
						printk (KERN_ERR
							"%s: %s: mI_alloc_skb "
							"failed \n",
							p->name, __func__);
				}
				dev_kfree_skb(skb);
				skb = NULL;
				get_next_bframe(bch);
			}
			return ret;
		case PH_ACTIVATE_REQ:
			if (!test_and_set_bit(FLG_ACTIVE, &bch->Flags)) {
				ret = hwskel_setup_bch(bch, ch->protocol);
			} else
				ret = 0;
			if (!ret)
				_queue_data(ch, PH_ACTIVATE_IND, MISDN_ID_ANY,
					0, NULL, GFP_KERNEL);
			break;
		case PH_DEACTIVATE_REQ:
			deactivate_bchannel(bch);
			_queue_data(ch, PH_DEACTIVATE_IND, MISDN_ID_ANY,
				    0, NULL, GFP_KERNEL);
			ret = 0;
			break;
	}
	if (!ret)
		dev_kfree_skb(skb);
	return ret;
}

/* S0 phyical commandy (activattion / deactivation) */
static void
hwskel_ph_command(struct port * p, u_char command) {
	if (debug & DEBUG_HW)
		printk(KERN_DEBUG "%s: %s: %x\n",
		       p->name, __func__, command);
	switch (command) {
		case L1_ACTIVATE_TE:
			p->dch.state = 7;
			schedule_event(&p->dch, FLG_PHCHANGE);
			break;
		case L1_FORCE_DEACTIVATE_TE:
			p->dch.state = 3;
			schedule_event(&p->dch, FLG_PHCHANGE);
			break;
		case L1_ACTIVATE_NT:
			p->dch.state = 3;
			schedule_event(&p->dch, FLG_PHCHANGE);
			break;
		case L1_DEACTIVATE_NT:
			p->dch.state = 1;
			schedule_event(&p->dch, FLG_PHCHANGE);
			break;
	}
}

/*
 * Layer2 -> Layer 1 Dchannel data
 */
static int
hwskel_l2l1D(struct mISDNchannel *ch, struct sk_buff *skb) {
	struct mISDNdevice	*dev = container_of(ch, struct mISDNdevice, D);
	struct dchannel		*dch = container_of(dev, struct dchannel, dev);
	struct mISDNhead	*hh = mISDN_HEAD_P(skb);
	struct port		*p = dch->hw;
	int			ret = -EINVAL;
	u_long			flags;

	switch (hh->prim) {
		case PH_DATA_REQ:
			if (debug & DEBUG_HW)
				printk (KERN_DEBUG "%s: %s: PH_DATA_REQ\n",
					p->name, __func__);

			spin_lock_irqsave(&p->lock, flags);
			ret = dchannel_senddata(dch, skb);
			spin_unlock_irqrestore(&p->lock, flags);
			if (ret > 0) {
				ret = 0;
				queue_ch_frame(ch, PH_DATA_CNF, hh->id, NULL);

				// l1 virtual bchannel loop
				dch->rx_skb = skb_copy(skb, GFP_ATOMIC);
				if (dch->rx_skb)
					recv_Dchannel(dch);
				else
					if (debug & DEBUG_HW)
						printk (KERN_ERR
							"%s: %s: mI_alloc_skb "
							"failed \n",
							p->name, __func__);
				dev_kfree_skb(skb);
				skb = NULL;
				get_next_dframe(dch);
			}
			return ret;

		case PH_ACTIVATE_REQ:
			if (debug & DEBUG_HW)
				printk (KERN_DEBUG
					"%s: %s: PH_ACTIVATE_REQ %s\n",
					p->name, __func__,
					(p->protocol == ISDN_P_NT_S0)?
							"NT":"TE");

			if (p->protocol == ISDN_P_NT_S0) {
				ret = 0;
				if (test_bit(FLG_ACTIVE, &dch->Flags)) {
					_queue_data(&dch->dev.D,
						PH_ACTIVATE_IND, MISDN_ID_ANY, 0,
						NULL, GFP_ATOMIC);
				} else {
					hwskel_ph_command(p, L1_ACTIVATE_NT);
					test_and_set_bit(FLG_L2_ACTIVATED,
							&dch->Flags);
				}
			} else {
				hwskel_ph_command(p, L1_ACTIVATE_TE);
				ret = l1_event(dch->l1, hh->prim);
			}
			break;

		case PH_DEACTIVATE_REQ:
			if (debug & DEBUG_HW)
				printk (KERN_DEBUG
					"%s: %s: PH_DEACTIVATE_REQ\n",
					p->name, __func__);
			test_and_clear_bit(FLG_L2_ACTIVATED, &dch->Flags);

			if (p->protocol == ISDN_P_NT_S0) {
				hwskel_ph_command(p, L1_DEACTIVATE_NT);
				spin_lock_irqsave(&p->lock, flags);
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
				spin_unlock_irqrestore(&p->lock, flags);
				ret = 0;
			} else {
				ret = l1_event(dch->l1, hh->prim);
			}
			break;

		case MPH_INFORMATION_REQ:
			hwskel_ph_info(p);
			ret = 0;
			break;
	}
	return ret;
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
		case MISDN_CTRL_FILL_EMPTY:
			test_and_set_bit(FLG_FILLEMPTY, &bch->Flags);
			if (debug & DEBUG_HW_OPEN)
				printk(KERN_DEBUG
					"%s: FILL_EMPTY request (nr=%d "
					"off=%d)\n", __func__, bch->nr,
					!!cq->p1);
			break;
		default:
			printk(KERN_WARNING "%s: unknown Op %x\n",
			       __func__, cq->op);
			ret = -EINVAL;
			break;
	}
	return ret;
}

/*
 * Layer 1 B-channel hardware access
 */
static int
hwskel_bctrl(struct mISDNchannel *ch, u_int cmd, void *arg)
{
	struct bchannel *bch = container_of(ch, struct bchannel, ch);
	int ret = -EINVAL;

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
open_dchannel(struct port *p, struct mISDNchannel *ch, struct channel_req *rq)
{
	int err = 0;

	if (debug & DEBUG_HW_OPEN)
		printk(KERN_DEBUG "%s: %s: dev(%d) open from %p\n",
			p->name, __func__, p->dch.dev.id,
			__builtin_return_address(0));
	if (rq->protocol == ISDN_P_NONE)
		return -EINVAL;

	test_and_clear_bit(FLG_ACTIVE, &p->dch.Flags);
	p->dch.state = 0;

	if (!p->initdone) {
		if (rq->protocol == ISDN_P_TE_S0) {
			p->protocol = ISDN_P_TE_S0;
			err = create_l1(&p->dch, hwskel_l1callback);
			if (err)
				return err;
		} else {
			p->protocol = ISDN_P_NT_S0;
		}
		ch->protocol = rq->protocol;
		p->initdone = 1;
	} else {
		if (rq->protocol != ch->protocol)
			return -EPROTONOSUPPORT;
	}

	if (((ch->protocol == ISDN_P_NT_S0) && (p->dch.state == 3)) ||
	    ((ch->protocol == ISDN_P_TE_S0) && (p->dch.state == 7)))
		_queue_data(ch, PH_ACTIVATE_IND, MISDN_ID_ANY,
			    0, NULL, GFP_KERNEL);

	rq->ch = ch;
	if (!try_module_get(THIS_MODULE))
		printk(KERN_WARNING "%s: %s: cannot get module\n",
		       p->name, __func__);
	return 0;
}

static int
open_bchannel(struct port * p, struct channel_req *rq)
{
	struct bchannel *bch;

	if (rq->adr.channel > 2)
		return -EINVAL;
	if (rq->protocol == ISDN_P_NONE)
		return -EINVAL;

	if (debug & DEBUG_HW)
		printk (KERN_DEBUG "%s: %s B%i\n",
			p->name, __func__, rq->adr.channel);

	bch = &p->bch[rq->adr.channel - 1];
	if (test_and_set_bit(FLG_OPEN, &bch->Flags))
		return -EBUSY; /* b-channel can be only open once */
	test_and_clear_bit(FLG_FILLEMPTY, &bch->Flags);
	bch->ch.protocol = rq->protocol;
	rq->ch = &bch->ch;

	if (!try_module_get(THIS_MODULE))
		printk(KERN_WARNING "%s: %s:cannot get module\n",
		       p->name, __func__);
	return 0;
}


static int
channel_ctrl(struct port *p, struct mISDN_ctrl_req *cq)
{
	int ret = 0;

	if (debug)
		printk (KERN_DEBUG "%s: %s op(0x%x) channel(0x%x)\n",
			p->name, __func__, (cq->op), (cq->channel));

	switch(cq->op) {
		case MISDN_CTRL_GETOP:
			cq->op = MISDN_CTRL_LOOP | MISDN_CTRL_CONNECT |
					MISDN_CTRL_DISCONNECT;
			break;
		default:
			printk(KERN_WARNING "%s: %s: unknown Op %x\n",
			       p->name, __func__, cq->op);
			ret= -EINVAL;
			break;
	}
	return ret;
}

/*
 * device control function
 */
static int
hwskel_dctrl(struct mISDNchannel *ch, u_int cmd, void *arg) {
	struct mISDNdevice	*dev = container_of(ch, struct mISDNdevice, D);
	struct dchannel		*dch = container_of(dev, struct dchannel, dev);
	struct port		*p = dch->hw;
	struct channel_req	*rq;
	int			err = 0;

	if (dch->debug & DEBUG_HW)
		printk(KERN_DEBUG "%s: %s: cmd:%x %p\n",
		       p->name, __func__, cmd, arg);
	switch (cmd) {
		case OPEN_CHANNEL:
			rq = arg;
			if ((rq->protocol == ISDN_P_TE_S0) ||
			    (rq->protocol == ISDN_P_NT_S0))
				err = open_dchannel(p, ch, rq);
			else
				err = open_bchannel(p, rq);
			break;
		case CLOSE_CHANNEL:
			if (debug & DEBUG_HW_OPEN)
				printk(KERN_DEBUG
					"%s: %s: dev(%d) close from %p\n",
					p->name, __func__, p->dch.dev.id,
					__builtin_return_address(0));
			module_put(THIS_MODULE);
			break;
		case CONTROL_CHANNEL:
			err = channel_ctrl(p, arg);
			break;
		default:
			if (dch->debug & DEBUG_HW)
				printk(KERN_DEBUG
					"%s: %s: unknown command %x\n",
					p->name, __func__, cmd);
			return -EINVAL;
	}
	return err;
}

/*
 * S0 TE state change event handler
 */
static void
ph_state_te(struct dchannel * dch)
{
	struct port * p = dch->hw;

	if (debug)
		printk (KERN_DEBUG "%s: %s: TE F%d\n",
			p->name, __func__, dch->state);

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
}

/*
 * S0 NT state change event handler
 */
static void
ph_state_nt(struct dchannel * dch)
{
	struct port * p = dch->hw;

	if (debug & DEBUG_HW)
		printk (KERN_INFO DRIVER_NAME "%s: %s: NT G%d\n",
			p->name, __func__, dch->state);

	switch (dch->state) {
		case (1):
			test_and_clear_bit(FLG_ACTIVE, &dch->Flags);
			test_and_clear_bit(FLG_L2_ACTIVATED, &dch->Flags);
			p->nt_timer = 0;
			p->timers &= ~NT_ACTIVATION_TIMER;
			break;
		case (2):
			if (p->nt_timer < 0) {
				p->nt_timer = 0;
				p->timers &= ~NT_ACTIVATION_TIMER;
			} else {
				p->timers |= NT_ACTIVATION_TIMER;
				p->nt_timer = NT_T1_COUNT;
			}
			break;
		case (3):
			p->nt_timer = 0;
			p->timers &= ~NT_ACTIVATION_TIMER;
			test_and_set_bit(FLG_ACTIVE, &dch->Flags);
			_queue_data(&dch->dev.D, PH_ACTIVATE_IND,
				     MISDN_ID_ANY, 0, NULL, GFP_ATOMIC);
			break;
		case (4):
			p->nt_timer = 0;
			p->timers &= ~NT_ACTIVATION_TIMER;
			return;
		default:
			break;
	}
	hwskel_ph_info(p);
}

static void
ph_state(struct dchannel *dch)
{
	struct port *p = dch->hw;
	if (p->protocol == ISDN_P_NT_S0)
		ph_state_nt(dch);
	else if (p->protocol == ISDN_P_TE_S0)
		ph_state_te(dch);
}

static int
setup_instance(struct hwskel *hw) {
	struct port *p;
	u_long	flags;
	int	err=0, i, j;

	if (debug)
		printk (KERN_DEBUG "%s: %s\n", DRIVER_NAME, __func__);

	spin_lock_init(&hw->lock);

	for (i=0; i<interfaces; i++) {
		p = hw->ports + i;
		spin_lock_init(&p->lock);
		mISDN_initdchannel(&p->dch, MAX_DFRAME_LEN_L1, ph_state);
		p->dch.debug = debug & 0xFFFF;
		p->dch.hw = p;
		p->dch.dev.Dprotocols = (1 << ISDN_P_TE_S0) |
				(1 << ISDN_P_NT_S0);
		p->dch.dev.Bprotocols = (1 << (ISDN_P_B_RAW & ISDN_P_B_MASK)) |
				(1 << (ISDN_P_B_HDLC & ISDN_P_B_MASK));
		p->dch.dev.D.send = hwskel_l2l1D;
		p->dch.dev.D.ctrl = hwskel_dctrl;
		p->dch.dev.nrbchan = 2;
		for (j=0; j<2; j++) {
			p->bch[j].nr = j + 1;
			set_channelmap(j + 1, p->dch.dev.channelmap);
			p->bch[j].debug = debug;
			/* minimum transparent datalen -1 cause that
			 * the driver will send what it has.
			 * If a positive value is set, the driver should
			 * only send a packet upstream, if this size is
			 * reached.
			 */
			mISDN_initbchannel(&p->bch[j], MAX_DATA_MEM, 0);
			p->bch[j].hw = p;
			p->bch[j].ch.send = hwskel_l2l1B;
			p->bch[j].ch.ctrl = hwskel_bctrl;
			p->bch[j].ch.nr = j + 1;
			list_add(&p->bch[j].ch.list, &p->dch.dev.bchannels);
		}

		snprintf(p->name, MISDN_MAX_IDLEN - 1, "%s.%d", DRIVER_NAME,
			 hwskel_cnt + 1);
		printk (KERN_INFO "%s: registered as '%s'\n",
			DRIVER_NAME, p->name);

		err = mISDN_register_device(&p->dch.dev, NULL, p->name);
		if (err) {
			mISDN_freebchannel(&p->bch[1]);
			mISDN_freebchannel(&p->bch[0]);
			mISDN_freedchannel(&p->dch);
		} else {
			hwskel_cnt++;
			write_lock_irqsave(&hwskel_lock, flags);
			list_add_tail(&hw->list, &hwskel_list);
			write_unlock_irqrestore(&hwskel_lock, flags);
		}
	}

	return err;
}

static int
release_instance(struct hwskel *hw) {
	struct port * p;
	int i;

	if (debug)
		printk (KERN_DEBUG "%s: %s\n", DRIVER_NAME, __func__);

	for (i=0; i<interfaces; i++) {
		p = hw->ports + i;
		hwskel_setup_bch(&p->bch[0], ISDN_P_NONE);
		hwskel_setup_bch(&p->bch[1], ISDN_P_NONE);
		if (p->protocol == ISDN_P_TE_S0)
			l1_event(p->dch.l1, CLOSE_CHANNEL);

		mISDN_unregister_device(&p->dch.dev);
		mISDN_freebchannel(&p->bch[1]);
		mISDN_freebchannel(&p->bch[0]);
		mISDN_freedchannel(&p->dch);
	}

	if (hw) {
		if (hw->ports)
			kfree(hw->ports);
		kfree(hw);
	}
	return 0;
}

static int __init
hwskel_init(void)
{
	if (interfaces <= 0)
		interfaces = 1;
	if (interfaces > 64)
		interfaces = 64;

	printk(KERN_INFO DRIVER_NAME " driver Rev. %s "
		"debug(0x%x) interfaces(%i)\n",
		hwskel_rev, debug, interfaces);

	if (!(hw = kzalloc(sizeof(struct hwskel), GFP_KERNEL))) {
		printk(KERN_ERR "%s: %s: no kmem for hw\n",
		       DRIVER_NAME, __FUNCTION__);
		return -ENOMEM;
	}

	if (!(hw->ports = kzalloc(sizeof(struct port)*interfaces, GFP_KERNEL))) {
		printk(KERN_ERR "%s: %s: no kmem for interfaces\n",
		       DRIVER_NAME, __FUNCTION__);
		kfree(hw);
		return -ENOMEM;
	}

	return (setup_instance(hw));
}

static void __exit
hwskel_cleanup(void)
{
	if (debug)
		printk(KERN_INFO DRIVER_NAME ": %s\n", __func__);

	release_instance(hw);
}

module_init(hwskel_init);
module_exit(hwskel_cleanup);

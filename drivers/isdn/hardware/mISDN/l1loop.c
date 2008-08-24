/* l1loop.c
 * virtual mISDN layer1 driver
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
 * module params:
 *
 * - interfaces=<n>, default 1, min: 1, max: 64
 * - vline=<n>, default 1
 *     0 : no virtual line at all, all interfaces behave like
 *         ISDN TAs without any bus connection
 *     1 : all interfaces are connected to one virtual
 *         ISDN bus. the first interface opened as NT interface
 *         will configure all others as TE interface
 *     2 : each interface is virtually equipped with a cross
 *         connector, so all data is looped internally
 * - numbch=<n>, default 2, min: 2, max: 30
 *     number of bchannels each interface will consist of
 * - debug=<n>, default=0, with n=0xHHHHGGGG
 *      H - l1 driver flags described in hfcs_usb.h
 *      G - common mISDN debug flags described at mISDNhw.h
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mISDNhw.h>
#include <linux/isdn_compat.h>
#include "l1loop.h"

const char *l1loop_rev = "Revision: 0.1.2 (socket), 2008-08-24";


static int l1loop_cnt;
static LIST_HEAD(l1loop_list);
static rwlock_t l1loop_lock = RW_LOCK_UNLOCKED;
struct l1loop * hw;
struct port *vbusnt = NULL; /* NT of virtual S0 bus when using vline=1 */

/* module params */
static unsigned int interfaces = 1;
static unsigned int vline = 1;
static unsigned int numbch = 2;
static unsigned int debug = 0;

#ifdef MODULE
MODULE_AUTHOR("Martin Bachem");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
module_param(interfaces, uint, 0);
module_param(vline, uint, 0);
module_param(numbch, uint, 0);
module_param(debug, uint, 0);
#endif

/* forward function prototypes */
static void ph_state(struct dchannel * dch);

/*
 * disable/enable BChannel for desired protocoll
 */
static int
l1loop_setup_bch(struct bchannel *bch, int protocol)
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
	return (0);
}

static void
deactivate_bchannel(struct bchannel *bch)
{
	struct port *p = bch->hw;

	if (bch->debug)
		printk(KERN_DEBUG "%s: %s: bch->nr(%i)\n",
		       p->name, __func__, bch->nr);

	spin_lock(&p->lock);
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
	clear_bit(FLG_ACTIVE, &bch->Flags);
	clear_bit(FLG_TX_BUSY, &bch->Flags);
	spin_unlock(&p->lock);

	l1loop_setup_bch(bch, ISDN_P_NONE);
}

/*
 * bch layer1 loop (vline=2): immediatly loop back every Bchannel data
 */
static void bch_vline_loop(struct bchannel *bch, struct sk_buff *skb)
{
	struct port *p = bch->hw;

	bch->rx_skb = skb_copy(skb, GFP_ATOMIC);
	if (bch->rx_skb)
		recv_Bchannel(bch);
	else
		if (debug & DEBUG_HW)
			printk (KERN_ERR "%s: %s: mI_alloc_skb failed \n",
				p->name, __func__);
	dev_kfree_skb(skb);
	skb = NULL;
	get_next_bframe(bch);
}

/*
 * bch layer1 bus (vline=1): copy frame to party with bch(x) FLAG_ACTIVE
 */
static void bch_vbus(struct bchannel *bch, struct sk_buff *skb)
{
	struct port *me = bch->hw;
	struct port *party;
	struct bchannel *target = NULL;
	int i, b;
	
	b = bch->nr-1;

	/* TE sends bch frame to NT */
	if ((me->portmode == ISDN_P_TE_S0) && vbusnt && vbusnt->opened)
		if (test_bit(FLG_ACTIVE, &vbusnt->bch[b].Flags)) {
			target = &vbusnt->bch[b];
		}

	/* NT sends bch frame to TE */
	if ((me->portmode == ISDN_P_NT_S0) && vbusnt && vbusnt->opened) {
		for (i=0; i<interfaces; i++) {
			party = hw->ports + i;
			if ((me != party) && party->opened &&
						  test_bit(FLG_ACTIVE,
					&party->bch[b].Flags)) {
				target = &party->bch[b];
			}
		}
	}

	if (target) {
		target->rx_skb = skb_copy(skb, GFP_ATOMIC);
		if (target->rx_skb)
			recv_Bchannel(target);
	}

	dev_kfree_skb(skb);
	skb = NULL;
	get_next_bframe(bch);
}

/*
 * Layer2 -> Layer 1 Bchannel data
 */
static int
l1loop_l2l1B(struct mISDNchannel *ch, struct sk_buff *skb) {
	struct bchannel		*bch = container_of(ch, struct bchannel, ch);
	struct port 		*p = bch->hw;
	int			ret = -EINVAL;
	struct mISDNhead	*hh = mISDN_HEAD_P(skb);

	switch (hh->prim) {
		case PH_DATA_REQ:
			spin_lock(&p->lock);
			ret = bchannel_senddata(bch, skb);
			spin_unlock(&p->lock);
			if (ret > 0) {
				ret = 0;
				queue_ch_frame(ch, PH_DATA_CNF, hh->id, NULL);
				switch (vline) {
					case VLINE_BUS:
						bch_vbus(bch, skb);
						break;
					case VLINE_LOOP:
						bch_vline_loop(bch, skb);
						break;
					case VLINE_NONE:
					default:
						break;
				}
			}
			return ret;
		case PH_ACTIVATE_REQ:
			if (!test_and_set_bit(FLG_ACTIVE, &bch->Flags))
				ret = l1loop_setup_bch(bch, ch->protocol);
			else
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

/*
 * every activation (NT and TE) causes every listening party to activate
 */
static void
vbus_activate(struct port *p)
{
	int i;

	if ((vbusnt) && (vbusnt->opened)) {
		for (i=0; i<interfaces; i++) {
			p = hw->ports + i;
			if (p->opened) {
				p->dch.state = VBUS_ACTIVE;
				ph_state(&p->dch);
			}
		}
	}
	else {
		// no interfaces opened as NT yet
		p->dch.state = VBUS_INACTIVE;
		ph_state(&p->dch);
	}
}

static void
vbus_deactivate(struct port *p)
{
	struct dchannel *dch;
	int i;

	for (i=0; i<interfaces; i++) {
		p = hw->ports + i;
		if (p->opened) {
			dch = &p->dch;
			dch->state = VBUS_ACTIVE;
			ph_state(dch);
		}
	}
}

static void
ph_command(struct port * p, u_char command) {
	if (debug & DEBUG_HW)
		printk(KERN_DEBUG "%s: %s: %x\n",
		       p->name, __func__, command);
	switch (command) {
		case L1_ACTIVATE_TE:
			switch (vline) {
				case VLINE_BUS:
					vbus_activate(p);
					break;
				case VLINE_LOOP:
					p->dch.state = VBUS_ACTIVE;
					schedule_event(&p->dch, FLG_PHCHANGE);
					break;
				case VLINE_NONE:
				default:
					p->dch.state = VBUS_INACTIVE;
					schedule_event(&p->dch, FLG_PHCHANGE);
					break;
			}
			break;
		case L1_ACTIVATE_NT:
			switch (vline) {
				case VLINE_BUS:
					vbus_activate(p);
					break;
				case VLINE_LOOP:
					p->dch.state = VBUS_ACTIVE;
					schedule_event(&p->dch, FLG_PHCHANGE);
					break;
				case VLINE_NONE:
				default:
					p->dch.state = VBUS_INACTIVE;
					schedule_event(&p->dch, FLG_PHCHANGE);
					break;
			}
			break;
		case L1_DEACTIVATE_NT:
			switch (vline) {
				case VLINE_BUS:
					vbus_deactivate(p);
					break;
				case VLINE_LOOP:
					p->dch.state = VBUS_ACTIVE;
					schedule_event(&p->dch, FLG_PHCHANGE);
				case VLINE_NONE:
				default:
					break;
			}
			break;
	}
}

/*
 * dch layer1 loop (vline=2): immediatly loop back every Dchannel data
 */
static void dch_vline_loop(struct dchannel *dch, struct sk_buff *skb)
{
	struct port *p = dch->hw;

	dch->rx_skb = skb_copy(skb, GFP_ATOMIC);
	if (dch->rx_skb)
		recv_Dchannel(dch);
	else
		if (debug & DEBUG_HW)
			printk (KERN_ERR "%s: %s: mI_alloc_skb failed \n",
				p->name, __func__);
	dev_kfree_skb(skb);
	skb = NULL;
	get_next_dframe(dch);
}

/*
 * dch layer1 bus (vline=1): copy frame to all partys
 */
static void dch_vbus(struct dchannel *dch, struct sk_buff *skb)
{
	struct port *me = dch->hw;
	struct port *party;
	struct dchannel *party_dch;
	int i;

	for (i=0; i<interfaces; i++) {
		party = hw->ports + i;
		if ((me != party) && party->opened) {
			party_dch = &party->dch;
			party_dch->rx_skb = skb_copy(skb, GFP_ATOMIC);
			if (party_dch->rx_skb)
				recv_Dchannel(party_dch);
		}
	}
	dev_kfree_skb(skb);
	skb = NULL;
	get_next_dframe(dch);
}

/*
 * Layer2 -> Layer 1 Dchannel data
 */
static int
l1loop_l2l1D(struct mISDNchannel *ch, struct sk_buff *skb) {
	struct mISDNdevice	*dev = container_of(ch, struct mISDNdevice, D);
	struct dchannel		*dch = container_of(dev, struct dchannel, dev);
	struct mISDNhead	*hh = mISDN_HEAD_P(skb);
	struct port		*p = dch->hw;
	int			ret = -EINVAL;

	switch (hh->prim) {
		case PH_DATA_REQ:
			spin_lock(&p->lock);
			ret = dchannel_senddata(dch, skb);
			spin_unlock(&p->lock);
			if (ret > 0) {
				ret = 0;
				queue_ch_frame(ch, PH_DATA_CNF, hh->id, NULL);
				switch (vline) {
					case VLINE_BUS:
						dch_vbus(dch, skb);
						break;
					case VLINE_LOOP:
						dch_vline_loop(dch, skb);
						break;
					case VLINE_NONE:
					default:
						break;
				}
			}
			return ret;

		case PH_ACTIVATE_REQ:
			if (debug & DEBUG_HW)
				printk (KERN_DEBUG
					"%s: %s: PH_ACTIVATE_REQ %s\n",
					p->name, __func__,
					(p->portmode == ISDN_P_TE_S0)?
							"TE":"NT");
			ret = 0;
			if (p->portmode == ISDN_P_NT_S0) {
				if (test_bit(FLG_ACTIVE, &dch->Flags))
					_queue_data(&dch->dev.D,
						PH_ACTIVATE_IND, MISDN_ID_ANY,
						0, NULL, GFP_ATOMIC);
				else {
					ph_command(p, L1_ACTIVATE_NT);
					test_and_set_bit(FLG_L2_ACTIVATED,
						&dch->Flags);
				}
			} else {
				if (test_bit(FLG_ACTIVE, &dch->Flags))
					_queue_data(&dch->dev.D,
						PH_ACTIVATE_CNF, MISDN_ID_ANY,
						0, NULL, GFP_ATOMIC);
				else
					ph_command(p, L1_ACTIVATE_TE);
			}
			break;

		case PH_DEACTIVATE_REQ:
			if (debug & DEBUG_HW)
				printk (KERN_DEBUG
					"%s: %s: PH_DEACTIVATE_REQ %s\n",
					p->name, __func__,
					(p->portmode == ISDN_P_TE_S0)?
							"TE":"NT");
			test_and_clear_bit(FLG_L2_ACTIVATED, &dch->Flags);

			if (p->portmode == ISDN_P_NT_S0)
				ph_command(p, L1_DEACTIVATE_NT);

			spin_lock(&p->lock);
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
			spin_unlock(&p->lock);
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
l1loop_bctrl(struct mISDNchannel *ch, u_int cmd, void *arg)
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
open_dchannel(struct port *p, struct mISDNchannel *ch, struct channel_req *rq)
{
	if (debug & DEBUG_HW_OPEN)
		printk(KERN_DEBUG "%s: %s: dev(%d) open from %p\n",
			p->name, __func__, p->dch.dev.id,
			__builtin_return_address(0));
	if (rq->protocol == ISDN_P_NONE)
		return -EINVAL;

	test_and_clear_bit(FLG_ACTIVE, &p->dch.Flags);
	p->dch.state = 0;

	if (!p->initdone) {
		/*
		 * first process claiming NT sets the one and only NT when
		 * using vline=VLINE_BUS (default)
		 */
		if ((vline==VLINE_BUS) && (rq->protocol == ISDN_P_NT_S0) &&
				   vbusnt)
			return -EPROTONOSUPPORT;
		if ((vline==VLINE_BUS) && (rq->protocol == ISDN_P_NT_S0))
			vbusnt = p;

		p->initdone = 1;
		p->portmode = rq->protocol;
		ch->protocol = rq->protocol;

		if ((rq->protocol == ISDN_P_TE_S0) && (vbusnt))
			p->dch.state = vbusnt->dch.state;
		else
			p->dch.state = VBUS_INACTIVE;

	} else {
		if (rq->protocol != ch->protocol)
			return -EPROTONOSUPPORT;
	}

	p->opened = 1;
	if (p->dch.state == VBUS_ACTIVE)
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
l1loop_dctrl(struct mISDNchannel *ch, u_int cmd, void *arg) {
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
			if (rq->adr.channel == 0)
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
			p->opened = 0;
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

static void
ph_state(struct dchannel *dch)
{
	struct port *p = dch->hw;

	if (debug & DEBUG_HW)
		printk (KERN_INFO "%s: %s: %s %s\n",
			p->name, __func__,
			(p->portmode == ISDN_P_TE_S0)?"TE":"NT",
			(dch->state)?"ACTIVE":"INACTIVE");

	switch (dch->state) {
		case VBUS_INACTIVE:
			clear_bit(FLG_L2_ACTIVATED, &dch->Flags);
			clear_bit(FLG_ACTIVE, &dch->Flags);
			_queue_data(&dch->dev.D, PH_DEACTIVATE_IND,
				     MISDN_ID_ANY, 0, NULL, GFP_ATOMIC);
			break;
		case VBUS_ACTIVE:
			if (!(test_and_set_bit(FLG_ACTIVE, &dch->Flags)))
				_queue_data(&dch->dev.D, PH_ACTIVATE_IND,
					     MISDN_ID_ANY, 0, NULL, GFP_ATOMIC);
			else
				_queue_data(&dch->dev.D, PH_ACTIVATE_CNF,
					     MISDN_ID_ANY, 0, NULL, GFP_ATOMIC);
			break;
		default:
			break;
	}
}

static int
setup_instance(struct l1loop *hw) {
	struct port *p;
	u_long	flags;
	int	err=0, i, b;

	if (debug)
		printk (KERN_DEBUG "%s: %s\n", DRIVER_NAME, __func__);

	spin_lock_init(&hw->lock);

	for (i=0; i<interfaces; i++) {
		p = hw->ports + i;

		if (!(p->bch = kzalloc(sizeof(struct bchannel)*numbch,
		      GFP_KERNEL))) {
			      printk(KERN_ERR "%s: %s: no kmem for bchannels\n",
				     DRIVER_NAME, __FUNCTION__);
			      return -ENOMEM;
		}

		spin_lock_init(&p->lock);
		mISDN_initdchannel(&p->dch, MAX_DFRAME_LEN_L1, ph_state);
		p->dch.debug = debug & 0xFFFF;
		p->dch.hw = p;
		p->dch.dev.Dprotocols = (1 << ISDN_P_TE_S0) |
				(1 << ISDN_P_NT_S0);
		p->dch.dev.Bprotocols = (1 << (ISDN_P_B_RAW & ISDN_P_B_MASK)) |
				(1 << (ISDN_P_B_HDLC & ISDN_P_B_MASK));
		p->dch.dev.D.send = l1loop_l2l1D;
		p->dch.dev.D.ctrl = l1loop_dctrl;
		p->dch.dev.nrbchan = numbch;
		for (b=0; b<numbch; b++) {
			p->bch[b].nr = b + 1;
			test_and_set_bit(b + 1,
					 (u_long *)p->dch.dev.channelmap);
			p->bch[b].debug = debug;
			mISDN_initbchannel(&p->bch[b], MAX_DATA_MEM);
			p->bch[b].hw = p;
			p->bch[b].ch.send = l1loop_l2l1B;
			p->bch[b].ch.ctrl = l1loop_bctrl;
			p->bch[b].ch.nr = b + 1;
			list_add(&p->bch[b].ch.list, &p->dch.dev.bchannels);
		}

		snprintf(p->name, MISDN_MAX_IDLEN - 1, "%s.%d", DRIVER_NAME,
			 l1loop_cnt + 1);
		printk (KERN_INFO "%s: registered as '%s'\n",
			DRIVER_NAME, p->name);

		err = mISDN_register_device(&p->dch.dev, p->name);
		if (err) {
			for (b=0; b<numbch; b++)
				mISDN_freebchannel(&p->bch[b]);
			mISDN_freedchannel(&p->dch);
		} else {
			l1loop_cnt++;
			write_lock_irqsave(&l1loop_lock, flags);
			list_add_tail(&hw->list, &l1loop_list);
			write_unlock_irqrestore(&l1loop_lock, flags);
		}
	}

	return err;
}

static int
release_instance(struct l1loop *hw) {
	struct port * p;
	int i, b;

	if (debug)
		printk (KERN_DEBUG "%s: %s\n", DRIVER_NAME, __func__);

	for (i=0; i<interfaces; i++) {
		p = hw->ports + i;
		for (b=0; b<numbch; b++)
			l1loop_setup_bch(&p->bch[b], ISDN_P_NONE);

		mISDN_unregister_device(&p->dch.dev);
		for (b=0; b<numbch; b++)
			mISDN_freebchannel(&p->bch[b]);
		mISDN_freedchannel(&p->dch);
	}

	if (hw) {
		if (hw->ports) {
			if (hw->ports->bch)
				kfree(hw->ports->bch);
			kfree(hw->ports);
		}
		kfree(hw);
	}
	return 0;
}

static int __init
l1loop_init(void)
{
	if (interfaces <= 0)
		interfaces = 1;
	if (interfaces > 64)
		interfaces = 64;
	if (numbch <= 0)
		numbch = 2;
	if (numbch > 30)
		numbch = 30;
	if (vline > MAX_VLINE_OPTION)
		return -ENODEV;

	printk(KERN_INFO DRIVER_NAME " driver Rev. %s "
		"debug(0x%x) interfaces(%i) numbch(%i) vline(%s)\n",
		l1loop_rev, debug, interfaces, numbch, VLINE_MODES[vline]);

	if (!(hw = kzalloc(sizeof(struct l1loop), GFP_KERNEL))) {
		printk(KERN_ERR "%s: %s: no kmem for hw\n",
		       DRIVER_NAME, __FUNCTION__);
		return -ENOMEM;
	}
	if (!(hw->ports = kzalloc(sizeof(struct port)*interfaces,
	      GFP_KERNEL))) {
		printk(KERN_ERR "%s: %s: no kmem for interfaces\n",
		       DRIVER_NAME, __FUNCTION__);
		kfree(hw);
		return -ENOMEM;
	}

	return setup_instance(hw);
}

static void __exit
l1loop_cleanup(void)
{
	if (debug)
		printk(KERN_INFO DRIVER_NAME ": %s\n", __func__);

	release_instance(hw);
}

module_init(l1loop_init);
module_exit(l1loop_cleanup);

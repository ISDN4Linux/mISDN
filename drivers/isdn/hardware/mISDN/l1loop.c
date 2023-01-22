/* l1loop.c
 * virtual mISDN layer1 driver
 *
 * Copyright 2008 by Martin Bachem (info@colognechip.com)
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
 * - interfaces=<n>, n=[1..64] default 2
 * - vline=<n>, n=[0,1,2] default 1
 *     0 : NONE (no virtual line at all, all interfaces behave like
 *         ISDN TAs without any bus connection)
 *     1 : VBUS (all interfaces are connected to one virtual
 *         ISDN bus. the first interface opened as NT interface
 *         will configure all others as TE interface)
 *     2 : VLOOP (each interface is virtually equipped with a cross
 *         connector, so all data is looped internally)
 *     3 : VLINK (an even number of interfaces must be given.
 *         every pair of these interfaces is interlinked)
 * - nchannel=<n>, n=[2..126] default 2
 *     number of bchannels each interface will consist of
 *     if vline==VLINK, multiple nchannel values may be given to
 *     define the number of channels for each pair of interfaces.
 * - pri=<n>, n=[0,1] default 0
 *      0: register all interfaces as BRI
 *      1: register all interfaces as PRI
 * - debug=<n>, default=0, with n=0xHHHHGGGG
 *      H - l1 driver flags described in hfcs_usb.h
 *      G - common mISDN debug flags described at mISDNhw.h
 *
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mISDNhw.h>
#include "l1loop.h"

const char *l1loop_rev = "v0.2, 2011-09-30";


static int l1loop_cnt;
static LIST_HEAD(l1loop_list);
static DEFINE_RWLOCK(l1loop_lock);
struct l1loop *hw;
struct port *vbusnt; /* NT of virtual S0/E1 bus when using vline=1 */

/* module params */
static unsigned int interfaces = 2;
static unsigned int vline = 1;
static unsigned int nchannel[32] = {2};
static unsigned int pri;
static unsigned int debug;

MODULE_AUTHOR("Martin Bachem");
MODULE_LICENSE("GPL");
module_param(interfaces, uint, S_IRUGO | S_IWUSR);
module_param(vline, uint, S_IRUGO | S_IWUSR);
module_param_array(nchannel, uint, NULL, S_IRUGO | S_IWUSR);
module_param(pri, uint, S_IRUGO | S_IWUSR);
module_param(debug, uint, S_IRUGO | S_IWUSR);

/*
 * send full D/B channel status information
 * as MPH_INFORMATION_IND
 */
static void l1loop_ph_info(struct port *p)
{
	struct ph_info *phi;
	struct dchannel *dch = &p->dch;
	int i;

	phi = kzalloc(sizeof(struct ph_info) +
		dch->dev.nrbchan * sizeof(struct ph_info_ch),
		GFP_ATOMIC);

	phi->dch.ch.protocol = p->protocol;
	phi->dch.ch.Flags = dch->Flags;
	phi->dch.state = dch->state;
	phi->dch.num_bch = dch->dev.nrbchan;
	for (i = 0; i < dch->dev.nrbchan; i++) {
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
		fallthrough;
	case (ISDN_P_NONE):
		if (bch->state == ISDN_P_NONE)
			return 0; /* already in idle state */
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
		return -ENOPROTOOPT;
	}
	l1loop_ph_info(p);
	return 0;
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
 * bch layer1 loop (vline=2): immediatly loop back every B-channel data
 */
static void bch_vline_loop(struct bchannel *bch, struct sk_buff *skb)
{
	struct port *p = bch->hw;

	bch->rx_skb = skb_copy(skb, GFP_KERNEL);
	if (bch->rx_skb)
		recv_Bchannel(bch, MISDN_ID_ANY, false);
	else
		if (debug & DEBUG_HW)
			printk(KERN_ERR "%s: %s: mI_alloc_skb failed\n",
				p->name, __func__);
	dev_kfree_skb(skb);
	get_next_bframe(bch);
}

/*
 * bch layer1 bus (vline=1): copy frame to each party with bch(x) FLAG_ACTIVE
 */
static void bch_vbus(struct bchannel *bch, struct sk_buff *skb)
{
	struct port *me = bch->hw;
	struct port *party;
	struct bchannel *target = NULL;
	int i, b;

	b = bch->nr - 1 - (bch->nr > 16);
	for (i = 0; i < interfaces; i++) {
		party = hw->ports + i;
		target = &party->bch[b];
		if ((me != party) && test_bit(FLG_ACTIVE, &target->Flags)) {
			/* immediately queue data to bch's RX queue */
			target->rx_skb = skb_copy(skb, GFP_KERNEL);
			if (target->rx_skb)
				recv_Bchannel(target, MISDN_ID_ANY, false);
		}
	}

	dev_kfree_skb(skb);
	get_next_bframe(bch);
}

/*
 * bch layer1 link (vline=3): copy frame to ohter party, if bch(x) FLAG_ACTIVE
 */
static void bch_vlink(struct bchannel *bch, struct sk_buff *skb)
{
	struct port *me = bch->hw;
	struct port *party = &hw->ports[me->instance ^ 1];
	struct bchannel *target = NULL;
	int b;

	b = bch->nr - 1 - (bch->nr > 16);
	target = &party->bch[b];
	if (test_bit(FLG_ACTIVE, &target->Flags)) {
		/* immediately queue data to bch's RX queue */
		target->rx_skb = skb_copy(skb, GFP_KERNEL);
		if (target->rx_skb)
			recv_Bchannel(target, MISDN_ID_ANY, false);
	}

	dev_kfree_skb(skb);
	get_next_bframe(bch);
}

/*
 * layer2 -> layer1 callback B-channel
 */
static int
l1loop_l2l1B(struct mISDNchannel *ch, struct sk_buff *skb) {
	struct bchannel		*bch = container_of(ch, struct bchannel, ch);
	struct port		*p = bch->hw;
	int			ret = -EINVAL;
	struct mISDNhead	*hh = mISDN_HEAD_P(skb);

	switch (hh->prim) {
	case PH_DATA_REQ:
		spin_lock(&p->lock);
		ret = bchannel_senddata(bch, skb);
		spin_unlock(&p->lock);
		if (ret > 0) {
			ret = 0;
			switch (vline) {
			case VLINE_BUS:
				bch_vbus(bch, skb);
				break;
			case VLINE_LOOP:
				bch_vline_loop(bch, skb);
				break;
			case VLINE_LINK:
				bch_vlink(bch, skb);
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
 * 'physical' S0/E1 bus state change handler
 */
static void
ph_state(struct dchannel *dch)
{
	struct port *p = dch->hw;
	char *ptext;

	if (p->protocol <= ISDN_P_MAX)
		ptext = ISDN_P_TEXT[p->protocol];
	else
		ptext = ISDN_P_TEXT[ISDN_P_MAX+1];

	if (debug & DEBUG_HW)
		printk(KERN_DEBUG "%s: %s: %s %s (%i)\n",
			p->name, __func__, ptext,
			(dch->state) ? "ACTIVE" : "INACTIVE",
			test_bit(FLG_ACTIVE, &dch->Flags));

	switch (dch->state) {
	case VBUS_INACTIVE:
		clear_bit(FLG_L2_ACTIVATED, &dch->Flags);
		clear_bit(FLG_ACTIVE, &dch->Flags);
		_queue_data(&dch->dev.D, PH_DEACTIVATE_IND,
			MISDN_ID_ANY, 0, NULL, GFP_KERNEL);
		break;
	case VBUS_ACTIVE:
		if (!(test_and_set_bit(FLG_ACTIVE, &dch->Flags)))
			_queue_data(&dch->dev.D, PH_ACTIVATE_IND,
				MISDN_ID_ANY, 0, NULL, GFP_KERNEL);
		else
			_queue_data(&dch->dev.D, PH_ACTIVATE_CNF,
				MISDN_ID_ANY, 0, NULL, GFP_KERNEL);
		break;
	default:
		break;
	}
	l1loop_ph_info(p);
}

/*
 * every activation (NT and TE) causes every listening party to activate
 */
static void
vbus_activate(struct port *p)
{
	int i;

	if (vbusnt) {
		for (i = 0; i < interfaces; i++) {
			p = hw->ports + i;
			p->dch.state = VBUS_ACTIVE;
			if (test_bit(FLG_OPEN, &p->dch.Flags))
				ph_state(&p->dch);
		}
	} else {
		/* no interfaces opened as NT yet */
		p->dch.state = VBUS_INACTIVE;
		ph_state(&p->dch);
	}
}

/*
 * set every party member to state VBUS_ACTIVE
 */
static void
vbus_deactivate(struct port *p)
{
	struct dchannel *dch;
	int i;

	for (i = 0; i < interfaces; i++) {
		p = hw->ports + i;
		dch = &p->dch;
		dch->state = VBUS_INACTIVE;
		if (test_bit(FLG_OPEN, &p->dch.Flags))
			ph_state(dch);
	}
}

/*
 * every activation (NT and TE) causes the ohter listening party to activate
 */
static void
vlink_activate(struct port *p)
{
	struct port *party = &hw->ports[p->instance ^ 1];

	if (test_bit(FLG_OPEN, &party->dch.Flags)) {
		party->dch.state = VBUS_ACTIVE;
		ph_state(&party->dch);
	} else {
		p->dch.state = VBUS_INACTIVE;
		ph_state(&p->dch);
	}
}

/*
 * set every party member to state VLINK_ACTIVE
 */
static void
vlink_deactivate(struct port *p)
{
	struct port *party = &hw->ports[p->instance ^ 1];

	p->dch.state = VBUS_INACTIVE;
	ph_state(&p->dch);
	if (test_bit(FLG_OPEN, &party->dch.Flags)) {
		party->dch.state = VBUS_INACTIVE;
		ph_state(&party->dch);
	}
}

/*
 * apply 'physical' bus commands:
 *    L1_ACTIVATE_TE, L1_ACTIVATE_NT, L1_DEACTIVATE_NT
 */
static void
ph_command(struct port *p, u_char command)
{
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
		case VLINE_LINK:
			vlink_activate(p);
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
		case VLINE_LINK:
			vlink_activate(p);
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
			p->dch.state = VBUS_INACTIVE;
			schedule_event(&p->dch, FLG_PHCHANGE);
			break;
		case VLINE_LINK:
			vlink_deactivate(p);
			break;
		case VLINE_NONE:
		default:
			break;
		}
		break;
	}
}

/*
 * dch layer1 loop (vline=2): immediatly loop back every D-channel data
 */
static void dch_vline_loop(struct dchannel *dch, struct sk_buff *skb)
{
	struct port *p = dch->hw;

	dch->rx_skb = skb_copy(skb, GFP_KERNEL);
	if (dch->rx_skb)
		recv_Dchannel(dch);
	else
		if (debug & DEBUG_HW)
			printk(KERN_ERR "%s: %s: mI_alloc_skb failed\n",
				p->name, __func__);
	dev_kfree_skb(skb);
	get_next_dframe(dch);
}

/*
 * dch layer1 link (vline=3): connect each pair of D-channel data
 */
static void dch_vline_link(struct dchannel *dch, struct sk_buff *skb)
{
	struct port *me = dch->hw;
	struct port *party = &hw->ports[me->instance ^ 1];
	struct dchannel *party_dch = &party->dch;

	if (test_bit(FLG_ACTIVE, &party_dch->Flags)) {
		party_dch->rx_skb = skb_copy(skb, GFP_KERNEL);
		if (party_dch->rx_skb)
			recv_Dchannel(party_dch);
	}
	dev_kfree_skb(skb);
	get_next_dframe(dch);
}

/*
 * dch layer1 bus (vline=1) on S0 bus
 */
static void dch_vbus_S0(struct dchannel *dch, struct sk_buff *skb)
{
	struct port *me = dch->hw;
	struct port *party;
	struct dchannel *party_dch;
	int i;

	if (vbusnt) {
		if (me != vbusnt) {
			/* TE -> NT */
			vbusnt->dch.rx_skb = skb_copy(skb, GFP_KERNEL);
			if (vbusnt->dch.rx_skb)
				recv_Dchannel(&vbusnt->dch);
			/* virtual E-channel ECHO */
			for (i = 0; i < interfaces; i++) {
				party = hw->ports + i;
				if ((party != vbusnt) && test_bit(FLG_ACTIVE,
				     &party->dch.Flags)) {
					party_dch = &party->dch;
					party_dch->rx_skb = skb_copy(skb,
							GFP_KERNEL);
					if (party_dch->rx_skb)
						recv_Echannel(party_dch,
							party_dch);
				}
			}
		} else {
			/* NT -> all TEs */
			for (i = 0; i < interfaces; i++) {
				party = hw->ports + i;
				if ((me != party) && test_bit(FLG_ACTIVE,
				     &party->dch.Flags)) {
					party_dch = &party->dch;
					party_dch->rx_skb = skb_copy(skb,
							GFP_KERNEL);
					if (party_dch->rx_skb)
						recv_Dchannel(party_dch);
				}
			}
		}
	}
	dev_kfree_skb(skb);
	get_next_dframe(dch);
}

/*
 * dch layer1 bus (vline=1) on E1 bus
 */
static void dch_vbus_E1(struct dchannel *dch, struct sk_buff *skb)
{
	struct port *me = dch->hw;
	struct port *party;
	struct dchannel *party_dch;
	int i;

	/* NT->TE / TE->NT */
	for (i = 0; i < interfaces; i++) {
		party = hw->ports + i;
		if ((me != party) && test_bit(FLG_ACTIVE, &party->dch.Flags)) {
			party_dch = &party->dch;
			party_dch->rx_skb = skb_copy(skb, GFP_KERNEL);
			if (party_dch->rx_skb)
				recv_Dchannel(party_dch);
		}
	}
	dev_kfree_skb(skb);
	get_next_dframe(dch);
}

/*
 * layer2 -> layer1 callback D-channel
 */
static int
l1loop_l2l1D(struct mISDNchannel *ch, struct sk_buff *skb) {
	struct mISDNdevice	*dev = container_of(ch, struct mISDNdevice, D);
	struct dchannel		*dch = container_of(dev, struct dchannel, dev);
	struct mISDNhead	*hh = mISDN_HEAD_P(skb);
	struct port		*p = dch->hw;
	int			ret = -EINVAL;
	char			*ptext;

	if (p->protocol <= ISDN_P_MAX)
		ptext = ISDN_P_TEXT[p->protocol];
	else
		ptext = ISDN_P_TEXT[ISDN_P_MAX+1];

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
				if (IS_ISDN_P_S0(p->protocol))
					dch_vbus_S0(dch, skb);
				else
					dch_vbus_E1(dch, skb);
				break;
			case VLINE_LOOP:
				dch_vline_loop(dch, skb);
				break;
			case VLINE_LINK:
				dch_vline_link(dch, skb);
			case VLINE_NONE:
			default:
				break;
			}
		}
		return ret;
	case PH_ACTIVATE_REQ:
		if (debug & DEBUG_HW)
			printk(KERN_DEBUG "%s: %s: PH_ACTIVATE_REQ %s\n",
				p->name, __func__, ptext);
		ret = 0;
		if (IS_ISDN_P_NT(p->protocol)) {
			if (test_bit(FLG_ACTIVE, &dch->Flags))
				_queue_data(&dch->dev.D, PH_ACTIVATE_CNF,
					MISDN_ID_ANY, 0, NULL, GFP_KERNEL);
			else {
				ph_command(p, L1_ACTIVATE_NT);
				test_and_set_bit(FLG_L2_ACTIVATED,
					&dch->Flags);
			}
		} else {
			if (test_bit(FLG_ACTIVE, &dch->Flags))
				_queue_data(&dch->dev.D, PH_ACTIVATE_CNF,
					 MISDN_ID_ANY, 0, NULL, GFP_KERNEL);
			else
				ph_command(p, L1_ACTIVATE_TE);
		}
		break;
	case PH_DEACTIVATE_REQ:
		if (debug & DEBUG_HW)
			printk(KERN_DEBUG "%s: %s: PH_DEACTIVATE_REQ %s\n",
				p->name, __func__,  ptext);
		test_and_clear_bit(FLG_L2_ACTIVATED, &dch->Flags);

		if (IS_ISDN_P_NT(p->protocol))
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
	case MPH_INFORMATION_REQ:
		l1loop_ph_info(p);
		ret = 0;
		break;
	}
	return ret;
}

/*
 * layer 1 B-channel 'hardware' access
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
				"%s: FILL_EMPTY request (nr=%d off=%d)\n",
				__func__, bch->nr, !!cq->p1);
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
 * layer1 B-channel 'hardware' access
 */
static int
l1loop_bctrl(struct mISDNchannel *ch, u_int cmd, void *arg)
{
	struct bchannel *bch = container_of(ch, struct bchannel, ch);
	int ret = -EINVAL;

	if (bch->debug & DEBUG_HW)
		printk(KERN_DEBUG "%s: cmd:%x %p\n", __func__, cmd, arg);

	switch (cmd) {
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
		ret = -EINVAL;
		printk(KERN_WARNING "%s: unknown prim(%x)\n",
			__func__, cmd);
	}
	return ret;
}

/*
 * open interface due to user process:
 *   intially register NT interface when using vline=VLINE_BUS (default)
 */
static int
open_dchannel(struct port *p, struct mISDNchannel *ch, struct channel_req *rq)
{
	if (debug & DEBUG_HW_OPEN)
		printk(KERN_DEBUG "%s: %s: dev(%d) open from %p\n",
			p->name, __func__, p->dch.dev.id,
			__builtin_return_address(0));
	if (rq->protocol == ISDN_P_NONE)
		return -EINVAL;

	if (!p->initdone) {
		if ((vline == VLINE_BUS) && (IS_ISDN_P_NT(rq->protocol))
				   && vbusnt)
			return -EPROTONOSUPPORT;

		/* set VBUS NT interface */
		if ((vline == VLINE_BUS) && (IS_ISDN_P_NT(rq->protocol)))
			vbusnt = p;

		p->initdone = 1;
		p->protocol = rq->protocol;
		ch->protocol = rq->protocol;

	} else {
		if (rq->protocol != ch->protocol)
			return -EPROTONOSUPPORT;
	}

	set_bit(FLG_OPEN, &p->dch.Flags);
	if (vbusnt) {
		if (p != vbusnt)
			p->dch.state = vbusnt->dch.state;
	} else {
		if (debug & DEBUG_HW_OPEN)
			printk(KERN_DEBUG "%s: %s: dev(%d) no NT found\n",
				 p->name, __func__, p->dch.dev.id);
		p->dch.state = VBUS_INACTIVE;
	}
	ph_state(&p->dch);

	rq->ch = ch;
	if (!try_module_get(THIS_MODULE))
		printk(KERN_WARNING "%s: %s: cannot get module\n",
		       p->name, __func__);
	return 0;
}

static int
open_bchannel(struct port *p, struct dchannel *dch, struct channel_req *rq)
{
	struct bchannel *bch;

	if (!test_channelmap(rq->adr.channel, dch->dev.channelmap))
		return -EINVAL;
	if (rq->protocol == ISDN_P_NONE)
		return -EINVAL;

	if (debug & DEBUG_HW)
		printk(KERN_DEBUG "%s: %s B%i\n",
			p->name, __func__, rq->adr.channel);

	bch = &p->bch[rq->adr.channel - 1 - (rq->adr.channel > 16)];
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
		printk(KERN_DEBUG "%s: %s op(0x%x) channel(0x%x)\n",
			p->name, __func__, (cq->op), (cq->channel));

	switch (cq->op) {
	case MISDN_CTRL_GETOP:
		cq->op = MISDN_CTRL_LOOP | MISDN_CTRL_CONNECT |
			MISDN_CTRL_DISCONNECT;
		break;

	case MISDN_CTRL_LOOP:
		/*
		 * cq->channel:
		 *   0 disable all testloop
		 *   1 B1 loop only
		 *   2 B2 loop only
		 *   4 D  loop only
		 *   3 B1 + B2 loop
		 *   7 B1 + B2 + D loop
		 */
		if (cq->channel < 0 || cq->channel > 7 || !(cq->channel & 7)) {
			ret = -EINVAL;
			break;
		}
		if (cq->channel & 1) {
			printk(KERN_INFO "%s: %s: @TODO enable testloop B1\n",
				p->name, __func__);
		}
		if (cq->channel & 2) {
			printk(KERN_INFO "%s: %s: @TODO enable testloop B2\n",
				p->name, __func__);
		}
		if (cq->channel & 4) {
			printk(KERN_INFO "%s: %s: @TODO enable testloop D\n",
				p->name, __func__);
		}
		break;

	default:
		printk(KERN_WARNING "%s: %s: unknown Op %x\n",
			p->name, __func__, cq->op);
		ret = -EINVAL;
		break;
	}
	return ret;
}

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
		if ((rq->protocol == ISDN_P_TE_S0) ||
		    (rq->protocol == ISDN_P_NT_S0) ||
		    (rq->protocol == ISDN_P_TE_E1) ||
		    (rq->protocol == ISDN_P_NT_E1))
			err = open_dchannel(p, ch, rq);
		else
			err = open_bchannel(p, dch, rq);
		break;
	case CLOSE_CHANNEL:
		if (debug & DEBUG_HW_OPEN)
			printk(KERN_DEBUG "%s: %s: dev(%d) close from %p\n",
				p->name, __func__, p->dch.dev.id,
				__builtin_return_address(0));
		module_put(THIS_MODULE);
		break;
	case CONTROL_CHANNEL:
		err = channel_ctrl(p, arg);
		break;
	default:
		if (dch->debug & DEBUG_HW)
			printk(KERN_DEBUG "%s: %s: unknown command %x\n",
				p->name, __func__, cmd);
		return -EINVAL;
	}
	return err;
}

static int
setup_instance(struct l1loop *hw) {
	struct port *p;
	u_long	flags;
	int	err = 0, i, b, n;

	if (debug)
		printk(KERN_DEBUG "%s: %s\n", DRIVER_NAME, __func__);

	for (i = 0; i < interfaces; i++) {
		p = hw->ports + i;
		n = nchannel[0];
		if (vline == VLINE_LINK && nchannel[i >> 1])
			n = nchannel[i >> 1];
		p->bch = kzalloc(sizeof(struct bchannel)*n, GFP_KERNEL);
		if (!p->bch) {
			printk(KERN_ERR "%s: %s: no kmem for bchannels\n",
				DRIVER_NAME, __func__);
			return -ENOMEM;
		}

		spin_lock_init(&p->lock);
		p->instance = i;
		mISDN_initdchannel(&p->dch, MAX_DFRAME_LEN_L1, ph_state);
		p->dch.debug = debug & 0xFFFF;
		p->dch.hw = p;
		if (!pri)
			p->dch.dev.Dprotocols = (1 << ISDN_P_TE_S0) |
					(1 << ISDN_P_NT_S0);
		else
			p->dch.dev.Dprotocols = (1 << ISDN_P_TE_E1) |
					(1 << ISDN_P_NT_E1);
		p->dch.dev.Bprotocols = (1 << (ISDN_P_B_RAW & ISDN_P_B_MASK)) |
				(1 << (ISDN_P_B_HDLC & ISDN_P_B_MASK));
		p->dch.dev.D.send = l1loop_l2l1D;
		p->dch.dev.D.ctrl = l1loop_dctrl;
		p->dch.dev.nrbchan = n;
		p->nrbchan = n;
		for (b = 0; b < n; b++) {
			p->bch[b].nr = b + 1 + (b >= 15);
			set_channelmap(p->bch[b].nr, p->dch.dev.channelmap);
			p->bch[b].debug = debug;
			mISDN_initbchannel(&p->bch[b], MAX_DATA_MEM, 0);
			p->bch[b].hw = p;
			p->bch[b].ch.send = l1loop_l2l1B;
			p->bch[b].ch.ctrl = l1loop_bctrl;
			p->bch[b].ch.nr = p->bch[b].nr;
			list_add(&p->bch[b].ch.list, &p->dch.dev.bchannels);
		}

		snprintf(p->name, MISDN_MAX_IDLEN - 1, "%s.%d", DRIVER_NAME,
			 l1loop_cnt + 1);
		printk(KERN_INFO "%s: registered as '%s'\n",
			DRIVER_NAME, p->name);

		/* TODO: parent device? */
		err = mISDN_register_device(&p->dch.dev, NULL, p->name);
		if (err) {
			for (b = 0; b < n; b++)
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
	struct port *p;
	int i, b;

	if (debug)
		printk(KERN_DEBUG "%s: %s\n", DRIVER_NAME, __func__);

	for (i = 0; i < interfaces; i++) {
		p = hw->ports + i;
		for (b = 0; b < p->nrbchan; b++)
			l1loop_setup_bch(&p->bch[b], ISDN_P_NONE);

		mISDN_unregister_device(&p->dch.dev);
		for (b = 0; b < p->nrbchan; b++)
			mISDN_freebchannel(&p->bch[b]);
		mISDN_freedchannel(&p->dch);
	}

	if (hw) {
		if (hw->ports) {
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
	int i;

	if (vline == 3 && (interfaces & 1)) {
		printk(KERN_ERR "%s: %s: an even number of interfaces are "
			"expected\n", DRIVER_NAME, __func__);
		return -EINVAL;
	}
	if (interfaces > 64)
		interfaces = 64;
	if (vline == 3) {
		for (i = 0; i < (interfaces >> 1); i++) {
			if (nchannel[i] > 126)
				nchannel[i] = 126;
		}
	} else {
		if (nchannel[0] > 126)
			nchannel[0] = 126;
	}
	if (pri && (vline == VLINE_BUS) && (interfaces > 2))
		interfaces = 2;
	if (vline > MAX_VLINE_OPTION)
		return -ENODEV;

	printk(KERN_INFO DRIVER_NAME " driver Rev. %s "
		"debug(0x%x) interfaces(%i) nchannel[0](%i) vline(%s)\n",
		l1loop_rev, debug, interfaces, nchannel[0], VLINE_MODES[vline]);

	hw = kzalloc(sizeof(struct l1loop), GFP_KERNEL);
	if (!hw) {
		printk(KERN_ERR "%s: %s: no kmem for hw\n",
		       DRIVER_NAME, __func__);
		return -ENOMEM;
	}
	hw->ports = kzalloc(sizeof(struct port)*interfaces, GFP_KERNEL);
	if (!hw->ports) {
		printk(KERN_ERR "%s: %s: no kmem for interfaces\n",
		       DRIVER_NAME, __func__);
		kfree(hw);
		return -ENOMEM;
	}

	return setup_instance(hw);
}

static void __exit
l1loop_cleanup(void)
{
	if (debug)
		printk(KERN_DEBUG DRIVER_NAME ": %s\n", __func__);

	release_instance(hw);
}

module_init(l1loop_init);
module_exit(l1loop_cleanup);

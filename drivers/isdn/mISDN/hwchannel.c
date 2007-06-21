/* $Id: hwchannel.c,v 2.0 2007/06/06 12:58:31 kkeil Exp $
 *
 * Author	Karsten Keil <kkeil@novell.com>
 *
 * Copyright 2007  by Karsten Keil <kkeil@novell.com>
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
 */

#include <linux/module.h>
#include <linux/mISDNhw.h>

int
mISDN_initdchannel(struct dchannel *ch, ulong prop, int maxlen)
{
	ch->Flags = prop;
	ch->maxlen = maxlen;
	ch->hw = NULL;
	ch->rx_skb = NULL;
	ch->tx_skb = NULL;
	ch->tx_idx = 0;
	skb_queue_head_init(&ch->squeue);
	INIT_LIST_HEAD(&ch->dev.bchannels);
	return 0;
}
EXPORT_SYMBOL(mISDN_initdchannel);

int
mISDN_initbchannel(struct bchannel *ch, ulong prop, int maxlen)
{
	ch->Flags = prop;
	ch->maxlen = maxlen;
	ch->hw = NULL;
	ch->rx_skb = NULL;
	ch->tx_skb = NULL;
	ch->tx_idx = 0;
	ch->next_skb = NULL;
	return 0;
}
EXPORT_SYMBOL(mISDN_initbchannel);

int
mISDN_freedchannel(struct dchannel *ch)
{
	if (ch->tx_skb) {
		dev_kfree_skb(ch->tx_skb);
		ch->tx_skb = NULL;
	}
	if (ch->rx_skb) {
		dev_kfree_skb(ch->rx_skb);
		ch->rx_skb = NULL;
	}
	skb_queue_purge(&ch->squeue);
	return (0);
}
EXPORT_SYMBOL(mISDN_freedchannel);

int
mISDN_freebchannel(struct bchannel *ch)
{
	if (ch->tx_skb) {
		dev_kfree_skb(ch->tx_skb);
		ch->tx_skb = NULL;
	}
	if (ch->rx_skb) {
		dev_kfree_skb(ch->rx_skb);
		ch->rx_skb = NULL;
	}
	if (ch->next_skb) {
		dev_kfree_skb(ch->next_skb);
		ch->next_skb = NULL;
	}
	return 0;
}
EXPORT_SYMBOL(mISDN_freebchannel);

int
dchannel_senddata(struct dchannel *ch, struct sk_buff *skb)
{
	struct mISDNhead *hh;
	/* HW lock must be obtained */
	/* check oversize */
	if (skb->len <= 0) {
		printk(KERN_WARNING "%s: skb too small\n", __FUNCTION__);
		return -EINVAL;
	}
	if (skb->len > ch->maxlen) {
		printk(KERN_WARNING "%s: skb too large(%d/%d)\n",
			__FUNCTION__, skb->len, ch->maxlen);
		return -EINVAL;
	}
	if (test_and_set_bit(FLG_TX_BUSY, &ch->Flags)) {
		skb_queue_tail(&ch->squeue, skb);
		return 0;
	} else {
		/* write to fifo */
		ch->tx_skb = skb;
		ch->tx_idx = 0;
		hh = mISDN_HEAD_P(skb);
		queue_ch_frame(&ch->dev.D, PH_DATA_CNF, hh->id, NULL);
		return 1;
	}
}
EXPORT_SYMBOL(dchannel_senddata);

int
bchannel_senddata(struct bchannel *ch, struct sk_buff *skb)
{
	struct mISDNhead *hh;
	/* HW lock must be obtained */
	/* check oversize */
	if (skb->len <= 0) {
		printk(KERN_WARNING "%s: skb too small\n", __FUNCTION__);
		return -EINVAL;
	}
	if (skb->len > ch->maxlen) {
		printk(KERN_WARNING "%s: skb too large(%d/%d)\n",
			__FUNCTION__, skb->len, ch->maxlen);
		return -EINVAL;
	}
	/* check for pending next_skb */
	if (ch->next_skb) {
		printk(KERN_WARNING
		    "%s: next_skb exist ERROR (skb->len=%d next_skb->len=%d)\n",
		    __FUNCTION__, skb->len, ch->next_skb->len);
		return -EBUSY;
	}
	if (test_and_set_bit(FLG_TX_BUSY, &ch->Flags)) {
		test_and_set_bit(FLG_TX_NEXT, &ch->Flags);
		ch->next_skb = skb;
		return 0;
	} else {
		/* write to fifo */
		ch->tx_skb = skb;
		ch->tx_idx = 0;
		hh = mISDN_HEAD_P(skb);
		queue_ch_frame(&ch->ch, PH_DATA_CNF, hh->id, NULL);
		return 1;
	}
}
EXPORT_SYMBOL(bchannel_senddata);

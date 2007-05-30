/* $Id: channel.c,v 1.2 2006/03/06 12:58:31 keil Exp $
 *
 *  Author       (c) Karsten Keil <kkeil@suse.de>
 *
 * This file is released under the GPLv2
 *
 */

#include <linux/module.h>
#include "channel.h"
#include "layer1.h"

int
mISDN_initchannel(channel_t *ch, ulong prop, int maxlen)
{
	ch->log = kmalloc(MAX_LOG_SPACE, GFP_ATOMIC);
	if (!ch->log) {
		printk(KERN_WARNING
			"mISDN: No memory for channel log\n");
		return (-ENOMEM);
	}
	ch->Flags = prop;
	ch->maxlen = maxlen;
	ch->hw = NULL;
	ch->rx_skb = NULL;
	ch->tx_skb = NULL;
	ch->tx_idx = 0;
	ch->next_skb = NULL;
	return (0);
}

int
mISDN_freechannel(channel_t *ch)
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
	kfree(ch->log);
	ch->log = NULL;
	return (0);
}

/* need called with HW lock */
int
mISDN_setpara(channel_t *ch, mISDN_stPara_t *stp)
{
	if (!stp) { // clear parameters
		ch->maxlen = 0;
		ch->up_headerlen = 0;
		return (0);
	}
	if (stp->up_headerlen)
		ch->up_headerlen = stp->up_headerlen;
	if (stp->maxdatalen) {
		if (ch->maxlen < stp->maxdatalen) {
			if (ch->rx_skb) {
				struct sk_buff	*skb;

				skb = alloc_skb(stp->maxdatalen +
					ch->up_headerlen, GFP_ATOMIC);
				if (!skb) {
					int_errtxt("no skb for %d+%d",
					    stp->maxdatalen, ch->up_headerlen);
					return (-ENOMEM);
				}
				skb_reserve(skb, ch->up_headerlen);
				memcpy(skb_put(skb, ch->rx_skb->len),
					ch->rx_skb->data, ch->rx_skb->len);
				dev_kfree_skb(ch->rx_skb);
				ch->rx_skb = skb;
			}
		}
		ch->maxlen = stp->maxdatalen;
	}
	return (0);
}

EXPORT_SYMBOL(mISDN_initchannel);
EXPORT_SYMBOL(mISDN_freechannel);
EXPORT_SYMBOL(mISDN_setpara);

/* $Id: helper.c,v 0.1 2001/02/11 22:46:19 kkeil Exp $
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#define __NO_VERSION__
#include "hisax.h"

int
discard_queue(struct sk_buff_head *q)
{
	struct sk_buff *skb;
	int ret=0;

	while ((skb = skb_dequeue(q))) {
		dev_kfree_skb(skb);
		ret++;
	}
	return(ret);
}

int
init_dchannel(dchannel_t *dch) {
	if (!(dch->dlog = kmalloc(MAX_DLOG_SPACE, GFP_ATOMIC))) {
		printk(KERN_WARNING
			"HiSax: No memory for dlog\n");
		return(-ENOMEM);
	}
	if (!(dch->rx_buf = kmalloc(MAX_DFRAME_LEN_L1, GFP_ATOMIC))) {
		printk(KERN_WARNING
			"HiSax: No memory for dchannel rx_buf\n");
		kfree(dch->dlog);
		return(-ENOMEM);
	}
	dch->rx_idx = 0;
	if (!(dch->tx_buf = kmalloc(MAX_DFRAME_LEN_L1, GFP_ATOMIC))) {
		printk(KERN_WARNING
			"HiSax: No memory for dchannel tx_buf\n");
		kfree(dch->dlog);
		kfree(dch->rx_buf);
		return(-ENOMEM);
	}
	dch->tx_idx = 0;
	dch->next_skb = NULL;
	dch->event = 0;
	dch->tqueue.next = 0;
	dch->tqueue.sync = 0;
	dch->tqueue.data = dch;
	skb_queue_head_init(&dch->rqueue);
	return(0);
}

int
free_dchannel(dchannel_t *dch) {
	discard_queue(&dch->rqueue);
	if (dch->rx_buf) {
		kfree(dch->rx_buf);
		dch->rx_buf = NULL;
	}
	if (dch->tx_buf) {
		kfree(dch->tx_buf);
		dch->tx_buf = NULL;
	}
	if (dch->next_skb) {
		dev_kfree_skb(dch->next_skb);
		dch->next_skb = NULL;
	}
	if (dch->dlog) {
		kfree(dch->dlog);
		dch->dlog = NULL;
	}
	return(0);
}

int
init_bchannel(bchannel_t *bch) {
	if (!(bch->rx_buf = kmalloc(HSCX_BUFMAX, GFP_ATOMIC))) {
		printk(KERN_WARNING
			"HiSax: No memory for bchannel rx_buf\n");
		return (-ENOMEM);
	}
	if (!(bch->tx_buf = kmalloc(HSCX_BUFMAX, GFP_ATOMIC))) {
		printk(KERN_WARNING
			"HiSax: No memory for bchannel tx_buf\n");
		kfree(bch->rx_buf);
		bch->rx_buf = NULL;
		return (-ENOMEM);
	}
	skb_queue_head_init(&bch->rqueue);
	bch->next_skb = NULL;
	bch->Flag = 0;
	bch->event = 0;
	bch->rx_idx = 0;
	bch->tx_len = 0;
	bch->tx_idx = 0;
	bch->tqueue.data = bch;
	return(0);
}

int
free_bchannel(bchannel_t *bch) {
	discard_queue(&bch->rqueue);
	if (bch->rx_buf) {
		kfree(bch->rx_buf);
		bch->rx_buf = NULL;
	}
	if (bch->tx_buf) {
		kfree(bch->tx_buf);
		bch->tx_buf = NULL;
	}
	if (bch->next_skb) {
		dev_kfree_skb(bch->next_skb);
		bch->next_skb = NULL;
	}
	return(0);
}


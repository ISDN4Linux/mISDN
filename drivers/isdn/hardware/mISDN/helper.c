/* $Id: helper.c,v 0.4 2001/02/27 17:45:44 kkeil Exp $
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#define __NO_VERSION__
#include <linux/hisaxif.h>
#include "helper.h"
#include "hisax_hw.h"

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
		dch->dlog = NULL;
		return(-ENOMEM);
	}
	dch->rx_idx = 0;
	if (!(dch->tx_buf = kmalloc(MAX_DFRAME_LEN_L1, GFP_ATOMIC))) {
		printk(KERN_WARNING
			"HiSax: No memory for dchannel tx_buf\n");
		kfree(dch->dlog);
		dch->dlog = NULL;
		kfree(dch->rx_buf);
		dch->rx_buf = NULL;
		return(-ENOMEM);
	}
	dch->tx_idx = 0;
	dch->next_skb = NULL;
	dch->event = 0;
	dch->tqueue.data = dch;
	skb_queue_head_init(&dch->rqueue);
	return(0);
}

int
free_dchannel(dchannel_t *dch) {

	if (dch->tqueue.sync)
		printk(KERN_ERR"free_dchannel tqueue.sync\n");
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
	if (!(bch->rx_buf = kmalloc(MAX_DATA_MEM, GFP_ATOMIC))) {
		printk(KERN_WARNING
			"HiSax: No memory for bchannel rx_buf\n");
		return (-ENOMEM);
	}
	if (!(bch->tx_buf = kmalloc(MAX_DATA_MEM, GFP_ATOMIC))) {
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

	if (bch->tqueue.sync)
		printk(KERN_ERR"free_bchannel tqueue.sync\n");
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

int bprotocol2pid(void *bp, hisax_pid_t *pid) {
	__u8		*p = bp;
	__u16		*w = bp;

	p += 6;
	pid->B1 = *w;
	pid->B1 |= ISDN_PID_LAYER1 | ISDN_PID_BCHANNEL_BIT;
	if (*p)
		pid->B1p = p;
	else
		pid->B1p = NULL;
	w++;
	p += *p;
	p++;
	pid->B2 = *w;
	pid->B2 |= ISDN_PID_LAYER2 | ISDN_PID_BCHANNEL_BIT;
	if (*p)
		pid->B2p = p;
	else
		pid->B2p = NULL;
	w++;
	p += *p;
	p++;
	pid->B3 = *w;
	pid->B3 |= ISDN_PID_LAYER3 | ISDN_PID_BCHANNEL_BIT;
	if (*p)
		pid->B3p = p;
	else
		pid->B3p = NULL;
	p += *p;
	p++;
	if (*p)
		pid->global = p;
	else
		pid->global = NULL;
	return(0);
}

int HasProtocol(hisaxinstance_t *inst, int proto) {
	int i;

	if (!inst || !inst->obj) {
		int_error();
		return(0);
	}
	for (i=0; i<inst->obj->protcnt; i++) {
		if (proto == inst->obj->protocols[i])
			return(1);
	}
	return(0); 
}

int DelIF(hisaxinstance_t *inst, hisaxif_t *mif, void *func, void *data) {
	hisaxif_t hif;

	hif.protocol = mif->protocol;
	hif.layer = mif->layer;
	hif.fdata = data;
	hif.func = func;
	mif->protocol = ISDN_PID_NONE;
	mif->stat = IF_NOACTIV;
	inst->obj->ctrl(inst->st, MGR_ADDIF | REQUEST, mif);
	return(inst->obj->ctrl(inst->st, MGR_DELIF | REQUEST, &hif));
}

/* $Id: helper.c,v 0.5 2001/03/03 08:07:29 kkeil Exp $
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
	__u8	*p = bp;
	__u16	*w = bp;
	int	i;
	

	p += 6;
	for (i=1; i<=3; i++) {
		if (*w > 23) {
			int_errtxt("L%d pid %x\n",i,*w);
			return(-EINVAL);
		}
		pid->protocol[i] = (1 <<*w) | ISDN_PID_LAYER(i) |
			ISDN_PID_BCHANNEL_BIT;
		if (*p)
			pid->param[i] = p;
		else
			pid->param[i] = NULL;
		w++;
		p += *p;
		p++;
	}
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

int
layermask2layer(int layermask) {
	switch(layermask) {
		case ISDN_LAYER(0): return(0);
		case ISDN_LAYER(1): return(1);
		case ISDN_LAYER(2): return(2);
		case ISDN_LAYER(3): return(3);
		case ISDN_LAYER(4): return(4);
		case ISDN_LAYER(5): return(5);
		case ISDN_LAYER(6): return(6);
		case ISDN_LAYER(7): return(7);
		case 0:	return(-1);
	}
	return(-2);
}

int
get_protocol(hisaxstack_t *st, int layermask)
{
	int layer = layermask2layer(layermask);

	if (!st){
		int_error();
		return(-EINVAL);
	}
	if (layer<0) {
		int_errtxt("lmask(%x) layer(%x) st(%x)",
			layermask, layer, st->id);
		return(-EINVAL);
	}
	return(st->pid.protocol[layer]);
}

int get_down_layer(int layermask) {
	int downlayer = 2;
	
	if (layermask>255 || (layermask & 1)) {
		int_errtxt("lmask %x out of range", layermask);
		return(0);
	}
	while(downlayer & 0xFF) {
		if (downlayer & layermask)
			break;
		downlayer <<= 1;
	}
	if (downlayer & 0xFF)
		downlayer >>= 1;
	else
		downlayer = 0;
	return(downlayer);
}

int get_up_layer(int layermask) {
	int uplayer = 0x40;
	
	if (layermask>=128) {
		int_errtxt("lmask %x out of range", layermask);
		return(0);
	}
	while(uplayer) {
		if (uplayer & layermask)
			break;
		uplayer >>= 1;
	}
	if (uplayer)
		uplayer <<= 1;
	else
		uplayer = 1;
	return(uplayer);
}

int DelIF(hisaxinstance_t *inst, hisaxif_t *mif, void *func, void *data) {
	hisaxif_t hif;

	memset(&hif, 0, sizeof(hisaxif_t));
	hif.protocol = mif->protocol;
	hif.layermask = mif->layermask;
	hif.fdata = data;
	hif.func = func;
	hif.func(&hif, MGR_DELIF | REQUEST, 0, 0, NULL);
	mif->protocol = ISDN_PID_NONE;
	inst->obj->ctrl(inst->st, MGR_ADDIF | REQUEST, mif);
	return(inst->obj->ctrl(inst->st, MGR_DELIF | REQUEST, &hif));
}

/* $Id: dchannel.c,v 1.1 2003/06/21 22:04:45 kkeil Exp $
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#define __NO_VERSION__
#include <linux/hisaxif.h>
#include "hisaxl1.h"
#include "helper.h"
#include "hisax_dch.h"

static void
dchannel_rcv(dchannel_t *dch)
{
	struct sk_buff	*skb;
	int		err;

	while ((skb = skb_dequeue(&dch->rqueue))) {
		err = if_newhead(&dch->inst.up, PH_DATA_IND, DINFO_SKB, skb);
		if (err < 0) {
			printk(KERN_WARNING "HiSax: dchannel deliver err %d\n", err);
			dev_kfree_skb(skb);
		}
	}
}

static void
dchannel_bh(dchannel_t *dch)
{
	if (!dch)
		return;
	if (dch->debug)
		printk(KERN_DEBUG "%s: event %x\n", __FUNCTION__, dch->event);
#if 0
	if (test_and_clear_bit(D_CLEARBUSY, &dch->event)) {
		if (dch->debug)
			debugprint(&dch->inst, "D-Channel Busy cleared");
		stptr = dch->stlist;
		while (stptr != NULL) {
			stptr->l1.l1l2(stptr, PH_PAUSE | CONFIRM, NULL);
			stptr = stptr->next;
		}
	}
#endif
	if (test_and_clear_bit(D_XMTBUFREADY, &dch->event)) {
		struct sk_buff *skb = dch->next_skb;

		if (skb) {
			dch->next_skb = NULL;
			skb_trim(skb, 0);
			if (if_newhead(&dch->inst.up, PH_DATA_CNF, DINFO_SKB,
				skb))
				dev_kfree_skb(skb);
		}
	}
	if (test_and_clear_bit(D_RCVBUFREADY, &dch->event))
		dchannel_rcv(dch);
	if (dch->hw_bh)
		dch->hw_bh(dch);
}

int
init_dchannel(dchannel_t *dch) {
	if (!(dch->dlog = kmalloc(MAX_DLOG_SPACE, GFP_ATOMIC))) {
		printk(KERN_WARNING
			"HiSax: No memory for dlog\n");
		return(-ENOMEM);
	}
	if (!(dch->tx_buf = kmalloc(MAX_DFRAME_LEN_L1, GFP_ATOMIC))) {
		printk(KERN_WARNING
			"HiSax: No memory for dchannel tx_buf\n");
		kfree(dch->dlog);
		dch->dlog = NULL;
		return(-ENOMEM);
	}
	dch->hw = NULL;
	dch->rx_skb = NULL;
	dch->tx_idx = 0;
	dch->next_skb = NULL;
	dch->event = 0;
	dch->tqueue.data = dch;
	dch->tqueue.routine = (void *) (void *) dchannel_bh;
	dch->hw_bh = NULL;
	skb_queue_head_init(&dch->rqueue);
	return(0);
}

int
free_dchannel(dchannel_t *dch) {

	if (dch->tqueue.sync)
		printk(KERN_ERR"free_dchannel tqueue.sync\n");
	discard_queue(&dch->rqueue);
	if (dch->rx_skb) {
		dev_kfree_skb(dch->rx_skb);
		dch->rx_skb = NULL;
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

void
set_dchannel_pid(hisax_pid_t *pid, int protocol, int layermask) {

	if (!layermask)
		layermask = ISDN_LAYER(0)| ISDN_LAYER(1) | ISDN_LAYER(2) |
			ISDN_LAYER(3) | ISDN_LAYER(4);
	
	memset(pid, 0, sizeof(hisax_pid_t));
	pid->layermask = layermask;
	if (layermask & ISDN_LAYER(0))
		pid->protocol[0] = ISDN_PID_L0_TE_S0;
	if (layermask & ISDN_LAYER(1))
		pid->protocol[1] = ISDN_PID_L1_TE_S0;
	if (layermask & ISDN_LAYER(2))
		pid->protocol[2] = ISDN_PID_L2_LAPD;
	if (layermask & ISDN_LAYER(3)) {
		if (protocol == 2)
			pid->protocol[3] = ISDN_PID_L3_DSS1USER;
	}
	if (layermask & ISDN_LAYER(4))
		pid->protocol[4] = ISDN_PID_L4_CAPI20;
}

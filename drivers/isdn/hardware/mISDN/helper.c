/* $Id: helper.c,v 1.3 2003/06/21 21:39:54 kkeil Exp $
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#define __NO_VERSION__
#include <linux/hisaxif.h>
#include "helper.h"
#include "hisax_bch.h"

int
init_bchannel(bchannel_t *bch) {
	int	devtyp = HISAX_RAW_DEVICE;

	if (!(bch->blog = kmalloc(MAX_BLOG_SPACE, GFP_ATOMIC))) {
		printk(KERN_WARNING
			"HiSax: No memory for blog\n");
		return(-ENOMEM);
	}
	if (!(bch->rx_buf = kmalloc(MAX_DATA_MEM, GFP_ATOMIC))) {
		printk(KERN_WARNING
			"HiSax: No memory for bchannel rx_buf\n");
		kfree(bch->blog);
		bch->blog = NULL;
		return (-ENOMEM);
	}
	if (!(bch->tx_buf = kmalloc(MAX_DATA_MEM, GFP_ATOMIC))) {
		printk(KERN_WARNING
			"HiSax: No memory for bchannel tx_buf\n");
		kfree(bch->blog);
		bch->blog = NULL;
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
	if (!bch->dev) {
		if (bch->inst.obj->ctrl(&bch->dev, MGR_GETDEVICE | REQUEST,
			&devtyp)) {
			printk(KERN_WARNING
				"HiSax: no raw device for bchannel\n");
		}
	}
	return(0);
}

int
free_bchannel(bchannel_t *bch) {

	if (bch->tqueue.sync)
		printk(KERN_ERR"free_bchannel tqueue.sync\n");
	discard_queue(&bch->rqueue);
	if (bch->blog) {
		kfree(bch->blog);
		bch->blog = NULL;
	}
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
	if (bch->inst.obj->ctrl(bch->dev, MGR_DELDEVICE | REQUEST, NULL)) {
		printk(KERN_WARNING
			"HiSax: del raw device error\n");
	} else
		bch->dev = NULL;
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
	pid->global = 0;
	if (*p == 2) { // len of 1 word
		p++;
		w = (__u16 *)p;
		pid->global = *w;
	}
	return(0);
}

int
HasProtocol(hisaxobject_t *obj, int protocol)
{
	int layer;
	int pmask;

	if (!obj) {
		int_error();
		return(0);
	}
	layer = (protocol & ISDN_PID_LAYER_MASK)>>24;
	if (layer > MAX_LAYER_NR) {
		int_errtxt("layer %d", layer);
		return(0);
	}
	if (protocol & ISDN_PID_BCHANNEL_BIT)
		pmask = obj->BPROTO.protocol[layer];
	else
		pmask = obj->DPROTO.protocol[layer];
	if (pmask == ISDN_PID_ANY)
		return(0);
	if (protocol == (protocol & pmask))
		return(1);
	else
		return(0); 
}

int
SetHandledPID(hisaxobject_t *obj, hisax_pid_t *pid)
{
	int layer;
	int ret = 0;
	hisax_pid_t sav;

	if (!obj || !pid) {
		int_error();
		return(0);
	}
	printk(KERN_DEBUG "%s: %s LM(%x)\n", __FUNCTION__, obj->name,
		pid->layermask);
	memcpy(&sav, pid, sizeof(hisax_pid_t));
	memset(pid, 0, sizeof(hisax_pid_t));
	pid->global = sav.global;
	if (!sav.layermask) {
		printk(KERN_WARNING "%s: no layermask in pid\n",
			__FUNCTION__);
		return(0);
	}
	for (layer=0; layer<=MAX_LAYER_NR; layer++) {
		if (!(ISDN_LAYER(layer) & sav.layermask)) {
			if (ret)
				break;
			else
				continue;
		}
		if (HasProtocol(obj, sav.protocol[layer])) {
			ret++;
			pid->protocol[layer] = sav.protocol[layer];
			pid->param[layer] = sav.param[layer];
			pid->layermask |= ISDN_LAYER(layer);
		} else
			break;
	}
	return(ret);
}

void
RemoveUsedPID(hisax_pid_t *pid, hisax_pid_t *used)
{
	int layer;

	if (!used || !pid) {
		int_error();
		return;
	}
	if (!used->layermask)
		return;
	
	for (layer=0; layer<=MAX_LAYER_NR; layer++) {
		if (!(ISDN_LAYER(layer) & used->layermask))
				continue;
		pid->protocol[layer] = ISDN_PID_NONE;
		pid->param[layer] = NULL;
		pid->layermask &= ~(ISDN_LAYER(layer));
	}
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
get_protocol(hisaxstack_t *st, int layer)
{

	if (!st){
		int_error();
		return(-EINVAL);
	}
	if (LAYER_OUTRANGE(layer)) {
		int_errtxt("L%d st(%x)", layer, st->id);
		return(-EINVAL);
	}
	return(st->pid.protocol[layer]);
}

int
get_lowlayer(int layermask)
{
	int layer;

	if (!layermask)
		return(0);
	for (layer=0; layer <= MAX_LAYER_NR; layer++)
		if (layermask & ISDN_LAYER(layer))
			return(layer);
	return(0);
}

int
get_down_layer(int layermask)
{
	int downlayer = 1;
	
	if (layermask>255 || (layermask & 1)) {
		int_errtxt("lmask %x out of range", layermask);
		return(0);
	}
	while(downlayer <= MAX_LAYER_NR) {
		if (ISDN_LAYER(downlayer) & layermask)
			break;
		downlayer++;
	}
	downlayer--;
	return(downlayer);
}

int get_up_layer(int layermask) {
	int uplayer = MAX_LAYER_NR;
	
	if (layermask>=128) {
		int_errtxt("lmask %x out of range", layermask);
		return(0);
	}
	while(uplayer>=0) {
		if (ISDN_LAYER(uplayer) & layermask)
			break;
		uplayer--;
	}
	uplayer++;
	return(uplayer);
}

int
SetIF(hisaxinstance_t *owner, hisaxif_t *hif, u_int prim, void *upfunc,
	void *downfunc, void *data)
{
	hisaxif_t *own_hif;

	if (!owner) {
		int_error();
		return(-EINVAL);
	}
	if (!hif) {
		int_error();
		return(-EINVAL);
	}
	if (IF_TYPE(hif) == IF_UP) {
		hif->func = upfunc;
		own_hif = &owner->up;
		printk(KERN_DEBUG "%s: IF_UP: func:%p(%p)\n",
			__FUNCTION__, hif->func, owner->data);
	} else if (IF_TYPE(hif) == IF_DOWN) {
		hif->func = downfunc;
		own_hif = &owner->down;
		printk(KERN_DEBUG "%s: IF_DOWN: func:%p(%p)\n",
			__FUNCTION__, hif->func, owner->data);
	} else {
		int_errtxt("stat(%x)", hif->stat);
		return(-EINVAL);
	}
	hif->peer = owner;
	hif->fdata = data;
	if ((prim & SUBCOMMAND_MASK) == REQUEST) {
		if (own_hif->stat == IF_NOACTIV) {
			if (IF_TYPE(hif) == IF_UP)
				own_hif->stat = IF_DOWN;
			else
				own_hif->stat = IF_UP;
			own_hif->owner = owner;
			return(hif->owner->obj->own_ctrl(hif->owner,
				MGR_SETIF | INDICATION, own_hif));
		} else {
			int_errtxt("REQ own stat(%x)", own_hif->stat);
			return(-EBUSY);
		}
	}
	return(0);
}

int
ConnectIF(hisaxinstance_t *owner, hisaxinstance_t *peer)
{
	hisaxif_t *hif;

	if (!owner) {
		int_error();
		return(-EINVAL);
	}
	if (!peer) {
		int_error();
		return(-EINVAL);
	}
	
	if (owner->pid.layermask < peer->pid.layermask) {
		hif = &owner->up;
		hif->owner = owner;
		hif->stat = IF_DOWN;
	} else if (owner->pid.layermask > peer->pid.layermask) {
		hif = &owner->down;
		hif->owner = owner;
		hif->stat = IF_UP;
	} else {
		int_errtxt("OLM == PLM: %x", owner->pid.layermask);
		return(-EINVAL);
	}
	return(peer->obj->own_ctrl(peer, MGR_SETIF | REQUEST, hif));
}

int DisConnectIF(hisaxinstance_t *inst, hisaxif_t *hif) {
	
	if (hif) {
		if (hif->next && hif->next->owner) {
			hif->next->owner->obj->ctrl(hif->next->owner,
				MGR_DISCONNECT | REQUEST, hif->next);
		}	
		if (inst->up.peer) {
			if (inst->up.peer == hif->owner)
				inst->up.peer->obj->ctrl(inst->up.peer,
					MGR_DISCONNECT | INDICATION, &inst->up);
		}
		if (inst->down.peer) {
			if (inst->down.peer == hif->owner)
				inst->down.peer->obj->ctrl(inst->down.peer,
					MGR_DISCONNECT | INDICATION, &inst->down);
		}
	}
	return(0);
}


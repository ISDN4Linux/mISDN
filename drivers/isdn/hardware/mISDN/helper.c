/* $Id: helper.c,v 1.9 2003/08/01 22:15:52 kkeil Exp $
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#define __NO_VERSION__
#include <linux/mISDNif.h>
#include "helper.h"

#undef	DEBUG_LM
#undef	DEBUG_IF

void
set_dchannel_pid(mISDN_pid_t *pid, int protocol, int layermask)
{
	if (!layermask)
		layermask = ISDN_LAYER(0)| ISDN_LAYER(1) | ISDN_LAYER(2) |
			ISDN_LAYER(3) | ISDN_LAYER(4);
	
	memset(pid, 0, sizeof(mISDN_pid_t));
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

int
bprotocol2pid(void *bp, mISDN_pid_t *pid)
{
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
HasProtocol(mISDNobject_t *obj, u_int protocol)
{
	int layer;
	u_int pmask;

	if (!obj) {
		int_error();
		return(-EINVAL);
	}
	layer = (protocol & ISDN_PID_LAYER_MASK)>>24;
	if (layer > MAX_LAYER_NR) {
		int_errtxt("layer %d", layer);
		return(-EINVAL);
	}
	if (protocol & ISDN_PID_BCHANNEL_BIT)
		pmask = obj->BPROTO.protocol[layer];
	else
		pmask = obj->DPROTO.protocol[layer];
	if (pmask == ISDN_PID_ANY)
		return(-EPROTO);
	if (protocol == (protocol & pmask))
		return(0);
	else
		return(-ENOPROTOOPT); 
}

int
SetHandledPID(mISDNobject_t *obj, mISDN_pid_t *pid)
{
	int layer;
	int ret = 0;
	mISDN_pid_t sav;

	if (!obj || !pid) {
		int_error();
		return(0);
	}
#ifdef DEBUG_LM
	printk(KERN_DEBUG "%s: %s LM(%x)\n", __FUNCTION__, obj->name, pid->layermask);
#endif
	memcpy(&sav, pid, sizeof(mISDN_pid_t));
	memset(pid, 0, sizeof(mISDN_pid_t));
	pid->global = sav.global;
	if (!sav.layermask) {
		printk(KERN_WARNING "%s: no layermask in pid\n", __FUNCTION__);
		return(0);
	}
	for (layer=0; layer<=MAX_LAYER_NR; layer++) {
		if (!(ISDN_LAYER(layer) & sav.layermask)) {
			if (ret)
				break;
			else
				continue;
		}
		if (0 == HasProtocol(obj, sav.protocol[layer])) {
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
RemoveUsedPID(mISDN_pid_t *pid, mISDN_pid_t *used)
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
get_protocol(mISDNstack_t *st, int layer)
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

int
get_up_layer(int layermask) {
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
SetIF(mISDNinstance_t *owner, mISDNif_t *hif, u_int prim, void *upfunc,
	void *downfunc, void *data)
{
	mISDNif_t *own_hif;

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
#ifdef DEBUG_IF
		printk(KERN_DEBUG "%s: IF_UP: func:%p(%p)\n", __FUNCTION__, hif->func, owner->data);
#endif
	} else if (IF_TYPE(hif) == IF_DOWN) {
		hif->func = downfunc;
		own_hif = &owner->down;
#ifdef DEBUG_IF
		printk(KERN_DEBUG "%s: IF_DOWN: func:%p(%p)\n", __FUNCTION__, hif->func, owner->data);
#endif
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
ConnectIF(mISDNinstance_t *owner, mISDNinstance_t *peer)
{
	mISDNif_t *hif;

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

int
DisConnectIF(mISDNinstance_t *inst, mISDNif_t *hif) {
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

void
init_mISDNinstance(mISDNinstance_t *inst, mISDNobject_t *obj, void *data)
{
	if (!obj) {
		int_error();
		return;
	}
	if (!obj->ctrl) {
		int_error();
		return;
	}
	inst->obj = obj;
	inst->data = data;
	inst->up.owner = inst;
	inst->down.owner = inst;
	obj->ctrl(NULL, MGR_DISCONNECT | REQUEST, &inst->down);
	obj->ctrl(NULL, MGR_DISCONNECT | REQUEST, &inst->up);
}

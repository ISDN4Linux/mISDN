/* $Id: stack.c,v 0.7 2001/03/26 11:40:02 kkeil Exp $
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#include "hisax_core.h"

hisaxstack_t	*hisax_stacklist = NULL;

int
get_stack_cnt(void) {
	int cnt = 0;
	hisaxstack_t *st = hisax_stacklist;

	while(st) {
		cnt++;
		st = st->next;
	}
	return(cnt);
}

void
get_stack_profile(iframe_t *frm) {
	hisaxstack_t *cst, *st = hisax_stacklist;
	int cnt = 0, i;
	int *dp,*ccnt;

	while(st) {
		cnt++;
		if (cnt == frm->addr) {
			dp = frm->data.p;
			*dp++ = st->id;
			for (i=0; i<=MAX_LAYER_NR; i++)
				*dp++ = st->pid.protocol[i];
			ccnt = dp++;
			*ccnt=0;
			cst = st->child;
			while(cst) {
				(*ccnt)++;
				*dp++ = cst->id;
				for (i=0; i<=MAX_LAYER_NR; i++)
					*dp++ = cst->pid.protocol[i];
				cst = cst->next;
			}
			frm->len = (u_char *)dp - (u_char *)frm->data.p;
			return;
		}
		st = st->next;
	}
	frm->len = 0;	
}

static int
get_free_stackid(hisaxstack_t *mst) {
	int id=1;
	hisaxstack_t *st;

	if (!mst) {
		while(id<127) {
			st = hisax_stacklist;
			while (st) {
				if (st->id == id)
					break;
				st = st->next;
			}
			if (st)
				id++;
			else
				return(id);
		}
		return(0); /* 127 used controllers ??? */
	} else { /* new child_id */
		id = mst->id;
		while(id<0x7fffffff) {
			id += 0x00010000;
			st = mst->child;
			while (st) {
				if (st->id == id)
					break;
				st = st->next;
			}
			if (!st)
				return(id);
		}
	}
	return(0);
}

hisaxstack_t *
get_stack4id(int id)
{
	hisaxstack_t *cst, *st = hisax_stacklist;

	
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "get_stack4id(%x)\n", id);
	while(st) {
		if (id == st->id)
			return(st);
		cst = st->child;
		while (cst) {
			if (cst->id == id)
				return(cst);
			cst = cst->next;
		}
		st = st->next;
	}
	return(NULL);
}

hisaxlayer_t *
getlayer4lay(hisaxstack_t *st, int layermask)
{
	hisaxlayer_t	*layer;
	hisaxinstance_t	*inst;

	if (!st) {
		int_error();
		return(NULL);
	}
	layer = st->lstack;
	while(layer) {
		inst = layer->inst;
		while(inst) {
			if (inst->pid.layermask & layermask)
				goto out;
			inst = inst->next;
		}
		layer = layer->next;
	}
out:
	return(layer);
}

hisaxinstance_t *
get_instance(hisaxstack_t *st, int layer_nr, int protocol)
{
	hisaxlayer_t	*layer;
	hisaxinstance_t	*inst=NULL;

	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "get_instance st(%p) lnr(%d) prot(%x)\n",
			st, layer_nr, protocol);
	if (!st) {
		int_error();
		return(NULL);
	}
	if ((layer_nr<0) || (layer_nr>MAX_LAYER_NR)) {
		int_errtxt("lnr %d", layer_nr);
		return(NULL);
	}
	layer = st->lstack;
	while(layer) {
		inst = layer->inst;
		while(inst) {
			if (core_debug & DEBUG_CORE_FUNC)
				printk(KERN_DEBUG "get_instance inst(%p) lm %x/%x prot %x/%x\n",
					inst, inst->pid.layermask, ISDN_LAYER(layer_nr),
					inst->pid.protocol[layer_nr], protocol);
			if ((inst->pid.layermask & ISDN_LAYER(layer_nr)) &&
				(inst->pid.protocol[layer_nr] == protocol))
				goto out;
			if (inst == inst->next) {
				int_errtxt("deadloop inst %p %s", inst, inst->name);
				return(NULL);
			}
			inst = inst->next;
		}
		if (layer == layer->next) {
			int_errtxt("deadloop layer %p", layer);
			return(NULL);
		}
		layer = layer->next;
	}
out:
	return(inst);
}

int
get_layermask(hisaxlayer_t *layer)
{
	int mask = 0;
	hisaxinstance_t *inst;

	inst = layer->inst;
	while(inst) {
		mask |= inst->pid.layermask;
		inst = inst->next;
	}
	return(mask);
}

int
insertlayer(hisaxstack_t *st, hisaxlayer_t *layer, int layermask)
{
	hisaxlayer_t *item;
	
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG __FUNCTION__"(%p, %p, %x)\n",
			st, layer, layermask);  
	if (!st || !layer) {
		int_error();
		return(-EINVAL);
	}
	item = st->lstack;
	if (!item) {
		st->lstack = layer;
	} else {
		while(item) {
			if (layermask < get_layermask(item)) {
				INSERT_INTO_LIST(layer, item, st->lstack);
				return(0);
			} else {
				if (!item->next)
					break;
			}
			item = item->next;
		}
		item->next = layer;
		layer->prev = item;
	}
	return(0);
}

hisaxstack_t *
new_stack(hisaxstack_t *master, hisaxinstance_t *inst)
{
	hisaxstack_t *newst;

	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "create %s stack inst(%p)\n",
			master ? "child" : "master", inst);
	if (!(newst = kmalloc(sizeof(hisaxstack_t), GFP_ATOMIC))) {
		printk(KERN_ERR "kmalloc hisax_stack failed\n");
		return(NULL);
	}
	memset(newst, 0, sizeof(hisaxstack_t));
	newst->id = get_free_stackid(master);
	newst->mgr = inst;
	if (master) {
		APPEND_TO_LIST(newst, master->child);
	} else {
		APPEND_TO_LIST(newst, hisax_stacklist);
	}
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "Stack id %x added\n", newst->id);
	if (inst)
		inst->st = newst;
	return(newst);
}


static int
release_layers(hisaxstack_t *st, u_int prim) {
	hisaxinstance_t *inst, *tmp;
	hisaxlayer_t    *layer;
	int cnt = 0;

	while((layer = st->lstack)) {
		inst = layer->inst;
		while (inst) {
			if (core_debug & DEBUG_CORE_FUNC)
				printk(KERN_DEBUG __FUNCTION__ ": st(%p) inst(%p) lm(%x)\n",
					st, inst, inst->pid.layermask);
			tmp = inst->next;
			inst->obj->own_ctrl(inst, prim, NULL);
			inst = tmp;
		}
		REMOVE_FROM_LISTBASE(layer, st->lstack);
		kfree(layer);
		if (cnt++ > 1000) {
			int_errtxt("release_layers st(%p)", st);
			return(-EINVAL);
		}
	}
	return(0);
}

int
release_stack(hisaxstack_t *st) {
	int err;
	hisaxstack_t *cst;

	while (st->child) {
		cst = st->child;
		if ((err = release_layers(cst, MGR_RELEASE | INDICATION))) {
			printk(KERN_WARNING "release_stack child err(%d)\n", err);
			return(err);
		}
		REMOVE_FROM_LISTBASE(cst, st->child);
		kfree(cst);
	}
	if ((err = release_layers(st, MGR_RELEASE | INDICATION))) {
		printk(KERN_WARNING "release_stack err(%d)\n", err);
		return(err);
	}
	REMOVE_FROM_LISTBASE(st, hisax_stacklist);
	kfree(st);
	return(0);
}

void
release_stacks(hisaxobject_t *obj) {
	hisaxstack_t *st, *tmp;
	hisaxlayer_t *layer;
	hisaxinstance_t *inst;
	int rel;

	st = hisax_stacklist;
	while (st) {
		rel = 0;
		layer = st->lstack;
		while(layer) {
			inst = layer->inst;
			while (inst) {
				if (inst->obj == obj)
					rel++;
				inst = inst->next;
			}
			layer = layer->next;
		}		
		if (rel) {
			tmp = st->next;
			release_stack(st);
			st = tmp;
		} else
			st = st->next;
	}
	if (obj->refcnt)
		printk(KERN_WARNING "release_stacks obj %s refcnt is %d\n",
			obj->name, obj->refcnt);
}

int
register_layer(hisaxstack_t *st, hisaxinstance_t *inst) {
	int lay;
	hisaxlayer_t *layer;

	if (!st || !inst)
		return(-EINVAL);
	lay = inst->pid.layermask;
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "register_layer st(%p) inst(%p/%p) lmask(%x)\n",
			st, inst, inst->obj, lay);
	if (!(layer = getlayer4lay(st, lay))) {
		if (!(layer = kmalloc(sizeof(hisaxlayer_t), GFP_ATOMIC))) {
			int_errtxt("no mem for layer %d", lay);
			return(-ENOMEM);
		}
		memset(layer, 0, sizeof(hisaxlayer_t));
		insertlayer(st, layer, lay);
	}
	APPEND_TO_LIST(inst, layer->inst);
	inst->st = st;
	inst->obj->refcnt++;
	return(0);
}

int
unregister_instance(hisaxinstance_t *inst) {
	hisaxstack_t *st;
	hisaxlayer_t *layer;

	if (!inst || !inst->st)
		return(-EINVAL);
	st = inst->st;
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "unregister_instance st(%p) inst(%p) lay(%x)\n",
			st, inst, inst->pid.layermask);
	if ((layer = getlayer4lay(st, inst->pid.layermask))) {
		REMOVE_FROM_LISTBASE(inst, layer->inst);
		inst->obj->refcnt--;
	} else
		return(-ENODEV);
	return(0);
}

int
set_stack(hisaxstack_t *st, hisax_pid_t *pid) {
	int err;
	hisaxinstance_t *inst;
	hisaxlayer_t *hl;

	if (!st || !pid) {
		int_error();
		return(-EINVAL);
	}
	memcpy(&st->pid, pid, sizeof(hisax_pid_t));
	if (!st->mgr || !st->mgr->obj || !st->mgr->obj->ctrl) {
		int_error();
		return(-EINVAL);
	}
	memcpy(&st->mgr->pid, pid, sizeof(hisax_pid_t));
	if (!SetHandledPID(st->mgr->obj, &st->mgr->pid)) {
		int_error();
		return(-ENOPROTOOPT);
	} else {
		RemoveUsedPID(pid, &st->mgr->pid);
	}
	err = st->mgr->obj->ctrl(st, MGR_REGLAYER | REQUEST, st->mgr);
	if (err) {
		int_error();
		return(err);
	}
	while (pid->layermask) {
		inst = get_next_instance(st, pid);
		if (!inst) {
			int_error();
			st->mgr->obj->ctrl(st, MGR_CLEARSTACK| REQUEST, NULL);
			return(-ENOPROTOOPT);
		}
		RemoveUsedPID(pid, &inst->pid);
	}
	hl = st->lstack;
	while(hl && hl->next) {
		hl->inst->obj->own_ctrl(hl->inst, MGR_CONNECT | REQUEST,
			hl->next->inst);
		hl = hl->next;
	}
	st->mgr->obj->own_ctrl(st->mgr, MGR_SETSTACK |CONFIRM, NULL);
	return(0);
}

int
clear_stack(hisaxstack_t *st) {

	if (!st)
		return(-EINVAL);
	memset(&st->pid, 0, sizeof(hisax_pid_t));
	return(release_layers(st, MGR_UNREGLAYER | REQUEST));
}

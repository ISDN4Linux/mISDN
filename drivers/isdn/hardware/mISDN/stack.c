/* $Id: stack.c,v 0.17 2001/10/31 23:06:07 kkeil Exp $
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#include "hisax_core.h"

hisaxstack_t	*hisax_stacklist = NULL;
hisaxinstance_t	*hisax_instlist = NULL;

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
get_stack_info(iframe_t *frm) {
	hisaxstack_t *cst, *st;
	stack_info_t *si;
	hisaxlayer_t *lay;

	st = get_stack4id(frm->addr);
	if (!st)
		frm->len = 0;
	else {
		si = (stack_info_t *)frm->data.p;
		memset(si, 0, sizeof(stack_info_t));
		si->id = st->id;
		si->extentions = st->extentions;
		if (st->mgr)
			si->mgr = st->mgr->id;
		else
			si->mgr = 0;
		memcpy(&si->pid, &st->pid, sizeof(hisax_pid_t));
		si->instcnt = 0;
		lay = st->lstack;
		while(lay) {
			if (lay->inst) {
				si->inst[si->instcnt] = lay->inst->id;
				si->instcnt++;
			}
			lay = lay->next;
		}
		si->childcnt = 0;
		cst = st->child;
		while(cst) {
			si->child[si->childcnt] = cst->id;
			si->childcnt++;
			cst = cst->next;
		}
		frm->len = sizeof(stack_info_t);
		if (si->childcnt>2)
			frm->len += (si->childcnt-2)*sizeof(int);
	}
}

static int
get_free_stackid(hisaxstack_t *mst, int flag) {
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
	} else if (flag & FLG_CLONE_STACK) {
		id = mst->id | FLG_CLONE_STACK;
		while(id < CLONE_ID_MAX) {
			id += CLONE_ID_INC;
			st = hisax_stacklist;
			while (st) {
				if (st->id == id)
					break;
				st = st->next;
			}
			if (!st)
				return(id);
		}
	} else if (flag & FLG_CHILD_STACK) {
		id = mst->id | FLG_CHILD_STACK;
		while(id < CHILD_ID_MAX) {
			id += CHILD_ID_INC;
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
	if (!id) /* 0 isn't a valid id */
		return(NULL);
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
		if(inst && (inst->pid.layermask & layermask))
			break;
		layer = layer->next;
	}
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
		if (inst) {
			if (core_debug & DEBUG_CORE_FUNC)
				printk(KERN_DEBUG "get_instance inst(%p, %x) lm %x/%x prot %x/%x\n",
					inst, inst->id, inst->pid.layermask, ISDN_LAYER(layer_nr),
					inst->pid.protocol[layer_nr], protocol);
			if ((inst->pid.layermask & ISDN_LAYER(layer_nr)) &&
				(inst->pid.protocol[layer_nr] == protocol))
				goto out;
			inst = NULL;
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

hisaxinstance_t *
get_instance4id(int id)
{
	hisaxinstance_t *inst = hisax_instlist;

	while(inst) {
		if (inst->id == id)
			return(inst);
		inst = inst->next;
	}
	return(NULL);
}

int
get_layermask(hisaxlayer_t *layer)
{
	int mask = 0;

	if (layer->inst)
		mask |= layer->inst->pid.layermask;
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
	if (!master) {
		if (inst && inst->st) {
			newst->id = get_free_stackid(inst->st, FLG_CLONE_STACK);
		} else {
			newst->id = get_free_stackid(NULL, 0);
		}
	} else {
		newst->id = get_free_stackid(master, FLG_CHILD_STACK);
	}
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
	hisaxinstance_t *inst;
	hisaxlayer_t    *layer;
	int cnt = 0;

	while((layer = st->lstack)) {
		inst = layer->inst;
		if (inst) {
			if (core_debug & DEBUG_CORE_FUNC)
				printk(KERN_DEBUG __FUNCTION__ ": st(%p) inst(%p):%x %s lm(%x)\n",
					st, inst, inst->id, inst->name,
					inst->pid.layermask);
			inst->obj->own_ctrl(inst, prim, NULL);
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

	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG __FUNCTION__ ": st(%p)\n", st);
	while (st->child) {
		cst = st->child;
		if (core_debug & DEBUG_CORE_FUNC)
			printk(KERN_DEBUG __FUNCTION__ ": cst(%p)\n", cst);
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
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG __FUNCTION__ ": obj(%p) %s\n",
			obj, obj->name);
	while (st) {
		rel = 0;
		layer = st->lstack;
		if (core_debug & DEBUG_CORE_FUNC)
			printk(KERN_DEBUG __FUNCTION__ ": st(%p) l(%p)\n",
				st, layer);
		while(layer) {
			inst = layer->inst;
			if (inst) {
				if (core_debug & DEBUG_CORE_FUNC)
					printk(KERN_DEBUG __FUNCTION__ ": inst(%p)\n",
						inst);
				if (inst->obj == obj)
					rel++;
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


static void
get_free_instid(hisaxstack_t *st, hisaxinstance_t *inst) {
	hisaxinstance_t *il = hisax_instlist;

	inst->id = get_lowlayer(inst->pid.layermask)<<20;
	inst->id |= FLG_INSTANCE;
	if (st) {
		inst->id |= st->id;
	} else {
		while(il) {
			if (il->id == inst->id) {
				if ((inst->id & IF_INSTMASK) >= INST_ID_MAX) {
					inst->id = 0;
					return;
				}
				inst->id += INST_ID_INC;
				il = hisax_instlist;
				continue;
			}
			il = il->next;
		}
	}
}

int
register_layer(hisaxstack_t *st, hisaxinstance_t *inst) {
	hisaxlayer_t	*layer = NULL;
	int		refinc = 0;

	if (!inst)
		return(-EINVAL);
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG __FUNCTION__":st(%p) inst(%p/%p) lmask(%x) id(%x)\n",
			st, inst, inst->obj, inst->pid.layermask, inst->id);
	if (inst->id) { /* allready registered */
		if (inst->st || !st) {
			int_errtxt("register duplicate %08x %p %p",
				inst->id, inst->st, st);
			return(-EBUSY);
		}
	}
	if (st) {
		if ((layer = getlayer4lay(st, inst->pid.layermask))) {
			if (layer->inst) {
				int_errtxt("stack %08x has layer %08x",
					st->id, layer->inst->id);
				return(-EBUSY);
			}
		} else if (!(layer = kmalloc(sizeof(hisaxlayer_t), GFP_ATOMIC))) {
			int_errtxt("no mem for layer %x", inst->pid.layermask);
			return(-ENOMEM);
		}
		memset(layer, 0, sizeof(hisaxlayer_t));
		insertlayer(st, layer, inst->pid.layermask);
		layer->inst = inst;
	}
	if (!inst->id)
		refinc++;
	get_free_instid(st, inst);
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG __FUNCTION__":inst(%p/%p) id(%x)%s\n",
			inst, inst->obj, inst->id, refinc ? " changed" : "");
	if (!inst->id) {
		int_errtxt("no free inst->id for layer %x", inst->pid.layermask);
		if (st && layer) {
			REMOVE_FROM_LISTBASE(layer, st->lstack);
			kfree(layer);
		}
		return(-EINVAL);
	}
	inst->st = st;
	if (refinc)
		inst->obj->refcnt++;
	APPEND_TO_LIST(inst, hisax_instlist);
	return(0);
}

int
unregister_instance(hisaxinstance_t *inst) {
	hisaxlayer_t *layer;
	int err = 0;

	if (!inst)
		return(-EINVAL);
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG __FUNCTION__": st(%p) inst(%p):%x lay(%x)\n",
			inst->st, inst, inst->id, inst->pid.layermask);
	if (inst->st) {
		if ((layer = getlayer4lay(inst->st, inst->pid.layermask))) {
			if (core_debug & DEBUG_CORE_FUNC)
				printk(KERN_DEBUG __FUNCTION__
					":layer(%p)->inst(%p)\n", layer, layer->inst);
			layer->inst = NULL;
		} else {
			printk(KERN_WARNING __FUNCTION__": no layer found\n");
			err = -ENODEV;
		}
		inst->st = NULL;
	}
	REMOVE_FROM_LISTBASE(inst, hisax_instlist);
	inst->prev = inst->next = NULL;
	inst->id = 0;
	inst->obj->refcnt--;
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
		if (!hl->inst) {
			int_error();
			return(-EINVAL);
		}
		if (!hl->inst->obj) {
			int_error();
			return(-EINVAL);
		}
		if (!hl->inst->obj->own_ctrl) {
			int_error();
			return(-EINVAL);
		}
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
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG __FUNCTION__": st(%p)\n", st);
	memset(&st->pid, 0, sizeof(hisax_pid_t));
	return(release_layers(st, MGR_UNREGLAYER | REQUEST));
}

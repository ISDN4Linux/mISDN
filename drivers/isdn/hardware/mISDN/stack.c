/* $Id: stack.c,v 1.5 2003/07/27 11:14:19 kkeil Exp $
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#include "core.h"

mISDNstack_t	*mISDN_stacklist = NULL;
mISDNinstance_t	*mISDN_instlist = NULL;

int
get_stack_cnt(void) {
	int cnt = 0;
	mISDNstack_t *st = mISDN_stacklist;

	while(st) {
		cnt++;
		st = st->next;
	}
	return(cnt);
}

void
get_stack_info(iframe_t *frm) {
	mISDNstack_t *cst, *st;
	stack_info_t *si;
	mISDNlayer_t *lay;

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
		memcpy(&si->pid, &st->pid, sizeof(mISDN_pid_t));
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
get_free_stackid(mISDNstack_t *mst, int flag) {
	u_int		id=1;
	mISDNstack_t	*st;

	if (!mst) {
		while(id<127) {
			st = mISDN_stacklist;
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
			st = mISDN_stacklist;
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

mISDNstack_t *
get_stack4id(u_int id)
{
	mISDNstack_t *cst, *st = mISDN_stacklist;

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

mISDNlayer_t *
getlayer4lay(mISDNstack_t *st, int layermask)
{
	mISDNlayer_t	*layer;
	mISDNinstance_t	*inst;

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

mISDNinstance_t *
get_instance(mISDNstack_t *st, int layer_nr, int protocol)
{
	mISDNlayer_t	*layer;
	mISDNinstance_t	*inst=NULL;

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

mISDNinstance_t *
get_instance4id(u_int id)
{
	mISDNinstance_t *inst = mISDN_instlist;

	while(inst) {
		if (inst->id == id)
			return(inst);
		inst = inst->next;
	}
	return(NULL);
}

int
get_layermask(mISDNlayer_t *layer)
{
	int mask = 0;

	if (layer->inst)
		mask |= layer->inst->pid.layermask;
	return(mask);
}

int
insertlayer(mISDNstack_t *st, mISDNlayer_t *layer, int layermask)
{
	mISDNlayer_t *item;
	
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s(%p, %p, %x)\n",
			__FUNCTION__, st, layer, layermask);  
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

mISDNstack_t *
new_stack(mISDNstack_t *master, mISDNinstance_t *inst)
{
	mISDNstack_t *newst;

	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "create %s stack inst(%p)\n",
			master ? "child" : "master", inst);
	if (!(newst = kmalloc(sizeof(mISDNstack_t), GFP_ATOMIC))) {
		printk(KERN_ERR "kmalloc mISDN_stack failed\n");
		return(NULL);
	}
	memset(newst, 0, sizeof(mISDNstack_t));
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
		APPEND_TO_LIST(newst, mISDN_stacklist);
	}
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "Stack id %x added\n", newst->id);
	if (inst)
		inst->st = newst;
	return(newst);
}


static int
release_layers(mISDNstack_t *st, u_int prim) {
	mISDNinstance_t *inst;
	mISDNlayer_t    *layer;
	int cnt = 0;

	while((layer = st->lstack)) {
		inst = layer->inst;
		if (inst) {
			if (core_debug & DEBUG_CORE_FUNC)
				printk(KERN_DEBUG  "%s: st(%p) inst(%p):%x %s lm(%x)\n",
					__FUNCTION__, st, inst, inst->id,
					inst->name, inst->pid.layermask);
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
release_stack(mISDNstack_t *st) {
	int err;
	mISDNstack_t *cst;

	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s: st(%p)\n", __FUNCTION__, st);
	while (st->child) {
		cst = st->child;
		if (core_debug & DEBUG_CORE_FUNC)
			printk(KERN_DEBUG "%s: cst(%p)\n", __FUNCTION__, cst);
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
	REMOVE_FROM_LISTBASE(st, mISDN_stacklist);
	kfree(st);
	return(0);
}

void
release_stacks(mISDNobject_t *obj) {
	mISDNstack_t *st, *tmp;
	mISDNlayer_t *layer;
	mISDNinstance_t *inst;
	int rel;

	st = mISDN_stacklist;
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s: obj(%p) %s\n",
			__FUNCTION__, obj, obj->name);
	while (st) {
		rel = 0;
		layer = st->lstack;
		if (core_debug & DEBUG_CORE_FUNC)
			printk(KERN_DEBUG "%s: st(%p) l(%p)\n",
				__FUNCTION__, st, layer);
		while(layer) {
			inst = layer->inst;
			if (inst) {
				if (core_debug & DEBUG_CORE_FUNC)
					printk(KERN_DEBUG "%s: inst(%p)\n",
						__FUNCTION__, inst);
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
get_free_instid(mISDNstack_t *st, mISDNinstance_t *inst) {
	mISDNinstance_t *il = mISDN_instlist;

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
				il = mISDN_instlist;
				continue;
			}
			il = il->next;
		}
	}
}

int
register_layer(mISDNstack_t *st, mISDNinstance_t *inst) {
	mISDNlayer_t	*layer = NULL;
	int		refinc = 0;

	if (!inst)
		return(-EINVAL);
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s:st(%p) inst(%p/%p) lmask(%x) id(%x)\n",
			__FUNCTION__, st, inst, inst->obj,
			inst->pid.layermask, inst->id);
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
		} else if (!(layer = kmalloc(sizeof(mISDNlayer_t), GFP_ATOMIC))) {
			int_errtxt("no mem for layer %x", inst->pid.layermask);
			return(-ENOMEM);
		}
		memset(layer, 0, sizeof(mISDNlayer_t));
		insertlayer(st, layer, inst->pid.layermask);
		layer->inst = inst;
	}
	if (!inst->id)
		refinc++;
	get_free_instid(st, inst);
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s: inst(%p/%p) id(%x)%s\n", __FUNCTION__,
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
	APPEND_TO_LIST(inst, mISDN_instlist);
	return(0);
}

int
unregister_instance(mISDNinstance_t *inst) {
	mISDNlayer_t *layer;
	int err = 0;

	if (!inst)
		return(-EINVAL);
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s: st(%p) inst(%p):%x lay(%x)\n",
			__FUNCTION__, inst->st, inst, inst->id, inst->pid.layermask);
	if (inst->st) {
		if ((layer = getlayer4lay(inst->st, inst->pid.layermask))) {
			if (core_debug & DEBUG_CORE_FUNC)
				printk(KERN_DEBUG "%s: layer(%p)->inst(%p)\n",
					__FUNCTION__, layer, layer->inst);
			layer->inst = NULL;
		} else {
			printk(KERN_WARNING "%s: no layer found\n", __FUNCTION__);
			err = -ENODEV;
		}
		if (inst->st && (inst->st->mgr != inst))
			inst->st = NULL;
	}
	REMOVE_FROM_LISTBASE(inst, mISDN_instlist);
	inst->prev = inst->next = NULL;
	inst->id = 0;
	inst->obj->refcnt--;
	return(0);
}

int
set_stack(mISDNstack_t *st, mISDN_pid_t *pid) {
	int err;
	mISDNinstance_t *inst;
	mISDNlayer_t *hl;

	if (!st || !pid) {
		int_error();
		return(-EINVAL);
	}
	memcpy(&st->pid, pid, sizeof(mISDN_pid_t));
	if (!st->mgr || !st->mgr->obj || !st->mgr->obj->ctrl) {
		int_error();
		return(-EINVAL);
	}
	memcpy(&st->mgr->pid, pid, sizeof(mISDN_pid_t));
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
clear_stack(mISDNstack_t *st) {

	if (!st)
		return(-EINVAL);
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s: st(%p)\n", __FUNCTION__, st);
	memset(&st->pid, 0, sizeof(mISDN_pid_t));
	return(release_layers(st, MGR_UNREGLAYER | REQUEST));
}

/* $Id: stack.c,v 0.1 2001/02/22 10:12:54 kkeil Exp $
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
	int i,cnt = 0;
	int *dp,*ccnt;

	while(st) {
		cnt++;
		if (cnt == frm->addr) {
			dp = frm->data.p;
			*dp++ = st->id;
			for (i=0; i<=MAX_LAYER; i++) {
				*dp++ = st->protocols[i];
			}
			ccnt=dp++;
			*ccnt=0;
			cst = st->child;
			while(cst) {
				(*ccnt)++;
				*dp++ = cst->id;
				for (i=0; i<=MAX_LAYER; i++) {
					*dp++ = cst->protocols[i];
				}
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

hisaxstack_t *
create_stack(hisaxinstance_t *inst, hisaxstack_t *master) {
	hisaxstack_t *newst;
	int err;

	if (!inst)
		return(NULL);
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "create %s stack for %s\n",
			master ? "child" : "master", inst->obj->name);
	if (!(newst = kmalloc(sizeof(hisaxstack_t), GFP_ATOMIC))) {
		printk(KERN_ERR "kmalloc hisax_stack failed\n");
		return(NULL);
	}
	memset(newst, 0, sizeof(hisaxstack_t));
	register_instance(newst, inst);
	newst->id = get_free_stackid(master);
	if ((err = inst->obj->own_ctrl(newst, MGR_ADDLAYER | CONFIRM, NULL))) {
		printk(KERN_ERR "hisax_stack register failed err %d\n", err);
		kfree(newst);
		inst->obj->refcnt--;
		return(NULL);
	}
	if (master) {
		APPEND_TO_LIST(newst, master->child);
	} else {
		APPEND_TO_LIST(newst, hisax_stacklist);
	}
	printk(KERN_INFO "Stack for %s.%d added\n", inst->obj->name, newst->id);
	return(newst);
}

static int
release_childstack(hisaxstack_t *st, hisaxstack_t *cst) {
	int l;
	hisaxinstance_t *inst, *tmp;

	for(l=0;l<=MAX_LAYER;l++) {
		inst = cst->inst[l];
		while (inst) {
			tmp = inst->next;
			inst->obj->own_ctrl(cst, MGR_RELEASE | INDICATION,
				inst);
			inst = tmp;
		}
	}
	REMOVE_FROM_LISTBASE(cst, st->child);
	kfree(cst);
	return(0);
}

static int
release_stack(hisaxstack_t *st) {
	int l;
	hisaxinstance_t *inst, *tmp;

	while (st->child)
		release_childstack(st, st->child);
	for(l=0;l<=MAX_LAYER;l++) {
		inst = st->inst[l];
		while (inst) {
			tmp = inst->next;
			inst->obj->own_ctrl(st, MGR_RELEASE | INDICATION,
				inst);
			inst = tmp;
		}
	}
	REMOVE_FROM_LISTBASE(st, hisax_stacklist);
	kfree(st);
	return(0);
}

void
release_stacks(hisaxobject_t *obj) {
	hisaxstack_t *st, *tmp;
	hisaxinstance_t *inst;
	int l, rel;

	if (!obj->refcnt)
		return;
	st = hisax_stacklist;
	while (st) {
		rel = 0;
		for (l=0;l<=MAX_LAYER;l++) {
			inst = st->inst[l];
			while (inst) {
				if (inst->obj == obj)
					rel++;
				inst = inst->next;
			}
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


/* $Id: core.c,v 0.12 2001/03/13 02:04:37 kkeil Exp $
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/config.h>
#include <linux/module.h>
#include "hisax_core.h"
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

hisaxobject_t	*hisax_objects = NULL;
int core_debug;

static int debug;
static int obj_id;

#ifdef MODULE
MODULE_AUTHOR("Karsten Keil");
MODULE_PARM(debug, "1i");
EXPORT_SYMBOL(HiSax_register);
EXPORT_SYMBOL(HiSax_unregister);
#define HiSaxInit init_module
#endif

static moditem_t modlist[] = {
	{"hisaxl1", ISDN_PID_L1_TE_S0},
	{"hisaxl2", ISDN_PID_L2_LAPD},
	{"hisaxl2", ISDN_PID_L2_B_X75SLP},
	{NULL, ISDN_PID_NONE}
};

static hisaxobject_t *
find_object(int protocol) {
	hisaxobject_t *obj = hisax_objects;
	int err;

	while (obj) {
		err = obj->own_ctrl(NULL, MGR_HASPROTOCOL | REQUEST, &protocol);
		if (!err)
			return(obj);
		if (err != -ENOPROTOOPT) {
			if (HasProtocol(obj, protocol))
				return(obj);
		}	
		obj = obj->next;
	}
	return(NULL);
}

static hisaxobject_t *
find_object_module(int protocol) {
	moditem_t *m = modlist;
	hisaxobject_t *obj;

	while (m->name != NULL) {
		if (m->protocol == protocol) {
#ifdef CONFIG_KMOD
			if (debug)
				printk(KERN_DEBUG
					"find_object_module %s - trying to load\n",
					m->name);
			request_module(m->name);
#else
			printk(KERN_WARNING "not possible to autoload %s please try to load manually\n",
				m->name);
#endif
			if ((obj = find_object(protocol)))
				return(obj);
		}
		m++;
	}
	if (debug)
		printk(KERN_DEBUG __FUNCTION__": no module for protocol %x found\n",
			protocol);
	return(NULL);
}

static void
remove_object(hisaxobject_t *obj) {
	hisaxstack_t *st = hisax_stacklist;
	hisaxlayer_t *layer;
	hisaxinstance_t *inst, *tmp;

	while (st) {
		layer = st->lstack;
		while(layer) {
			inst = layer->inst;
			while (inst) {
				if (inst->obj == obj) {
					tmp = inst->next;
					inst->obj->own_ctrl(st, MGR_RELEASE
						| INDICATION, inst);
					inst = tmp;
				} else
					inst = inst->next;
			}
			layer = layer->next;
		}
		st = st->next;
	}
}

static int
dummy_if(hisaxif_t *hif, u_int prim, int dinfo, int len, void *arg) {
	if (debug & DEBUG_DUMMY_FUNC)
		printk(KERN_DEBUG __FUNCTION__": prim:%x hif:%p dinfo:%x len:%d arg:%p\n",
			prim, hif, dinfo, len, arg);
	return(-EINVAL);
}

static int
add_stack_if(hisaxstack_t *st, hisaxif_t *hif) {
	int		err, lay;
	hisaxinstance_t	*inst;
	hisaxobject_t	*obj = NULL;

	if (!hif)
		return(-EINVAL);
	lay = layermask2layer(hif->layermask);
	if (debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "add_stack_if for layer %d proto %x/%x\n",
			lay, hif->protocol, hif->stat);
	if (hif->protocol == ISDN_PID_NONE) {
		printk(KERN_WARNING "add_stack_if: for protocol none\n");
		hif->fdata = NULL;
		hif->func = dummy_if;
		hif->stat = IF_NOACTIV;
		return(0);
	}
	if (lay<0) {
		int_errtxt("lm %x", hif->layermask);
		return(-EINVAL);
	}
	inst = get_instance(st, lay, hif->protocol);
	if (inst)
		obj = inst->obj;
	if (!obj)
		obj = find_object(hif->protocol);
	if (!obj)
		obj = find_object_module(hif->protocol);
	if (!obj) {
		printk(KERN_WARNING "add_stack_if: no object found\n");
		return(-ENOPROTOOPT);
	}
	if ((err = obj->own_ctrl(st, MGR_ADDIF | REQUEST, hif))) {
		return(err);
	}
	return(0);
}

static int
del_stack_if(hisaxstack_t *st, hisaxif_t *hif) {
	int		err, lay;
	hisaxinstance_t	*inst;
	hisaxobject_t	*obj = NULL;

	if (!hif)
		return(-EINVAL);
	if (!hif->layermask)
		return(-EINVAL);
	lay = layermask2layer(hif->layermask);
	if (debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "del_stack_if for layer %d proto %x/%x\n",
			lay, hif->protocol, hif->stat);
	if (lay<0) {
		int_errtxt("lm %x", hif->layermask);
		return(-EINVAL);
	}
	if (!(inst = get_instance(st, lay, hif->protocol))) {
		printk(KERN_DEBUG "del_stack_if no instance found\n");
		return(-ENODEV);
	}
	while(inst) {
		if (inst->pid.protocol[lay] == hif->protocol) {
			obj = inst->obj;
			err = obj->own_ctrl(st, MGR_DELIF | REQUEST, hif);
			if (err)
				printk(KERN_WARNING "del_stack_if err %d\n",
					err);
		}
		inst = inst->next;
	}
	if (!obj) {
		printk(KERN_WARNING "del_stack_if: no object found\n");
		return(-ENOPROTOOPT);
	}
	return(0);
}

static char tmpbuf[4096];
static int
debugout(hisaxinstance_t *inst, logdata_t *log)
{
	char *p = tmpbuf;

	if (log->head && *log->head)
		p += sprintf(p,"%s ", log->head);
	else
		p += sprintf(p,"%s ", inst->obj->name);
	p += vsprintf(p, log->fmt, log->args);
	printk(KERN_DEBUG "%s\n", tmpbuf);
	return(0);
}

static int central_manager(void *data, u_int prim, void *arg) {
	hisaxstack_t *st = data;

	if ((prim != (MGR_ADDSTACK | REQUEST)) && !data)
		return(-EINVAL);
	switch(prim) {
	    case MGR_ADDSTACK | REQUEST:
		if (!(st = new_stack(arg, data))) {
			return(-EINVAL);
		} else {
			hisaxinstance_t *inst = arg;
			
			if (inst)
				inst->st = st;
		}
		return(0);
	    case MGR_SETSTACK | REQUEST:
		return(set_stack(st, arg));
	    case MGR_CLEARSTACK | REQUEST:
		return(clear_stack(st));
	    case MGR_DELSTACK | REQUEST:
		return(release_stack(st));
	    case MGR_ADDLAYER | INDICATION:
		return(register_layer(st, arg));
	    case MGR_ADDLAYER | REQUEST:
		if (!register_layer(st, arg)) {
			hisaxinstance_t *inst = arg;
			return(inst->obj->own_ctrl(st, MGR_ADDLAYER | CONFIRM, arg));
		}
	    case MGR_DELLAYER | REQUEST:
		return(unregister_instance(arg));
	    case MGR_ADDIF | REQUEST:
		return(add_stack_if(st, arg));
	    case MGR_DELIF | REQUEST:
		return(del_stack_if(st, arg));
	    case MGR_LOADFIRM | REQUEST:
	    	if (st->mgr && st->mgr->obj && st->mgr->obj->own_ctrl)
	    		return(st->mgr->obj->own_ctrl(st, prim, arg));
	    	break;
	    case MGR_DEBUGDATA | REQUEST:
	    	return(debugout(data, arg));
	    default:
		printk(KERN_WARNING "manager prim %x not handled\n", prim);
		break;
	}
	return(-EINVAL);
}

void
hisaxlock_core(void) {
#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
}

void
hisaxunlock_core(void) {
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
}

int HiSax_register(hisaxobject_t *obj) {

	if (!obj)
		return(-EINVAL);
	obj->id = obj_id++;
	APPEND_TO_LIST(obj, hisax_objects);
	obj->ctrl = central_manager;
	// register_prop
	if (debug)
	        printk(KERN_DEBUG "HiSax_register %s id %x\n", obj->name,
	        	obj->id);
	return(0);
}

int HiSax_unregister(hisaxobject_t *obj) {
	
	if (!obj)
		return(-EINVAL);
	if (debug)
		printk(KERN_DEBUG "HiSax_unregister %s %d refs\n",
			obj->name, obj->refcnt);
	if (obj->DPROTO.protocol[0])
		release_stacks(obj);
	else
		remove_object(obj);
	REMOVE_FROM_LISTBASE(obj, hisax_objects);
	return(0);
}

int
HiSaxInit(void)
{
	int err;

	core_debug = debug;
	err = init_hisaxdev(debug);
	return(err);
}

#ifdef MODULE
void cleanup_module(void) {
	hisaxstack_t *st;

	free_hisaxdev();
	if (hisax_objects) {
		printk(KERN_WARNING "hisaxcore hisax_objects not empty\n");
	}
	if (hisax_stacklist) {
		printk(KERN_WARNING "hisaxcore hisax_stacklist not empty\n");
		st = hisax_stacklist;
		while (st) {
			printk(KERN_WARNING "hisaxcore st %x in list\n",
				st->id);
			if (st == st->next) {
				printk(KERN_WARNING "hisaxcore st == next\n");
				break;
			}
			st = st->next;
		}
	}
	printk(KERN_DEBUG "hisaxcore unloaded\n");
}
#endif

/* $Id: core.c,v 0.16 2001/04/11 10:21:10 kkeil Exp $
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

hisaxobject_t *
get_object(int id) {
	hisaxobject_t *obj = hisax_objects;

	while(obj) {
		if (obj->id == id)
			return(obj);
		obj = obj->next;
	}
	return(NULL);
}

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

hisaxinstance_t *
get_next_instance(hisaxstack_t *st, hisax_pid_t *pid)
{
	int		err;
	hisaxinstance_t	*next;
	int		layer, proto;
	hisaxobject_t	*obj;

	layer = get_lowlayer(pid->layermask);
	proto = pid->protocol[layer];
	next = get_instance(st, layer, proto);
	if (!next) {
		obj = find_object(proto);
		if (!obj)
			obj = find_object_module(proto);
		if (!obj) {
			printk(KERN_WARNING __FUNCTION__": no object found\n");
			return(NULL);
		}
		err = obj->own_ctrl(st, MGR_NEWLAYER | REQUEST, pid);
		if (err) {
			printk(KERN_WARNING __FUNCTION__": newlayer err(%d)\n",
				err);
			return(NULL);
		}
		next = get_instance(st, layer, proto);
	}
	return(next);
}

static int
disconnect_if(hisaxinstance_t *inst, u_int prim, hisaxif_t *hif) {
	int	err = 0;

	if (hif) {
		hif->stat = IF_NOACTIV;
		hif->func = dummy_if;
		hif->peer = NULL;
		hif->fdata = NULL;
	}
	if (inst)
		err = inst->obj->own_ctrl(inst, prim, hif);
	return(err);
}

static int
add_if(hisaxinstance_t *inst, u_int prim, hisaxif_t *hif) {
	hisaxif_t *myif;

	if (!inst)
		return(-EINVAL);
	if (!hif)
		return(-EINVAL);
	if (hif->stat & IF_UP) {
		myif = &inst->down;
	} else if (hif->stat & IF_DOWN) {
		myif = &inst->up;
	} else
		return(-EINVAL);
	APPEND_TO_LIST(hif, myif);
	inst->obj->own_ctrl(inst, prim, hif);
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

	if ((prim != (MGR_NEWSTACK | REQUEST)) &&
		(prim != (MGR_DISCONNECT | REQUEST)) && !data)
		return(-EINVAL);
	switch(prim) {
	    case MGR_NEWSTACK | REQUEST:
		if (!(st = new_stack(data, arg)))
			return(-EINVAL);
		return(0);
	    case MGR_SETSTACK | REQUEST:
		return(set_stack(st, arg));
	    case MGR_CLEARSTACK | REQUEST:
		return(clear_stack(st));
	    case MGR_DELSTACK | REQUEST:
		return(release_stack(st));
	    case MGR_REGLAYER | INDICATION:
		return(register_layer(st, arg));
	    case MGR_REGLAYER | REQUEST:
		if (!register_layer(st, arg)) {
			hisaxinstance_t *inst = arg;
			return(inst->obj->own_ctrl(arg, MGR_REGLAYER | CONFIRM, NULL));
		}
		return(-EINVAL);
	    case MGR_UNREGLAYER | REQUEST:
		return(unregister_instance(data));
	    case MGR_DISCONNECT | REQUEST:
	    case MGR_DISCONNECT | INDICATION:
		return(disconnect_if(data, prim, arg));
	    case MGR_ADDIF | REQUEST:
		return(add_if(data, prim, arg));
	    case MGR_CONNECT | REQUEST:
		return(ConnectIF(data, arg));
	    case MGR_LOADFIRM | REQUEST:
	    	if (st->mgr && st->mgr->obj && st->mgr->obj->own_ctrl)
	    		return(st->mgr->obj->own_ctrl(st->mgr, prim, arg));
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

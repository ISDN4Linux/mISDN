/* $Id: core.c,v 0.6 2001/02/22 10:14:16 kkeil Exp $
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

static int debug = 0;

#ifdef MODULE
MODULE_AUTHOR("Karsten Keil");
MODULE_PARM(debug, "1i");
EXPORT_SYMBOL(HiSax_register);
EXPORT_SYMBOL(HiSax_unregister);
#define HiSaxInit init_module
#endif

static moditem_t modlist[] = {
	{"hisaxl1", 1, ISDN_PID_L1_TE_S0},
	{"hisaxl2", 2, ISDN_PID_L2_LAPD},
	{"hisaxl2", 2, ISDN_PID_L2_B_X75SLP},
	{NULL, -1, ISDN_PID_NONE}
};

static hisaxobject_t *
find_object(int layer, int protocol) {
	hisaxobject_t *obj = hisax_objects;
	int i, *pp;

	while (obj) {
		if (obj->layer == layer) {
			pp = obj->protocols;
			for (i=0; i<obj->protcnt; i++) {
				if (*pp == protocol)
					return(obj);
				pp++;
			}
		}
		obj = obj->next;
	}
	return(NULL);
}

static hisaxobject_t *
find_object_module(int layer, int protocol) {
	moditem_t *m = modlist;
	hisaxobject_t *obj;

	while (m->name != NULL) {
		if (m->layer == layer) {
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
				if ((obj = find_object(layer, protocol)))
					return(obj);
			}
		}
		m++;
	}
	if (debug)
		printk(KERN_DEBUG "find_object_module: no module l%d prot %x found\n",
			layer, protocol);
	return(NULL);
}

static int
register_instance(hisaxstack_t *st, hisaxinstance_t *inst) {
	int lay;

	if (!st || !inst)
		return(-EINVAL);
	lay = inst->layer;
	if ((lay>MAX_LAYER) || (lay<0))
		return(-EINVAL);
	APPEND_TO_LIST(inst, st->inst[lay]);
	st->protocols[lay] = inst->protocol;
	inst->st = st;
	inst->obj->refcnt++;
	return(0);
}

static void
remove_object(hisaxobject_t *obj) {
	hisaxstack_t *st = hisax_stacklist;
	hisaxinstance_t *inst, *tmp;
	int l;

	while (st) {
		for(l=0;l<=MAX_LAYER;l++) {
			inst = st->inst[l];
			while (inst) {
				if (inst->obj == obj) {
					tmp = inst->next;
					inst->obj->own_ctrl(st, MGR_RELEASE
						| INDICATION, inst);
					inst = tmp;
				} else
					inst = inst->next;
			}
		}
		st = st->next;
	}
}

static int
dummy_if(hisaxif_t *hif, u_int prim, uint nr, int dtyp, void *arg) {
	if (debug & DEBUG_DUMMY_FUNC)
		printk(KERN_DEBUG "hisax dummy_if prim:%x hif:%p nr: %d dtyp %x arg:%p\n",
			prim, hif, nr, dtyp, arg);
	return(-EINVAL);
}

static int
add_stack_if(hisaxstack_t *st, hisaxif_t *hif) {
	int		err, lay;
	hisaxinstance_t	*inst;
	hisaxobject_t	*obj = NULL;

	if (!hif)
		return(-EINVAL);
	lay = hif->layer;
	if (debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "add_stack_if for layer %d proto %x/%x\n",
			lay, hif->protocol, hif->stat);
	if ((lay>MAX_LAYER) || (lay<0))
		return(-EINVAL);
	if (hif->protocol == ISDN_PID_NONE) {
		printk(KERN_WARNING "add_stack_if: for protocol none\n");
		hif->fdata = NULL;
		hif->func = dummy_if;
		hif->stat = IF_NOACTIV;
		return(0);
	}
	inst = st->inst[lay];
	while(inst) {
		if (inst->protocol == hif->protocol)
			obj = inst->obj;
		inst = inst->next;
	}
	if (!obj)
		obj = find_object(lay, hif->protocol);
	if (!obj)
		obj = find_object_module(lay, hif->protocol);
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
	lay = hif->layer;
	if (debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "del_stack_if for layer %d proto %x/%x\n",
			lay, hif->protocol, hif->stat);
	if ((lay>MAX_LAYER) || (lay<0))
		return(-EINVAL);
	inst = st->inst[lay];
	while(inst) {
		if (inst->protocol == hif->protocol) {
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

	if (!data)
		return(-EINVAL);
	switch(prim) {
	    case MGR_ADDSTACK | REQUEST:
	    	if (!(st = create_stack(data, arg)))
	    		return(-EINVAL);
	    	return(0);
	    case MGR_ADDLAYER | INDICATION:
		return(register_instance(st, arg));
	    case MGR_ADDIF | REQUEST:
		return(add_stack_if(st, arg));
	    case MGR_DELIF | REQUEST:
		return(del_stack_if(st, arg));
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
	APPEND_TO_LIST(obj, hisax_objects);
	obj->ctrl = central_manager;
	// register_prop
	if (debug)
	        printk(KERN_DEBUG "HiSax_register %s\n", obj->name);
	return(0);
}

int HiSax_unregister(hisaxobject_t *obj) {
	
	if (!obj)
		return(-EINVAL);
	if (debug)
		printk(KERN_DEBUG "HiSax_unregister %s %d refs\n",
			obj->name, obj->refcnt);
	if (obj->layer == 0)
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

	free_hisaxdev();
	if (hisax_objects) {
		printk(KERN_WARNING "hisaxcore hisax_objects not empty\n");
	}
	if (hisax_stacklist) {
		printk(KERN_WARNING "hisaxcore hisax_stacklist not empty\n");
	}
	printk(KERN_DEBUG "hisaxcore unloaded\n");
}
#endif

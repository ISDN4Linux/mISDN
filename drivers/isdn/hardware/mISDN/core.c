/* $Id: core.c,v 1.6 2003/06/30 11:23:33 kkeil Exp $
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
#ifdef CONFIG_SMP
#include <linux/smp_lock.h>
#endif

hisaxobject_t	*hisax_objects = NULL;
int core_debug;

static int debug;
static int obj_id;

#ifdef MODULE
MODULE_AUTHOR("Karsten Keil");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
MODULE_PARM(debug, "1i");
EXPORT_SYMBOL(HiSax_register);
EXPORT_SYMBOL(HiSax_unregister);
#define HiSaxInit init_module
#endif

typedef struct _hisax_thread {
	/* thread */
	struct task_struct	*thread;
	wait_queue_head_t	waitq;
	struct semaphore	*notify;
	u_int			Flags;
	struct sk_buff_head	workq;
} hisax_thread_t;

#define	HISAX_TFLAGS_STARTED	1
#define HISAX_TFLAGS_RMMOD	2
#define HISAX_TFLAGS_ACTIV	3
#define HISAX_TFLAGS_TEST	4

static hisax_thread_t	hisax_thread;

static moditem_t modlist[] = {
	{"hisaxl1", ISDN_PID_L1_TE_S0},
	{"hisaxl2", ISDN_PID_L2_LAPD},
	{"hisaxl2", ISDN_PID_L2_B_X75SLP},
	{"l3udss1", ISDN_PID_L3_DSS1USER},
	{"hisaxdtmf", ISDN_PID_L2_B_TRANSDTMF},
	{NULL, ISDN_PID_NONE}
};

/* 
 * kernel thread to do work which cannot be done
 *in interrupt context 
 */

static int
hisaxd(void *data)
{
	hisax_thread_t	*hkt = data;

#ifdef CONFIG_SMP
	lock_kernel();
#endif
	daemonize();
	sigfillset(&current->blocked);
	strcpy(current->comm,"hisaxd");
	hkt->thread = current;
#ifdef CONFIG_SMP
	unlock_kernel();
#endif
	printk(KERN_DEBUG "hisaxd: daemon started\n");

	test_and_set_bit(HISAX_TFLAGS_STARTED, &hkt->Flags);

	for (;;) {
		int		err;
		struct sk_buff	*skb;
		hisax_headext_t	*hhe;

		if (test_and_clear_bit(HISAX_TFLAGS_RMMOD, &hkt->Flags))
			break;
		if (hkt->notify != NULL)
			up(hkt->notify);
		interruptible_sleep_on(&hkt->waitq);
		if (test_and_clear_bit(HISAX_TFLAGS_RMMOD, &hkt->Flags))
			break;
		while ((skb = skb_dequeue(&hkt->workq))) {
			test_and_set_bit(HISAX_TFLAGS_ACTIV, &hkt->Flags);
			err = -EINVAL;
			hhe=HISAX_HEADEXT_P(skb);
			switch (hhe->what) {
				case MGR_FUNCTION:
					err=hhe->func.ctrl(hhe->data[0], hhe->prim, skb->data);
					if (err) {
						printk(KERN_WARNING "hisaxd: what(%x) prim(%x) failed err(%x)\n",
							hhe->what, hhe->prim, err);
					} else {
						printk(KERN_DEBUG "hisaxd: what(%x) prim(%x) success\n",
							hhe->what, hhe->prim);
						err--; /* to free skb */
					}
					break;
				default:
					int_error();
					printk(KERN_WARNING "hisaxd: what(%x) prim(%x) unknown\n",
						hhe->what, hhe->prim);
					break;
			}
			if (err)
				kfree_skb(skb);
			test_and_clear_bit(HISAX_TFLAGS_ACTIV, &hkt->Flags);
		}
		if (test_and_clear_bit(HISAX_TFLAGS_TEST, &hkt->Flags))
			printk(KERN_DEBUG "hisaxd: test event done\n");
	}
	
	printk(KERN_DEBUG "hisaxd: daemon exit now\n");
	test_and_clear_bit(HISAX_TFLAGS_STARTED, &hkt->Flags);
	test_and_clear_bit(HISAX_TFLAGS_ACTIV, &hkt->Flags);
	discard_queue(&hkt->workq);
	hkt->thread = NULL;
	if (hkt->notify != NULL)
		up(hkt->notify);
	return(0);
}

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
	int		err;
	moditem_t	*m = modlist;
	hisaxobject_t	*obj;

	while (m->name != NULL) {
		if (m->protocol == protocol) {
#ifdef CONFIG_KMOD
			if (debug)
				printk(KERN_DEBUG
					"find_object_module %s - trying to load in_irq(%d)\n",
					m->name, in_interrupt());
			err=request_module(m->name);
			if (debug)
				printk(KERN_DEBUG "find_object_module: request_module(%s) returns(%d)\n",
					m->name, err);
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
		printk(KERN_DEBUG "%s: no module for protocol %x found\n",
			__FUNCTION__, protocol);
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
dummy_if(hisaxif_t *hif, struct sk_buff *skb)
{
	if (!skb) {
		printk(KERN_WARNING "%s: hif(%p) without skb\n",
			__FUNCTION__, hif);
		return(-EINVAL);
	}
	if (debug & DEBUG_DUMMY_FUNC)
		printk(KERN_DEBUG "%s: hif(%p) skb(%p) len(%d) prim(%x)\n",
			__FUNCTION__, hif, skb, skb->len, *((u_int *)skb->data));
	dev_kfree_skb_any(skb);
	return(0);
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
			printk(KERN_WARNING "%s: no object found\n",
				__FUNCTION__);
			return(NULL);
		}
		err = obj->own_ctrl(st, MGR_NEWLAYER | REQUEST, pid);
		if (err) {
			printk(KERN_WARNING "%s: newlayer err(%d)\n",
				__FUNCTION__, err);
			return(NULL);
		}
		next = get_instance(st, layer, proto);
	}
	return(next);
}

static int
sel_channel(hisaxstack_t *st, channel_info_t *ci)
{
	int		err = -EINVAL;

	if (!ci)
		return(err);
	printk(KERN_DEBUG "%s: st(%p) st->mgr(%p)\n",
		__FUNCTION__, st, st->mgr);
	if (st->mgr) {
		if (st->mgr->obj && st->mgr->obj->own_ctrl) {
			err = st->mgr->obj->own_ctrl(st->mgr,
				MGR_SELCHANNEL | REQUEST, ci);
			printk(KERN_DEBUG "%s: MGR_SELCHANNEL(%d)\n",
				__FUNCTION__, err);
		} else
			int_error();
	} else {
		printk(KERN_WARNING "%s: no mgr st(%p)\n", __FUNCTION__, st);
	}
	if (err) {
		hisaxstack_t	*cst = st->child;
		int		nr = 0;

		ci->st.p = NULL;
		if (!(ci->channel & (~CHANNEL_NUMBER))) {
			/* only number is set */
			while(cst) {
				nr++;
				if (nr == (ci->channel & 3)) {
					ci->st.p = cst;
					err = 0;
					break;
				}
				cst = cst->next;
			}
		}
	}
	return(err);
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

static int
get_hdevice(hisaxdevice_t **dev, int *typ)
{
	if (!dev)
		return(-EINVAL);
	if (!typ)
		return(-EINVAL);
	if (*typ == HISAX_RAW_DEVICE) {
		*dev = get_free_rawdevice();
		if (!(*dev))
			return(-ENODEV);
		return(0);
	}
	return(-EINVAL);
}

static int central_manager(void *data, u_int prim, void *arg) {
	hisaxstack_t *st = data;

	switch(prim) {
	    case MGR_NEWSTACK | REQUEST:
		if (!(st = new_stack(data, arg)))
			return(-EINVAL);
		return(0);
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
	    case MGR_GETDEVICE | REQUEST:
	    	return(get_hdevice(data, arg));
	    case MGR_DELDEVICE | REQUEST:
	    	return(free_device(data));
	}
	if (!data)
		return(-EINVAL);
	switch(prim) {
	    case MGR_SETSTACK | REQUEST:
	    	/* can sleep in case of module reload */
	    	if (in_interrupt()) {
			struct sk_buff	*skb;
			hisax_headext_t	*hhe;

			skb = alloc_skb(sizeof(hisax_pid_t), GFP_ATOMIC);
			hhe = HISAX_HEADEXT_P(skb);
			hhe->prim = prim;
			hhe->what = MGR_FUNCTION;
			hhe->data[0] = st;
			/* FIXME: handling of optional pid parameters */
			memcpy(skb_put(skb, sizeof(hisax_pid_t)), arg, sizeof(hisax_pid_t));
			hhe->func.ctrl = central_manager;
			skb_queue_tail(&hisax_thread.workq, skb);
	    		wake_up_interruptible(&hisax_thread.waitq);
	    		return(0);
	    	} else
			return(set_stack(st, arg));
		break;
	    case MGR_CLEARSTACK | REQUEST:
		return(clear_stack(st));
	    case MGR_DELSTACK | REQUEST:
		return(release_stack(st));
	    case MGR_SELCHANNEL | REQUEST:
		return(sel_channel(st, arg));
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
	DECLARE_MUTEX_LOCKED(sem);
	int err;

	core_debug = debug;
	err = init_hisaxdev(debug);
	if (err)
		return(err);
	init_waitqueue_head(&hisax_thread.waitq);
	skb_queue_head_init(&hisax_thread.workq);
	hisax_thread.notify = &sem;
	kernel_thread(hisaxd, (void *)&hisax_thread, 0);
	down(&sem);
	hisax_thread.notify = NULL;
	test_and_set_bit(HISAX_TFLAGS_TEST, &hisax_thread.Flags);
	wake_up_interruptible(&hisax_thread.waitq);
	return(err);
}

#ifdef MODULE
void cleanup_module(void) {
	DECLARE_MUTEX_LOCKED(sem);
	hisaxstack_t *st;

	if (hisax_thread.thread) {
		/* abort hisaxd kernel thread */
		hisax_thread.notify = &sem;
		test_and_set_bit(HISAX_TFLAGS_RMMOD, &hisax_thread.Flags);
		wake_up_interruptible(&hisax_thread.waitq);
		down(&sem);
		hisax_thread.notify = NULL;
	}
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

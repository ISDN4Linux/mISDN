/* $Id: core.c,v 1.23 2004/06/30 15:13:18 keil Exp $
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
#include <linux/spinlock.h>
#include "core.h"
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif
#ifdef CONFIG_SMP
#include <linux/smp_lock.h>
#endif

static char		*mISDN_core_revision = "$Revision: 1.23 $";

LIST_HEAD(mISDN_objectlist);
rwlock_t		mISDN_objects_lock = RW_LOCK_UNLOCKED;

int core_debug;

static u_char		entityarray[MISDN_MAX_ENTITY/8];
static spinlock_t	entity_lock = SPIN_LOCK_UNLOCKED;

static int debug;
static int obj_id;

#ifdef MODULE
MODULE_AUTHOR("Karsten Keil");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
MODULE_PARM(debug, "1i");
#endif

typedef struct _mISDN_thread {
	/* thread */
	struct task_struct	*thread;
	wait_queue_head_t	waitq;
	struct semaphore	*notify;
	u_long			Flags;
	struct sk_buff_head	workq;
} mISDN_thread_t;

#define	mISDN_TFLAGS_STARTED	1
#define mISDN_TFLAGS_RMMOD	2
#define mISDN_TFLAGS_ACTIV	3
#define mISDN_TFLAGS_TEST	4

static mISDN_thread_t	mISDN_thread;

static moditem_t modlist[] = {
	{"mISDN_l1", ISDN_PID_L1_TE_S0},
	{"mISDN_l2", ISDN_PID_L2_LAPD},
	{"mISDN_l2", ISDN_PID_L2_B_X75SLP},
	{"l3udss1", ISDN_PID_L3_DSS1USER},
	{"mISDN_dtmf", ISDN_PID_L2_B_TRANSDTMF},
	{NULL, ISDN_PID_NONE}
};

/* 
 * kernel thread to do work which cannot be done
 *in interrupt context 
 */

static int
mISDNd(void *data)
{
	mISDN_thread_t	*hkt = data;

#ifdef CONFIG_SMP
	lock_kernel();
#endif
	MAKEDAEMON("mISDNd");
	sigfillset(&current->blocked);
	hkt->thread = current;
#ifdef CONFIG_SMP
	unlock_kernel();
#endif
	printk(KERN_DEBUG "mISDNd: kernel daemon started\n");

	test_and_set_bit(mISDN_TFLAGS_STARTED, &hkt->Flags);

	for (;;) {
		int		err;
		struct sk_buff	*skb;
		mISDN_headext_t	*hhe;

		if (test_and_clear_bit(mISDN_TFLAGS_RMMOD, &hkt->Flags))
			break;
		if (hkt->notify != NULL)
			up(hkt->notify);
		interruptible_sleep_on(&hkt->waitq);
		if (test_and_clear_bit(mISDN_TFLAGS_RMMOD, &hkt->Flags))
			break;
		while ((skb = skb_dequeue(&hkt->workq))) {
			test_and_set_bit(mISDN_TFLAGS_ACTIV, &hkt->Flags);
			err = -EINVAL;
			hhe=mISDN_HEADEXT_P(skb);
			switch (hhe->addr) {
				case MGR_FUNCTION:
					err=hhe->func.ctrl(hhe->data[0], hhe->prim, skb->data);
					if (err) {
						printk(KERN_WARNING "mISDNd: addr(%x) prim(%x) failed err(%x)\n",
							hhe->addr, hhe->prim, err);
					} else {
						if (debug)
							printk(KERN_DEBUG "mISDNd: addr(%x) prim(%x) success\n",
								hhe->addr, hhe->prim);
						err--; /* to free skb */
					}
					break;
				case MGR_QUEUEIF:
					err = hhe->func.iff(hhe->data[0], skb);
					if (err) {
						printk(KERN_WARNING "mISDNd: addr(%x) prim(%x) failed err(%x)\n",
							hhe->addr, hhe->prim, err);
					}
					break;
				default:
					int_error();
					printk(KERN_WARNING "mISDNd: addr(%x) prim(%x) unknown\n",
						hhe->addr, hhe->prim);
					err = -EINVAL;
					break;
			}
			if (err)
				kfree_skb(skb);
			test_and_clear_bit(mISDN_TFLAGS_ACTIV, &hkt->Flags);
		}
		if (test_and_clear_bit(mISDN_TFLAGS_TEST, &hkt->Flags))
			printk(KERN_DEBUG "mISDNd: test event done\n");
	}
	
	printk(KERN_DEBUG "mISDNd: daemon exit now\n");
	test_and_clear_bit(mISDN_TFLAGS_STARTED, &hkt->Flags);
	test_and_clear_bit(mISDN_TFLAGS_ACTIV, &hkt->Flags);
	discard_queue(&hkt->workq);
	hkt->thread = NULL;
	if (hkt->notify != NULL)
		up(hkt->notify);
	return(0);
}

mISDNobject_t *
get_object(int id) {
	mISDNobject_t	*obj;

	read_lock(&mISDN_objects_lock);
	list_for_each_entry(obj, &mISDN_objectlist, list)
		if (obj->id == id) {
			read_unlock(&mISDN_objects_lock);
			return(obj);
		}
	read_unlock(&mISDN_objects_lock);
	return(NULL);
}

static mISDNobject_t *
find_object(int protocol) {
	mISDNobject_t	*obj;
	int		err;

	read_lock(&mISDN_objects_lock);
	list_for_each_entry(obj, &mISDN_objectlist, list) {
		err = obj->own_ctrl(NULL, MGR_HASPROTOCOL | REQUEST, &protocol);
		if (!err)
			goto unlock;
		if (err != -ENOPROTOOPT) {
			if (0 == mISDN_HasProtocol(obj, protocol))
				goto unlock;
		}	
	}
	obj = NULL;
unlock:
	read_unlock(&mISDN_objects_lock);
	return(obj);
}

static mISDNobject_t *
find_object_module(int protocol) {
#ifdef CONFIG_KMOD
	int		err;
#endif
	moditem_t	*m = modlist;
	mISDNobject_t	*obj;

	while (m->name != NULL) {
		if (m->protocol == protocol) {
#ifdef CONFIG_KMOD
			if (debug)
				printk(KERN_DEBUG
					"find_object_module %s - trying to load\n",
					m->name);
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
remove_object(mISDNobject_t *obj) {
	mISDNstack_t *st, *nst;
	mISDNlayer_t *layer, *nl;
	mISDNinstance_t *inst;

	list_for_each_entry_safe(st, nst, &mISDN_stacklist, list) {
		list_for_each_entry_safe(layer, nl, &st->layerlist, list) {
			inst = layer->inst;
			if (inst && inst->obj == obj)
				inst->obj->own_ctrl(st, MGR_RELEASE | INDICATION, inst);
		}
	}
}

static int
dummy_if(mISDNif_t *hif, struct sk_buff *skb)
{
	if (!skb) {
		printk(KERN_WARNING "%s: hif(%p) without skb\n",
			__FUNCTION__, hif);
		return(-EINVAL);
	}
	if (debug & DEBUG_DUMMY_FUNC) {
		mISDN_head_t	*hh = mISDN_HEAD_P(skb);

		printk(KERN_DEBUG "%s: hif(%p) skb(%p) len(%d) prim(%x)\n",
			__FUNCTION__, hif, skb, skb->len, hh->prim);
	}
	dev_kfree_skb_any(skb);
	return(0);
}

mISDNinstance_t *
get_next_instance(mISDNstack_t *st, mISDN_pid_t *pid)
{
	int		err;
	mISDNinstance_t	*next;
	int		layer, proto;
	mISDNobject_t	*obj;

	layer = mISDN_get_lowlayer(pid->layermask);
	proto = pid->protocol[layer];
	next = get_instance(st, layer, proto);
	if (!next) {
		obj = find_object(proto);
		if (!obj)
			obj = find_object_module(proto);
		if (!obj) {
			if (debug)
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
sel_channel(mISDNstack_t *st, channel_info_t *ci)
{
	int	err = -EINVAL;

	if (!ci)
		return(err);
	if (debug)
		printk(KERN_DEBUG "%s: st(%p) st->mgr(%p)\n",
			__FUNCTION__, st, st->mgr);
	if (st->mgr) {
		if (st->mgr->obj && st->mgr->obj->own_ctrl) {
			err = st->mgr->obj->own_ctrl(st->mgr, MGR_SELCHANNEL | REQUEST, ci);
			if (debug)
				printk(KERN_DEBUG "%s: MGR_SELCHANNEL(%d)\n", __FUNCTION__, err);
		} else
			int_error();
	} else {
		printk(KERN_WARNING "%s: no mgr st(%p)\n", __FUNCTION__, st);
	}
	if (err) {
		mISDNstack_t	*cst;
		u_int		nr = 0;

		ci->st.p = NULL;
		if (!(ci->channel & (~CHANNEL_NUMBER))) {
			/* only number is set */
			struct list_head	*head;
			if (list_empty(&st->childlist)) {
				if ((st->id & FLG_CLONE_STACK) &&
					(st->childlist.prev != &st->childlist)) {
					head = st->childlist.prev;
				} else {
					printk(KERN_WARNING "%s: invalid empty childlist (no clone) stid(%x) childlist(%p<-%p->%p)\n",
						__FUNCTION__, st->id, st->childlist.prev, &st->childlist, st->childlist.next);
					return(err);
				}
			} else
				head = &st->childlist;
			list_for_each_entry(cst, head, list) {
				nr++;
				if (nr == (ci->channel & 3)) {
					ci->st.p = cst;
					return(0);
				}
			}
		}
	}
	return(err);
}

static int
disconnect_if(mISDNinstance_t *inst, u_int prim, mISDNif_t *hif) {
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
add_if(mISDNinstance_t *inst, u_int prim, mISDNif_t *hif) {
	mISDNif_t *myif;

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
	while(myif->clone)
		myif = myif->clone;
	myif->clone = hif;
	hif->predecessor = myif;
	inst->obj->own_ctrl(inst, prim, hif);
	return(0);
}

static char tmpbuf[4096];
static int
debugout(mISDNinstance_t *inst, logdata_t *log)
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
get_hdevice(mISDNdevice_t **dev, int *typ)
{
	if (!dev)
		return(-EINVAL);
	if (!typ)
		return(-EINVAL);
	if (*typ == mISDN_RAW_DEVICE) {
		*dev = get_free_rawdevice();
		if (!(*dev))
			return(-ENODEV);
		return(0);
	}
	return(-EINVAL);
}

static int
mgr_queue(void *data, u_int prim, struct sk_buff *skb)
{
	mISDN_headext_t *hhe = mISDN_HEADEXT_P(skb);

	hhe->addr = prim;
	skb_queue_tail(&mISDN_thread.workq, skb);
	wake_up_interruptible(&mISDN_thread.waitq);
	return(0);
}

static int central_manager(void *, u_int, void *);

static int
set_stack_req(mISDNstack_t *st, mISDN_pid_t *pid)
{
	struct sk_buff	*skb;
	mISDN_headext_t	*hhe;
	mISDN_pid_t	*npid;
	u_char		*pbuf = NULL;

	skb = alloc_skb(sizeof(mISDN_pid_t) + pid->maxplen, GFP_ATOMIC);
	hhe = mISDN_HEADEXT_P(skb);
	hhe->prim = MGR_SETSTACK_NW | REQUEST;
	hhe->addr = MGR_FUNCTION;
	hhe->data[0] = st;
	npid = (mISDN_pid_t *)skb_put(skb, sizeof(mISDN_pid_t));
	if (pid->pbuf)
		pbuf = skb_put(skb, pid->maxplen);
	copy_pid(npid, pid, pbuf);
	hhe->func.ctrl = central_manager;
	skb_queue_tail(&mISDN_thread.workq, skb);
	wake_up_interruptible(&mISDN_thread.waitq);
	return(0);
}

int
mISDN_alloc_entity(int *entity)
{
	u_long	flags;

	spin_lock_irqsave(&entity_lock, flags);
	*entity = 1;
	while(*entity < MISDN_MAX_ENTITY) {
		if (!test_and_set_bit(*entity, (u_long *)&entityarray[0]))
			break;
		(*entity)++;
	}
	spin_unlock_irqrestore(&entity_lock, flags);
	if (*entity == MISDN_MAX_ENTITY)
		return(-EBUSY);
	return(0);
}

int
mISDN_delete_entity(int entity)
{
	u_long	flags;
	int	ret = 0;

	spin_lock_irqsave(&entity_lock, flags);
	if (!test_and_clear_bit(entity, (u_long *)&entityarray[0])) {
		printk(KERN_WARNING "mISDN: del_entity(%d) but entity not allocated\n", entity);
		ret = -ENODEV;
	}
	spin_unlock_irqrestore(&entity_lock, flags);
	return(ret);
}

static int
new_entity(mISDNinstance_t *inst)
{
	int	entity;
	int	ret;

	if (!inst)
		return(-EINVAL);
	ret = mISDN_alloc_entity(&entity);
	if (ret) {
		printk(KERN_WARNING "mISDN: no more entity available(max %d)\n", MISDN_MAX_ENTITY);
		return(ret);
	}
	ret = inst->obj->own_ctrl(inst, MGR_NEWENTITY | CONFIRM, (void *)entity);
	if (ret)
		mISDN_delete_entity(entity);
	return(ret);
}

static int central_manager(void *data, u_int prim, void *arg) {
	mISDNstack_t *st = data;

	switch(prim) {
	    case MGR_NEWSTACK | REQUEST:
		if (!(st = new_stack(data, arg)))
			return(-EINVAL);
		return(0);
	    case MGR_NEWENTITY | REQUEST:
		return(new_entity(data));
	    case MGR_DELENTITY | REQUEST:
	    case MGR_DELENTITY | INDICATION:
	    	return(mISDN_delete_entity((int)arg));
	    case MGR_REGLAYER | INDICATION:
		return(register_layer(st, arg));
	    case MGR_REGLAYER | REQUEST:
		if (!register_layer(st, arg)) {
			mISDNinstance_t *inst = arg;
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
	    case MGR_QUEUEIF | REQUEST:
	    	return(mgr_queue(data, MGR_QUEUEIF, arg));
	}
	if (!data)
		return(-EINVAL);
	switch(prim) {
	    case MGR_SETSTACK | REQUEST:
		/* can sleep in case of module reload */
		return(set_stack_req(st, arg));
	    case MGR_SETSTACK_NW | REQUEST:
		return(set_stack(st, arg));
	    case MGR_CLEARSTACK | REQUEST:
		return(clear_stack(st));
	    case MGR_DELSTACK | REQUEST:
		return(release_stack(st));
	    case MGR_SELCHANNEL | REQUEST:
		return(sel_channel(st, arg));
	    case MGR_ADDIF | REQUEST:
		return(add_if(data, prim, arg));
	    case MGR_CTRLREADY | INDICATION:
		return(do_for_all_layers(st, prim, arg));
	    case MGR_ADDSTPARA | REQUEST:
	    case MGR_CLRSTPARA | REQUEST:
		return(change_stack_para(st, prim, arg));
	    case MGR_CONNECT | REQUEST:
		return(mISDN_ConnectIF(data, arg));
	    case MGR_EVALSTACK  | REQUEST:
	    	return(evaluate_stack_pids(data, arg));
	    case MGR_GLOBALOPT | REQUEST:
	    case MGR_LOADFIRM | REQUEST:
	    	if (st->mgr && st->mgr->obj && st->mgr->obj->own_ctrl)
	    		return(st->mgr->obj->own_ctrl(st->mgr, prim, arg));
	    	break;
	    case MGR_DEBUGDATA | REQUEST:
	    	return(debugout(data, arg));
	    default:
	    	if (debug)
			printk(KERN_WARNING "manager prim %x not handled\n", prim);
		break;
	}
	return(-EINVAL);
}

int mISDN_register(mISDNobject_t *obj) {
	u_long	flags;

	if (!obj)
		return(-EINVAL);
	write_lock_irqsave(&mISDN_objects_lock, flags);
	obj->id = obj_id++;
	list_add_tail(&obj->list, &mISDN_objectlist);
	write_unlock_irqrestore(&mISDN_objects_lock, flags);
	obj->ctrl = central_manager;
	// register_prop
	if (debug)
		printk(KERN_DEBUG "mISDN_register %s id %x\n", obj->name,
			obj->id);
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "mISDN_register: obj(%p)\n", obj);
	return(0);
}

int mISDN_unregister(mISDNobject_t *obj) {
	u_long	flags;

	if (!obj)
		return(-EINVAL);
	if (debug)
		printk(KERN_DEBUG "mISDN_unregister %s %d refs\n",
			obj->name, obj->refcnt);
	if (obj->DPROTO.protocol[0])
		release_stacks(obj);
	else
		remove_object(obj);
	write_lock_irqsave(&mISDN_objects_lock, flags);
	list_del(&obj->list);
	write_unlock_irqrestore(&mISDN_objects_lock, flags);
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "mISDN_unregister: mISDN_objectlist(%p<-%p->%p)\n",
			mISDN_objectlist.prev, &mISDN_objectlist, mISDN_objectlist.next);
	return(0);
}

int
mISDNInit(void)
{
	DECLARE_MUTEX_LOCKED(sem);
	int err;

	printk(KERN_INFO "Modular ISDN Stack core %s\n", mISDN_core_revision);
	core_debug = debug;
#ifdef MISDN_MEMDEBUG
	err = __mid_init();
	if (err)
		return(err);
#endif
	err = init_mISDNdev(debug);
	if (err)
		return(err);
	init_waitqueue_head(&mISDN_thread.waitq);
	skb_queue_head_init(&mISDN_thread.workq);
	mISDN_thread.notify = &sem;
	kernel_thread(mISDNd, (void *)&mISDN_thread, 0);
	down(&sem);
	mISDN_thread.notify = NULL;
	test_and_set_bit(mISDN_TFLAGS_TEST, &mISDN_thread.Flags);
	wake_up_interruptible(&mISDN_thread.waitq);
	return(err);
}

void mISDN_cleanup(void) {
	DECLARE_MUTEX_LOCKED(sem);
	mISDNstack_t *st, *nst;

	free_mISDNdev();
	if (!list_empty(&mISDN_objectlist)) {
		printk(KERN_WARNING "mISDNcore mISDN_objects not empty\n");
	}
	if (!list_empty(&mISDN_stacklist)) {
		printk(KERN_WARNING "mISDNcore mISDN_stacklist not empty\n");
		list_for_each_entry_safe(st, nst, &mISDN_stacklist, list) {
			printk(KERN_WARNING "mISDNcore st %x still in list\n",
				st->id);
			if (list_empty(&st->list)) {
				printk(KERN_WARNING "mISDNcore st == next\n");
				break;
			}
		}
	}
	if (mISDN_thread.thread) {
		/* abort mISDNd kernel thread */
		mISDN_thread.notify = &sem;
		test_and_set_bit(mISDN_TFLAGS_RMMOD, &mISDN_thread.Flags);
		wake_up_interruptible(&mISDN_thread.waitq);
		down(&sem);
		mISDN_thread.notify = NULL;
	}
#ifdef MISDN_MEMDEBUG
	__mid_cleanup();
#endif
	printk(KERN_DEBUG "mISDNcore unloaded\n");
}

module_init(mISDNInit);
module_exit(mISDN_cleanup);

EXPORT_SYMBOL(mISDN_register);
EXPORT_SYMBOL(mISDN_unregister);

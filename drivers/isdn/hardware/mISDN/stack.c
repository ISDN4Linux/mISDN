/* $Id: stack.c,v 1.19 2006/08/07 23:35:59 keil Exp $
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 * This file is released under the GPLv2
 *
 */

#include "core.h"

static LIST_HEAD(mISDN_stacklist);
static rwlock_t	stacklist_lock = RW_LOCK_UNLOCKED;
static LIST_HEAD(mISDN_instlist);
static rwlock_t	instlist_lock = RW_LOCK_UNLOCKED;

int
get_stack_cnt(void)
{
	int cnt = 0;
	mISDNstack_t *st;

	read_lock(&stacklist_lock);
	list_for_each_entry(st, &mISDN_stacklist, list)
		cnt++;
	read_unlock(&stacklist_lock);
	return(cnt);
}

void
get_stack_info(struct sk_buff *skb)
{
	mISDN_head_t	*hp;
	mISDNstack_t	*cst, *st;
	stack_info_t	*si;
	int		i;

	hp = mISDN_HEAD_P(skb);
	if (hp->addr == 0) {
		hp->dinfo = get_stack_cnt();
		hp->len = 0;
		return;
	} else if (hp->addr <= 127 && hp->addr <= get_stack_cnt()) {
		/* stack nr */
		i = 1;
		read_lock(&stacklist_lock);
		list_for_each_entry(st, &mISDN_stacklist, list) {
			if (i == hp->addr)
				break;
			i++;
		}
		read_unlock(&stacklist_lock);
	} else
		st = get_stack4id(hp->addr);
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s: addr(%08x) st(%p) id(%08x)\n", __FUNCTION__, hp->addr, st,
			st ? st->id : 0);
	if (!st) {
		hp->len = -ENODEV;
		return;
	} else {
		si = (stack_info_t *)skb->data;
		memset(si, 0, sizeof(stack_info_t));
		si->id = st->id;
		si->extentions = st->extentions;
		if (st->mgr)
			si->mgr = st->mgr->id;
		else
			si->mgr = 0;
		memcpy(&si->pid, &st->pid, sizeof(mISDN_pid_t));
		memcpy(&si->para, &st->para, sizeof(mISDN_stPara_t));
		if (st->clone)
			si->clone = st->clone->id;
		else
			si->clone = 0;
		if (st->master)
			si->master = st->master->id;
		else
			si->master = 0;
		si->instcnt = 0;
		for (i = 0; i <= MAX_LAYER_NR; i++) {
			if (st->i_array[i]) {
				si->inst[si->instcnt] = st->i_array[i]->id;
				si->instcnt++;
			}
		}
		si->childcnt = 0;
		list_for_each_entry(cst, &st->childlist, list) {
			si->child[si->childcnt] = cst->id;
			si->childcnt++;
		}
		hp->len = sizeof(stack_info_t);
		if (si->childcnt>2)
			hp->len += (si->childcnt-2)*sizeof(int);
	}
	skb_put(skb, hp->len);
}

static int
get_free_stackid(mISDNstack_t *mst, int flag)
{
	u_int		id = 0, found;
	mISDNstack_t	*st;

	if (!mst) {
		while(id < STACK_ID_MAX) {
			found = 0;
			id += STACK_ID_INC;
			read_lock(&stacklist_lock);
			list_for_each_entry(st, &mISDN_stacklist, list) {
				if (st->id == id) {
					found++;
					break;
				}
			}
			read_unlock(&stacklist_lock);
			if (!found)
				return(id);
		}
	} else if (flag & FLG_CLONE_STACK) {
		id = mst->id | FLG_CLONE_STACK;
		while(id < CLONE_ID_MAX) {
			found = 0;
			id += CLONE_ID_INC;
			st = mst->clone;
			while (st) {
				if (st->id == id) {
					found++;
					break;
				}
				st = st->clone;
			}
			if (!found)
				return(id);
		}
	} else if (flag & FLG_CHILD_STACK) {
		id = mst->id | FLG_CHILD_STACK;
		while(id < CHILD_ID_MAX) {
			id += CHILD_ID_INC;
			found = 0;
			list_for_each_entry(st, &mst->childlist, list) {
				if (st->id == id) {
					found++;
					break;
				}
			}
			if (!found)
				return(id);
		}
	}
	return(0);
}

mISDNstack_t *
get_stack4id(u_int id)
{
	mISDNstack_t *cst, *st;

	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "get_stack4id(%x)\n", id);
	if (!id) /* 0 isn't a valid id */
		return(NULL);
	read_lock(&stacklist_lock);
	list_for_each_entry(st, &mISDN_stacklist, list) {
		if (id == st->id) {
			read_unlock(&stacklist_lock);
			return(st);
		}
		if ((id & FLG_CHILD_STACK) && ((id & MASTER_ID_MASK) == st->id)) {
			list_for_each_entry(cst, &st->childlist, list) {
				if (cst->id == id) {
					read_unlock(&stacklist_lock);
					return(cst);
				}
			}
		}
		if ((id & FLG_CLONE_STACK) && ((id & MASTER_ID_MASK) == st->id)) {
			cst = st->clone;
			while (cst) {
				if (cst->id == id) {
					read_unlock(&stacklist_lock);
					return(cst);
				}
				cst = cst->clone;
			}
		}
	}
	read_unlock(&stacklist_lock);
	return(NULL);
}

mISDNinstance_t *
getlayer4lay(mISDNstack_t *st, int layermask)
{
	int	i;

	if (!st) {
		int_error();
		return(NULL);
	}
	for (i = 0; i <= MAX_LAYER_NR; i++) {
		if (st->i_array[i] && (st->i_array[i]->pid.layermask & layermask))
			return(st->i_array[i]);
	}
	return(NULL);
}

static mISDNinstance_t *
get_nextlayer(mISDNstack_t *st, u_int addr)
{
	mISDNinstance_t	*inst=NULL;
	int		layer = addr & LAYER_ID_MASK;

	if (!(addr & FLG_MSG_TARGET)) {
		switch(addr & MSG_DIR_MASK) {
			case FLG_MSG_DOWN:
				if (addr & FLG_MSG_CLONED) {
					/* OK */
				} else
					layer -= LAYER_ID_INC;
				break;
			case FLG_MSG_UP:
				if (addr & FLG_MSG_CLONED) {
					/* OK */
				} else
					layer += LAYER_ID_INC;
				break;
			case MSG_TO_OWNER:
				break;
			default: /* broadcast */
				int_errtxt("st(%08x) addr(%08x) wrong address", st->id, addr);
				return(NULL);
		}
	}
	if ((layer < 0) || (layer > MAX_LAYER_NR)) {
		int_errtxt("st(%08x) addr(%08x) layer %d out of range", st->id, addr, layer);
		return(NULL);
	}
	inst = st->i_array[layer];
	if (core_debug & DEBUG_QUEUE_FUNC)
		printk(KERN_DEBUG "%s: st(%08x) addr(%08x) -> inst(%08x)\n",
			__FUNCTION__, st->id, addr, inst ? inst->id : 0);
	return(inst);
}

mISDNinstance_t *
get_instance(mISDNstack_t *st, int layer_nr, int protocol)
{
	mISDNinstance_t	*inst=NULL;
	int		i;

	if (!st) {
		int_error();
		return(NULL);
	}
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "get_instance st(%08x) lnr(%d) prot(%x)\n",
			st->id, layer_nr, protocol);
	if ((layer_nr < 0) || (layer_nr > MAX_LAYER_NR)) {
		int_errtxt("lnr %d", layer_nr);
		return(NULL);
	}
	list_for_each_entry(inst, &st->prereg, list) {
		if (core_debug & DEBUG_CORE_FUNC)
			printk(KERN_DEBUG "get_instance prereg(%p, %x) lm %x/%x prot %x/%x\n",
				inst, inst->id, inst->pid.layermask, ISDN_LAYER(layer_nr),
				inst->pid.protocol[layer_nr], protocol);
		if ((inst->pid.layermask & ISDN_LAYER(layer_nr)) &&
			(inst->pid.protocol[layer_nr] == protocol)) {
			i = register_layer(st, inst);
			if (i) {
				int_errtxt("error(%d) register preregistered inst(%08x) on st(%08x)", i, inst->id, st->id);
				return(NULL);
			}
			return(inst);
		}
	}
	for (i = 0; i <= MAX_LAYER_NR; i++) {
		if ((inst = st->i_array[i])) {
			if (core_debug & DEBUG_CORE_FUNC)
				printk(KERN_DEBUG "get_instance inst%d(%p, %x) lm %x/%x prot %x/%x\n",
					i,inst, inst->id, inst->pid.layermask, ISDN_LAYER(layer_nr),
					inst->pid.protocol[layer_nr], protocol);
			if ((inst->pid.layermask & ISDN_LAYER(layer_nr)) &&
				(inst->pid.protocol[layer_nr] == protocol))
				return(inst);
		}
	}
	return(NULL);
}

mISDNinstance_t *
get_instance4id(u_int id)
{
	mISDNinstance_t *inst;

	read_lock(&instlist_lock);
	list_for_each_entry(inst, &mISDN_instlist, list)
		if (inst->id == id) {
			read_unlock(&instlist_lock);
			return(inst);
		}
	read_unlock(&instlist_lock);
	return(NULL);
}

#ifdef OBSOLETE
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
	if (list_empty(&st->layerlist)) {
		list_add(&layer->list, &st->layerlist);
	} else {
		list_for_each_entry(item, &st->layerlist, list) {
			if (layermask < get_layermask(item)) {
				list_add_tail(&layer->list, &item->list);
				return(0);
			}
		}
		list_add_tail(&layer->list, &st->layerlist);
	}
	return(0);
}
#endif

inline void
_queue_message(mISDNstack_t *st, struct sk_buff *skb)
{
	skb_queue_tail(&st->msgq, skb);
	if (likely(!test_bit(mISDN_STACK_STOPPED, &st->status))) {
		test_and_set_bit(mISDN_STACK_WORK, &st->status);
		wake_up_interruptible(&st->workq);
	}
}

int
mISDN_queue_message(mISDNinstance_t *inst, u_int aflag, struct sk_buff *skb)
{
	mISDN_head_t	*hh = mISDN_HEAD_P(skb);
	mISDNstack_t	*st = inst->st;
	u_int		id;

	if (core_debug & DEBUG_QUEUE_FUNC)
		printk(KERN_DEBUG "%s(%08x, %x, prim(%x))\n", __FUNCTION__,
			inst->id, aflag, hh->prim);
	if (aflag & FLG_MSG_TARGET) {
		id = aflag;
	} else {
		id = (inst->id & INST_ID_MASK) | aflag;
	}
	if ((aflag & MSG_DIR_MASK) == FLG_MSG_DOWN) {
		if (inst->parent) {
			inst = inst->parent;
			st = inst->st;
			id = (inst->id & INST_ID_MASK) | FLG_MSG_TARGET | FLG_MSG_CLONED | FLG_MSG_DOWN;
		}
	}
	if (!st)
		return(-EINVAL);
	if (st->id == 0 || test_bit(mISDN_STACK_ABORT, &st->status))
		return(-EBUSY);
	if (inst->id == 0) { /* instance is not initialised */
		if (!(aflag & FLG_MSG_TARGET)) {
			id &= INST_ID_MASK;
			id |= (st->id & INST_ID_MASK) | aflag | FLG_INSTANCE;
		}
	}
	if (test_bit(mISDN_STACK_KILLED, &st->status))
		return(-EBUSY);
	if ((st->id & STACK_ID_MASK) != (id & STACK_ID_MASK)) {
		int_errtxt("stack id not match st(%08x) id(%08x) inst(%08x) aflag(%08x) prim(%x)",
			st->id, id, inst->id, aflag, hh->prim);
	}
	hh->addr = id;
	_queue_message(st, skb);
	return(0);
}

static void
do_broadcast(mISDNstack_t *st, struct sk_buff *skb)
{
	mISDN_head_t	*hh = mISDN_HEAD_P(skb);
	mISDNinstance_t	*inst = NULL;
	struct sk_buff	*c_skb = NULL;
	int i, err;

	for(i=0; i<=MAX_LAYER_NR; i++) {
		if (i == (hh->addr & LAYER_ID_MASK))
			continue; // skip own layer
		inst = st->i_array[i];
		if (!inst)
			continue;  // maybe we have a gap
		if (!c_skb)
			c_skb = skb_copy(skb, GFP_KERNEL);  // we need a new private copy
		if (!c_skb)
			break;  // stop here when copy not possible

		if (core_debug & DEBUG_MSG_THREAD_INFO)
			printk(KERN_DEBUG "%s: inst(%08x) msg call addr(%08x) prim(%x)\n",
				__FUNCTION__, inst->id, hh->addr, hh->prim);

		if (inst->function) {
			err = inst->function(inst, c_skb);
			if (!err)
				c_skb = NULL; /* function consumed the skb */
			if (core_debug & DEBUG_MSG_THREAD_INFO)
				printk(KERN_DEBUG "%s: inst(%08x) msg call return %d\n",
					__FUNCTION__, inst->id, err);

		} else {
			if (core_debug & DEBUG_MSG_THREAD_ERR)
				printk(KERN_DEBUG "%s: instance(%08x) no function\n",
					__FUNCTION__, inst->id);
		}
	}
	if (c_skb)
		dev_kfree_skb(c_skb);
	dev_kfree_skb(skb);
}

static void
release_layers(mISDNstack_t *st, u_int prim)
{
	int	i;

	for (i = 0; i <= MAX_LAYER_NR; i++) {
		if (st->i_array[i]) {
			if (core_debug & DEBUG_CORE_FUNC)
				printk(KERN_DEBUG  "%s: st(%p) inst%d(%p):%x %s lm(%x)\n",
					__FUNCTION__, st, i, st->i_array[i], st->i_array[i]->id,
					st->i_array[i]->name, st->i_array[i]->pid.layermask);
			st->i_array[i]->obj->own_ctrl(st->i_array[i], prim, NULL);
		}
	}
}

static void
do_clear_stack(mISDNstack_t *st) {

	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s: st(%08x)\n", __FUNCTION__, st->id);
	kfree(st->pid.pbuf);
	memset(&st->pid, 0, sizeof(mISDN_pid_t));
	memset(&st->para, 0, sizeof(mISDN_stPara_t));
	release_layers(st, MGR_UNREGLAYER | REQUEST);
}

static int
mISDNStackd(void *data)
{
	mISDNstack_t	*st = data;
	int		err = 0;

#ifdef CONFIG_SMP
	lock_kernel();
#endif
	MAKEDAEMON("mISDNStackd");
	sigfillset(&current->blocked);
	st->thread = current;
#ifdef CONFIG_SMP
	unlock_kernel();
#endif
//	if ( core_debug & DEBUG_THREADS)
	printk(KERN_DEBUG "mISDNStackd started for id(%08x)\n", st->id);

	for (;;) {
		struct sk_buff	*skb, *c_skb;
		mISDN_head_t	*hh;
		
		if (unlikely(test_bit(mISDN_STACK_STOPPED, &st->status))) {
			test_and_clear_bit(mISDN_STACK_WORK, &st->status);
			test_and_clear_bit(mISDN_STACK_RUNNING, &st->status);
		} else
			test_and_set_bit(mISDN_STACK_RUNNING, &st->status);
		while (test_bit(mISDN_STACK_WORK, &st->status)) {
			mISDNinstance_t	*inst;

			skb = skb_dequeue(&st->msgq);
			if (!skb) {
				test_and_clear_bit(mISDN_STACK_WORK, &st->status);
				/* test if a race happens */
				if (!(skb = skb_dequeue(&st->msgq)))
					continue;
				test_and_set_bit(mISDN_STACK_WORK, &st->status);
			}
#ifdef MISDN_MSG_STATS
			st->msg_cnt++;
#endif
			hh = mISDN_HEAD_P(skb);
			if (hh->prim == (MGR_CLEARSTACK | REQUEST)) {
				mISDN_headext_t	*hhe = (mISDN_headext_t *)hh;

				if (test_and_set_bit(mISDN_STACK_CLEARING, &st->status)) {
					int_errtxt("double clearing");
				}
				if (hhe->data[0]) {
					if (st->notify) {
						int_errtxt("notify already set");
						up(st->notify);
					}
					st->notify = hhe->data[0];
				}
				dev_kfree_skb(skb);
				continue;
			}
			if ((hh->addr & MSG_DIR_MASK) == MSG_BROADCAST) {
				do_broadcast(st, skb);
				continue;
			}
			inst = get_nextlayer(st, hh->addr);
			if (!inst) {
				if (core_debug & DEBUG_MSG_THREAD_ERR)
					printk(KERN_DEBUG "%s: st(%08x) no instance for addr(%08x) prim(%x) dinfo(%x)\n",
						__FUNCTION__, st->id, hh->addr, hh->prim, hh->dinfo);
				dev_kfree_skb(skb);
				continue;
			}
			if (inst->clone && ((hh->addr & MSG_DIR_MASK) == FLG_MSG_UP)) {
				u_int	id = (inst->clone->id & INST_ID_MASK) | FLG_MSG_TARGET | FLG_MSG_CLONED | FLG_MSG_UP;

#ifdef MISDN_MSG_STATS
				st->clone_cnt++;
#endif
				c_skb = skb_copy(skb, GFP_KERNEL);
				if (c_skb) {
					if (core_debug & DEBUG_MSG_THREAD_INFO)
						printk(KERN_DEBUG "%s: inst(%08x) msg clone msg to(%08x) caddr(%08x) prim(%x)\n",
							__FUNCTION__, inst->id, inst->clone->id, id, hh->prim);
					err = mISDN_queue_message(inst->clone, id, c_skb);
					if (err) {
						if (core_debug & DEBUG_MSG_THREAD_ERR)
							printk(KERN_DEBUG "%s: clone instance(%08x) cannot queue msg(%08x) err(%d)\n",
								__FUNCTION__, inst->clone->id, id, err);
						dev_kfree_skb(c_skb);
					}
				} else {
					printk(KERN_WARNING "%s OOM on msg cloning inst(%08x) caddr(%08x) prim(%x) len(%d)\n",
						__FUNCTION__, inst->id, id, hh->prim, skb->len);
				}
			}
			if (core_debug & DEBUG_MSG_THREAD_INFO)
				printk(KERN_DEBUG "%s: inst(%08x) msg call addr(%08x) prim(%x)\n",
					__FUNCTION__, inst->id, hh->addr, hh->prim);
			if (!inst->function) {
				if (core_debug & DEBUG_MSG_THREAD_ERR)
					printk(KERN_DEBUG "%s: instance(%08x) no function\n",
						__FUNCTION__, inst->id);
				dev_kfree_skb(skb);
				continue;
			}
			err = inst->function(inst, skb);
			if (err) {
				if (core_debug & DEBUG_MSG_THREAD_ERR)
					printk(KERN_DEBUG "%s: instance(%08x)->function return(%d)\n",
						__FUNCTION__, inst->id, err);
				dev_kfree_skb(skb);
				continue;
			}
			if (unlikely(test_bit(mISDN_STACK_STOPPED, &st->status))) {
				test_and_clear_bit(mISDN_STACK_WORK, &st->status);
				test_and_clear_bit(mISDN_STACK_RUNNING, &st->status);
				break;
			}
		}
		if (test_bit(mISDN_STACK_CLEARING, &st->status)) {
			test_and_set_bit(mISDN_STACK_STOPPED, &st->status);
			test_and_clear_bit(mISDN_STACK_RUNNING, &st->status);
			do_clear_stack(st);
			test_and_clear_bit(mISDN_STACK_CLEARING, &st->status);
			test_and_set_bit(mISDN_STACK_RESTART, &st->status);
		}
		if (test_and_clear_bit(mISDN_STACK_RESTART, &st->status)) {
			test_and_clear_bit(mISDN_STACK_STOPPED, &st->status);
			test_and_set_bit(mISDN_STACK_RUNNING, &st->status);
			if (!skb_queue_empty(&st->msgq))
				test_and_set_bit(mISDN_STACK_WORK, &st->status);
		}
		if (test_bit(mISDN_STACK_ABORT, &st->status))
			break;
		if (st->notify != NULL) {
			up(st->notify);
			st->notify = NULL;
		}
#ifdef MISDN_MSG_STATS
		st->sleep_cnt++;
#endif
		test_and_clear_bit(mISDN_STACK_ACTIVE, &st->status);
		wait_event_interruptible(st->workq, (st->status & mISDN_STACK_ACTION_MASK));
		if (core_debug & DEBUG_MSG_THREAD_INFO)
			printk(KERN_DEBUG "%s: %08x wake status %08lx\n", __FUNCTION__, st->id, st->status);
		test_and_set_bit(mISDN_STACK_ACTIVE, &st->status);

		test_and_clear_bit(mISDN_STACK_WAKEUP, &st->status);

		if (test_bit(mISDN_STACK_STOPPED, &st->status)) {
			test_and_clear_bit(mISDN_STACK_RUNNING, &st->status);
#ifdef MISDN_MSG_STATS
			st->stopped_cnt++;
#endif
		}
	}
#ifdef MISDN_MSG_STATS
	printk(KERN_DEBUG "mISDNStackd daemon for id(%08x) proceed %d msg %d clone %d sleep %d stopped\n",
		st->id, st->msg_cnt, st->clone_cnt, st->sleep_cnt, st->stopped_cnt);
	printk(KERN_DEBUG "mISDNStackd daemon for id(%08x) utime(%ld) stime(%ld)\n", st->id, st->thread->utime, st->thread->stime);
	printk(KERN_DEBUG "mISDNStackd daemon for id(%08x) nvcsw(%ld) nivcsw(%ld)\n", st->id, st->thread->nvcsw, st->thread->nivcsw);
#endif
	printk(KERN_DEBUG "mISDNStackd daemon for id(%08x) killed now\n", st->id);
	test_and_set_bit(mISDN_STACK_KILLED, &st->status);
	test_and_clear_bit(mISDN_STACK_RUNNING, &st->status);
	test_and_clear_bit(mISDN_STACK_ACTIVE, &st->status);
	test_and_clear_bit(mISDN_STACK_ABORT, &st->status);
	discard_queue(&st->msgq);
	st->thread = NULL;
	if (st->notify != NULL) {
		up(st->notify);
		st->notify = NULL;
	}
	return(0);
}

int
mISDN_start_stack_thread(mISDNstack_t *st)
{
	int	err = 0;

	if (st->thread == NULL && test_bit(mISDN_STACK_KILLED, &st->status)) {
		test_and_clear_bit(mISDN_STACK_KILLED, &st->status);
		kernel_thread(mISDNStackd, (void *)st, 0);
	} else
		err = -EBUSY;
	return(err);
}

mISDNstack_t *
new_stack(mISDNstack_t *master, mISDNinstance_t *inst)
{
	mISDNstack_t	*newst;
	int		err;
	u_long		flags;

	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "create %s stack inst(%p)\n",
			master ? "child" : "master", inst);
	if (!(newst = kmalloc(sizeof(mISDNstack_t), GFP_ATOMIC))) {
		printk(KERN_ERR "kmalloc mISDN_stack failed\n");
		return(NULL);
	}
	memset(newst, 0, sizeof(mISDNstack_t));
	INIT_LIST_HEAD(&newst->list);
	INIT_LIST_HEAD(&newst->childlist);
	INIT_LIST_HEAD(&newst->prereg);
	init_waitqueue_head(&newst->workq);
	skb_queue_head_init(&newst->msgq);
	if (!master) {
		if (inst && inst->st) {
			master = inst->st;
			while(master->clone)
				master = master->clone;
			newst->id = get_free_stackid(inst->st, FLG_CLONE_STACK);
			newst->master = master;
			master->clone = newst;
			master = NULL;
		} else {
			newst->id = get_free_stackid(NULL, 0);
		}
	} else {
		newst->id = get_free_stackid(master, FLG_CHILD_STACK);
	}
	newst->mgr = inst;
	if (master) {
		list_add_tail(&newst->list, &master->childlist);
		newst->parent = master;
	} else if (!(newst->id & FLG_CLONE_STACK)) {
		write_lock_irqsave(&stacklist_lock, flags);
		list_add_tail(&newst->list, &mISDN_stacklist);
		write_unlock_irqrestore(&stacklist_lock, flags);
	}
	if (inst) {
		inst->st = newst;
	}
	err = mISDN_register_sysfs_stack(newst);
	if (err) {
		// FIXME error handling
		printk(KERN_ERR "Stack id %x not registered in sysfs\n", newst->id);
	}
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "Stack id %x added\n", newst->id);
	kernel_thread(mISDNStackd, (void *)newst, 0);
	return(newst);
}

int
mISDN_start_stop(mISDNstack_t *st, int start)
{
	int	ret;

	if (start) {
		ret = test_and_clear_bit(mISDN_STACK_STOPPED, &st->status);
		test_and_set_bit(mISDN_STACK_WAKEUP, &st->status);
		if (!skb_queue_empty(&st->msgq))
			test_and_set_bit(mISDN_STACK_WORK, &st->status);
		wake_up_interruptible(&st->workq);
	} else
		ret = test_and_set_bit(mISDN_STACK_STOPPED, &st->status);
	return(ret);
}

int
do_for_all_layers(void *data, u_int prim, void *arg)
{
	mISDNstack_t	*st = data;
	int		i;

	if (!st) {
		int_error();
		return(-EINVAL);
	}
	for (i = 0; i <= MAX_LAYER_NR; i++) {
		if (st->i_array[i]) {
			if (core_debug & DEBUG_CORE_FUNC)
				printk(KERN_DEBUG  "%s: st(%p) inst%d(%p):%x %s prim(%x) arg(%p)\n",
					__FUNCTION__, st, i, st->i_array[i], st->i_array[i]->id,
					st->i_array[i]->name, prim, arg);
			st->i_array[i]->obj->own_ctrl(st->i_array[i], prim, arg);
		}
	}
	return(0);
}

int
change_stack_para(mISDNstack_t *st, u_int prim, mISDN_stPara_t *stpara)
{
	int	changed = 0;
	if (!st) {
		int_error();
		return(-EINVAL);
	}
	if (prim == (MGR_ADDSTPARA | REQUEST)) {
		if (!stpara) {
			int_error();
			return(-EINVAL);
		}
		prim = MGR_ADDSTPARA | INDICATION;
		if (stpara->maxdatalen > 0 && stpara->maxdatalen < st->para.maxdatalen) {
			changed++;
			st->para.maxdatalen = stpara->maxdatalen;
		}
		if (stpara->up_headerlen > st->para.up_headerlen) {
			changed++;
			st->para.up_headerlen = stpara->up_headerlen;
		}
		if (stpara->down_headerlen > st->para.down_headerlen) {
			changed++;
			st->para.down_headerlen = stpara->down_headerlen;
		}
		if (!changed)
			return(0);
		stpara = &st->para;
	} else if (prim == (MGR_CLRSTPARA | REQUEST)) {
		prim = MGR_CLRSTPARA | INDICATION;
		memset(&st->para, 0, sizeof(mISDN_stPara_t));
		stpara = NULL;
	}
	return(do_for_all_layers(st, prim, stpara));
}

static int
delete_stack(mISDNstack_t *st)
{
	DECLARE_MUTEX_LOCKED(sem);
	u_long	flags;

	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s: st(%p:%08x)\n", __FUNCTION__, st, st->id);
	mISDN_unregister_sysfs_st(st);
	if (st->parent)
		st->parent = NULL;
	if (!list_empty(&st->prereg)) {
		mISDNinstance_t	*inst, *ni;

		int_errtxt("st(%08x)->prereg not empty\n", st->id);
		list_for_each_entry_safe(inst, ni, &st->prereg, list) {
			int_errtxt("inst(%p:%08x) preregistered", inst, inst->id);
			list_del(&inst->list);
		}
	}
	if (st->thread) {
		if (st->thread != current) {
			if (st->notify) {
				int_error();
				up(st->notify);
			}
			st->notify = &sem;
		}
		test_and_set_bit(mISDN_STACK_ABORT, &st->status);
		mISDN_start_stop(st, 1);
		if (st->thread != current) /* we cannot wait for us */
			down(&sem);
	}
	release_layers(st, MGR_RELEASE | INDICATION);
	write_lock_irqsave(&stacklist_lock, flags);
	list_del(&st->list);
	write_unlock_irqrestore(&stacklist_lock, flags);
	kfree(st);
	return(0);
}

int
release_stack(mISDNstack_t *st) {
	int err;
	mISDNstack_t *cst, *nst;

	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s: st(%p)\n", __FUNCTION__, st);

	list_for_each_entry_safe(cst, nst, &st->childlist, list) {
		if ((err = delete_stack(cst))) {
			return(err);
		}
	}
	if (st->clone) {
		st->clone->master = st->master;
	}
	if (st->master) {
		st->master->clone = st->clone;
	} else if (st->clone) { /* no master left -> delete clone too */
		delete_stack(st->clone);
		st->clone = NULL;
	}
	if ((err = delete_stack(st)))
		return(err);

	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s: mISDN_stacklist(%p<-%p->%p)\n", __FUNCTION__,
			mISDN_stacklist.prev, &mISDN_stacklist, mISDN_stacklist.next);
	return(0);
}

void
cleanup_object(mISDNobject_t *obj)
{
	mISDNstack_t	*st, *nst;
	mISDNinstance_t	*inst;
	int		i;

	read_lock(&stacklist_lock);
	list_for_each_entry_safe(st, nst, &mISDN_stacklist, list) {
		for (i = 0; i < MAX_LAYER_NR; i++) {
			inst = st->i_array[i];
			if (inst && inst->obj == obj) {
				read_unlock(&stacklist_lock);
				inst->obj->own_ctrl(st, MGR_RELEASE | INDICATION, inst);
				read_lock(&stacklist_lock);
			}
		}
	}
	read_unlock(&stacklist_lock);
}

void
check_stacklist(void)
{
	mISDNstack_t	*st, *nst;

	read_lock(&stacklist_lock);
	if (!list_empty(&mISDN_stacklist)) {
		printk(KERN_WARNING "mISDNcore mISDN_stacklist not empty\n");
		list_for_each_entry_safe(st, nst, &mISDN_stacklist, list) {
			printk(KERN_WARNING "mISDNcore st %x still in list\n", st->id);
			if (list_empty(&st->list)) {
				printk(KERN_WARNING "mISDNcore st == next\n");
				break;
			}
		}
	}
	read_unlock(&stacklist_lock);
}

void
release_stacks(mISDNobject_t *obj)
{
	mISDNstack_t	*st, *tmp;
	int		rel, i;

	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s: obj(%p) %s\n", __FUNCTION__, obj, obj->name);
	read_lock(&stacklist_lock);
	list_for_each_entry_safe(st, tmp, &mISDN_stacklist, list) {
		rel = 0;
		if (core_debug & DEBUG_CORE_FUNC)
			printk(KERN_DEBUG "%s: st(%p)\n", __FUNCTION__, st);
		for (i = 0; i <= MAX_LAYER_NR; i++) {
			if (!st->i_array[i])
				continue;
			if (core_debug & DEBUG_CORE_FUNC)
				printk(KERN_DEBUG "%s: inst%d(%p)\n", __FUNCTION__, i, st->i_array[i]);
			if (st->i_array[i]->obj == obj)
				rel++;
		}
		if (rel) {
			read_unlock(&stacklist_lock);
			release_stack(st);
			read_lock(&stacklist_lock);
		}
	}
	read_unlock(&stacklist_lock);
	if (obj->refcnt)
		printk(KERN_WARNING "release_stacks obj %s refcnt is %d\n",
			obj->name, obj->refcnt);
}

#ifdef OBSOLETE
static void
get_free_instid(mISDNstack_t *st, mISDNinstance_t *inst) {
	mISDNinstance_t *il;

	inst->id = mISDN_get_lowlayer(inst->pid.layermask)<<20;
	inst->id |= FLG_INSTANCE;
	if (st) {
		inst->id |= st->id;
	} else {
		list_for_each_entry(il, &mISDN_instlist, list) {
			if (il->id == inst->id) {
				if ((inst->id & IF_INSTMASK) >= INST_ID_MAX) {
					inst->id = 0;
					return;
				}
				inst->id += LAYER_ID_INC;
				il = list_entry(mISDN_instlist.next, mISDNinstance_t, list);
			}
		}
	}
}
#endif

int
register_layer(mISDNstack_t *st, mISDNinstance_t *inst)
{
	int		idx, err;
	mISDNinstance_t	*dup;
	u_long		flags;

	if (!inst || !st)
		return(-EINVAL);
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s:st(%p) inst(%p/%p) lmask(%x) id(%x)\n",
			__FUNCTION__, st, inst, inst->obj,
			inst->pid.layermask, inst->id);
	if (inst->id) { /* already registered */
		// if (inst->st || !st) {
			int_errtxt("register duplicate %08x %p %p",
				inst->id, inst->st, st);
			return(-EBUSY);
		//}
	}
	/*
	 * To simplify registration we assume that our stacks are
	 * always build with monoton increasing layernumbers from
	 * bottom (HW,L0) to highest number
	 */
//	if (st) {
		for (idx = 0; idx <= MAX_LAYER_NR; idx++)
			if (!st->i_array[idx])
				break;
		if (idx > MAX_LAYER_NR) {
			int_errtxt("stack %08x overflow", st->id);
			return(-EXFULL);
		}
		inst->regcnt++;
		st->i_array[idx] = inst;
		inst->id = st->id | FLG_INSTANCE | idx;
		dup = get_instance4id(inst->id);
		if (dup) {
			int_errtxt("register duplicate %08x i1(%p) i2(%p) i1->st(%p) i2->st(%p) st(%p)",
				inst->id, inst, dup, inst->st, dup->st, st);
			inst->regcnt--;
			st->i_array[idx] = NULL;
			inst->id = 0;
			return(-EBUSY);
		}
//	}

	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s: inst(%p/%p) id(%x)\n", __FUNCTION__,
			inst, inst->obj, inst->id);

	if (!list_empty(&inst->list)) {
		if (core_debug & DEBUG_CORE_FUNC)
			printk(KERN_DEBUG "%s: register preregistered instance st(%p/%p)",
				__FUNCTION__, st, inst->st);
		list_del_init(&inst->list);
	}
	inst->st = st;
	write_lock_irqsave(&instlist_lock, flags);
	list_add_tail(&inst->list, &mISDN_instlist);
	write_unlock_irqrestore(&instlist_lock, flags);
	err = mISDN_register_sysfs_inst(inst);
	if (err) {
		// FIXME error handling
		printk(KERN_ERR "%s: register_sysfs failed %d st(%08x) inst(%08x)\n",
			__FUNCTION__, err, st->id, inst->id);
	}
	return(0);
}

int
preregister_layer(mISDNstack_t *st, mISDNinstance_t *inst)
{
	if (!inst || !st)
		return(-EINVAL);
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s:st(%08x) inst(%p:%08x) lmask(%x)\n",
			__FUNCTION__, st->id, inst, inst->id, inst->pid.layermask);
	if (inst->id) {
		/* already registered */
		int_errtxt("register duplicate %08x %p %p",
			inst->id, inst->st, st);
		return(-EBUSY);
	}
	inst->st = st;
	list_add_tail(&inst->list, &st->prereg);
	return(0);
}

int
unregister_instance(mISDNinstance_t *inst) {
	int	i;
	u_long	flags;

	if (!inst)
		return(-EINVAL);
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s: st(%p) inst(%p):%x lay(%x)\n",
			__FUNCTION__, inst->st, inst, inst->id, inst->pid.layermask);
	mISDN_unregister_sysfs_inst(inst);
	if (inst->st && inst->id) {
		i = inst->id & LAYER_ID_MASK;
		if (i > MAX_LAYER_NR) {
			int_errtxt("unregister %08x  st(%08x) wrong layer", inst->id, inst->st->id);
			return(-EINVAL);
		}
		if (inst->st->i_array[i] == inst) {
			inst->regcnt--;
			inst->st->i_array[i] = NULL;
		} else if (inst->st->i_array[i]) {
			int_errtxt("unregister %08x  st(%08x) wrong instance %08x",
				inst->id, inst->st->id, inst->st->i_array[i]->id);
			return(-EINVAL);
		} else
			printk(KERN_WARNING "unregister %08x  st(%08x) not in stack",
				inst->id, inst->st->id);
		if (inst->st && (inst->st->mgr != inst))
			inst->st = NULL;
	}
	if (inst->parent) { /*we are cloned */
		inst->parent->clone = inst->clone;
		if (inst->clone)
			inst->clone->parent = inst->parent;
		inst->clone = NULL;
		inst->parent = NULL;
	} else if (inst->clone) {
		/* deleting the top level master of a clone */
		/* FIXME: should be handled somehow, maybe unregister the clone */
		int_errtxt("removed master(%08x) of clone(%08x)", inst->id, inst->clone->id);
		inst->clone->parent = NULL;
		inst->clone = NULL;
	}
	write_lock_irqsave(&instlist_lock, flags);
	if (inst->list.prev && inst->list.next)
		list_del_init(&inst->list);
	else
		int_errtxt("uninitialized list inst(%08x)", inst->id);
	inst->id = 0;
	write_unlock_irqrestore(&instlist_lock, flags);
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s: mISDN_instlist(%p<-%p->%p)\n", __FUNCTION__,
			mISDN_instlist.prev, &mISDN_instlist, mISDN_instlist.next);
	return(0);
}

int
copy_pid(mISDN_pid_t *dpid, mISDN_pid_t *spid, u_char *pbuf)
{
	memcpy(dpid, spid, sizeof(mISDN_pid_t));
	if (spid->pbuf && spid->maxplen) {
		if (!pbuf) {
			int_error();
			return(-ENOMEM);
		}
		dpid->pbuf = pbuf;
		memcpy(dpid->pbuf, spid->pbuf, spid->maxplen);
	}
	return(0);
}

int
set_stack(mISDNstack_t *st, mISDN_pid_t *pid)
{
	int 		err, i;
	u_char		*pbuf = NULL;
	mISDNinstance_t	*inst;
//	mISDNlayer_t	*hl, *hln;

	if (!st || !pid) {
		int_error();
		return(-EINVAL);
	}
	if (!st->mgr || !st->mgr->obj) {
		int_error();
		return(-EINVAL);
	}
	if (pid->pbuf)
		pbuf = kmalloc(pid->maxplen, GFP_ATOMIC);
	err = copy_pid(&st->pid, pid, pbuf);
	if (err)
		return(err);
	memcpy(&st->mgr->pid, &st->pid, sizeof(mISDN_pid_t));
	if (!mISDN_SetHandledPID(st->mgr->obj, &st->mgr->pid)) {
		int_error();
		return(-ENOPROTOOPT);
	} else {
		mISDN_RemoveUsedPID(pid, &st->mgr->pid);
	}
	err = mISDN_ctrl(st, MGR_REGLAYER | REQUEST, st->mgr);
	if (err) {
		int_error();
		return(err);
	}
	while (pid->layermask) {
		inst = get_next_instance(st, pid);
		if (!inst) {
			int_error();
			mISDN_ctrl(st, MGR_CLEARSTACK| REQUEST, (void *)1);
			return(-ENOPROTOOPT);
		}
		mISDN_RemoveUsedPID(pid, &inst->pid);
	}
	if (!list_empty(&st->prereg))
		int_errtxt("st(%08x)->prereg not empty\n", st->id);

	for (i = 0; i <= MAX_LAYER_NR; i++) {
		inst = st->i_array[i];
		if (!inst)
			break;
		if (!inst->obj) {
			int_error();
			continue;
		}
		if (!inst->obj->own_ctrl) {
			int_error();
			continue;
		}
		inst->obj->own_ctrl(inst, MGR_SETSTACK | INDICATION, NULL);
	}
	return(0);
}

int
clear_stack(mISDNstack_t *st, int wait) {
	struct sk_buff	*skb;
	mISDN_headext_t	*hhe;

	if (!st)
		return(-EINVAL);
	if (core_debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s: st(%08x)\n", __FUNCTION__, st->id);

	if (!(skb = alloc_skb(8, GFP_ATOMIC)))
		return(-ENOMEM);
	hhe = mISDN_HEADEXT_P(skb);
	hhe->prim = MGR_CLEARSTACK | REQUEST;
	hhe->addr = st->id;

	if (wait) {
		DECLARE_MUTEX_LOCKED(sem);

		hhe->data[0] = &sem;
		_queue_message(st, skb);
		if (st->thread == current) {/* we cannot wait for us */
			int_error();
			return(-EBUSY);
		}
		down(&sem);
	} else {
		hhe->data[0] = NULL;
		_queue_message(st, skb);
	}
	return(0);
}

static int
test_stack_protocol(mISDNstack_t *st, u_int l1prot, u_int l2prot, u_int l3prot)
{
	int		cnt = MAX_LAYER_NR + 1, ret = 1;
	mISDN_pid_t	pid;
	mISDNinstance_t	*inst;

	clear_stack(st,1);
	memset(&pid, 0, sizeof(mISDN_pid_t));
	pid.layermask = ISDN_LAYER(1);
	if (!(((l2prot == 2) || (l2prot == 0x40)) && (l3prot == 1)))
		pid.layermask |= ISDN_LAYER(2);
	if (!(l3prot == 1))
		pid.layermask |= ISDN_LAYER(3);

	pid.protocol[1] = l1prot | ISDN_PID_LAYER(1) | ISDN_PID_BCHANNEL_BIT;
	if (pid.layermask & ISDN_LAYER(2))
		pid.protocol[2] = l2prot | ISDN_PID_LAYER(2) | ISDN_PID_BCHANNEL_BIT;
	if (pid.layermask & ISDN_LAYER(3))
		pid.protocol[3] = l3prot | ISDN_PID_LAYER(3) | ISDN_PID_BCHANNEL_BIT;
	copy_pid(&st->pid, &pid, NULL);
	memcpy(&st->mgr->pid, &pid, sizeof(mISDN_pid_t));
	if (!mISDN_SetHandledPID(st->mgr->obj, &st->mgr->pid)) {
		memset(&st->pid, 0, sizeof(mISDN_pid_t));
		return(-ENOPROTOOPT);
	} else {
		mISDN_RemoveUsedPID(&pid, &st->mgr->pid);
	}
	if (!pid.layermask) {
		memset(&st->pid, 0, sizeof(mISDN_pid_t));
		return(0);
	}
	ret = mISDN_ctrl(st, MGR_REGLAYER | REQUEST, st->mgr);
	if (ret) {
		clear_stack(st, 1);
		return(ret);
	}
	while (pid.layermask && cnt--) {
		inst = get_next_instance(st, &pid);
		if (!inst) {
			ret = -ENOPROTOOPT;
			break;
		}
		mISDN_RemoveUsedPID(&pid, &inst->pid);
	}
	if (!cnt)
		ret = -ENOPROTOOPT;
	clear_stack(st, 1);
	return(ret);
}

static u_int	validL1pid4L2[ISDN_PID_IDX_MAX + 1] = {
			0x022d,
			0x03ff,
			0x0000,
			0x0000,
			0x0010,
			0x022d,
			0x03ff,
			0x0380,
			0x022d,
			0x022d,
			0x022d,
			0x01c6,
			0x0000,
};

static u_int	validL2pid4L3[ISDN_PID_IDX_MAX + 1] = {
			0x1fff,
			0x0000,
			0x0101,
			0x0101,
			0x0010,
			0x0010,
			0x0000,
			0x00c0,
			0x0000,
};

int
evaluate_stack_pids(mISDNstack_t *st, mISDN_pid_t *pid)
{
	int 		err;
	mISDN_pid_t	pidmask;
	u_int		l1bitm, l2bitm, l3bitm;
	u_int		l1idx, l2idx, l3idx;

	if (!st || !pid) {
		int_error();
		return(-EINVAL);
	}
	if (!st->mgr || !st->mgr->obj) {
		int_error();
		return(-EINVAL);
	}
	copy_pid(&pidmask, pid, NULL);
	memset(pid, 0, sizeof(mISDN_pid_t));
	for (l1idx=0; l1idx <= ISDN_PID_IDX_MAX; l1idx++) {
		l1bitm = 1 << l1idx;
		if (!(pidmask.protocol[1] & l1bitm))
			continue;
		for (l2idx=0; l2idx <= ISDN_PID_IDX_MAX; l2idx++) {
			l2bitm = 1 << l2idx;
			if (!(pidmask.protocol[2] & l2bitm))
				continue;
			if (!(validL1pid4L2[l2idx] & l1bitm))
				continue;
			for (l3idx=0; l3idx <= ISDN_PID_IDX_MAX; l3idx++) {
				err = 1;
				l3bitm = 1 << l3idx;
				if (!(pidmask.protocol[3] & l3bitm))
					continue;
				if (!(validL2pid4L3[l3idx] & l2bitm))
					continue;
				err = test_stack_protocol(st, l1bitm, l2bitm, l3bitm);
				if (!err) {
					pid->protocol[3] |= l3bitm;
					pid->protocol[2] |= l2bitm;
					pid->protocol[1] |= l1bitm;
				}
			}
		}
	}
	clear_stack(st, 1);
	return(0);
}

EXPORT_SYMBOL(mISDN_queue_message);

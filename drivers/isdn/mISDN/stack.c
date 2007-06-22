/* $Id: stack.c,v 2.0 2007/06/10 12:51:33 kkeil Exp $
 *
 * Author	Karsten Keil <kkeil@novell.com>
 *
 * Copyright 2007  by Karsten Keil <kkeil@novell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/mISDNif.h>
#include "core.h"

static u_int	*debug;

int
mISDN_queue_message(struct mISDNstack *st, struct sk_buff *skb)
{
	skb_queue_tail(&st->msgq, skb);
	if (likely(!test_bit(mISDN_STACK_STOPPED, &st->status))) {
		test_and_set_bit(mISDN_STACK_WORK, &st->status);
		wake_up_interruptible(&st->workq);
	}
	return 0;
}

EXPORT_SYMBOL(mISDN_queue_message);

static inline int
send_msg_to_layer(struct mISDNstack *st, struct sk_buff *skb)
{
	struct mISDNhead *hh = mISDN_HEAD_P(skb);
	int	lm;

	lm = hh->prim & MISDN_LAYERMASK;
	if (*debug & DEBUG_QUEUE_FUNC)
		printk(KERN_DEBUG "%s prim(%x) id(%x)\n", __FUNCTION__,
		    hh->prim, hh->id);
	if (lm == 0x2) {
		if (likely(st->layer[0]))
			return st->layer[0]->send(st->layer[0], skb);
		else
			printk(KERN_WARNING "%s: dev(%s) prim %x no L1\n",
			    __FUNCTION__, st->dev->name, hh->prim);
	} else if (lm == 0x4) {
		if (likely(st->layer[1]))
			return st->layer[1]->send(st->layer[1], skb);
		else
			printk(KERN_WARNING "%s: dev(%s) prim %x no L2\n",
			    __FUNCTION__, st->dev->name, hh->prim);
	} else if (lm == 0x8) {
		if (likely(st->layer[2]))
			return st->layer[2]->send(st->layer[2], skb);
		else
			printk(KERN_WARNING "%s: dev(%s) prim %x no L3\n",
			    __FUNCTION__, st->dev->name, hh->prim);
	} else {
		/* broadcast not handled yet */
		printk(KERN_WARNING "%s: dev(%s) prim %x not delivered\n",
		    __FUNCTION__, st->dev->name, hh->prim);
	}
	return -ESRCH;
}

static void
do_clear_stack(struct mISDNstack *st)
{
}

static int
mISDNStackd(void *data)
{
	struct mISDNstack	*st = data;
	int		err = 0;

#ifdef CONFIG_SMP
	lock_kernel();
#endif
	MAKEDAEMON(st->name);
	sigfillset(&current->blocked);
	st->thread = current;
#ifdef CONFIG_SMP
	unlock_kernel();
#endif
	if (*debug & DEBUG_MSG_THREAD)
		printk(KERN_DEBUG "mISDNStackd started for id(%d)\n", st->id);

	if (st->notify != NULL) {
		up(st->notify);
		st->notify = NULL;
	}
	
	for (;;) {
		struct sk_buff	*skb;
		
		if (unlikely(test_bit(mISDN_STACK_STOPPED, &st->status))) {
			test_and_clear_bit(mISDN_STACK_WORK, &st->status);
			test_and_clear_bit(mISDN_STACK_RUNNING, &st->status);
		} else
			test_and_set_bit(mISDN_STACK_RUNNING, &st->status);
		while (test_bit(mISDN_STACK_WORK, &st->status)) {

			skb = skb_dequeue(&st->msgq);
			if (!skb) {
				test_and_clear_bit(mISDN_STACK_WORK,
				    &st->status);
				/* test if a race happens */
				if (!(skb = skb_dequeue(&st->msgq)))
					continue;
				test_and_set_bit(mISDN_STACK_WORK,
				    &st->status);
			}
#ifdef MISDN_MSG_STATS
			st->msg_cnt++;
#endif
			err = send_msg_to_layer(st, skb);
			if (unlikely(err)) {
				if (*debug & DEBUG_SEND_ERR)
					printk(KERN_DEBUG
					    "%s: %s prim(%x) id(%x) "
					    "send call(%d)\n",
					    __FUNCTION__, st->name,
					    mISDN_HEAD_PRIM(skb),
					    mISDN_HEAD_ID(skb), err);
				dev_kfree_skb(skb);
				continue;
			}
			if (unlikely(test_bit(mISDN_STACK_STOPPED,
			    &st->status))) {
				test_and_clear_bit(mISDN_STACK_WORK,
				    &st->status);
				test_and_clear_bit(mISDN_STACK_RUNNING,
				    &st->status);
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
				test_and_set_bit(mISDN_STACK_WORK,
				    &st->status);
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
		wait_event_interruptible(st->workq, (st->status &
		    mISDN_STACK_ACTION_MASK));
		if (*debug & DEBUG_MSG_THREAD)
			printk(KERN_DEBUG "%s: %d wake status %08lx\n",
			    __FUNCTION__, st->id, st->status);
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
	printk(KERN_DEBUG "mISDNStackd daemon for id(%08x) proceed %d "
	    "msg %d sleep %d stopped\n",
	    st->id, st->msg_cnt, st->sleep_cnt, st->stopped_cnt);
	printk(KERN_DEBUG
	    "mISDNStackd daemon for id(%08x) utime(%ld) stime(%ld)\n",
	    st->id, st->thread->utime, st->thread->stime);
	printk(KERN_DEBUG
	    "mISDNStackd daemon for id(%08x) nvcsw(%ld) nivcsw(%ld)\n",
	    st->id, st->thread->nvcsw, st->thread->nivcsw);
	printk(KERN_DEBUG "mISDNStackd daemon for id(%08x) killed now\n",
	    st->id);
#endif
	test_and_set_bit(mISDN_STACK_KILLED, &st->status);
	test_and_clear_bit(mISDN_STACK_RUNNING, &st->status);
	test_and_clear_bit(mISDN_STACK_ACTIVE, &st->status);
	test_and_clear_bit(mISDN_STACK_ABORT, &st->status);
	skb_queue_purge(&st->msgq);
	st->thread = NULL;
	if (st->notify != NULL) {
		up(st->notify);
		st->notify = NULL;
	}
	return(0);
}

int
mISDN_start_stack_thread(struct mISDNstack *st)
{
	int	err = 0;

	if (st->thread == NULL && test_bit(mISDN_STACK_KILLED, &st->status)) {
		test_and_clear_bit(mISDN_STACK_KILLED, &st->status);
		kernel_thread(mISDNStackd, (void *)st, 0);
	} else
		err = -EBUSY;
	return(err);
}

static inline int
check_send(struct mISDNdmux *mux, struct mISDNhead *hh)
{
	if (test_bit(MISDN_OPT_ALL, &mux->prop))
		return 1;
	if ((hh->id & MISDN_ID_ADDR_MASK) == mux->addr)
		return 1;
	if ((hh->id & MISDN_ID_SAPI_MASK) == (mux->addr & MISDN_ID_SAPI_MASK))
		if ((hh->id & MISDN_ID_TEI_MASK) == MISDN_ID_TEI_ANY)
			return 1;
	if ((hh->id & MISDN_ID_ADDR_MASK) == MISDN_ID_ANY)
		return 1;
	return 0;
}

static int
mux_receive(struct mISDNstack *st, struct sk_buff *skb)
{
	struct sk_buff		*cskb;
	struct mISDNhead	*hh = mISDN_HEAD_P(skb);
	int			send, ret = 0;

	if (!st)
		return -ENODEV;
	send = check_send(&st->rmux, hh);
	if (st->rmux.next && send) {
		cskb = skb_copy(skb, GFP_ATOMIC);
		if (!cskb)
			printk(KERN_WARNING "%s: no free skb\n", __FUNCTION__);
		else
			if (mux_receive(st->rmux.next, cskb))
				dev_kfree_skb(cskb);
	} else if (st->rmux.next)
		ret = mux_receive(st->rmux.next, skb);
	if (send)
		mISDN_queue_message(st, skb);
	return ret;
}


static void
add_rmux(struct mISDNchannel *ch, struct mISDNstack *st)
{
	st->rmux.next = ch->rst;
	ch->rst = st;
}

static void
add_smux(struct mISDNchannel *ch, struct mISDNstack *st)
{
	st->smux.next = ch->sst;
	ch->sst = st;
}

static void
del_rmux(struct mISDNchannel *ch, struct mISDNstack *st)
{
	struct mISDNstack **prev = &ch->rst;

	while(*prev) {
		if (*prev == st) {
			*prev = st->rmux.next;
			return;
		}
		prev = &(*prev)->rmux.next;
	}
	printk(KERN_WARNING "%s stack not found\n", __FUNCTION__);
}

static void
del_smux(struct mISDNchannel *ch, struct mISDNstack *st)
{
	struct mISDNstack **prev = &ch->sst;

	while(*prev) {
		if (*prev == st) {
			*prev = st->smux.next;
			return;
		}
		prev = &(*prev)->smux.next;
	}
	printk(KERN_WARNING "%s stack not found\n", __FUNCTION__);
}

static int
setup_dstack(struct mISDNstack *st, u_int protocol, struct sockaddr_mISDN *adr)
{
	struct mISDNchannel	*ch;
	struct channel_req	rq;
	int			err = 0;

	if (*debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s(%p, %x)\n", __FUNCTION__,
		    st, protocol);
	ch = &st->dev->D;
	switch(protocol) {
	case ISDN_P_TE_S0:
	case ISDN_P_NT_S0:
	 	ch->recv = mux_receive;
		st->layer[0] = ch;
		test_and_set_bit(MISDN_OPT_ALL, &st->rmux.prop);
		rq.adr.channel = 0;
		rq.protocol = protocol;
		err = ch->ctrl(ch, OPEN_CHANNEL, &rq);
		break;
	case ISDN_P_LAPD_TE:
	 	ch->recv = mux_receive;
		st->layer[0] = ch;
		rq.adr.channel = 0;
		rq.protocol = ISDN_P_TE_S0;
		err = ch->ctrl(ch, OPEN_CHANNEL, &rq);
		if (err)
			break;
		rq.protocol = protocol;
		rq.adr = *adr;
		err = st->dev->mgr->ch.ctrl(&st->dev->mgr->ch,
		    CREATE_CHANNEL, &rq);
		if (err) {
			ch->ctrl(ch, CLOSE_CHANNEL, NULL);
		} else {
			st->layer[1] = rq.ch;
			rq.ch->recv = mISDN_queue_message;
			rq.ch->rst = st;
			rq.ch->ctrl(rq.ch, OPEN_CHANNEL, NULL); /* cannot fail */
		}
		break;
	case ISDN_PH_PACKET:
		test_and_set_bit(MISDN_OPT_ALL, &st->rmux.prop);
		rq.adr.channel = 0;
		rq.protocol = ch->protocol;
		err = ch->ctrl(ch, OPEN_CHANNEL, &rq);
		if (!err)
			add_smux(ch, st);
		break;
	default:
		err = -EPROTONOSUPPORT;
	}
	if (err)
		return err;
	err = register_stack(st, st->dev);
	if (!err)
		add_rmux(ch, st);
	return err;
}

static int
setup_bstack(struct mISDNstack *st, u_int protocol, struct sockaddr_mISDN *adr)
{
	struct channel_req	rq;
	int			err;
		
	if (*debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s(%p, %x, %d)\n", __FUNCTION__,
		    st, protocol, adr->channel);
	rq.adr = *adr;
	rq.protocol = protocol;
	err = st->dev->D.ctrl(&st->dev->D, OPEN_CHANNEL, &rq);
	if (err)
		return err;
	st->layer[0] = rq.ch;
	rq.ch->rst = st;
	rq.ch->recv = mISDN_queue_message;
	sprintf(st->name, "%s B%d", st->dev->name, adr->channel);
	return 0;
}

void
set_stack_address(struct mISDNstack *st, u_int sapi, u_int tei)
{
	st->rmux.addr = sapi | (tei <<8);
}

void
set_mgr_channel(struct mISDNchannel *ch, struct mISDNstack *st)
{
	ch->rst = st;
	ch->recv = mISDN_queue_message;
}

int
open_mgr_channel(struct mISDNstack *st, u_int protocol)
{
	struct channel_req	rq;
	u_int			p = 0;
	u_int			err;

	if (protocol == ISDN_P_MGR_TE) {
		if (test_bit(MGR_OPT_NETWORK, &st->dev->mgr->options))
			return -EINVAL;
		if (!test_bit(MGR_OPT_USER, &st->dev->mgr->options))
			p = ISDN_P_TE_S0;
	} else if (protocol == ISDN_P_MGR_NT) {
		if (test_bit(MGR_OPT_USER, &st->dev->mgr->options))
			return -EINVAL;
		if (!test_bit(MGR_OPT_NETWORK, &st->dev->mgr->options))
			p = ISDN_P_NT_S0;
	} else
		return -EINVAL;
	if (p) { /* first time setup */
		rq.adr.channel = 0;
		rq.protocol = p;
		err = st->dev->D.ctrl(&st->dev->D, OPEN_CHANNEL, &rq);
		if (err)
			return err;
		if (protocol == ISDN_P_MGR_TE)
			test_and_set_bit(MGR_OPT_USER, &st->dev->mgr->options);
		else
			test_and_set_bit(MGR_OPT_NETWORK, &st->dev->mgr->options);
	} 
	st->dev->D.ctrl(&st->dev->D, GET_CHANNEL, NULL);
	return 0;
}

void
close_mgr_channel(struct mISDNstack *st)
{
	st->dev->D.ctrl(&st->dev->D, PUT_CHANNEL, NULL);
}

int
create_mgr_stack(struct mISDNdevice *dev)
{
	struct mISDNstack	*newst;
	struct mISDNmanager	*mgr;
	DECLARE_MUTEX_LOCKED(sem);

	if (!(newst = kzalloc(sizeof(struct mISDNstack), GFP_KERNEL))) {
		printk(KERN_ERR "kmalloc mISDN_stack failed\n");
		return -ENOMEM;
	}
	mgr = mISDN_create_manager();
	if (!mgr) {
		printk(KERN_ERR "kmalloc mISDNmanager failed\n");
		kfree(newst);
		return -ENOMEM;
	}
	mgr->ch.dev = dev;
	INIT_LIST_HEAD(&newst->list);
	init_waitqueue_head(&newst->workq);
	skb_queue_head_init(&newst->msgq);
	spin_lock_init(&newst->llock);
	newst->layer[0] = &dev->D;
	dev->D.recv = mux_receive;
	dev->D.rst = newst;
	set_stack_address(newst, TEI_SAPI, GROUP_TEI);
	newst->layer[1] = &mgr->ch;
	set_mgr_channel(&mgr->ch, newst);
	register_stack(newst, dev); /* cannot fail since it is stack 0 */
	if (*debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s: st(%s)\n", __FUNCTION__, newst->name);
	dev->mgr = mgr;
	newst->notify = &sem;
	kernel_thread(mISDNStackd, (void *)newst, 0);
	down(&sem);
	return 0;
}

int
connect_data_stack(struct mISDNchannel *ch, u_int protocol,
    struct sockaddr_mISDN *adr)
{
	return 0;
}

int
create_data_stack(struct mISDNchannel *ch, u_int protocol,
    struct sockaddr_mISDN *adr)
{
	struct mISDNstack	*newst;
	struct mISDNdevice	*dev;
	int			err, dchan, layer;

	if (*debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG
		    "%s(%p, %x adr(%d %d %d %d %d))\n", __FUNCTION__,
		    ch, protocol, adr->dev, adr->channel, adr->id,
		    adr->sapi, adr->tei);
	dev = get_mdevice(adr->dev);
	if (!dev)
		return -ENODEV;
	switch (protocol) {
	case ISDN_P_TE_S0:
	case ISDN_P_NT_S0:
		dchan = 1;
		layer = 1;
		break;
	case ISDN_P_LAPD_TE:
		dchan = 1;
		layer = 2;
		break;
	case ISDN_P_B_RAW:
	case ISDN_P_B_HDLC:
		dchan = 0;
		layer = 1;
		break;
	case ISDN_PH_PACKET:
		dchan = 1;
		layer = 1;
		break;
	default:
		return -ENOPROTOOPT;
	}
	if (dchan && adr->channel)
		return -EINVAL;
	if (dchan == 0 && adr->channel == 0)
		return -EINVAL;
	if (!(newst = kzalloc(sizeof(struct mISDNstack), GFP_KERNEL))) {
		printk(KERN_ERR "kmalloc mISDN_stack failed\n");
		return -ENOMEM;
	}
	newst->protocol = protocol;
	ch->dev = dev;
	INIT_LIST_HEAD(&newst->list);
	init_waitqueue_head(&newst->workq);
	skb_queue_head_init(&newst->msgq);
	spin_lock_init(&newst->llock);
	newst->layer[layer] = ch;
	newst->dev = dev;
	ch->recv = (recv_func_t *)mISDN_queue_message;
	if (protocol == ISDN_PH_PACKET)
		newst->layer[0] = ch;
	if (dchan)
		err = setup_dstack(newst, protocol, adr);
	else
		err = setup_bstack(newst, protocol, adr);
	if (!err) {
		DECLARE_MUTEX_LOCKED(sem);

		if (*debug & DEBUG_CORE_FUNC)
			printk(KERN_DEBUG "%s: st(%s)\n",
			    __FUNCTION__, newst->name);
		ch->rst = newst;
		newst->notify = &sem;
		kernel_thread(mISDNStackd, (void *)newst, 0);
		down(&sem);
		printk(KERN_DEBUG "%s: %p %p %p\n", __FUNCTION__, newst->layer[0], newst->layer[1], newst->layer[2]);
	} else
		kfree(newst);
	return err;
}

int
mISDN_start_stop(struct mISDNstack *st, int start)
{
	int	ret;

	if (start) {
		ret = test_and_clear_bit(mISDN_STACK_STOPPED, &st->status);
		test_and_set_bit(mISDN_STACK_WAKEUP, &st->status);
		if (!skb_queue_empty(&st->msgq))
			test_and_set_bit(mISDN_STACK_WORK, &st->status);
			wake_up_interruptible(&st->workq);
		} else
			ret = test_and_set_bit(mISDN_STACK_STOPPED,
			    &st->status);
	return(ret);
}

int
delete_stack(struct mISDNstack *st)
{
	DECLARE_MUTEX_LOCKED(sem);
	int	i;

	if (*debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s: st(%s) proto(%x)\n", __FUNCTION__,
		    st->name, st->protocol);
	if (st->thread) {
		if (st->notify) {
			printk(KERN_WARNING "%s: notifier in use\n",
			    __FUNCTION__);
				up(st->notify);
		}
		st->notify = &sem;
		test_and_set_bit(mISDN_STACK_ABORT, &st->status);
		mISDN_start_stop(st, 1);
		down(&sem);
	}
	printk(KERN_DEBUG "%s: 1 %p %p %p\n", __FUNCTION__, st->layer[0], st->layer[1], st->layer[2]);
	if (test_bit(mISDN_STACK_BCHANNEL, &st->status)) {
		if (st->layer[0])
			st->layer[0]->rst = NULL;
	} else {
		if (st->layer[0]) {
			if (st->layer[0]->rst)
				del_rmux(st->layer[0], st);
			if (st->layer[0]->sst)
				del_smux(st->layer[0], st);
			if (st->protocol == ISDN_PH_PACKET)
				st->layer[0] = NULL; /* l1 == l2 */
		}
	}
	printk(KERN_DEBUG "%s: 2 %p %p %p\n", __FUNCTION__, st->layer[0], st->layer[1], st->layer[2]);
	for (i = 0; i < 3; i++) {
		if (st->layer[i]) {
			if (!st->layer[i]->ctrl) {
				printk(KERN_WARNING
				    "stack %s layer %d no ctrl\n",
				    st->name, i);
				WARN_ON(1);
				continue;
			}
			st->layer[i]->ctrl(st->layer[i], CLOSE_CHANNEL, NULL);
			st->layer[i] = NULL;
		}
	}
	printk(KERN_DEBUG "%s: 3 %p %p %p\n", __FUNCTION__, st->layer[0], st->layer[1], st->layer[2]);
	list_del(&st->list);
	kfree(st);
	return(0);
}

void
mISDN_initstack(u_int *dp)
{
	debug = dp;
}

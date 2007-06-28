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

static inline void
_queue_message(struct mISDNstack *st, struct sk_buff *skb)
{
	struct mISDNhead	*hh = mISDN_HEAD_P(skb);

	if (*debug & DEBUG_QUEUE_FUNC)
		printk(KERN_DEBUG "%s prim(%x) id(%x) %p\n",
		    __FUNCTION__, hh->prim, hh->id, skb);
	skb_queue_tail(&st->msgq, skb);
	if (likely(!test_bit(mISDN_STACK_STOPPED, &st->status))) {
		test_and_set_bit(mISDN_STACK_WORK, &st->status);
		wake_up_interruptible(&st->workq);
	}
}

int
mISDN_queue_message(struct mISDNchannel *ch, struct sk_buff *skb)
{
	_queue_message(ch->st, skb);
	return 0;	
}

static struct mISDNchannel *
get_channel4id(struct mISDNstack *st, u_int id)
{
	struct mISDNchannel	*ch;

	down(&st->lsem);
	list_for_each_entry(ch, &st->layer2, list) {
		if (id == ch->nr)
			goto unlock;
	}
	ch = NULL;
unlock:
	up(&st->lsem);
	return ch;
}

static void
send_socklist(struct mISDN_sock_list *sl, struct sk_buff *skb, gfp_t gfp_mask)
{
	struct hlist_node	*node;
	struct sock		*sk;
	struct sk_buff		*cskb = NULL;

	read_lock_bh(&sl->lock);
	sk_for_each(sk, node, &sl->head) {
		if (sk->sk_state != MISDN_BOUND)
			continue;
		if (!cskb)
			cskb = skb_copy(skb, gfp_mask);
		if (!cskb) {
			printk(KERN_WARNING "%s no skb\n", __FUNCTION__);
			break;
		}
		if (!sock_queue_rcv_skb(sk, cskb))
			cskb = NULL;
	}
	read_unlock_bh(&sl->lock);
	if (cskb)
		dev_kfree_skb(cskb);
}

static inline int
check_send(struct mISDNchannel *ch, struct mISDNhead *hh)
{
	if (test_bit(MISDN_OPT_ALL, &ch->opt))
		return 1;
	if ((hh->id & MISDN_ID_ADDR_MASK) == ch->addr)
		return 1;
	if ((hh->id & MISDN_ID_SAPI_MASK) == (ch->addr & MISDN_ID_SAPI_MASK))
		if ((hh->id & MISDN_ID_TEI_MASK) == MISDN_ID_TEI_ANY)
			return 1;
	if ((hh->id & MISDN_ID_ADDR_MASK) == MISDN_ID_ANY)
		return 1;
	return 0;
}

static void
send_layer2(struct mISDNstack *st, struct sk_buff *skb)
{
	struct sk_buff		*cskb = NULL;
	struct mISDNhead	*hh = mISDN_HEAD_P(skb);
	struct mISDNchannel	*ch;
	int			send, ret;

	if (!st)
		return;
	down(&st->lsem);
	list_for_each_entry(ch, &st->layer2, list) {
		printk(KERN_DEBUG "%s ch %d addr %x\n", __FUNCTION__, ch->nr, ch->addr);
		send = check_send(ch, hh);
		if (send && list_is_last(&ch->list, &st->layer2)) {
			ret = ch->send(ch, skb);
			if (!ret)
				skb = NULL;
		} else if (send) {
			if (!cskb)
				cskb = skb_copy(skb, GFP_KERNEL);
			if (cskb) {
				ret = ch->send(ch, cskb);
				if (!ret)
					cskb = NULL;
			}
		} 
	}
	up(&st->lsem);
	if (cskb)
		dev_kfree_skb(cskb);
	if (skb)
		dev_kfree_skb(skb);
}

static inline int
send_msg_to_layer(struct mISDNstack *st, struct sk_buff *skb)
{
	struct mISDNhead	*hh = mISDN_HEAD_P(skb);
	struct mISDNchannel	*ch;
	int	lm;

	lm = hh->prim & MISDN_LAYERMASK;
	if (*debug & DEBUG_QUEUE_FUNC)
		printk(KERN_DEBUG "%s prim(%x) id(%x) %p\n",
		    __FUNCTION__, hh->prim, hh->id, skb);
	if (lm == 0x1) {
		if (!hlist_empty(&st->l1sock.head))
			send_socklist(&st->l1sock, skb, GFP_KERNEL);
		return st->layer1->send(st->layer1, skb);
	} else if (lm == 0x2) {
		send_layer2(st, skb);
		return 0;
	} else if (lm == 0x4) {
		ch = get_channel4id(st, hh->id);
		if (ch)
			return ch->send(ch, skb);
		else
			printk(KERN_WARNING
			    "%s: dev(%s) prim(%x) id(%x) no channel\n",
			    __FUNCTION__, st->dev->name, hh->prim, hh->id);
	} else if (lm == 0x8) {
		WARN_ON(lm == 0x8);
		ch = get_channel4id(st, hh->id);
		if (ch)
			return ch->send(ch, skb);
		else
			printk(KERN_WARNING
			    "%s: dev(%s) prim(%x) id(%x) no channel\n",
			    __FUNCTION__, st->dev->name, hh->prim, hh->id);
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
	MAKEDAEMON(st->dev->name);
	sigfillset(&current->blocked);
	st->thread = current;
#ifdef CONFIG_SMP
	unlock_kernel();
#endif
	if (*debug & DEBUG_MSG_THREAD)
		printk(KERN_DEBUG "mISDNStackd %s started\n", st->dev->name);

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
					    __FUNCTION__, st->dev->name,
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
			printk(KERN_DEBUG "%s: %s wake status %08lx\n",
			    __FUNCTION__, st->dev->name, st->status);
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
	printk(KERN_DEBUG "mISDNStackd daemon for %s proceed %d "
	    "msg %d sleep %d stopped\n",
	    st->dev->name, st->msg_cnt, st->sleep_cnt, st->stopped_cnt);
	printk(KERN_DEBUG
	    "mISDNStackd daemon for %s utime(%ld) stime(%ld)\n",
	    st->dev->name, st->thread->utime, st->thread->stime);
	printk(KERN_DEBUG
	    "mISDNStackd daemon for %s nvcsw(%ld) nivcsw(%ld)\n",
	    st->dev->name, st->thread->nvcsw, st->thread->nivcsw);
	printk(KERN_DEBUG "mISDNStackd daemon for %s killed now\n",
	    st->dev->name);
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

static int
l1_receive(struct mISDNchannel *ch, struct sk_buff *skb)
{
	if (!ch->st)
		return -ENODEV;
	if (!hlist_empty(&ch->st->l1sock.head)) {
		printk(KERN_ERR "%s: not yet\n", __FUNCTION__);
		send_socklist(&ch->st->l1sock, skb, GFP_ATOMIC);
	}

	_queue_message(ch->st, skb);
	return 0;
}

void
set_channel_address(struct mISDNchannel *ch, u_int sapi, u_int tei)
{
        ch->addr = sapi | (tei <<8);
}
        
static void
add_layer2(struct mISDNchannel *ch, struct mISDNstack *st)
{
	down(&st->lsem);
	list_add_tail(&ch->list, &st->layer2);
	up(&st->lsem);
}

static int
st_own_ctrl(struct mISDNchannel *ch, u_int cmd, void *arg)
{
	if (!ch->st || ch->st->layer1)
		return -EINVAL;
	return ch->st->layer1->ctrl(ch->st->layer1, cmd, arg);
}

int
create_stack(struct mISDNdevice *dev)
{
	struct mISDNstack	*newst;
	int			err;
	DECLARE_MUTEX_LOCKED(sem);

	if (!(newst = kzalloc(sizeof(struct mISDNstack), GFP_KERNEL))) {
		printk(KERN_ERR "kmalloc mISDN_stack failed\n");
		return -ENOMEM;
	}
	err = create_teimanager(dev);
	if (err) {
		printk(KERN_ERR "kmalloc teimanager failed\n");
		kfree(newst);
		return err;
	}
	newst->dev = dev;
	INIT_LIST_HEAD(&newst->layer2);
	INIT_HLIST_HEAD(&newst->l1sock.head);
	rwlock_init(&newst->l1sock.lock);
	init_waitqueue_head(&newst->workq);
	skb_queue_head_init(&newst->msgq);
	init_MUTEX(&newst->lsem);
	dev->teimgr->peer = &newst->own;
	dev->teimgr->recv = mISDN_queue_message;
	dev->teimgr->st = newst;
	add_layer2(dev->teimgr, newst);
	newst->layer1 = &dev->D;
	dev->D.recv = l1_receive;
	dev->D.peer = &newst->own;
	dev->D.st = newst;
	newst->own.st = newst;
	newst->own.ctrl = st_own_ctrl;
	newst->own.send = mISDN_queue_message;
	newst->own.recv = mISDN_queue_message;
	if (*debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s: st(%s)\n", __FUNCTION__, newst->dev->name);
	newst->notify = &sem;
	kernel_thread(mISDNStackd, (void *)newst, 0);
	down(&sem);
	return 0;
}

int
connect_layer1(struct mISDNdevice *dev, struct mISDNchannel *ch,
    u_int protocol, struct sockaddr_mISDN *adr)
{
        struct mISDN_sock	*msk = container_of(ch, struct mISDN_sock, ch);
	struct channel_req	rq;
	int			err;
	

	if (*debug &  DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s: %s proto(%x) adr(%d %d %d %d)\n",
			__FUNCTION__, dev->name, protocol,
			adr->dev, adr->channel, adr->sapi,
			adr->tei);
	switch(protocol) {
	case ISDN_P_NT_S0:
	case ISDN_P_TE_S0:
		if (dev->D.protocol && dev->D.protocol != protocol)
			return -EINVAL;
	 	ch->recv = mISDN_queue_message;
	 	ch->peer = &dev->D.st->own;
		ch->st = dev->D.st;
		rq.protocol = protocol;
		rq.adr.channel = 0;
		err = dev->D.ctrl(&dev->D, OPEN_CHANNEL, &rq);
		printk(KERN_DEBUG "%s: ret 1 %d\n", __FUNCTION__, err);
		if (err)
			break;
		write_lock_bh(&dev->D.st->l1sock.lock);
		sk_add_node(&msk->sk, &dev->D.st->l1sock.head);
		write_unlock_bh(&dev->D.st->l1sock.lock);
		break;
	default:
		return -ENOPROTOOPT;
	}
	return 0;
}

int
connect_Bstack(struct mISDNdevice *dev, struct mISDNchannel *ch,
    u_int protocol, struct sockaddr_mISDN *adr)
{
	struct channel_req	rq, rq2;
	int			pmask, err;
	struct Bprotocol	*bp;

	if (*debug &  DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s: %s proto(%x) adr(%d %d %d %d)\n",
			__FUNCTION__, dev->name, protocol,
			adr->dev, adr->channel, adr->sapi,
			adr->tei);
	pmask = 1 << (protocol & ISDN_P_B_MASK );
	if (pmask & dev->Bprotocols) {
		rq.protocol = protocol;
		rq.adr = *adr;
		err = dev->D.ctrl(&dev->D, OPEN_CHANNEL, &rq);
		if (err)
			return err;
		ch->recv = rq.ch->send;
		ch->peer = rq.ch;
		rq.ch->recv = ch->send;
		rq.ch->peer = ch;
		rq.ch->st = dev->D.st;
	} else {
		bp = get_Bprotocol4mask(pmask);
		if (!bp)
			return -ENOPROTOOPT;
		rq2.protocol = protocol;
		rq2.adr = *adr;
		rq2.ch = ch;
		err = bp->create(&rq2);
		if (err)
			return err;
		ch->recv = rq2.ch->send;
		ch->peer = rq2.ch;
		rq2.ch->st = dev->D.st;
		rq.protocol = rq2.protocol;
		rq.adr = *adr;
		err = dev->D.ctrl(&dev->D, OPEN_CHANNEL, &rq);
		if (err) {
			rq2.ch->ctrl(rq2.ch, CLOSE_CHANNEL, NULL);
			return err;
		}
		rq2.ch->recv = rq.ch->send;
		rq2.ch->peer = rq.ch;
		rq.ch->recv = rq2.ch->send;
		rq.ch->peer = rq2.ch;
		rq.ch->st = dev->D.st;
	}
	ch->st = dev->D.st;
	ch->protocol = protocol;
	ch->nr = rq.ch->nr;
	return 0;
}

int
create_l2entity(struct mISDNdevice *dev, struct mISDNchannel *ch,
    u_int protocol, struct sockaddr_mISDN *adr)
{
	struct channel_req	rq;
	int			err;

	if (*debug &  DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s: %s proto(%x) adr(%d %d %d %d)\n",
			__FUNCTION__, dev->name, protocol,
			adr->dev, adr->channel, adr->sapi,
			adr->tei);
	rq.protocol = ISDN_P_TE_S0;
	switch(protocol) {
	case ISDN_P_LAPD_NT:
		rq.protocol = ISDN_P_NT_S0;
	case ISDN_P_LAPD_TE:
	 	ch->recv = mISDN_queue_message;
	 	ch->peer = &dev->D.st->own;
		ch->st = dev->D.st;
		rq.adr.channel = 0;
		err = dev->D.ctrl(&dev->D, OPEN_CHANNEL, &rq);
		printk(KERN_DEBUG "%s: ret 1 %d\n", __FUNCTION__, err);
		if (err)
			break;
		rq.protocol = protocol;
		rq.adr = *adr;
		rq.ch = ch;
		err = dev->teimgr->ctrl(dev->teimgr, OPEN_CHANNEL, &rq);
		printk(KERN_DEBUG "%s: ret 2 %d\n", __FUNCTION__, err); 
		if (err) {
			dev->D.ctrl(&dev->D, CLOSE_CHANNEL, NULL);
		} else {
			add_layer2(rq.ch, dev->D.st);
			rq.ch->recv = mISDN_queue_message;
			rq.ch->peer = &dev->D.st->own;
			rq.ch->ctrl(rq.ch, OPEN_CHANNEL, NULL); /* cannot fail */
			rq.ch->st = dev->D.st;
		}
		break;
	default:
		err = -EPROTONOSUPPORT;
	}
	return err;
}

void
delete_channel(struct mISDNchannel *ch)
{
        struct mISDN_sock	*msk = container_of(ch, struct mISDN_sock, ch);
	struct mISDNchannel	*pch;

	if (!ch->st) {
		printk(KERN_WARNING "%s: no stack\n", __FUNCTION__);
		return;
	}
	if (*debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s: st(%s) protocol(%x)\n", __FUNCTION__,
		    ch->st->dev->name, ch->protocol);
	if (ch->protocol >= ISDN_P_B_START) {
		if (ch->peer) {
			ch->peer->ctrl(ch->peer, CLOSE_CHANNEL, NULL);
			ch->peer = NULL;
		}
		return;
	}
	switch(ch->protocol) {
	case ISDN_P_NT_S0:
	case ISDN_P_TE_S0:
		write_lock_bh(&ch->st->l1sock.lock);
		sk_del_node_init(&msk->sk);
		write_unlock_bh(&ch->st->l1sock.lock);
		ch->st->dev->D.ctrl(&ch->st->dev->D, CLOSE_CHANNEL, NULL);
		break;
	case ISDN_P_LAPD_TE:
	case ISDN_P_LAPD_NT:
		pch = get_channel4id(ch->st, ch->nr);
		if (pch) {
			down(&ch->st->lsem);
			list_del(&pch->list);
			up(&ch->st->lsem);
			pch->ctrl(pch, CLOSE_CHANNEL, NULL);
			ch->st->dev->D.ctrl(&ch->st->dev->D,
			    CLOSE_CHANNEL, NULL);
		} else
			printk(KERN_WARNING "%s: no l2 channel\n",
			    __FUNCTION__);
		break;
	default:
		break;
	}
	return;
}

void
delete_stack(struct mISDNdevice *dev)
{
	struct mISDNstack	*st = dev->D.st;
	DECLARE_MUTEX_LOCKED(sem);

	if (*debug & DEBUG_CORE_FUNC)
		printk(KERN_DEBUG "%s: st(%s)\n", __FUNCTION__,
		    st->dev->name);
	if (dev->teimgr) {
		down(&st->lsem);
		list_del(&dev->teimgr->list);	
		up(&st->lsem);
		dev->teimgr->ctrl(dev->teimgr, CLOSE_CHANNEL, NULL);
	}
	if (st->thread) {
		if (st->notify) {
			printk(KERN_WARNING "%s: notifier in use\n",
			    __FUNCTION__);
				up(st->notify);
		}
		st->notify = &sem;
		test_and_set_bit(mISDN_STACK_ABORT, &st->status);
		test_and_set_bit(mISDN_STACK_WAKEUP, &st->status);
		wake_up_interruptible(&st->workq);
		down(&sem);
	}
	if (!list_empty(&st->layer2))
		printk(KERN_WARNING "%s: layer2 list not empty\n",
		    __FUNCTION__);
	if (!hlist_empty(&st->l1sock.head))
		printk(KERN_WARNING "%s: layer2 list not empty\n",
		    __FUNCTION__);
	kfree(st);
}

void
mISDN_initstack(u_int *dp)
{
	debug = dp;
}

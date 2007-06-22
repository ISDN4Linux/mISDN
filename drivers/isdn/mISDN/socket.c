/*
 * $Id: socket.c ,v 2.0 2007/06/06 11:25:06 kkeil Exp $
 *
 * socket.c
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
#include <linux/net.h>
#include <net/sock.h>
#include "socket.h"
#include "core.h"

static int	*debug;

static struct mISDN_sock_list	mgr_sockets = {
	.lock = RW_LOCK_UNLOCKED
};

struct mISDN_mgr_sock {
	struct sock		sk;
	struct mISDNchannel	ch;
	struct mISDN_mgr_sock	*master;
	struct mISDN_mgr_sock	*next;
};

#define _pmgr(sk)	((struct mISDN_mgr_sock *)sk)

static struct mISDN_sock_list	data_sockets = {
	.lock = RW_LOCK_UNLOCKED
};

struct mISDN_data_sock {
	struct sock		sk;
	struct mISDNchannel	ch;
};

#define _pdata(sk)	((struct mISDN_data_sock *)sk)

static struct mISDN_sock_list	base_sockets = {
	.lock = RW_LOCK_UNLOCKED
};

struct mISDN_base_sock {
	struct sock		sk;
	struct mISDNdevice	*dev;
};

#define _pbase(sk)	((struct mISDN_base_sock *)sk)

#define L2_HEADER_LEN	4

static inline struct sk_buff *
_l2_alloc_skb(unsigned int len, gfp_t gfp_mask)
{
	struct sk_buff  *skb;

	skb = alloc_skb(len + L2_HEADER_LEN, gfp_mask);
	if (likely(skb))
		skb_reserve(skb, L2_HEADER_LEN);
	return skb;
}

static void
mISDN_sock_link(struct mISDN_sock_list *l, struct sock *sk)
{
	write_lock_bh(&l->lock);
	sk_add_node(sk, &l->head);
	write_unlock_bh(&l->lock);
}

static void mISDN_sock_unlink(struct mISDN_sock_list *l, struct sock *sk)
{
	write_lock_bh(&l->lock);
	sk_del_node_init(sk);
	write_unlock_bh(&l->lock);
}

static int
mgr_sock_release(struct socket *sock)
{
	struct sock		*sk = sock->sk;
	struct mISDN_mgr_sock	*p, *ms = _pmgr(sk);
	u_long			flags;

        printk(KERN_DEBUG "%s(%p) sk=%p\n", __FUNCTION__, sock, sk);
	if (!sk)
		return 0;
	sk->sk_state = MISDN_CLOSED;
	if (!ms->ch.rst)
		goto done;
	spin_lock_irqsave(& ms->ch.rst->llock, flags);
	if (ms->master) {
		p = ms->master;
		while (p) {
			if (p->next == ms)
				break;
			p = p->next;
		}
		if (p == NULL) {
			printk(KERN_WARNING "%s: wrong chain\n", __FUNCTION__);
			goto unlock;
		}
		p->next = p->next->next;
	} else { /* we are master */
		if (ms->next) {
			ms->ch.rst->layer[2] = &ms->next->ch;
			ms->next->master = NULL;
		} else /* last */
			ms->ch.rst->layer[2] = NULL;
	}
	close_mgr_channel(ms->ch.rst);
unlock:
	spin_unlock_irqrestore(& ms->ch.rst->llock, flags); 
	ms->ch.rst = NULL;
	ms->ch.dev = NULL;
done:
	mISDN_sock_unlink(&mgr_sockets, sk);
	sock_orphan(sk);
	sock_put(sk);

	return 0;
}

static int
mgr_sock_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock		*sk = sock->sk;
	int 			err = 0, id;
	struct mISDNdevice	*dev;

	switch (cmd) {
	default:
		err = -EINVAL;
	}
	return err;
}

static int
mgr_send(struct mISDNchannel *ch, struct sk_buff *skb)
{
	struct mISDN_mgr_sock *ns, *msk = container_of(ch,
	    struct mISDN_mgr_sock, ch);
	struct sk_buff *cskb;

	ns = msk;
	while (ns->next) {
		ns = ns->next;
		if (ns->sk.sk_state != MISDN_BOUND)
			continue;
		cskb = skb_copy(skb, GFP_KERNEL);
		if (!cskb)
			continue;
		if (sock_queue_rcv_skb(&ns->sk, cskb))
			kfree_skb(cskb);
	}
	if (msk->sk.sk_state != MISDN_BOUND) {
		kfree_skb(skb);
	} else {
		if (sock_queue_rcv_skb(&msk->sk, skb))
        		kfree_skb(skb);
	}
	return 0;
}

static int
mgr_ctrl(struct mISDNchannel *ch, u_int cmd, void *arg)
{
	struct mISDN_mgr_sock *dsk = container_of(ch,
	    struct mISDN_mgr_sock, ch);

	if (*debug & DEBUG_SOCKET)
		printk(KERN_DEBUG "%s(%p, %x, %p)\n",
		    __FUNCTION__, ch, cmd, arg);
	switch(cmd) {
	case CLOSE_CHANNEL:
		dsk->sk.sk_state = MISDN_CLOSED;
		break;
	}
	return 0;
}

static int
mgr_sock_bind(struct socket *sock, struct sockaddr *addr, int addr_len)
{
	struct sockaddr_mISDN 	*maddr = (struct sockaddr_mISDN *) addr;
	struct sock 		*sk = sock->sk;
	struct mISDNchannel	*ch;
	struct mISDN_mgr_sock	*ms = _pmgr(sk);
	u_long			flags;
	int 			err = 0;

	if (!maddr || maddr->family != AF_ISDN) {
		printk(KERN_WARNING "%s: wrong address\n",
		    __FUNCTION__);
		return -EINVAL;
	}
	lock_sock(sk);

	if ((ms->sk.sk_protocol != ISDN_P_MGR_TE) &&
	    (ms->sk.sk_protocol != ISDN_P_MGR_NT)) {
		err = -EINVAL;
		printk(KERN_WARNING "%s: wrong protocol\n", __FUNCTION__);
		goto done;
	}
	if (ms->ch.dev) {
		err = -EALREADY;
		printk(KERN_WARNING "%s: already bound to %s\n",
		    __FUNCTION__, ms->ch.dev->name);
		goto done;
	}

	ms->ch.dev = get_mdevice(maddr->dev);
	if (!ms->ch.dev) {
		printk(KERN_WARNING "%s: no device %d\n",
		    __FUNCTION__, maddr->dev);
		err = -ENODEV;
		goto done;
	}	

	if (!ms->ch.dev->mgr || !ms->ch.dev->mgr->ch.rst) {
		printk(KERN_WARNING "%s: no mgr\n",
		    __FUNCTION__);
		err = -EINVAL;
		goto done;
	}
	ms->ch.send = mgr_send;
	ms->ch.ctrl = mgr_ctrl;
	spin_lock_irqsave(& ms->ch.dev->mgr->ch.rst->llock, flags);
	ch = ms->ch.dev->mgr->ch.rst->layer[2];
	if (!ch) {
		ms->master = NULL;
		ms->ch.dev->mgr->ch.rst->layer[2] = &ms->ch;
	} else {
		ms->master = container_of(ch, struct mISDN_mgr_sock, ch);
		ms->next = ms->master->next;
		ms->master->next = ms;
	}
	set_mgr_channel(&ms->ch, ms->ch.dev->mgr->ch.rst);
	err = open_mgr_channel(ms->ch.dev->mgr->ch.rst, sk->sk_protocol);
	if (err) {
		if (ms->master)
			ms->master->next = ms->next;
		else
			ms->ch.dev->mgr->ch.rst->layer[2] = NULL;
		goto unlock;
	}
	sk->sk_state = MISDN_BOUND;
unlock:
	spin_unlock_irqrestore(& ms->ch.dev->mgr->ch.rst->llock, flags);
done:
	release_sock(sk);
	return err;
}

static int
mgr_sock_recvmsg(struct kiocb *iocb, struct socket *sock, 
    struct msghdr *msg, size_t len, int flags)
{
	struct sk_buff	*skb;
	struct sock	*sk = sock->sk;
	int		copied, err;

	if (*debug & DEBUG_SOCKET)
		printk(KERN_DEBUG "%s: len %ld, flags %x\n",
			__FUNCTION__, len, flags);
	if (flags & (MSG_OOB))
		return -EOPNOTSUPP;

	if (sk->sk_state == MISDN_CLOSED)
		return 0;

	if (!(skb = skb_recv_datagram(sk, flags, flags & MSG_DONTWAIT, &err)))
		return err;

	msg->msg_namelen = 0;
	copied = skb->len + MISDN_HEADER_LEN;
	if (len < copied) {
		if (flags & MSG_PEEK) {
			atomic_dec(&skb->users);
		} else {
			skb_queue_head(&sk->sk_receive_queue, skb);
		}
		return -ENOSPC;
	}
	// TODO: Maybe use of cmsg for this data
	memcpy(skb_push(skb, MISDN_HEADER_LEN), mISDN_HEAD_P(skb),
	    MISDN_HEADER_LEN);
//	skb->h.raw = skb->data;
	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);

//	hci_sock_cmsg(sk, msg, skb);

	skb_free_datagram(sk, skb);

	return err ? : copied;
}

static int
mgr_sock_sendmsg(struct kiocb *iocb, struct socket *sock,
    struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	int err = -ENOMEM;

	if (*debug & DEBUG_SOCKET)
		printk(KERN_DEBUG "%s: len %ld\n", __FUNCTION__, len);

	if (msg->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	if (msg->msg_flags & ~(MSG_DONTWAIT|MSG_NOSIGNAL|MSG_ERRQUEUE))
		return -EINVAL;

	if (len < MISDN_HEADER_LEN)
		return -EINVAL;

	if (sk->sk_state != MISDN_BOUND)
		return -EBADFD;

	lock_sock(sk);

	if (!(skb = _l2_alloc_skb(len, GFP_KERNEL)))
		goto done;

	if (memcpy_fromiovec(skb_put(skb, len), msg->msg_iov, len)) {
		err = -EFAULT;
		goto drop;
	}

	memcpy(mISDN_HEAD_P(skb), skb->data, MISDN_HEADER_LEN);
	skb_pull(skb, MISDN_HEADER_LEN);

	if ((err = _pmgr(sk)->ch.recv(_pmgr(sk)->ch.rst, skb)))
		goto drop;
	
	err = len;

done:
	release_sock(sk);
	return err;

drop:
	kfree_skb(skb);
	goto done;
}

static const struct proto_ops mgr_sock_ops = {
	.family		= PF_ISDN,
	.owner		= THIS_MODULE,
	.release	= mgr_sock_release,
	.ioctl		= mgr_sock_ioctl,
	.bind		= mgr_sock_bind,
	.getname	= sock_no_getname,
	.sendmsg	= mgr_sock_sendmsg,
	.recvmsg	= mgr_sock_recvmsg,
	.poll		= sock_no_poll,
	.listen		= sock_no_listen,
	.shutdown	= sock_no_shutdown,
	.setsockopt	= sock_no_setsockopt,
	.getsockopt	= sock_no_getsockopt,
	.connect	= sock_no_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= sock_no_accept,
	.mmap		= sock_no_mmap
};

static struct proto mgr_proto = {
	.name		= "misdnmgr",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct mISDN_mgr_sock)
};

static int
mgr_sock_create(struct socket *sock, int protocol)
{
	struct sock *sk;

	if (sock->type != SOCK_RAW)
		return -ESOCKTNOSUPPORT;

	sk = sk_alloc(PF_ISDN, GFP_KERNEL, &mgr_proto, 1);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);
	sock->ops = &mgr_sock_ops;
	sock->state = SS_UNCONNECTED;
	sock_reset_flag(sk, SOCK_ZAPPED);

	sk->sk_protocol = protocol;
	sk->sk_state    = MISDN_OPEN;
	mISDN_sock_link(&mgr_sockets, sk);

	return 0;
}

static int
data_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

        printk(KERN_DEBUG "%s(%p) sk=%p\n", __FUNCTION__, sock, sk);
	if (!sk)
		return 0;
	
	if (_pdata(sk)->ch.rst)
		delete_stack(_pdata(sk)->ch.rst);
	mISDN_sock_unlink(&data_sockets, sk);
	sock_orphan(sk);
	sock_put(sk);

	return 0;
}

static int
data_sock_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock		*sk = sock->sk;
	int 			err = 0, id;
	struct mISDNdevice	*dev;

	switch (cmd) {
	case IMGETCOUNT:
		id = get_mdevice_count();
		if (put_user(id, (int __user *)arg))
			err = -EFAULT;
		break;
	case IMGETDEVINFO:
		if (get_user(id, (int __user *)arg)) {
			err = -EFAULT;
			break;
		}
		dev = get_mdevice(id);
		if (dev) {
			struct mISDN_devinfo di;

			di.id = dev->id;
			di.protocols = dev->D.protocols;
			di.protocol = dev->D.protocol;
			di.nrbchan = dev->nrbchan;
			strcpy(di.name, dev->name);
			if (copy_to_user((void __user *)arg, &di, sizeof(di)))
				err = -EFAULT;
		} else
			err = -ENODEV;
		break;
	default:
		err = -EINVAL;
	}
	return err;
}

static int
data_send(struct mISDNchannel *ch, struct sk_buff *skb)
{
	struct mISDN_data_sock *dsk = container_of(ch,
	    struct mISDN_data_sock, ch);

	if (dsk->sk.sk_state != MISDN_BOUND)
		return -EUNATCH;
	if (sock_queue_rcv_skb(&dsk->sk, skb))
        	kfree_skb(skb);
	return 0;
}

static int
data_ctrl(struct mISDNchannel *ch, u_int cmd, void *arg)
{
	struct mISDN_data_sock *dsk = container_of(ch,
	    struct mISDN_data_sock, ch);

	if (*debug & DEBUG_SOCKET)
		printk(KERN_DEBUG "%s(%p, %x, %p)\n",
		    __FUNCTION__, ch, cmd, arg);
	switch(cmd) {
	case CLOSE_CHANNEL:
		dsk->sk.sk_state = MISDN_CLOSED;
		break;
	}
	return 0;
}

static int
data_sock_bind(struct socket *sock, struct sockaddr *addr, int addr_len)
{
	struct sockaddr_mISDN *maddr = (struct sockaddr_mISDN *) addr;
	struct sock *sk = sock->sk;
	int err = 0;

	if (!maddr || maddr->family != AF_ISDN)
		return -EINVAL;

	lock_sock(sk);

	if (_pdata(sk)->ch.dev) {
		err = -EALREADY;
		goto done;
	}

	_pdata(sk)->ch.send = data_send;
	_pdata(sk)->ch.ctrl = data_ctrl;

	switch(sk->sk_protocol) {
	case ISDN_P_TE_S0:
	case ISDN_P_NT_S0:
	case ISDN_P_LAPD_TE:
	case ISDN_P_B_RAW:
	case ISDN_P_B_HDLC:
	case ISDN_PH_PACKET:
		err = create_data_stack(&_pdata(sk)->ch, sk->sk_protocol,
		    maddr);
		break;
	case ISDN_P_LAPD_NT:
		err = connect_data_stack(&_pdata(sk)->ch, sk->sk_protocol,
		    maddr);
		break;
	default:
		err = -EPROTONOSUPPORT;
	}
	if (err)
		goto done;
	sk->sk_state = MISDN_BOUND;
	
done:
	release_sock(sk);
	return err;
}

static int 
data_sock_getname(struct socket *sock, struct sockaddr *addr,
    int *addr_len, int peer)
{
	struct sockaddr_mISDN 	*maddr = (struct sockaddr_mISDN *) addr;
	struct sock		*sk = sock->sk;
	struct mISDNstack	*st;

	if (!_pdata(sk)->ch.dev)
		return -EBADFD;

	lock_sock(sk);

	*addr_len = sizeof(*maddr);
	memset(maddr, 0, *addr_len);
	maddr->family = AF_ISDN;
	maddr->dev  = _pdata(sk)->ch.dev->id;
	st = _pdata(sk)->ch.rst;
	if (!st)
		goto done;
	maddr->id = st->id;
	if (!st->layer[0])
		goto done;
	maddr->channel = st->layer[0]->nr;
	switch(sk->sk_protocol) {
	case ISDN_P_LAPD_TE:
	case ISDN_P_LAPD_NT:
		maddr->sapi = st->rmux.addr & 0xff;
		maddr->tei = (st->rmux.addr >>8) & 0xff;
		break;
	}
done:		
	release_sock(sk);
	return 0;
}

static int
data_sock_recvmsg(struct kiocb *iocb, struct socket *sock, 
    struct msghdr *msg, size_t len, int flags)
{
	struct sk_buff	*skb;
	struct sock	*sk = sock->sk;
	int		copied, err;

	if (*debug & DEBUG_SOCKET)
		printk(KERN_DEBUG "%s: len %ld, flags %x\n",
			__FUNCTION__, len, flags);
	if (flags & (MSG_OOB))
		return -EOPNOTSUPP;

	if (sk->sk_state == MISDN_CLOSED)
		return 0;

	if (!(skb = skb_recv_datagram(sk, flags, flags & MSG_DONTWAIT, &err)))
		return err;

	msg->msg_namelen = 0;
	copied = skb->len + MISDN_HEADER_LEN;
	if (len < copied) {
		if (flags & MSG_PEEK) {
			atomic_dec(&skb->users);
		} else {
			skb_queue_head(&sk->sk_receive_queue, skb);
		}
		return -ENOSPC;
	}
	// TODO: Maybe use of cmsg for this data
	memcpy(skb_push(skb, MISDN_HEADER_LEN), mISDN_HEAD_P(skb),
	    MISDN_HEADER_LEN);
//	skb->h.raw = skb->data;
	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);

//	hci_sock_cmsg(sk, msg, skb);

	skb_free_datagram(sk, skb);

	return err ? : copied;
}

static int
data_sock_sendmsg(struct kiocb *iocb, struct socket *sock,
    struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	int err = -ENOMEM;

	if (*debug & DEBUG_SOCKET)
		printk(KERN_DEBUG "%s: len %ld\n", __FUNCTION__, len);

	if (msg->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	if (sk->sk_protocol == ISDN_PH_PACKET)
		return 0; /* no data to this socket */
		
	if (msg->msg_flags & ~(MSG_DONTWAIT|MSG_NOSIGNAL|MSG_ERRQUEUE))
		return -EINVAL;

	if (len < MISDN_HEADER_LEN)
		return -EINVAL;

	if (sk->sk_state != MISDN_BOUND)
		return -EBADFD;

	lock_sock(sk);

	if (!(skb = _l2_alloc_skb(len, GFP_KERNEL)))
		goto done;

	if (memcpy_fromiovec(skb_put(skb, len), msg->msg_iov, len)) {
		err = -EFAULT;
		goto drop;
	}

	memcpy(mISDN_HEAD_P(skb), skb->data, MISDN_HEADER_LEN);
	skb_pull(skb, MISDN_HEADER_LEN);

	if ((err = _pdata(sk)->ch.recv(_pdata(sk)->ch.rst, skb)))
		goto drop;
	
	err = len;

done:
	release_sock(sk);
	return err;

drop:
	kfree_skb(skb);
	goto done;
}

static const struct proto_ops data_sock_ops = {
	.family		= PF_ISDN,
	.owner		= THIS_MODULE,
	.release	= data_sock_release,
	.ioctl		= data_sock_ioctl,
	.bind		= data_sock_bind,
	.getname	= data_sock_getname,
	.sendmsg	= data_sock_sendmsg,
	.recvmsg	= data_sock_recvmsg,
	.poll		= datagram_poll,
	.listen		= sock_no_listen,
	.shutdown	= sock_no_shutdown,
	.setsockopt	= sock_no_setsockopt,
	.getsockopt	= sock_no_getsockopt,
	.connect	= sock_no_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= sock_no_accept,
	.mmap		= sock_no_mmap
};

static struct proto data_proto = {
	.name		= "misdndata",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct mISDN_data_sock)
};

static int
data_sock_create(struct socket *sock, int protocol)
{
	struct sock *sk;

	if (sock->type != SOCK_RAW)
		return -ESOCKTNOSUPPORT;

	sk = sk_alloc(PF_ISDN, GFP_KERNEL, &data_proto, 1);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);
	sock->ops = &data_sock_ops;
	sock->state = SS_UNCONNECTED;
	sock_reset_flag(sk, SOCK_ZAPPED);

	sk->sk_protocol = protocol;
	sk->sk_state    = MISDN_OPEN;
	mISDN_sock_link(&data_sockets, sk);

	return 0;
}

static int
base_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

        printk(KERN_DEBUG "%s(%p) sk=%p\n", __FUNCTION__, sock, sk);
	if (!sk)
		return 0;
	
	mISDN_sock_unlink(&base_sockets, sk);
	sock_orphan(sk);
	sock_put(sk);

	return 0;
}

static int
base_sock_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock		*sk = sock->sk;
	int 			err = 0, id;
	struct mISDNdevice	*dev;

	switch (cmd) {
	case IMGETCOUNT:
		id = get_mdevice_count();
		if (put_user(id, (int __user *)arg))
			err = -EFAULT;
		break;
	case IMGETDEVINFO:
		if (get_user(id, (int __user *)arg)) {
			err = -EFAULT;
			break;
		}
		dev = get_mdevice(id);
		if (dev) {
			struct mISDN_devinfo di;

			di.id = dev->id;
			di.protocols = dev->D.protocols;
			di.protocol = dev->D.protocol;
			di.nrbchan = dev->nrbchan;
			strcpy(di.name, dev->name);
			if (copy_to_user((void __user *)arg, &di, sizeof(di)))
				err = -EFAULT;
		} else
			err = -ENODEV;
		break;
	default:
		err = -EINVAL;
	}
	return err;
}

static int
base_sock_bind(struct socket *sock, struct sockaddr *addr, int addr_len)
{
	struct sockaddr_mISDN *maddr = (struct sockaddr_mISDN *) addr;
	struct sock *sk = sock->sk;
	int err = 0;

	if (!maddr || maddr->family != AF_ISDN)
		return -EINVAL;

	lock_sock(sk);

	if (_pbase(sk)->dev) {
		err = -EALREADY;
		goto done;
	}

	_pbase(sk)->dev = get_mdevice(maddr->dev);
	if (!_pbase(sk)->dev) {
		err = -ENODEV;
		goto done;
	}	
	sk->sk_state = MISDN_BOUND;

done:
	release_sock(sk);
	return err;
}

static const struct proto_ops base_sock_ops = {
	.family		= PF_ISDN,
	.owner		= THIS_MODULE,
	.release	= base_sock_release,
	.ioctl		= base_sock_ioctl,
	.bind		= base_sock_bind,
	.getname	= sock_no_getname,
	.sendmsg	= sock_no_sendmsg,
	.recvmsg	= sock_no_recvmsg,
	.poll		= sock_no_poll,
	.listen		= sock_no_listen,
	.shutdown	= sock_no_shutdown,
	.setsockopt	= sock_no_setsockopt,
	.getsockopt	= sock_no_getsockopt,
	.connect	= sock_no_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= sock_no_accept,
	.mmap		= sock_no_mmap
};

static struct proto base_proto = {
	.name		= "misdnbase",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct mISDN_base_sock)
};

static int
base_sock_create(struct socket *sock, int protocol)
{
	struct sock *sk;

	if (sock->type != SOCK_RAW)
		return -ESOCKTNOSUPPORT;

	sk = sk_alloc(PF_ISDN, GFP_KERNEL, &base_proto, 1);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);
	sock->ops = &base_sock_ops;
	sock->state = SS_UNCONNECTED;
	sock_reset_flag(sk, SOCK_ZAPPED);
	sk->sk_protocol = protocol;
	sk->sk_state    = MISDN_OPEN;
	mISDN_sock_link(&base_sockets, sk);

	return 0;
}

static int
mISDN_sock_create(struct socket *sock, int proto)
{
        int err = -EPROTONOSUPPORT;

	switch(proto) {
	case ISDN_P_BASE:
		err = base_sock_create(sock, proto);
		break;
	case ISDN_P_TE_S0:
	case ISDN_P_NT_S0:
	case ISDN_P_LAPD_TE:
	case ISDN_P_LAPD_NT:
	case ISDN_P_B_RAW:
	case ISDN_P_B_HDLC:
	case ISDN_PH_PACKET:
		err = data_sock_create(sock, proto);
		break;
	case ISDN_P_MGR_TE:
	case ISDN_P_MGR_NT:
		err = mgr_sock_create(sock, proto);
		break;
	default:
		return err;
	}	

        return err;
}

static struct 
net_proto_family mISDN_sock_family_ops =
{
	.owner  = THIS_MODULE,
	.family = PF_ISDN,
	.create = mISDN_sock_create,
};

int
misdn_sock_init(u_int *deb)
{
	int err;

	debug = deb;
	err = sock_register(&mISDN_sock_family_ops);
	if (err)
		printk(KERN_ERR "%s: error(%d)\n", __FUNCTION__, err);
	return err;
}

void
misdn_sock_cleanup(void)
{
	sock_unregister(PF_ISDN);
}


/*
 * debugtool.c: Debug-Tool for mISDN
 *
 * Copyright (C) 2007, Nadi Sarrar
 *
 * Nadi Sarrar <nadi@beronet.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/skbuff.h>
#include <linux/delay.h>

#include "core.h"
#include <linux/mISDNdebugtool.h>

#define DT_VERSION 1
#define MODULE_NAME "mISDN_debugtool"

#define ADDR INADDR_LOOPBACK
static int PORT = 50501;

static int dt_enabled = 0;

static struct task_struct *thread;
static struct sk_buff_head skb_q;
static DECLARE_WAIT_QUEUE_HEAD(skb_q_wait);

static void dt_new_frame (mISDNstack_t *stack, enum mISDN_dt_type type, struct sk_buff *skb, int duplicate_skb)
{
	struct sk_buff *dup;
	mISDN_dt_header_t *hdr;

	dup = skb ? (duplicate_skb ? skb_copy(skb, GFP_ATOMIC) : skb) : alloc_skb(0, GFP_ATOMIC);
	if (!dup)
		return;

	hdr = (mISDN_dt_header_t *)dup->cb;
	memset(hdr, 0, sizeof(mISDN_dt_header_t));
	hdr->version = DT_VERSION;
	hdr->type = type;
	hdr->stack_id = stack->id;
	hdr->stack_protocol = stack->pid.modparm_protocol;
	hdr->time = current_kernel_time();
	hdr->plength = skb ? skb->len : 0;

	skb_queue_tail(&skb_q, dup);
	wake_up_interruptible(&skb_q_wait);
}

static inline int dt_send_buf (struct socket *sock, struct sockaddr_in *addr, unsigned char *buf, int len)
{
	struct msghdr msg;
	struct iovec iov;
	mm_segment_t oldfs;
	int size = 0;

	if (!sock->sk)
		return 0;

	iov.iov_base = buf;
	iov.iov_len = len;

	msg.msg_flags = 0;
	msg.msg_name = addr;
	msg.msg_namelen  = sizeof(struct sockaddr_in);
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	size = sock_sendmsg(sock, &msg, len);
	set_fs(oldfs);

	return size;
}

static inline void dt_send_skb (struct socket *sock, struct sockaddr_in *addr, const struct sk_buff *skb)
{
	if (skb->len) {
		unsigned char buf[sizeof(mISDN_dt_header_t) + skb->len];
		memcpy(buf, skb->cb, sizeof(mISDN_dt_header_t));
		memcpy(buf + sizeof(mISDN_dt_header_t), skb->data, skb->len);
		dt_send_buf(sock, addr, buf, sizeof(buf));
	} else
		dt_send_buf(sock, addr, (unsigned char *)skb->cb, sizeof(mISDN_dt_header_t));
}

static void dt_run (void)
{
	int ret;
	struct sk_buff *skb;
	struct socket *sock;
	struct sockaddr_in addr;

	lock_kernel();
	current->flags |= PF_NOFREEZE;
	daemonize(MODULE_NAME);
	allow_signal(SIGKILL);
	unlock_kernel();

	/* init socket */
	ret = sock_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP, &sock);
	if (ret < 0) {
		printk(KERN_INFO "%s: sock_create failed! (%d)\n", __FUNCTION__, -ret);
		sock = NULL;
		return;
	}
	memset(&addr, 0, sizeof(struct sockaddr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(ADDR);
	addr.sin_port = htons(PORT);
	ret = sock->ops->connect(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr), 0);
	if (ret < 0) {
		printk(KERN_INFO "%s: Could not connect to socket (%d)\n", __FUNCTION__, -ret);
		sock_release(sock);
		sock = NULL;
		return;
	}

	printk(KERN_INFO MODULE_NAME ": Using destination port %d.\n", PORT);

	for (;;) {
		wait_event_interruptible(skb_q_wait, signal_pending(current) || !skb_queue_empty(&skb_q));
		if (signal_pending(current))
			break;
		skb = skb_dequeue(&skb_q);
		dt_send_skb(sock, &addr, skb);
		kfree_skb(skb);
	}

	printk(KERN_INFO MODULE_NAME ": Leaving thread successfully.\n");
	
	sock_release(sock);
}

/* sysfs */
static void dt_class_release (struct class_device *dev)
{}

static struct class dt_class = {
	.name = "mISDN-debugtool",
#ifndef CLASS_WITHOUT_OWNER
	.owner = THIS_MODULE,
#endif
	.release = &dt_class_release,
};

static ssize_t attr_show_enabled (struct class *class, char *buf)
{
	return sprintf(buf, "%d\n", dt_enabled);
}

static ssize_t attr_store_enabled (struct class *class, const char *buf, size_t count)
{
	if (count > 0 && *buf == '1') {
		mISDN_dt_enable();
		dt_enabled = 1;
	} else {
		mISDN_dt_disable();
		dt_enabled = 0;
	}

	return count;
}

static CLASS_ATTR(enabled, 0644, attr_show_enabled, attr_store_enabled);

module_param(PORT, int, S_IRUGO);

int __init dt_init(void)
{
	/* init queue */
	skb_queue_head_init(&skb_q);

	/* init sysfs */
	if (class_register(&dt_class) ||
		class_create_file(&dt_class, &class_attr_enabled)) {
		printk(KERN_INFO MODULE_NAME "%s: Failed to initialize sysfs!\n", __FUNCTION__);
		return -EPERM;
	}

	/* start thread */
	thread = kthread_run((void *)dt_run, NULL, MODULE_NAME);
	if (IS_ERR(thread)) {
		printk(KERN_INFO MODULE_NAME "%s: Could not start kernel thread!\n", __FUNCTION__);
		class_unregister(&dt_class);
		return -ENOMEM;
	}

	/* register with mISDN */
	mISDN_module_register(THIS_MODULE);
	mISDN_dt_set_callback(dt_new_frame);

	printk(KERN_INFO MODULE_NAME ": module loaded\n");

	return 0;
}

void __exit dt_exit(void)
{
	int ret;

	mISDN_module_unregister(THIS_MODULE);

	if (thread) {
		lock_kernel();
		ret = kill_proc(thread->pid, SIGKILL, 1);
		unlock_kernel();
		if (ret < 0)
			printk(KERN_INFO MODULE_NAME ": Unknown error (%d) while trying to terminate kernel thread!\n", -ret);
		wake_up_interruptible(&skb_q_wait);
	}

	class_unregister(&dt_class);

	skb_queue_purge(&skb_q);

	printk(KERN_INFO MODULE_NAME ": module unloaded\n");
}

module_init(dt_init);
module_exit(dt_exit);

MODULE_DESCRIPTION("Debug-Tool for mISDN");
MODULE_AUTHOR("Nadi Sarrar <nadi@beronet.com>");
MODULE_LICENSE("GPL");


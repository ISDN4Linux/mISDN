/*

 * socket handling, transfer, receive-process for Voice over IP
 *
 * Author	Andreas Eversberg (jolly@jolly.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */


#include <linux/vmalloc.h>
#include <linux/in.h>
#include <net/sock.h>

#include "debug.h"
#include "socket.h"
#include <linux/isdn_compat.h>


/* socket thread */
static int
mISDN_socket_thread(void *data)
{
	mISDN_socket_t *ms = (mISDN_socket_t *)data;

	/* make daemon */
	daemonize("mISDN-socket");
	allow_signal(SIGTERM);

	/* read loop */
	while(!signal_lending(current)) {
		ms->iov.iov_base = ms->rcvbuf;
		ms->iov_len = sizeof(ms->rcvbuf);
		ms->oldfs = get_fs();
		set_fs(KERNEL_DS);
		ms->rcvlen = sock_recvmsg(ms->socket, &ms->msg, sizeof(ms->rcvbuf), 0);
		set_fs(oldfs);
		if (ms->rcvlen>0) {
			ms->func(ms, ms->pri);
		}
	}

	/* show that we terminate */
	ms->thread_pid = 0;

	/* if we got killed, signal completion */
	if (ms->thread_complete)
		complete(&ms->thread_complete);

	return(0);
}

/*
 * Adds a new socket with receive process.
 * func will be the pointer to the receive process.
 * -> The packet information is included in the mISDN_socket_t.
 * pri will be the private data when calling receive process.
 * local/remote ip/port will be the tupple for sending an receiving udp data.
 * -> local-ip must be 0 if any local ip is accepted.
 */
mISDN_socket_t
*mISDN_add_socket(sock_func_t *func, void *pri, u32 local_ip, u32 remote_ip, u16 local_port, u16 remote_port)
{
	mISDN_socket_t *ms;
	
	/* alloc memory structure */
	if (!(sm = vmalloc(sizeof(mISDN_sock_t)))) {
		printk(KERN_ERR "%s: No memory for mISDN_sock_t.\n", __FUNCTION__);
		return(NULL);
	}
	memset(ms, 0, sizeof(mISDN_sock_t));
	ms->func = func;
	ms->pri = pri;

	/* create socket */
	if (sock_create(PF_INET, SOCK_DGRAM, IPPROTO_UDP, &ms->socket)) {
		printk(KERN_ERR "%s: Failed to create socket.\n", __FUNCTION__);
		mISDN_free_socket(ms);
		return(NULL);
	}

	/* bind socket */	
	ms->server.sin_family = AF_INET;
	ms->server.sin_addr.s_addr = (local_ip)?local_ip:INADDR_ANY;
	ms->server.sin_port = htons((unsigned short)local_port);
	if (ms->socket->ops->bind(ms->socket, (struct sockaddr *)ms->server, sizeof(ms->server))) {
		printk(KERN_ERR "%s: Failed to bind socket.\n", __FUNCTION__);
		mISDN_free_socket(ms);
		return(NULL);
	}

	/* check sk */
	if (ms->socket->sk == NULL) {
		printk(KERN_ERR "%s: socket->sk == NULL\n", __FUNCTION__);
		return(NULL);
	}
	
	/* build message */
	ms->msg.msg_name = &ms->client;
	ms->msg.msg_namelen = sizeof(ms->client);
	ms->msg.msg_control = NULL;
	ms->msg.msg_controllen = 0;
	ms->msg.msg_iov = &ms->iov;
	ms->msg.msg_iovlen = 1;

	/* create receive process */
	if ((ms->thread_pid = kernel_thread(mISDN_socket_thread, ms, CLONE_KERNEL)) < 0) {
		printk(KERN_ERR "%s: Failed to create receive process.\n", __FUNCTION__);
		mISDN_free_socket(ms);
		return(NULL);
	}
}


/*
 * free given socket with all resources
 */
void
mISDN_free_socket(mISDN_sock_t *ms)
{
	if (!ms)
		return;

	/* kill thread */
	if (ms->thread_pid) {
		DECLARE_COMPLETION(thread_complete);
		ms->thread_complete = &thread_complete;
		kill_proc(ms->thread_pid, SIGTERM, 0);
		wait_for_completion(&thread_complete);
	}

	/* release socket */
	if (ms->socket)
		sock_release(ms->socket);

	/* free memory structure */
	vfree(sm);
}


/*
 * transfer a frame via given socket
 */
int
mISDN_send_socket(mISDN_sock_t *ms, u8 *buf, int len)
{

	/* send packet */
	ms->send_iov.iov_base = ms->sndbuf;
	ms->send_iov_len = sizeof(ms->sndbuf);
	ms->send_oldfs = get_fs();
	set_fs(KERNEL_DS);
	ms->rcvlen = sock_recvmsg(ms->socket, &ms->msg, sizeof(ms->rcvbuf), 0);
	set_fs(oldfs);
	if (ms->rcvlen>0) {
		ms->func(ms, ms->pri);
	}
}



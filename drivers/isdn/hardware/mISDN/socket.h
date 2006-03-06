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

struct _mISDN_socket_t;
typedef void (sock_func_t)(_mISDN_socket_t *ms, void *pri);

typedef struct _mISDN_socket_t {
	sock_func_t *func;
	void *pri;
	int thread_pid;
	struct completion *thread_complete;
	struct socket *socket;
	struct sockaddr_in server, client;
	struct sockaddr *address;
	struct msghdr msg;
	struct iovec iov;
	mm_segment_t oldfs;
	u8 rcvbuf[1500]; 
	int rcvlen;
} mISDN_socket_t;

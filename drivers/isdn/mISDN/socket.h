/*
 * $Id: socket.h ,v 1.0 2007/06/06 11:25:06 kkeil Exp $
 *
 * socket.h
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
#ifndef mISDN_SOCKET_H
#define mISDN_SOCKET_H

struct mISDN_sock_list {
	struct hlist_head	head;
	rwlock_t		lock;
};

/* socket states */
#define MISDN_OPEN	1
#define MISDN_BOUND	2
#define MISDN_CLOSED	3


extern int	misdn_sock_init(u_int *);
extern void	misdn_sock_cleanup(void);

#endif

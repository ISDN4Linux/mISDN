/* $Id: mISDNif.h,v 2.0 2007/06/06 15:39:31 kkeil Exp $
 *
 * Author	Karsten Keil <kkeil@novell.com>
 *
 * Copyright 2007  by Karsten Keil <kkeil@novell.com>
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU LESSER GENERAL PUBLIC LICENSE 
 * version 2.1 as published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU LESSER GENERAL PUBLIC LICENSE for more details.
 *
 */

#ifndef mISDNIF_H
#define mISDNIF_H

#include <stdarg.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/socket.h>

/*
 * ABI Version 32 bit
 *
 * <16 bit> Major version
 *		- changed if any interface become backwards incompatible
 *
 * <16 bit> Minor version
 *              - changed if any interface is extended but backwards compatible
 *
 */
#define	MISDN_MAJOR_VERSION	1
#define	MISDN_MINOR_VERSION	0
#define	MISDN_VERSION		((MISDN_MAJOR_VERSION<<16) | MISDN_MINOR_VERSION)

#define MISDN_REVISION		"$Revision: 2.0 $"
#define MISDN_DATE		"$Date: 2007/06/05 15:39:31 $"


#define MISDN_MSG_STATS

/* collect some statistics about the message queues */
//#define MISDN_MSG_STATS

/* primitives for information exchange
 * generell format
 * <16  bit  0 >
 * <8  bit command>
 *    BIT 8 = 1 LAYER private
 *    BIT 7 = 1 answer
 *    BIT 6 = 1 DATA 
 * <8  bit target layer mask>
 *
 * Layer = 00 is reserved for general commands
   Layer = 01  L2 -> HW
   Layer = 02  HW -> L2
   Layer = 04  L3 -> L2
   Layer = 08  L2 -> L3
 * Layer = FF is reserved for broadcast commands
 */

#define MISDN_CMDMASK		0xff00
#define MISDN_LAYERMASK		0x00ff

/* generell commands */
#define OPEN_CHANNEL		0x0100
#define CLOSE_CHANNEL		0x0200
#define CONTROL_CHANNEL		0x0300

/* layer 2 -> layer 1 */
#define PH_ACTIVATE_REQ		0x0101
#define PH_DEACTIVATE_REQ	0x0201
#define PH_DATA_REQ		0x2001
#define MPH_ACTIVATE_REQ	0x0501
#define MPH_DEACTIVATE_REQ	0x0601
#define MPH_INFORMATION_REQ	0x0701
#define PH_CONTROL_REQ		0x0801

/* layer 1 -> layer 2 */
#define PH_ACTIVATE_IND		0x0102
#define PH_DEACTIVATE_IND	0x0202
#define PH_DATA_IND		0x2002
#define MPH_ACTIVATE_IND	0x0502
#define MPH_DEACTIVATE_IND	0x0602
#define MPH_INFORMATION_IND	0x0702
#define PH_DATA_CNF		0x6002
#define PH_CONTROL_IND		0x0802

/* layer 3 -> layer 2 */
#define DL_ESTABLISH_REQ	0x1004
#define DL_RELEASE_REQ		0x1104
#define DL_DATA_REQ		0x3004
#define DL_UNITDATA_REQ		0x3104
#define DL_INFORMATION_REQ	0x0004

/* layer 2 -> layer 3 */
#define DL_ESTABLISH_IND	0x1008
#define DL_ESTABLISH_CNF	0x5008
#define DL_RELEASE_IND		0x1108
#define DL_RELEASE_CNF		0x5108
#define DL_DATA_IND		0x3008
#define DL_UNITDATA_IND		0x3108
#define DL_INFORMATION_IND	0x0008

/* intern layer 2 managment */
#define MDL_ASSIGN_REQ		0x1804
#define MDL_ASSIGN_IND		0x1904
#define MDL_REMOVE_REQ		0x1A04
#define MDL_REMOVE_IND		0x1B04
#define MDL_STATUS_IND		0x1C04
#define MDL_ERROR_IND		0x1D04
#define MDL_ERROR_RSP		0x5D04

/* DL_INFORMATION_IND types */
#define DL_INFO_L2_CONNECT	0x0001

/* PH_CONTROL types */
/* TOUCH TONE IS 0x20XX  XX "0"..."9", "A","B","C","D","*","#" */
#define DTMF_TONE_VAL		0x2000
#define DTMF_TONE_MASK		0x007F
#define DTMF_TONE_START		0x2100
#define DTMF_TONE_STOP		0x2200

/* 
 * protocol ids
 * D channel 1-31
 * B channel 33 - 63
 */

#define ISDN_P_NONE		0
#define ISDN_P_BASE		0
#define ISDN_P_TE_S0		0x01
#define ISDN_P_NT_S0  		0x02
#define ISDN_P_LAPD_TE		0x10
#define	ISDN_P_LAPD_NT		0x11	

#define ISDN_P_B_MASK		0x1f
#define ISDN_P_B_START		0x20

#define ISDN_P_B_RAW		0x21
#define ISDN_P_B_HDLC		0x22
#define ISDN_P_B_X75SLP		0x23
#define ISDN_P_B_L2DTMF		0x24
#define ISDN_P_B_L2DSP		0x25

#define OPTION_L2_PMX		1
#define OPTION_L2_PTP		2
#define OPTION_L2_FIXEDTEI	3

/* should be in sync with linux/kobject.h:KOBJ_NAME_LEN */
#define MISDN_MAX_IDLEN		20

struct mISDNhead {
	unsigned int	prim;
	unsigned int	id;
	unsigned int	len;
}  __attribute__((packed));

#define MISDN_HEADER_LEN	sizeof(struct mISDNhead)
#define MAX_DATA_SIZE		2048
#define MAX_DATA_MEM		(MAX_DATA_SIZE + MISDN_HEADER_LEN)
#define MAX_DFRAME_LEN		260

#define MISDN_ID_ADDR_MASK	0xFFFF
#define MISDN_ID_TEI_MASK	0xFF00
#define MISDN_ID_SAPI_MASK	0x00FF
#define MISDN_ID_TEI_ANY	0x7F00

#define MISDN_ID_ANY		0xFFFF
#define MISDN_ID_NONE		0xFFFE

#define GROUP_TEI		127
#define TEI_SAPI		63
#define CTRL_SAPI		0

/* socket */
#ifndef	AF_ISDN
#define AF_ISDN		27
#define PF_ISDN		AF_ISDN
#endif

struct sockaddr_mISDN {
	sa_family_t    family;
	unsigned char	dev;
	unsigned char	channel;
	unsigned char	sapi;
	unsigned char	tei;
};

/* timer device ioctl */
#define IMADDTIMER	_IOR('I', 64, int)
#define IMDELTIMER	_IOR('I', 65, int)
/* socket ioctls */
#define	IMGETCOUNT	_IOR('I', 66, int)
#define IMGETDEVINFO	_IOR('I', 67, int)
#define IMCTRLREQ	_IOR('I', 68, int)

struct mISDN_devinfo {
	u_int		id;
	u_int		Dprotocols;
	u_int		Bprotocols;
	u_int		protocol;
	u_int		nrbchan;
	char		name[MISDN_MAX_IDLEN];
};

#define MISDN_CTRL_GETOP		0x0000
#define MISDN_CTRL_LOOP			0x0001
#define MISDN_CTRL_CONNECT		0x0002
#define MISDN_CTRL_DISCONNECT		0x0004
#define MISDN_CTRL_PCMCONNECT		0x0010
#define MISDN_CTRL_PCMDISCONNECT	0x0020

struct mISDN_ctrl_req {
	int		op;
	int		channel;
	int		p1;
	int		p2;
};

/* muxer options */
#define MISDN_OPT_ALL		1
#define MISDN_OPT_TEIMGR	2

#ifdef __KERNEL__
#include <linux/isdn_compat.h>
#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/net.h>
#include <net/sock.h>

#define DEBUG_CORE		0x000000ff
#define DEBUG_CORE_FUNC		0x00000002
#define DEBUG_SOCKET		0x00000004
#define DEBUG_SEND_ERR		0x00000010
#define DEBUG_MSG_THREAD	0x00000020
#define DEBUG_QUEUE_FUNC	0x00000040
#define DEBUG_L1		0x0000ff00
#define DEBUG_L1_FSM		0x00000200
#define DEBUG_L2		0x00ff0000
#define DEBUG_L2_FSM		0x00020000
#define DEBUG_L2_CTRL		0x00040000
#define DEBUG_L2_RECV		0x00080000
#define DEBUG_L2_TEI		0x00100000
#define DEBUG_L2_TEIFSM		0x00200000
#define DEBUG_TIMER		0x01000000
 
#define mISDN_HEAD_P(s)		((struct mISDNhead *)&s->cb[0])
#define mISDN_HEAD_PRIM(s)	((struct mISDNhead *)&s->cb[0])->prim
#define mISDN_HEAD_ID(s)	((struct mISDNhead *)&s->cb[0])->id
#define mISDN_HEAD_LEN(s)	((struct mISDNhead *)&s->cb[0])->len

/* socket states */
#define MISDN_OPEN	1
#define MISDN_BOUND	2
#define MISDN_CLOSED	3

struct mISDNchannel;
struct mISDNdevice;
struct mISDNstack;

struct channel_req {
	u_int			protocol;
	struct sockaddr_mISDN	adr;
	struct mISDNchannel	*ch;
};

typedef	int	(ctrl_func_t)(struct mISDNchannel *, u_int, void *);
typedef	int	(send_func_t)(struct mISDNchannel *, struct sk_buff *);
typedef int	(create_func_t)(struct channel_req *);

struct Bprotocol {
	struct list_head	list;
	char			*name;
	u_int			Bprotocols;
	create_func_t		*create;
};

struct mISDNchannel {
	struct list_head	list;
	u_int			protocol;
	u_int			nr;
	u_long			opt;
	u_int			addr;
	struct mISDNstack	*st;
	struct mISDNchannel	*peer;
	send_func_t		*send;
	send_func_t		*recv;
	ctrl_func_t		*ctrl;
};

struct mISDN_sock_list {
	struct hlist_head	head;
	rwlock_t		lock;
};

struct mISDN_sock {
	struct sock		sk;
	struct mISDNchannel	ch;
	struct mISDNdevice	*dev;
};

struct mISDNdevice {
	struct mISDNchannel	D;
	u_int			id;
	char			name[MISDN_MAX_IDLEN];
	u_int			Dprotocols;
	u_int			Bprotocols;
	u_int			nrbchan;
	struct list_head	bchannels;
	struct mISDNchannel	*teimgr;
	struct class_device	class_dev;
};

struct mISDNstack {
	u_long			status;
	struct mISDNdevice	*dev;
	struct task_struct	*thread;
	struct semaphore	*notify;
	wait_queue_head_t	workq;
	struct sk_buff_head	msgq;
	struct list_head	layer2;
	struct mISDNchannel	*layer1;
	struct mISDNchannel	own;
	struct semaphore	lsem;
	struct mISDN_sock_list	l1sock;
#ifdef MISDN_MSG_STATS
	u_int			msg_cnt;
	u_int			sleep_cnt;
	u_int			stopped_cnt;
#endif
};

/* global alloc/queue dunctions */

static inline struct sk_buff *
mI_alloc_skb(unsigned int len, gfp_t gfp_mask)
{
	struct sk_buff	*skb;

	skb = alloc_skb(len + MISDN_HEADER_LEN, gfp_mask);
	if (likely(skb))
		skb_reserve(skb, MISDN_HEADER_LEN);
	return skb;
}

static inline struct sk_buff *
_alloc_mISDN_skb(u_int prim, u_int id, u_int len, void *dp, gfp_t gfp_mask)
{
	struct sk_buff	*skb = mI_alloc_skb(len, gfp_mask);
	struct mISDNhead *hh;

	if (!skb)
		return NULL;
	if (len)
		memcpy(skb_put(skb, len), dp, len);
	hh = mISDN_HEAD_P(skb);
	hh->prim = prim;
	hh->id = id;
	hh->len = len;
	return skb;
}	

static inline void
_queue_data(struct mISDNchannel *ch, u_int prim,
    u_int id, u_int len, void *dp, gfp_t gfp_mask)
{
	struct sk_buff		*skb;

	if (!ch->peer)
		return;
	skb = _alloc_mISDN_skb(prim, id, len, dp, gfp_mask);
	if (!skb)
		return;
	if (ch->recv(ch->peer, skb))
		dev_kfree_skb(skb);
}

/* global register/unregister functions */

extern int	mISDN_register_device(struct mISDNdevice *);
extern void	mISDN_unregister_device(struct mISDNdevice *);
extern int	mISDN_register_Bprotocol(struct Bprotocol *);
extern void	mISDN_unregister_Bprotocol(struct Bprotocol *);

extern void	set_channel_address(struct mISDNchannel *, u_int, u_int);

#endif /* __KERNEL__ */
#endif /* mISDNIF_H */

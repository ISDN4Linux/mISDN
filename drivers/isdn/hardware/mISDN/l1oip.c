/*

 * l1oip.c  low level driver for tunneling layer 1 over IP
 *
 * NOTE: It is not compatible with TDMoIP nor "ISDN over IP".
 *
 * Author	Andreas Eversberg (jolly@eversberg.eu)
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

/* module parameters:
 * type:
	Value 1	= BRI
	Value 2	= PRI
	Value 3 = BRI (multi channel frame)
	Value 4 = PRI (multi channel frame)
	A multi channel frame reduces overhead to a single frame for all
       	b-channels, but increases delay.

 * codec:
	Value 0 = transparent

 * protocol:
 	Bit 0-3 = protocol
	Bit 4	= NT-Mode
	Bit 5	= PTP (instead of multipoint)

 * layermask:
	NOTE: Must be given for all ports, not for the number of cards.
	mask of layers to be used for D-channel stack

 * limit:
	limitation of B-channels to control bandwidth (1...29)

 * ip:
	byte representation of remote ip address (127.0.0.1 -> 127,0,0,1)
	If not given or four 0, no remote address is set.
	For multiple interfaces, concat ip addresses. (127.0.0.1,127,0,0,1)

 * port:
	port number (remote interface)
	If not given or 0, port 931 is used for fist instance, 932 for next...
	For multiple interfaces, different ports must be given.

 * ondemand:
	0 = fixed (always transmit packets, even when remote side timed out)
	1 = on demand (only transmit packets, when remote side is detected)
	the default is 0
	NOTE: ID must also be set for on demand.
 
 * id:
	optional value to identify frames. This value must be equal on both
	peers and should be random. If omitted or 0, no ID is transmitted.

 * debug:
	NOTE: only one debug value must be given for all cards
	enable debugging (see l1oip.h for debug options)


Special PH_CONTROL messages:

 dinfo = L1OIP_SETPEER*
 data bytes 0-3 : remote IP address in network order (left element first)
 data bytes 4-5 : local port in network order (high byte first)

 dinfo = L1OIP_UNSETPEER*

 * Use l1oipctrl for comfortable setting or removing ip address.
   (Layer 1 Over IP CTRL)
	

L1oIP-Protocol
--------------

Frame Header:

 7 6 5 4 3 2 1 0 7 6 5 4 3 2 1 0
+---+-----------+-+-|-----------+
|Ver|  Coding   |T|I|  Channel  |
+---+-----------+-+-|-----------+
|        Channel Map 3,2*       |
+-------------------------------+
|        Channel Map 1,0*       |
+-------------------------------+
|           Time Base           |
+-------------------------------+
|          ID byte 3,2*         |
+-------------------------------+
|          ID byte 1,0*         |
+-------------------------------+

* Only included in some cases.

- Ver = Version
Must be 1 for Version 1.

- Coding = Type of codec
Must be 0 for no transcoding. Also for D-channel and other HDLC frames.

- T = Type of interface
Must be 0 for S0 or 1 for E1.

- I = Id present
If bit is set, four ID bytes are included in frame.

- Channel = Channel number
0 reserved
1-3 channel data for S0 (3 is D-channel)
1-31 channel data for E1 (16 is D-channel)
32-62 reserved
63 see channel map

- Channel Map = Map of channels in this frame
The map is only included if the channel number is 63.
Each bit represents the existance of the equivalent channel number.
The codec must be equal for all channels and cannot be mixed with D-channel or HDLC.
The size of data must be a multiple of the included channels.
The time base must be equal for all channels.

- Time Base = Timestamp of first sample in frame
The "Time Base" is used to rearange packets and to detect packet loss.
The 16 bits are sent in network order (MSB first) and count 1/8000 th of a
second. This causes a wrap arround each 8,192 seconds. There is no requirement
for the initial "Time Base", but 0 should be used for the first packet.
In case of HDLC data, this timestamp counts the packet or byte number.

- ID = Connection ID
Additional ID to prevent Denial of Service attacs. Also it prevents hijacking
connections with dynamic IP. The ID should be random and must not be 0.


Two Timers:

After initialisation, a timer of 15 seconds is started. Whenever a packet is
transmitted, the timer is reset to 15 seconds again. If the timer expires, an
empty packet is transmitted. This keep the connection alive.

When a valid packet is received, a timer 65 seconds is started. The interface
become ACTIVE. If the timer expires, the interface becomes INACTIVE.


Dynamic IP handling:

To allow dynamic IP, the ID must be non 0. In this case, any packet with the
correct port number and ID will be accepted. If the remote side changes its IP
the new IP is used for all transmitted packets until it changes again.


On Demand:

If the ondemand parameter is given, the remote IP is set to 0 on timeout.
This will stop keepalive traffic to remote. If the remote is online again,
traffic will continue to the remote address. This is usefull for road warriors.
This feature only works with ID set, otherwhise it is highly unsecure.


Socket and Thread
-----------------

The complete socket opening and closing is done by a thread.
When the thread opened a socket, the hc->socket descriptor is set. Whenever a
packet shall be sent to the socket, the hc->socket must be checked wheter not
NULL. To prevent change in socket descriptor, the hc->socket_lock must be used.
To change the socket, a recall of l1oip_socket_open() will safely kill the
socket process and create a new one.

*/


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>

#include "core.h"
#include "channel.h"
#include "layer1.h"
#include "dsp.h"
#include "debug.h"
#include <linux/isdn_compat.h>
#include <linux/init.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <net/sock.h>

#include "l1oip.h"

static const char *l1oip_revision = "$Revision: 1.8 $";

static int l1oip_cnt = 0;

static mISDNobject_t	l1oip_obj;

static char l1oipName[] = "Layer1oIP";

/****************/
/* module stuff */
/****************/

#define MAX_CARDS	16
static u_int type[MAX_CARDS];
static u_int codec[MAX_CARDS];
static u_int protocol[MAX_CARDS];
static int layermask[MAX_CARDS];

static char *ip[MAX_CARDS];

static u_int port[MAX_CARDS*4];
static u_int ondemand[MAX_CARDS];
static u_int limit[MAX_CARDS];
static u_int id[MAX_CARDS];
static int debug;

#ifdef MODULE
MODULE_AUTHOR("Andreas Eversberg");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
module_param_array(type, uint, NULL, S_IRUGO | S_IWUSR);
module_param_array(codec, uint, NULL, S_IRUGO | S_IWUSR);
module_param_array(protocol, uint, NULL, S_IRUGO | S_IWUSR);
module_param_array(layermask, uint, NULL, S_IRUGO | S_IWUSR);
module_param_array(ip, charp, NULL, S_IRUGO | S_IWUSR);
module_param_array(port, uint, NULL, S_IRUGO | S_IWUSR);
module_param_array(ondemand, uint, NULL, S_IRUGO | S_IWUSR);
//module_param_array(limit, uint, NULL, S_IRUGO | S_IWUSR);
module_param_array(id, uint, NULL, S_IRUGO | S_IWUSR);
module_param(debug, uint, S_IRUGO | S_IWUSR);
#endif


/*
 * send a frame via socket, if open and restart timer
 */
static int
l1oip_socket_send(l1oip_t *hc, u8 codec, u8 channel, u32 chanmask, u16 timebase, u8 *buf, int len)
{
	mm_segment_t oldfs;
	u_long		flags;
	u8 frame[len+32]; /* add some space for the header */
	u8 *p = frame;
	struct socket *socket;

	if (debug & DEBUG_L1OIP_MSG)
		printk(KERN_DEBUG "%s: sending data to socket (len = %d)\n", __FUNCTION__, len);

	/* restart timer */
	if ((int)(hc->keep_tl.expires-jiffies) < 5*HZ) {
		del_timer(&hc->keep_tl);
		hc->keep_tl.expires = jiffies + L1OIP_KEEPALIVE*HZ;
		add_timer(&hc->keep_tl);
	} else
		hc->keep_tl.expires = jiffies + L1OIP_KEEPALIVE*HZ;

	if (debug & DEBUG_L1OIP_MSG)
		printk(KERN_DEBUG "%s: resetting timer\n", __FUNCTION__);

	/* drop if we have no remote ip */
	if (!hc->sin_remote.sin_addr.s_addr) {
		if (debug & DEBUG_L1OIP_MSG)
			printk(KERN_DEBUG "%s: dropping frame, because remote IP is not set.\n", __FUNCTION__);
		return(len);
	}

	/* assemble frame */
	*p++ = 0x40 + codec; /* version and coding */
	*p++ = (hc->pri?0x80:0x00) + (hc->id?0x40:0x00) + channel; /* type, id, channel */
	if (channel == 63) {
		*p++ = chanmask>>24; /* channel map */
		*p++ = chanmask>>16;
		*p++ = chanmask>>8;
		*p++ = chanmask;
	}
	*p++ = timebase>>8; /* time base */
	*p++ = timebase;
	if (hc->id) {
		*p++ = hc->id>>24; /* id */
		*p++ = hc->id>>16;
		*p++ = hc->id>>8;
		*p++ = hc->id;
	}
	if (buf && len)
		memcpy(p, buf, len); /* add data to frame */
	len += (int)(p-frame); /* add header size to length */

	/* check for socket in safe condition */
	spin_lock_irqsave(&hc->socket_lock, flags);
	if (hc->socket) {
		/* seize socket */
		socket = hc->socket;
		hc->socket = NULL;
		spin_unlock_irqrestore(&hc->socket_lock, flags);

		/* send packet */
		if (debug & DEBUG_L1OIP_MSG)
			printk(KERN_DEBUG "%s: sending packet to socket (len = %d)\n", __FUNCTION__, len);
		hc->sendiov.iov_base = frame;
		hc->sendiov.iov_len  = len;
		oldfs = get_fs();
		set_fs(KERNEL_DS);
		len = sock_sendmsg(socket, &hc->sendmsg, len);
		set_fs(oldfs);

		/* give socket back */
		hc->socket = socket; /* no locking required */
	} else
		spin_unlock_irqrestore(&hc->socket_lock, flags);

	return(len);
}


/*
 * receive channel data from socket
 */
static void
l1oip_socket_recv(l1oip_t *hc, u8 codec, u8 channel, u16 timebase, u8 *buf, int len)
{
	struct sk_buff *nskb;
	channel_t *ch;
	u8 *p;

	/* check empty frame */
	if (len == 0) {
		if (debug & DEBUG_L1OIP_MSG)
			printk(KERN_DEBUG "%s: receivd empty keepalive data, ignoring\n", __FUNCTION__);
		return;
	}

	if (debug & DEBUG_L1OIP_MSG)
		printk(KERN_DEBUG "%s: receivd data, sending to mISDN (%d)\n", __FUNCTION__, len);

	if (channel<1 || channel>31) {
		printk(KERN_WARNING "%s: packet error - channel %d out of range\n", __FUNCTION__, channel);
		return;
	}
	ch = hc->chan[channel].ch;
	if (!ch) {
		printk(KERN_WARNING "%s: packet error - channel %d not in stack\n", __FUNCTION__, channel);
		return;
	}

	/* send message */
	nskb = alloc_stack_skb(len, ch->up_headerlen);
	if (!nskb) {
		printk(KERN_ERR "%s: No mem for skb.\n", __FUNCTION__);
		return;
	}
	p = skb_put(nskb, len);
	memcpy(p, buf, len);
	queue_ch_frame(ch, INDICATION, MISDN_ID_ANY, nskb);

}


/*
 * parse frame and extract channel data
 */
static void
l1oip_socket_parse(l1oip_t *hc, struct sockaddr_in *sin, u8 *buf, int len)
{
	u32 id;
	u8 channel;
	u16 chanmask = 0;
	u8 codec;
	u16 timebase;
	int chancount, chunk, i;

	if (debug & DEBUG_L1OIP_MSG)
		printk(KERN_DEBUG "%s: receivd frame, parsing... (%d)\n", __FUNCTION__, len);

	/* check lenght */
	if (len < 2+2) {
		printk(KERN_WARNING "%s: packet error - length %d below 4 bytes\n", __FUNCTION__, len);
		return;
	}

	/* check version */
	if (*buf>>6 != 1) {
		printk(KERN_WARNING "%s: packet error - unknown version %d\n", __FUNCTION__, buf[0]>>6);
		return;
	}

	/* check coding */
	codec = *buf&0x3f;
	if (codec > 0) {
		printk(KERN_WARNING "%s: packet error - codec %d unsupported\n", __FUNCTION__, codec);
		return;
	}
	buf++;
	len--;
	
	/* check type */
	if (*buf>>7 && !hc->pri) {
		printk(KERN_WARNING "%s: packet error - received E1 packet on S0 interface\n", __FUNCTION__);
		return;
	}
	if (!(*buf>>7) && hc->pri) {
		printk(KERN_WARNING "%s: packet error - received S0 packet on E1 interface\n", __FUNCTION__);
		return;
	}

	/* get id flag and channel */
	id = (*buf>>6)&1;
	channel = *buf&0x3f;
	buf++;
	len--;

	/* check channel mask */	
	if (channel == 63) {
		if (len < 4+2) {
			printk(KERN_WARNING "%s: packet error - packet to short for channel map\n", __FUNCTION__);
			return;
		}
		chanmask = *buf++ << 24;
		chanmask += *buf++ << 16;
		chanmask += *buf++ << 8;
		chanmask += *buf++;
		len -= 4;
		if (!chanmask) {
			printk(KERN_WARNING "%s: packet error - channel map is empty\n", __FUNCTION__);
			return;
		}

	}

	/* get  time base */
	timebase = *buf++ << 8;
	timebase += *buf++;
	len -= 2;

	/* check id */
	if (id) {
		if (!hc->id) {
			printk(KERN_WARNING "%s: packet error - packet has id 0x%x, but we have not\n", __FUNCTION__, id);
			return;
		}
		if (len < 4) {
			printk(KERN_WARNING "%s: packet error - packet too short for ID value\n", __FUNCTION__);
			return;
		}
		id = *buf++ << 24;
		id += *buf++ << 16;
		id += *buf++ << 8;
		id += *buf++;
		len -= 4;

		if (id != hc->id) {
			printk(KERN_WARNING "%s: packet error - ID mismatch, got 0x%x, we 0x%x\n", __FUNCTION__, id, hc->id);
			return;
		}
	} else {
		if (hc->id) {
			printk(KERN_WARNING "%s: packet error - packet has no ID, but we have\n", __FUNCTION__);
			return;
		}
	}
	
	/* restart timer */
	if ((int)(hc->timeout_tl.expires-jiffies) < 5*HZ) {
		del_timer(&hc->timeout_tl);
		hc->timeout_tl.expires = jiffies + L1OIP_TIMEOUT*HZ;
		add_timer(&hc->timeout_tl);
	} else
		hc->timeout_tl.expires = jiffies + L1OIP_TIMEOUT*HZ;

	/* if inactive, we send up a PH_ACTIVATE and activate */
	if (!test_bit(FLG_ACTIVE, &hc->chan[hc->dch].ch->Flags)) {
		if (debug & DEBUG_L1OIP_MSG)
			printk(KERN_DEBUG "%s: interface become active due to received packet\n", __FUNCTION__);
		test_and_set_bit(FLG_ACTIVE, &hc->chan[hc->dch].ch->Flags);
		mISDN_queue_data(&hc->chan[hc->dch].ch->inst, FLG_MSG_UP, PH_ACTIVATE | INDICATION, 0, 0, NULL, 0);
		mISDN_queue_data(&hc->chan[hc->dch].ch->inst, hc->chan[hc->dch].ch->inst.id | MSG_BROADCAST,
			MGR_SHORTSTATUS | INDICATION, SSTATUS_L1_ACTIVATED, 0, NULL, 0);
	}

	/* if ip changes */
	if (hc->sin_remote.sin_addr.s_addr != sin->sin_addr.s_addr) {
		if (debug & DEBUG_L1OIP_SOCKET)
			printk(KERN_DEBUG "%s: remote IP address changes from 0x%08x to 0x%08x\n", __FUNCTION__, hc->sin_remote.sin_addr.s_addr, sin->sin_addr.s_addr);
		hc->sin_remote.sin_addr.s_addr = sin->sin_addr.s_addr;
	}
	
	/* in case of channel map */
	chancount = 0;
	if (chanmask) {
		/* count number of channels */
		i = 0;
		while(i < 32) {
			if (chanmask & (1<<i))
				chancount++;
			i++;
		}

		/* check if we have a remainder */
		if (len % chancount) {
			printk(KERN_WARNING "%s: packet error - packet has %d bytes, but cannot divided by %d channels\n", __FUNCTION__, len, chancount);
			return;
		}

		/* distribute packets */
		chunk = len/chancount;
		i = 0;
		while(i < 32) {
			if (chanmask & (1<<i)) {
				l1oip_socket_recv(hc, codec, channel, timebase, buf, chunk);
				buf += chunk;
			}
			i++;
		}
	} else {
		/* distribute packet */
		l1oip_socket_recv(hc, codec, channel, timebase, buf, len);
	}
}


/*
 * socket thread
 */
static int
l1oip_socket_thread(void *data)
{
	l1oip_t *hc = (l1oip_t *)data;
	int ret = 0;
	struct msghdr msg;
	struct iovec iov;
	mm_segment_t oldfs;
	unsigned long flags;
	struct sockaddr_in sin_rx;
	unsigned char recvbuf[1500];
	int recvlen;
	struct socket *socket = NULL;
	DECLARE_COMPLETION(wait);

	/* make daemon */
	daemonize(hc->name);
	allow_signal(SIGTERM);

	/* create socket */
	if (sock_create(PF_INET, SOCK_DGRAM, IPPROTO_UDP, &socket)) {
		printk(KERN_ERR "%s: Failed to create socket.\n", __FUNCTION__);
		return(-EIO);
	}

	/* set incoming address */
	hc->sin_local.sin_family = AF_INET;
	hc->sin_local.sin_addr.s_addr = INADDR_ANY;
	hc->sin_local.sin_port = htons((unsigned short)hc->remoteport); /* local and remote port are equal */

	/* set outgoing address */
	hc->sin_remote.sin_family = AF_INET;
	hc->sin_remote.sin_addr.s_addr = htonl((hc->remoteip[0]<<24) + (hc->remoteip[1]<<16) + (hc->remoteip[2]<<8) + hc->remoteip[3]);  
	hc->sin_remote.sin_port = htons((unsigned short)hc->remoteport);
	
	/* bind to incomming port */
	if (socket->ops->bind(socket, (struct sockaddr *)&hc->sin_local, sizeof(hc->sin_local))) {
		printk(KERN_ERR "%s: Failed to bind socket to port %d.\n", __FUNCTION__, hc->remoteport);
		ret = -EINVAL;
		goto fail;
	}

	/* check sk */
	if (socket->sk == NULL) {
		printk(KERN_ERR "%s: socket->sk == NULL\n", __FUNCTION__);
		ret = -EIO;
		goto fail;
	}
	
	/* build receive message */
	msg.msg_name = &sin_rx;
	msg.msg_namelen = sizeof(sin_rx);
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	/* build send message */
	hc->sendmsg.msg_name = &hc->sin_remote;
	hc->sendmsg.msg_namelen = sizeof(hc->sin_remote);
	hc->sendmsg.msg_control = NULL;
	hc->sendmsg.msg_controllen = 0;
	hc->sendmsg.msg_iov    = &hc->sendiov;
	hc->sendmsg.msg_iovlen = 1;

	/* give away socket */
	spin_lock_irqsave(&hc->socket_lock, flags);
	hc->socket = socket;
	spin_unlock_irqrestore(&hc->socket_lock, flags);

	/* read loop */
	if (debug & DEBUG_L1OIP_SOCKET)
		printk(KERN_DEBUG "%s: socket created and open\n", __FUNCTION__);
	while(!signal_pending(current)) {
		iov.iov_base = recvbuf;
		iov.iov_len = sizeof(recvbuf);
		oldfs = get_fs();
		set_fs(KERNEL_DS);
		recvlen = sock_recvmsg(socket, &msg, sizeof(recvbuf), 0);
		set_fs(oldfs);
		if (recvlen>0) {
			l1oip_socket_parse(hc, &sin_rx, recvbuf, recvlen);
		} else {
			printk(KERN_WARNING "%s: broken pipe on socket\n", __FUNCTION__);
		}
	}

	/* get socket back, check first if in use, maybe by send function */
	spin_lock_irqsave(&hc->socket_lock, flags);
	while (!hc->socket) { /* if hc->socket is NULL, it is in use until it is given back */
		spin_unlock_irqrestore(&hc->socket_lock, flags);
		schedule_timeout(HZ/10);
		spin_lock_irqsave(&hc->socket_lock, flags);
	}
	hc->socket = NULL;
	spin_unlock_irqrestore(&hc->socket_lock, flags);

	if (debug & DEBUG_L1OIP_SOCKET)
		printk(KERN_DEBUG "%s: socket thread terminating\n", __FUNCTION__);

fail:
	/* close socket */
	if (socket) {
		sock_release(socket);
	}

	/* if we got killed, signal completion */
	complete(&hc->socket_complete);
	hc->socket_pid = 0; /* show termination of thread */

	if (debug & DEBUG_L1OIP_SOCKET)
		printk(KERN_DEBUG "%s: socket thread terminated\n", __FUNCTION__);
	return(ret);
}


/*
 * close socket
 */
static void
l1oip_socket_close(l1oip_t *hc)
{
	/* kill thread */
	if (hc->socket_pid) {
		if (debug & DEBUG_L1OIP_SOCKET)
			printk(KERN_DEBUG "%s: socket thread exists, kílling...\n", __FUNCTION__);
		kill_proc(hc->socket_pid, SIGTERM, 0);
		wait_for_completion(&hc->socket_complete);
	}
}


/*
 * init socket
 */
static int
l1oip_socket_open(l1oip_t *hc)
{
	/* in case of reopen, we need to close first */
	l1oip_socket_close(hc);
	
	init_completion(&hc->socket_complete);

	/* create receive process */
	if ((hc->socket_pid = kernel_thread(l1oip_socket_thread, hc, CLONE_KERNEL)) < 0) {
		printk(KERN_ERR "%s: Failed to create socket process.\n", __FUNCTION__);
		sock_release(hc->socket);
		return(-EIO);
	}
	if (debug & DEBUG_L1OIP_SOCKET)
		printk(KERN_DEBUG "%s: socket thread created\n", __FUNCTION__);

	return(0);
}


/*
 * keepalife timer expires
 */
static void
l1oip_keepalive(void *data)
{
	l1oip_t *hc = (l1oip_t *)data;

	if (debug & DEBUG_L1OIP_MSG)
		printk(KERN_DEBUG "%s: keepalive timer expired, sending empty frame on dchannel\n", __FUNCTION__);

	/* send an empty l1oip frame at D-channel */
	l1oip_socket_send(hc, 0, hc->dch, 0, 0, NULL, 0);
}


/*
 * timeout timer expires
 */
static void
l1oip_timeout(void *data)
{
	l1oip_t *hc = (l1oip_t *)data;

	if (debug & DEBUG_L1OIP_MSG)
		printk(KERN_DEBUG "%s: timeout timer expired, turn layer one down.\n", __FUNCTION__);

	/* if timeout, we send up a PH_DEACTIVATE and deactivate */
	if (test_bit(FLG_ACTIVE, &hc->chan[hc->dch].ch->Flags)) {
		if (debug & DEBUG_L1OIP_MSG)
			printk(KERN_DEBUG "%s: interface become deactivated due to timeout\n", __FUNCTION__);
		test_and_clear_bit(FLG_ACTIVE, &hc->chan[hc->dch].ch->Flags);
		mISDN_queue_data(&hc->chan[hc->dch].ch->inst, FLG_MSG_UP, PH_DEACTIVATE | INDICATION, 0, 0, NULL, 0);
		mISDN_queue_data(&hc->chan[hc->dch].ch->inst, hc->chan[hc->dch].ch->inst.id | MSG_BROADCAST,
			MGR_SHORTSTATUS | INDICATION, SSTATUS_L1_DEACTIVATED, 0, NULL, 0);
	}

	/* if we have ondemand set, we remove ip address */
	if (hc->ondemand) {
		if (debug & DEBUG_L1OIP_MSG)
			printk(KERN_DEBUG "%s: on demand causes ip address to be removed\n", __FUNCTION__);
		hc->sin_remote.sin_addr.s_addr = 0;
	}
}


/*
 * message transfer from layer 2
 */
static int
l1oip_channel(mISDNinstance_t *inst, struct sk_buff *skb)
{
	channel_t	*ch = container_of(inst, channel_t, inst);
	l1oip_t		*hc;
	int		ret = 0;
	mISDN_head_t	*hh;
	int		i, l, ll;
	u8		*p;

	hh = mISDN_HEAD_P(skb);
	hc = ch->inst.privat;

	if (ch->channel > hc->numbch+1 || ch->channel < 1) {
		if (debug & DEBUG_L1OIP_MSG)
			printk(KERN_DEBUG "%s: channel out of range, interface uses channels 1-%d, but message has %d\n", __FUNCTION__, hc->numbch+1, ch->channel);
		return(-EINVAL);
	}

	if (hh->prim == PH_DATA_REQ || hh->prim == DL_DATA_REQ) {
		/* check oversize */
		if (skb->len <= 0) {
			printk(KERN_WARNING "%s: skb too small\n", __FUNCTION__);
			return(-EINVAL);
		}
		if (skb->len > MAX_DFRAME_LEN_L1 || skb->len > L1OIP_MAX_LEN) {
			printk(KERN_WARNING "%s: skb too large\n", __FUNCTION__);
			return(-EINVAL);
		}

		/* check for AIS */
		if (ch->channel != hc->dch) {
			if (debug & DEBUG_L1OIP_MSG)
				printk(KERN_DEBUG "%s: bchannel-data\n", __FUNCTION__);
			p = skb->data;
			l = skb->len;
			i = 0;
			while(i < l) {
				if (*p++ != 0xff)
					break;
				i++;
			}
			if (i == l) {
				if (debug & DEBUG_L1OIP_MSG)
					printk(KERN_DEBUG "%s: got AIS, not sending, but counting\n", __FUNCTION__);
				hc->chan[ch->channel].counter += l;
				goto confirm;
			}
		}

		/* send frame */
		p = skb->data;
		l = skb->len;
		while(l) {
			ll = (l<L1OIP_MAX_PERFRAME)?l:L1OIP_MAX_PERFRAME;
			if (ch->channel == hc->dch) {
				/* dchannel */
				l1oip_socket_send(hc, 0, ch->channel, 0, hc->chan[ch->channel].counter++, p, ll);
			} else {
				/* bchannel */
				l1oip_socket_send(hc, 0, ch->channel, 0, hc->chan[ch->channel].counter, p, ll);
				hc->chan[ch->channel].counter += ll;
			}
			p += ll;
			l -= ll;
		}
		confirm:
		skb_trim(skb, 0);
		queue_ch_frame(ch, CONFIRM, hh->dinfo, skb);
		return(0);
	} else if (hh->prim == (PH_CONTROL | REQUEST)) {
		if (ch->channel != hc->dch) {
			printk(KERN_WARNING "%s: unknown PH_CONTROL info %x for bchannel\n", __FUNCTION__, hh->dinfo);
			ret = -EINVAL;
		} else switch (hh->dinfo) {
			case L1OIP_SETPEER:
			if (skb->len < 6) {
				ret = -EINVAL;
				break;
			}
			memcpy(hc->remoteip, skb->data, 4);
			hc->remoteport = (skb->data[4]<<8) + skb->data[5];
			if (debug & DEBUG_L1OIP_SOCKET)
				printk(KERN_DEBUG "%s: got new ip address from user space.\n", __FUNCTION__);
			l1oip_socket_open(hc);
			return(mISDN_queueup_newhead(inst, 0, PH_CONTROL | CONFIRM, L1OIP_SETPEER, skb));
			break;

			case L1OIP_UNSETPEER:
			if (debug & DEBUG_L1OIP_SOCKET)
				printk(KERN_DEBUG "%s: removing ip address.\n", __FUNCTION__);
			memset(hc->remoteip, 0, 4);
			l1oip_socket_open(hc);
			return(mISDN_queueup_newhead(inst, 0, PH_CONTROL | CONFIRM, L1OIP_UNSETPEER, skb));
			break;

			default:
			printk(KERN_WARNING "%s: unknown PH_CONTROL info %x for dchannel\n", __FUNCTION__, hh->dinfo);
			ret = -EINVAL;
		}
	} else if (hh->prim == (PH_ACTIVATE | REQUEST)) {
		if (debug & DEBUG_L1OIP_MSG)
			printk(KERN_DEBUG "%s: PH_ACTIVATE channel %d (1..%d)\n", __FUNCTION__, ch->channel, hc->numbch+1);
		if (ch->channel == hc->dch) {
			if (test_bit(FLG_ACTIVE, &ch->Flags))
				return(mISDN_queueup_newhead(inst, 0, PH_ACTIVATE | INDICATION, 0, skb));
			else
				return(mISDN_queueup_newhead(inst, 0, PH_DEACTIVATE | INDICATION, 0, skb));
		}
		test_and_set_bit(FLG_ACTIVE, &ch->Flags);		
		return(mISDN_queueup_newhead(inst, 0, PH_ACTIVATE | CONFIRM, 0, skb));
	} else if (hh->prim == (PH_DEACTIVATE | REQUEST)) {
		if (debug & DEBUG_L1OIP_MSG)
			printk(KERN_DEBUG "%s: PH_DEACTIVATE channel %d (1..%d)\n", __FUNCTION__, ch->channel, hc->numbch+1);
		if (ch->channel == hc->dch) {
			if (test_bit(FLG_ACTIVE, &ch->Flags))
				return(mISDN_queueup_newhead(inst, 0, PH_ACTIVATE | INDICATION, 0, skb));
			else
				return(mISDN_queueup_newhead(inst, 0, PH_DEACTIVATE | INDICATION, 0, skb));
		}
		test_and_clear_bit(FLG_ACTIVE, &ch->Flags);		
		return(mISDN_queueup_newhead(inst, 0, PH_DEACTIVATE | CONFIRM, 0, skb));
	} else if (hh->prim == (DL_ESTABLISH | REQUEST)) {
		if (debug & DEBUG_L1OIP_MSG)
			printk(KERN_DEBUG "%s: DL_ESTABLISH channel %d (1..%d)\n", __FUNCTION__, ch->channel, hc->numbch+1);
		if (ch->channel == hc->dch) {
			printk(KERN_DEBUG "%s: ERROR - DL_ESTABLISH on D-channel?\n", __FUNCTION__);
			return(-EINVAL);
		}
		test_and_set_bit(FLG_ACTIVE, &ch->Flags);		
		return(mISDN_queueup_newhead(inst, 0, DL_ESTABLISH | CONFIRM, 0, skb));
	} else if (hh->prim == (DL_RELEASE | REQUEST)) {
		if (debug & DEBUG_L1OIP_MSG)
			printk(KERN_DEBUG "%s: DL_RELEASE channel %d (1..%d)\n", __FUNCTION__, ch->channel, hc->numbch+1);
		if (ch->channel == hc->dch) {
			printk(KERN_DEBUG "%s: ERROR - DL_RELEASE on D-channel?\n", __FUNCTION__);
			return(-EINVAL);
		}
		test_and_clear_bit(FLG_ACTIVE, &ch->Flags);		
		return(mISDN_queueup_newhead(inst, 0, DL_RELEASE | CONFIRM, 0, skb));
        } else if ((hh->prim & MISDN_CMD_MASK) == MGR_SHORTSTATUS) {
		u_int           temp = hh->dinfo & SSTATUS_ALL; // remove SSTATUS_BROADCAST_BIT
		if (temp == SSTATUS_ALL || temp == SSTATUS_L1) {
			if (hh->dinfo & SSTATUS_BROADCAST_BIT)
				temp = ch->inst.id | MSG_BROADCAST;
			else
				temp = hh->addr | FLG_MSG_TARGET;
			skb_trim(skb, 0);
			hh->dinfo = test_bit(FLG_ACTIVE, &ch->Flags) ?
				SSTATUS_L1_ACTIVATED : SSTATUS_L1_DEACTIVATED;
			hh->prim = MGR_SHORTSTATUS | CONFIRM;
			return(mISDN_queue_message(&ch->inst, temp, skb));
		}
		ret = -EOPNOTSUPP;
	} else {
		if (debug & DEBUG_L1OIP_MSG)
			printk(KERN_DEBUG "%s: unknown prim %x\n", __FUNCTION__, hh->prim);
		ret = -EINVAL;
	}
	if (!ret) {
		dev_kfree_skb(skb);
	}
	return(ret);
}


/*
 * MGR stuff
 */
static int
l1oip_manager(void *data, u_int prim, void *arg)
{
	l1oip_t		*hc;
	mISDNinstance_t	*inst = data;
	struct sk_buff	*skb;
	channel_t	*chan = NULL;
	int		ch = -1;
	int		i;
	u_long		flags;

	if (!data) {
		MGR_HASPROTOCOL_HANDLER(prim,arg,&l1oip_obj)
		printk(KERN_ERR "%s: no data prim %x arg %p\n", __FUNCTION__, prim, arg);
		return(-EINVAL);
	}

	/* find channel and card */
	spin_lock_irqsave(&l1oip_obj.lock, flags);
	list_for_each_entry(hc, &l1oip_obj.ilist, list) {
		i = 0;
		while(i < 32) {
//printk(KERN_DEBUG "comparing (D-channel) card=%08x inst=%08x with inst=%08x\n", hc, &hc->dch[i].inst, inst);
			if ((hc->chan[i].ch) &&
				(&hc->chan[i].ch->inst == inst)) {
				ch = i;
				chan = hc->chan[i].ch;
				break;
			}
			i++;
		}
		if (ch >= 0)
			break;
	}
	spin_unlock_irqrestore(&l1oip_obj.lock, flags);
	if (ch < 0) {
		printk(KERN_ERR "%s: no card/channel found  data %p prim %x arg %p\n", __FUNCTION__, data, prim, arg);
		return(-EINVAL);
	}
	if (debug & DEBUG_L1OIP_MGR)
		printk(KERN_DEBUG "%s: channel %d (0..31)  data %p prim %x arg %p\n", __FUNCTION__, ch, data, prim, arg);

	switch(prim) {
		case MGR_REGLAYER | CONFIRM:
		if (debug & DEBUG_L1OIP_MGR)
			printk(KERN_DEBUG "%s: MGR_REGLAYER\n", __FUNCTION__);
		mISDN_setpara(chan, &inst->st->para);
		break;

		case MGR_UNREGLAYER | REQUEST:
		if (debug & DEBUG_L1OIP_MGR)
			printk(KERN_DEBUG "%s: MGR_UNREGLAYER\n", __FUNCTION__);
		i = test_bit(FLG_DCHANNEL, &chan->Flags) ? HW_DEACTIVATE : 0;
		if ((skb = create_link_skb(PH_CONTROL | REQUEST, i, 0, NULL, 0))) {
			if (l1oip_channel(inst, skb))
				dev_kfree_skb(skb);
		}
		mISDN_ctrl(inst, MGR_UNREGLAYER | REQUEST, NULL);
		break;

		case MGR_CLRSTPARA | INDICATION:
		arg = NULL;
		// fall through
		case MGR_ADDSTPARA | INDICATION:
		if (debug & DEBUG_L1OIP_MGR)
			printk(KERN_DEBUG "%s: MGR_***STPARA\n", __FUNCTION__);
		mISDN_setpara(chan, arg);
		break;

		case MGR_RELEASE | INDICATION:
		if (debug & DEBUG_L1OIP_MGR)
			printk(KERN_DEBUG "%s: MGR_RELEASE = remove port from mISDN\n", __FUNCTION__);
		break;
#ifdef FIXME
		case MGR_CONNECT | REQUEST:
		if (debug & DEBUG_L1OIP_MGR)
			printk(KERN_DEBUG "%s: MGR_CONNECT\n", __FUNCTION__);
		return(mISDN_ConnectIF(inst, arg));

		case MGR_SETIF | REQUEST:
		case MGR_SETIF | INDICATION:
		if (debug & DEBUG_L1OIP_MGR)
			printk(KERN_DEBUG "%s: MGR_SETIF\n", __FUNCTION__);
		if (dch)
			return(mISDN_SetIF(inst, arg, prim, l1oip_channel, NULL, dch));
		if (bch)
			return(mISDN_SetIF(inst, arg, prim, l1oip_channel, NULL, bch));
		break;

		case MGR_DISCONNECT | REQUEST:
		case MGR_DISCONNECT | INDICATION:
		if (debug & DEBUG_L1OIP_MGR)
			printk(KERN_DEBUG "%s: MGR_DISCONNECT\n", __FUNCTION__);
		return(mISDN_DisConnectIF(inst, arg));
#endif
		case MGR_SELCHANNEL | REQUEST:
		if (debug & DEBUG_L1OIP_MGR)
			printk(KERN_DEBUG "%s: MGR_SELCHANNEL\n", __FUNCTION__);
		if (!test_bit(FLG_DCHANNEL, &chan->Flags)) {
			printk(KERN_WARNING "%s(MGR_SELCHANNEL|REQUEST): selchannel not dinst\n", __FUNCTION__);
			return(-EINVAL);
		}
		return(-EINVAL);
		//return(SelFreeBChannel(hc, ch, arg));

		case MGR_SETSTACK | INDICATION:
		if (debug & DEBUG_L1OIP_MGR)
			printk(KERN_DEBUG "%s: MGR_SETSTACK\n", __FUNCTION__);
		if (test_bit(FLG_BCHANNEL, &chan->Flags) && inst->pid.global==2) {
			if ((skb = create_link_skb(PH_ACTIVATE | REQUEST, 0, 0, NULL, 0))) {
				if (l1oip_channel(inst, skb))
					dev_kfree_skb(skb);
			}
			if (inst->pid.protocol[2] == ISDN_PID_L2_B_TRANS)
				mISDN_queue_data(inst, FLG_MSG_UP, DL_ESTABLISH | INDICATION, 0, 0, NULL, 0);
			else
				mISDN_queue_data(inst, FLG_MSG_UP, PH_ACTIVATE | INDICATION, 0, 0, NULL, 0);
		}
		break;

		PRIM_NOT_HANDLED(MGR_CTRLREADY | INDICATION);
		PRIM_NOT_HANDLED(MGR_GLOBALOPT | REQUEST);
		default:
		printk(KERN_WARNING "%s: prim %x not handled\n", __FUNCTION__, prim);
		return(-EINVAL);
	}
	return(0);
}


/* remove card from stack */

static void
release_card(l1oip_t *hc)
{
	int	i = 0;
	u_long	flags;

	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: entered\n", __FUNCTION__);

	/* free socket */
	l1oip_socket_close(hc);

	/* remove keepalive timer */
	if (timer_pending(&hc->keep_tl))
		del_timer(&hc->keep_tl);

	/* remove timeout timer */
	if (timer_pending(&hc->timeout_tl))
		del_timer(&hc->timeout_tl);

	/* close socket, if open */
	if (hc->socket_pid)
		l1oip_socket_close(hc);
	
	if (hc->chan[hc->dch].ch) {
		if (debug & DEBUG_L1OIP_INIT)
			printk(KERN_DEBUG "%s: free D-channel %d (1..32)\n", __FUNCTION__, hc->dch);
		mISDN_freechannel(hc->chan[hc->dch].ch);
		mISDN_ctrl(&hc->chan[hc->dch].ch->inst, MGR_UNREGLAYER | REQUEST, NULL);
		kfree(hc->chan[hc->dch].ch);
		hc->chan[hc->dch].ch = NULL;
	}
	i = 0;
	while(i < 32) {
		if (hc->chan[i].ch && i!=hc->dch) {
			if (debug & DEBUG_L1OIP_INIT)
				printk(KERN_DEBUG "%s: free B-channel %d (1..32)\n", __FUNCTION__, i);
			mISDN_freechannel(hc->chan[i].ch);
			kfree(hc->chan[i].ch);
			hc->chan[i].ch = NULL;
		}
		i++;
	}

	/* remove us from list and delete */
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_WARNING "%s: remove instance from list\n", __FUNCTION__);
	spin_lock_irqsave(&l1oip_obj.lock, flags);
	list_del(&hc->list);
	spin_unlock_irqrestore(&l1oip_obj.lock, flags);
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_WARNING "%s: delete instance\n", __FUNCTION__);
	kfree(hc);
	l1oip_cnt--;
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_WARNING "%s: card successfully removed\n", __FUNCTION__);
}


/*
 * cleanup module
 */
static void __exit
l1oip_cleanup(void)
{
	l1oip_t *hc,*next;
	int err;

	/* unregister mISDN object */
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: entered (refcnt = %d l1oip_cnt = %d)\n", __FUNCTION__, l1oip_obj.refcnt, l1oip_cnt);
	if ((err = mISDN_unregister(&l1oip_obj))) {
		printk(KERN_ERR "Can't unregister L1oIP error(%d)\n", err);
	}

	/* remove remaining devices, but this should never happen */
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: now checking ilist (refcnt = %d)\n", __FUNCTION__, l1oip_obj.refcnt);

	list_for_each_entry_safe(hc, next, &l1oip_obj.ilist, list) {
		printk(KERN_ERR "L1oIP devices struct not empty refs %d\n", l1oip_obj.refcnt);
		release_card(hc);
	}
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: done (refcnt = %d l1oip_cnt = %d)\n", __FUNCTION__, l1oip_obj.refcnt, l1oip_cnt);
}


/*
 * module and stack init
 */
static int __init
l1oip_init(void)
{
	int		i;
	int		pri, bundle;
	l1oip_t		*hc;
	char		tmpstr[64];
	channel_t	*dch, *bch;
	int		ch;
	int		ret_err = -EIO, err;
	u_long		flags;
	mISDN_pid_t     pid;
	mISDNstack_t    *dst = NULL; /* make gcc happy */

#if !defined(MODULE)
#error	"CONFIG_MODULE is not defined."
#endif
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: init entered\n", __FUNCTION__);

	strcpy(tmpstr, l1oip_revision);
	printk(KERN_INFO "mISDN: Layer-1-over-IP driver Rev. %s\n", mISDN_getrev(tmpstr));

	memset(&l1oip_obj, 0, sizeof(l1oip_obj));
#ifdef MODULE
	l1oip_obj.owner = THIS_MODULE;
#endif
	spin_lock_init(&l1oip_obj.lock);
	INIT_LIST_HEAD(&l1oip_obj.ilist);
	l1oip_obj.name = l1oipName;
	l1oip_obj.own_ctrl = l1oip_manager;
	l1oip_obj.DPROTO.protocol[0] = ISDN_PID_L0_TE_S0 | ISDN_PID_L0_NT_S0
				| ISDN_PID_L0_TE_E1 | ISDN_PID_L0_NT_E1;
	l1oip_obj.DPROTO.protocol[1] = ISDN_PID_L1_TE_S0 | ISDN_PID_L1_NT_S0
				| ISDN_PID_L1_TE_E1 | ISDN_PID_L1_NT_E1;
	l1oip_obj.BPROTO.protocol[1] = ISDN_PID_L1_B_64TRANS | ISDN_PID_L1_B_64HDLC;
	l1oip_obj.BPROTO.protocol[2] = ISDN_PID_L2_B_TRANS | ISDN_PID_L2_B_RAWDEV;

	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: registering l1oip_obj\n", __FUNCTION__);
	if ((err = mISDN_register(&l1oip_obj))) {
		printk(KERN_ERR "Can't register L1oIP error(%d)\n", err);
		return(err);
	}
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: new mISDN object (refcnt = %d)\n", __FUNCTION__, l1oip_obj.refcnt);

	l1oip_cnt = 0;

next_card:
	/* check card type */
	switch (type[l1oip_cnt] & 0xff) {
		case 1:
		pri = 0;
		bundle = 0;
		break;

		case 2:
		pri = 1;
		bundle = 0;
		break;

		case 3:
		pri = 0;
		bundle = 1;
		break;

		case 4:
		pri = 1;
		bundle = 1;
		break;

		case 0:
		printk(KERN_INFO "%d virtual devices registered\n", l1oip_cnt);

		return(0);

		default:
		printk(KERN_ERR "Card type(%d) not supported.\n", type[l1oip_cnt] & 0xff);
		ret_err = -EINVAL;
		goto free_object;
	}
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: interface %d is %s with %s.\n", __FUNCTION__, l1oip_cnt, pri?"PRI":"BRI", bundle?"bundled IP packet for all B-channels":"seperate IP packets for every B-channel");


	/* allocate card+fifo structure */
	if (!(hc = kmalloc(sizeof(l1oip_t), GFP_ATOMIC))) {
		printk(KERN_ERR "No kmem for L1-over-IP driver.\n");
		ret_err = -ENOMEM;
		goto free_object;
	}
	memset(hc, 0, sizeof(l1oip_t));
	hc->socket_lock = SPIN_LOCK_UNLOCKED;
	hc->idx = l1oip_cnt;
	hc->pri = pri;
	hc->dch = pri?16:3;
	hc->numbch = pri?30:2;
	hc->bundle = bundle;

	if (hc->pri)
		sprintf(hc->name, "L1oIP-E1#%d", l1oip_cnt+1);
	else
		sprintf(hc->name, "L1oIP-S0#%d", l1oip_cnt+1);

	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: (after APPEND_TO_LIST)\n", __FUNCTION__);
	
	spin_lock_irqsave(&l1oip_obj.lock, flags);
	list_add_tail(&hc->list, &l1oip_obj.ilist);
	spin_unlock_irqrestore(&l1oip_obj.lock, flags);
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: (after APPEND_TO_LIST)\n", __FUNCTION__);

	spin_lock_init(&hc->dummylock);
	spin_lock_init(&hc->socket_lock);

	/* check codec */
	switch (codec[l1oip_cnt]) {
		case 0:
		break;

		default:
		printk(KERN_ERR "Codec(%d) not supported.\n", codec[l1oip_cnt]);
		ret_err = -EINVAL;
		goto free_channels;
	}
	hc->codec = codec[l1oip_cnt];
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: using codec %d\n", __FUNCTION__, hc->codec);

	/* check ID */
	if (id[l1oip_cnt] == 0) {
		printk(KERN_WARNING "Warning: No 'id' value given or 0, this is highly unsecure. Please use 32 bit randmom number 0x...\n");
	}
	hc->id = id[l1oip_cnt];
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: using id 0x%x\n", __FUNCTION__, hc->id);

	/* check on demand */
	hc->ondemand = ondemand[l1oip_cnt];
	if (hc->ondemand && !hc->id) {
		printk(KERN_ERR "%s: ondemand option only allowed in conjunction with non 0 ID\n", __FUNCTION__);
		ret_err = -EINVAL;
		goto free_channels;
	}
	
	/* check protocol */
	if (protocol[l1oip_cnt] == 0) {
		printk(KERN_ERR "No 'protocol' value given.\n");
		ret_err = -EINVAL;
		goto free_channels;
	}

	/* set bchannel limit */
	hc->limit = limit[l1oip_cnt];

	/* set remote ip, remote port, local port */
	char *p=ip[l1oip_cnt];
	char *ipb;
	
	ipb=strsep(&p,".");	
	if (ipb) hc->remoteip[0] = simple_strtol(ipb,NULL,10);

	ipb=strsep(&p,".");	
	if (ipb) hc->remoteip[1] = simple_strtol(ipb,NULL,10);

	ipb=strsep(&p,".");	
	if (ipb) hc->remoteip[2] = simple_strtol(ipb,NULL,10);

	ipb=strsep(&p,".");	
	if (ipb) hc->remoteip[3] = simple_strtol(ipb,NULL,10);

	hc->remoteport = port[l1oip_cnt]?:(L1OIP_DEFAULTPORT+l1oip_cnt);
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: using remote ip %d.%d.%d.%d port %d ondemand %d\n", __FUNCTION__, hc->remoteip[0], hc->remoteip[1], hc->remoteip[2], hc->remoteip[3], hc->remoteport, hc->ondemand);
	
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: Registering D-channel, card(%d) protocol(%x)\n", __FUNCTION__, l1oip_cnt+1, protocol[l1oip_cnt]);
	dch = kmalloc(sizeof(channel_t), GFP_ATOMIC);
	if (!dch) {
		ret_err = -ENOMEM;
		goto free_channels;
	}
	memset(dch, 0, sizeof(channel_t));
	dch->channel = hc->dch;
	//dch->debug = debug;
	dch->inst.obj = &l1oip_obj;
	dch->inst.hwlock = &hc->dummylock;
	mISDN_init_instance(&dch->inst, &l1oip_obj, hc, l1oip_channel);
	dch->inst.pid.layermask = ISDN_LAYER(0);
	sprintf(dch->inst.name, "L1OIP%d", l1oip_cnt);
//	if (!(hc->chan[ch].rx_buf = kmalloc(MAX_DFRAME_LEN_L1, GFP_ATOMIC))) {
//		ret_err = -ENOMEM;
//		goto free_channels;
//	}
	if (mISDN_initchannel(dch, MSK_INIT_DCHANNEL, L1OIP_MAX_LEN)) {
		ret_err = -ENOMEM;
		goto free_channels;
	}
	hc->chan[hc->dch].ch = dch;

	i=0;
	while(i < hc->numbch) {
		ch = i + 1 + (i>=(hc->dch-1)); /* skip dchannel number */
		if (debug & DEBUG_L1OIP_INIT)
			printk(KERN_DEBUG "%s: Registering B-channel, card(%d) channel(%d)\n", __FUNCTION__, l1oip_cnt+1, ch);
		bch = kmalloc(sizeof(channel_t), GFP_ATOMIC);
		if (!bch) {
			ret_err = -ENOMEM;
			goto free_channels;
		}
		memset(bch, 0, sizeof(channel_t));
		bch->channel = ch;
		mISDN_init_instance(&bch->inst, &l1oip_obj, hc, l1oip_channel);
		bch->inst.pid.layermask = ISDN_LAYER(0);
		bch->inst.hwlock = &hc->dummylock;
		//bch->debug = debug;
		sprintf(bch->inst.name, "%s B%d",
			dch->inst.name, i+1);
		if (mISDN_initchannel(bch, MSK_INIT_BCHANNEL, L1OIP_MAX_LEN)) {
			kfree(bch);
			ret_err = -ENOMEM;
			goto free_channels;
		}
		hc->chan[ch].ch = bch;
#ifdef FIXME  // TODO
		if (bch->dev) {
			bch->dev->wport.pif.func = l1oip_channel;
			bch->dev->wport.pif.fdata = bch;
		}
#endif
		i++;
	}

	/* set D-channel */
	mISDN_set_dchannel_pid(&pid, protocol[l1oip_cnt], layermask[l1oip_cnt]);

	/* set PRI */
	if (hc->pri) {
		if (layermask[l1oip_cnt] & ISDN_LAYER(2)) {
			pid.protocol[2] |= ISDN_PID_L2_DF_PTP;
		}
		if (layermask[l1oip_cnt] & ISDN_LAYER(3)) {
			pid.protocol[3] |= ISDN_PID_L3_DF_PTP;
			pid.protocol[3] |= ISDN_PID_L3_DF_EXTCID;
			pid.protocol[3] |= ISDN_PID_L3_DF_CRLEN2;
		}
	}

	/* set protocol type */
	if (protocol[l1oip_cnt] & 0x10) {
		/* NT-mode */
		dch->inst.pid.protocol[0] = (hc->pri)?ISDN_PID_L0_NT_E1:ISDN_PID_L0_NT_S0;
		dch->inst.pid.protocol[1] = (hc->pri)?ISDN_PID_L1_NT_E1:ISDN_PID_L1_NT_S0;
		pid.protocol[0] = (hc->pri)?ISDN_PID_L0_NT_E1:ISDN_PID_L0_NT_S0;
		pid.protocol[1] = (hc->pri)?ISDN_PID_L1_NT_E1:ISDN_PID_L1_NT_S0;
		if (layermask[l1oip_cnt] & ISDN_LAYER(2))
			pid.protocol[2] = ISDN_PID_L2_LAPD_NET;
	} else {
		/* TE-mode */
		dch->inst.pid.protocol[0] = (hc->pri)?ISDN_PID_L0_TE_E1:ISDN_PID_L0_TE_S0;
		dch->inst.pid.protocol[1] = (hc->pri)?ISDN_PID_L1_TE_E1:ISDN_PID_L1_TE_S0;
		pid.protocol[0] = (hc->pri)?ISDN_PID_L0_TE_E1:ISDN_PID_L0_TE_S0;
		pid.protocol[1] = (hc->pri)?ISDN_PID_L1_TE_E1:ISDN_PID_L1_TE_S0;
	}
	dch->inst.pid.layermask |= ISDN_LAYER(1);
	pid.layermask |= ISDN_LAYER(1);


	/* add stacks */
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: Adding d-stack: card(%d)\n", __FUNCTION__, l1oip_cnt+1);
	if ((ret_err = mISDN_ctrl(NULL, MGR_NEWSTACK | REQUEST, &dch->inst))) {
		printk(KERN_ERR  "MGR_ADDSTACK REQUEST dch err(%d)\n", ret_err);
		free_release:
		l1oip_socket_close(hc);
		goto free_channels;
	}
	dst = dch->inst.st;
	i=0;
	while(i < hc->numbch) {
		ch = i + 1 + (i>=(hc->dch-1)); /* skip dchannel number */
		bch = hc->chan[ch].ch;
		if (debug & DEBUG_L1OIP_INIT)
			printk(KERN_DEBUG "%s: Adding b-stack: card(%d) B-channel(%d)\n", __FUNCTION__, l1oip_cnt+1, bch->channel);
		if ((ret_err = mISDN_ctrl(dst, MGR_NEWSTACK | REQUEST, &bch->inst))) {
			printk(KERN_ERR "MGR_ADDSTACK bchan error %d\n", ret_err);
			free_delstack:
			mISDN_ctrl(dst, MGR_DELSTACK | REQUEST, NULL);
			goto free_release;
		}
		i++;
	}
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: (before MGR_SETSTACK REQUEST) layermask=0x%x\n", __FUNCTION__, pid.layermask);

	if ((ret_err = mISDN_ctrl(dst, MGR_SETSTACK | REQUEST, &pid))) {
		printk(KERN_ERR "MGR_SETSTACK REQUEST dch err(%d)\n", ret_err);
		goto free_delstack;
	}
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: (after MGR_SETSTACK REQUEST)\n", __FUNCTION__);

	/* delay some time */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((100*HZ)/1000); /* Timeout 100ms */

	/* tell stack, that we are ready */
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: (before MGR_CTRLREADY REQUEST)\n", __FUNCTION__);
	mISDN_ctrl(dst, MGR_CTRLREADY | INDICATION, NULL);
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: (after MGR_CTRLREADY REQUEST)\n", __FUNCTION__);

	/* run card setup */
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: Setting up network(%d)\n", __FUNCTION__, l1oip_cnt+1);
	if ((ret_err = l1oip_socket_open(hc))) {
		goto free_channels;
	}

	/* set keepalive timer */
	hc->keep_tl.function = (void *)l1oip_keepalive;
	hc->keep_tl.data = (u32)hc;
	init_timer(&hc->keep_tl);
	hc->keep_tl.expires = jiffies + 2*HZ; /* two seconds for the first time */
	add_timer(&hc->keep_tl);

	/* set timeout timer */
	hc->timeout_tl.function = (void *)l1oip_timeout;
	hc->timeout_tl.data = (u32)hc;
	init_timer(&hc->timeout_tl);
	hc->timeout_tl.expires = jiffies; /* make timer already timed out */

	l1oip_cnt++;
	goto next_card;

	/* if an error ocurred */

	free_channels:
	if (hc->chan[hc->dch].ch) {
		if (debug & DEBUG_L1OIP_INIT)
			printk(KERN_DEBUG "%s: free port D-channel %d (1..32)\n", __FUNCTION__, hc->dch);
		mISDN_freechannel(hc->chan[hc->dch].ch);
		mISDN_ctrl(&hc->chan[hc->dch].ch->inst, MGR_UNREGLAYER | REQUEST, NULL);
		kfree(hc->chan[hc->dch].ch);
		hc->chan[hc->dch].ch = NULL;
	}
	i = 0;
	while(i < 32) {
		if (hc->chan[i].ch && i!=hc->dch) {
			if (debug & DEBUG_L1OIP_INIT)
				printk(KERN_DEBUG "%s: free B-channel %d (1..32)\n", __FUNCTION__, i);
			mISDN_freechannel(hc->chan[i].ch);
			kfree(hc->chan[i].ch);
			hc->chan[i].ch = NULL;
		}
		i++;
	}
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: before REMOVE_FROM_LIST (refcnt = %d)\n", __FUNCTION__, l1oip_obj.refcnt);
	spin_lock_irqsave(&l1oip_obj.lock, flags);
	list_del(&hc->list);
	spin_unlock_irqrestore(&l1oip_obj.lock, flags);
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: after REMOVE_FROM_LIST (refcnt = %d)\n", __FUNCTION__, l1oip_obj.refcnt);
	kfree(hc);

	free_object:
	l1oip_cleanup();
	return(ret_err);
}



#ifdef MODULE
module_init(l1oip_init);
module_exit(l1oip_cleanup);
#endif



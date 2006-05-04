/*
 * gateway between mISDN and Linux's netdev subsystem
 *
 * Copyright (C) 2005-2006 Christian Richter
 *
 * Authors: Christian Richter 
 * 
 * Thanks to Daniele Orlandi from the vISDN Developer Team
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/netdevice.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/crc32.h>
#include <linux/mISDNif.h>

#include "core.h"
#include "lapd.h"


wait_queue_head_t waitq;
struct sk_buff_head finishq;


struct mISDN_netdev {
	struct sk_buff_head workq;
	struct list_head list;
	struct net_device *netdev;
	struct net_device_stats stats; 
	mISDNstack_t *st;
	int nt;
	int addr;
};

LIST_HEAD(mISDN_netdev_list);
	
static int misdn_chan_frame_xmit(
	struct net_device *netdev,
	struct sk_buff *skb, int rx)
{
	struct mISDN_netdev *ndev=netdev->priv;
	struct lapd_prim_hdr *prim_hdr;

	netdev->last_rx = jiffies;

	skb->protocol = htons(ETH_P_LAPD);
	skb->dev = netdev;
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_HOST;

	skb_push(skb, sizeof(struct lapd_prim_hdr));
	prim_hdr = (struct lapd_prim_hdr *)skb->data;
	prim_hdr->primitive_type = LAPD_PH_DATA_INDICATION;

	if (rx)
		ndev->stats.rx_packets++;
	else
		ndev->stats.rx_packets++;
		
	
	return netif_rx(skb);
}



static struct net_device_stats *misdn_netdev_get_stats(
	struct net_device *netdev)
{
		struct mISDN_netdev *ndev=netdev->priv;
		return &ndev->stats;
}

static void misdn_netdev_set_multicast_list(
	struct net_device *netdev)
{
}

static int misdn_netdev_do_ioctl(
	struct net_device *netdev,
	struct ifreq *ifr, int cmd)
{
	return -EOPNOTSUPP;
}

static int misdn_netdev_change_mtu(
	struct net_device *netdev,
	int new_mtu)
{
	return 0;
}

static int lapd_mac_addr(struct net_device *dev, void *addr)
{
	return 0;
}

static void setup_lapd(struct net_device *netdev)
{
	netdev->change_mtu = NULL;
	netdev->hard_header = NULL;
	netdev->rebuild_header = NULL;
	netdev->set_mac_address = lapd_mac_addr;
	netdev->hard_header_cache = NULL;
	netdev->header_cache_update= NULL;
	//netdev->hard_header_parse = lapd_hard_header_parse;
	netdev->hard_header_parse = NULL;

	netdev->type = ARPHRD_LAPD;
	netdev->hard_header_len = 0;
	netdev->addr_len = 1;
	netdev->tx_queue_len = 10;

	memset(netdev->broadcast, 0, sizeof(netdev->broadcast));

	netdev->flags = 0;
}



static int misdn_netdev_open(struct net_device *netdev)
{
	return 0;
}

static int misdn_netdev_stop(struct net_device *netdev)
{
	return 0;
}


static int mISDN_netdevd(void *data)
{
	struct sk_buff *skb;
	struct mISDN_netdev *ndev;
	
	for(;;) {
		wait_event_interruptible(waitq, 1);

		if ((skb=skb_dequeue(&finishq))) {
			kfree_skb(skb);
			return 0;
		}

		list_for_each_entry(ndev, &mISDN_netdev_list, list) {
			while ((skb=skb_dequeue(&ndev->workq))) {
				misdn_chan_frame_xmit(ndev->netdev, skb, 1);
			}
		}
	}

	return 0;
}


static int misdn_netdev_frame_xmit(
	struct sk_buff *skb,
	struct net_device *netdev)
{
	return 0;
}


/*************************/
/** Interface Functions **/
/*************************/

void misdn_log_frame(mISDNstack_t* st, char *data, int len, int direction) 
{
	struct sk_buff *skb=alloc_skb(sizeof(struct lapd_prim_hdr)+len, GFP_KERNEL);
	struct mISDN_netdev *ndev;

	skb_reserve(skb, sizeof(struct lapd_prim_hdr));
	skb_reserve(skb, 4);
	memcpy(skb_put(skb, len), data, len);

	list_for_each_entry(ndev, &mISDN_netdev_list, list) {
		if (ndev->st==st) break;
	}

	if (ndev)
		misdn_chan_frame_xmit(ndev->netdev, skb , 1);
	else
		printk(KERN_WARNING "No mISDN_netdev for stack:%x\n",st->id);


}




int misdn_netdev_addstack(mISDNstack_t *st) 
{
	struct net_device *netdev;
	struct mISDN_netdev *ndev;
	int err;
	char name[8];
	
	sprintf(name, "mISDN%01x%01x",
	        (st->id/10) & 0xF,
	        (st->id%10) & 0xF);

	printk(KERN_NOTICE "allocating %s as netdev\n", name);
	
	netdev = alloc_netdev(0, name, setup_lapd);
	if(!netdev) {
		printk(KERN_ERR "net_device alloc failed, abort.\n");
		return -ENOMEM;
	}
	
	ndev = kmalloc(sizeof(struct mISDN_netdev), GFP_KERNEL);
	if(!ndev) {
		printk(KERN_ERR "mISDN_netdevice alloc failed, abort.\n");
		return -ENOMEM;
	}

	memset(&ndev->stats,0,sizeof(ndev->stats));

	skb_queue_head_init(&ndev->workq);

	ndev->netdev=netdev; 

	netdev->priv = ndev;
	netdev->open = misdn_netdev_open;
	netdev->stop = misdn_netdev_stop;
	netdev->hard_start_xmit = misdn_netdev_frame_xmit;
	netdev->get_stats = misdn_netdev_get_stats;
	netdev->set_multicast_list = misdn_netdev_set_multicast_list;
	netdev->do_ioctl = misdn_netdev_do_ioctl;
	netdev->change_mtu = misdn_netdev_change_mtu;
	netdev->features = NETIF_F_NO_CSUM;
	
	switch (st->pid.protocol[0] & ~ISDN_PID_FEATURE_MASK) {
		case ISDN_PID_L0_TE_S0:
		case ISDN_PID_L0_TE_E1:
			printk(KERN_NOTICE " --> TE Mode\n");
			memset(netdev->dev_addr, 0,  sizeof(netdev->dev_addr));
			break;
		case ISDN_PID_L0_NT_S0:
		case ISDN_PID_L0_NT_E1:
			printk(KERN_NOTICE " --> NT Mode\n");
			memset(netdev->dev_addr, 1,  sizeof(netdev->dev_addr));
			break;
	}

	SET_MODULE_OWNER(netdev);

	netdev->irq = 0;
	netdev->base_addr = 0;
	
	list_add(&ndev->list, &mISDN_netdev_list);

	printk ("10\n");
	err = register_netdev(netdev);
	
	if (err < 0) {
		printk(KERN_ERR "Cannot register net device %s, aborting.\n",
		       netdev->name);
	}
	

	return 0;
}


int misdn_netdev_init(void)
{
	int err=0;
	
	init_waitqueue_head(&waitq);
	skb_queue_head_init(&finishq);


	kernel_thread(mISDN_netdevd, NULL, 0);
	
	return err;
}

void misdn_netdev_exit(void)
{
	struct sk_buff *skb=alloc_skb(8,GFP_KERNEL);
	struct mISDN_netdev *ndev;

	skb_queue_tail(&finishq,skb);

	list_for_each_entry(ndev, &mISDN_netdev_list, list) {
		unregister_netdev(ndev->netdev);
	}
}



/*
 * loop.c  loop driver for looped bchannel pairs
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

/* module parameters:
 * interfaces:
	Number of loop interfaces. Default is 1.

 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/delay.h>

#include "channel.h"
#include "layer1.h"
#include "debug.h"
#include <linux/isdn_compat.h>

#include "loop.h"

static const char *loop_revision = "$Revision: 1.6 $";

static int loop_cnt;

static mISDNobject_t	loop_obj;

static char LoopName[] = "loop";


/****************/
/* module stuff */
/****************/

static int interfaces;
static int debug;

#ifdef MODULE
MODULE_AUTHOR("Andreas Eversberg");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
module_param(interfaces, uint, S_IRUGO | S_IWUSR);
module_param(debug, uint, S_IRUGO | S_IWUSR);
#endif


/****************************/
/* Layer 1 D-channel access */
/****************************/

/* message transfer from layer 1.
 */
static int loop_l1hw(mISDNinstance_t *inst, struct sk_buff *skb)
{
	channel_t	*dch = container_of(inst, channel_t, inst);
	loop_t		*hc;
	int		ret = 0;
	mISDN_head_t	*hh;

	hh = mISDN_HEAD_P(skb);
	hc = dch->inst.privat;
	
	if (debug & DEBUG_LOOP_MSG)
		printk(KERN_DEBUG "%s: unsupported prim %x\n", __FUNCTION__, hh->prim);
	ret = -EINVAL;
	if (!ret)
		dev_kfree_skb(skb);
	return(ret);
}
/******************************/
/* Layer2 -> Layer 1 Transfer */
/******************************/

/* messages from layer 2 to layer 1 are processed here.
 */
static int
loop_l2l1(mISDNinstance_t *inst, struct sk_buff *skb)
{
	int		ch;
	channel_t	*bch = container_of(inst, channel_t, inst);
	int		ret = -EINVAL;
	mISDN_head_t	*hh;
	loop_t	*hc;
	struct sk_buff	*nskb;

	hh = mISDN_HEAD_P(skb);
	hc = bch->inst.privat;
	ch = bch->channel;

	if ((hh->prim == PH_DATA_REQ)
	 || (hh->prim == (DL_DATA | REQUEST))) {
		if (skb->len <= 0) {
			printk(KERN_WARNING "%s: skb too small\n", __FUNCTION__);
			return(-EINVAL);
		}
		if (skb->len > MAX_DATA_MEM) {
			printk(KERN_WARNING "%s: skb too large\n", __FUNCTION__);
			return(-EINVAL);
		}
		if ((nskb = skb_clone(skb, GFP_ATOMIC)))
			queue_ch_frame(hc->bch[ch^1], INDICATION, MISDN_ID_ANY, nskb);
		skb_trim(skb, 0);
		return(mISDN_queueup_newhead(inst, 0, hh->prim | CONFIRM, hh->dinfo, skb));
	} else if ((hh->prim == (PH_ACTIVATE | REQUEST))
	 || (hh->prim == (DL_ESTABLISH  | REQUEST))) {
		/* activate B-channel if not already activated */
		skb_trim(skb, 0);
		return(mISDN_queueup_newhead(inst, 0, hh->prim | CONFIRM, ret, skb));
	} else if ((hh->prim == (PH_DEACTIVATE | REQUEST))
	 || (hh->prim == (DL_RELEASE | REQUEST))
	 || ((hh->prim == (PH_CONTROL | REQUEST) && (hh->dinfo == HW_DEACTIVATE)))) {
		skb_trim(skb, 0);
		return(mISDN_queueup_newhead(inst, 0, hh->prim | CONFIRM, ret, skb));
	} else
	if (hh->prim == (PH_CONTROL | REQUEST)) {
		switch (hh->dinfo) {
			default:
			printk(KERN_DEBUG "%s: unknown PH_CONTROL info %x\n", __FUNCTION__, hh->dinfo);
			ret = -EINVAL;
		}
	} else {
		printk(KERN_WARNING "%s: unknown prim(%x)\n", __FUNCTION__, hh->prim);
		ret = -EINVAL;
	}
	if (!ret) {
		dev_kfree_skb(skb);
	}
	return(ret);
}



/**************************
 * remove card from stack *
 **************************/

static void
loop_delete(loop_t *hc)
{
	int	ch;
	u_long	flags;

	if (debug & DEBUG_LOOP_INIT)
		printk(KERN_DEBUG "%s: entered\n", __FUNCTION__);

	/* free channels */
	if (hc->dch) {
		if (debug & DEBUG_LOOP_INIT)
			printk(KERN_DEBUG "%s: free D-channel\n", __FUNCTION__);
		mISDN_freechannel(hc->dch);
		kfree(hc->dch);
		hc->dch = NULL;
	}
	ch = 0;
	while(ch < LOOP_CHANNELS) {
		if (hc->bch[ch]) {
			if (debug & DEBUG_LOOP_INIT)
				printk(KERN_DEBUG "%s: free B-channel %d\n", __FUNCTION__, ch);
			mISDN_freechannel(hc->bch[ch]);
			kfree(hc->bch[ch]);
			hc->bch[ch] = NULL;
		}
		ch++;
	}
	
	/* remove us from list and delete */
	if (debug & DEBUG_LOOP_INIT)
		printk(KERN_WARNING "%s: remove instance from list\n", __FUNCTION__);
	spin_lock_irqsave(&loop_obj.lock, flags);
	list_del(&hc->list);
	spin_unlock_irqrestore(&loop_obj.lock, flags);
	if (debug & DEBUG_LOOP_INIT)
		printk(KERN_WARNING "%s: delete instance\n", __FUNCTION__);
	kfree(hc);
	loop_cnt--;
	if (debug & DEBUG_LOOP_INIT)
		printk(KERN_WARNING "%s: card successfully removed\n", __FUNCTION__);
}

static int
loop_manager(void *data, u_int prim, void *arg)
{
	loop_t	*hc;
	mISDNinstance_t	*inst = data;
	struct sk_buff	*skb;
	channel_t	*dch = NULL;
	channel_t	*bch = NULL;
	int		ch = 0;
	u_long		flags;

	if (!data) {
		MGR_HASPROTOCOL_HANDLER(prim,arg,&loop_obj)
		printk(KERN_ERR "%s: no data prim %x arg %p\n", __FUNCTION__, prim, arg);
		return(-EINVAL);
	}

	/* find channel and card */
	spin_lock_irqsave(&loop_obj.lock, flags);
	list_for_each_entry(hc, &loop_obj.ilist, list) {
		if (hc->dch) if (&hc->dch->inst == inst) {
			dch = hc->dch;
			spin_unlock_irqrestore(&loop_obj.lock, flags);
			if (debug & DEBUG_LOOP_MGR)
				printk(KERN_DEBUG "%s: D-channel  data %p prim %x arg %p\n", __FUNCTION__, data, prim, arg);
			goto found;
		}

		ch = 0;
		while(ch < LOOP_CHANNELS) {
			if (hc->bch[ch]) if (&hc->bch[ch]->inst == inst) {
				bch = hc->bch[ch];
				spin_unlock_irqrestore(&loop_obj.lock, flags);
				if (debug & DEBUG_LOOP_MGR)
					printk(KERN_DEBUG "%s: B-channel %d (0..%d)  data %p prim %x arg %p\n", __FUNCTION__, ch, LOOP_CHANNELS-1, data, prim, arg);
				goto found;
			}
		}
	}
	spin_unlock_irqrestore(&loop_obj.lock, flags);
	printk(KERN_ERR "%s: no card/channel found  data %p prim %x arg %p\n", __FUNCTION__, data, prim, arg);
	return(-EINVAL);

found:
	switch(prim) {
		case MGR_REGLAYER | CONFIRM:
		if (debug & DEBUG_LOOP_MGR)
			printk(KERN_DEBUG "%s: MGR_REGLAYER\n", __FUNCTION__);
		mISDN_setpara(dch, &inst->st->para);
		break;

		case MGR_UNREGLAYER | REQUEST:
		if (debug & DEBUG_LOOP_MGR)
			printk(KERN_DEBUG "%s: MGR_UNREGLAYER\n", __FUNCTION__);
		if (dch) {
			if ((skb = create_link_skb(PH_CONTROL | REQUEST, HW_DEACTIVATE, 0, NULL, 0))) {
				if (loop_l1hw(inst, skb)) dev_kfree_skb(skb);
			}
		} else
		if (bch) {
			if ((skb = create_link_skb(PH_CONTROL | REQUEST, 0, 0, NULL, 0))) {
				if (loop_l2l1(inst, skb)) dev_kfree_skb(skb);
			}
		}
		mISDN_ctrl(inst, MGR_UNREGLAYER | REQUEST, NULL);
		break;

		case MGR_CLRSTPARA | INDICATION:
		arg = NULL;
		// fall through
		case MGR_ADDSTPARA | INDICATION:
		if (debug & DEBUG_LOOP_MGR)
			printk(KERN_DEBUG "%s: MGR_***STPARA\n", __FUNCTION__);
		mISDN_setpara(dch, arg);
		break;

		case MGR_RELEASE | INDICATION:
		if (debug & DEBUG_LOOP_MGR)
			printk(KERN_DEBUG "%s: MGR_RELEASE = remove port from mISDN\n", __FUNCTION__);
		if (dch) {
			loop_delete(hc); /* hc is free */
		}
		break;
#ifdef FIXME
		case MGR_CONNECT | REQUEST:
		if (debug & DEBUG_LOOP_MGR)
			printk(KERN_DEBUG "%s: MGR_CONNECT\n", __FUNCTION__);
		return(mISDN_ConnectIF(inst, arg));

		case MGR_SETIF | REQUEST:
		case MGR_SETIF | INDICATION:
		if (debug & DEBUG_LOOP_MGR)
			printk(KERN_DEBUG "%s: MGR_SETIF\n", __FUNCTION__);
		if (dch)
			return(mISDN_SetIF(inst, arg, prim, loop_l1hw, NULL, dch));
		if (bch)
			return(mISDN_SetIF(inst, arg, prim, loop_l2l1, NULL, bch));
		break;

		case MGR_DISCONNECT | REQUEST:
		case MGR_DISCONNECT | INDICATION:
		if (debug & DEBUG_LOOP_MGR)
			printk(KERN_DEBUG "%s: MGR_DISCONNECT\n", __FUNCTION__);
		return(mISDN_DisConnectIF(inst, arg));
#endif
#if 0
		case MGR_SELCHANNEL | REQUEST:
		if (debug & DEBUG_LOOP_MGR)
			printk(KERN_DEBUG "%s: MGR_SELCHANNEL\n", __FUNCTION__);
		if (!dch) {
			printk(KERN_WARNING "%s(MGR_SELCHANNEL|REQUEST): selchannel not dinst\n", __FUNCTION__);
			return(-EINVAL);
		}
		return(SelFreeBChannel(hc, ch, arg));
#endif
		
		case MGR_SETSTACK | INDICATION:
		if (debug & DEBUG_LOOP_MGR)
			printk(KERN_DEBUG "%s: MGR_SETSTACK\n", __FUNCTION__);
		if (bch && inst->pid.global==2) {
			if ((skb = create_link_skb(PH_ACTIVATE | REQUEST, 0, 0, NULL, 0))) {
				if (loop_l2l1(inst, skb)) dev_kfree_skb(skb);
			}
			if (inst->pid.protocol[2] == ISDN_PID_L2_B_TRANS)
			mISDN_queue_data(inst, FLG_MSG_UP, DL_ESTABLISH | INDICATION, 0, 0, NULL, 0);
		else mISDN_queue_data(inst, FLG_MSG_UP, PH_ACTIVATE | INDICATION, 0, 0, NULL, 0);
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

/*************************
 * create cards instance *
 *************************/

static int __devinit loop_new(void)
{
	int		ret_err=0;
	int		ch;
	loop_t	*hc;
	mISDN_pid_t	pid;
	mISDNstack_t	*dst = NULL; /* make gcc happy */
	channel_t	*dch;
	channel_t	*bch;
	u_long		flags;

	if (debug & DEBUG_LOOP_INIT)
	printk(KERN_DEBUG "%s: Registering loop driver #%d\n", __FUNCTION__, loop_cnt+1);

	/* allocate structure */
	if (!(hc = kmalloc(sizeof(loop_t), GFP_ATOMIC))) {
		printk(KERN_ERR "No kmem for loop driver\n");
		ret_err = -ENOMEM;
		goto free_object;
	}
	memset(hc, 0, sizeof(loop_t));
	hc->idx = loop_cnt;
	hc->id = loop_cnt + 1;

	sprintf(hc->name, "LOOP#%d", loop_cnt+1);

	if (debug & DEBUG_LOOP_INIT)
		printk(KERN_DEBUG "%s: (after APPEND_TO_LIST)\n", __FUNCTION__);
	
	spin_lock_irqsave(&loop_obj.lock, flags);
	list_add_tail(&hc->list, &loop_obj.ilist);
	spin_unlock_irqrestore(&loop_obj.lock, flags);
	if (debug & DEBUG_LOOP_INIT)
		printk(KERN_DEBUG "%s: (after APPEND_TO_LIST)\n", __FUNCTION__);

	spin_lock_init(&hc->lock);

	if (debug & DEBUG_LOOP_INIT)
		printk(KERN_DEBUG "%s: Registering D-channel, card(%d)\n", __FUNCTION__, loop_cnt+1);
	dch = kmalloc(sizeof(channel_t), GFP_ATOMIC);
	if (!dch) {
		ret_err = -ENOMEM;
		goto free_channels;
	}
	memset(dch, 0, sizeof(channel_t));
	dch->channel = 0;
	//dch->debug = debug;
	dch->inst.obj = &loop_obj;
	dch->inst.hwlock = &hc->lock;
	mISDN_init_instance(&dch->inst, &loop_obj, hc, loop_l1hw);
	dch->inst.pid.layermask = ISDN_LAYER(0);
	sprintf(dch->inst.name, "LOOP%d", loop_cnt+1);
	if (mISDN_initchannel(dch, MSK_INIT_DCHANNEL, MAX_DATA_MEM)) {
		ret_err = -ENOMEM;
		goto free_channels;
	}
	hc->dch = dch;

	ch=0;
	while(ch < LOOP_CHANNELS) {
		if (debug & DEBUG_LOOP_INIT)
			printk(KERN_DEBUG "%s: Registering B-channel, card(%d) ch(%d)\n", __FUNCTION__, loop_cnt+1, ch);
		bch = kmalloc(sizeof(channel_t), GFP_ATOMIC);
		if (!bch) {
			ret_err = -ENOMEM;
			goto free_channels;
		}
		memset(bch, 0, sizeof(channel_t));
		bch->channel = ch;
		mISDN_init_instance(&bch->inst, &loop_obj, hc, loop_l2l1);
		bch->inst.pid.layermask = ISDN_LAYER(0);
		bch->inst.hwlock = &hc->lock;
		//bch->debug = debug;
		sprintf(bch->inst.name, "%s B%d",
			dch->inst.name, ch+1);
		if (mISDN_initchannel(bch, MSK_INIT_BCHANNEL, MAX_DATA_MEM)) {
			kfree(bch);
			ret_err = -ENOMEM;
			goto free_channels;
		}
		hc->bch[ch] = bch;
#ifdef FIXME  // TODO
		if (bch->dev) {
			bch->dev->wport.pif.func = loop_l2l1;
			bch->dev->wport.pif.fdata = bch;
		}
#endif
		ch++;
	}

	/* set D-channel */
	mISDN_set_dchannel_pid(&pid, 0x00, ISDN_LAYER(0));
	pid.protocol[0] = ISDN_PID_L0_LOOP;
	pid.layermask = ISDN_LAYER(0);

	/* add stacks */
	if (debug & DEBUG_LOOP_INIT)
		printk(KERN_DEBUG "%s: Adding d-stack: card(%d)\n", __FUNCTION__, loop_cnt+1);
	if ((ret_err = mISDN_ctrl(NULL, MGR_NEWSTACK | REQUEST, &dch->inst))) {
		printk(KERN_ERR  "MGR_ADDSTACK REQUEST dch err(%d)\n", ret_err);
		free_release:
		loop_delete(hc); /* hc is free */
		goto free_object;
	}

	dst = dch->inst.st;

	ch = 0;
	while(ch < LOOP_CHANNELS) {
		if (debug & DEBUG_LOOP_INIT)
			printk(KERN_DEBUG "%s: Adding b-stack: card(%d) B-channel(%d)\n", __FUNCTION__, loop_cnt+1, ch+1);
		bch = hc->bch[ch];
		if ((ret_err = mISDN_ctrl(dst, MGR_NEWSTACK | REQUEST, &bch->inst))) {
			printk(KERN_ERR "MGR_ADDSTACK bchan error %d\n", ret_err);
			free_delstack:
			mISDN_ctrl(dst, MGR_DELSTACK | REQUEST, NULL);
			goto free_release;
		}
		ch++;
	}
	if (debug & DEBUG_LOOP_INIT)
		printk(KERN_DEBUG "%s: (before MGR_SETSTACK REQUEST) layermask=0x%x\n", __FUNCTION__, pid.layermask);

	if ((ret_err = mISDN_ctrl(dst, MGR_SETSTACK | REQUEST, &pid))) {
		printk(KERN_ERR "MGR_SETSTACK REQUEST dch err(%d)\n", ret_err);
		goto free_delstack;
	}
	if (debug & DEBUG_LOOP_INIT)
		printk(KERN_DEBUG "%s: (after MGR_SETSTACK REQUEST)\n", __FUNCTION__);

	/* delay some time */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((100*HZ)/1000); /* Timeout 100ms */

	/* tell stack, that we are ready */
	mISDN_ctrl(dst, MGR_CTRLREADY | INDICATION, NULL);

	loop_cnt++;
	return(0);

	/* if an error ocurred */
	free_channels:
	if (hc->dch) {
		if (debug & DEBUG_LOOP_INIT)
			printk(KERN_DEBUG "%s: free D-channel\n", __FUNCTION__);
		mISDN_freechannel(hc->dch);
		kfree(hc->dch);
		hc->dch = NULL;
	}
	ch = 0;
	while(ch < LOOP_CHANNELS) {
		if (hc->bch[ch]) {
			if (debug & DEBUG_LOOP_INIT)
				printk(KERN_DEBUG "%s: free B-channel %d\n", __FUNCTION__, ch);
			mISDN_freechannel(hc->bch[ch]);
			kfree(hc->bch[ch]);
			hc->bch[ch] = NULL;
		}
		ch++;
	}
	if (debug & DEBUG_LOOP_INIT)
		printk(KERN_DEBUG "%s: before REMOVE_FROM_LIST (refcnt = %d)\n", __FUNCTION__, loop_obj.refcnt);
	spin_lock_irqsave(&loop_obj.lock, flags);
	list_del(&hc->list);
	spin_unlock_irqrestore(&loop_obj.lock, flags);
	if (debug & DEBUG_LOOP_INIT)
		printk(KERN_DEBUG "%s: after REMOVE_FROM_LIST (refcnt = %d)\n", __FUNCTION__, loop_obj.refcnt);
	kfree(hc);

	free_object:
	return(ret_err);
}


static void __exit
loop_cleanup(void)
{
	loop_t *hc,*next;
	int err;

	/* unregister mISDN object */
	if (debug & DEBUG_LOOP_INIT)
		printk(KERN_DEBUG "%s: entered (refcnt = %d loop_cnt = %d)\n", __FUNCTION__, loop_obj.refcnt, loop_cnt);
	if ((err = mISDN_unregister(&loop_obj))) {
		printk(KERN_ERR "Can't unregister Loop cards error(%d)\n", err);
	}

	/* remove remaining devices, but this should never happen */
	if (debug & DEBUG_LOOP_INIT)
		printk(KERN_DEBUG "%s: now checking ilist (refcnt = %d)\n", __FUNCTION__, loop_obj.refcnt);

	list_for_each_entry_safe(hc, next, &loop_obj.ilist, list) {
		printk(KERN_ERR "Loop card struct not empty refs %d\n", loop_obj.refcnt);
		loop_delete(hc);
	}
	if (debug & DEBUG_LOOP_INIT)
		printk(KERN_DEBUG "%s: done (refcnt = %d loop_cnt = %d)\n", __FUNCTION__, loop_obj.refcnt, loop_cnt);

}

static int __init
loop_init(void)
{
	int err;
	char tmpstr[64];

	if (debug & DEBUG_LOOP_INIT)
		printk(KERN_DEBUG "%s: init entered\n", __FUNCTION__);

	strcpy(tmpstr, loop_revision);
	printk(KERN_INFO "mISDN: loop-driver Rev. %s\n", mISDN_getrev(tmpstr));

	memset(&loop_obj, 0, sizeof(loop_obj));
#ifdef MODULE
	loop_obj.owner = THIS_MODULE;
#endif
	spin_lock_init(&loop_obj.lock);
	INIT_LIST_HEAD(&loop_obj.ilist);
	loop_obj.name = LoopName;
	loop_obj.own_ctrl = loop_manager;
	loop_obj.DPROTO.protocol[0] = ISDN_PID_L0_LOOP;
	loop_obj.BPROTO.protocol[1] = ISDN_PID_L1_B_64TRANS | ISDN_PID_L1_B_64HDLC;
	loop_obj.BPROTO.protocol[2] = ISDN_PID_L2_B_TRANS | ISDN_PID_L2_B_RAWDEV;

	if (debug & DEBUG_LOOP_INIT)
		printk(KERN_DEBUG "%s: registering loop_obj\n", __FUNCTION__);
	if ((err = mISDN_register(&loop_obj))) {
		printk(KERN_ERR "Can't register Loop cards error(%d)\n", err);
		return(err);
	}
	if (debug & DEBUG_LOOP_INIT)
		printk(KERN_DEBUG "%s: new mISDN object (refcnt = %d)\n", __FUNCTION__, loop_obj.refcnt);

	if (interfaces < 1)
		interfaces = 1;
	loop_cnt = 0;
	while(loop_cnt < interfaces)
	{
		if ((err = loop_new()))
			break;
	}

	if (err)
	{
		printk(KERN_ERR "error registering pci driver:%x\n",err);
		loop_cleanup();
		return(err);
	}
	printk(KERN_INFO "%d devices registered\n", loop_cnt);

	return(0);
}


#ifdef MODULE
module_init(loop_init);
module_exit(loop_cleanup);
#endif


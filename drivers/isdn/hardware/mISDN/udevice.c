/* $Id: udevice.c,v 1.11 2004/01/26 22:21:31 keil Exp $
 *
 * Copyright 2000  by Karsten Keil <kkeil@isdn4linux.de>
 *
 */

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/poll.h>
#include <linux/vmalloc.h>
#include <linux/config.h>
#include <linux/timer.h>
#include <linux/list.h>
#include "core.h"

#define MAX_HEADER_LEN	4

#define FLG_MGR_SETSTACK	1
#define FLG_MGR_OWNSTACK	2

#define FLG_MGR_TIMER_INIT	1
#define	FLG_MGR_TIMER_RUNING	2


typedef struct _devicelayer {
	struct _devicelayer	*prev;
	struct _devicelayer	*next;
	mISDNdevice_t		*dev;
	mISDNinstance_t		inst;
	mISDNinstance_t		*slave;
	mISDNif_t		s_up;
	mISDNif_t		s_down;
	int			iaddr;
	int			lm_st;
	u_long			Flags;
} devicelayer_t;

typedef struct _devicestack {
	struct _devicestack	*prev;
	struct _devicestack	*next;
	mISDNdevice_t		*dev;
	mISDNstack_t		*st;
	int			extentions;
} devicestack_t;

typedef struct _mISDNtimer {
	struct _mISDNtimer	*prev;
	struct _mISDNtimer	*next;
	struct _mISDNdevice	*dev;
	struct timer_list	tl;
	int			id;
	u_long			Flags;
} mISDNtimer_t;

typedef struct entity_item {
	struct list_head	head;
	int			entity;
} entity_item_t;

static mISDNdevice_t	*mISDN_devicelist = NULL;
static rwlock_t	mISDN_device_lock = RW_LOCK_UNLOCKED;

static mISDNobject_t	udev_obj;
static char MName[] = "UserDevice";
static u_char  stbuf[1000];

static int device_debug = 0;

static int from_up_down(mISDNif_t *, struct sk_buff *);
static int mISDN_wdata(mISDNdevice_t *dev);

// static int from_peer(mISDNif_t *, u_int, int, int, void *);
// static int to_peer(mISDNif_t *, u_int, int, int, void *);


static mISDNdevice_t *
get_mISDNdevice4minor(int minor)
{
	mISDNdevice_t	*dev = mISDN_devicelist;

	while(dev) {
		if (dev->minor == minor)
			break;
		dev = dev->next;
	}
	return(dev);
}

static __inline__ void
p_memcpy_i(mISDNport_t *port, void *src, size_t count)
{
	u_char	*p = src;
	size_t	frag;

	frag = port->buf + port->size - port->ip;
	if (frag <= count) {
		memcpy(port->ip, p, frag);
		count -= frag;
		port->cnt += frag;
		port->ip = port->buf;
	} else
		frag = 0;
	if (count) {
		memcpy(port->ip, p + frag, count);
		port->cnt += count;
		port->ip += count;
	}
}

static __inline__ void
p_memcpy_o(mISDNport_t *port, void *dst, size_t count)
{
	u_char	*p = dst;
	size_t	frag;

	frag = port->buf + port->size - port->op;
	if (frag <= count) {
		memcpy(p, port->op, frag);
		count -= frag;
		port->cnt -= frag;
		port->op = port->buf;
	} else
		frag = 0;
	if (count) {
		memcpy(p + frag, port->op, count);
		port->cnt -= count;
		port->op += count;
	}
}

static __inline__ void
p_pull_o(mISDNport_t *port, size_t count)
{
	size_t	frag;

	frag = port->buf + port->size - port->op;
	if (frag <= count) {
		count -= frag;
		port->cnt -= frag;
		port->op = port->buf;
	}
	if (count) {
		port->cnt -= count;
		port->op += count;
	}
}

static size_t
next_frame_len(mISDNport_t *port)
{
	size_t		len;
	int		*lp;

	if (port->cnt < IFRAME_HEAD_SIZE) {
		int_errtxt("not a frameheader cnt(%d)", port->cnt);
		return(0);
	}
	len = port->buf + port->size - port->op;
	if (len < IFRAME_HEAD_SIZE) {
		len = IFRAME_HEAD_SIZE - len - 4;
		lp = (int *)(port->buf + len);
	} else {
		lp = (int *)(port->op + 12);
	}
	if (*lp <= 0) {
		len = IFRAME_HEAD_SIZE;
	} else {
		len = IFRAME_HEAD_SIZE + *lp;
	}
	if (len > (size_t)port->cnt) {
		int_errtxt("size mismatch %d/%d/%d", *lp, len, port->cnt);
		return(0);
	}
	return(len);
}

static int
mISDN_rdata_raw(mISDNif_t *hif, struct sk_buff *skb) {
	mISDNdevice_t	*dev;
	mISDN_head_t	*hh;
	u_long		flags;
	int		retval = 0;

	if (!hif || !hif->fdata || !skb)
		return(-EINVAL);
	dev = hif->fdata;
	hh = mISDN_HEAD_P(skb);
	if (hh->prim == (PH_DATA | INDICATION)) {
		if (test_bit(FLG_mISDNPORT_OPEN, &dev->rport.Flag)) {
			spin_lock_irqsave(&dev->rport.lock, flags);
			if (skb->len < (u_int)(dev->rport.size - dev->rport.cnt)) {
				p_memcpy_i(&dev->rport, skb->data, skb->len);
			} else {
				retval = -ENOSPC;
			}
			spin_unlock_irqrestore(&dev->rport.lock, flags);
			wake_up_interruptible(&dev->rport.procq);
		} else {
			printk(KERN_WARNING "%s: PH_DATA_IND device(%d) not read open\n",
				__FUNCTION__, dev->minor);
			retval = -ENOENT;
		}
	} else if (hh->prim == (PH_DATA | CONFIRM)) {
		test_and_clear_bit(FLG_mISDNPORT_BLOCK, &dev->wport.Flag);
		mISDN_wdata(dev);
	} else if ((hh->prim == (PH_ACTIVATE | CONFIRM)) ||
		(hh->prim == (PH_ACTIVATE | INDICATION))) {
			test_and_set_bit(FLG_mISDNPORT_ENABLED,
				&dev->wport.Flag);
			test_and_clear_bit(FLG_mISDNPORT_BLOCK,
				&dev->wport.Flag);
	} else if ((hh->prim == (PH_DEACTIVATE | CONFIRM)) ||
		(hh->prim == (PH_DEACTIVATE | INDICATION))) {
			test_and_clear_bit(FLG_mISDNPORT_ENABLED,
				&dev->wport.Flag);
	} else {
		printk(KERN_WARNING "%s: prim(%x) dinfo(%x) not supported\n",
			__FUNCTION__, hh->prim, hh->dinfo);
		retval = -EINVAL;
	}
	if (!retval)
		dev_kfree_skb_any(skb);
	return(retval);
}

static int
mISDN_rdata(mISDNdevice_t *dev, iframe_t *iff, int use_value) {
	int		len = 4*sizeof(u_int);
	u_long		flags;
	mISDNport_t	*port = &dev->rport;

	if (iff->len > 0)
		len +=  iff->len;
	spin_lock_irqsave(&port->lock, flags);
	if (len < (port->size - port->cnt)) {
		if (len <= 20 && use_value) {
			p_memcpy_i(port, iff, len);
		} else {
			p_memcpy_i(port, iff, 4*sizeof(u_int));
			if (iff->len>0)
				p_memcpy_i(port, iff->data.p, iff->len);
		}
		spin_unlock_irqrestore(&port->lock, flags);
	} else {
		spin_unlock_irqrestore(&port->lock, flags);
		printk(KERN_WARNING "%s: no rport space for %d\n",
			__FUNCTION__, len);
		len = -ENOSPC;
	}
	wake_up_interruptible(&port->procq);
	return(len);
}

static devicelayer_t
*get_devlayer(mISDNdevice_t   *dev, int addr) {
	devicelayer_t *dl = dev->layer;

	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG "%s: addr:%x\n", __FUNCTION__, addr);
	while(dl) {
//		if (device_debug & DEBUG_MGR_FUNC)
//			printk(KERN_DEBUG "%s: dl(%p) iaddr:%x\n",
//				__FUNCTION__, dl, dl->iaddr);
		if ((u_int)dl->iaddr == (IF_IADDRMASK & addr))
			break;
		dl = dl->next;
	}
	return(dl);
}

static devicestack_t
*get_devstack(mISDNdevice_t *dev, int addr)
{
	devicestack_t *ds = dev->stack;

	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG "%s: addr:%x\n", __FUNCTION__, addr);
	while(ds) {
		if (ds->st && (ds->st->id == (u_int)addr))
			break;
		ds = ds->next;
	}
	return(ds);
}

static mISDNtimer_t
*get_devtimer(mISDNdevice_t   *dev, int id)
{
	mISDNtimer_t	*ht = dev->timer;

	if (device_debug & DEBUG_DEV_TIMER)
		printk(KERN_DEBUG "%s: dev:%p id:%x\n", __FUNCTION__, dev, id);
	while(ht) {
		if (ht->id == id)
			break;
		ht = ht->next;
	}
	return(ht);
}

static int
stack_inst_flg(mISDNdevice_t *dev, mISDNstack_t *st, int bit, int clear)
{
	int ret = -1;
	devicelayer_t *dl = dev->layer;

	while(dl) {
		if (dl->inst.st == st) {
			if (clear)
				ret = test_and_clear_bit(bit, &dl->Flags);
			else
				ret = test_and_set_bit(bit, &dl->Flags);
			break;
		}
		dl = dl->next;
	}
	return(ret);
}

static int
new_devstack(mISDNdevice_t *dev, stack_info_t *si)
{
	int		err;
	mISDNstack_t	*st;
	mISDNinstance_t	inst;
	devicestack_t	*nds;

	memset(&inst, 0, sizeof(mISDNinstance_t));
	st = get_stack4id(si->id);
	if (si->extentions & EXT_STACK_CLONE) {
		if (st) {
			inst.st = st;
		} else {
			int_errtxt("ext(%x) st(%x)", si->extentions, si->id);
			return(-EINVAL);
		}
	}
	err = udev_obj.ctrl(NULL, MGR_NEWSTACK | REQUEST, &inst);
	if (err) {
		int_error();
		return(err);
	}
	if (!(nds = kmalloc(sizeof(devicestack_t), GFP_ATOMIC))) {
		printk(KERN_ERR "kmalloc devicestack failed\n");
		udev_obj.ctrl(inst.st, MGR_DELSTACK | REQUEST, NULL);
		return(-ENOMEM);
	}
	memset(nds, 0, sizeof(devicestack_t));
	nds->dev = dev;
	if (si->extentions & EXT_STACK_CLONE) {
//		memcpy(&inst.st->pid, &st->pid, sizeof(mISDN_pid_t));
		inst.st->child = st->child;
	} else {
		memcpy(&inst.st->pid, &si->pid, sizeof(mISDN_pid_t));
	}
	nds->extentions = si->extentions;
	inst.st->extentions |= si->extentions;
	inst.st->mgr = get_instance4id(si->mgr);
	nds->st = inst.st;
	APPEND_TO_LIST(nds, dev->stack);
	return(inst.st->id);
}

static mISDNstack_t *
sel_channel(u_int addr, u_int channel)
{
	mISDNstack_t	*st;
	channel_info_t	ci;

	st = get_stack4id(addr);
	if (!st)
		return(NULL);
	ci.channel = channel;
	ci.st.p = NULL;
	if (udev_obj.ctrl(st, MGR_SELCHANNEL | REQUEST, &ci))
		return(NULL);
	return(ci.st.p);
}

static int
create_layer(mISDNdevice_t *dev, layer_info_t *linfo, int *adr)
{
	mISDNlayer_t	*layer;
	mISDNstack_t	*st;
	int		i, ret;
	devicelayer_t	*nl;
	mISDNobject_t	*obj;
	mISDNinstance_t *inst = NULL;

	if (!(st = get_stack4id(linfo->st))) {
		int_error();
		return(-ENODEV);
	}
	if (linfo->object_id != -1) {
		obj = get_object(linfo->object_id);
		if (!obj) {
			printk(KERN_WARNING "%s: no object %x found\n",
				__FUNCTION__, linfo->object_id);
			return(-ENODEV);
		}
		ret = obj->own_ctrl(st, MGR_NEWLAYER | REQUEST, &linfo->pid);
		if (ret) {
			printk(KERN_WARNING "%s: error nl req %d\n",
				__FUNCTION__, ret);
			return(ret);
		}
		layer = getlayer4lay(st, linfo->pid.layermask);
		if (!layer) {
			printk(KERN_WARNING "%s: no layer for lm(%x)\n",
				__FUNCTION__, linfo->pid.layermask);
			return(-EINVAL);
		}
		inst = layer->inst;
		if (!inst) {
			printk(KERN_WARNING "%s: no inst in layer(%p)\n",
				__FUNCTION__, layer);
			return(-EINVAL);
		}
	} else if ((layer = getlayer4lay(st, linfo->pid.layermask))) {
		if (!(linfo->extentions & EXT_INST_MIDDLE)) {
			printk(KERN_WARNING
				"mISDN create_layer st(%x) LM(%x) inst not empty(%p)\n",
				st->id, linfo->pid.layermask, layer);
			return(-EBUSY);
		}
	}
	if (!(nl = kmalloc(sizeof(devicelayer_t), GFP_ATOMIC))) {
		printk(KERN_ERR "kmalloc devicelayer failed\n");
		return(-ENOMEM);
	}
	memset(nl, 0, sizeof(devicelayer_t));
	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG
			"mISDN create_layer LM(%x) nl(%p) nl inst(%p)\n",
			linfo->pid.layermask, nl, &nl->inst);
	nl->dev = dev;
	memcpy(&nl->inst.pid, &linfo->pid, sizeof(mISDN_pid_t));
	strcpy(nl->inst.name, linfo->name);
	nl->inst.extentions = linfo->extentions;
	for (i=0; i<= MAX_LAYER_NR; i++) {
		if (linfo->pid.layermask & ISDN_LAYER(i)) {
			if (st && (st->pid.protocol[i] == ISDN_PID_NONE)) {
				st->pid.protocol[i] = linfo->pid.protocol[i];
				nl->lm_st |= ISDN_LAYER(i);
			}
		}
	}
	if (st && (linfo->extentions & EXT_INST_MGR)) {
		st->mgr = &nl->inst;
		test_and_set_bit(FLG_MGR_OWNSTACK, &nl->Flags);
	}
	nl->inst.down.owner = &nl->inst;
	nl->inst.up.owner = &nl->inst;
	nl->inst.obj = &udev_obj;
	nl->inst.data = nl;
	APPEND_TO_LIST(nl, dev->layer);
	nl->inst.obj->ctrl(st, MGR_REGLAYER | INDICATION, &nl->inst);
	nl->iaddr = nl->inst.id;
	*adr++ = nl->iaddr;
	if (inst) {
		nl->slave = inst;
	} else
		*adr = 0;
	return(8);
}

static int
remove_if(devicelayer_t *dl, int stat) {
	mISDNif_t *hif,*phif,*shif;
	int err;

	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG "%s: dl(%p) stat(%x)\n", __FUNCTION__,
			dl, stat);
	phif = NULL;
	if (stat & IF_UP) {
		hif = &dl->inst.up;
		shif = &dl->s_up;
		if (shif->owner)
			phif = &shif->owner->down;
	} else if (stat & IF_DOWN) {
		hif = &dl->inst.down;
		shif = &dl->s_down;
		if (shif->owner)
			phif = &shif->owner->up;
	} else {
		printk(KERN_WARNING "%s: stat not UP/DOWN\n", __FUNCTION__);
		return(-EINVAL);
	}
	err = udev_obj.ctrl(hif->peer, MGR_DISCONNECT | REQUEST, hif);
	if (phif) {
		memcpy(phif, shif, sizeof(mISDNif_t));
		memset(shif, 0, sizeof(mISDNif_t));
	}
	REMOVE_FROM_LIST(hif);
	return(err);
}

static int
del_stack(devicestack_t *ds)
{
	mISDNdevice_t	*dev;

	if (!ds) {
		int_error();
		return(-EINVAL);
	}
	dev = ds->dev;
	if (device_debug & DEBUG_MGR_FUNC) {
		printk(KERN_DEBUG "%s: ds(%p) dev(%p)\n", 
			__FUNCTION__, ds, dev);
	}
	if (!dev)
		return(-EINVAL);
	if (ds->st) {
		if (ds->extentions & EXT_STACK_CLONE)
			ds->st->child = NULL;
		udev_obj.ctrl(ds->st, MGR_DELSTACK | REQUEST, NULL);
	}
	REMOVE_FROM_LISTBASE(ds, dev->stack);
	kfree(ds);
	return(0);
}

static int
del_layer(devicelayer_t *dl) {
	mISDNinstance_t *inst = &dl->inst;
	mISDNdevice_t	*dev = dl->dev;
	int		i;

	if (device_debug & DEBUG_MGR_FUNC) {
		printk(KERN_DEBUG "%s: dl(%p) inst(%p) LM(%x) dev(%p) nexti(%p)\n", 
			__FUNCTION__, dl, inst, inst->pid.layermask, dev, inst->next);
		printk(KERN_DEBUG "%s: iaddr %x inst %s slave %p\n",
			__FUNCTION__, dl->iaddr, inst->name, dl->slave);
	}
	remove_if(dl, IF_UP);
	remove_if(dl, IF_DOWN);
	if (dl->slave) {
		if (dl->slave->obj)
			dl->slave->obj->own_ctrl(dl->slave,
				MGR_UNREGLAYER | REQUEST, NULL);
		else
			dl->slave = NULL; 
	}
	if (dl->lm_st && inst->st) {
		for (i=0; i<= MAX_LAYER_NR; i++) {
			if (dl->lm_st & ISDN_LAYER(i)) {
				inst->st->pid.protocol[i] = ISDN_PID_NONE;
			}
		}
		dl->lm_st = 0;
	}
	if (test_and_clear_bit(FLG_MGR_SETSTACK, &dl->Flags) && inst->st) {
		if (device_debug & DEBUG_MGR_FUNC)
			printk(KERN_DEBUG "del_layer: CLEARSTACK id(%x)\n",
				inst->st->id);
		udev_obj.ctrl(inst->st, MGR_CLEARSTACK | REQUEST, NULL);
	}
	if (inst->up.peer) {
		inst->up.peer->obj->ctrl(inst->up.peer,
			MGR_DISCONNECT | REQUEST, &inst->up);
	}
	if (inst->down.peer) {
		inst->down.peer->obj->ctrl(inst->down.peer,
			MGR_DISCONNECT | REQUEST, &inst->down);
	}
	dl->iaddr = 0;
	REMOVE_FROM_LISTBASE(dl, dev->layer);
	udev_obj.ctrl(inst, MGR_UNREGLAYER | REQUEST, NULL);
	if (test_and_clear_bit(FLG_MGR_OWNSTACK, &dl->Flags)) {
		if (dl->inst.st) {
			del_stack(get_devstack(dev, dl->inst.st->id));
		}
	}
	kfree(dl);
	return(0);
}

static mISDNinstance_t *
clone_instance(devicelayer_t *dl, mISDNstack_t  *st, mISDNinstance_t *peer) {
	int		err;

	if (dl->slave) {
		printk(KERN_WARNING "%s: layer has slave, cannot clone\n",
			__FUNCTION__);
		return(NULL);
	}
	if (!(peer->extentions & EXT_INST_CLONE)) {
		printk(KERN_WARNING "%s: peer cannot clone\n", __FUNCTION__);
		return(NULL);
	}
	dl->slave = (mISDNinstance_t *)st;
	if ((err = peer->obj->own_ctrl(peer, MGR_CLONELAYER | REQUEST,
		&dl->slave))) {
		dl->slave = NULL;
		printk(KERN_WARNING "%s: peer clone error %d\n",
			__FUNCTION__, err);
		return(NULL);
	}
	return(dl->slave);
}

static int
connect_if_req(mISDNdevice_t *dev, iframe_t *iff) {
	devicelayer_t *dl;
	interface_info_t *ifi = (interface_info_t *)&iff->data.p;
	mISDNinstance_t *owner;
	mISDNinstance_t *peer;
	mISDNinstance_t *pp;
	mISDNif_t	*hifp;
	int		stat;

	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG "%s: addr:%x own(%x) peer(%x)\n",
			__FUNCTION__, iff->addr, ifi->owner, ifi->peer);
	if (!(dl=get_devlayer(dev, ifi->owner))) {
		int_errtxt("no devive_layer for %08x", ifi->owner);
		return(-ENXIO);
	}
	if (!(owner = get_instance4id(ifi->owner))) {
		printk(KERN_WARNING "%s: owner(%x) not found\n",
			__FUNCTION__, ifi->owner);
		return(-ENODEV);
	}
	if (!(peer = get_instance4id(ifi->peer))) {
		printk(KERN_WARNING "%s: peer(%x) not found\n",
			__FUNCTION__, ifi->peer);
		return(-ENODEV);
	}
	if (owner->pid.layermask < peer->pid.layermask) {
		hifp = &peer->down;
		stat = IF_DOWN;
	} else if (owner->pid.layermask > peer->pid.layermask) {
		hifp = &peer->up;
		stat = IF_UP;
	} else {
		int_errtxt("OLM == PLM: %x", owner->pid.layermask);
		return(-EINVAL);
	}
	if (ifi->extentions == EXT_IF_CHAIN) {
		if (!(pp = hifp->peer)) {
			printk(KERN_WARNING "%s: peer if has no peer\n",
				__FUNCTION__);
			return(-EINVAL);
		}
		if (stat == IF_UP) {
			memcpy(&owner->up, hifp, sizeof(mISDNif_t));
			memcpy(&dl->s_up, hifp, sizeof(mISDNif_t));
			owner->up.owner = owner;
			hifp->peer = owner;
			hifp->func = from_up_down;
			hifp->fdata = dl;
			hifp = &pp->down;
			memcpy(&owner->down, hifp, sizeof(mISDNif_t));
			memcpy(&dl->s_down, hifp, sizeof(mISDNif_t));
			owner->down.owner = owner;
			hifp->peer = owner;
			hifp->func = from_up_down;
			hifp->fdata = dl;
		} else {
			memcpy(&owner->down, hifp, sizeof(mISDNif_t));
			memcpy(&dl->s_down, hifp, sizeof(mISDNif_t));
			owner->up.owner = owner;
			hifp->peer = owner;
			hifp->func = from_up_down;
			hifp->fdata = dl;
			hifp = &pp->up;
			memcpy(&owner->up, hifp, sizeof(mISDNif_t));
			memcpy(&dl->s_up, hifp, sizeof(mISDNif_t));
			owner->down.owner = owner;
			hifp->peer = owner;
			hifp->func = from_up_down;
			hifp->fdata = dl;
		}
		return(0);
	}
	if (ifi->extentions & EXT_IF_CREATE) {
		/* create new instance if allready in use */
		if (hifp->stat != IF_NOACTIV) {
			if ((peer = clone_instance(dl, owner->st, peer))) {
				if (stat == IF_UP)
					hifp = &peer->up;
				else
					hifp = &peer->down;
			} else {
				printk(KERN_WARNING "%s: cannot create new peer instance\n",
					__FUNCTION__);
				return(-EBUSY);
			}
		}
	}
	if (ifi->extentions & EXT_IF_EXCLUSIV) {
		if (hifp->stat != IF_NOACTIV) {
			printk(KERN_WARNING "%s: peer if is in use\n",
				__FUNCTION__);
			return(-EBUSY);
		}
	}			
	return(mISDN_ConnectIF(owner, peer));
}

static int
set_if_req(mISDNdevice_t *dev, iframe_t *iff) {
	mISDNif_t *hif,*phif,*shif;
	int stat;
	interface_info_t *ifi = (interface_info_t *)&iff->data.p;
	devicelayer_t *dl;
	mISDNinstance_t *inst, *peer;

	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG "%s: addr:%x own(%x) peer(%x)\n",
			__FUNCTION__, iff->addr, ifi->owner, ifi->peer);
	if (!(dl=get_devlayer(dev, iff->addr)))
		return(-ENXIO);
	if (!(inst = get_instance4id(ifi->owner))) {
		printk(KERN_WARNING "%s: owner(%x) not found\n",
			__FUNCTION__, ifi->owner);
		return(-ENODEV);
	}
	if (!(peer = get_instance4id(ifi->peer))) {
		printk(KERN_WARNING "%s: peer(%x) not found\n",
			__FUNCTION__, ifi->peer);
		return(-ENODEV);
	}

	if (ifi->stat == IF_UP) {
		hif = &dl->inst.up;
		phif = &peer->down;
		shif = &dl->s_up;
		stat = IF_DOWN;
	} else if (ifi->stat == IF_DOWN) {
		hif = &dl->inst.down;
		shif = &dl->s_down;
		phif = &peer->up;
		stat = IF_UP;
	} else {
		printk(KERN_WARNING "%s: if not UP/DOWN\n", __FUNCTION__);
		return(-EINVAL);
	}

	
	if (shif->stat != IF_NOACTIV) {
		printk(KERN_WARNING "%s: save if busy\n", __FUNCTION__);
		return(-EBUSY);
	}
	if (hif->stat != IF_NOACTIV) {
		printk(KERN_WARNING "%s: own if busy\n", __FUNCTION__);
		return(-EBUSY);
	}
	hif->stat = stat;
	hif->owner = inst;
	memcpy(shif, phif, sizeof(mISDNif_t));
	memset(phif, 0, sizeof(mISDNif_t));
	return(peer->obj->own_ctrl(peer, iff->prim, hif));
}

static int
add_if_req(mISDNdevice_t *dev, iframe_t *iff) {
	mISDNif_t *hif;
	interface_info_t *ifi = (interface_info_t *)&iff->data.p;
	mISDNinstance_t *inst, *peer;

	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG "%s: addr:%x own(%x) peer(%x)\n",
			__FUNCTION__, iff->addr, ifi->owner, ifi->peer);
	if (!(inst = get_instance4id(ifi->owner))) {
		printk(KERN_WARNING "%s: owner(%x) not found\n",
			__FUNCTION__, ifi->owner);
		return(-ENODEV);
	}
	if (!(peer = get_instance4id(ifi->peer))) {
		printk(KERN_WARNING "%s: peer(%x) not found\n",
			__FUNCTION__, ifi->peer);
		return(-ENODEV);
	}

	if (ifi->stat == IF_DOWN) {
		hif = &inst->up;
	} else if (ifi->stat == IF_UP) {
		hif = &inst->down;
	} else {
		printk(KERN_WARNING "%s: if not UP/DOWN\n", __FUNCTION__);
		return(-EINVAL);
	}
	return(peer->obj->ctrl(peer, iff->prim, hif));
}

static int
del_if_req(mISDNdevice_t *dev, iframe_t *iff)
{
	devicelayer_t *dl;

	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG "%s: addr:%x\n", __FUNCTION__, iff->addr);
	if (!(dl=get_devlayer(dev, iff->addr)))
		return(-ENXIO);
	return(remove_if(dl, iff->addr));
}

static void
dev_expire_timer(mISDNtimer_t *ht)
{
	iframe_t off;

	if (device_debug & DEBUG_DEV_TIMER)
		printk(KERN_DEBUG "%s: timer(%x)\n", __FUNCTION__, ht->id);
	if (test_and_clear_bit(FLG_MGR_TIMER_RUNING, &ht->Flags)) {
		off.dinfo = 0;
		off.prim = MGR_TIMER | INDICATION;
		off.addr = ht->id;
		off.len = 0;
		mISDN_rdata(ht->dev, &off, 0);
	} else
		printk(KERN_WARNING "%s: timer(%x) not active\n",
			__FUNCTION__, ht->id);
}

static int
new_entity_req(mISDNdevice_t *dev, int *entity)
{
	int		ret;
	entity_item_t	*ei = kmalloc(sizeof(entity_item_t), GFP_ATOMIC);

	if (!ei)
		return(-ENOMEM);
	ret = mISDN_alloc_entity(entity);
	ei->entity = *entity;
	if (ret)
		kfree(entity);
	else
		list_add((struct list_head *)ei, &dev->entitylist);
	return(ret);
}

static int
del_entity_req(mISDNdevice_t *dev, int entity)
{
	struct list_head	*item, *nxt;

	list_for_each_safe(item, nxt, &dev->entitylist) {
		if (((entity_item_t *)item)->entity == entity) {
			list_del(item);
			mISDN_delete_entity(entity);
			kfree(item);
			return(0);
		}
	}
	return(-ENODEV);
}

static int
dev_init_timer(mISDNdevice_t *dev, iframe_t *iff)
{
	mISDNtimer_t	*ht;

	ht = get_devtimer(dev, iff->addr);
	if (!ht) {
		ht = kmalloc(sizeof(mISDNtimer_t), GFP_ATOMIC);
		if (!ht)
			return(-ENOMEM);
		ht->prev = NULL;
		ht->next = NULL;
		ht->dev = dev;
		ht->id = iff->addr;
		ht->tl.data = (long) ht;
		ht->tl.function = (void *) dev_expire_timer;
		init_timer(&ht->tl);
		APPEND_TO_LIST(ht, dev->timer);
		if (device_debug & DEBUG_DEV_TIMER)
			printk(KERN_DEBUG "%s: new(%x)\n", __FUNCTION__, ht->id);
	} else if (device_debug & DEBUG_DEV_TIMER)
		printk(KERN_DEBUG "%s: old(%x)\n", __FUNCTION__, ht->id);
	if (timer_pending(&ht->tl)) {
		printk(KERN_WARNING "%s: timer(%x) pending\n", __FUNCTION__,
			ht->id);
		del_timer(&ht->tl);
	}
	init_timer(&ht->tl);
	test_and_set_bit(FLG_MGR_TIMER_INIT, &ht->Flags);
	return(0);
}

static int
dev_add_timer(mISDNdevice_t *dev, iframe_t *iff)
{
	mISDNtimer_t	*ht;

	ht = get_devtimer(dev, iff->addr);
	if (!ht) {
		printk(KERN_WARNING "%s: no timer(%x)\n", __FUNCTION__,
			iff->addr);
		return(-ENODEV);
	}
	if (timer_pending(&ht->tl)) {
		printk(KERN_WARNING "%s: timer(%x) pending\n",
			__FUNCTION__, ht->id);
		return(-EBUSY);
	}
	if (iff->dinfo < 10) {
		printk(KERN_WARNING "%s: timer(%x): %d ms too short\n",
			__FUNCTION__, ht->id, iff->dinfo);
		return(-EINVAL);
	}
	if (device_debug & DEBUG_DEV_TIMER)
		printk(KERN_DEBUG "%s: timer(%x) %d ms\n",
			__FUNCTION__, ht->id, iff->dinfo);
	init_timer(&ht->tl);
	ht->tl.expires = jiffies + (iff->dinfo * HZ) / 1000;
	test_and_set_bit(FLG_MGR_TIMER_RUNING, &ht->Flags);
	add_timer(&ht->tl);
	return(0);
}

static int
dev_del_timer(mISDNdevice_t *dev, iframe_t *iff)
{
	mISDNtimer_t	*ht;

	ht = get_devtimer(dev, iff->addr);
	if (!ht) {
		printk(KERN_WARNING "%s: no timer(%x)\n", __FUNCTION__,
			iff->addr);
		return(-ENODEV);
	}
	if (device_debug & DEBUG_DEV_TIMER)
		printk(KERN_DEBUG "%s: timer(%x)\n",
			__FUNCTION__, ht->id);
	del_timer(&ht->tl);
	if (!test_and_clear_bit(FLG_MGR_TIMER_RUNING, &ht->Flags))
		printk(KERN_WARNING "%s: timer(%x) not running\n",
			__FUNCTION__, ht->id);
	return(0);
}

static int
dev_remove_timer(mISDNdevice_t *dev, int id)
{
	mISDNtimer_t	*ht;

	ht = get_devtimer(dev, id);
	if (!ht)  {
		printk(KERN_WARNING "%s: no timer(%x)\n", __FUNCTION__, id);
		return(-ENODEV);
	}
	if (device_debug & DEBUG_DEV_TIMER)
		printk(KERN_DEBUG "%s: timer(%x)\n", __FUNCTION__, ht->id);
	del_timer(&ht->tl);
	REMOVE_FROM_LISTBASE(ht, dev->timer);
	kfree(ht);
	return(0);
}

static int
get_status(iframe_t *off)
{
	status_info_t	*si = (status_info_t *)off->data.p;
	mISDNinstance_t	*inst;
	int err;

	if (!(inst = get_instance4id(off->addr & IF_ADDRMASK))) {
		printk(KERN_WARNING "%s: no instance\n", __FUNCTION__);
		err = -ENODEV;
	} else {
		err = inst->obj->own_ctrl(inst, MGR_STATUS | REQUEST, si);
	}
	if (err)
		off->len = err;
	else
		off->len = si->len + 2*sizeof(int);
	return(err);	
}

static void
get_layer_info(iframe_t *frm)
{
	mISDNinstance_t *inst;
	layer_info_t	*li = (layer_info_t *)frm->data.p;
	
	if (!(inst = get_instance4id(frm->addr & IF_ADDRMASK))) {
		printk(KERN_WARNING "%s: no instance\n", __FUNCTION__);
		frm->len = -ENODEV;
		return;
	}
	memset(li, 0, sizeof(layer_info_t));
	if (inst->obj)
		li->object_id = inst->obj->id;
	strcpy(li->name, inst->name);
	li->extentions = inst->extentions;
	li->id = inst->id;
	if (inst->st)
		li->st = inst->st->id;
	memcpy(&li->pid, &inst->pid, sizeof(mISDN_pid_t));
	frm->len = sizeof(layer_info_t);
}

static void
get_if_info(iframe_t *frm)
{
	mISDNinstance_t		*inst;
	mISDNif_t		*hif;
	interface_info_t	*ii = (interface_info_t *)frm->data.p;
	
	if (!(inst = get_instance4id(frm->addr & IF_ADDRMASK))) {
		printk(KERN_WARNING "%s: no instance\n", __FUNCTION__);
		frm->len = -ENODEV;
		return;
	}
	if (frm->dinfo == IF_DOWN)
		hif = &inst->down;
	else if (frm->dinfo == IF_UP)
		hif = &inst->up;
	else {
		printk(KERN_WARNING "%s: wrong interface %x\n",
			__FUNCTION__, frm->dinfo);
		frm->len = -EINVAL;
		return;
	}
	frm->dinfo = 0;
	memset(ii, 0, sizeof(interface_info_t));
	if (hif->owner)
		ii->owner = hif->owner->id;
	if (hif->peer)
		ii->peer = hif->peer->id;
	ii->extentions = hif->extentions;
	ii->stat = hif->stat;
	frm->len = sizeof(interface_info_t);
}

static int
wdata_frame(mISDNdevice_t *dev, iframe_t *iff) {
	mISDNif_t *hif = NULL;
	devicelayer_t *dl;
	int err=-ENXIO;

	if (device_debug & DEBUG_WDATA)
		printk(KERN_DEBUG "%s: addr:%x\n", __FUNCTION__, iff->addr);
	if (!(dl=get_devlayer(dev, iff->addr)))
		return(-ENXIO);
	if (iff->addr & IF_UP) {
		hif = &dl->inst.up;
		if (IF_TYPE(hif) != IF_DOWN) {
			printk(KERN_WARNING "%s: inst.up no down\n", __FUNCTION__);
			hif = NULL;
		}
	} else if (iff->addr & IF_DOWN) {
		hif = &dl->inst.down;
		if (IF_TYPE(hif) != IF_UP) {
			printk(KERN_WARNING "%s: inst.down no up\n", __FUNCTION__);
			hif = NULL;
		}
	}
	if (hif) {
		if (device_debug & DEBUG_WDATA)
			printk(KERN_DEBUG "%s: pr(%x) di(%x) l(%d)\n",
				__FUNCTION__, iff->prim, iff->dinfo, iff->len);
		if (iff->len < 0) {
			printk(KERN_WARNING "%s: data negativ(%d)\n",
				__FUNCTION__, iff->len);
			return(-EINVAL);
		}
		err = if_link(hif, iff->prim, iff->dinfo, iff->len,
			&iff->data.b[0], L3_EXTRA_SIZE);
		if (device_debug & DEBUG_WDATA && err)
			printk(KERN_DEBUG "%s: if_link ret(%x)\n",
				__FUNCTION__, err);
	} else {
		if (device_debug & DEBUG_WDATA)
			printk(KERN_DEBUG "mISDN: no matching interface\n");
	}
	return(err);
}

static int
mISDN_wdata_if(mISDNdevice_t *dev, iframe_t *iff, int len) {
	iframe_t        off;
	mISDNstack_t	*st;
	devicelayer_t	*dl;
	mISDNlayer_t    *layer;
	int		lay;
	int		err = 0;
	int		used = 0;
	int		head = 4*sizeof(u_int);

	if (len < head) {
		printk(KERN_WARNING "%s: frame(%d) too short\n",
			__FUNCTION__, len);
		return(len);
	}
	if (device_debug & DEBUG_WDATA)
		printk(KERN_DEBUG "mISDN_wdata: %x:%x %x %d\n",
			iff->addr, iff->prim, iff->dinfo, iff->len);
	switch(iff->prim) {
	    case (MGR_GETSTACK | REQUEST):
		used = head;
		off.addr = iff->addr;
		off.prim = MGR_GETSTACK | CONFIRM;
		off.dinfo = 0;
		if (iff->addr <= 0) {
			off.dinfo = get_stack_cnt();
			off.len = 0;
		} else {
			off.data.p = stbuf;
			get_stack_info(&off);
		}
		mISDN_rdata(dev, &off, 0);
		break;
	    case (MGR_SETSTACK | REQUEST):
		used = head + sizeof(mISDN_pid_t);
		if (len<used)
			return(len);
		off.addr = iff->addr;
		off.dinfo = 0;
		off.prim = MGR_SETSTACK | CONFIRM;
		off.len = 0;
		if ((st = get_stack4id(iff->addr))) {
			stack_inst_flg(dev, st, FLG_MGR_SETSTACK, 0);
			err = udev_obj.ctrl(st, iff->prim, &iff->data.i);
			if (err<0)
				off.len = err;
		} else
			off.len = -ENODEV;
		mISDN_rdata(dev, &off, 1);
		break;
	    case (MGR_NEWSTACK | REQUEST):
		used = head + iff->len;
		if (len<used)
			return(len);
		off.addr = iff->addr;
		off.dinfo = 0;
		off.prim = MGR_NEWSTACK | CONFIRM;
		off.len = 0;
		err = new_devstack(dev, (stack_info_t *)&iff->data.p);
		if (err<0)
			off.len = err;
 		else
 			off.dinfo = err;
		mISDN_rdata(dev, &off, 1);
		break;	
	    case (MGR_CLEARSTACK | REQUEST):
		used = head;
		off.addr = iff->addr;
		off.prim = MGR_CLEARSTACK | CONFIRM;
		off.dinfo = 0;
		off.len = 0;
		if ((st = get_stack4id(iff->addr))) {
			stack_inst_flg(dev, st, FLG_MGR_SETSTACK, 1);
			err = udev_obj.ctrl(st, iff->prim, NULL);
			if (err<0)
				off.len = err;
		} else
			off.len = -ENODEV;
		mISDN_rdata(dev, &off, 1);
		break;
	    case (MGR_SELCHANNEL | REQUEST):
		used = head;
		off.addr = iff->addr;
		off.prim = MGR_SELCHANNEL | CONFIRM;
		st = sel_channel(iff->addr, iff->dinfo);
		if (st) {
			off.len = 0;
			off.dinfo = st->id;
		} else {
			off.dinfo = 0;
			off.len = -ENODEV;
		}
		mISDN_rdata(dev, &off, 1);
		break;
	    case (MGR_GETLAYERID | REQUEST):
		used = head;
		off.addr = iff->addr;
		off.prim = MGR_GETLAYERID | CONFIRM;
		off.dinfo = 0;
		lay = iff->dinfo;
		off.len = 0;
		if (LAYER_OUTRANGE(lay)) {
			off.len = -EINVAL;
			mISDN_rdata(dev, &off, 1);
			break;
		} else
			lay = ISDN_LAYER(lay);
		if ((st = get_stack4id(iff->addr))) {
			if ((layer = getlayer4lay(st, lay))) {
				if (layer->inst)
					off.dinfo = layer->inst->id;
			}
		}
		mISDN_rdata(dev, &off, 0);
		break;
	    case (MGR_GETLAYER | REQUEST):
		used = head;
		off.addr = iff->addr;
		off.prim = MGR_GETLAYER | CONFIRM;
		off.dinfo = 0;
		off.len = 0;
		off.data.p = stbuf;
		get_layer_info(&off);
		mISDN_rdata(dev, &off, 0);
		break;
	    case (MGR_NEWLAYER | REQUEST):
		used = head + sizeof(layer_info_t);
		if (len<used)
			return(len);
		off.addr = iff->addr;
		off.dinfo = 0;
		off.prim = MGR_NEWLAYER | CONFIRM;
		off.data.p = stbuf;
		off.len = create_layer(dev, (layer_info_t *)&iff->data.i,
			(int *)stbuf);
		mISDN_rdata(dev, &off, 0);
		break;	
	    case (MGR_DELLAYER | REQUEST):
		used = head;
		off.addr = iff->addr;
		off.prim = MGR_DELLAYER | CONFIRM;
		off.dinfo = 0;
		if ((dl=get_devlayer(dev, iff->addr)))
			off.len = del_layer(dl);
		else
			off.len = -ENXIO;
		mISDN_rdata(dev, &off, 1);
		break;
	    case (MGR_GETIF | REQUEST):
		used = head;
		off.addr = iff->addr;
		off.prim = MGR_GETIF | CONFIRM;
		off.dinfo = iff->dinfo;
		off.len = 0;
		off.data.p = stbuf;
		get_if_info(&off);
		mISDN_rdata(dev, &off, 0);
		break;
	    case (MGR_CONNECT | REQUEST):
		used = head + sizeof(interface_info_t);
		if (len<used)
			return(len);
		off.addr = iff->addr;
		off.prim = MGR_CONNECT | CONFIRM;
		off.dinfo = 0;
		off.len = connect_if_req(dev, iff);
		mISDN_rdata(dev, &off, 1);
		break;
	    case (MGR_SETIF | REQUEST):
		used = head + iff->len;
		if (len<used)
			return(len);
		off.addr = iff->addr;
		off.prim = MGR_SETIF | CONFIRM;
		off.dinfo = 0;
		off.len = set_if_req(dev, iff);
		mISDN_rdata(dev, &off, 1);
		break;
	    case (MGR_ADDIF | REQUEST):
		used = head + iff->len;
		if (len<used)
			return(len);
		off.addr = iff->addr;
		off.prim = MGR_ADDIF | CONFIRM;
		off.dinfo = 0;
		off.len = add_if_req(dev, iff);
		mISDN_rdata(dev, &off, 1);
		break;
	    case (MGR_DISCONNECT | REQUEST):
		used = head;
		off.addr = iff->addr;
		off.prim = MGR_DISCONNECT | CONFIRM;
		off.dinfo = 0;
		off.len = del_if_req(dev, iff);
		mISDN_rdata(dev, &off, 1);
		break;
	    case (MGR_NEWENTITY | REQUEST):
		used = head;
		off.addr = iff->addr;
		off.prim = MGR_NEWENTITY | CONFIRM;
		off.len = new_entity_req(dev, &off.dinfo);
		mISDN_rdata(dev, &off, 1);
		break;
	    case (MGR_DELENTITY | REQUEST):
		used = head;
		off.addr = iff->addr;
		off.prim = MGR_DELENTITY | CONFIRM;
		off.dinfo = iff->dinfo;
		off.len = del_entity_req(dev, iff->dinfo);
		mISDN_rdata(dev, &off, 1);
		break;
	    case (MGR_INITTIMER | REQUEST):
		used = head;
		off.len = dev_init_timer(dev, iff);
		off.addr = iff->addr;
		off.prim = MGR_INITTIMER | CONFIRM;
		off.dinfo = iff->dinfo;
		mISDN_rdata(dev, &off, 0);
		break;
	    case (MGR_ADDTIMER | REQUEST):
		used = head;
		off.len = dev_add_timer(dev, iff);
		off.addr = iff->addr;
		off.prim = MGR_ADDTIMER | CONFIRM;
		off.dinfo = 0;
		mISDN_rdata(dev, &off, 0);
		break;
	    case (MGR_DELTIMER | REQUEST):
		used = head;
		off.len = dev_del_timer(dev, iff);
		off.addr = iff->addr;
		off.prim = MGR_DELTIMER | CONFIRM;
		off.dinfo = iff->dinfo;
		mISDN_rdata(dev, &off, 0);
		break;
	    case (MGR_REMOVETIMER | REQUEST):
		used = head;
		off.len = dev_remove_timer(dev, iff->addr);
		off.addr = iff->addr;
		off.prim = MGR_REMOVETIMER | CONFIRM;
		off.dinfo = 0;
		mISDN_rdata(dev, &off, 0);
		break;
	    case (MGR_TIMER | RESPONSE):
		used = head;
		break;
	    case (MGR_STATUS | REQUEST):
		used = head;
		off.addr = iff->addr;
		off.prim = MGR_STATUS | CONFIRM;
		off.dinfo = 0;
		off.data.p = stbuf;
		if (get_status(&off))
			mISDN_rdata(dev, &off, 1);
		else
			mISDN_rdata(dev, &off, 0);
		break;
	    case (MGR_SETDEVOPT | REQUEST):
	    	used = head;
	    	off.addr = iff->addr;
	    	off.prim = MGR_SETDEVOPT | CONFIRM;
	    	off.dinfo = 0;
	    	off.len = 0;
	    	if (iff->dinfo == FLG_mISDNPORT_ONEFRAME) {
	    		test_and_set_bit(FLG_mISDNPORT_ONEFRAME,
	    			&dev->rport.Flag);
	    	} else if (!iff->dinfo) {
	    		test_and_clear_bit(FLG_mISDNPORT_ONEFRAME,
	    			&dev->rport.Flag);
	    	} else {
	    		off.len = -EINVAL;
	    	}
	    	mISDN_rdata(dev, &off, 0);
	    	break;
	    case (MGR_GETDEVOPT | REQUEST):
	    	used = head;
	    	off.addr = iff->addr;
	    	off.prim = MGR_GETDEVOPT | CONFIRM;
	    	off.len = 0;
	    	if (test_bit(FLG_mISDNPORT_ONEFRAME, &dev->rport.Flag))
	    		off.dinfo = FLG_mISDNPORT_ONEFRAME;
	    	else
	    		off.dinfo = 0;
	    	mISDN_rdata(dev, &off, 0);
	    	break;
	    default:
		used = head + iff->len;
		if (len<used) {
			printk(KERN_WARNING "mISDN_wdata: framelen error prim %x %d/%d\n",
				iff->prim, len, used);
			used=len;
		} else if (iff->addr & IF_TYPEMASK) {
			err = wdata_frame(dev, iff);
			if (err)
				if (device_debug & DEBUG_WDATA)
					printk(KERN_DEBUG "wdata_frame returns error %d\n", err);
		} else {
			printk(KERN_WARNING "mISDN: prim %x addr %x not implemented\n",
				iff->prim, iff->addr);
		}
		break;
	}
	return(used);
}

static int
mISDN_wdata(mISDNdevice_t *dev) {
	int	used = 0;
	u_long	flags;

	spin_lock_irqsave(&dev->wport.lock, flags);
	if (test_and_set_bit(FLG_mISDNPORT_BUSY, &dev->wport.Flag)) {
		spin_unlock_irqrestore(&dev->wport.lock, flags);
		return(0);
	}
	while (1) {
		size_t	frag;

		if (!dev->wport.cnt) {
			wake_up(&dev->wport.procq);
			break;
		}
		if (dev->minor == mISDN_CORE_DEVICE) {
			iframe_t	*iff;
			iframe_t	hlp;
			int		broken = 0;
			
			frag = dev->wport.buf + dev->wport.size
				- dev->wport.op;
			if (dev->wport.cnt < IFRAME_HEAD_SIZE) {
				printk(KERN_WARNING "%s: frame(%d,%d) too short\n",
					__FUNCTION__, dev->wport.cnt, IFRAME_HEAD_SIZE);
				p_pull_o(&dev->wport, dev->wport.cnt);
				wake_up(&dev->wport.procq);
				break;
			}
			if (frag < IFRAME_HEAD_SIZE) {
				broken = 1;
				p_memcpy_o(&dev->wport, &hlp, IFRAME_HEAD_SIZE);
				if (hlp.len >0) {
					if (hlp.len < dev->wport.cnt) {
						printk(KERN_WARNING
							"%s: framedata(%d/%d)too short\n",
							__FUNCTION__, dev->wport.cnt, hlp.len);
						p_pull_o(&dev->wport, dev->wport.cnt);
						wake_up(&dev->wport.procq);
						break;
					}
				}
				iff = &hlp;
			} else {
				iff = (iframe_t *)dev->wport.op;
				if (iff->len > 0) {
					if (dev->wport.cnt < (iff->len + IFRAME_HEAD_SIZE)) {
						printk(KERN_WARNING "%s: frame(%d,%d) too short\n",
							__FUNCTION__, dev->wport.cnt, IFRAME_HEAD_SIZE + iff->len);
						p_pull_o(&dev->wport, dev->wport.cnt);
						wake_up(&dev->wport.procq);
						break;
					}
					if (frag < (size_t)(iff->len + IFRAME_HEAD_SIZE)) {
						broken = 1;
						p_memcpy_o(&dev->wport, &hlp, IFRAME_HEAD_SIZE);
					}
				}
			}
			if (broken) {
				if (hlp.len > 0) {
					iff = vmalloc(IFRAME_HEAD_SIZE + hlp.len);
					if (!iff) {
						printk(KERN_WARNING "%s: no %d vmem for iff\n",
							__FUNCTION__, IFRAME_HEAD_SIZE + hlp.len);
						p_pull_o(&dev->wport, hlp.len);
						wake_up(&dev->wport.procq);
						continue;
					}
					memcpy(iff, &hlp, IFRAME_HEAD_SIZE);
					p_memcpy_o(&dev->wport, &iff->data.p,
						iff->len);
				} else {
					iff = &hlp;
				}
			}
			used = IFRAME_HEAD_SIZE;
			if (iff->len > 0)
				used += iff->len; 
			spin_unlock_irqrestore(&dev->wport.lock, flags);
			mISDN_wdata_if(dev, iff, used);
			if (broken) {
				if (used>IFRAME_HEAD_SIZE)
					vfree(iff);
				spin_lock_irqsave(&dev->wport.lock, flags);
			} else {
				spin_lock_irqsave(&dev->wport.lock, flags);
				p_pull_o(&dev->wport, used);
			}
		} else { /* RAW DEVICES */
			printk(KERN_DEBUG "%s: wflg(%lx)\n",
				__FUNCTION__, dev->wport.Flag);
			if (test_bit(FLG_mISDNPORT_BLOCK, &dev->wport.Flag))
				break;
			used = dev->wport.cnt;
			if (used > MAX_DATA_SIZE)
				used = MAX_DATA_SIZE;
			printk(KERN_DEBUG "%s: cnt %d/%d\n",
				__FUNCTION__, used, dev->wport.cnt);
			if (test_bit(FLG_mISDNPORT_ENABLED, &dev->wport.Flag)) {
				struct sk_buff	*skb;

				skb = alloc_skb(used, GFP_ATOMIC);
				if (skb) {
					p_memcpy_o(&dev->wport, skb_put(skb,
						used), used);
					test_and_set_bit(FLG_mISDNPORT_BLOCK,
						&dev->wport.Flag);
					spin_unlock_irqrestore(&dev->wport.lock, flags);
					used = if_newhead(&dev->wport.pif,
						PH_DATA | REQUEST, (int)skb, skb);
					if (used) {
						printk(KERN_WARNING 
							"%s: dev(%d) down err(%d)\n",
							__FUNCTION__, dev->minor, used);
						kfree_skb(skb);
					}
					spin_lock_irqsave(&dev->wport.lock, flags);
				} else {
					printk(KERN_WARNING
						"%s: dev(%d) no skb(%d)\n",
						__FUNCTION__, dev->minor, used);
					p_pull_o(&dev->wport, used);
				}
			} else {
				printk(KERN_WARNING
					"%s: dev(%d) wport not enabled\n",
					__FUNCTION__, dev->minor);
				p_pull_o(&dev->wport, used);
			}
		}
		wake_up(&dev->wport.procq);
	}
	test_and_clear_bit(FLG_mISDNPORT_BUSY, &dev->wport.Flag);
	spin_unlock_irqrestore(&dev->wport.lock, flags);
	return(0);
}

static mISDNdevice_t *
init_device(u_int minor) {
	mISDNdevice_t	*dev;
	u_long		flags;

	dev = kmalloc(sizeof(mISDNdevice_t), GFP_KERNEL);
	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG "%s: dev(%d) %p\n",
			__FUNCTION__, minor, dev); 
	if (dev) {
		memset(dev, 0, sizeof(mISDNdevice_t));
		dev->minor = minor;
		init_waitqueue_head(&dev->rport.procq);
		init_waitqueue_head(&dev->wport.procq);
		init_MUTEX(&dev->io_sema);
		INIT_LIST_HEAD(&dev->entitylist);
		write_lock_irqsave(&mISDN_device_lock, flags);
		APPEND_TO_LIST(dev, mISDN_devicelist);
		write_unlock_irqrestore(&mISDN_device_lock, flags);
	}
	return(dev);
}

mISDNdevice_t *
get_free_rawdevice(void)
{
	mISDNdevice_t	*dev;
	u_int		minor;

	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG "%s:\n", __FUNCTION__);
	for (minor=mISDN_MINOR_RAW_MIN; minor<=mISDN_MINOR_RAW_MAX; minor++) {
		dev = get_mISDNdevice4minor(minor);
		if (device_debug & DEBUG_MGR_FUNC)
			printk(KERN_DEBUG "%s: dev(%d) %p\n",
				__FUNCTION__, minor, dev); 
		if (!dev) {
			dev = init_device(minor);
			if (!dev)
				return(NULL);
			dev->rport.pif.func = mISDN_rdata_raw;
			dev->rport.pif.fdata = dev;
			return(dev);
		}
	}
	return(NULL);
}

int
free_device(mISDNdevice_t *dev)
{
	u_long	flags;

	if (!dev)
		return(-ENODEV);
	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG "%s: dev(%d)\n", __FUNCTION__, dev->minor);
	/* release related stuff */
	while(dev->layer)
		del_layer(dev->layer);
	while(dev->stack)
		del_stack(dev->stack);
	while(dev->timer)
		dev_remove_timer(dev, dev->timer->id);
	if (dev->rport.buf)
		vfree(dev->rport.buf);
	if (dev->wport.buf)
		vfree(dev->wport.buf);
	write_lock_irqsave(&mISDN_device_lock, flags);
	REMOVE_FROM_LISTBASE(dev, mISDN_devicelist);
	write_unlock_irqrestore(&mISDN_device_lock, flags);
	if (!list_empty(&dev->entitylist)) {
		printk(KERN_WARNING "MISDN %s: entitylist not empty\n", __FUNCTION__);
		while(!list_empty(&dev->entitylist)) {
			struct entity_item	*ei;
			ei = (struct entity_item *)dev->entitylist.next;
			list_del((struct list_head *)ei);
			mISDN_delete_entity(ei->entity);
			kfree(ei);
		}
	}
	kfree(dev);
	return(0);
}

static int
mISDN_open(struct inode *ino, struct file *filep)
{
	u_int		minor = iminor(ino);
	mISDNdevice_t 	*dev = NULL;
	u_long		flags;
	int		isnew = 0;

	if (device_debug & DEBUG_DEV_OP)
		printk(KERN_DEBUG "mISDN_open in: minor(%d) %p %p mode(%x)\n",
			minor, filep, filep->private_data, filep->f_mode);
	if (minor) {
		dev = get_mISDNdevice4minor(minor);
		if (dev) {
			if ((dev->open_mode & filep->f_mode) & (FMODE_READ | FMODE_WRITE))
				return(-EBUSY);
		} else
			return(-ENODEV);
	} else if ((dev = init_device(minor)))
		isnew = 1;
	else
		return(-ENOMEM);
	dev->open_mode |= filep->f_mode & (FMODE_READ | FMODE_WRITE);
	if (dev->open_mode & FMODE_READ){
		if (!dev->rport.buf) {
			dev->rport.buf = vmalloc(mISDN_DEVBUF_SIZE);
			if (!dev->rport.buf) {
				if (isnew) {
					write_lock_irqsave(&mISDN_device_lock, flags);
					REMOVE_FROM_LISTBASE(dev, mISDN_devicelist);
					write_unlock_irqrestore(&mISDN_device_lock, flags);
					kfree(dev);
				}
				return(-ENOMEM);
			}
			dev->rport.lock = SPIN_LOCK_UNLOCKED;
			dev->rport.size = mISDN_DEVBUF_SIZE;
		}
		test_and_set_bit(FLG_mISDNPORT_OPEN, &dev->rport.Flag);
		dev->rport.ip = dev->rport.op = dev->rport.buf;
		dev->rport.cnt = 0;
	}
	if (dev->open_mode & FMODE_WRITE) {
		if (!dev->wport.buf) {
			dev->wport.buf = vmalloc(mISDN_DEVBUF_SIZE);
			if (!dev->wport.buf) {
				if (isnew) {
					if (dev->rport.buf)
						vfree(dev->rport.buf);
					write_lock_irqsave(&mISDN_device_lock, flags);
					REMOVE_FROM_LISTBASE(dev, mISDN_devicelist);
					write_unlock_irqrestore(&mISDN_device_lock, flags);
					kfree(dev);
				}
				return(-ENOMEM);
			}
			dev->wport.lock = SPIN_LOCK_UNLOCKED;
			dev->wport.size = mISDN_DEVBUF_SIZE;
		}
		test_and_set_bit(FLG_mISDNPORT_OPEN, &dev->wport.Flag);
		dev->wport.ip = dev->wport.op = dev->wport.buf;
		dev->wport.cnt = 0;
	}
	filep->private_data = dev;
	if (device_debug & DEBUG_DEV_OP)
		printk(KERN_DEBUG "mISDN_open out: %p %p\n", filep, filep->private_data);
	return(0);
}

static int
mISDN_close(struct inode *ino, struct file *filep)
{
	mISDNdevice_t	*dev = mISDN_devicelist;

	if (device_debug & DEBUG_DEV_OP)
		printk(KERN_DEBUG "mISDN: mISDN_close %p %p\n", filep, filep->private_data);
	read_lock(&mISDN_device_lock);
	while (dev) {
		if (dev == filep->private_data) {
			if (device_debug & DEBUG_DEV_OP)
				printk(KERN_DEBUG "mISDN: dev(%d) %p mode %x/%x\n",
					dev->minor, dev, dev->open_mode, filep->f_mode);
			dev->open_mode &= ~filep->f_mode;
			read_unlock(&mISDN_device_lock);
			if (filep->f_mode & FMODE_READ) {
				test_and_clear_bit(FLG_mISDNPORT_OPEN,
					&dev->rport.Flag);
			}
			if (filep->f_mode & FMODE_WRITE) {
				test_and_clear_bit(FLG_mISDNPORT_OPEN,
					&dev->wport.Flag);
			}
			filep->private_data = NULL;
			if (!dev->minor)
				free_device(dev);
			return 0;
		}
		dev = dev->next;
	}
	read_unlock(&mISDN_device_lock);
	printk(KERN_WARNING "mISDN: No private data while closing device\n");
	return 0;
}

static __inline__ ssize_t
do_mISDN_read(struct file *file, char *buf, size_t count, loff_t * off)
{
	mISDNdevice_t	*dev = file->private_data;
	size_t		len, frag;
	u_long		flags;

	if (off != &file->f_pos)
		return(-ESPIPE);
	if (!dev->rport.buf)
		return -EINVAL;	
	if (!access_ok(VERIFY_WRITE, buf, count))
		return(-EFAULT);
	if (device_debug & DEBUG_DEV_OP)
		printk(KERN_DEBUG "mISDN_read: file(%d) %p max %d\n",
			dev->minor, file, count);
	while (!dev->rport.cnt) {
		if (file->f_flags & O_NONBLOCK)
			return(-EAGAIN);
		interruptible_sleep_on(&(dev->rport.procq));
		if (signal_pending(current))
			return(-ERESTARTSYS);
	}
	spin_lock_irqsave(&dev->rport.lock, flags);
	if (test_bit(FLG_mISDNPORT_ONEFRAME, &dev->rport.Flag)) {
		len = next_frame_len(&dev->rport);
		if (!len) {
			spin_unlock_irqrestore(&dev->rport.lock, flags);
			return(-EINVAL);
		}
		if (count < len) {
			spin_unlock_irqrestore(&dev->rport.lock, flags);
			return(-ENOSPC);
		}
	} else {
		if (count < (size_t)dev->rport.cnt)
			len = count;
		else
			len = dev->rport.cnt;
	}
	frag = dev->rport.buf + dev->rport.size - dev->rport.op;
	if (frag <= len) {
		if (copy_to_user(buf, dev->rport.op, frag)) {
			spin_unlock_irqrestore(&dev->rport.lock, flags);
			return(-EFAULT);
		}
		len -= frag;
		dev->rport.op = dev->rport.buf;
		dev->rport.cnt -= frag;
	} else
		frag = 0;
	if (len) {
		if (copy_to_user(buf + frag, dev->rport.op, len)) {
			spin_unlock_irqrestore(&dev->rport.lock, flags);
			return(-EFAULT);
		}
		dev->rport.cnt -= len;
		dev->rport.op += len;
	}
	*off += len + frag;
	spin_unlock_irqrestore(&dev->rport.lock, flags);
	if (device_debug & DEBUG_DEV_OP)
		printk(KERN_DEBUG "mISDN_read: file(%d) %d\n",
			dev->minor, len + frag);
	return(len + frag);
}

static ssize_t
mISDN_read(struct file *file, char *buf, size_t count, loff_t * off)
{
	mISDNdevice_t	*dev = file->private_data;
	ssize_t		ret;

	if (!dev)
		return(-ENODEV);
	down(&dev->io_sema);
	ret = do_mISDN_read(file, buf, count, off);
	up(&dev->io_sema);
	return(ret);
}

static loff_t
mISDN_llseek(struct file *file, loff_t offset, int orig)
{
	return -ESPIPE;
}

static __inline__ ssize_t
do_mISDN_write(struct file *file, const char *buf, size_t count, loff_t * off)
{
	mISDNdevice_t	*dev = file->private_data;
	size_t		len, frag;
	u_long		flags;

	if (off != &file->f_pos)
		return(-ESPIPE);
	if (device_debug & DEBUG_DEV_OP)
		printk(KERN_DEBUG "mISDN_write: file(%d) %p count %d/%d/%d\n",
			dev->minor, file, count, dev->wport.cnt, dev->wport.size);
	if (!dev->wport.buf)
		return -EINVAL;	
	if (!access_ok(VERIFY_WRITE, buf, count))
		return(-EFAULT);
	if (count > (size_t)dev->wport.size)
		return(-ENOSPC);
	spin_lock_irqsave(&dev->wport.lock, flags);
	while ((size_t)(dev->wport.size - dev->wport.cnt) < count) {
		spin_unlock_irqrestore(&dev->wport.lock, flags);
		if (file->f_flags & O_NONBLOCK)
			return(-EAGAIN);
		interruptible_sleep_on(&(dev->wport.procq));
		if (signal_pending(current))
			return(-ERESTARTSYS);
		spin_lock_irqsave(&dev->wport.lock, flags);
	}
	len = count;
	frag = dev->wport.buf + dev->wport.size - dev->wport.ip;
	if (frag <= len) {
		copy_from_user(dev->wport.ip, buf, frag);
		dev->wport.ip = dev->wport.buf;
		len -= frag;
		dev->wport.cnt += frag;
	} else
		frag = 0;
	if (len) {
		copy_from_user(dev->wport.ip, buf + frag, len);
		dev->wport.cnt += len;
		dev->wport.ip += len;
	}
	spin_unlock_irqrestore(&dev->wport.lock, flags);
	mISDN_wdata(dev);
	return(count);
}

static ssize_t
mISDN_write(struct file *file, const char *buf, size_t count, loff_t * off)
{
	mISDNdevice_t	*dev = file->private_data;
	ssize_t		ret;

	if (!dev)
		return(-ENODEV);
	down(&dev->io_sema);
	ret = do_mISDN_write(file, buf, count, off);
	up(&dev->io_sema);
	return(ret);
}

static unsigned int
mISDN_poll(struct file *file, poll_table * wait)
{
	unsigned int	mask = POLLERR;
	mISDNdevice_t	*dev = file->private_data;
	mISDNport_t	*rport = (file->f_mode & FMODE_READ) ?
					&dev->rport : NULL;
	mISDNport_t	*wport = (file->f_mode & FMODE_WRITE) ?
					&dev->wport : NULL;

	if (dev) {
		if (device_debug & DEBUG_DEV_OP)
			printk(KERN_DEBUG "mISDN_poll in: file(%d) %p\n",
				dev->minor, file);
		if (rport) {
			poll_wait(file, &rport->procq, wait);
			mask = 0;
			if (rport->cnt)
				mask |= (POLLIN | POLLRDNORM);
		}
		if (wport) {
			poll_wait(file, &wport->procq, wait);
			if (mask == POLLERR)
				mask = 0;
			if (wport->cnt < wport->size)
				mask |= (POLLOUT | POLLWRNORM);
		}
	}
	if (device_debug & DEBUG_DEV_OP)
		printk(KERN_DEBUG "mISDN_poll out: file %p mask %x\n",
			file, mask);
	return(mask);
}

static struct file_operations mISDN_fops =
{
	llseek:		mISDN_llseek,
	read:		mISDN_read,
	write:		mISDN_write,
	poll:		mISDN_poll,
//	ioctl:		mISDN_ioctl,
	open:		mISDN_open,
	release:	mISDN_close,
};

static int
from_up_down(mISDNif_t *hif, struct sk_buff *skb) {
	
	devicelayer_t	*dl;
	iframe_t	off;
	mISDN_head_t	*hh; 
	int		retval = -EINVAL;

	if (!hif || !hif->fdata || !skb)
		return(-EINVAL);
	dl = hif->fdata;
	hh = mISDN_HEAD_P(skb);
	off.data.p = skb->data;
	off.len = skb->len;
	off.addr = dl->iaddr | IF_TYPE(hif);
	off.prim = hh->prim;
	off.dinfo = hh->dinfo;
	if (device_debug & DEBUG_RDATA)
		printk(KERN_DEBUG "from_up_down: %x(%x) dinfo:%x len:%d\n",
			off.prim, off.addr, off.dinfo, off.len);
	retval = mISDN_rdata(dl->dev, &off, 0);
	if (retval == (int)(4*sizeof(u_int) + off.len)) {
		dev_kfree_skb(skb);
		retval = 0;
	} else if (retval == 0)
		retval = -ENOSPC;
	return(retval);
}


static int
set_if(devicelayer_t *dl, u_int prim, mISDNif_t *hif)
{
	int err = 0;

	err = mISDN_SetIF(&dl->inst, hif, prim, from_up_down, from_up_down, dl);
	return(err);
}

static int
udev_manager(void *data, u_int prim, void *arg) {
	mISDNinstance_t *inst = data;
	mISDNdevice_t	*dev = mISDN_devicelist;
	devicelayer_t	*dl = NULL;
	int err = -EINVAL;

	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG "udev_manager data:%p prim:%x arg:%p\n",
			data, prim, arg);
	if (!data)
		return(-EINVAL);
	read_lock(&mISDN_device_lock);
	while(dev) {
		dl = dev->layer;
		while(dl) {
			if (&dl->inst == inst)
				break;
			dl = dl->next;
		}
		if (dl)
			break;
		dev = dev->next;
	}
	if (!dl) {
		printk(KERN_WARNING "dev_manager prim %x without device layer\n", prim);
		goto out;
	}
	switch(prim) {
	    case MGR_CONNECT | REQUEST:
	    	err = mISDN_ConnectIF(inst, arg);
	    	break;
	    case MGR_SETIF | REQUEST:
	    case MGR_SETIF | INDICATION:
	    	err = set_if(dl, prim, arg);
		break;
	    case MGR_DISCONNECT | REQUEST:
	    case MGR_DISCONNECT | INDICATION:
	    	err = mISDN_DisConnectIF(inst, arg);
	    	break;
	    case MGR_RELEASE | INDICATION:
		if (device_debug & DEBUG_MGR_FUNC)
			printk(KERN_DEBUG "release_dev id %x\n",
				dl->inst.st->id);
	    	del_layer(dl);
		err = 0;
	    	break;
	    default:
		printk(KERN_WARNING "dev_manager prim %x not handled\n", prim);
		break;
	}
out:
	read_unlock(&mISDN_device_lock);
	return(err);
}

int init_mISDNdev (int debug) {
	int err,i;

	udev_obj.name = MName;
	for (i=0; i<=MAX_LAYER_NR; i++) {
		udev_obj.DPROTO.protocol[i] = ISDN_PID_ANY;
		udev_obj.BPROTO.protocol[i] = ISDN_PID_ANY;
	}
	udev_obj.own_ctrl = udev_manager;
	udev_obj.prev = NULL;
	udev_obj.next = NULL;
	device_debug = debug;
	if (register_chrdev(mISDN_MAJOR, "mISDN", &mISDN_fops)) {
		printk(KERN_WARNING "mISDN: Could not register devices\n");
		return(-EIO);
	}
	if ((err = mISDN_register(&udev_obj))) {
		printk(KERN_ERR "Can't register %s error(%d)\n", MName, err);
	}
	return(err);
}

int free_mISDNdev(void) {
	int 		err = 0;
	mISDNdevice_t	*dev = mISDN_devicelist;

	if (mISDN_devicelist) {
		printk(KERN_WARNING "mISDN: devices open on remove\n");
		while (dev) {
			free_device(dev);
			dev = mISDN_devicelist;
		}
		err = -EBUSY;
	}
	if ((err = mISDN_unregister(&udev_obj))) {
		printk(KERN_ERR "Can't unregister UserDevice(%d)\n", err);
	}
	if ((err = unregister_chrdev(mISDN_MAJOR, "mISDN"))) {
		printk(KERN_WARNING "mISDN: devices busy on remove\n");
	}
	return(err);
}

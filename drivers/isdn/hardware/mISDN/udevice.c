/* $Id: udevice.c,v 0.20 2001/09/29 20:05:01 kkeil Exp $
 *
 * Copyright 2000  by Karsten Keil <kkeil@isdn4linux.de>
 */

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/poll.h>
#include <linux/vmalloc.h>
#include <linux/config.h>
#include <linux/timer.h>
#include "hisax_core.h"

#define MAX_HEADER_LEN	4

#define FLG_MGR_SETSTACK	1
#define FLG_MGR_OWNSTACK	2

#define FLG_MGR_TIMER_INIT	1
#define	FLG_MGR_TIMER_RUNING	2


typedef struct _devicelayer {
	struct _devicelayer	*prev;
	struct _devicelayer	*next;
	hisaxdevice_t		*dev;
	hisaxinstance_t		inst;
	hisaxinstance_t		*slave;
	hisaxif_t		s_up;
	hisaxif_t		s_down;
	int			iaddr;
	int			lm_st;
	int			Flags;
} devicelayer_t;

typedef struct _devicestack {
	struct _devicestack	*prev;
	struct _devicestack	*next;
	hisaxdevice_t		*dev;
	hisaxstack_t		*st;
	int			extentions;
} devicestack_t;

typedef struct _hisaxtimer {
	struct _hisaxtimer	*prev;
	struct _hisaxtimer	*next;
	struct _hisaxdevice	*dev;
	struct timer_list	tl;
	int			id;
	int			Flags;
} hisaxtimer_t;

static hisaxdevice_t	*hisax_devicelist = NULL;
static rwlock_t	hisax_device_lock = RW_LOCK_UNLOCKED;

static hisaxobject_t	udev_obj;
static char MName[] = "UserDevice";
static u_char  stbuf[1000];

static int device_debug = 0;

static int from_up_down(hisaxif_t *, struct sk_buff *);
static int hisax_wdata(hisaxdevice_t *dev);

// static int from_peer(hisaxif_t *, u_int, int, int, void *);
// static int to_peer(hisaxif_t *, u_int, int, int, void *);


static hisaxdevice_t *
get_hisaxdevice4minor(int minor)
{
	hisaxdevice_t	*dev = hisax_devicelist;

	while(dev) {
		if (dev->minor == minor)
			break;
		dev = dev->next;
	}
	return(dev);
}

static __inline__ void
p_memcpy_i(hisaxport_t *port, void *src, size_t count)
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
p_memcpy_o(hisaxport_t *port, void *dst, size_t count)
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
p_pull_o(hisaxport_t *port, size_t count)
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

static int
hisax_rdata_raw(hisaxif_t *hif, struct sk_buff *skb) {
	hisaxdevice_t	*dev;
	hisax_head_t	*hh;
	u_long		flags;
	int		retval = 0;

	if (!hif || !hif->fdata || !skb)
		return(-EINVAL);
	dev = hif->fdata;
	if (skb->len < HISAX_FRAME_MIN)
		return(-EINVAL);
	hh = (hisax_head_t *)skb->data;
	if (hh->prim == (PH_DATA | INDICATION)) {
		if (test_bit(FLG_HISAXPORT_OPEN, &dev->rport.Flag)) {
			skb_pull(skb, HISAX_HEAD_SIZE);
			spin_lock_irqsave(&dev->rport.lock, flags);
			if (skb->len < (dev->rport.size - dev->rport.cnt)) {
				p_memcpy_i(&dev->rport, skb->data, skb->len);
			} else {
				skb_push(skb, HISAX_HEAD_SIZE);
				retval = -ENOSPC;
			}
			spin_unlock_irqrestore(&dev->rport.lock, flags);
			wake_up_interruptible(&dev->rport.procq);
		} else {
			printk(KERN_WARNING __FUNCTION__
				": PH_DATA_IND device(%d) not read open\n",
				dev->minor);
			retval = -ENOENT;
		}
	} else if (hh->prim == (PH_DATA | CONFIRM)) {
		test_and_clear_bit(FLG_HISAXPORT_BLOCK, &dev->wport.Flag);
		hisax_wdata(dev);
	} else if ((hh->prim == (PH_ACTIVATE | CONFIRM)) ||
		(hh->prim == (PH_ACTIVATE | INDICATION))) {
			test_and_set_bit(FLG_HISAXPORT_ENABLED,
				&dev->wport.Flag);
			test_and_clear_bit(FLG_HISAXPORT_BLOCK,
				&dev->wport.Flag);
	} else if ((hh->prim == (PH_DEACTIVATE | CONFIRM)) ||
		(hh->prim == (PH_DEACTIVATE | INDICATION))) {
			test_and_clear_bit(FLG_HISAXPORT_ENABLED,
				&dev->wport.Flag);
	} else {
		printk(KERN_WARNING __FUNCTION__
			": prim(%x) dinfo(%x) not supported\n",
			hh->prim, hh->dinfo);
		retval = -EINVAL;
	}
	if (!retval)
		dev_kfree_skb(skb);
	return(retval);
}

static int
hisax_rdata(hisaxdevice_t *dev, iframe_t *iff, int use_value) {
	int		len = 4*sizeof(u_int);
	u_long		flags;
	hisaxport_t	*port = &dev->rport;

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
	} else
		len = -ENOSPC;
	spin_unlock_irqrestore(&port->lock, flags);
	wake_up_interruptible(&port->procq);
	return(len);
}

static devicelayer_t
*get_devlayer(hisaxdevice_t   *dev, int addr) {
	devicelayer_t *dl = dev->layer;

	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG __FUNCTION__ ": addr:%x\n", addr);
	while(dl) {
//		if (device_debug & DEBUG_MGR_FUNC)
//			printk(KERN_DEBUG __FUNCTION__ ": dl(%p) iaddr:%x\n",
//				dl, dl->iaddr);
		if (dl->iaddr == (IF_IADDRMASK & addr))
			break;
		dl = dl->next;
	}
	return(dl);
}

static devicestack_t
*get_devstack(hisaxdevice_t   *dev, int addr)
{
	devicestack_t *ds = dev->stack;

	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG __FUNCTION__ ": addr:%x\n", addr);
	while(ds) {
		if (ds->st && (ds->st->id == addr))
			break;
		ds = ds->next;
	}
	return(ds);
}

static hisaxtimer_t
*get_devtimer(hisaxdevice_t   *dev, int id)
{
	hisaxtimer_t	*ht = dev->timer;

	if (device_debug & DEBUG_DEV_TIMER)
		printk(KERN_DEBUG __FUNCTION__ ": dev:%p id:%x\n", dev, id);
	while(ht) {
		if (ht->id == id)
			break;
		ht = ht->next;
	}
	return(ht);
}

static int
stack_inst_flg(hisaxdevice_t *dev, hisaxstack_t *st, int bit, int clear)
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
new_devstack(hisaxdevice_t *dev, stack_info_t *si)
{
	int		err;
	hisaxstack_t	*st;
	hisaxinstance_t	inst;
	devicestack_t	*nds;

	memset(&inst, 0, sizeof(hisaxinstance_t));
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
//		memcpy(&inst.st->pid, &st->pid, sizeof(hisax_pid_t));
		inst.st->child = st->child;
	} else {
		memcpy(&inst.st->pid, &si->pid, sizeof(hisax_pid_t));
	}
	nds->extentions = si->extentions;
	inst.st->extentions |= si->extentions;
	inst.st->mgr = get_instance4id(si->mgr);
	nds->st = inst.st;
	APPEND_TO_LIST(nds, dev->stack);
	return(inst.st->id);
}

static int
create_layer(hisaxdevice_t *dev, layer_info_t *linfo, int *adr)
{
	hisaxlayer_t	*layer;
	hisaxstack_t	*st;
	int		i, ret;
	devicelayer_t	*nl;
	hisaxobject_t	*obj;
	hisaxinstance_t *inst = NULL;

	if (!(st = get_stack4id(linfo->st))) {
		int_error();
		return(-ENODEV);
	}
	if (linfo->object_id != -1) {
		obj = get_object(linfo->object_id);
		if (!obj) {
			printk(KERN_WARNING __FUNCTION__ ": no object %x found\n",
				linfo->object_id);
			return(-ENODEV);
		}
		ret = obj->own_ctrl(st, MGR_NEWLAYER | REQUEST, &linfo->pid);
		if (ret) {
			printk(KERN_WARNING __FUNCTION__ ": error nl req %d\n",
				ret);
			return(ret);
		}
		layer = getlayer4lay(st, linfo->pid.layermask);
		if (!layer) {
			printk(KERN_WARNING __FUNCTION__ ": no layer for lm(%x)\n",
				linfo->pid.layermask);
			return(-EINVAL);
		}
		inst = layer->inst;
		if (!inst) {
			printk(KERN_WARNING __FUNCTION__ ": no inst in layer(%p)\n",
				layer);
			return(-EINVAL);
		}
	} else if ((layer = getlayer4lay(st, linfo->pid.layermask))) {
		printk(KERN_WARNING
			"HiSax create_layer st(%x) LM(%x) inst not empty(%p)\n",
			st->id, linfo->pid.layermask, layer);
		return(-EBUSY);
	}
	if (!(nl = kmalloc(sizeof(devicelayer_t), GFP_ATOMIC))) {
		printk(KERN_ERR "kmalloc devicelayer failed\n");
		return(-ENOMEM);
	}
	memset(nl, 0, sizeof(devicelayer_t));
	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG
			"HiSax create_layer LM(%x) nl(%p) nl inst(%p)\n",
			linfo->pid.layermask, nl, &nl->inst);
	nl->dev = dev;
	memcpy(&nl->inst.pid, &linfo->pid, sizeof(hisax_pid_t));
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
	hisaxif_t *hif,*phif,*shif;
	int err;

	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG __FUNCTION__":dl(%p) stat(%x)\n", dl, stat);
	phif = NULL;
	if (stat & IF_UP) {
		hif = &dl->inst.up;
		shif = &dl->s_up;
		if (shif->owner)
			phif = &shif->owner->down;
	else if (stat & IF_DOWN)
		hif = &dl->inst.down;
		shif = &dl->s_down;
		if (shif->owner)
			phif = &shif->owner->up;
	} else {
		printk(KERN_WARNING __FUNCTION__": stat not UP/DOWN\n");
		return(-EINVAL);
	}
	err = udev_obj.ctrl(hif->peer, MGR_DISCONNECT | REQUEST, hif);
	if (phif)
		memcpy(phif, shif, sizeof(hisaxif_t));
	REMOVE_FROM_LIST(hif);
	return(err);
}

static int
del_stack(devicestack_t *ds)
{
	hisaxdevice_t	*dev;

	if (!ds) {
		int_error();
		return(-EINVAL);
	}
	dev = ds->dev;
	if (device_debug & DEBUG_MGR_FUNC) {
		printk(KERN_DEBUG __FUNCTION__": ds(%p) dev(%p)\n", 
			ds, dev);
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
	hisaxinstance_t *inst = &dl->inst;
	hisaxdevice_t	*dev = dl->dev;
	int		i;

	if (device_debug & DEBUG_MGR_FUNC) {
		printk(KERN_DEBUG __FUNCTION__": dl(%p) inst(%p) LM(%x) dev(%p) nexti(%p)\n", 
			dl, inst, inst->pid.layermask, dev, inst->next);
		printk(KERN_DEBUG __FUNCTION__": iaddr %x inst %s slave %p\n",
			dl->iaddr, inst->name, dl->slave);
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

static hisaxinstance_t *
clone_instance(devicelayer_t *dl, hisaxstack_t  *st, hisaxinstance_t *peer) {
	int		err;

	if (dl->slave) {
		printk(KERN_WARNING __FUNCTION__": layer has slave, cannot clone\n");
		return(NULL);
	}
	if (!(peer->extentions & EXT_INST_CLONE)) {
		printk(KERN_WARNING __FUNCTION__": peer cannot clone\n");
		return(NULL);
	}
	dl->slave = (hisaxinstance_t *)st;
	if ((err = peer->obj->own_ctrl(peer, MGR_CLONELAYER | REQUEST,
		&dl->slave))) {
		dl->slave = NULL;
		printk(KERN_WARNING __FUNCTION__": peer clone error %d\n", err);
		return(NULL);
	}
	return(dl->slave);
}

static int
connect_if_req(hisaxdevice_t *dev, iframe_t *iff) {
	devicelayer_t *dl;
	interface_info_t *ifi = (interface_info_t *)&iff->data.p;
	hisaxinstance_t *owner;
	hisaxinstance_t *peer;
	hisaxinstance_t *pp;
	hisaxif_t	*hifp;
	int		stat;

	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG __FUNCTION__": addr:%x own(%x) peer(%x)\n",
			iff->addr, ifi->owner, ifi->peer);
	if (!(dl=get_devlayer(dev, ifi->owner))) {
		int_errtxt("no devive_layer for %08x", ifi->owner);
		return(-ENXIO);
	}
	if (!(owner = get_instance4id(ifi->owner))) {
		printk(KERN_WARNING __FUNCTION__": owner(%x) not found\n",
			ifi->owner);
		return(-ENODEV);
	}
	if (!(peer = get_instance4id(ifi->peer))) {
		printk(KERN_WARNING __FUNCTION__": peer(%x) not found\n",
			ifi->peer);
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
			printk(KERN_WARNING __FUNCTION__": peer if has no peer\n");
			return(-EINVAL);
		}
		if (stat == IF_UP) {
			memcpy(&owner->up, hifp, sizeof(hisaxif_t));
			memcpy(&dl->s_up, hifp, sizeof(hisaxif_t));
			owner->up.owner = owner;
			hifp->peer = owner;
			hifp->func = from_up_down;
			hifp->fdata = dl;
			hifp = &pp->down;
			memcpy(&owner->down, hifp, sizeof(hisaxif_t));
			memcpy(&dl->s_down, hifp, sizeof(hisaxif_t));
			owner->down.owner = owner;
			hifp->peer = owner;
			hifp->func = from_up_down;
			hifp->fdata = dl;
		} else {
			memcpy(&owner->down, hifp, sizeof(hisaxif_t));
			memcpy(&dl->s_down, hifp, sizeof(hisaxif_t));
			owner->up.owner = owner;
			hifp->peer = owner;
			hifp->func = from_up_down;
			hifp->fdata = dl;
			hifp = &pp->up;
			memcpy(&owner->up, hifp, sizeof(hisaxif_t));
			memcpy(&dl->s_up, hifp, sizeof(hisaxif_t));
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
				printk(KERN_WARNING __FUNCTION__": cannot create new peer instance\n");
				return(-EBUSY);
			}
		}
	}
	if (ifi->extentions & EXT_IF_EXCLUSIV) {
		if (hifp->stat != IF_NOACTIV) {
			printk(KERN_WARNING __FUNCTION__": peer if is in use\n");
			return(-EBUSY);
		}
	}			
	return(ConnectIF(owner, peer));
}

static int
set_if_req(hisaxdevice_t *dev, iframe_t *iff) {
	hisaxif_t *hif,*phif,*shif;
	int stat;
	interface_info_t *ifi = (interface_info_t *)&iff->data.p;
	devicelayer_t *dl;
	hisaxinstance_t *inst, *peer;

	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG __FUNCTION__": addr:%x own(%x) peer(%x)\n",
			iff->addr, ifi->owner, ifi->peer);
	if (!(dl=get_devlayer(dev, iff->addr)))
		return(-ENXIO);
	if (!(inst = get_instance4id(ifi->owner))) {
		printk(KERN_WARNING __FUNCTION__": owner(%x) not found\n",
			ifi->owner);
		return(-ENODEV);
	}
	if (!(peer = get_instance4id(ifi->peer))) {
		printk(KERN_WARNING __FUNCTION__": peer(%x) not found\n",
			ifi->peer);
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
		printk(KERN_WARNING __FUNCTION__": if not UP/DOWN\n");
		return(-EINVAL);
	}

	
	if (shif->stat != IF_NOACTIV) {
		printk(KERN_WARNING __FUNCTION__": save if busy\n");
		return(-EBUSY);
	}
	if (hif->stat != IF_NOACTIV) {
		printk(KERN_WARNING __FUNCTION__": own if busy\n");
		return(-EBUSY);
	}
	hif->stat = stat;
	hif->owner = inst;
	memcpy(shif, phif, sizeof(hisaxif_t));
	memset(phif, 0, sizeof(hisaxif_t));
	return(peer->obj->own_ctrl(peer, iff->prim, hif));
}

static int
add_if_req(hisaxdevice_t *dev, iframe_t *iff) {
	hisaxif_t *hif;
	interface_info_t *ifi = (interface_info_t *)&iff->data.p;
	hisaxinstance_t *inst, *peer;

	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG __FUNCTION__": addr:%x own(%x) peer(%x)\n",
			iff->addr, ifi->owner, ifi->peer);
	if (!(inst = get_instance4id(ifi->owner))) {
		printk(KERN_WARNING __FUNCTION__": owner(%x) not found\n",
			ifi->owner);
		return(-ENODEV);
	}
	if (!(peer = get_instance4id(ifi->peer))) {
		printk(KERN_WARNING __FUNCTION__": peer(%x) not found\n",
			ifi->peer);
		return(-ENODEV);
	}

	if (ifi->stat == IF_DOWN) {
		hif = &inst->up;
	} else if (ifi->stat == IF_UP) {
		hif = &inst->down;
	} else {
		printk(KERN_WARNING __FUNCTION__": if not UP/DOWN\n");
		return(-EINVAL);
	}
	return(peer->obj->ctrl(peer, iff->prim, hif));
}

static int
del_if_req(hisaxdevice_t *dev, iframe_t *iff)
{
	devicelayer_t *dl;

	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG __FUNCTION__": addr:%x\n", iff->addr);
	if (!(dl=get_devlayer(dev, iff->addr)))
		return(-ENXIO);
	return(remove_if(dl, iff->addr));
}

static void
dev_expire_timer(hisaxtimer_t *ht)
{
	iframe_t off;

	if (device_debug & DEBUG_DEV_TIMER)
		printk(KERN_DEBUG __FUNCTION__": timer(%x)\n",
			ht->id);
	if (test_and_clear_bit(FLG_MGR_TIMER_RUNING, &ht->Flags)) {
		off.dinfo = 0;
		off.prim = MGR_TIMER | INDICATION;
		off.addr = ht->id;
		off.len = 0;
		hisax_rdata(ht->dev, &off, 0);
	} else
		printk(KERN_WARNING __FUNCTION__": timer(%x) not active\n",
			ht->id);
}

static int
dev_init_timer(hisaxdevice_t *dev, iframe_t *iff)
{
	hisaxtimer_t	*ht;

	ht = get_devtimer(dev, iff->addr);
	if (!ht) {
		ht = kmalloc(sizeof(hisaxtimer_t), GFP_KERNEL);
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
			printk(KERN_DEBUG __FUNCTION__": new(%x)\n",
				ht->id);
	} else if (device_debug & DEBUG_DEV_TIMER)
		printk(KERN_DEBUG __FUNCTION__": old(%x)\n",
			ht->id);
	if (timer_pending(&ht->tl)) {
		printk(KERN_WARNING __FUNCTION__": timer(%x) pending\n",
			ht->id);
		del_timer(&ht->tl);
	}
	init_timer(&ht->tl);
	test_and_set_bit(FLG_MGR_TIMER_INIT, &ht->Flags);
	return(0);
}

static int
dev_add_timer(hisaxdevice_t *dev, iframe_t *iff)
{
	hisaxtimer_t	*ht;

	ht = get_devtimer(dev, iff->addr);
	if (!ht) {
		printk(KERN_WARNING __FUNCTION__": no timer(%x)\n", iff->addr);
		return(-ENODEV);
	}
	if (timer_pending(&ht->tl)) {
		printk(KERN_WARNING __FUNCTION__": timer(%x) pending\n",
			ht->id);
		return(-EBUSY);
	}
	if (iff->dinfo < 10) {
		printk(KERN_WARNING __FUNCTION__": timer(%x): %d ms too short\n",
			ht->id, iff->dinfo);
		return(-EINVAL);
	}
	if (device_debug & DEBUG_DEV_TIMER)
		printk(KERN_DEBUG __FUNCTION__": timer(%x) %d ms\n",
			ht->id, iff->dinfo);
	init_timer(&ht->tl);
	ht->tl.expires = jiffies + (iff->dinfo * HZ) / 1000;
	test_and_set_bit(FLG_MGR_TIMER_RUNING, &ht->Flags);
	add_timer(&ht->tl);
	return(0);
}

static int
dev_del_timer(hisaxdevice_t *dev, iframe_t *iff)
{
	hisaxtimer_t	*ht;

	ht = get_devtimer(dev, iff->addr);
	if (!ht) {
		printk(KERN_WARNING __FUNCTION__": no timer(%x)\n", iff->addr);
		return(-ENODEV);
	}
	if (device_debug & DEBUG_DEV_TIMER)
		printk(KERN_DEBUG __FUNCTION__": timer(%x)\n",
			ht->id);
	del_timer(&ht->tl);
	if (!test_and_clear_bit(FLG_MGR_TIMER_RUNING, &ht->Flags))
		printk(KERN_WARNING __FUNCTION__": timer(%x) not running\n",
			ht->id);
	return(0);
}

static int
dev_remove_timer(hisaxdevice_t *dev, int id)
{
	hisaxtimer_t	*ht;

	ht = get_devtimer(dev, id);
	if (!ht)  {
		printk(KERN_WARNING __FUNCTION__": no timer(%x)\n", id);
		return(-ENODEV);
	}
	if (device_debug & DEBUG_DEV_TIMER)
		printk(KERN_DEBUG __FUNCTION__": timer(%x)\n",
			ht->id);
	del_timer(&ht->tl);
	REMOVE_FROM_LISTBASE(ht, dev->timer);
	kfree(ht);
	return(0);
}

static int
get_status(iframe_t *off)
{
	status_info_t	*si = (status_info_t *)off->data.p;
	hisaxinstance_t	*inst;
	int err;

	if (!(inst = get_instance4id(off->addr & IF_ADDRMASK))) {
		printk(KERN_WARNING __FUNCTION__": no instance\n");
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
	hisaxinstance_t *inst;
	layer_info_t	*li = (layer_info_t *)frm->data.p;
	
	if (!(inst = get_instance4id(frm->addr & IF_ADDRMASK))) {
		printk(KERN_WARNING __FUNCTION__": no instance\n");
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
	memcpy(&li->pid, &inst->pid, sizeof(hisax_pid_t));
	frm->len = sizeof(layer_info_t);
}

static void
get_if_info(iframe_t *frm)
{
	hisaxinstance_t		*inst;
	hisaxif_t		*hif;
	interface_info_t	*ii = (interface_info_t *)frm->data.p;
	
	if (!(inst = get_instance4id(frm->addr & IF_ADDRMASK))) {
		printk(KERN_WARNING __FUNCTION__": no instance\n");
		frm->len = -ENODEV;
		return;
	}
	if (frm->dinfo == IF_DOWN)
		hif = &inst->down;
	else if (frm->dinfo == IF_UP)
		hif = &inst->up;
	else {
		printk(KERN_WARNING __FUNCTION__": wrong interface %x\n",
			frm->dinfo);
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
wdata_frame(hisaxdevice_t *dev, iframe_t *iff) {
	hisaxif_t *hif = NULL;
	devicelayer_t *dl;
	int err=-ENXIO;

	if (device_debug & DEBUG_WDATA)
		printk(KERN_DEBUG __FUNCTION__": addr:%x\n", iff->addr);
	if (!(dl=get_devlayer(dev, iff->addr)))
		return(-ENXIO);
	if (iff->addr & IF_UP) {
		hif = &dl->inst.up;
		if (IF_TYPE(hif) != IF_DOWN) {
			printk(KERN_WARNING __FUNCTION__": inst.up no down\n");
			hif = NULL;
		}
	} else if (iff->addr & IF_DOWN) {
		hif = &dl->inst.down;
		if (IF_TYPE(hif) != IF_UP) {
			printk(KERN_WARNING __FUNCTION__": inst.down no up\n");
			hif = NULL;
		}
	}
	if (hif) {
		if (device_debug & DEBUG_WDATA)
			printk(KERN_DEBUG __FUNCTION__": pr(%x) di(%x) l(%d)\n",
				iff->prim, iff->dinfo, iff->len);
		if (iff->len < 0) {
			printk(KERN_WARNING
				__FUNCTION__":data negativ(%d)\n",
				iff->len);
			return(-EINVAL);
		}
		err = if_link(hif, iff->prim, iff->dinfo, iff->len,
			&iff->data.b[0], UPLINK_HEADER_SPACE);
		if (device_debug & DEBUG_WDATA && err)
			printk(KERN_DEBUG __FUNCTION__": if_link ret(%x)\n",
				err);
	} else {
		if (device_debug & DEBUG_WDATA)
			printk(KERN_DEBUG "hisax: no matching interface\n");
	}
	return(err);
}

static int
hisax_wdata_if(hisaxdevice_t *dev, iframe_t *iff, int len) {
	iframe_t        off;
	hisaxstack_t	*st;
	devicelayer_t	*dl;
	hisaxlayer_t    *layer;
	int		lay;
	int		err = 0;
	int		used = 0;
	int		head = 4*sizeof(u_int);

	if (len < head) {
		printk(KERN_WARNING __FUNCTION__": frame(%d) too short\n",
			len);
		return(len);
	}
	if (device_debug & DEBUG_WDATA)
		printk(KERN_DEBUG "hisax_wdata: %x:%x %x %d\n",
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
		hisax_rdata(dev, &off, 0);
		break;
	    case (MGR_SETSTACK | REQUEST):
		used = head + sizeof(hisax_pid_t);
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
		hisax_rdata(dev, &off, 1);
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
		hisax_rdata(dev, &off, 1);
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
		hisax_rdata(dev, &off, 1);
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
			hisax_rdata(dev, &off, 1);
			break;
		} else
			lay = ISDN_LAYER(lay);
		if ((st = get_stack4id(iff->addr))) {
			if ((layer = getlayer4lay(st, lay))) {
				if (layer->inst)
					off.dinfo = layer->inst->id;
			}
		}
		hisax_rdata(dev, &off, 0);
		break;
	    case (MGR_GETLAYER | REQUEST):
		used = head;
		off.addr = iff->addr;
		off.prim = MGR_GETLAYER | CONFIRM;
		off.dinfo = 0;
		off.len = 0;
		off.data.p = stbuf;
		get_layer_info(&off);
		hisax_rdata(dev, &off, 0);
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
		hisax_rdata(dev, &off, 0);
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
		hisax_rdata(dev, &off, 1);
		break;
	    case (MGR_GETIF | REQUEST):
		used = head;
		off.addr = iff->addr;
		off.prim = MGR_GETIF | CONFIRM;
		off.dinfo = iff->dinfo;
		off.len = 0;
		off.data.p = stbuf;
		get_if_info(&off);
		hisax_rdata(dev, &off, 0);
		break;
	    case (MGR_CONNECT | REQUEST):
		used = head + sizeof(interface_info_t);
		if (len<used)
			return(len);
		off.addr = iff->addr;
		off.prim = MGR_CONNECT | CONFIRM;
		off.dinfo = 0;
		off.len = connect_if_req(dev, iff);
		hisax_rdata(dev, &off, 1);
		break;
	    case (MGR_SETIF | REQUEST):
		used = head + iff->len;
		if (len<used)
			return(len);
		off.addr = iff->addr;
		off.prim = MGR_SETIF | CONFIRM;
		off.dinfo = 0;
		off.len = set_if_req(dev, iff);
		hisax_rdata(dev, &off, 1);
		break;
	    case (MGR_ADDIF | REQUEST):
		used = head + iff->len;
		if (len<used)
			return(len);
		off.addr = iff->addr;
		off.prim = MGR_ADDIF | CONFIRM;
		off.dinfo = 0;
		off.len = add_if_req(dev, iff);
		hisax_rdata(dev, &off, 1);
		break;
	    case (MGR_DISCONNECT | REQUEST):
		used = head;
		off.addr = iff->addr;
		off.prim = MGR_DISCONNECT | CONFIRM;
		off.dinfo = 0;
		off.len = del_if_req(dev, iff);
		hisax_rdata(dev, &off, 1);
		break;
	    case (MGR_INITTIMER | REQUEST):
		used = head;
		off.len = dev_init_timer(dev, iff);
		off.addr = iff->addr;
		off.prim = MGR_INITTIMER | CONFIRM;
		off.dinfo = iff->dinfo;
		hisax_rdata(dev, &off, 0);
		break;
	    case (MGR_ADDTIMER | REQUEST):
		used = head;
		off.len = dev_add_timer(dev, iff);
		off.addr = iff->addr;
		off.prim = MGR_ADDTIMER | CONFIRM;
		off.dinfo = 0;
		hisax_rdata(dev, &off, 0);
		break;
	    case (MGR_DELTIMER | REQUEST):
		used = head;
		off.len = dev_del_timer(dev, iff);
		off.addr = iff->addr;
		off.prim = MGR_DELTIMER | CONFIRM;
		off.dinfo = iff->dinfo;
		hisax_rdata(dev, &off, 0);
		break;
	    case (MGR_REMOVETIMER | REQUEST):
		used = head;
		off.len = dev_remove_timer(dev, iff->addr);
		off.addr = iff->addr;
		off.prim = MGR_REMOVETIMER | CONFIRM;
		off.dinfo = 0;
		hisax_rdata(dev, &off, 0);
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
			hisax_rdata(dev, &off, 1);
		else
			hisax_rdata(dev, &off, 0);
		break;
	    default:
		used = head + iff->len;
		if (len<used) {
			printk(KERN_WARNING "hisax_wdata: framelen error prim %x %d/%d\n",
				iff->prim, len, used);
			used=len;
		} else if (iff->addr & IF_TYPEMASK) {
			err = wdata_frame(dev, iff);
			if (err)
				if (device_debug & DEBUG_WDATA)
					printk(KERN_DEBUG "wdata_frame returns error %d\n", err);
		} else {
			printk(KERN_WARNING "hisax: prim %x addr %x not implemented\n",
				iff->prim, iff->addr);
		}
		break;
	}
	return(used);
}

static int
hisax_wdata(hisaxdevice_t *dev) {
	int	used = 0;
	u_long	flags;

	spin_lock_irqsave(&dev->wport.lock, flags);
	if (test_and_set_bit(FLG_HISAXPORT_BUSY, &dev->wport.Flag)) {
		spin_unlock_irqrestore(&dev->wport.lock, flags);
		return(0);
	}
	while (1) {
		size_t	frag;

		if (!dev->wport.cnt) {
			wake_up(&dev->wport.procq);
			break;
		}
		if (dev->minor == HISAX_CORE_DEVICE) {
			iframe_t	*iff;
			iframe_t	hlp;
			int		broken = 0;
			
			frag = dev->wport.buf + dev->wport.size
				- dev->wport.op;
			if (dev->wport.cnt < IFRAME_HEAD_SIZE) {
				printk(KERN_WARNING __FUNCTION__": frame(%d,%d) too short\n",
					dev->wport.cnt, IFRAME_HEAD_SIZE);
				p_pull_o(&dev->wport, dev->wport.cnt);
				wake_up(&dev->wport.procq);
				break;
			}
			if (frag < IFRAME_HEAD_SIZE) {
				broken = 1;
				p_memcpy_o(&dev->wport, &hlp, IFRAME_HEAD_SIZE);
				if (hlp.len >0) {
					if (hlp.len < dev->wport.cnt) {
						printk(KERN_WARNING __FUNCTION__": framedata(%d/%d)too short\n",
							dev->wport.cnt, hlp.len);
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
						printk(KERN_WARNING __FUNCTION__": frame(%d,%d) too short\n",
							dev->wport.cnt, IFRAME_HEAD_SIZE + iff->len);
						p_pull_o(&dev->wport, dev->wport.cnt);
						wake_up(&dev->wport.procq);
						break;
					}
					if (frag < (iff->len + IFRAME_HEAD_SIZE)) {
						broken = 1;
						p_memcpy_o(&dev->wport, &hlp, IFRAME_HEAD_SIZE);
					}
				}
			}
			if (broken) {
				if (hlp.len > 0) {
					iff = vmalloc(IFRAME_HEAD_SIZE + hlp.len);
					if (!iff) {
						printk(KERN_WARNING __FUNCTION__": no %d vmem for iff\n",
							IFRAME_HEAD_SIZE + hlp.len);
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
			hisax_wdata_if(dev, iff, used);
			if (broken) {
				if (used>IFRAME_HEAD_SIZE)
					vfree(iff);
				spin_lock_irqsave(&dev->wport.lock, flags);
			} else {
				spin_lock_irqsave(&dev->wport.lock, flags);
				p_pull_o(&dev->wport, used);
			}
		} else { /* RAW DEVICES */
			printk(KERN_DEBUG __FUNCTION__": wflg(%x)\n",
				dev->wport.Flag);
			if (test_bit(FLG_HISAXPORT_BLOCK, &dev->wport.Flag))
				break;
			used = dev->wport.cnt;
			if (used > MAX_DATA_SIZE)
				used = MAX_DATA_SIZE;
			printk(KERN_DEBUG __FUNCTION__": cnt %d/%d\n",
				used, dev->wport.cnt);
			if (test_bit(FLG_HISAXPORT_ENABLED, &dev->wport.Flag)) {
				struct sk_buff	*skb;

				skb = alloc_skb(used + HISAX_HEAD_SIZE,
					GFP_ATOMIC);
				if (skb) {
					skb_reserve(skb, HISAX_HEAD_SIZE);
					p_memcpy_o(&dev->wport, skb_put(skb,
						used), used);
					test_and_set_bit(FLG_HISAXPORT_BLOCK,
						&dev->wport.Flag);
					spin_unlock_irqrestore(&dev->wport.lock, flags);
					used = if_addhead(&dev->wport.pif,
						PH_DATA | REQUEST, (int)skb, skb);
					if (used) {
						printk(KERN_WARNING __FUNCTION__
							": dev(%d) down err(%d)\n",
							dev->minor, used);
						dev_kfree_skb(skb);
					}
					spin_lock_irqsave(&dev->wport.lock, flags);
				} else {
					printk(KERN_WARNING __FUNCTION__
						": dev(%d) no skb(%d)\n",
						dev->minor, used + HISAX_HEAD_SIZE);
					p_pull_o(&dev->wport, used);
				}
			} else {
				printk(KERN_WARNING __FUNCTION__
					": dev(%d) wport not enabled\n",
					dev->minor);
				p_pull_o(&dev->wport, used);
			}
		}
		wake_up(&dev->wport.procq);
	}
	test_and_clear_bit(FLG_HISAXPORT_BUSY, &dev->wport.Flag);
	spin_unlock_irqrestore(&dev->wport.lock, flags);
	return(0);
}

static hisaxdevice_t *
init_device(u_int minor) {
	hisaxdevice_t	*dev;
	u_long		flags;

	dev = kmalloc(sizeof(hisaxdevice_t), GFP_KERNEL);
	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG __FUNCTION__": dev(%d) %p\n",
			minor, dev); 
	if (dev) {
		memset(dev, 0, sizeof(hisaxdevice_t));
		dev->minor = minor;
		dev->io_sema = MUTEX;
		write_lock_irqsave(&hisax_device_lock, flags);
		APPEND_TO_LIST(dev, hisax_devicelist);
		write_unlock_irqrestore(&hisax_device_lock, flags);
	}
	return(dev);
}

hisaxdevice_t *
get_free_rawdevice(void)
{
	hisaxdevice_t	*dev;
	u_int		minor;

	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG __FUNCTION__":\n");
	for (minor=HISAX_MINOR_RAW_MIN; minor<=HISAX_MINOR_RAW_MAX; minor++) {
		dev = get_hisaxdevice4minor(minor);
		if (device_debug & DEBUG_MGR_FUNC)
			printk(KERN_DEBUG __FUNCTION__": dev(%d) %p\n",
				minor, dev); 
		if (!dev) {
			dev = init_device(minor);
			if (!dev)
				return(NULL);
			dev->rport.pif.func = hisax_rdata_raw;
			dev->rport.pif.fdata = dev;
			return(dev);
		}
	}
	return(NULL);
}

int
free_device(hisaxdevice_t *dev)
{
	u_long	flags;

	if (!dev)
		return(-ENODEV);
	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG __FUNCTION__": dev(%d)\n",
			dev->minor);
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
	write_lock_irqsave(&hisax_device_lock, flags);
	REMOVE_FROM_LISTBASE(dev, hisax_devicelist);
	write_unlock_irqrestore(&hisax_device_lock, flags);
	kfree(dev);
	return(0);
}

static int
hisax_open(struct inode *ino, struct file *filep)
{
	u_int		minor = MINOR(ino->i_rdev);
	hisaxdevice_t 	*dev = NULL;
	u_long		flags;
	int		isnew = 0;

	if (device_debug & DEBUG_DEV_OP)
		printk(KERN_DEBUG "hisax_open in: minor(%d) %p %p mode(%x)\n",
			minor, filep, filep->private_data, filep->f_mode);
	if (minor) {
		dev = get_hisaxdevice4minor(minor);
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
			dev->rport.buf = vmalloc(HISAX_DEVBUF_SIZE);
			if (!dev->rport.buf) {
				if (isnew) {
					write_lock_irqsave(&hisax_device_lock, flags);
					REMOVE_FROM_LISTBASE(dev, hisax_devicelist);
					write_unlock_irqrestore(&hisax_device_lock, flags);
					kfree(dev);
				}
				return(-ENOMEM);
			}
			dev->rport.lock = SPIN_LOCK_UNLOCKED;
			dev->rport.size = HISAX_DEVBUF_SIZE;
		}
		test_and_set_bit(FLG_HISAXPORT_OPEN, &dev->rport.Flag);
		dev->rport.ip = dev->rport.op = dev->rport.buf;
		dev->rport.cnt = 0;
	}
	if (dev->open_mode & FMODE_WRITE) {
		if (!dev->wport.buf) {
			dev->wport.buf = vmalloc(HISAX_DEVBUF_SIZE);
			if (!dev->wport.buf) {
				if (isnew) {
					if (dev->rport.buf)
						vfree(dev->rport.buf);
					write_lock_irqsave(&hisax_device_lock, flags);
					REMOVE_FROM_LISTBASE(dev, hisax_devicelist);
					write_unlock_irqrestore(&hisax_device_lock, flags);
					kfree(dev);
				}
				return(-ENOMEM);
			}
			dev->wport.lock = SPIN_LOCK_UNLOCKED;
			dev->wport.size = HISAX_DEVBUF_SIZE;
		}
		test_and_set_bit(FLG_HISAXPORT_OPEN, &dev->wport.Flag);
		dev->wport.ip = dev->wport.op = dev->wport.buf;
		dev->wport.cnt = 0;
	}
	hisaxlock_core();
	filep->private_data = dev;
	if (device_debug & DEBUG_DEV_OP)
		printk(KERN_DEBUG "hisax_open out: %p %p\n", filep, filep->private_data);
	return(0);
}

static int
hisax_close(struct inode *ino, struct file *filep)
{
	hisaxdevice_t	*dev = hisax_devicelist;

	if (device_debug & DEBUG_DEV_OP)
		printk(KERN_DEBUG "hisax: hisax_close %p %p\n", filep, filep->private_data);
	read_lock(&hisax_device_lock);
	while (dev) {
		if (dev == filep->private_data) {
			if (device_debug & DEBUG_DEV_OP)
				printk(KERN_DEBUG "hisax: dev(%d) %p mode %x/%x\n",
					dev->minor, dev, dev->open_mode, filep->f_mode);
			dev->open_mode &= ~filep->f_mode;
			read_unlock(&hisax_device_lock);
			if (filep->f_mode & FMODE_READ) {
				test_and_clear_bit(FLG_HISAXPORT_OPEN,
					&dev->rport.Flag);
			}
			if (filep->f_mode & FMODE_WRITE) {
				test_and_clear_bit(FLG_HISAXPORT_OPEN,
					&dev->wport.Flag);
			}
			filep->private_data = NULL;
			if (!dev->minor)
				free_device(dev);
			hisaxunlock_core();
			return 0;
		}
		dev = dev->next;
	}
	read_unlock(&hisax_device_lock);
	printk(KERN_WARNING "hisax: No private data while closing device\n");
	return 0;
}

static __inline__ ssize_t
do_hisax_read(struct file *file, char *buf, size_t count, loff_t * off)
{
	hisaxdevice_t	*dev = file->private_data;
	size_t		len, frag;
	u_long		flags;

	if (off != &file->f_pos)
		return(-ESPIPE);
	if (!dev->rport.buf)
		return -EINVAL;	
	if (!access_ok(VERIFY_WRITE, buf, count))
		return(-EFAULT);
	if (device_debug & DEBUG_DEV_OP)
		printk(KERN_DEBUG "hisax_read: file(%d) %p count %d\n",
			dev->minor, file, count);
	while (!dev->rport.cnt) {
		if (file->f_flags & O_NONBLOCK)
			return(-EAGAIN);
		interruptible_sleep_on(&(dev->rport.procq));
		if (signal_pending(current))
			return(-ERESTARTSYS);
	}
	spin_lock_irqsave(&dev->rport.lock, flags);
	if (count < dev->rport.cnt)
		len = count;
	else
		len = dev->rport.cnt;
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
	return(len + frag);
}

static ssize_t
hisax_read(struct file *file, char *buf, size_t count, loff_t * off)
{
	hisaxdevice_t	*dev = file->private_data;
	ssize_t		ret;

	if (!dev)
		return(-ENODEV);
	down(&dev->io_sema);
	ret = do_hisax_read(file, buf, count, off);
	up(&dev->io_sema);
	return(ret);
}

static loff_t
hisax_llseek(struct file *file, loff_t offset, int orig)
{
	return -ESPIPE;
}

static __inline__ ssize_t
do_hisax_write(struct file *file, const char *buf, size_t count, loff_t * off)
{
	hisaxdevice_t	*dev = file->private_data;
	size_t		len, frag;
	u_long		flags;

	if (off != &file->f_pos)
		return(-ESPIPE);
	if (device_debug & DEBUG_DEV_OP)
		printk(KERN_DEBUG "hisax_write: file(%d) %p count %d/%d/%d\n",
			dev->minor, file, count, dev->wport.cnt, dev->wport.size);
	if (!dev->wport.buf)
		return -EINVAL;	
	if (!access_ok(VERIFY_WRITE, buf, count))
		return(-EFAULT);
	if (count > dev->wport.size)
		return(-ENOSPC);
	spin_lock_irqsave(&dev->wport.lock, flags);
	while ((dev->wport.size - dev->wport.cnt) < count) {
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
	hisax_wdata(dev);
	return(count);
}

static ssize_t
hisax_write(struct file *file, const char *buf, size_t count, loff_t * off)
{
	hisaxdevice_t	*dev = file->private_data;
	ssize_t		ret;

	if (!dev)
		return(-ENODEV);
	down(&dev->io_sema);
	ret = do_hisax_write(file, buf, count, off);
	up(&dev->io_sema);
	return(ret);
}

static unsigned int
hisax_poll(struct file *file, poll_table * wait)
{
	unsigned int	mask = POLLERR;
	hisaxdevice_t	*dev = file->private_data;
	hisaxport_t	*rport = (file->f_mode & FMODE_READ) ?
					&dev->rport : NULL;
	hisaxport_t	*wport = (file->f_mode & FMODE_WRITE) ?
					&dev->wport : NULL;

	if (dev) {
		if (device_debug & DEBUG_DEV_OP)
			printk(KERN_DEBUG "hisax_poll in: file(%d) %p\n",
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
		printk(KERN_DEBUG "hisax_poll out: file %p mask %x\n",
			file, mask);
	return(mask);
}

static struct file_operations hisax_fops =
{
	llseek:		hisax_llseek,
	read:		hisax_read,
	write:		hisax_write,
	poll:		hisax_poll,
//	ioctl:		hisax_ioctl,
	open:		hisax_open,
	release:	hisax_close,
};

static int
from_up_down(hisaxif_t *hif, struct sk_buff *skb) {
	
	devicelayer_t *dl;
	iframe_t off;
	hisax_head_t *hh; 
	int retval = -EINVAL;

	if (!hif || !hif->fdata || !skb)
		return(-EINVAL);
	dl = hif->fdata;
	if (skb->len < HISAX_FRAME_MIN)
		return(-EINVAL);
	hh = (hisax_head_t *)skb->data;
	off.data.p = skb_pull(skb, sizeof(hisax_head_t));
	off.len = skb->len;
	off.addr = dl->iaddr | IF_TYPE(hif);
	off.prim = hh->prim;
	off.dinfo = hh->dinfo;
	if (device_debug & DEBUG_RDATA)
		printk(KERN_DEBUG "from_up_down: %x(%x) dinfo:%x len:%d\n",
			off.prim, off.addr, off.dinfo, off.len);
	retval = hisax_rdata(dl->dev, &off, 0);
	if (retval == (4*sizeof(u_int) + off.len)) {
		dev_kfree_skb(skb);
		retval = 0;
	} else if (retval == 0)
		retval = -ENOSPC;
	return(retval);
}


static int
set_if(devicelayer_t *dl, u_int prim, hisaxif_t *hif)
{
	int err = 0;

	err = SetIF(&dl->inst, hif, prim, from_up_down, from_up_down, dl);
	return(err);
}

static int
udev_manager(void *data, u_int prim, void *arg) {
	hisaxinstance_t *inst = data;
	hisaxdevice_t	*dev = hisax_devicelist;
	devicelayer_t	*dl = NULL;
	int err = -EINVAL;

	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG "udev_manager data:%p prim:%x arg:%p\n",
			data, prim, arg);
	if (!data)
		return(-EINVAL);
	read_lock(&hisax_device_lock);
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
	    	err = ConnectIF(inst, arg);
	    	break;
	    case MGR_SETIF | REQUEST:
	    case MGR_SETIF | INDICATION:
	    	err = set_if(dl, prim, arg);
		break;
	    case MGR_DISCONNECT | REQUEST:
	    case MGR_DISCONNECT | INDICATION:
	    	err = DisConnectIF(inst, arg);
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
	read_unlock(&hisax_device_lock);
	return(err);
}

int init_hisaxdev (int debug) {
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
	if (register_chrdev(HISAX_MAJOR, "hisax", &hisax_fops)) {
		printk(KERN_WARNING "hisax: Could not register devices\n");
		return(-EIO);
	}
	if ((err = HiSax_register(&udev_obj))) {
		printk(KERN_ERR "Can't register %s error(%d)\n", MName, err);
	}
	return(err);
}

int free_hisaxdev(void) {
	int 		err = 0;
	hisaxdevice_t	*dev = hisax_devicelist;

	if (hisax_devicelist) {
		printk(KERN_WARNING "hisax: devices open on remove\n");
		while (dev) {
			free_device(dev);
			dev = hisax_devicelist;
		}
		err = -EBUSY;
	}
	if ((err = HiSax_unregister(&udev_obj))) {
		printk(KERN_ERR "Can't unregister UserDevice(%d)\n", err);
	}
	if ((err = unregister_chrdev(HISAX_MAJOR, "hisax"))) {
		printk(KERN_WARNING "hisax: devices busy on remove\n");
	}
	return(err);
}

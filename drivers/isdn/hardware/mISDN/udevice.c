/* $Id: udevice.c,v 0.18 2001/07/10 16:01:03 kkeil Exp $
 *
 * Copyright 2000  by Karsten Keil <kkeil@isdn4linux.de>
 */

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/poll.h>
#include <linux/vmalloc.h>
#include <linux/config.h>
#include "hisax_core.h"

#define MAX_HEADER_LEN	4

#define FLG_MGR_SETSTACK	1
#define FLG_MGR_OWNSTACK	2

typedef struct _hisaxdevice {
	struct _hisaxdevice	*prev;
	struct _hisaxdevice	*next;
	struct inode		*inode;
	struct file		*file;
	struct wait_queue	*procq;
	spinlock_t		slock;
	int			rcnt;
	int			wcnt;
	u_char			*rbuf, *rp;
	u_char			*wbuf, *wp;
	struct _devicelayer	*layer;
	struct _devicestack	*stack;
} hisaxdevice_t;

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

static hisaxdevice_t	*hisax_devicelist = NULL;
static rwlock_t	hisax_device_lock = RW_LOCK_UNLOCKED;

static hisaxobject_t	udev_obj;
static char MName[] = "UserDevice";
static u_char  stbuf[1000];

static int device_debug = 0;

static int from_up_down(hisaxif_t *, u_int, int, int, void *);
// static int from_peer(hisaxif_t *, u_int, int, int, void *);
// static int to_peer(hisaxif_t *, u_int, int, int, void *);


static int
hisax_rdata(hisaxdevice_t *dev, iframe_t *iff, int use_value) {
	int len = 4*sizeof(u_int);
	u_char *p;
	u_long flags;

	if (iff->len > 0)
		len +=  iff->len;
	spin_lock_irqsave(&dev->slock, flags);
	if (len < (HISAX_DEVBUF_SIZE - dev->rcnt)) {
		p = dev->rp + dev->rcnt;
		if (len <= 20 && use_value) {
			memcpy(p, iff, len);
		} else {
			memcpy(p, iff, 4*sizeof(u_int));
			p += 4*sizeof(u_int);
			if (iff->len>0)
				memcpy(p, iff->data.p, iff->len);
		}
		dev->rcnt += len;
	} else
		len = -ENOSPC;
	spin_unlock_irqrestore(&dev->slock, flags);
	if (len > 0)
		wake_up_interruptible(&dev->procq);
	return(len);
}

static devicelayer_t
*get_devlayer(hisaxdevice_t   *dev, int addr) {
	devicelayer_t *dl = dev->layer;

	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG __FUNCTION__ ": addr:%x\n", addr);
	while(dl) {
		if (dl->iaddr == (IF_IADDRMASK & addr))
			break;
		dl = dl->next;
	}
	return(dl);
}

static devicestack_t
*get_devstack(hisaxdevice_t   *dev, int addr) {
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
del_if_req(hisaxdevice_t *dev, iframe_t *iff) {
	devicelayer_t *dl;

	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG __FUNCTION__": addr:%x\n", iff->addr);
	if (!(dl=get_devlayer(dev, iff->addr)))
		return(-ENXIO);
	return(remove_if(dl, iff->addr));
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
	int sub, len, err=-ENXIO;
	struct sk_buff *skb;
	u_char	*dp;

	if (device_debug & DEBUG_WDATA)
		printk(KERN_DEBUG __FUNCTION__": addr:%x\n", iff->addr);
	if (!(dl=get_devlayer(dev, iff->addr)))
		return(-ENXIO);
	if (iff->addr & IF_UP) {
		hif = &dl->inst.up;
		if (IF_TYPE(hif) != IF_DOWN) {
			hif = NULL;
		}
	} else if (iff->addr & IF_DOWN) {
		hif = &dl->inst.down;
		if (IF_TYPE(hif) != IF_UP) {
			hif = NULL;
		}
	}
	if (hif) {
		if (!hif->func) {
			printk(KERN_ERR "hisax no interface func for %p\n",
				hif);
			return(-EINVAL);
		} 
		sub =iff->prim & SUBCOMMAND_MASK;
		if (CMD_IS_DATA(iff->prim)) {
			if ((sub == REQUEST) || (sub == INDICATION)) {
				if (iff->len <= 0) {
					printk(KERN_WARNING
						"hisax data len(%d) to short\n",
						iff->len);
					return(-EINVAL);
				}
				len = iff->len;
				if (!(skb = alloc_skb(len + MAX_HEADER_LEN, GFP_ATOMIC))) {
					printk(KERN_WARNING "hisax: alloc_skb failed\n");
					return(-ENOMEM);
				} else
					skb_reserve(skb, MAX_HEADER_LEN);
				dp = &iff->data.b[0];
				memcpy(skb_put(skb, len), dp, len);
				err = hif->func(hif, iff->prim, DINFO_SKB, 0, skb);
				if (err)
					dev_kfree_skb(skb);
				return(err);
			}
		}
		if (device_debug & DEBUG_WDATA)
			printk(KERN_DEBUG "wdata_frame: hif %p f:%p d:%p s:%p o:%p p:%p\n",
				hif, hif->func, hif->fdata, hif->st, hif->owner, hif->peer);
		err = hif->func(hif, iff->prim, iff->dinfo, iff->len, &iff->data.b[0]);
	} else {
		if (device_debug & DEBUG_WDATA)
			printk(KERN_DEBUG "hisax: no matching interface\n");
	}
	return(err);
}

static int
hisax_wdata(hisaxdevice_t *dev, void *dp, int len) {
	iframe_t	*iff = dp;
	iframe_t        off;
	hisaxstack_t	*st;
	devicelayer_t	*dl;
	hisaxlayer_t    *layer;
	int		lay;
	int		err = 0;
	int		used = 0;
	int		head = 4*sizeof(u_int);

	if (len < head) {
		printk(KERN_WARNING "hisax: if_frame(%d) too short\n", len);
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
hisax_open(struct inode *ino, struct file *filep)
{
//	u_int		minor = MINOR(ino->i_rdev);
	hisaxdevice_t 	*newdev;
	u_long flags;

	if (device_debug & DEBUG_DEV_OP)
		printk(KERN_DEBUG "hisax_open in: %p %p\n", filep, filep->private_data);
	if ((newdev = (hisaxdevice_t *) kmalloc(sizeof(hisaxdevice_t), GFP_KERNEL))) {
		memset(newdev, 0, sizeof(hisaxdevice_t));
		newdev->file = filep;
		newdev->inode = ino;
		if (!(newdev->rbuf = vmalloc(HISAX_DEVBUF_SIZE))) {
			kfree(newdev);
			return(-ENOMEM);
		}
		newdev->rp = newdev->rbuf;
		if (!(newdev->wbuf = vmalloc(HISAX_DEVBUF_SIZE))) {
			vfree(newdev->rbuf);
			kfree(newdev);
			return(-ENOMEM);
		}
		newdev->wp = newdev->wbuf;
		newdev->slock = SPIN_LOCK_UNLOCKED;
		hisaxlock_core();
		write_lock_irqsave(&hisax_device_lock, flags);
		APPEND_TO_LIST(newdev, hisax_devicelist);
		write_unlock_irqrestore(&hisax_device_lock, flags);
		filep->private_data = newdev;
	} else
		return(-ENOMEM);
	if (device_debug & DEBUG_DEV_OP)
		printk(KERN_DEBUG "hisax_open out: %p %p\n", filep, filep->private_data);
	return(0);
}

static int
hisax_close(struct inode *ino, struct file *filep)
{
	hisaxdevice_t	*dev = hisax_devicelist;
	u_long flags;

	if (device_debug & DEBUG_DEV_OP)
		printk(KERN_DEBUG "hisax: hisax_close %p %p\n", filep, filep->private_data);
	read_lock(&hisax_device_lock);
	while (dev) {
		if (dev == filep->private_data) {
			if (device_debug & DEBUG_DEV_OP)
				printk(KERN_DEBUG "hisax: dev: %p\n", dev);
			/* release related stuff */
			while(dev->layer)
				del_layer(dev->layer);
			while(dev->stack)
				del_stack(dev->stack);
			read_unlock(&hisax_device_lock);
			write_lock_irqsave(&hisax_device_lock, flags);
			vfree(dev->rbuf);
			vfree(dev->wbuf);
			dev->rp = dev->rbuf = NULL;
			dev->wp = dev->wbuf = NULL;
			dev->rcnt = 0;
			dev->wcnt = 0;
			REMOVE_FROM_LISTBASE(dev, hisax_devicelist);
			write_unlock_irqrestore(&hisax_device_lock, flags);
			filep->private_data = NULL;
			kfree(dev);
			hisaxunlock_core();
			return 0;
		}
		dev = dev->next;
	}
	read_unlock(&hisax_device_lock);
	printk(KERN_WARNING "hisax: No private data while closing device\n");
	return 0;
}

static ssize_t
hisax_read(struct file *file, char *buf, size_t count, loff_t * off)
{
	hisaxdevice_t	*dev = file->private_data;
	int len = 0;
	u_long flags;

	if (off != &file->f_pos)
		return(-ESPIPE);
	if (!dev)
		return(-ENODEV);
	
	if (device_debug & DEBUG_DEV_OP)
		printk(KERN_DEBUG "hisax_read: file %p count %d\n",
			file, count);
	spin_lock_irqsave(&dev->slock, flags);
	if (!dev->rcnt) {
		spin_unlock_irqrestore(&dev->slock, flags);
		if (file->f_flags & O_NONBLOCK)
			return(-EAGAIN);
		interruptible_sleep_on(&dev->procq);
		spin_lock_irqsave(&dev->slock, flags);
		if (!dev->rcnt) {
			spin_unlock_irqrestore(&dev->slock, flags);
			return(-EAGAIN);
		}
	}
	if (count < dev->rcnt)
		len = count;
	else
		len = dev->rcnt;
	if (copy_to_user(buf, dev->rp, len)) {
		spin_unlock_irqrestore(&dev->slock, flags);
		return(-EFAULT);
	}
	dev->rcnt -= len;
	if (dev->rcnt)
		dev->rp += len;
	else
		dev->rp = dev->rbuf;
	spin_unlock_irqrestore(&dev->slock, flags);
	*off += len;
	return(len);
}

static loff_t
hisax_llseek(struct file *file, loff_t offset, int orig)
{
	return -ESPIPE;
}

static ssize_t
hisax_write(struct file *file, const char *buf, size_t count, loff_t * off)
{
	hisaxdevice_t	*dev = file->private_data;
	int used;
	u_long flags;

	if (off != &file->f_pos)
		return(-ESPIPE);
	if (!dev)
		return(-ENODEV);
	if (count>HISAX_DEVBUF_SIZE)
		return(-ENOSPC);
	if (device_debug & DEBUG_DEV_OP)
		printk(KERN_DEBUG "hisax_write: file %p count %d\n",
			file, count);
	spin_lock_irqsave(&dev->slock, flags);
	if (dev->wcnt) {
		spin_unlock_irqrestore(&dev->slock, flags);
		if (file->f_flags & O_NONBLOCK)
			return(-EAGAIN);
		interruptible_sleep_on(&(dev->procq));
		spin_lock_irqsave(&dev->slock, flags);
		if (dev->wcnt) {
			spin_unlock_irqrestore(&dev->slock, flags);
			return(-EAGAIN);
		}
	}
	copy_from_user(dev->wbuf, buf, count);
	dev->wcnt += count;
	dev->wp = dev->wbuf;
	while (dev->wcnt > 0) {
		spin_unlock_irqrestore(&dev->slock, flags);
		used = hisax_wdata(dev, dev->wp, dev->wcnt);
		spin_lock_irqsave(&dev->slock, flags);
		dev->wcnt -= used;
		dev->wp += used;
	}
	dev->wcnt = 0; /* if go negatic due to errors */
	spin_unlock_irqrestore(&dev->slock, flags);
	wake_up_interruptible(&dev->procq);
	return(count);
}

static unsigned int
hisax_poll(struct file *file, poll_table * wait)
{
	unsigned int mask = POLLERR;
	hisaxdevice_t    *dev = file->private_data;

	if (device_debug & DEBUG_DEV_OP)
		printk(KERN_DEBUG "hisax_poll in: file %p\n", file);
	if (dev) {
		if (!dev->rcnt)
			poll_wait(file, &(dev->procq), wait);
		mask = 0;
		if (dev->rcnt) {
			mask |= POLLIN | POLLRDNORM;
		}
		if (!dev->wcnt) {
			mask |= POLLOUT | POLLWRNORM;
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
from_up_down(hisaxif_t *hif, u_int prim, int dinfo, int len, void *arg) {
	
	devicelayer_t *dl;
	iframe_t off;
	int retval = -EINVAL;
	u_int sub;
	struct sk_buff *skb;

	if (!hif || !hif->fdata)
		return(-EINVAL);
	dl = hif->fdata;
	sub = prim & SUBCOMMAND_MASK;
	off.addr = dl->iaddr | IF_TYPE(hif);
	off.prim = prim;
	off.len = 0;
	off.dinfo = dinfo;
	if (device_debug & DEBUG_RDATA)
		printk(KERN_DEBUG "from_up_down: %x(%x) dinfo:%x len:%d\n",
			off.prim, off.addr, dinfo, len);
	if (CMD_IS_DATA(prim)) {
		if (dinfo == DINFO_SKB) {
			if ((sub == REQUEST) || (sub == INDICATION)) {
				if (!(skb = arg))
					return(-EINVAL);
				off.len = skb->len;
				off.data.p = skb->data;
				retval = hisax_rdata(dl->dev, &off, 0);
				if (!retval) {
					if (sub == REQUEST)
						sub = CONFIRM;
					else if (sub == INDICATION)
						sub = RESPONSE;
					prim &= ~SUBCOMMAND_MASK;
					prim |= sub; 
					if (IF_TYPE(hif) == IF_UP)
						retval = dl->inst.up.func(
							&dl->inst.up, prim,
							dinfo, len, arg);
					else if (IF_TYPE(hif) == IF_DOWN)
						retval = dl->inst.down.func(
							&dl->inst.down, prim,
							dinfo, len, arg);
					if (retval) {
						dev_kfree_skb(skb);
						retval = 0;
					}
				}
			} else {
				retval = hisax_rdata(dl->dev, &off, 0);
				if ((skb = arg)) {
					dev_kfree_skb(skb);
				}
			}
		} else {
			printk(KERN_WARNING
				"from_up_down: data prim(%x) no skb type(%x)\n",
				prim, len);
		}
	} else {
		off.data.p = arg;
		off.len = len;
		retval = hisax_rdata(dl->dev, &off, 0);
	}
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
			while(dev->layer)
				del_layer(dev->layer);
			while(dev->stack)
				del_stack(dev->stack);
			REMOVE_FROM_LISTBASE(dev, hisax_devicelist);
			vfree(dev->rbuf);
			vfree(dev->wbuf);
			kfree(dev);
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

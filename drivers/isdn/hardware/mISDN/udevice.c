/* $Id: udevice.c,v 0.3 2001/02/13 14:30:32 kkeil Exp $
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
} hisaxdevice_t;

typedef struct _devicelayer {
	struct _devicelayer	*prev;
	struct _devicelayer	*next;
	hisaxdevice_t		*dev;
	hisaxinstance_t		inst;
	int			iaddr;
} devicelayer_t;

static hisaxdevice_t	*hisax_devicelist = NULL;
static rwlock_t	hisax_device_lock = RW_LOCK_UNLOCKED;

static hisaxobject_t	udev_obj;
static char MName[] = "UserDevice";
static int  stbuf[32*(MAX_LAYER+2)];

static int device_debug = 0;

static int UD_Protocols[] = {	ISDN_PID_ANY };
#define PROTOCOLCNT	(sizeof(UD_Protocols)/sizeof(int))

static int from_up_down(hisaxif_t *, u_int, u_int, int, void *);

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
		printk(KERN_DEBUG "get_devlayer: addr:%x\n", addr);
	while(dl) {
		if (dl->iaddr == (IF_IADDRMASK & addr))
			break;
		dl = dl->next;
	}
	return(dl);
}

static int
create_layer(hisaxdevice_t *dev, hisaxstack_t *st, int *ip) {
	int	ret;
	u_char	*p;
	devicelayer_t *nl;

	if ((*ip < 0) || (*ip > MAX_LAYER))
		return(-EINVAL);
	if (st->inst[*ip]) {
		printk(KERN_WARNING
			"HiSax create_layer st(%d) lay(%d) inst not empty(%p)\n",
			st->id, *ip, st->inst[*ip]);
		return(-EBUSY);
	}
	if (!(nl = kmalloc(sizeof(devicelayer_t), GFP_ATOMIC))) {
		printk(KERN_ERR "kmalloc devicelayer failed\n");
		return(-ENOMEM);
	}
	memset(nl, 0, sizeof(devicelayer_t));
	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG
			"HiSax create_layer lay(%d) nl(%p) nl inst(%p)\n",
			*ip, nl, &nl->inst);
	nl->dev = dev;
	nl->inst.layer = *ip++;
	nl->inst.protocol = *ip++;
	p = (u_char *)ip;
	strcpy(nl->inst.id, p);
	p += HISAX_MAX_IDLEN;
	ip = (int *)p;
	nl->inst.down.protocol = *ip++;
	if ((*ip < 0) || (*ip > MAX_LAYER)) {
		kfree(nl);
		return(-EINVAL);
	}
	nl->inst.down.layer = *ip++;
	nl->inst.up.protocol = *ip++;
	if ((*ip < 0) || (*ip > MAX_LAYER)) {
		kfree(nl);
		return(-EINVAL);
	}
	nl->inst.up.layer = *ip++;
	nl->inst.obj = &udev_obj;
	nl->inst.data = nl;
	APPEND_TO_LIST(nl, dev->layer);
	udev_obj.ctrl(st, MGR_ADDLAYER | INDICATION, &nl->inst);
	nl->inst.down.stat = IF_UP;
	ret = udev_obj.ctrl(st, MGR_ADDIF | REQUEST, &nl->inst.down);
	if (ret) {
		nl->inst.down.stat = IF_NOACTIV;
	}
	nl->inst.up.stat = IF_DOWN;
	ret = udev_obj.ctrl(st, MGR_ADDIF | REQUEST, &nl->inst.up);
	if (ret) {
		nl->inst.up.stat = IF_NOACTIV;
	}
	nl->iaddr = st->id | IADDR_BIT | (0x000F0000 & (nl->inst.layer << 16));
	ret = nl->iaddr;
	return(ret);
}

static int
del_layer(devicelayer_t *dl) {
	hisaxif_t	hif;
	hisaxinstance_t *inst = &dl->inst;
	hisaxdevice_t	*dev = dl->dev;

	if (device_debug & DEBUG_MGR_FUNC) {
		printk(KERN_DEBUG "del_layer: dl(%p) inst(%p) lay(%d) dev(%p) nexti(%p)\n", 
			dl, inst, inst->layer, dev, inst->next);
		printk(KERN_DEBUG "del_layer iaddr %x inst %s\n",
			dl->iaddr, inst->id);
	}
	memset(&hif, 0, sizeof(hisaxif_t));
	hif.fdata = dl;
	hif.func = from_up_down;
	hif.protocol = inst->up.protocol;
	hif.layer = inst->up.layer;
	udev_obj.ctrl(inst->st, MGR_DELIF | REQUEST, &hif);
	hif.fdata = dl;
	hif.func = from_up_down;
	hif.protocol = inst->down.protocol;
	hif.layer = inst->down.layer;
	udev_obj.ctrl(inst->st, MGR_DELIF | REQUEST, &hif);
	dl->iaddr = 0;
	REMOVE_FROM_LISTBASE(dl, dev->layer);
	REMOVE_FROM_LIST(inst);
	if (inst->st) {
		if (device_debug & DEBUG_MGR_FUNC)
			printk(KERN_DEBUG
				"del_layer: st(%p) st->prot(l)%x st->inst(l) %p\n",
				inst->st, inst->st->protocols[inst->layer],
				inst->st->inst[inst->layer]);
		inst->st->protocols[inst->layer] = ISDN_PID_NONE;
		if (inst->st->inst[inst->layer] == inst)
			inst->st->inst[inst->layer] = inst->next;
	}
	kfree(dl);
	udev_obj.refcnt--;
	return(0);
}

static int
wdata_frame(hisaxdevice_t *dev, iframe_t *iff) {
	hisaxif_t *hif = NULL;
	devicelayer_t *dl;
	int sub, len, err=-ENXIO;
	struct sk_buff *skb;
	u_char	*dp;

	if (device_debug & DEBUG_WDATA)
		printk(KERN_DEBUG "wdata_frame: addr:%x\n", iff->addr);
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
				err = hif->func(hif, iff->prim, iff->nr, DTYPE_SKB, skb);
				if (err)
					dev_kfree_skb(skb);
				return(err);
			}
		}
		if (device_debug & DEBUG_WDATA)
			printk(KERN_DEBUG "wdata_frame: hif %p f:%p d:%p s:%p i:%p\n",
				hif, hif->func, hif->fdata, hif->st, hif->inst);
		err = hif->func(hif, iff->prim, iff->nr, iff->len, &iff->data.b[0]);
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
	int		lay;
	int		err = 0;
	int		used = 0;
	int		head = 4*sizeof(u_int);
	int		*ip,i;

	if (len < head) {
		printk(KERN_WARNING "hisax: if_frame(%d) too short\n", len);
		return(len);
	}
	if (device_debug & DEBUG_WDATA)
		printk(KERN_DEBUG "hisax_wdata: %x:%x %d %d\n",
			iff->addr, iff->prim, iff->nr, iff->len);
	switch(iff->prim) {
	    case (MGR_GETSTACK | REQUEST):
		used = head;
		off.addr = iff->addr;
		off.prim = MGR_GETSTACK | CONFIRM;
		off.nr = iff->nr;
		if (iff->addr <= 0) {
			off.data.i = get_stack_cnt();
			off.len = sizeof(int);
			err = 1;
		} else if (iff->addr <= get_stack_cnt()) {
			off.data.p = stbuf;
			get_stack_profile(&off);
		} else
			off.len = 0;
		hisax_rdata(dev, &off, err);
		break;
	    case (MGR_GETLAYER | REQUEST):
		used = head + sizeof(u_int);
		if (len<used)
			return(len);
		off.addr = iff->addr;
		off.prim = MGR_GETLAYER | CONFIRM;
		off.nr = iff->nr;
		lay = iff->data.i;
		off.len = 0;
		if ((lay >= 0) && (lay <= MAX_LAYER)) {
			if ((st = get_stack4id(iff->addr))) {
				if (st->inst[lay]) {
					off.data.p = stbuf;
					ip = stbuf;
					*ip++ = st->inst[lay]->layer;
					*ip++ = st->inst[lay]->protocol;
					if (st->inst[lay]->obj) {
						*ip++ = st->inst[lay]->obj->protcnt;
						for (i=0; i<st->inst[lay]->obj->protcnt; i++) {
							*ip++ = st->inst[lay]->obj->protocols[i];
						}
					} else
						*ip++ = 0;
					off.len = (u_char *)ip - (u_char *)stbuf;
				}
			}
		}
		hisax_rdata(dev, &off, 0);
		break;
	    case (MGR_ADDLAYER | REQUEST):
		used = head + HISAX_MAX_IDLEN + 6*sizeof(u_int);
		if (len<used)
			return(len);
		ip = &iff->data.i;
		off.addr = iff->addr;
		off.nr = iff->nr;
		off.prim = MGR_ADDLAYER | CONFIRM;
		off.len = 4;
		if ((st = get_stack4id(iff->addr))) {
			err = create_layer(dev, st, ip);
			if (err<0)
				off.len = err;
			else
				off.data.i = err;
		} else
			off.len = -ENODEV;
		hisax_rdata(dev, &off, 1);
		break;	
	    case (MGR_DELLAYER | REQUEST):
		used = head;
		off.addr = iff->addr;
		off.prim = MGR_DELLAYER | CONFIRM;
		off.nr = iff->nr;
		if ((dl=get_devlayer(dev, iff->addr)))
			off.len = del_layer(dl);
		else
			off.len = -ENXIO;
		hisax_rdata(dev, &off, 1);
		break;
	    default:
		used = head + iff->len;
		if (len<used) {
			printk(KERN_WARNING "hisax_wdata: framelen error prim %x %d/%d\n",
				iff->prim, len, used);
			used=len;
		} else if (iff->addr & IADDR_BIT) {
			err = wdata_frame(dev, iff);
			if (err)
				if (device_debug & DEBUG_WDATA)
					printk(KERN_DEBUG "wdata_frame returns error %d\n", err);
		} else {
			printk(KERN_WARNING "hisax: prim %x addr %x not implemented\n",
				iff->prim, iff->addr);
			used=len;
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
from_up_down(hisaxif_t *hif, u_int prim, u_int nr, int len, void *arg) {
	
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
	off.nr = nr;
	if (device_debug & DEBUG_RDATA)
		printk(KERN_DEBUG "from_up_down: %x(%x) nr:%d len:%d\n",
			off.prim, off.addr, nr, len);
	if (CMD_IS_DATA(prim)) {
		if (len == DTYPE_SKB) {
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
							nr, len, arg);
					else if (IF_TYPE(hif) == IF_DOWN)
						retval = dl->inst.down.func(
							&dl->inst.down, prim,
							nr, len, arg);
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
add_if(devicelayer_t *dl, hisaxif_t *hif) {
	int err;
	hisaxinstance_t *inst = &dl->inst;

	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG "userdev add_if lay %d/%x prot %x\n", hif->layer,
			hif->stat, hif->protocol);
	hif->fdata = dl;
	if (IF_TYPE(hif) == IF_UP) {
		hif->func = from_up_down;
		if (inst->up.stat == IF_NOACTIV) {
			inst->up.stat = IF_DOWN;
			inst->up.protocol =
				inst->st->protocols[inst->up.layer];
			err = udev_obj.ctrl(inst->st, MGR_ADDIF | REQUEST, &inst->up);
			if (err)
				inst->up.stat = IF_NOACTIV;
		}
	} else if (IF_TYPE(hif) == IF_DOWN) {
		hif->func = from_up_down;
		if (inst->down.stat == IF_NOACTIV) {
			inst->down.stat = IF_UP;
			inst->down.protocol =
				inst->st->protocols[inst->down.layer];
			err = udev_obj.ctrl(inst->st, MGR_ADDIF | REQUEST, &inst->down);
			if (err)
				inst->down.stat = IF_NOACTIV;
		}
	} else
		return(-EINVAL);
	return(0);
}

static int
del_if(devicelayer_t *dl, hisaxif_t *hif) {
	int err;
	hisaxinstance_t *inst = &dl->inst;

	if (device_debug & DEBUG_MGR_FUNC)
		printk(KERN_DEBUG "dev del_if lay %d/%d %p/%p\n", hif->layer,
			hif->stat, hif->func, hif->fdata);
	if ((hif->func == inst->up.func) && (hif->fdata == inst->up.fdata)) {
		inst->up.stat = IF_NOACTIV;
		inst->up.protocol = ISDN_PID_NONE;
		err = udev_obj.ctrl(inst->st, MGR_ADDIF | REQUEST, &inst->up);
	} else if ((hif->func == inst->down.func) && (hif->fdata == inst->down.fdata)) {
		inst->down.stat = IF_NOACTIV;
		inst->down.protocol = ISDN_PID_NONE;
		err = udev_obj.ctrl(inst->st, MGR_ADDIF | REQUEST, &inst->down);
	} else {
		if (device_debug & DEBUG_MGR_FUNC)
			printk(KERN_DEBUG "device del_if no if found\n");
		return(-EINVAL);
	}
	return(0);
}

static int
udev_manager(void *data, u_int prim, void *arg) {
	hisaxstack_t	*st = data;
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
			if (dl->inst.st == st)
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
	    case MGR_ADDIF | REQUEST:
	    	err = add_if(dl, arg);
	    	break;
	    case MGR_DELIF | REQUEST:
	    	err = del_if(dl, arg);
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
	int err;

	udev_obj.name = MName;
	udev_obj.protocols = UD_Protocols;
	udev_obj.protcnt = PROTOCOLCNT;
	udev_obj.own_ctrl = udev_manager;
	udev_obj.prev = NULL;
	udev_obj.next = NULL;
	udev_obj.layer = -1;
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

/* $Id: core.c,v 2.0 2007/06/06 10:43:45 kkeil Exp $
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

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/mISDNif.h>
#include "core.h"
#include "socket.h"

static char		*core_revision = "$Revision: 2.0 $";
static char		*core_version = MISDNVERSION ;

static u_int debug;

#ifdef MODULE
MODULE_AUTHOR("Karsten Keil");
MODULE_LICENSE("GPL");
module_param (debug, uint, S_IRUGO | S_IWUSR);
#endif

static LIST_HEAD(devices);
static rwlock_t		device_lock = RW_LOCK_UNLOCKED;
static u64		device_ids;
#define MAX_DEVICE_ID	63

struct mISDNdevice
*get_mdevice(u_int id)
{
	struct mISDNdevice	*dev;

	read_lock(&device_lock);
	list_for_each_entry(dev, &devices, D.list)
		if (dev->id == id) {
			read_unlock(&device_lock);
			return(dev);
		}
	read_unlock(&device_lock);
	return(NULL);
}

int
get_mdevice_count(void)
{
	struct mISDNdevice	*dev;
	int			cnt = 0;

	read_lock(&device_lock);
	list_for_each_entry(dev, &devices, D.list)
		cnt++;
	read_unlock(&device_lock);
	return(cnt);
}

static int
get_free_devid(void)
{
	u_int	i;

	for (i = 0; i <= MAX_DEVICE_ID; i++)
		if (!test_and_set_bit(i, &device_ids))
			return i;
	return -1;
}

static int
get_free_stack_id(struct mISDNdevice *dev)
{
	u64	ids = 0;
	int	i;
	struct mISDNstack *st;

	list_for_each_entry(st, &dev->stacks, list) {
		if (st->id > 63) {
			printk(KERN_WARNING 
			    "%s: more as 63 stacks for one device %s\n",
			    __FUNCTION__, dev->name);
			return -1;
		}
		test_and_set_bit(st->id, &ids);
	}
	for (i = 0; i < 64; i++)
		if (!test_bit(i, &ids))
			return i;
	printk(KERN_WARNING "%s: more as 63 stacks for one device %s\n",
	    __FUNCTION__, dev->name);
	return -1;
}

int
register_stack(struct mISDNstack *st, struct mISDNdevice *dev)
{
	u_long	flags;
	int	id, ret = 0;

	write_lock_irqsave(&dev->slock, flags);
	id = get_free_stack_id(dev);
	if (id < 0) {
		ret = -EBUSY;
		goto done;
	}
	st->id = id;
	st->dev = dev;
	list_add_tail(&st->list, &dev->stacks);
	sprintf(st->name, "%s %d", dev->name, id);
done:
	write_unlock_irqrestore(&dev->slock, flags);
	return ret;
}

int
mISDN_register_device(struct mISDNdevice *dev)
{
	u_long	flags;
	int	err;

	dev->id = get_free_devid();
	if (dev->id < 0)
		return -EBUSY;
	sprintf(dev->name,"mISDN%d", dev->id);
	if (debug & DEBUG_CORE)
		printk(KERN_DEBUG "mISDN_register %s %d\n",
			dev->name, dev->id);
	dev->slock = RW_LOCK_UNLOCKED;
	INIT_LIST_HEAD(&dev->stacks);
	err = create_mgr_stack(dev);
	if (err)
		return err;
	write_lock_irqsave(&device_lock, flags);
	list_add_tail(&dev->D.list, &devices);
	write_unlock_irqrestore(&device_lock, flags);
	return 0;
}

void
mISDN_unregister_device(struct mISDNdevice *dev) {
	u_long	flags;
	struct mISDNstack *st, *nst;

	if (debug & DEBUG_CORE)
		printk(KERN_DEBUG "mISDN_unregister %s %d\n",
			dev->name, dev->id);
	write_lock_irqsave(&device_lock, flags);
	list_del(&dev->D.list);
	write_unlock_irqrestore(&device_lock, flags);
	test_and_clear_bit(dev->id, &device_ids);
	write_lock_irqsave(&dev->slock, flags);
	list_for_each_entry_safe(st, nst, &dev->stacks, list) {
		delete_stack(st);
	}
	write_unlock_irqrestore(&dev->slock, flags);
}

extern int	l1_init(u_int *);
extern void	l1_cleanup(void);
extern int 	Isdnl2_Init(u_int *);
extern void	Isdnl2_cleanup(void);

int
mISDNInit(void)
{
	int	err;

	printk(KERN_INFO "Modular ISDN core version (%s) revision (%s)\n",
		core_version, core_revision);
	mISDN_initstack(&debug);
	err = l1_init(&debug);
	if (err) {
		goto error;
	}
	err = Isdnl2_Init(&debug);
	if (err) {
		l1_cleanup();
		goto error;
	}
	err = misdn_sock_init(&debug);
	if (err) {
		l1_cleanup();
		Isdnl2_cleanup();
	}
error:
	return err;
}

void mISDN_cleanup(void) {
	misdn_sock_cleanup();
	l1_cleanup();
	Isdnl2_cleanup();
	printk(KERN_DEBUG "mISDNcore unloaded\n");
}

module_init(mISDNInit);
module_exit(mISDN_cleanup);

EXPORT_SYMBOL(mISDN_register_device);
EXPORT_SYMBOL(mISDN_unregister_device);

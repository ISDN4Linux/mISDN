/* $Id: sysfs_inst.c,v 1.6 2006/05/18 13:35:46 crich Exp $
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 * mISDN sysfs stuff for isnstances
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */
#include "core.h"
#include "sysfs.h"

#define to_mISDNinstance(d) container_of(d, mISDNinstance_t, class_dev)

static ssize_t show_inst_id(struct class_device *class_dev, char *buf)
{
	mISDNinstance_t	*inst = to_mISDNinstance(class_dev);
	return sprintf(buf, "%08x\n", inst->id);
}
static CLASS_DEVICE_ATTR(id, S_IRUGO, show_inst_id, NULL);

static ssize_t show_inst_name(struct class_device *class_dev, char *buf)
{
	mISDNinstance_t	*inst = to_mISDNinstance(class_dev);
	return sprintf(buf, "%s\n", inst->name);
}
static CLASS_DEVICE_ATTR(name, S_IRUGO, show_inst_name, NULL);

static ssize_t show_inst_extentions(struct class_device *class_dev, char *buf)
{
	mISDNinstance_t	*inst = to_mISDNinstance(class_dev);
	return sprintf(buf, "%08x\n", inst->extentions);
}
static CLASS_DEVICE_ATTR(extentions, S_IRUGO, show_inst_extentions, NULL);

static ssize_t show_inst_regcnt(struct class_device *class_dev, char *buf)
{
	mISDNinstance_t	*inst = to_mISDNinstance(class_dev);
	return sprintf(buf, "%d\n", inst->regcnt);
}
static CLASS_DEVICE_ATTR(regcnt, S_IRUGO, show_inst_regcnt, NULL);

MISDN_PROTO(mISDNinstance, pid, S_IRUGO);

static void release_mISDN_inst(struct class_device *dev)
{
	mISDNinstance_t	*inst = to_mISDNinstance(dev);

#ifdef SYSFS_REMOVE_WORKS
	if (inst->obj)
		sysfs_remove_link(&dev->kobj, "obj");
	sysfs_remove_group(&inst->class_dev.kobj, &pid_group);
#endif
	if (core_debug & DEBUG_SYSFS)
		printk(KERN_INFO "release instance class dev %s\n", dev->class_id);
}

static struct class inst_dev_class = {
	.name		= "mISDN-instances",
#ifndef CLASS_WITHOUT_OWNER
	.owner		= THIS_MODULE,
#endif
	.release	= &release_mISDN_inst,
};

int
mISDN_register_sysfs_inst(mISDNinstance_t *inst) {
	int	err;
	char	name[8];

	inst->class_dev.class = &inst_dev_class;
	snprintf(inst->class_dev.class_id, BUS_ID_SIZE, "inst-%08x", inst->id);
	err = class_device_register(&inst->class_dev);
	if (err)
		return(err);
	class_device_create_file(&inst->class_dev, &class_device_attr_id);
	class_device_create_file(&inst->class_dev, &class_device_attr_name);
	class_device_create_file(&inst->class_dev, &class_device_attr_extentions);
	class_device_create_file(&inst->class_dev, &class_device_attr_regcnt);
	err = sysfs_create_group(&inst->class_dev.kobj, &pid_group);
	if (err)
		goto out_unreg;
	if (inst->obj)
		sysfs_create_link(&inst->class_dev.kobj, &inst->obj->class_dev.kobj, "obj");
	if (inst->st) {
		sprintf(name,"layer.%d", inst->id & LAYER_ID_MASK);
		sysfs_create_link(&inst->st->class_dev.kobj, &inst->class_dev.kobj, name);
		sysfs_create_link(&inst->class_dev.kobj, &inst->st->class_dev.kobj, "stack");
		if (inst->st->mgr == inst) {
			sysfs_create_link(&inst->st->class_dev.kobj, &inst->class_dev.kobj, "mgr");
		}
	}
	return(err);

out_unreg:
	class_device_unregister(&inst->class_dev);
	return(err);
}

void
mISDN_unregister_sysfs_inst(mISDNinstance_t *inst)
{
	char	name[8];

	if (inst->id) {
		if (inst->st) {
			sprintf(name,"layer.%d", inst->id & LAYER_ID_MASK);

#ifdef SYSFS_REMOVE_WORKS
			sysfs_remove_link(&inst->st->class_dev.kobj, name);
			sysfs_remove_link(&inst->class_dev.kobj, "stack");
			if (inst->st->mgr == inst)
				sysfs_remove_link(&inst->st->class_dev.kobj, "mgr");
#endif
		}
		class_device_unregister(&inst->class_dev);
	}
}

int
mISDN_sysfs_inst_init(void)
{
	return(class_register(&inst_dev_class));
}

void
mISDN_sysfs_inst_cleanup(void)
{
	class_unregister(&inst_dev_class);
}

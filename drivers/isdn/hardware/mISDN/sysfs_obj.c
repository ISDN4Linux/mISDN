/* $Id: sysfs_obj.c,v 1.6 2006/05/18 13:35:46 crich Exp $
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 * mISDN sysfs object and common stuff
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */
#include "core.h"
#include "sysfs.h"

#define to_mISDNobject(d) container_of(d, mISDNobject_t, class_dev)

static ssize_t show_obj_name(struct class_device *class_dev, char *buf)
{
	mISDNobject_t *obj = to_mISDNobject(class_dev);
	return sprintf(buf, "%s\n", obj->name);
}

static CLASS_DEVICE_ATTR(name, S_IRUGO, show_obj_name, NULL);

static ssize_t show_obj_id(struct class_device *class_dev, char *buf)
{
	mISDNobject_t *obj = to_mISDNobject(class_dev);
	return sprintf(buf, "%d\n", obj->id);
}
static CLASS_DEVICE_ATTR(id, S_IRUGO, show_obj_id, NULL);

static ssize_t show_obj_refcnt(struct class_device *class_dev, char *buf)
{
	mISDNobject_t *obj = to_mISDNobject(class_dev);
	return sprintf(buf, "%d\n", obj->refcnt);
}
static CLASS_DEVICE_ATTR(refcnt, S_IRUGO, show_obj_refcnt, NULL);

ssize_t mISDN_show_pid_protocol(mISDN_pid_t *pid, char *buf)
{
	char	*p = buf;
	uint	i;

	for (i=0; i <= MAX_LAYER_NR; i++)
		p += sprintf(p,"0x%08x,", pid->protocol[i]);
	p--;
	*p++ = '\n';
	return (p -buf);
}

ssize_t mISDN_show_pid_parameter(mISDN_pid_t *pid, char *buf)
{
	char	*p = buf, *t;
	uint	i, l;

	for (i=0; i <= MAX_LAYER_NR; i++) {
		if (pid->param[i]) {
			t = pid->param[i];
			l = *t++;
			p += sprintf(p,"0x%02x,", l);
			while(l--)
				p += sprintf(p,"0x%02x,", *t++);
		}else {
			p += sprintf(p,"0x00,");
		}
	}
	p--;
	*p++ = '\n';
	return (p -buf);
}

MISDN_PROTO(mISDNobject, BPROTO, S_IRUGO);
MISDN_PROTO(mISDNobject, DPROTO, S_IRUGO);

static void release_mISDN_obj(struct class_device *dev)
{
	mISDNobject_t	*obj = to_mISDNobject(dev);

	if ( core_debug & DEBUG_SYSFS) 
		printk(KERN_INFO "release object class dev %s\n", dev->class_id);

#ifdef SYSFS_REMOVE_WORKS
	if (obj->owner)
#ifdef MODULE_MKOBJ_POINTER
	if (obj->owner->mkobj)
#endif
		sysfs_remove_link(&dev->kobj, "module");
	sysfs_remove_group(&obj->class_dev.kobj, &BPROTO_group);
	sysfs_remove_group(&obj->class_dev.kobj, &DPROTO_group);
#endif

}

static struct class obj_dev_class = {
	.name		= "mISDN-objects",
#ifndef CLASS_WITHOUT_OWNER
	.owner		= THIS_MODULE,
#endif
	.release	= &release_mISDN_obj,
};

int
mISDN_register_sysfs_obj(mISDNobject_t *obj) {
	int	err;

	obj->class_dev.class = &obj_dev_class;
	snprintf(obj->class_dev.class_id, BUS_ID_SIZE, "obj-%d", obj->id);
	err = class_device_register(&obj->class_dev);
	if (err)
		goto out;
	class_device_create_file(&obj->class_dev, &class_device_attr_id);
	class_device_create_file(&obj->class_dev, &class_device_attr_name);
	class_device_create_file(&obj->class_dev, &class_device_attr_refcnt);
	err = sysfs_create_group(&obj->class_dev.kobj, &BPROTO_group);
	if (err)
		goto out_unreg;
	err = sysfs_create_group(&obj->class_dev.kobj, &DPROTO_group);
	if (err)
		goto out_unreg;
	if (obj->owner)
#ifdef MODULE_MKOBJ_POINTER
		if (obj->owner->mkobj)
			sysfs_create_link(&obj->class_dev.kobj, &obj->owner->mkobj->kobj, "module");
#else
		sysfs_create_link(&obj->class_dev.kobj, &obj->owner->mkobj.kobj, "module");
#endif
	return(err);
out_unreg:
	class_device_unregister(&obj->class_dev);
out:
	return(err);
}

int
mISDN_sysfs_init(void) {
	int	err;

	err = class_register(&obj_dev_class);
	if (err)
		return(err);
	err = mISDN_sysfs_inst_init();
	if (err)
		goto unreg_obj;
	err = mISDN_sysfs_st_init();
	if (err)
		goto unreg_inst;
	return(err);
unreg_inst:
	mISDN_sysfs_inst_cleanup();
unreg_obj:
	class_unregister(&obj_dev_class);
	return(err);
}

void
mISDN_sysfs_cleanup(void) {
	class_unregister(&obj_dev_class);
	mISDN_sysfs_inst_cleanup();
	mISDN_sysfs_st_cleanup();
}

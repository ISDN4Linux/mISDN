/* $Id: sysfs.h,v 1.2 2006/03/06 12:58:31 keil Exp $
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 * mISDN sysfs common defines
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */
#include <linux/stringify.h>

extern ssize_t	mISDN_show_pid_protocol(mISDN_pid_t *, char *);
extern ssize_t	mISDN_show_pid_parameter(mISDN_pid_t *, char *);

static inline ssize_t show_pid_layermask(mISDN_pid_t *pid, char *buf)
{
	return sprintf(buf, "0x%08x\n", pid->layermask);
}

static inline ssize_t show_pid_global(mISDN_pid_t *pid, char *buf)
{
	return sprintf(buf, "0x%04x\n", pid->global);
}

static inline ssize_t show_pid_maxplen(mISDN_pid_t *pid, char *buf)
{
	return sprintf(buf, "%d\n", pid->maxplen);
}

#define MISDN_PROTO(_type, _name, _mode) \
static ssize_t show_protocol_##_name(struct class_device *class_dev, char *buf) \
{ \
	_type##_t	*p = to_##_type(class_dev); \
	return(mISDN_show_pid_protocol(&p->_name, buf)); \
} \
struct class_device_attribute _type##_attr_protocol_##_name = \
	__ATTR(protocol,_mode,show_protocol_##_name, NULL); \
static ssize_t show_parameter_##_name(struct class_device *class_dev, char *buf) \
{ \
	_type##_t	*p = to_##_type(class_dev); \
	return(mISDN_show_pid_parameter(&p->_name, buf)); \
} \
struct class_device_attribute _type##_attr_parameter_##_name = \
	__ATTR(parameter,_mode,show_parameter_##_name, NULL); \
static ssize_t show_layermask_##_name(struct class_device *class_dev, char *buf) \
{ \
	_type##_t	*p = to_##_type(class_dev); \
	return(show_pid_layermask(&p->_name, buf)); \
} \
struct class_device_attribute _type##_attr_layermask_##_name = \
	__ATTR(layermask,_mode,show_layermask_##_name, NULL); \
static ssize_t show_global_##_name(struct class_device *class_dev, char *buf) \
{ \
	_type##_t	*p = to_##_type(class_dev); \
	return(show_pid_global(&p->_name, buf)); \
} \
struct class_device_attribute _type##_attr_global_##_name = \
	__ATTR(global,_mode,show_global_##_name, NULL); \
static ssize_t show_maxplen_##_name(struct class_device *class_dev, char *buf) \
{ \
	_type##_t	*p = to_##_type(class_dev); \
	return(show_pid_maxplen(&p->_name, buf)); \
} \
struct class_device_attribute _type##_attr_maxplen_##_name = \
	__ATTR(maxplen,_mode,show_maxplen_##_name, NULL); \
static struct attribute *attr_##_name[] = { \
	&_type##_attr_global_##_name.attr, \
	&_type##_attr_layermask_##_name.attr, \
	&_type##_attr_maxplen_##_name.attr, \
	&_type##_attr_parameter_##_name.attr, \
	&_type##_attr_protocol_##_name.attr, \
	NULL \
}; \
static struct attribute_group _name##_group = { \
	.name  = __stringify(_name), \
	.attrs  = attr_##_name, \
}

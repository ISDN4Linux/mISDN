#ifndef _LINUX_ISDN_COMPAT_H
#define _LINUX_ISDN_COMPAT_H

#ifdef __KERNEL__
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10)
#define	OLD_PCI_REGISTER_DRIVER	1
#define OLD_MODULE_PARAM_ARRAY
#else
#undef	OLD_PCI_REGISTER_DRIVER
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,8)
#define __ATTR(_name,_mode,_show,_store) { \
	.attr = {.name = __stringify(_name), .mode = _mode, .owner = THIS_MODULE },	\
	.show   = _show,								\
	.store  = _store,								\
}
#define LOCAL_FCSTAB
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11)
#define MODULE_MKOBJ_POINTER
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,11)
#define CLASSDEV_HAS_DEVT
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,13)
#define OLD_MODULE_PARAM
/* udev sysfs stuff */
#define CLASS_WITHOUT_OWNER
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,9)
#define kzalloc kmalloc
#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14)
#define kzalloc(s,f) kcalloc(1,s,f)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
#include <linux/config.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
typedef void    (WFUNC_t)(void *);
#define _INIT_WORK(a, b) INIT_WORK(a, (WFUNC_t *)b, a)
#else
#define _INIT_WORK(a, b) INIT_WORK(a, b)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
#define _EACH_DEVICE(a, b, c) class_for_each_device(a, b, c)
#define _FIND_DEVICE(a, b, c) class_find_device(a, b, c)
#else
#define _EACH_DEVICE(a, b, c) class_for_each_device(a, NULL, b, c)
#define _FIND_DEVICE(a, b, c) class_find_device(a, NULL, b, c)
#endif


#include <linux/interrupt.h>
#ifndef IRQF_SHARED
#define IRQF_SHARED	SA_SHIRQ
#endif

#endif /* __KERNEL__ */
#endif /* _LINUX_ISDN_COMPAT_H */

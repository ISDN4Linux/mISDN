#ifndef _LINUX_ISDN_COMPAT_H
#define _LINUX_ISDN_COMPAT_H

#ifdef __KERNEL__
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,2,18)

#define set_current_state(sta) (current->state = sta)
#define module_init(x)  int init_module(void) { return x(); }
#define module_exit(x)  void cleanup_module(void) { x(); }
#define BUG() do { printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); *(int
#define init_MUTEX(x)                           *(x)=MUTEX
#define init_MUTEX_LOCKED(x)                    *(x)=MUTEX_LOCKED
#define __devinit
#define __devinitdata

#endif
#endif /* __KERNEL__ */
#endif /* _LINUX_ISDN_COMPAT_H */

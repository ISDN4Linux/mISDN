#ifndef _LINUX_ISDN_COMPAT_H
#define _LINUX_ISDN_COMPAT_H

#ifdef __KERNEL__
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,2,18)

#define set_current_state(sta) (current->state = sta)
#define module_init(x)  int init_module(void) { return x(); }
#define module_exit(x)  void cleanup_module(void) { x(); }
#define BUG() do { printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); *(int *)0 = 0; } while (0)
#define init_MUTEX(x)                           *(x)=MUTEX
#define init_MUTEX_LOCKED(x)                    *(x)=MUTEX_LOCKED
#define __devinit
#define __devinitdata

#else
#define COMPAT_HAS_NEW_WAITQ
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)

#define COMPAT_HAS_2_2_PCI
#define get_pcibase(ps,nr) ps->base_address[nr]
#define pci_resource_start_io(pdev,nr)	pdev->base_address[nr] & PCI_BASE_ADDRESS_IO_MASK
#define pci_resource_start_mem(pdev,nr)	pdev->base_address[nr] & PCI_BASE_ADDRESS_MEM_MASK
#define pci_get_sub_vendor(pdev, id)	pci_read_config_word(pdev, PCI_SUBSYSTEM_VENDOR_ID, &id)
#define pci_get_sub_system(pdev, id)	pci_read_config_word(pdev, PCI_SUBSYSTEM_ID, &id)
#define dev_kfree_skb_any(a)		dev_kfree_skb(a)
#define dev_kfree_skb_irq(a)		dev_kfree_skb(a)
typedef	struct timer_list		timer_t;
#else /* 2.4.0 and later */
#include <linux/netdevice.h>
#define pci_resource_start_io(pdev, nr) pci_resource_start(pdev, nr)
#define pci_resource_start_mem(pdev, nr) pci_resource_start(pdev, nr)
#define get_pcibase(ps, nr) ps->resource[nr].start
#define pci_get_sub_system(pdev, id)	id = pdev->subsystem_device
#define pci_get_sub_vendor(pdev, id)	id = pdev->subsystem_vendor
#endif /* 2,4,0 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
#ifndef IRQ_HANDLED /* maybe these are also defined in include/linux/interrupt.h */
typedef void irqreturn_t;
#define IRQ_NONE
#define IRQ_HANDLED
#define IRQ_RETVAL(x)
#endif
#define CAPI_SendMessage_void
#define OLDCAPI_DRIVER_INTERFACE
#undef  HAS_WORKQUEUE
#define work_struct	tq_struct
#define INIT_WORK(q, f, d)	(q)->routine=f;(q)->data=d;
#define schedule_work(q)	queue_task(q, &tq_immediate);mark_bh(IMMEDIATE_BH);
#define MAKEDAEMON(n)		daemonize();strcpy(current->comm, n)
#undef NEW_ISAPNP
#define pnp_register_driver(d)		isapnp_register_driver(d)
#define pnp_unregister_driver(d)	isapnp_unregister_driver(d)
#define pnp_get_drvdata(d)		pci_get_drvdata(d)
#define pnp_set_drvdata(p,d)		pci_set_drvdata(p,d)
#define pnp_activate_dev(d)		isapnp_activate_dev(d, "mISDN")
#define pnp_disable_dev(d)		((struct pci_dev *)d)->prepare(d);((struct pci_dev *)d)->deactivate(d)
#define pnp_port_start(d,n)		d->resource[n].start
#define pnp_irq(d,n)			d->irq_resource[n].start
#undef iminor
#define iminor(i)	MINOR(i->i_rdev)
#else
#undef  OLDCAPI_DRIVER_INTERFACE
#define HAS_WORKQUEUE
#undef  MINOR
#define MINOR(inode)	minor(inode)
#define NEED_JIFFIES_INCLUDE
#define MAKEDAEMON(n)	daemonize(n)
#define NEW_ISAPNP
#endif /* 2,5,0 */

#ifndef COMPAT_HAS_NEW_WAITQ
typedef struct wait_queue wait_queue_t;
typedef struct wait_queue *wait_queue_head_t;

#define DECLARE_WAITQUEUE(wait, current)	struct wait_queue wait = { current, NULL }
#define DECLARE_WAIT_QUEUE_HEAD(wait)		wait_queue_head_t wait
#define init_waitqueue_head(x)			*(x)=NULL
#define init_waitqueue_entry(q,p)		((q)->task)=(p)
#endif /* COMPAT_HAS_NEW_WAITQ */

#endif /* __KERNEL__ */
#endif /* _LINUX_ISDN_COMPAT_H */

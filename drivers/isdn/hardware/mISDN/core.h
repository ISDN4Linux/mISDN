/* $Id: core.h,v 1.19 2006/12/21 15:25:06 nadi Exp $
 * 
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#include <linux/slab.h>
#include <linux/string.h>
#include <linux/mISDNif.h>
#include <linux/mISDNdebugtool.h>
#include "helper.h"
#ifdef MISDN_MEMDEBUG
#include "memdbg.h"
#endif

#define	mISDN_MAJOR		46
#define mISDN_MINOR_CORE	0
#define mISDN_MINOR_RAW_MIN	128
#define mISDN_MINOR_RAW_MAX	255

/* debugging */
#define DEBUG_CORE_FUNC		0x0001
//#define DEBUG_DUMMY_FUNC	0x0002
#define DEBUG_MSG_THREAD_ERR	0x0010
#define DEBUG_MSG_THREAD_INFO	0x0020
#define DEBUG_QUEUE_FUNC	0x0040
#define DEBUG_DEV_OP		0x0100
#define DEBUG_MGR_FUNC		0x0200
#define DEBUG_DEV_TIMER		0x0400
#define DEBUG_RDATA		0x1000
#define DEBUG_WDATA		0x2000
#define DEBUG_SYSFS		0x4000
#define DEBUG_THREADS		0x8000


/* from udevice.c */
extern int		init_mISDNdev(int);
extern int		free_mISDNdev(void);
extern mISDNdevice_t	*get_free_rawdevice(void);
extern int		free_device(mISDNdevice_t *dev);

/* from stack.c */
extern void		get_stack_info(struct sk_buff *);
extern int		get_stack_cnt(void);
extern void		check_stacklist(void);
extern void		cleanup_object(mISDNobject_t *);
extern mISDNstack_t	*get_stack4id(u_int);
extern int		mISDN_start_stack_thread(mISDNstack_t *);
extern mISDNstack_t	*new_stack(mISDNstack_t *, mISDNinstance_t *);
extern int		mISDN_start_stop(mISDNstack_t *, int);
extern int		release_stack(mISDNstack_t *);
extern int		do_for_all_layers(void *, u_int, void *);
extern int		change_stack_para(mISDNstack_t *, u_int, mISDN_stPara_t *);
extern void		release_stacks(mISDNobject_t *);
extern int		copy_pid(mISDN_pid_t *, mISDN_pid_t *, u_char *);
extern int		set_stack(mISDNstack_t *, mISDN_pid_t *);
extern int		clear_stack(mISDNstack_t *, int);
extern int		evaluate_stack_pids(mISDNstack_t *, mISDN_pid_t *);
extern mISDNinstance_t	*getlayer4lay(mISDNstack_t *, int);
extern mISDNinstance_t	*get_instance(mISDNstack_t *, int, int);

/* from sysfs_obj.c */
extern int		mISDN_register_sysfs_obj(mISDNobject_t *);
extern int		mISDN_sysfs_init(void);
extern void		mISDN_sysfs_cleanup(void);

/* from sysfs_inst.c */
extern int		mISDN_register_sysfs_inst(mISDNinstance_t *);
extern void		mISDN_unregister_sysfs_inst(mISDNinstance_t *);
extern int		mISDN_sysfs_inst_init(void);
extern void		mISDN_sysfs_inst_cleanup(void);

/* from sysfs_stack.c */
extern int		mISDN_register_sysfs_stack(mISDNstack_t *);
extern void		mISDN_unregister_sysfs_st(mISDNstack_t *);
extern int		mISDN_sysfs_st_init(void);
extern void		mISDN_sysfs_st_cleanup(void);

/* from core.c */
extern int core_debug;
extern int		register_layer(mISDNstack_t *, mISDNinstance_t *);
extern int		preregister_layer(mISDNstack_t *, mISDNinstance_t *);
extern int		unregister_instance(mISDNinstance_t *);
extern mISDNinstance_t	*get_next_instance(mISDNstack_t *, mISDN_pid_t *);
extern mISDNobject_t	*get_object(int);
extern mISDNinstance_t	*get_instance4id(u_int);
extern int		mISDN_alloc_entity(int *);
extern int		mISDN_delete_entity(int);
extern void		mISDN_module_register(struct module *);
extern void		mISDN_module_unregister(struct module *);
extern void		mISDN_inc_usage(void);
extern void		mISDN_dec_usage(void);

/* debugtool helpers from core.c */
extern void     mISDN_dt_set_callback(void (*new_frame) (mISDNstack_t *stack, enum mISDN_dt_type type, struct sk_buff *skb, int duplicate_skb));
extern void     mISDN_dt_enable(void);
extern void     mISDN_dt_disable(void);
extern void     mISDN_dt_new_frame(mISDNstack_t *stack, enum mISDN_dt_type type, struct sk_buff *skb, int duplicate_skb);

#ifdef CONFIG_MISDN_NETDEV
/* from netdev_main.c */
void misdn_log_frame(mISDNstack_t *, 		/* Stack for which to log */
		unsigned char *,		/* frame to log */
		int, 				/* frame len */
		int );				/* direction (0=rx,1=tx) */

int misdn_netdev_addstack(mISDNstack_t *); 	/* create new netdevice by 
						   stack */

int              misdn_netdev_init(void); 	/* initialize netdevices */
void             misdn_netdev_exit(void);	/* exit netdeivces */
#endif


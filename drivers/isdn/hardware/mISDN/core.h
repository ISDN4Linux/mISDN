/* $Id: core.h,v 1.9 2004/06/30 15:13:18 keil Exp $
 * 
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#include <linux/slab.h>
#include <linux/string.h>
#include <linux/mISDNif.h>
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
#define DEBUG_DUMMY_FUNC	0x0002
#define DEBUG_DEV_OP		0x0100
#define DEBUG_MGR_FUNC		0x0200
#define DEBUG_DEV_TIMER		0x0400
#define DEBUG_RDATA		0x1000
#define DEBUG_WDATA		0x2000

/* from mISDN_dev.c */

extern int		init_mISDNdev(int);
extern int		free_mISDNdev(void);
extern mISDNdevice_t	*get_free_rawdevice(void);
extern int		free_device(mISDNdevice_t *dev);

/* from mISDN_stack.c */

extern struct list_head	mISDN_stacklist;
extern struct list_head	mISDN_instlist;

extern void		get_stack_info(struct sk_buff *);
extern int		get_stack_cnt(void);
extern mISDNstack_t	*get_stack4id(u_int);
extern mISDNstack_t	*new_stack(mISDNstack_t *, mISDNinstance_t *);
extern int		release_stack(mISDNstack_t *);
extern int		do_for_all_layers(mISDNstack_t *, u_int, void *);
extern int		change_stack_para(mISDNstack_t *, u_int, mISDN_stPara_t *);
extern void		release_stacks(mISDNobject_t *);
extern int		copy_pid(mISDN_pid_t *, mISDN_pid_t *, u_char *);
extern int		set_stack(mISDNstack_t *, mISDN_pid_t *);
extern int		clear_stack(mISDNstack_t *);
extern int		evaluate_stack_pids(mISDNstack_t *, mISDN_pid_t *);
extern mISDNlayer_t	*getlayer4lay(mISDNstack_t *, int);
extern mISDNinstance_t	*get_instance(mISDNstack_t *, int, int);

/* from mISDN_core.c */

extern struct list_head	mISDN_objectlist;
extern int core_debug;

extern int		register_layer(mISDNstack_t *, mISDNinstance_t *);
extern int		unregister_instance(mISDNinstance_t *);
extern mISDNinstance_t	*get_next_instance(mISDNstack_t *, mISDN_pid_t *);
extern mISDNobject_t	*get_object(int);
extern mISDNinstance_t	*get_instance4id(u_int);
extern int		mISDN_alloc_entity(int *);
extern int		mISDN_delete_entity(int);

/* $Id: core.h,v 0.7 2001/04/08 16:45:56 kkeil Exp $
 * 
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/hisaxif.h>
#include "helper.h"
#ifdef MEMDBG
#include "memdbg.h"
#endif

#define	HISAX_MAJOR		46
#define HISAX_DEVBUF_SIZE	8192

/* debugging */
#define DEBUG_CORE_FUNC		0x0001
#define DEBUG_DUMMY_FUNC	0x0002
#define DEBUG_DEV_OP		0x0100
#define DEBUG_MGR_FUNC		0x0200
#define DEBUG_RDATA		0x1000
#define DEBUG_WDATA		0x2000

/* from hisax_dev.c */

extern int init_hisaxdev(int);
extern int free_hisaxdev(void);

/* from hisax_stack.c */

extern hisaxstack_t	*hisax_stacklist;

extern void		get_stack_profile(iframe_t *);
extern int		get_stack_cnt(void);
extern hisaxstack_t	*get_stack4id(int);
extern hisaxstack_t	*new_stack(hisaxstack_t *, hisaxinstance_t *);
extern int		release_stack(hisaxstack_t *);
extern void		release_stacks(hisaxobject_t *);
extern int		set_stack(hisaxstack_t *, hisax_pid_t *);
extern int		clear_stack(hisaxstack_t *);
extern hisaxlayer_t	*getlayer4lay(hisaxstack_t *, int);
extern hisaxinstance_t	*get_instance(hisaxstack_t *, int, int);

/* from hisax_core.c */

extern hisaxobject_t	*hisax_objects;
extern int core_debug;

extern void		hisaxlock_core(void);
extern void		hisaxunlock_core(void);
extern int		register_layer(hisaxstack_t *, hisaxinstance_t *);
extern int		unregister_instance(hisaxinstance_t *);
extern hisaxinstance_t	*get_next_instance(hisaxstack_t *, hisax_pid_t *);
extern hisaxobject_t	*get_object(int);
extern hisaxinstance_t	*get_instance4id(int);

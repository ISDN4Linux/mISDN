/*
 *
 */

#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/hisaxif.h>
#include "helper.h"
 
/* intern exported lists */
extern hisaxobject_t	*hisax_objects;
extern hisaxstack_t	*hisax_stacklist;

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

/* from hisax_core.c */
extern void get_stack_profile(iframe_t *);
extern int get_stack_cnt(void);
extern hisaxstack_t *get_stack4id(int);

extern void hisaxlock_core(void);
extern void hisaxunlock_core(void);

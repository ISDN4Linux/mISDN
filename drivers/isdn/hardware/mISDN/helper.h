/* $Id: helper.h,v 0.6 2001/03/03 08:07:29 kkeil Exp $
 *
 *   Basic declarations, defines and prototypes
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */
#include <linux/kernel.h>
#include <linux/skbuff.h>
#ifdef MEMDBG
#include "memdbg.h"
#endif

#define int_error() \
        printk(KERN_ERR "hisax: INTERNAL ERROR in %s:%d\n", \
                       __FILE__, __LINE__)
                       
#define int_errtxt(fmt, arg...) \
        printk(KERN_ERR "hisax: INTERNAL ERROR in %s:%d " fmt "\n", \
                       __FILE__, __LINE__, ## arg)
                       
#define APPEND_TO_LIST(item,base) \
	item->prev = base; \
	while (item->prev && item->prev->next) \
		item->prev = item->prev->next; \
	if (base) \
		item->prev->next = item; \
	else \
		base = item
	                                                        
#define REMOVE_FROM_LIST(item) \
	if (item->prev) \
		item->prev->next = item->next; \
	if (item->next) \
		item->next->prev = item->prev

#define REMOVE_FROM_LISTBASE(item,base) \
	REMOVE_FROM_LIST(item); \
	if (item == base) \
		base = item->next

extern int discard_queue(struct sk_buff_head *);
extern int get_up_layer(int);
extern int get_down_layer(int);
extern int layermask2layer(int);

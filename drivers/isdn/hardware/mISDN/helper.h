/* $Id: helper.h,v 0.1 2001/02/11 22:46:19 kkeil Exp $
 *
 *   Basic declarations, defines and prototypes
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */
#include <linux/skbuff.h>
      
extern int discard_queue(struct sk_buff_head *);

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

/* $Id: helper.h,v 0.12 2001/11/02 23:41:26 kkeil Exp $
 *
 *   Basic declarations, defines and prototypes
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */
#ifndef _HISAX_HELPER_H
#define	_HISAX_HELPER_H
#include <linux/kernel.h>
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
	if (item->prev || item->next) \
		int_errtxt("APPEND not clean %p<-%p->%p", \
			item->prev, item, item->next); \
	item->next = NULL; \
	item->prev = base; \
	while (item->prev && item->prev->next) \
		item->prev = item->prev->next; \
	if (item->prev == item) { \
		int_errtxt("APPEND DUP %p", item); \
	} else \
		if (base) { \
			item->prev->next = item; \
		} else \
			base = item

#define INSERT_INTO_LIST(newi,nexti,base) \
	newi->next = nexti; \
	newi->prev = nexti->prev; \
	if (newi->prev) \
		newi->prev->next = newi; \
	nexti->prev = newi; \
	if (base == nexti) \
		base = newi

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
extern struct sk_buff *alloc_uplink_skb(size_t);
extern int get_lowlayer(int);
extern int get_up_layer(int);
extern int get_down_layer(int);
extern int layermask2layer(int);

extern __inline__ void hisax_newhead(u_int prim, int dinfo, struct sk_buff *skb)
{
	hisax_head_t *hh = (hisax_head_t *)skb->data;

	hh->prim = prim;
	hh->dinfo = dinfo;
}

extern __inline__ int if_newhead(hisaxif_t *i, u_int prim, int dinfo,
	struct sk_buff *skb)
{
	if (!i->func || !skb)
		return(-ENXIO);
	hisax_newhead(prim, dinfo, skb);
	return(i->func(i, skb));
}

extern __inline__ void hisax_addhead(u_int prim, int dinfo, struct sk_buff *skb)
{
	hisax_head_t *hh = (hisax_head_t *)skb_push(skb, HISAX_HEAD_SIZE);

	hh->prim = prim;
	hh->dinfo = dinfo;
}


extern __inline__ int if_addhead(hisaxif_t *i, u_int prim, int dinfo,
	struct sk_buff *skb)
{
	if (!i->func || !skb)
		return(-ENXIO);
	hisax_addhead(prim, dinfo, skb);
	return(i->func(i, skb));
}


extern __inline__ struct sk_buff *create_link_skb(u_int prim, int dinfo,
	int len, void *arg, int reserve)
{
	struct sk_buff	*skb;

	if (!(skb = alloc_skb(len + HISAX_HEAD_SIZE + reserve, GFP_ATOMIC))) {
		printk(KERN_WARNING __FUNCTION__": no skb size %d+%d+%d\n",
			len, HISAX_HEAD_SIZE, reserve);
		return(NULL);
	} else
		skb_reserve(skb, reserve + HISAX_HEAD_SIZE);
	if (len)
		memcpy(skb_put(skb, len), arg, len);
	hisax_addhead(prim, dinfo, skb);
	return(skb);
}

extern __inline__ int if_link(hisaxif_t *i, u_int prim, int dinfo, int len,
	void *arg, int reserve)
{
	struct sk_buff	*skb;
	int		err;

	if (!(skb = create_link_skb(prim, dinfo, len, arg, reserve)))
		return(-ENOMEM);
	if (!i)
		err = -ENXIO;
	else
		err = i->func(i, skb);
	if (err)
		kfree_skb(skb);
	return(err);
}

#endif /* _HISAX_HELPER_H */

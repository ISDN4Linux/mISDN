/* $Id: helper.h,v 1.13 2004/06/17 12:31:12 keil Exp $
 *
 *   Basic declarations, defines and prototypes
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */
#ifndef _mISDN_HELPER_H
#define	_mISDN_HELPER_H
#include <linux/kernel.h>
#ifdef MISDN_MEMDEBUG
#include "memdbg.h"
#endif

#define int_error() \
        printk(KERN_ERR "mISDN: INTERNAL ERROR in %s:%d\n", \
                       __FILE__, __LINE__)
                       
#define int_errtxt(fmt, arg...) \
        printk(KERN_ERR "mISDN: INTERNAL ERROR in %s:%d " fmt "\n", \
                       __FILE__, __LINE__, ## arg)
                       
static inline int
discard_queue(struct sk_buff_head *q)
{
	struct sk_buff *skb;
	int ret=0;

	while ((skb = skb_dequeue(q))) {
		dev_kfree_skb(skb);
		ret++;
	}
	return(ret);
}

#ifdef MISDN_MEMDEBUG
#define alloc_stack_skb(s, r)	__mid_alloc_stack_skb(s, r, __FILE__, __LINE__)
static inline struct sk_buff *
__mid_alloc_stack_skb(size_t size, size_t reserve, char *fn, int line)
{
	struct sk_buff *skb;

	if (!(skb = __mid_alloc_skb(size + reserve, GFP_ATOMIC, fn, line)))
#else
static inline struct sk_buff *
alloc_stack_skb(size_t size, size_t reserve)
{
	struct sk_buff *skb;

	if (!(skb = alloc_skb(size + reserve, GFP_ATOMIC)))
#endif
		printk(KERN_WARNING "%s(%d,%d): no skb size\n", __FUNCTION__,
			size, reserve);
	else
		skb_reserve(skb, reserve);
	return(skb);
}

extern void	mISDN_set_dchannel_pid(mISDN_pid_t *, int, int);
extern int	mISDN_get_lowlayer(int);
extern int	mISDN_get_up_layer(int);
extern int	mISDN_get_down_layer(int);
extern int	mISDN_layermask2layer(int);
extern int	mISDN_get_protocol(mISDNstack_t *, int);
extern int	mISDN_HasProtocol(mISDNobject_t *, u_int);
extern int	mISDN_SetHandledPID(mISDNobject_t *, mISDN_pid_t *);
extern void	mISDN_RemoveUsedPID(mISDN_pid_t *, mISDN_pid_t *);
extern void	mISDN_init_instance(mISDNinstance_t *, mISDNobject_t *, void *);

static inline int
count_list_member(struct list_head *head)
{
	int			cnt = 0;
	struct list_head	*m;

	list_for_each(m, head)
		cnt++;
	return(cnt);
}

static inline int
mISDN_HasProtocolP(mISDNobject_t *obj, int *PP)
{
	if (!PP) {
		int_error();
		return(-EINVAL);
	}
	return(mISDN_HasProtocol(obj, *PP));
}

static inline void
mISDN_sethead(u_int prim, int dinfo, struct sk_buff *skb)
{
	mISDN_head_t *hh = mISDN_HEAD_P(skb);

	hh->prim = prim;
	hh->dinfo = dinfo;
}

static inline int
if_newhead(mISDNif_t *i, u_int prim, int dinfo, struct sk_buff *skb)
{
	if (!i->func || !skb)
		return(-ENXIO);
	mISDN_sethead(prim, dinfo, skb);
	return(i->func(i, skb));
}

#ifdef MISDN_MEMDEBUG
#define create_link_skb(p, d, l, a, r)	__mid_create_link_skb(p, d, l, a, r, __FILE__, __LINE__)
static inline struct sk_buff *
__mid_create_link_skb(u_int prim, int dinfo, int len, void *arg, int reserve, char *fn, int line)
{
	struct sk_buff	*skb;

	if (!(skb = __mid_alloc_skb(len + reserve, GFP_ATOMIC, fn, line))) {
#else
static inline struct sk_buff *
create_link_skb(u_int prim, int dinfo, int len, void *arg, int reserve)
{
	struct sk_buff	*skb;

	if (!(skb = alloc_skb(len + reserve, GFP_ATOMIC))) {
#endif
		printk(KERN_WARNING "%s: no skb size %d+%d\n",
			__FUNCTION__, len, reserve);
		return(NULL);
	} else
		skb_reserve(skb, reserve);
	if (len)
		memcpy(skb_put(skb, len), arg, len);
	mISDN_sethead(prim, dinfo, skb);
	return(skb);
}

#ifdef MISDN_MEMDEBUG
#define if_link(i, p, d, l, a, r)	__mid_if_link(i, p, d, l, a, r, __FILE__, __LINE__)
static inline int
__mid_if_link(mISDNif_t *i, u_int prim, int dinfo, int len, void *arg, int reserve, char *fn, int line)
{
	struct sk_buff	*skb;
	int		err;

	if (!(skb = __mid_create_link_skb(prim, dinfo, len, arg, reserve, fn, line)))
#else
static inline int
if_link(mISDNif_t *i, u_int prim, int dinfo, int len, void *arg, int reserve)
{
	struct sk_buff	*skb;
	int		err;

	if (!(skb = create_link_skb(prim, dinfo, len, arg, reserve)))
#endif
		return(-ENOMEM);
	if (!i)
		err = -ENXIO;
	else
		err = i->func(i, skb);
	if (err)
		kfree_skb(skb);
	return(err);
}

/* L3 data struct helper functions */

extern	signed int	mISDN_l3_ie2pos(u_char);
extern	unsigned char	mISDN_l3_pos2ie(int);
extern	void		mISDN_initQ931_info(Q931_info_t *);
#ifdef MISDN_MEMDEBUG
#define mISDN_alloc_l3msg(a, b)	__mid_alloc_l3msg(a, b, __FILE__, __LINE__)
extern	struct sk_buff 	*__mid_alloc_l3msg(int, u_char, char *, int);
#else
extern	struct sk_buff 	*mISDN_alloc_l3msg(int, u_char);
#endif
extern	void		mISDN_AddvarIE(struct sk_buff *, u_char *);
extern	void		mISDN_AddIE(struct sk_buff *, u_char, u_char *);
extern	void		mISDN_LogL3Msg(struct sk_buff *);

/* manager default handler helper macros */

#define PRIM_NOT_HANDLED(p)	case p: break

#define MGR_HASPROTOCOL_HANDLER(p,a,o)	\
	if ((MGR_HASPROTOCOL | REQUEST) == p) {\
		if (a) {\
			int prot = *((int *)a);\
			return(mISDN_HasProtocol(o, prot));\
		} else \
			return(-EINVAL);\
	}

#endif /* _mISDN_HELPER_H */

/* $Id: helper.h,v 1.14 2005/03/09 03:09:06 keil Exp $
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

/* shortcut to report errors locations, sometime also used for debugging !FIXME! */
#define int_error() \
        printk(KERN_ERR "mISDN: INTERNAL ERROR in %s:%d\n", \
                       __FILE__, __LINE__)

/* shortcut to report errors locations with an additional message text */
#define int_errtxt(fmt, arg...) \
        printk(KERN_ERR "mISDN: INTERNAL ERROR in %s:%d " fmt "\n", \
                       __FILE__, __LINE__, ## arg)
                       
/* cleanup SKB queues, return count of dropped packets */
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

/* allocate a SKB for DATA packets in the mISDN stack with enough headroom
 * the MEMDEBUG version is for debugging memory leaks in the mISDN stack
 */
 
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

/* 
 * mISDN_set_dchannel_pid(mISDN_pid_t *pid, int protocol, int layermask)
 *
 * set default values for the D-channel protocol ID struct
 *
 * layermask - bitmask which layers should be set (default 0,1,2,3,4)
 * protocol  - bitmask for special L2/L3 option (from protocol module parameter of L0 modules)
 */
extern void	mISDN_set_dchannel_pid(mISDN_pid_t *, int, int);

/*
 * int mISDN_get_lowlayer(int layermask)
 *
 * get the lowest layer number of a layermask
 * e.g layermask=0x0c returns 2
 */ 
extern int	mISDN_get_lowlayer(int);

/*
 * int mISDN_get_up_layer(int layermask)
 *
 * get the next higher layer number, which is not part of the given layermask
 */
extern int	mISDN_get_up_layer(int);

/*
 * int mISDN_get_down_layer(int layermask)
 *
 * get the next lower layer number, which is not part of the given layermask
 */
extern int	mISDN_get_down_layer(int);

/*
 * int mISDN_layermask2layer(int layermask)
 *
 * translate bit position in layermask into the layernumber
 * only valid if only one bit in the mask was set
 */
extern int	mISDN_layermask2layer(int);

/*
 * int mISDN_get_protocol(mISDNstack_t *st, int layer)
 *
 * get the protocol value of layer <layer> in stack <st>
 */
extern int	mISDN_get_protocol(mISDNstack_t *, int);

/*
 * int mISDN_HasProtocol(mISDNobject_t *obj, u_int protocol)
 *
 * test if given object can handle protocol <protocol>
 *
 * return 0 if yes
 *
 */
extern int	mISDN_HasProtocol(mISDNobject_t *, u_int);

/*
 * int mISDN_SetHandledPID(mISDNobject_t *obj, mISDN_pid_t *pid)
 *
 * returns the layermask of the supported protocols of object <obj>
 * from the protocol ID struct <pid>
 */
extern int	mISDN_SetHandledPID(mISDNobject_t *, mISDN_pid_t *);

/*
 * mISDN_RemoveUsedPID(mISDN_pid_t *pid, mISDN_pid_t *used)
 *
 * remove the protocol values from <pid> struct which are also in the
 * <used> struct
 */
extern void	mISDN_RemoveUsedPID(mISDN_pid_t *, mISDN_pid_t *);

/*
 * mISDN_init_instance(mISDNinstance_t *inst, mISDNobject_t *obj, void *data)
 *
 * initialisize the mISDNinstance_t struct <inst>
 */ 
extern void	mISDN_init_instance(mISDNinstance_t *, mISDNobject_t *, void *);

/* returns the member count of a list */
static inline int
count_list_member(struct list_head *head)
{
	int			cnt = 0;
	struct list_head	*m;

	list_for_each(m, head)
		cnt++;
	return(cnt);
}

/* same as mISDN_HasProtocol, but for a pointer */
static inline int
mISDN_HasProtocolP(mISDNobject_t *obj, int *PP)
{
	if (!PP) {
		int_error();
		return(-EINVAL);
	}
	return(mISDN_HasProtocol(obj, *PP));
}

/* set primitiv and dinfo field of a internal (SKB) mISDN message */
static inline void
mISDN_sethead(u_int prim, int dinfo, struct sk_buff *skb)
{
	mISDN_head_t *hh = mISDN_HEAD_P(skb);

	hh->prim = prim;
	hh->dinfo = dinfo;
}

/* send the skb through this interface with new header values */
static inline int
if_newhead(mISDNif_t *i, u_int prim, int dinfo, struct sk_buff *skb)
{
	if (!i->func || !skb)
		return(-ENXIO);
	mISDN_sethead(prim, dinfo, skb);
	return(i->func(i, skb));
}

/* allocate a mISDN message SKB with enough headroom and set the header fields
 * the MEMDEBUG version is for debugging memory leaks in the mISDN stack
 */
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

/* allocate a SKB for a mISDN message with enough headroom
 * fill mesage data into this SKB and send it trough the interface
 * the MEMDEBUG version is for debugging memory leaks in the mISDN stack
 */
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

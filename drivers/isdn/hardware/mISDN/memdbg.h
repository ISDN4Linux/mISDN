#ifndef MEMDBG_H
#define MEMDBG_H

#ifdef MISDN_MEMDEBUG
#include <linux/vmalloc.h>
#include <linux/slab.h>

#undef kmalloc
#undef kfree
#undef vmalloc
#undef vfree
#undef alloc_skb
#undef dev_alloc_skb
#undef skb_clone
#undef skb_copy
#undef skb_realloc_headroom

#define kmalloc(a, b)			__mid_kmalloc(a, b, __FILE__, __LINE__)
#define kfree(a)			__mid_kfree(a)
#define vmalloc(s)			__mid_vmalloc(s, __FILE__, __LINE__)
#define vfree(p)			__mid_vfree(p)
#define alloc_skb(a, b)			__mid_alloc_skb(a, b, __FILE__, __LINE__)
#define dev_alloc_skb(a)		__mid_dev_alloc_skb(a, __FILE__, __LINE__)
#define skb_clone(a, b)			__mid_skb_clone(a, b, __FILE__, __LINE__)
#define skb_copy(a, b)			__mid_skb_copy(a, b, __FILE__, __LINE__)
#define skb_realloc_headroom(a, b)	__mid_skb_realloc_headroom(a, b, __FILE__, __LINE__)

extern void		*__mid_kmalloc(size_t, int, char *, int);
extern void		__mid_kfree(const void *);
extern void		*__mid_vmalloc(size_t, char *, int);
extern void		__mid_vfree(const void *);
extern void		__mid_cleanup(void);
extern int		__mid_init(void);
extern struct sk_buff	*__mid_alloc_skb(unsigned int,int, char *, int);
extern struct sk_buff	*__mid_dev_alloc_skb(unsigned int,char *, int);
extern struct sk_buff	*__mid_skb_clone(struct sk_buff *, int, char *, int);
extern struct sk_buff	*__mid_skb_copy(struct sk_buff *, int, char *, int);
extern struct sk_buff	*__mid_skb_realloc_headroom(struct sk_buff *, unsigned int, char *, int);
#endif

#endif

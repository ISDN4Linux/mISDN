/* $Id: channel.h,v 1.3 2006/05/05 10:04:52 mbachem Exp $
 *
 *   Basic declarations for a mISDN HW channel
 *
 *  Author       (c) Karsten Keil <kkeil@suse.de>
 *
 * This file is released under the GPLv2
 *
 */

#ifndef MISDN_CHANNEL_H
#define MISDN_CHANNEL_H
#include <linux/mISDNif.h>
#include <linux/timer.h>
#include <linux/skbuff.h>
#include "helper.h"
#ifdef MISDN_MEMDEBUG
#include "memdbg.h"
#endif
#ifdef CONFIG_MISDN_NETDEV
#include "core.h"
#endif

#define MAX_DFRAME_LEN_L1	300
#define MAX_MON_FRAME		32
#define MAX_LOG_SPACE		2048
#define MISDN_COPY_SIZE		32

/* channel->Flags bit field */
#define FLG_TX_BUSY		0	// tx_buf in use
#define FLG_TX_NEXT		1	// next_skb in use
#define FLG_L1_BUSY		2	// L1 is permanent busy
#define FLG_USED		5	// channel is in use		
#define FLG_ACTIVE		6	// channel is activated
#define FLG_BUSY_TIMER		7
/* channel type */
#define FLG_DCHANNEL		8	// channel is D-channel
#define	FLG_BCHANNEL		9	// channel is B-channel
#define FLG_ECHANNEL		10	// channel is E-channel
#define FLG_TRANSPARENT		12	// channel use transparent data
#define FLG_HDLC		13	// channel use hdlc data
#define FLG_L2DATA		14	// channel use L2 DATA primitivs
#define FLG_ORIGIN		15	// channel is on origin site 
/* channel specific stuff */
/* arcofi specific */
#define FLG_ARCOFI_TIMER	16
#define FLG_ARCOFI_ERROR	17
/* isar specific */
#define FLG_INITIALIZED		16
#define FLG_DLEETX		17
#define FLG_LASTDLE		18
#define FLG_FIRST		19
#define FLG_LASTDATA		20
#define FLG_NMD_DATA		21
#define FLG_FTI_RUN		22
#define FLG_LL_OK		23
#define FLG_LL_CONN		24
#define FLG_DTMFSEND		25


#define MSK_INIT_DCHANNEL	((1<<FLG_DCHANNEL)|(1<<FLG_HDLC))
#define MSK_INIT_BCHANNEL	(1<<FLG_BCHANNEL)
#define MSK_INIT_ECHANNEL	(1<<FLG_ECHANNEL)


typedef struct _channel_t {
	mISDNinstance_t		inst;
	int			channel;
	/* basic properties */
	u_long			Flags;
	u_int			type;
	u_int			state;
	/* HW access */
	u_char			(*read_reg) (void *, u_char);
	void			(*write_reg) (void *, u_char, u_char);
	void			(*read_fifo) (void *, u_char *, int);
	void			(*write_fifo) (void *, u_char *, int);
	void			*hw;
	struct timer_list	timer;
	/* receive data */
	struct sk_buff		*rx_skb;
	int			maxlen;
	int			up_headerlen;
	/* send data */
	struct sk_buff		*next_skb;
	struct sk_buff		*tx_skb;
	int			tx_idx;
	/* debug */
	int			debug;
	char			*log;
	/* statistics */
	int			err_crc;
	int			err_tx;
	int			err_rx;
} channel_t;

extern int	mISDN_initchannel(channel_t *, ulong, int);
extern int	mISDN_freechannel(channel_t *);
extern int	mISDN_setpara(channel_t *, mISDN_stPara_t *);

static inline void
queue_ch_frame(channel_t *ch, u_int pr, int dinfo, struct sk_buff *skb)
{
	int	err;

	pr |= test_bit(FLG_L2DATA, &ch->Flags) ? DL_DATA : PH_DATA;
	if (!skb) {
		err = mISDN_queue_data(&ch->inst, FLG_MSG_UP, pr, dinfo, 0, NULL, ch->up_headerlen);
	} else {
#ifdef CONFIG_MISDN_NETDEV
		misdn_log_frame(ch->inst.st, skb->data, skb->len, FLG_MSG_UP);
#endif
		err = mISDN_queueup_newhead(&ch->inst, 0, pr, dinfo, skb);
	}
	if (unlikely(err)) {
		int_errtxt("err=%d", err);
		if (skb)
			dev_kfree_skb(skb);
	}
}

static inline int
channel_senddata(channel_t *ch, int di, struct sk_buff *skb)
{
	/* HW lock must be obtained */
	/* check oversize */
	if (skb->len <= 0) {
		printk(KERN_WARNING "%s: skb too small\n", __FUNCTION__);
		return(-EINVAL);
	}
	if (skb->len > ch->maxlen) {
		printk(KERN_WARNING "%s: skb too large(%d/%d)\n",
			__FUNCTION__, skb->len, ch->maxlen);
		return(-EINVAL);
	}
	/* check for pending next_skb */
	if (ch->next_skb) {
		printk(KERN_WARNING "%s: next_skb exist ERROR (skb->len=%d next_skb->len=%d)\n",
			__FUNCTION__, skb->len, ch->next_skb->len);
		return(-EBUSY);
	}
	if (test_and_set_bit(FLG_TX_BUSY, &ch->Flags)) {
		test_and_set_bit(FLG_TX_NEXT, &ch->Flags);
#ifdef CONFIG_MISDN_NETDEV
		misdn_log_frame(ch->inst.st, skb->data, skb->len, FLG_MSG_DOWN);
#endif
		ch->next_skb = skb;
		return(0);
	} else {
		/* write to fifo */
		ch->tx_skb = skb;
		ch->tx_idx = 0;
#ifdef CONFIG_MISDN_NETDEV
		misdn_log_frame(ch->inst.st, skb->data, skb->len, FLG_MSG_DOWN);
#endif
		queue_ch_frame(ch, CONFIRM, di, NULL);
		return(skb->len);
	}
}

#endif

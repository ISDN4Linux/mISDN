/* $Id: bchannel.h,v 1.9 2004/01/26 22:21:30 keil Exp $
 *
 *   Basic declarations, defines for Bchannel hardware
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#include <linux/mISDNif.h>
#ifdef HAS_WORKQUEUE
#include <linux/workqueue.h>
#else
#include <linux/tqueue.h>
#endif
#include <linux/smp.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <asm/io.h>
#include <linux/ioport.h>
#include <linux/skbuff.h>
#ifdef MISDN_MEMDEBUG
#include "memdbg.h"
#endif

#define MAX_BLOG_SPACE		256

#define BC_FLG_INIT	1
#define BC_FLG_ACTIV	2
#define BC_FLG_TX_BUSY	3
#define BC_FLG_NOFRAME	4
#define BC_FLG_HALF	5
#define BC_FLG_EMPTY	6
#define BC_FLG_ORIG	7
#define BC_FLG_DLEETX	8
#define BC_FLG_LASTDLE	9
#define BC_FLG_FIRST	10
#define BC_FLG_LASTDATA	11
#define BC_FLG_NMD_DATA	12
#define BC_FLG_FTI_RUN	13
#define BC_FLG_LL_OK	14
#define BC_FLG_LL_CONN	15
#define BC_FLG_TX_NEXT	16
#define BC_FLG_DTMFSEND	17

typedef struct _bchannel_t {
	int			channel;
	int			protocol;
	u_long			Flag;
	int			debug;
	mISDNstack_t		*st;
	mISDNinstance_t		inst;
	mISDNdevice_t		*dev;
	void			*hw;
	u_char			(*Read_Reg)(void *, int, u_char);
	void			(*Write_Reg)(void *, int, u_char, u_char);
	struct sk_buff		*next_skb;
	u_char			*tx_buf;
	int			tx_idx;
	int             	tx_len;
	u_char			*rx_buf;
	int			rx_idx;
	struct sk_buff_head	rqueue;	/* B-Channel receive Queue */
	u_char			*blog;
	u_char			*conmsg;
	struct timer_list	transbusy;
	struct work_struct	work;
	void			(*hw_bh) (struct _bchannel_t *);
	u_long			event;
	int			maxdatasize;
	int			up_headerlen;
	int			err_crc;
	int			err_tx;
	int			err_rdo;
	int			err_inv;
} bchannel_t;

extern int mISDN_init_bch(bchannel_t *);
extern int mISDN_free_bch(bchannel_t *);

static inline void
bch_set_para(bchannel_t *bch, mISDN_stPara_t *stp)
{
	if (stp) {
		bch->maxdatasize = stp->maxdatalen;
		bch->up_headerlen = stp->up_headerlen;
	} else {
		bch->maxdatasize = 0;
		bch->up_headerlen = 0;
	}
}

static inline void
bch_sched_event(bchannel_t *bch, int event)
{
	test_and_set_bit(event, &bch->event);
	schedule_work(&bch->work);
}

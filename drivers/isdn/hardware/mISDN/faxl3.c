/* $Id: faxl3.c,v 1.2 2004/06/17 12:31:12 keil Exp $
 *
 * Linux ISDN subsystem, Fax Layer 3
 *
 * Author	Karsten Keil (kkeil@suse.de)
 *
 * Copyright 2003 by Karsten Keil (kkeil@suse.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include "layer1.h"
#include "m_capi.h"
#include "helper.h"
#include "debug.h"

static int ttt=180;

typedef struct _faxl3 {
	struct list_head	list;
	spinlock_t		lock;
	u_long 			state;
	int			debug;
	mISDNinstance_t		inst;
	u16			options;
	u16			format;
	u8			stationID[24];
	u8			headline[64];
	u8			DIS[12];
	u8			CIS[24];  // only max 20 are used
	u8			NSF[12];
	u8			DTC[12];
	u8			DCS[12];
	u8			CIG[24];
	u8			NSC[12];
	u_int			peer_rate_mask;
	u_int			own_rate_mask;
	int			current_rate_idx;
	int			current_mod;
	int			current_rate;
	int			pending_mod;
	int			pending_rate;
	int			result;
	struct FsmInst		main;
	struct FsmTimer 	deltimer;
	struct FsmTimer 	timer1;
	struct FsmInst		mod;
	struct FsmTimer 	modtimer;
	struct sk_buff_head	downq;
	struct sk_buff_head	dataq;
	struct sk_buff_head	pageq;
	struct sk_buff_head	saveq;
	struct sk_buff		*data_skb;
	struct sk_buff		*page_skb;
	int			entity;
	int			next_id;
	int			maxdatalen;
	int			up_headerlen;
	int			down_headerlen;
	u32			ncci;
	// SFFHEADER
	int			pages;
	u32			offset_lpage;
	u32			offset_dend;
	int			current_page;
	// SFF page header
	u8			page_vres;
	u8			page_hres;
	u8			page_code;
	u8			page_rsv1;
	u16			page_llen;
	u16			page_plen;
	u32			page_oprv;
	u32			page_onxt;
	u8			lasttyp;
	int			lastlen;
	int			line_cnt;
	int			page_retry;
} faxl3_t;

#define	FAXL3_STATE_OUTGOING	1
#define FAXL3_STATE_SENT_TIS	2
#define FAXL3_STATE_CAPICONNECT	3
#define FAXL3_STATE_GOT_DIS	8
#define FAXL3_STATE_GOT_CIS	9
#define FAXL3_STATE_GOT_NSF	10
#define FAXL3_STATE_SFFHEADER	16
#define FAXL3_STATE_PAGEHEADER	17
#define FAXL3_STATE_NEWPAGE	18
#define FAXL3_STATE_LASTPAGE	19
#define FAXL3_STATE_CONTINUE	20
#define FAXL3_STATE_HAVEDATA	21
#define FAXL3_STATE_DATABUSY	24
#define FAXL3_STATE_DATALAST	25
#define FAXL3_STATE_DATAREADY	26

#define FAXL3_RESULT_NONE	0
#define FAXL3_RESULT_CFR	1
#define FAXL3_RESULT_FTT	2
#define FAXL3_RESULT_MCF	3
#define FAXL3_RESULT_RTP	4
#define FAXL3_RESULT_RTN	5

static char logbuf[2000];
static int debug = 0;
#define DEBUG_FAXL3_FUNC	0x0001
#define DEBUG_FAXL3_MGR		0x0010
#define DEBUG_FAXL3_CFG		0x0020
#define DEBUG_FAXL3_MSG		0x0100
#define DEBUG_FAXL3_SIG         0x0200
#define DEBUG_FAXL3_PAGEPREPARE	0x1000

static mISDNobject_t faxl3_obj;

static char *mISDN_faxl3_revision = "$Revision: 1.2 $";

static u_char FaxModulation[] = {24,48,72,74,96,98,122,146};
static u_char FaxModulationTrain[] = {24,48,72,73,96,97,121,145};
static int FaxModulationBaud[] = {2400,4800,7200,7200,9600,9600,12000,14400};

#define MAX_FAXMODULATION_INDEX	7
#define FAXMODULATION_MASK	0xff

#define FAXMODM_UNDEF		0x00
#define FAXMODM_V27		0x03
#define FAXMODM_V27_V29		0x17
#define FAXMODM_V27_V29_V33	0x17 //We don't have V.33 definition yet
#define FAXMODM_V27_V29_V33_V17	0xff

static u_int FaxModulationRates[16] = {
	FAXMODM_UNDEF,
	FAXMODM_UNDEF,
	FAXMODM_V27,
	FAXMODM_V27_V29,
	FAXMODM_UNDEF,
	FAXMODM_UNDEF,
	FAXMODM_UNDEF,
	FAXMODM_V27_V29_V33,
	FAXMODM_UNDEF,
	FAXMODM_UNDEF,
	FAXMODM_UNDEF,
	FAXMODM_V27_V29_V33_V17,
	FAXMODM_UNDEF,
	FAXMODM_UNDEF,
	FAXMODM_UNDEF,
	FAXMODM_UNDEF
};

static u8 FaxModulationRates_DCS[8] = {
	0x0,
	0x2,
	0x3,
	0xb,
	0x1,
	0x9,
	0xa,
	0x8
};

#define Dxx_TYPE_DIS	0
#define Dxx_TYPE_DTC	1
#define Dxx_TYPE_DCS	2

static void	l3m_debug(struct FsmInst *fi, char *fmt, ...);
static int	send_hdlc_data(faxl3_t *fl3, u8 adr, u8 hcf, u8 fcf, u8 *para, int len);
static int	sendL4frame(faxl3_t *fl3, int prim, int di, int len, void *arg, struct sk_buff *skb);
static int	send_capi_msg_ncpi(faxl3_t *fl3, int prim, u16 Info);
static int	prepare_page_data(faxl3_t *fl3);
static int	copy_page_data4retry(faxl3_t *fl3);

static
struct Fsm faxl3fsm = {NULL, 0, 0, NULL, NULL};

enum {
	ST_L3_IDLE,
	ST_L3_WAIT_RECVDIS,
	ST_L3_RECV_DIS,
	ST_L3_WAIT_SENDDCS,
	ST_L3_SEND_DCS,
	ST_L3_WAIT_SENDTRAIN, 
	ST_L3_SEND_TRAIN,
	ST_L3_WAIT_TRAINSTATE,
	ST_L3_RECV_TRAINSTATE,
	ST_L3_WAIT_SENDPAGE,
	ST_L3_SEND_PAGE,
	ST_L3_WAIT_PAGESTATE,
	ST_L3_RECV_PAGESTATE,
	ST_L3_WAIT_SENDEOP,
	ST_L3_SEND_EOP,
	ST_L3_WAIT_RECVMCF,
	ST_L3_RECV_MCF,
	ST_L3_WAIT_SENDDCN,
	ST_L3_SEND_DCN,
	ST_L3_CLEARING,
};

#define FAXL3_STATE_COUNT (ST_L3_CLEARING+1)

static char *strfaxl3State[] =
{
	"ST_L3_IDLE",
	"ST_L3_WAIT_RECVDIS",
	"ST_L3_RECV_DIS",
	"ST_L3_WAIT_SENDDCS",
	"ST_L3_SEND_DCS",
	"ST_L3_WAIT_SENDTRAIN", 
	"ST_L3_SEND_TRAIN",
	"ST_L3_WAIT_TRAINSTATE",
	"ST_L3_RECV_TRAINSTATE",
	"ST_L3_WAIT_SENDPAGE",
	"ST_L3_SEND_PAGE",
	"ST_L3_WAIT_PAGESTATE",
	"ST_L3_RECV_PAGESTATE",
	"ST_L3_WAIT_SENDEOP",
	"ST_L3_SEND_EOP",
	"ST_L3_WAIT_RECVMCF",
	"ST_L3_RECV_MCF",
	"ST_L3_WAIT_SENDDCN",
	"ST_L3_SEND_DCN",
	"ST_L3_CLEARING",
};

enum {
	EV_CALL_OUT,
	EV_MODEM_ACTIV,
	EV_MODEM_IDLE,
	EV_MODEM_ERROR,
	EV_DATA,
	EV_NEXT_DATA,
	EV_DELAYTIMER,
	EV_CLEARING,
};

#define FAXL3_EVENT_COUNT (EV_CLEARING + 1)

static char *strfaxl3Event[] =
{
	"EV_CALL_OUT",
	"EV_MODEM_ACTIV",
	"EV_MODEM_IDLE",
	"EV_MODEM_ERROR",
	"EV_DATA",
	"EV_NEXT_DATA",
	"EV_DELAYTIMER",
	"EV_CLEARING",
};

static
struct Fsm modfsm = {NULL, 0, 0, NULL, NULL};

enum {
	ST_MOD_NULL,
	ST_MOD_IDLE,
	ST_MOD_WAITCONNECT,
	ST_MOD_CONNECTED,
	ST_MOD_WAITDISCONNECT,
};

#define MOD_STATE_COUNT (ST_MOD_WAITDISCONNECT + 1)

static char *strmodState[] =
{
	"ST_MOD_NULL",
	"ST_MOD_IDLE",
	"ST_MOD_WAITCONNECT",
	"ST_MOD_CONNECTED",
	"ST_MOD_WAITDISCONNECT",
};

enum {
	EV_MOD_READY,
	EV_MOD_NEW,
	EV_MOD_CONNECT,
	EV_MOD_DISCONNECT,
	EV_MOD_NOCARRIER,
	EV_MOD_ERROR,
	EV_MOD_TIMEOUT,
};

#define MOD_EVENT_COUNT (EV_MOD_TIMEOUT + 1)

static char *strmodEvent[] =
{
	"EV_MOD_READY",
	"EV_MOD_NEW",
	"EV_MOD_CONNECT",
	"EV_MOD_DISCONNECT",
	"EV_MOD_NOCARRIER",
	"EV_MOD_ERROR",
	"EV_MOD_TIMEOUT",
};

static int
data_next_id(faxl3_t *fl3)
{
	u_long	flags;
	int	id;

	spin_lock_irqsave(&fl3->lock, flags);
	id = fl3->next_id++;
	if (id == 0x0fff)
		fl3->next_id = 1;
	spin_unlock_irqrestore(&fl3->lock, flags);
	id |= (fl3->entity << 16);
	return(id);
}

static void
print_hexdata(faxl3_t *fl3, char *head, int len, char *data)
{
	char	*t = logbuf;

	t += sprintf(logbuf, "%s", head);
	if (len > 650)
		len = 650;
	mISDN_QuickHex(t, data, len);
	printk(KERN_DEBUG "%s\n", logbuf);
}

static char *rate_1[16] = {
	"undef",
	"undef",
	"2400,4800 V.27ter",
	"9600,7200,4800,2400 V.27ter/V.29",
	"undef",
	"undef",
	"undef",
	"14400,12000,9600,7200,4800,2400 V.27ter/V.29/V.33",
	"undef",
	"undef",
	"undef",
	"14400,12000,9600,7200,4800,2400 V.27ter/V.29/V.33/V.17",
	"undef",
	"undef",
	"undef",
	"undef"
};

static char *rate_2[16] = {
	"2400(V.27ter)",
	"9600(V.29)",
	"4800(V.27ter)",
	"7200(V.29)",
	"14400(V.33)",
	"undef",
	"12000(V.33)",
	"undef",
	"14400(V.17)",
	"9600(V.17)",
	"12000(V.17)",
	"7200(V.17)",
	"undef",
	"undef",
	"undef",
	"undef"
};

static char *pwidth_1[4] = {
	"1728/A4",
	"1728/A4 2048/B4",
	"1728/A4 2048/B4 2432/A3",
	"undef"
};

static char *pwidth_2[4] = {
	"1728 A4",
	"2048 B4",
	"2432 A3",
	"undef"
};

static char *plength[4] = {
	"297/A4",
	"364/B4",
	"unlimited",
	"undef"
};

static char *minrowtime_1[8] = {
	"20ms",
	"5ms",
	"10ms",
	"20ms*",
	"40ms",
	"40ms*",
	"10ms*",
	"0ms"
};

static char *minrowtime_2[8] = {
	"20ms",
	"5ms",
	"10ms",
	" ",
	"40ms",
	" ",
	" ",
	"0ms"
};

static void
print_Dxx(faxl3_t *fl3, int typ)
{
	char	*ts;
	u8	*p, v1,v2,v3;

	switch (typ) {
		case Dxx_TYPE_DIS:
			ts = "DIS";
			p = fl3->DIS;
			break;
		case Dxx_TYPE_DTC:
			ts = "DTC";
			p = fl3->DTC;
			break;
		case Dxx_TYPE_DCS:
			ts = "DCS";
			p = fl3->DCS;
			break;
		default:
			int_error();
			return;
	}
	/* OK byte one is only for group 1/2 compatibility */
	printk(KERN_DEBUG "%s: byte1 %02X\n", ts, *p);
	v1 = (p[1] >> 2) & 0xf;
	printk(KERN_DEBUG "%s:%s%s %s%s%s\n", ts,
		(test_bit(8, (u_long *)p) && (typ != Dxx_TYPE_DCS)) ? " SendG3" : "",
		(test_bit(9, (u_long *)p)) ? " RecvG3" : "",
		(typ == Dxx_TYPE_DCS) ? rate_2[v1] : rate_1[v1],
		(test_bit(14, (u_long *)p)) ? " 7,7Row/mm" : "",
		(test_bit(15, (u_long *)p)) ? " 2-Dim" : "");

	v1 = p[2] & 3;
	v2 = (p[2] >> 2) & 3;
	v3 = (p[2] >> 4) & 7;
	printk(KERN_DEBUG "%s: width(%s) plength(%s) MinRow(%s)\n", ts,
		(typ == Dxx_TYPE_DCS) ? pwidth_2[v1] : pwidth_1[v1],
		plength[v2],
		(typ == Dxx_TYPE_DCS) ? minrowtime_2[v3] : minrowtime_1[v3]);

	if (!test_bit(23, (u_long *)p))
		return;

	if (typ == Dxx_TYPE_DCS)
		printk(KERN_DEBUG "%s:%s%s%s BS(%s)%s\n", ts,
			(test_bit(24, (u_long *)p)) ? " 2400" : "",
			(test_bit(25, (u_long *)p)) ? " uncompressed" : "",
			(test_bit(26, (u_long *)p)) ? " ECM" : "",
			(test_bit(27, (u_long *)p)) ? "64" : "256",
			(test_bit(30, (u_long *)p)) ? " MMR" : "");
	else
		printk(KERN_DEBUG "%s:%s%s%s%s\n", ts,
			(test_bit(24, (u_long *)p)) ? " 2400" : "",
			(test_bit(25, (u_long *)p)) ? " uncompressed" : "",
			(test_bit(26, (u_long *)p)) ? " ECM" : "",
			(test_bit(30, (u_long *)p)) ? " MMR" : "");
	if (!test_bit(31, (u_long *)p))
		return;
	/* byte is reseved */
	if (!test_bit(39, (u_long *)p))
		return;
// TODO
	if (!test_bit(47, (u_long *)p))
		return;
// TODO
	if (!test_bit(55, (u_long *)p))
		return;
// TODO
	if (!test_bit(63, (u_long *)p))
		return;
// TODO
}

static u8
calc_dtcrate(faxl3_t *fl3)
{
	if ((FAXMODM_V27_V29_V33_V17 & fl3->own_rate_mask) == FAXMODM_V27_V29_V33_V17)
		return(11);
	if ((FAXMODM_V27_V29_V33 & fl3->own_rate_mask) == FAXMODM_V27_V29_V33)
		return(7);
	if ((FAXMODM_V27_V29 & fl3->own_rate_mask) == FAXMODM_V27_V29)
		return(3);
	if ((FAXMODM_V27& fl3->own_rate_mask) == FAXMODM_V27)
		return(2);
	return(0);
}

static u8
calc_dcsrate(faxl3_t *fl3)
{
	if ((fl3->current_rate_idx > MAX_FAXMODULATION_INDEX) ||
		(fl3->current_rate_idx < 0)) {
		int_errtxt("current_rate_idx(%d)", fl3->current_rate_idx);
		return(0xf);
	} 
	return(FaxModulationRates_DCS[fl3->current_rate_idx]);
}

static void
fill_Dxx(faxl3_t *fl3, int typ)
{
	u8	*p, v1,v2,v3;

	switch (typ) {
		case Dxx_TYPE_DIS:
			p = fl3->DIS;
			break;
		case Dxx_TYPE_DTC:
			p = fl3->DTC;
			break;
		case Dxx_TYPE_DCS:
			p = fl3->DCS;
			break;
		default:
			int_error();
			return;
	}
	memset(p, 0, 12); // clear all bits
	/* OK byte one is only for group 1/2 compatibility, skipped */
	if (typ == Dxx_TYPE_DCS)
		v1 = calc_dcsrate(fl3);
	else
		v1 = calc_dtcrate(fl3);
	p[1] = v1 << 2;
	if (typ == Dxx_TYPE_DCS)
		test_and_set_bit(9, (u_long *)p);
	else
		test_and_set_bit(8, (u_long *)p);
	if (fl3->options & 1)
		test_and_set_bit(14, (u_long *)p);
// TODO: calc
	test_and_set_bit(14, (u_long *)p);
	v1 = 0; // A4, TODO: calc 
	v2 = 2; // unlimited, TODO: calc
	v3 = 7; // 0 ms, TODO: calc
	p[2] = v1 | (v2 << 2) | (v3 << 4);
	test_and_set_bit(23, (u_long *)p); // next byte exist
	p[3] = 0; // TODO: calc
}

static int
send_Dxx(faxl3_t *fl3, int typ, int last)
{
	u8	*p, fcf, hdlc_cf = last ? 0x13 : 3;
	int 	len;

	switch (typ) {
		case Dxx_TYPE_DIS:
			p = fl3->DIS;
			fcf = 80;
			break;
		case Dxx_TYPE_DTC:
			p = fl3->DTC;
			fcf = 0x81;
			break;
		case Dxx_TYPE_DCS:
			p = fl3->DCS;
			fcf = 0x83;
			break;
		default:
			int_error();
			return(-EINVAL);
	}
	if (!test_bit(23, (u_long *)p))
		len = 3;
	else if (!test_bit(31, (u_long *)p))
		len = 4;
	else if (!test_bit(39, (u_long *)p))
		len = 5;
	else if (!test_bit(47, (u_long *)p))
		len = 6;
	else if (!test_bit(55, (u_long *)p))
		len = 7;
	else if (!test_bit(63, (u_long *)p))
		len = 8;
	else
		len = 9;
	return(send_hdlc_data(fl3, 0xff, hdlc_cf, fcf, p, len));
}

static int
send_char20(faxl3_t *fl3, u8 *p, int fcf, int last)
{
	u8	buf[20], *s, hdlc_cf = last ? 0x13 : 3;
	int	len, i;

	memset(buf, ' ', 20);
	len = strlen(p);
	if (len > 20)
		len = 20;
	s = buf;
	for (i=len; i>0; i--)
		*s++ = p[i-1];
	return(send_hdlc_data(fl3, 0xff, hdlc_cf, fcf, buf, 20));
}

static int
is_valid_rate_idx(faxl3_t *fl3, int ridx)
{
	if ((ridx > MAX_FAXMODULATION_INDEX) || (ridx < 0))
		return(0);
	if (((1<<ridx) & fl3->own_rate_mask & fl3->peer_rate_mask) == 0)
		return(0);
	else
		return(1);
}

static int
fallback_rate(faxl3_t *fl3)
{
	fl3->current_rate_idx--;
	while ((fl3->current_rate_idx >= 0) &&
		!is_valid_rate_idx(fl3, fl3->current_rate_idx)) {
		fl3->current_rate_idx--;
	}
	return(is_valid_rate_idx(fl3, fl3->current_rate_idx));
}

static int
calc_max_rate(faxl3_t *fl3)
{
	int i;

	for (i = MAX_FAXMODULATION_INDEX; i >= 0; i--) {
		if (is_valid_rate_idx(fl3, i))
			return(i);
	}
	return(-1);
}

static int
send_data_down(faxl3_t *fl3, struct sk_buff *skb) {
	int		ret = 0;
	mISDNif_t	*down = &fl3->inst.down;

	if (test_and_set_bit(FAXL3_STATE_DATABUSY, &fl3->state)) {
		skb_queue_tail(&fl3->downq, skb);
	} else {
		mISDN_sethead(PH_DATA_REQ, data_next_id(fl3), skb);
		ret = down->func(down, skb);
		if (ret) {
			int_errtxt("down: error(%d)", ret);
		}
	}
	return(ret);
}

static int
send_hdlc_data(faxl3_t *fl3, u8 adr, u8 hcf, u8 fcf, u8 *para, int len)
{
	struct sk_buff	*skb;
	u_char		*p;
	int		ret;

	if (!(skb = alloc_stack_skb(3 + len, 1)))
		return(-ENOMEM);
	p = skb_put(skb, 3);
	*p++ = adr;
	*p++ = hcf;
	*p++ = fcf;
	if (len)
		memcpy(skb_put(skb, len), para, len);
	ret = send_data_down(fl3, skb);
	if (ret)
		dev_kfree_skb(skb);
	return(ret);
}

static void
mod_init(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t *fl3 = fi->userdata;
	int err;

	err = if_link(&fl3->inst.down, PH_ACTIVATE | REQUEST, 0, 0, NULL, 0);
	if (err) {
		int_error();
		return;
	}
}

static void
set_new_modulation(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t *fl3 = fi->userdata;
	int err;

	if ((fl3->pending_mod < 0) || (fl3->pending_rate <0)) {
		if (event == EV_MOD_READY)
			mISDN_FsmChangeState(fi, ST_MOD_IDLE);
		else
			int_error();
		return;
	}
	mISDN_FsmChangeState(fi, ST_MOD_WAITCONNECT);
	err = if_link(&fl3->inst.down, PH_CONTROL | REQUEST, fl3->pending_mod, sizeof(int), &fl3->pending_rate, 0);
	if (err) {
		int_error();
		return;
	}
}

static void
mod_activ(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t *fl3 = fi->userdata;

	mISDN_FsmChangeState(fi, ST_MOD_CONNECTED);
	fl3->current_mod = fl3->pending_mod;
	fl3->pending_mod = -1;
	fl3->current_rate = fl3->pending_rate;
	fl3->pending_rate = -1;
	mISDN_FsmEvent(&fl3->main, EV_MODEM_ACTIV, NULL);	
}

static void
mod_disconnect(struct FsmInst *fi, int event, void *arg)
{
	mISDN_FsmChangeState(fi, ST_MOD_WAITDISCONNECT);
}

static void
mod_error(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t *fl3 = fi->userdata;

	mISDN_FsmChangeState(fi, ST_MOD_IDLE);
	mISDN_FsmEvent(&fl3->main, EV_MODEM_ERROR, NULL);	
}

static void
mod_nocarrier(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t *fl3 = fi->userdata;

	mISDN_FsmChangeState(fi, ST_MOD_IDLE);
	mISDN_FsmEvent(&fl3->main, EV_MODEM_IDLE, NULL);	
}

static struct FsmNode ModFnList[] =
{
	{ST_MOD_NULL,		EV_MOD_NEW,		mod_init},
	{ST_MOD_NULL,		EV_MOD_READY,		set_new_modulation},
	{ST_MOD_IDLE,		EV_MOD_READY,		set_new_modulation},
	{ST_MOD_IDLE,		EV_MOD_NEW,		set_new_modulation},
	{ST_MOD_WAITCONNECT,	EV_MOD_CONNECT,		mod_activ},
	{ST_MOD_WAITCONNECT,	EV_MOD_ERROR,		mod_error},
	{ST_MOD_CONNECTED,	EV_MOD_NOCARRIER,	mod_nocarrier},
	{ST_MOD_CONNECTED,	EV_MOD_DISCONNECT,	mod_disconnect},
	{ST_MOD_WAITDISCONNECT,	EV_MOD_NOCARRIER,	mod_nocarrier},
};

#define MOD_FN_COUNT (sizeof(ModFnList)/sizeof(struct FsmNode))


static void
l3m_callout(struct FsmInst *fi, int event, void *arg)
{
	mISDN_FsmChangeState(fi, ST_L3_WAIT_RECVDIS);
}

static void
l3m_activ_dis(struct FsmInst *fi, int event, void *arg)
{
	mISDN_FsmChangeState(fi, ST_L3_RECV_DIS);
}

static int
get_Dxx(u8 *dst, u8 *src, int len) {
	if (len < 3) {
		int_errtxt("Dxx too short %d", len);
		return(-1);
	}
	if (len > 12)
		len = 12; // normally max 9 bytes
	memcpy(dst, src, len);
	return(0);
}

static int
get_CHAR20(u8 *dst, u8 *src, int len) {
	int i;

	if (len <= 0) {
		int_errtxt("string too short %d", len);
		return(-1);
	}
	if (len > 20) {
		int_errtxt("string too big (%d) rest ignored", len);
		len = 20;
	}
	for (i = 20; i > len; i--)
		dst[20-i] = ' ';
	for (; i > 0; i--)
		dst[20-i] = src[i-1];
	dst[20] = 0;
	return(0);
}

static int
get_Nxx(u8 *dst, u8 *src, int len) {
	if (len < 2) {
		int_errtxt("Nxx too short %d", len);
		return(-1);
	}
	if (len > 12) {
		int_errtxt("Nxx too big (%d) ignored", len);
		return(-2);
	}
	memcpy(dst, src, len);
	return(0);
}

static void
init_newpage(faxl3_t *fl3)
{
	fl3->page_retry = 0;
	fl3->line_cnt = 0;
	fl3->result = 0;
	discard_queue(&fl3->pageq);
	discard_queue(&fl3->saveq);
	test_and_clear_bit(FAXL3_STATE_NEWPAGE, &fl3->state);
}

static void
l3m_receive_dis(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t		*fl3 = fi->userdata;
	struct sk_buff	*skb = arg;
	u8		end, *p = skb->data;

	if (skb->len < 3) {
		int_errtxt("HDLC too short %d", skb->len);
		return;
	}
	if (*p != 0xff) {
		int_errtxt("HDLC addr not FF (%02X)", *p);
	}
	p++;
	if (*p == 0x03) {
		end = 0;
	} else if (*p == 0x13) {
		end = 1;
	} else {
		int_errtxt("wrong HDLC CTRL (%02X)", *p);
	}
	p++;
	skb_pull(skb, 3);
	switch(*p) {
		case 0x80: // DIS
			if (0 == get_Dxx(fl3->DIS, p+1, skb->len))
				test_and_set_bit(FAXL3_STATE_GOT_DIS, &fl3->state);
			break;
		case 0x40: // CIS
			if (0 == get_CHAR20(fl3->CIS, p+1, skb->len))
				test_and_set_bit(FAXL3_STATE_GOT_CIS, &fl3->state);
			break;
		case 0x20: // NSF
			if (0 == get_Nxx(fl3->NSF, p+1, skb->len))
				test_and_set_bit(FAXL3_STATE_GOT_NSF, &fl3->state);
			break;
		default:
			int_errtxt("unhandled FCF (%02X) len %d", *p, skb->len);
			break;
	}
}

static void
l3m_finish_dis(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t *fl3 = fi->userdata;

	if (fl3->debug & DEBUG_FAXL3_SIG) {
		if (test_bit(FAXL3_STATE_GOT_DIS, &fl3->state))
			print_Dxx(fl3, Dxx_TYPE_DIS);
		if (test_bit(FAXL3_STATE_GOT_CIS, &fl3->state))
			printk(KERN_DEBUG "CIS: %s\n", fl3->CIS);
		if (test_bit(FAXL3_STATE_GOT_NSF, &fl3->state))
			print_hexdata(fl3, "NSF: ", 12, fl3->NSF);
	}
	fl3->peer_rate_mask = FaxModulationRates[(fl3->DIS[1]>>2) &0xf];
	fl3->current_rate_idx = calc_max_rate(fl3);
	if (fl3->current_rate_idx > 0) {
		fill_Dxx(fl3, Dxx_TYPE_DCS);
		if (fl3->debug & DEBUG_FAXL3_SIG)
			print_Dxx(fl3, Dxx_TYPE_DCS);
		fl3->pending_mod = HW_MOD_FTH;
		fl3->pending_rate = 3;
		mISDN_FsmChangeState(fi, ST_L3_WAIT_SENDDCS);
		mISDN_FsmEvent(&fl3->mod, EV_MOD_NEW, NULL);
	} else // ABORT
		mISDN_FsmChangeState(fi, ST_L3_IDLE);
}

static void
l3m_send_dcs(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t *fl3 = fi->userdata;

	mISDN_FsmChangeState(fi, ST_L3_SEND_DCS);
	if (!test_and_set_bit(FAXL3_STATE_SENT_TIS, &fl3->state))
		send_char20(fl3, &fl3->stationID[1], 0x43, 0);
	send_Dxx(fl3, Dxx_TYPE_DCS, 1);
}

static void
l3m_send_lastdata(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t	*fl3 = fi->userdata;
	int	err;

	if (test_and_clear_bit(FAXL3_STATE_DATALAST, &fl3->state)) {
		err = 0;
		err = if_link(&fl3->inst.down, PH_CONTROL | REQUEST, HW_MOD_LASTDATA, sizeof(int), &err, 0);
		if (err) {
			int_error();
			return;
		}
	}
}

static void
l3m_finish_dcs(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t *fl3 = fi->userdata;

	mISDN_FsmChangeState(fi, ST_L3_WAIT_SENDTRAIN);
	/* wait 75 ms */
	mISDN_FsmRestartTimer(&fl3->deltimer, 75, EV_DELAYTIMER, NULL, 2);
}

static void
l3m_setup_train(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t *fl3 = fi->userdata;

	if (is_valid_rate_idx(fl3, fl3->current_rate_idx)) {
		fl3->pending_mod = HW_MOD_FTM;
		fl3->pending_rate = FaxModulationTrain[fl3->current_rate_idx];
		mISDN_FsmEvent(&fl3->mod, EV_MOD_NEW, NULL);
	} else // ABORT
		mISDN_FsmChangeState(fi, ST_L3_IDLE);
}

static void
l3m_send_train(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t		*fl3 = fi->userdata;
	struct sk_buff	*skb;
	int		len, ret;

	mISDN_FsmChangeState(fi, ST_L3_SEND_TRAIN);
	len = 3*(FaxModulationBaud[fl3->current_rate_idx]/16); // 1,5 sec
	if (!(skb = alloc_stack_skb(len, 1)))
		return;
	memset(skb_put(skb, len), 0, len);
	test_and_set_bit(FAXL3_STATE_DATALAST, &fl3->state);
	ret = send_data_down(fl3, skb);
	if (ret) {
		dev_kfree_skb(skb);
	}
}

static void
l3m_finish_train(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t *fl3 = fi->userdata;

	fl3->pending_mod = HW_MOD_FRH;
	fl3->pending_rate = 3;
	mISDN_FsmChangeState(fi, ST_L3_WAIT_TRAINSTATE);
	mISDN_FsmEvent(&fl3->mod, EV_MOD_NEW, NULL);
}

static void
l3m_activ_trainstate(struct FsmInst *fi, int event, void *arg)
{
	mISDN_FsmChangeState(fi, ST_L3_RECV_TRAINSTATE);
}

static void
l3m_receive_trainstate(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t		*fl3 = fi->userdata;
	struct sk_buff	*skb = arg;
	u8		end, *p = skb->data;

	if (skb->len < 3) {
		int_errtxt("HDLC too short %d", skb->len);
		return;
	}
	if (*p != 0xff) {
		int_errtxt("HDLC addr not FF (%02X)", *p);
	}
	p++;
	if (*p == 0x03) {
		end = 0;
	} else if (*p == 0x13) {
		end = 1;
	} else {
		int_errtxt("wrong HDLC CTRL (%02X)", *p);
	}
	p++;
	skb_pull(skb, 3);
	switch(*p) {
		case 0x84: // CFR
			fl3->result = FAXL3_RESULT_CFR;
			printk(KERN_DEBUG "training successfull\n");
			if (!test_and_set_bit(FAXL3_STATE_CAPICONNECT, &fl3->state))
				send_capi_msg_ncpi(fl3, CAPI_CONNECT_B3_ACTIVE_IND, 0);
			break;
		case 0x44: // FTT
			fl3->result = FAXL3_RESULT_FTT;
			printk(KERN_DEBUG "training failed\n");
			break;
		default:
			fl3->result = FAXL3_RESULT_NONE;
			int_errtxt("unhandled FCF (%02X) len %d", *p, skb->len);
			break;
	}
}

static void
l3m_finish_trainstate(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t *fl3 = fi->userdata;

	if (fl3->result == FAXL3_RESULT_FTT) {
		fl3->pending_mod = HW_MOD_FTH;
		fl3->pending_rate = 3;
		if (fallback_rate(fl3)) {
			fill_Dxx(fl3, Dxx_TYPE_DCS);
			mISDN_FsmChangeState(fi, ST_L3_WAIT_SENDDCS);
		} else {
			mISDN_FsmChangeState(fi, ST_L3_WAIT_SENDDCN);
		}
	} else {
		mISDN_FsmChangeState(fi, ST_L3_WAIT_SENDPAGE);
		mISDN_FsmRestartTimer(&fl3->deltimer, 75, EV_DELAYTIMER, NULL, 2);
	}
}

static void
l3m_setup_sendpage(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t *fl3 = fi->userdata;

	if (is_valid_rate_idx(fl3, fl3->current_rate_idx)) {
		fl3->pending_mod = HW_MOD_FTM;
		fl3->pending_rate = FaxModulation[fl3->current_rate_idx];
		mISDN_FsmEvent(&fl3->mod, EV_MOD_NEW, NULL);
	} else // ABORT
		mISDN_FsmChangeState(fi, ST_L3_IDLE);
}

static void
l3m_ready_sendpage(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t	*fl3 = fi->userdata;
	struct sk_buff	*skb;
	int		ret;

	mISDN_FsmChangeState(fi, ST_L3_SEND_PAGE);
	if (!(skb = alloc_stack_skb(4000, 1)))
		return;
//	memset(skb_put(skb, 1000), 0xff, 1000);
//	memset(skb_put(skb, 1000), 0, 1000);
//	memset(skb_put(skb, 100), 0xff, 100);
	memset(skb_put(skb, ttt), 0, ttt);
	test_and_set_bit(FAXL3_STATE_DATAREADY, &fl3->state);
	ret = send_data_down(fl3, skb);
	if (ret) {
		dev_kfree_skb(skb);
	}
}

static void
l3m_send_next_pagedata(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t		*fl3 = fi->userdata;
	struct sk_buff	*skb;
	int		err;

start:
	if (test_bit(FAXL3_STATE_DATALAST, &fl3->state)) {
		if (skb_queue_empty(&fl3->pageq)) {
			err = 0;
			err = if_link(&fl3->inst.down, PH_CONTROL | REQUEST, HW_MOD_LASTDATA, sizeof(int), &err, 0);
			if (err) {
				int_error();
				return;
			}
			test_and_clear_bit(FAXL3_STATE_DATALAST, &fl3->state);
		} else {
			while((skb = skb_dequeue(&fl3->pageq)))
				send_data_down(fl3, skb);
		}
	} else {
		if (!fl3->page_retry) {
			prepare_page_data(fl3);
		} else if (skb_queue_empty(&fl3->pageq)) {
			test_and_set_bit(FAXL3_STATE_DATALAST, &fl3->state);
			goto start;
		}
		if ((skb = skb_dequeue(&fl3->pageq)))
			send_data_down(fl3, skb);
	}
}

static void
l3m_finish_page(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t *fl3 = fi->userdata;

	test_and_clear_bit(FAXL3_STATE_DATAREADY, &fl3->state);
	fl3->pending_mod = HW_MOD_FTH;
	fl3->pending_rate = 3;
	mISDN_FsmChangeState(fi, ST_L3_WAIT_SENDEOP);
	mISDN_FsmEvent(&fl3->mod, EV_MOD_NEW, NULL);
}

static void
l3m_send_endofpage(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t *fl3 = fi->userdata;
	u8	fcf;

	mISDN_FsmChangeState(fi, ST_L3_SEND_EOP);
	if (test_bit(FAXL3_STATE_LASTPAGE, &fl3->state))
		fcf = 0x2f; // EOP
	else
		fcf = 0x4f; // MPS
	send_hdlc_data(fl3, 0xff, 0x13, fcf, NULL, 0);
}

static void
l3m_finish_eop(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t *fl3 = fi->userdata;

	fl3->pending_mod = HW_MOD_FRH;
	fl3->pending_rate = 3;
	mISDN_FsmChangeState(fi, ST_L3_WAIT_RECVMCF);
	mISDN_FsmEvent(&fl3->mod, EV_MOD_NEW, NULL);
}

static void
l3m_activ_mcf(struct FsmInst *fi, int event, void *arg)
{
	mISDN_FsmChangeState(fi, ST_L3_RECV_MCF);
}

static void
l3m_receive_mcf(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t		*fl3 = fi->userdata;
	struct sk_buff	*skb = arg;
	u8		end, *p = skb->data;

	if (skb->len < 3) {
		int_errtxt("HDLC too short %d", skb->len);
		return;
	}
	if (*p != 0xff) {
		int_errtxt("HDLC addr not FF (%02X)", *p);
	}
	p++;
	if (*p == 0x03) {
		end = 0;
	} else if (*p == 0x13) {
		end = 1;
	} else {
		int_errtxt("wrong HDLC CTRL (%02X)", *p);
	}
	p++;
	skb_pull(skb, 3);
	switch(*p) {
		case 0x8C: // MCF
			fl3->result = FAXL3_RESULT_MCF;
			printk(KERN_DEBUG "got MCF\n");
			break;
		case 0xCC: // RTP
			fl3->result = FAXL3_RESULT_RTP;
			printk(KERN_DEBUG "got RTP\n");
			break;
		case 0x4C: // RTN
			fl3->result = FAXL3_RESULT_RTN;
			printk(KERN_DEBUG "got RTN\n");
			break;
		default:
			fl3->result = FAXL3_RESULT_NONE;
			int_errtxt("unhandled FCF (%02X) len %d", *p, skb->len);
			break;
	}
}

static void
l3m_finish_mcf(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t *fl3 = fi->userdata;
	int	newstate = ST_L3_WAIT_SENDDCN;

	if (fl3->result == FAXL3_RESULT_RTN) {
		fl3->page_retry++;
		if ((fl3->page_retry < 5) && fallback_rate(fl3)) {
			newstate = ST_L3_WAIT_SENDDCS;
			fill_Dxx(fl3, Dxx_TYPE_DCS);
			copy_page_data4retry(fl3);
		}
	} else if (fl3->result == FAXL3_RESULT_MCF) {
		if (!test_bit(FAXL3_STATE_LASTPAGE, &fl3->state)) {
			init_newpage(fl3);
			prepare_page_data(fl3);
			mISDN_FsmChangeState(fi, ST_L3_WAIT_SENDPAGE);
			mISDN_FsmRestartTimer(&fl3->deltimer, 75, EV_DELAYTIMER, NULL, 2);
			return;
		}
	} else if (fl3->result == FAXL3_RESULT_RTP) {
		if (!test_bit(FAXL3_STATE_LASTPAGE, &fl3->state)) {
			init_newpage(fl3);
			prepare_page_data(fl3);
			newstate = ST_L3_WAIT_SENDDCS;
			fill_Dxx(fl3, Dxx_TYPE_DCS);
		}
	} else {
		int_errtxt("unhandled result %d abort", fl3->result);
	}
	fl3->pending_mod = HW_MOD_FTH;
	fl3->pending_rate = 3;
	mISDN_FsmChangeState(fi, newstate);
	mISDN_FsmEvent(&fl3->mod, EV_MOD_NEW, NULL);
}

static void

l3m_send_dcn(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t *fl3 = fi->userdata;

	send_hdlc_data(fl3, 0xff, 0x13, 0xfb, NULL, 0);
}

static void
l3m_finish_dcn(struct FsmInst *fi, int event, void *arg)
{
	faxl3_t *fl3 = fi->userdata;

	
	send_capi_msg_ncpi(fl3, CAPI_DISCONNECT_B3_IND, 0);
	mISDN_FsmChangeState(fi, ST_L3_CLEARING);
}

static struct FsmNode FaxL3FnList[] =
{
	{ST_L3_IDLE,		EV_CALL_OUT,		l3m_callout},
	{ST_L3_WAIT_RECVDIS,	EV_MODEM_ACTIV,		l3m_activ_dis},
	{ST_L3_RECV_DIS,	EV_DATA,		l3m_receive_dis},
	{ST_L3_RECV_DIS,	EV_MODEM_IDLE,		l3m_finish_dis},
	{ST_L3_WAIT_SENDDCS,	EV_MODEM_ACTIV,		l3m_send_dcs},
	{ST_L3_SEND_DCS,	EV_NEXT_DATA,		l3m_send_lastdata},
	{ST_L3_SEND_DCS,        EV_MODEM_IDLE,		l3m_finish_dcs},
	{ST_L3_WAIT_SENDTRAIN,	EV_DELAYTIMER,		l3m_setup_train},
	{ST_L3_WAIT_SENDTRAIN,	EV_MODEM_ACTIV,		l3m_send_train},
	{ST_L3_SEND_TRAIN,	EV_MODEM_IDLE,		l3m_finish_train},
	{ST_L3_SEND_TRAIN,	EV_NEXT_DATA,		l3m_send_lastdata},
	{ST_L3_WAIT_TRAINSTATE,	EV_MODEM_ACTIV,		l3m_activ_trainstate},
	{ST_L3_RECV_TRAINSTATE,	EV_DATA,		l3m_receive_trainstate},
	{ST_L3_RECV_TRAINSTATE,	EV_MODEM_IDLE,		l3m_finish_trainstate},
	{ST_L3_WAIT_SENDPAGE,   EV_DELAYTIMER,		l3m_setup_sendpage},
	{ST_L3_WAIT_SENDPAGE,	EV_MODEM_ACTIV,		l3m_ready_sendpage},
	{ST_L3_SEND_PAGE,	EV_NEXT_DATA,		l3m_send_next_pagedata},
	{ST_L3_SEND_PAGE,	EV_MODEM_IDLE,		l3m_finish_page},
	{ST_L3_WAIT_SENDEOP,	EV_MODEM_ACTIV,		l3m_send_endofpage},
	{ST_L3_SEND_EOP,	EV_NEXT_DATA,		l3m_send_lastdata},
	{ST_L3_SEND_EOP,	EV_MODEM_IDLE,		l3m_finish_eop},
	{ST_L3_WAIT_RECVMCF,	EV_MODEM_ACTIV,		l3m_activ_mcf},
	{ST_L3_RECV_MCF,	EV_DATA,		l3m_receive_mcf},
	{ST_L3_RECV_MCF,	EV_MODEM_IDLE,		l3m_finish_mcf},
	{ST_L3_WAIT_SENDDCN,	EV_MODEM_ACTIV,		l3m_send_dcn},
	{ST_L3_SEND_DCN,	EV_NEXT_DATA,		l3m_send_lastdata},
	{ST_L3_SEND_DCN,	EV_MODEM_IDLE,		l3m_finish_dcn},
};

#define FAXL3_FN_COUNT (sizeof(FaxL3FnList)/sizeof(struct FsmNode))

static int
data_b3_conf(faxl3_t *fl3, struct sk_buff *skb)
{
	mISDN_head_t	*hh = mISDN_HEAD_P(skb);
	u8		buf[4];

	hh++;
	capimsg_setu16(buf, 0, hh->prim); // datahandle	
	hh--;
	capimsg_setu16(buf, 2, 0); // Info
	sendL4frame(fl3, CAPI_DATA_B3_CONF, hh->dinfo, 4, buf, NULL);
	dev_kfree_skb(skb);
	return(0);
}

static int
copy_page_data4retry(faxl3_t *fl3) {
	struct sk_buff_head	tmpq;
	struct sk_buff		*skb, *nskb;
	int			err = 0;

	skb_queue_head_init(&tmpq);
	discard_queue(&fl3->pageq);
	while((skb = skb_dequeue(&fl3->saveq))) {
		nskb = skb_clone(skb, GFP_ATOMIC);
		skb_queue_tail(&fl3->pageq, skb);
		if (!nskb) {
			int_error();
			err = -ENOMEM;
		} else
			skb_queue_tail(&tmpq, nskb);
		
	}
	if (err) {
		discard_queue(&tmpq);
	} else {
		while((skb = skb_dequeue(&tmpq)))
			skb_queue_tail(&fl3->saveq, skb);
	}
	return(err);
}

#define	PAGE_SKB_LEN	1024

static int
collect_page_data(faxl3_t *fl3, int len, u8 *buf, int flush)
{
	int		l = len;
	struct sk_buff	*skb;

	if (!fl3->page_skb) {
		fl3->page_skb = alloc_stack_skb(PAGE_SKB_LEN, 1);
		if (!fl3->page_skb)
			return(-ENOMEM);
	}
	if ((fl3->page_skb->len + len) >= PAGE_SKB_LEN) {
		l = PAGE_SKB_LEN - fl3->page_skb->len;
		memcpy(skb_put(fl3->page_skb, l), buf, l);
		buf += l;
		skb = skb_clone(fl3->page_skb, GFP_ATOMIC);
		if (!skb) {
			int_error();
		} else {
			// for resend pages
			skb_queue_tail(&fl3->saveq, skb);
		}
		skb_queue_tail(&fl3->pageq, fl3->page_skb);
		fl3->page_skb = alloc_stack_skb(PAGE_SKB_LEN, 1);
		if (!fl3->page_skb) {
			int_error();
			return(-ENOMEM);
		}
		l = len - l;
	}
	if (l) {
		memcpy(skb_put(fl3->page_skb, l), buf, l);
	}
	if (flush) {
		skb = skb_clone(fl3->page_skb, GFP_ATOMIC);
		if (!skb) {
			int_error();
		} else {
			// for resend pages
			skb_queue_tail(&fl3->saveq, skb);
		}
		skb_queue_tail(&fl3->pageq, fl3->page_skb);
		fl3->page_skb = NULL;
	}
	return(0);
}

static int
fill_empty_lines(faxl3_t *fl3, int cnt) {
	u8		buf[4], *p;
	int		l,ret = 0;

	if (fl3->debug & DEBUG_FAXL3_PAGEPREPARE)
		printk(KERN_DEBUG "%s %d\n", __FUNCTION__, cnt);
	p = buf;
	if (fl3->page_llen == 1728) {
		*p++ = 0x00;
		*p++ = 0x40;
		*p++ = 0xd9;
		*p++ = 0xa4;
		l = 4;
	} else {
		int_error();
		return(-EINVAL);
	}
	while(cnt) {
		ret = collect_page_data(fl3, l, buf, 0);
		fl3->line_cnt++;
		cnt--;
	}
	return(ret);	
}

static int
fill_rtc(faxl3_t *fl3) {
	u8		buf[8] = {0, 0x08, 0x80,};
	int		cnt = 3, ret = 0;

	if (fl3->debug & DEBUG_FAXL3_PAGEPREPARE)
		printk(KERN_DEBUG "%s\n", __FUNCTION__);
	while(cnt) {
		ret = collect_page_data(fl3, 3, buf, 0);
		cnt--;
	}
	memset(buf, 0 ,8);
	ret = collect_page_data(fl3, 8, buf, 1);
	return(ret);	
}

static int
fill_line(faxl3_t *fl3, int cnt1, u8 *data1, int cnt2, u8 *data2) {
	u8		eol[2] = {0x00, 0x80};
	int		ret = 0;

	if (fl3->debug & DEBUG_FAXL3_PAGEPREPARE)
		printk(KERN_DEBUG "%s: %d/%d\n", __FUNCTION__, cnt1, cnt2);
	ret = collect_page_data(fl3, 2, eol, 0);
	if (cnt1)
		ret = collect_page_data(fl3, cnt1, data1, 0);
	if (cnt2)
		ret = collect_page_data(fl3, cnt2, data2, 0);
	fl3->line_cnt++;
	return(ret);	
}

static int
next_data_skb(faxl3_t *fl3) {
	struct sk_buff	*skb;
	int		cnt = 3, ret = 0;

	skb = skb_dequeue(&fl3->dataq);
	if (fl3->debug & DEBUG_FAXL3_PAGEPREPARE)
		printk(KERN_DEBUG "%s: %p/%p %d %d %lx\n", __FUNCTION__, fl3->data_skb, skb,
			fl3->lasttyp, fl3->lastlen, fl3->state);
	if (!skb) {
		if (fl3->data_skb && (fl3->data_skb->len == 0)) {
			data_b3_conf(fl3, fl3->data_skb);
			fl3->data_skb = NULL;
			test_and_clear_bit(FAXL3_STATE_HAVEDATA, &fl3->state);
		}
		return(-EAGAIN);
	}
	if (fl3->data_skb) {
		if (fl3->debug & DEBUG_FAXL3_PAGEPREPARE)
			printk(KERN_DEBUG "%s: len(%d) hl(%d)\n", __FUNCTION__, 
				fl3->data_skb->len, skb_headroom(skb));
		if (fl3->data_skb->len) {
			if (fl3->data_skb->len <= skb_headroom(skb)) {
				memcpy(skb_push(skb, fl3->data_skb->len), fl3->data_skb->data, fl3->data_skb->len);
				skb_pull(fl3->data_skb, fl3->data_skb->len);
			}
			if (test_and_clear_bit(FAXL3_STATE_CONTINUE, &fl3->state)) {
				cnt = fl3->lastlen - fl3->data_skb->len;
				fill_line(fl3, fl3->data_skb->len, fl3->data_skb->data, cnt, skb->data);
				skb_pull(fl3->data_skb, fl3->data_skb->len);
				skb_pull(skb, cnt);
			}
			if (fl3->data_skb->len) {
				int_error();
				return(-EINVAL);
			}
		}
		data_b3_conf(fl3, fl3->data_skb);
	}
	fl3->data_skb = skb;
	return(ret);	
}

static int
prepare_page_data(faxl3_t *fl3)
{
	u32	tmp32;
	u16	tmp16;
	u8	ver, len8;

	if (!fl3->data_skb) {
		fl3->data_skb = skb_dequeue(&fl3->dataq);
		if (!fl3->data_skb)
			return(-EAGAIN);
	}
	if (test_bit(FAXL3_STATE_CONTINUE, &fl3->state)) {
		if (next_data_skb(fl3))
			return(-EAGAIN);
	}
	if (!test_and_set_bit(FAXL3_STATE_SFFHEADER, &fl3->state)) {
		if (fl3->data_skb->len < 0x14) {
			int_error();
			return(-EINVAL);
		}
		tmp32 = CAPIMSG_U32(fl3->data_skb->data, 0);
		ver = CAPIMSG_U8(fl3->data_skb->data, 4);
		len8 = CAPIMSG_U8(fl3->data_skb->data, 5);
		tmp16 = CAPIMSG_U16(fl3->data_skb->data, 6);
		printk(KERN_DEBUG "SFFHEADER(%x,%x,%x,%x)\n", tmp32, ver, len8, tmp16);
		if (tmp32 != 0x66666653) { // SFFF
			int_error();
			return(-EINVAL);
		}
		if (ver != 1) { // only ver 1 supported
			int_error();
			return(-EINVAL);
		}
		fl3->pages = CAPIMSG_U16(fl3->data_skb->data, 8);
		tmp16 = CAPIMSG_U16(fl3->data_skb->data, 10);
		fl3->offset_lpage = CAPIMSG_U32(fl3->data_skb->data, 12);
		fl3->offset_dend = CAPIMSG_U32(fl3->data_skb->data, 16);
		printk(KERN_DEBUG "SFFHEADER pages %d ofP(%x) o_lpage(%x) o_dend(%x)\n",
			fl3->pages, tmp16, fl3->offset_lpage, fl3->offset_dend);
		if (tmp16 != 0x14) {
			int_error();
			return(-EINVAL);
		}
		skb_pull(fl3->data_skb, 0x14);
	}
	if (fl3->data_skb->len < 2) {
		if (next_data_skb(fl3))
			return(-EAGAIN);
	}
	while (fl3->data_skb->len > 1) {
		fl3->lasttyp = CAPIMSG_U8(fl3->data_skb->data, 0);
		fl3->lastlen = 0;
		if (fl3->lasttyp == 255) {
			int_error();
			return(-EINVAL);
		} else if (fl3->lasttyp == 254) {
			// page header
			len8 = CAPIMSG_U8(fl3->data_skb->data, 1);
			printk(KERN_DEBUG "current page end: %d lines\n", fl3->line_cnt); 
			if (len8 == 0) {
				// doc end
				printk(KERN_DEBUG "SFF doc end found\n");
				test_and_set_bit(FAXL3_STATE_LASTPAGE, &fl3->state);
				test_and_set_bit(FAXL3_STATE_DATALAST, &fl3->state);
				skb_pull(fl3->data_skb, 2);
				fill_rtc(fl3);
				// TODO clean up skb
				return(0);
			}
			if (test_and_set_bit(FAXL3_STATE_NEWPAGE, &fl3->state)) {
				// next page
				fill_rtc(fl3);
				test_and_set_bit(FAXL3_STATE_DATALAST, &fl3->state);
				return(0);
			}
			if (fl3->data_skb->len < (2 + len8)) {
				if (next_data_skb(fl3))
					return(-EAGAIN);
			}
			printk(KERN_DEBUG "SFF page header len %d\n", len8);
			if (len8 < 10) {
				int_error();
				return(-EINVAL);
			}
			fl3->page_vres = CAPIMSG_U8(fl3->data_skb->data, 2);
			fl3->page_hres = CAPIMSG_U8(fl3->data_skb->data, 3);
			fl3->page_code = CAPIMSG_U8(fl3->data_skb->data, 4);
			fl3->page_rsv1 = CAPIMSG_U8(fl3->data_skb->data, 5);
			fl3->page_llen = CAPIMSG_U16(fl3->data_skb->data, 6);
			fl3->page_plen = CAPIMSG_U16(fl3->data_skb->data, 8);
			fl3->page_oprv = CAPIMSG_U32(fl3->data_skb->data, 10);
			fl3->page_onxt = CAPIMSG_U32(fl3->data_skb->data, 14);
			skb_pull(fl3->data_skb, len8 +2);
			printk(KERN_DEBUG "SFF page header: vres(%x) hres(%x) code(%x) resrv(%x)\n",
				fl3->page_vres, fl3->page_hres, fl3->page_code, fl3->page_rsv1);
			printk(KERN_DEBUG "SFF page header: llen(%d) plen(%d) op(%x) on(%x)\n",
				fl3->page_llen, fl3->page_plen, fl3->page_oprv, fl3->page_onxt);
			continue;
		} else if (fl3->lasttyp == 0) {
			if (fl3->data_skb->len < 3) {
				if (next_data_skb(fl3))
					return(-EAGAIN);
			}
			fl3->lastlen = CAPIMSG_U16(fl3->data_skb->data, 1);
			skb_pull(fl3->data_skb, 3);
		} else if (fl3->lasttyp < 216) {
			fl3->lastlen = fl3->lasttyp;
			skb_pull(fl3->data_skb, 1);
		} else if (fl3->lasttyp < 253) {
			// white lines
			skb_pull(fl3->data_skb, 1);
			fill_empty_lines(fl3, fl3->lasttyp - 216);
			continue;
		}
		if (fl3->data_skb->len < fl3->lastlen) {
			test_and_set_bit(FAXL3_STATE_CONTINUE, &fl3->state);
			break;
		}
		fill_line(fl3, fl3->lastlen, fl3->data_skb->data, 0, NULL);
		skb_pull(fl3->data_skb, fl3->lastlen);
	}
	return(0);
}

static u16
data_b3_req(faxl3_t *fl3, struct sk_buff *skb)
{
	__u16		size;
	mISDN_head_t	*hh = mISDN_HEAD_P(skb);

	if (skb->len < 10) {
		int_errtxt("skb too short");
		return(0x2007);
	}

	size =  CAPIMSG_U16(skb->data, 4);

	/* we save DataHandle and Flags in a area after normal mISDN_HEAD */ 
	hh++;
	hh->prim = CAPIMSG_U16(skb->data, 6);
	hh->dinfo = CAPIMSG_U16(skb->data, 8);
	/* the data begins behind the header, we don't use Data32/Data64 here */
	if ((skb->len - size) == 18)
		skb_pull(skb, 18);
	else if ((skb->len - size) == 10) // old format
		skb_pull(skb, 10);
	else {
		int_errtxt("skb data_b3 header len mismatch len %d", skb->len - size);
		return(0x2007);
	}
	if (test_and_set_bit(FAXL3_STATE_HAVEDATA, &fl3->state)) {
		skb_queue_tail(&fl3->dataq, skb);
	} else {
		fl3->data_skb = skb;
		prepare_page_data(fl3);
	}
	return(0);
}

static int
data_b3_resp(faxl3_t *faxl3, u_int di, struct sk_buff *skb)
{
	dev_kfree_skb(skb);
	return(0);
}

static int
connect_b3_req(faxl3_t *fl3, u_int di, u32 addr, struct sk_buff *skb)
{
	u16	info = 0;

	print_hexdata(fl3, "NCPI: ", skb->len, skb->data);
	fl3->pending_mod = HW_MOD_FRH;
	fl3->pending_rate = 3;
	mISDN_FsmEvent(&fl3->mod, EV_MOD_NEW, NULL);
	mISDN_FsmEvent(&fl3->main, EV_CALL_OUT, NULL);
	fl3->ncci = 0x10000 | addr;
	skb_push(skb, 4);
	sendL4frame(fl3, CAPI_CONNECT_B3_CONF, di, 2, &info, skb);
	return(0);
}

static int
sendL4frame(faxl3_t *fl3, int prim, int di, int len, void *arg, struct sk_buff *skb)
{
	u_char		*p;
	int		ret;

	if (!skb) {
		skb = alloc_stack_skb(len +  20, fl3->up_headerlen);
		if (!skb)
			return(-ENOMEM);
	} else {
		skb_trim(skb, 0);
	}
	capimsg_setu32(skb_put(skb, 4), 0, fl3->ncci);
	switch(prim) {
		case CAPI_CONNECT_B3_CONF:
			capimsg_setu16(skb_put(skb, 2), 0, *((u16 *)arg));
			break;
		case CAPI_DISCONNECT_B3_IND:
//			capimsg_setu16(skb_put(skb, 2), 0, flags & 0xffff);
		case CAPI_CONNECT_B3_IND:
		case CAPI_RESET_B3_IND:
		case CAPI_CONNECT_B3_ACTIVE_IND:
		case CAPI_DATA_B3_CONF:
			if (len) {
				p = skb_put(skb, len);
				memcpy(p, arg, len);
			} else {
				p = skb_put(skb, 1);
				*p = 0;
			}
			break;
		default:
			int_error();
			dev_kfree_skb(skb);
			return(-EINVAL);
	}
	ret = if_newhead(&fl3->inst.up, prim, di, skb);
	if (ret) {
		printk(KERN_WARNING "%s: up error %d\n", __FUNCTION__, ret);
		dev_kfree_skb(skb);
	}
	return(ret);
}

static int
send_capi_msg_ncpi(faxl3_t *fl3, int prim, u16 Info)
{
	u8	ncpi[36], *p, off=0, len = 0;
	u16	lastmod = 0;

	memset(ncpi, 0, 36);
	switch(prim) {
		case CAPI_CONNECT_B3_ACTIVE_IND:
			if (is_valid_rate_idx(fl3, fl3->current_rate_idx))
				lastmod = FaxModulationBaud[fl3->current_rate_idx];
			p = fl3->CIS;
			break;
		case CAPI_DISCONNECT_B3_IND:
			if (is_valid_rate_idx(fl3, fl3->current_rate_idx))
				lastmod = FaxModulationBaud[fl3->current_rate_idx];
			p = fl3->CIS;
			capimsg_setu16(ncpi, 0, Info);
			off = 2;
			break;
		default:
			int_error();
			return(-EINVAL);
	}
	capimsg_setu16(ncpi, off+1, lastmod);
	capimsg_setu16(ncpi, off+3, 0x8000); // FIXME no ECM
	capimsg_setu16(ncpi, off+5, 0);
	capimsg_setu16(ncpi, off+7, 0);
	len = strlen(p);
	if (len > 20)
		len = 20;
	capimsg_setu8(ncpi, off+9, len);
	if (len)
		memcpy(&ncpi[off+10], p, len);
	len += 9; // 8*u16 + lenfield
	capimsg_setu8(ncpi, off, len);
	return(sendL4frame(fl3, prim, 0, len + off, ncpi, NULL));
}

static int
faxl3_from_up(mISDNif_t *hif, struct sk_buff *skb)
{
	faxl3_t		*faxl3;
	mISDN_head_t	*hh;
	__u32		addr;
	__u16		info = 0;
	int		err = 0;

	if (!hif || !hif->fdata || !skb)
		return(-EINVAL);
	faxl3 = hif->fdata;
	if (!faxl3->inst.down.func) {
		return(-ENXIO);
	}
	hh = mISDN_HEAD_P(skb);
	if (faxl3->debug & DEBUG_FAXL3_FUNC)
		printk(KERN_DEBUG "%s: prim(%x) dinfo(%x) len(%d)\n", __FUNCTION__, hh->prim, hh->dinfo, skb->len);
	if (skb->len < 4) {
		printk(KERN_WARNING "%s: skb too short (%d)\n", __FUNCTION__, skb->len);
		return(-EINVAL);
	} else {
		addr = CAPIMSG_U32(skb->data, 0);
		skb_pull(skb, 4);
	}
	if (faxl3->debug & DEBUG_FAXL3_FUNC)
		printk(KERN_DEBUG "%s: addr(%x)\n", __FUNCTION__, addr);
	switch(hh->prim) {
		case CAPI_DATA_B3_REQ:
			info = data_b3_req(faxl3, skb);
			if (info) {
			} else
				err = 0;
			break;
		case CAPI_DATA_B3_RESP:
			return(data_b3_resp(faxl3, hh->dinfo, skb));
		case CAPI_CONNECT_B3_REQ:
			return(connect_b3_req(faxl3, hh->dinfo, addr, skb));
		case CAPI_RESET_B3_REQ:
			break;
		case CAPI_DISCONNECT_B3_REQ:
			break;
		case CAPI_CONNECT_B3_RESP:
			if (skb->len <= 2) {
				printk(KERN_WARNING "%s: CAPI_CONNECT_B3_RESP skb too short (%d)\n",
					__FUNCTION__, skb->len);
				skb_push(skb, 4);
				return(-EINVAL);
			}
			info = CAPIMSG_U16(skb->data, 0);
			skb_pull(skb, 2);
			if (info == 0)
				;
			if (skb->len <= 4) { // default NCPI
			} else {
			}
			dev_kfree_skb(skb);
			err = 0;
			break;
		case CAPI_CONNECT_B3_ACTIVE_RESP:
			// nothing to do
			dev_kfree_skb(skb);
			err = 0;
			break;
		case CAPI_RESET_B3_RESP:
			dev_kfree_skb(skb);
			err = 0;
			break;
		case CAPI_DISCONNECT_B3_RESP:
			dev_kfree_skb(skb);
			err = 0;
			break;
		default:
			printk(KERN_WARNING "%s: unknown prim %x dinfo %x\n",
				__FUNCTION__, hh->prim, hh->dinfo);
			err = -EINVAL;
	}
	return(err);
}

static void
ph_status_ind(faxl3_t *fl3, int status)
{
	switch(status) {
		case HW_MOD_READY:
			mISDN_FsmEvent(&fl3->mod, EV_MOD_READY, NULL);
			break;
		case HW_MOD_CONNECT:
			mISDN_FsmEvent(&fl3->mod, EV_MOD_CONNECT, NULL);
			break;
		case HW_MOD_OK:
		case HW_MOD_NOCARR:
			mISDN_FsmEvent(&fl3->mod, EV_MOD_NOCARRIER, NULL);
			break;
		default:
			int_errtxt("unhandled status(%x)", status);
			break;
	}
}

static void
ph_data_cnf(faxl3_t *fl3, int status)
{
	struct sk_buff	*skb ;
	mISDNif_t	*down = &fl3->inst.down;
	int		ret;

	if (!test_bit(FAXL3_STATE_DATABUSY, &fl3->state)) {
		int_errtxt("PH_DATA | CONFIRM without DATABUSY");
		return;
	}
	skb = skb_dequeue(&fl3->downq);
	if ((skb == NULL) && test_bit(FAXL3_STATE_DATAREADY, &fl3->state)) {
		skb = skb_dequeue(&fl3->pageq);
	}
	if (skb)  {
		mISDN_sethead(PH_DATA_REQ, data_next_id(fl3), skb);
		ret = down->func(down, skb);
		if (ret) {
			dev_kfree_skb(skb);
			int_errtxt("down: error(%d)", ret);
			return;
		}
	} else {
		test_and_clear_bit(FAXL3_STATE_DATABUSY, &fl3->state);
		mISDN_FsmEvent(&fl3->main, EV_NEXT_DATA, NULL);
	}
}

static int
faxl3_from_down(mISDNif_t *hif,  struct sk_buff *skb)
{
	faxl3_t		*faxl3;
	mISDN_head_t	*hh;
	int		err = 0;

	if (!hif || !hif->fdata || !skb)
		return(-EINVAL);
	faxl3 = hif->fdata;
	if (!faxl3->inst.up.func) {
		return(-ENXIO);
	}
	hh = mISDN_HEAD_P(skb);
	if (faxl3->debug & DEBUG_FAXL3_FUNC)
		printk(KERN_DEBUG "%s: prim(%x) dinfo(%x) len(%d)\n", __FUNCTION__, hh->prim, hh->dinfo, skb->len);
	switch(hh->prim) {
		case PH_ACTIVATE | INDICATION:
		case PH_ACTIVATE | CONFIRM:
			break;
		case PH_STATUS | INDICATION:
			ph_status_ind(faxl3, hh->dinfo);
			break;
		case PH_DATA | INDICATION:
			mISDN_FsmEvent(&faxl3->main, EV_DATA, skb);
			break;
		case PH_DATA | CONFIRM:
			ph_data_cnf(faxl3, hh->dinfo);
			break;
		default:
			printk(KERN_WARNING "%s: unknown prim %x dinfo %x\n",
				__FUNCTION__, hh->prim, hh->dinfo);
			err = -EINVAL;
			break;
	}
	if (!err)
		dev_kfree_skb(skb);
	return(err);
}

static char MName[] = "FAXL3";

#ifdef MODULE
MODULE_AUTHOR("Karsten Keil");
MODULE_PARM(debug, "1i");
MODULE_PARM(ttt, "1i");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
#endif

static void
l3m_debug(struct FsmInst *fi, char *fmt, ...)
{
	faxl3_t *fl3 = fi->userdata;
	logdata_t log;

	va_start(log.args, fmt);
	log.fmt = fmt;
	log.head = fl3->inst.name;
	fl3->inst.obj->ctrl(&fl3->inst, MGR_DEBUGDATA | REQUEST, &log);
	va_end(log.args);
}

static void
release_faxl3(faxl3_t *faxl3) {
	mISDNinstance_t	*inst = &faxl3->inst;

	if (inst->up.peer) {
		inst->up.peer->obj->ctrl(inst->up.peer,
			MGR_DISCONNECT | REQUEST, &inst->up);
	}
	if (inst->down.peer) {
		inst->down.peer->obj->ctrl(inst->down.peer,
			MGR_DISCONNECT | REQUEST, &inst->down);
	}
	list_del(&faxl3->list);
	discard_queue(&faxl3->downq);
	discard_queue(&faxl3->dataq);
	discard_queue(&faxl3->pageq);
	discard_queue(&faxl3->saveq);
	mISDN_FsmDelTimer(&faxl3->deltimer, 99);
	mISDN_FsmDelTimer(&faxl3->timer1, 99);
	mISDN_FsmDelTimer(&faxl3->modtimer, 99);
	if (faxl3->entity != MISDN_ENTITY_NONE)
		faxl3_obj.ctrl(inst, MGR_DELENTITY | REQUEST, (void *)faxl3->entity);
	faxl3_obj.ctrl(inst, MGR_UNREGLAYER | REQUEST, NULL);
	kfree(faxl3);
}

static int
new_faxl3(mISDNstack_t *st, mISDN_pid_t *pid) {
	faxl3_t *n_faxl3;
	u8	*p;
	int	err;

	if (!st || !pid)
		return(-EINVAL);
	if (!(n_faxl3 = kmalloc(sizeof(faxl3_t), GFP_ATOMIC))) {
		printk(KERN_ERR "kmalloc faxl3_t failed\n");
		return(-ENOMEM);
	}
	memset(n_faxl3, 0, sizeof(faxl3_t));
	n_faxl3->entity = MISDN_ENTITY_NONE;
	n_faxl3->next_id = 1;
	spin_lock_init(&n_faxl3->lock);
	memcpy(&n_faxl3->inst.pid, pid, sizeof(mISDN_pid_t));
	if (n_faxl3->inst.pid.global == 1)
		test_and_set_bit(FAXL3_STATE_OUTGOING, &n_faxl3->state);
	p = n_faxl3->inst.pid.param[3];
	if (p) {
		if (*p < 6) {
			int_errtxt("B3cfg too shoort(%d)", *p);
		} else {
			n_faxl3->options = CAPIMSG_U16(p, 1);
			n_faxl3->format = CAPIMSG_U16(p, 3);
			p += 5;
			if (*p && (*p <= 20))
				memcpy(n_faxl3->stationID, p, *p + 1);
			p += (*p +1);
			if (*p && (*p <= 62))
				memcpy(n_faxl3->headline, p, *p + 1);
		}
	}
	if (debug & DEBUG_FAXL3_CFG) {
		printk(KERN_DEBUG "%s opt(%x) fmt(%x) id=%s head=%s\n",
			test_bit(FAXL3_STATE_OUTGOING, &n_faxl3->state) ? "out" : "in",
			n_faxl3->options,
			n_faxl3->format,
			&n_faxl3->stationID[1],
			&n_faxl3->headline[1]);
		if (n_faxl3->inst.pid.param[1]) {
			p = n_faxl3->inst.pid.param[1];
			printk(KERN_DEBUG "B1 param len %d rate %d\n", *p,
				(*p > 1) ? *((u16 *)(p+1)) : -1);
		}
	}
	mISDN_init_instance(&n_faxl3->inst, &faxl3_obj, n_faxl3);
	if (!mISDN_SetHandledPID(&faxl3_obj, &n_faxl3->inst.pid)) {
		int_error();
		kfree(n_faxl3);
		return(-ENOPROTOOPT);
	}
	n_faxl3->own_rate_mask = FAXMODM_V27_V29_V33_V17; 
	n_faxl3->current_rate_idx = -1;
	n_faxl3->current_mod = -1;
	n_faxl3->current_rate = -1;
	n_faxl3->pending_mod = -1;
	n_faxl3->pending_rate = -1;
	n_faxl3->debug = debug;
	n_faxl3->main.fsm = &faxl3fsm;
	n_faxl3->main.state = ST_L3_IDLE;
	n_faxl3->main.debug = debug;
	n_faxl3->main.userdata = n_faxl3;
	n_faxl3->main.userint = 0;
	n_faxl3->main.printdebug = l3m_debug;
	mISDN_FsmInitTimer(&n_faxl3->main, &n_faxl3->deltimer);
	mISDN_FsmInitTimer(&n_faxl3->main, &n_faxl3->timer1);

	n_faxl3->mod.fsm = &modfsm;
	n_faxl3->mod.state = ST_MOD_NULL;
	n_faxl3->mod.debug = debug;
	n_faxl3->mod.userdata = n_faxl3;
	n_faxl3->mod.userint = 0;
	n_faxl3->mod.printdebug = l3m_debug;
	mISDN_FsmInitTimer(&n_faxl3->mod, &n_faxl3->modtimer);
	skb_queue_head_init(&n_faxl3->downq);
	skb_queue_head_init(&n_faxl3->dataq);
	skb_queue_head_init(&n_faxl3->pageq);
	skb_queue_head_init(&n_faxl3->saveq);

	list_add_tail(&n_faxl3->list, &faxl3_obj.ilist);
	n_faxl3->entity = MISDN_ENTITY_NONE;
	err = faxl3_obj.ctrl(&n_faxl3->inst, MGR_NEWENTITY | REQUEST, NULL);
	if (err) {
		printk(KERN_WARNING "mISDN %s: MGR_NEWENTITY REQUEST failed err(%x)\n",
			__FUNCTION__, err);
	}
	err = faxl3_obj.ctrl(st, MGR_REGLAYER | INDICATION, &n_faxl3->inst);
	if (err) {
		list_del(&n_faxl3->list);
		kfree(n_faxl3);
	} else {
		if (st->para.maxdatalen)
			n_faxl3->maxdatalen = st->para.maxdatalen;
		if (st->para.up_headerlen)
			n_faxl3->up_headerlen = st->para.up_headerlen;
		if (st->para.down_headerlen)
			n_faxl3->down_headerlen = st->para.down_headerlen;
		if (debug)
			printk(KERN_DEBUG "%s:mlen(%d) hup(%d) hdown(%d)\n", __FUNCTION__,
				n_faxl3->maxdatalen, n_faxl3->up_headerlen, n_faxl3->down_headerlen);
	}
	return(err);
}

static int
faxl3_manager(void *data, u_int prim, void *arg) {
	mISDNinstance_t	*inst = data;
	faxl3_t		*faxl3_l;
	int		err = -EINVAL;

	if (debug & DEBUG_FAXL3_MGR)
		printk(KERN_DEBUG "faxl3_manager data:%p prim:%x arg:%p\n", data, prim, arg);
	if (!data)
		return(err);
	list_for_each_entry(faxl3_l, &faxl3_obj.ilist, list) {
		if (&faxl3_l->inst == inst) {
			err = 0;
			break;
		}
	}
	if (prim == (MGR_NEWLAYER | REQUEST))
		return(new_faxl3(data, arg));
	if (err) {
		printk(KERN_WARNING "faxl3_manager prim(%x) no instance\n", prim);
		return(err);
	}
	switch(prim) {
	    case MGR_CLRSTPARA | INDICATION:
	    case MGR_CLONELAYER | REQUEST:
		break;
	    case MGR_ADDSTPARA | INDICATION:
		{
			mISDN_stPara_t *stp = arg;

			if (stp->down_headerlen)
				faxl3_l->down_headerlen = stp->down_headerlen;
			if (stp->up_headerlen)
				faxl3_l->up_headerlen = stp->up_headerlen;
			printk(KERN_DEBUG "MGR_ADDSTPARA: (%d/%d/%d)\n",
				stp->maxdatalen, stp->down_headerlen, stp->up_headerlen);
	    	}
	    	break;
	    case MGR_CONNECT | REQUEST:
		return(mISDN_ConnectIF(inst, arg));
	    case MGR_NEWENTITY | CONFIRM:
		faxl3_l->entity = (int)arg;
		break;
	    case MGR_SETIF | REQUEST:
	    case MGR_SETIF | INDICATION:
		return(mISDN_SetIF(inst, arg, prim, faxl3_from_up, faxl3_from_down, faxl3_l));
	    case MGR_DISCONNECT | REQUEST:
	    case MGR_DISCONNECT | INDICATION:
		return(mISDN_DisConnectIF(inst, arg));
	    case MGR_UNREGLAYER | REQUEST:
	    case MGR_RELEASE | INDICATION:
		if (debug & DEBUG_FAXL3_MGR)
			printk(KERN_DEBUG "release_faxl3 id %x\n", faxl3_l->inst.st->id);
		release_faxl3(faxl3_l);
		break;
	    default:
		if (debug & DEBUG_FAXL3_MGR)
			printk(KERN_WARNING "faxl3_manager prim %x not handled\n", prim);
		return(-EINVAL);
	}
	return(0);
}

static int faxl3_init(void)
{
	int err;

	printk(KERN_INFO "%s modul version %s\n", MName, mISDN_getrev(mISDN_faxl3_revision));
#ifdef MODULE
	faxl3_obj.owner = THIS_MODULE;
#endif
	faxl3_obj.name = MName;
	faxl3_obj.BPROTO.protocol[3] = ISDN_PID_L3_B_T30;
	faxl3_obj.own_ctrl = faxl3_manager;
	INIT_LIST_HEAD(&faxl3_obj.ilist);
	if ((err = mISDN_register(&faxl3_obj))) {
		printk(KERN_ERR "Can't register %s error(%d)\n", MName, err);
		return(err);
	}
	faxl3fsm.state_count = FAXL3_STATE_COUNT;
	faxl3fsm.event_count = FAXL3_EVENT_COUNT;
	faxl3fsm.strEvent = strfaxl3Event;
	faxl3fsm.strState = strfaxl3State;
	mISDN_FsmNew(&faxl3fsm, FaxL3FnList, FAXL3_FN_COUNT);
	modfsm.state_count = MOD_STATE_COUNT;
	modfsm.event_count = MOD_EVENT_COUNT;
	modfsm.strEvent = strmodEvent;
	modfsm.strState = strmodState;
	mISDN_FsmNew(&modfsm, ModFnList, MOD_FN_COUNT);
	return(err);
}

static void faxl3_cleanup(void)
{
	faxl3_t	*l3, *nl3;
	int	err;

	if ((err = mISDN_unregister(&faxl3_obj))) {
		printk(KERN_ERR "Can't unregister DTMF error(%d)\n", err);
	}
	if(!list_empty(&faxl3_obj.ilist)) {
		printk(KERN_WARNING "faxl3 inst list not empty\n");
		list_for_each_entry_safe(l3, nl3, &faxl3_obj.ilist, list)
			release_faxl3(l3);
	}
	mISDN_FsmFree(&faxl3fsm);
	mISDN_FsmFree(&modfsm);
}

module_init(faxl3_init);
module_exit(faxl3_cleanup);

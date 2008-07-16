/*
 *
 * Linux mISDN subsystem, DTMF tone module
 *
 * Author	Karsten Keil <kkeil@suse.de>
 *
 * based on I4L isdn_audio code
 * Copyright 2008 by Karsten Keil <kkeil@novell.com>
 * Copyright 1994-1999 by Fritz Elfert <fritz@isdn4linux.de>
 * DTMF code (c) 1996 by Christian Mock <cm@kukuruz.ping.at>
 * Silence detection (c) 1998 by Armin Schindler <mac@gismo.telekom.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/mISDNif.h>
#include "core.h"

#define DTMF_VERSION	"2.0"

static u_int debug;

MODULE_AUTHOR("Karsten Keil");
module_param(debug, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "dtmf debug mask");
MODULE_LICENSE("GPL");

#define	FLG_DTMF_ULAW	1
#define FLG_DTMF_ACTIV	2

#define DTMF_NPOINTS 205        /* Number of samples for DTMF recognition */

#define DEBUG_DTMF_CTRL		0x001
#define DEBUG_DTMF_TONE		0x010
#define DEBUG_DTMF_DETECT	0x100
#define DEBUG_DTMF_KOEFF	0x200

struct dtmf {
	struct mISDNchannel	ch;
	struct mISDNchannel	*up;
	u_long 			Flags;
	char			last;
	int			idx;
	int			buf[DTMF_NPOINTS];
};


/*
 * Misc. lookup-tables.
 */

/* ulaw -> signed 16-bit */
static short isdn_audio_ulaw_to_s16[] =
{
	0x8284, 0x8684, 0x8a84, 0x8e84, 0x9284, 0x9684, 0x9a84, 0x9e84,
	0xa284, 0xa684, 0xaa84, 0xae84, 0xb284, 0xb684, 0xba84, 0xbe84,
	0xc184, 0xc384, 0xc584, 0xc784, 0xc984, 0xcb84, 0xcd84, 0xcf84,
	0xd184, 0xd384, 0xd584, 0xd784, 0xd984, 0xdb84, 0xdd84, 0xdf84,
	0xe104, 0xe204, 0xe304, 0xe404, 0xe504, 0xe604, 0xe704, 0xe804,
	0xe904, 0xea04, 0xeb04, 0xec04, 0xed04, 0xee04, 0xef04, 0xf004,
	0xf0c4, 0xf144, 0xf1c4, 0xf244, 0xf2c4, 0xf344, 0xf3c4, 0xf444,
	0xf4c4, 0xf544, 0xf5c4, 0xf644, 0xf6c4, 0xf744, 0xf7c4, 0xf844,
	0xf8a4, 0xf8e4, 0xf924, 0xf964, 0xf9a4, 0xf9e4, 0xfa24, 0xfa64,
	0xfaa4, 0xfae4, 0xfb24, 0xfb64, 0xfba4, 0xfbe4, 0xfc24, 0xfc64,
	0xfc94, 0xfcb4, 0xfcd4, 0xfcf4, 0xfd14, 0xfd34, 0xfd54, 0xfd74,
	0xfd94, 0xfdb4, 0xfdd4, 0xfdf4, 0xfe14, 0xfe34, 0xfe54, 0xfe74,
	0xfe8c, 0xfe9c, 0xfeac, 0xfebc, 0xfecc, 0xfedc, 0xfeec, 0xfefc,
	0xff0c, 0xff1c, 0xff2c, 0xff3c, 0xff4c, 0xff5c, 0xff6c, 0xff7c,
	0xff88, 0xff90, 0xff98, 0xffa0, 0xffa8, 0xffb0, 0xffb8, 0xffc0,
	0xffc8, 0xffd0, 0xffd8, 0xffe0, 0xffe8, 0xfff0, 0xfff8, 0x0000,
	0x7d7c, 0x797c, 0x757c, 0x717c, 0x6d7c, 0x697c, 0x657c, 0x617c,
	0x5d7c, 0x597c, 0x557c, 0x517c, 0x4d7c, 0x497c, 0x457c, 0x417c,
	0x3e7c, 0x3c7c, 0x3a7c, 0x387c, 0x367c, 0x347c, 0x327c, 0x307c,
	0x2e7c, 0x2c7c, 0x2a7c, 0x287c, 0x267c, 0x247c, 0x227c, 0x207c,
	0x1efc, 0x1dfc, 0x1cfc, 0x1bfc, 0x1afc, 0x19fc, 0x18fc, 0x17fc,
	0x16fc, 0x15fc, 0x14fc, 0x13fc, 0x12fc, 0x11fc, 0x10fc, 0x0ffc,
	0x0f3c, 0x0ebc, 0x0e3c, 0x0dbc, 0x0d3c, 0x0cbc, 0x0c3c, 0x0bbc,
	0x0b3c, 0x0abc, 0x0a3c, 0x09bc, 0x093c, 0x08bc, 0x083c, 0x07bc,
	0x075c, 0x071c, 0x06dc, 0x069c, 0x065c, 0x061c, 0x05dc, 0x059c,
	0x055c, 0x051c, 0x04dc, 0x049c, 0x045c, 0x041c, 0x03dc, 0x039c,
	0x036c, 0x034c, 0x032c, 0x030c, 0x02ec, 0x02cc, 0x02ac, 0x028c,
	0x026c, 0x024c, 0x022c, 0x020c, 0x01ec, 0x01cc, 0x01ac, 0x018c,
	0x0174, 0x0164, 0x0154, 0x0144, 0x0134, 0x0124, 0x0114, 0x0104,
	0x00f4, 0x00e4, 0x00d4, 0x00c4, 0x00b4, 0x00a4, 0x0094, 0x0084,
	0x0078, 0x0070, 0x0068, 0x0060, 0x0058, 0x0050, 0x0048, 0x0040,
	0x0038, 0x0030, 0x0028, 0x0020, 0x0018, 0x0010, 0x0008, 0x0000
};

/* alaw -> signed 16-bit */
static short isdn_audio_alaw_to_s16[] =
{
	0x13fc, 0xec04, 0x0144, 0xfebc, 0x517c, 0xae84, 0x051c, 0xfae4,
	0x0a3c, 0xf5c4, 0x0048, 0xffb8, 0x287c, 0xd784, 0x028c, 0xfd74,
	0x1bfc, 0xe404, 0x01cc, 0xfe34, 0x717c, 0x8e84, 0x071c, 0xf8e4,
	0x0e3c, 0xf1c4, 0x00c4, 0xff3c, 0x387c, 0xc784, 0x039c, 0xfc64,
	0x0ffc, 0xf004, 0x0104, 0xfefc, 0x417c, 0xbe84, 0x041c, 0xfbe4,
	0x083c, 0xf7c4, 0x0008, 0xfff8, 0x207c, 0xdf84, 0x020c, 0xfdf4,
	0x17fc, 0xe804, 0x018c, 0xfe74, 0x617c, 0x9e84, 0x061c, 0xf9e4,
	0x0c3c, 0xf3c4, 0x0084, 0xff7c, 0x307c, 0xcf84, 0x030c, 0xfcf4,
	0x15fc, 0xea04, 0x0164, 0xfe9c, 0x597c, 0xa684, 0x059c, 0xfa64,
	0x0b3c, 0xf4c4, 0x0068, 0xff98, 0x2c7c, 0xd384, 0x02cc, 0xfd34,
	0x1dfc, 0xe204, 0x01ec, 0xfe14, 0x797c, 0x8684, 0x07bc, 0xf844,
	0x0f3c, 0xf0c4, 0x00e4, 0xff1c, 0x3c7c, 0xc384, 0x03dc, 0xfc24,
	0x11fc, 0xee04, 0x0124, 0xfedc, 0x497c, 0xb684, 0x049c, 0xfb64,
	0x093c, 0xf6c4, 0x0028, 0xffd8, 0x247c, 0xdb84, 0x024c, 0xfdb4,
	0x19fc, 0xe604, 0x01ac, 0xfe54, 0x697c, 0x9684, 0x069c, 0xf964,
	0x0d3c, 0xf2c4, 0x00a4, 0xff5c, 0x347c, 0xcb84, 0x034c, 0xfcb4,
	0x12fc, 0xed04, 0x0134, 0xfecc, 0x4d7c, 0xb284, 0x04dc, 0xfb24,
	0x09bc, 0xf644, 0x0038, 0xffc8, 0x267c, 0xd984, 0x026c, 0xfd94,
	0x1afc, 0xe504, 0x01ac, 0xfe54, 0x6d7c, 0x9284, 0x06dc, 0xf924,
	0x0dbc, 0xf244, 0x00b4, 0xff4c, 0x367c, 0xc984, 0x036c, 0xfc94,
	0x0f3c, 0xf0c4, 0x00f4, 0xff0c, 0x3e7c, 0xc184, 0x03dc, 0xfc24,
	0x07bc, 0xf844, 0x0008, 0xfff8, 0x1efc, 0xe104, 0x01ec, 0xfe14,
	0x16fc, 0xe904, 0x0174, 0xfe8c, 0x5d7c, 0xa284, 0x05dc, 0xfa24,
	0x0bbc, 0xf444, 0x0078, 0xff88, 0x2e7c, 0xd184, 0x02ec, 0xfd14,
	0x14fc, 0xeb04, 0x0154, 0xfeac, 0x557c, 0xaa84, 0x055c, 0xfaa4,
	0x0abc, 0xf544, 0x0058, 0xffa8, 0x2a7c, 0xd584, 0x02ac, 0xfd54,
	0x1cfc, 0xe304, 0x01cc, 0xfe34, 0x757c, 0x8a84, 0x075c, 0xf8a4,
	0x0ebc, 0xf144, 0x00d4, 0xff2c, 0x3a7c, 0xc584, 0x039c, 0xfc64,
	0x10fc, 0xef04, 0x0114, 0xfeec, 0x457c, 0xba84, 0x045c, 0xfba4,
	0x08bc, 0xf744, 0x0018, 0xffe8, 0x227c, 0xdd84, 0x022c, 0xfdd4,
	0x18fc, 0xe704, 0x018c, 0xfe74, 0x657c, 0x9a84, 0x065c, 0xf9a4,
	0x0cbc, 0xf344, 0x0094, 0xff6c, 0x327c, 0xcd84, 0x032c, 0xfcd4
};

#define NCOEFF            8     /* number of frequencies to be analyzed       */
#define DTMF_TRESH     4000     /* above this is dtmf                         */
#define SILENCE_TRESH   200     /* below this is silence                      */
#define AMP_BITS          9     /* bits per sample, reduced to avoid overflow */
#define LOGRP             0
#define HIGRP             1

/* For DTMF recognition:
 * 2 * cos(2 * PI * k / N) precalculated for all k
 */
static int cos2pik[NCOEFF] =
{
	55813, 53604, 51193, 48591, 38114, 33057, 25889, 18332
};

static char dtmf_matrix[4][4] =
{
	{'1', '2', '3', 'A'},
	{'4', '5', '6', 'B'},
	{'7', '8', '9', 'C'},
	{'*', '0', '#', 'D'}
};

/*
 * Goertzel algorithm.
 * See http://ptolemy.eecs.berkeley.edu/~pino/Ptolemy/papers/96/dtmf_ict/
 * for more info.
 */

static void
isdn_audio_goertzel(struct dtmf *dtmf)
{
	int		sk[NCOEFF], sk1[NCOEFF], sk2[NCOEFF];
	register int	sample;
	int		k, n;
	int		thresh, silence;
	int		lgrp, hgrp;
	char		what;

	memset(sk, 0, NCOEFF*sizeof(int));
	memset(sk1, 0, NCOEFF*sizeof(int));
	memset(sk2, 0, NCOEFF*sizeof(int));
	for (n = 0; n < DTMF_NPOINTS; n++) {
		sample = dtmf->buf[n];
		for (k = 0; k < NCOEFF; k++)
			sk[k] = sample + ((cos2pik[k] * sk1[k]) >> 15) - sk2[k];
		memcpy(sk2, sk1, NCOEFF*sizeof(int));
		memcpy(sk1, sk, NCOEFF*sizeof(int));
	}
	thresh = 0;
	silence = 0;
	lgrp = -1;
	hgrp = -1;
	for (k = 0; k < NCOEFF; k++) {
		sk[k] >>= 1;
		sk2[k] >>= 1;
		/* compute |X(k)|**2 */
		/* report overflows. This should not happen. */
		/* Comment this out if desired */
		if (sk[k] < -32768 || sk[k] > 32767)
			printk(KERN_DEBUG
			    "dtmf goertzel overflow, sk[%d]=%d\n", k, sk[k]);
		if (sk2[k] < -32768 || sk2[k] > 32767)
			printk(KERN_DEBUG
			    "isdn_audio: dtmf goertzel overflow, sk2[%d]=%d\n",
			    k, sk2[k]);
		sk1[k] = ((sk[k] * sk[k]) >> AMP_BITS) -
			((((cos2pik[k] * sk[k]) >> 15) * sk2[k]) >> AMP_BITS) +
			((sk2[k] * sk2[k]) >> AMP_BITS);
		if (sk1[k] > DTMF_TRESH) {
			if (sk1[k] > thresh)
				thresh = sk1[k];
		} else if (sk1[k] < SILENCE_TRESH)
			silence++;
	}
	if (debug & DEBUG_DTMF_KOEFF)
		printk(KERN_DEBUG
			"DTMF koeff(%d,%d,%d,%d,%d,%d,%d,%d) range(%d-%d)\n",
			sk1[0], sk1[1], sk1[2], sk1[3], sk1[4], sk1[5],
			sk1[6], sk1[7], SILENCE_TRESH, DTMF_TRESH);
	if (silence == NCOEFF)
		what = ' ';
	else {
		if (thresh > 0)	{
			/* touchtones must match within 12 dB */
			thresh = thresh >> 4;
			for (k = 0; k < NCOEFF; k++) {
				if (sk1[k] < thresh)
					continue;  /* ignore */
				/* good level found. This is allowed
				   only one time per group */
				if (k < NCOEFF / 2) {
					/* lowgroup */
					if (lgrp >= 0) {
						/* Bad. Another tone found. */
						lgrp = -1;
						break;
					} else
						lgrp = k;
				} else { /* higroup */
					if (hgrp >= 0) {
						/* Bad. Another tone found. */
						hgrp = -1;
						break;
					} else
						hgrp = k - NCOEFF/2;
				}
			}
			if ((lgrp >= 0) && (hgrp >= 0)) {
				what = dtmf_matrix[lgrp][hgrp];
				if (dtmf->last != ' ' && dtmf->last != '.')
					/* min. 1 non-DTMF between DTMF */
					dtmf->last = what;
			} else
					what = '.';
		} else
			what = '.';
	}
	if (debug & DEBUG_DTMF_DETECT)
		printk(KERN_DEBUG "DTMF: last(%c) what(%c)\n",
			dtmf->last, what);
	if ((what != dtmf->last) && (what != ' ') && (what != '.')) {
		struct sk_buff *skb;

		if (debug & DEBUG_DTMF_TONE)
			printk(KERN_DEBUG "DTMF: tone='%c'\n", what);
		k = what | DTMF_TONE_VAL;
		skb = _alloc_mISDN_skb(PH_CONTROL_IND, MISDN_ID_ANY,
		    sizeof(int), &k, GFP_ATOMIC);
		if (skb) {
			if (dtmf->up) {
				n = dtmf->up->send(dtmf->up, skb);
				if (n)
					dev_kfree_skb(skb);
			}
		}
	}
	dtmf->last = what;
}

/*
 * Decode audio stream into signed u16
 * start detection if enough data was sampled
 */
static void
isdn_audio_calc_dtmf(struct dtmf *dtmf, struct sk_buff *skb)
{
	int len = skb->len;
	u_char	*p = skb->data;
	int i;
	int c;

	while (len) {
		c = DTMF_NPOINTS - dtmf->idx;
		if (c > len)
			c = len;
		if (c <= 0)
			break;
		for (i = 0; i < c; i++) {
			if (test_bit(FLG_DTMF_ULAW, &dtmf->Flags))
				dtmf->buf[dtmf->idx++] =
				    isdn_audio_ulaw_to_s16[*p++] >>
				    (15 - AMP_BITS);
			else
				dtmf->buf[dtmf->idx++] =
				    isdn_audio_alaw_to_s16[*p++] >>
				    (15 - AMP_BITS);
		}
		if (dtmf->idx == DTMF_NPOINTS) {
			isdn_audio_goertzel(dtmf);
			dtmf->idx = 0;
		}
		len -= c;
	}
}

static void
dtmf_reset(struct dtmf *dtmf)
{
	dtmf->last = ' ';
	dtmf->idx = 0;
}

static int
dtmf_function(struct mISDNchannel *ch,  struct sk_buff *skb)
{
	struct dtmf		*dtmf = container_of(ch, struct dtmf, ch);
	struct mISDNhead	*hh;

	hh = mISDN_HEAD_P(skb);
	switch (hh->prim) {
	case (PH_DATA_IND):
		if (test_bit(FLG_DTMF_ACTIV, &dtmf->Flags))
			isdn_audio_calc_dtmf(dtmf, skb);
		break;
	case (PH_CONTROL_REQ):
		if (skb->len >= sizeof(int)) {
			int *data = (int *)skb->data;

			if (debug & DEBUG_DTMF_CTRL)
				printk(KERN_DEBUG
				    "DTMF: PH_CONTROL REQ data %04x\n", *data);
			if (*data == DTMF_TONE_START) {
				test_and_set_bit(FLG_DTMF_ACTIV, &dtmf->Flags);
				dtmf_reset(dtmf);
				dev_kfree_skb(skb);
				return 0;
			} else if (*data == DTMF_TONE_STOP) {
				test_and_clear_bit(FLG_DTMF_ACTIV,
				    &dtmf->Flags);
				dtmf_reset(dtmf);
				dev_kfree_skb(skb);
				return 0;
			}
		}
		break;
	}
	if ((hh->prim & MISDN_LAYERMASK) == 1 && ch->peer)
		return ch->recv(ch->peer, skb);
	if (dtmf->up)
		return dtmf->up->send(dtmf->up, skb);
	else
		dev_kfree_skb(skb);
	return 0;
}


static int
dtmf_ctrl(struct mISDNchannel *ch, u_int cmd, void *arg)
{
	struct dtmf	*dtmf = container_of(ch, struct dtmf, ch);

	if (debug & DEBUG_DTMF_CTRL)
		printk(KERN_DEBUG "%s:(%x)\n", __func__, cmd);

	switch (cmd) {
	case OPEN_CHANNEL:
		break;
	case CLOSE_CHANNEL:
		if (dtmf->ch.peer)
			dtmf->ch.peer->ctrl(dtmf->ch.peer, CLOSE_CHANNEL, NULL);
		kfree(dtmf);
		module_put(THIS_MODULE);
		break;
	}
	return 0;
}

static int
dtmfcreate(struct channel_req *crq)
{
	struct dtmf	*nd;

	if (crq->protocol != ISDN_P_B_L2DTMF)
		return -EPROTONOSUPPORT;
	nd = kzalloc(sizeof(struct dtmf), GFP_KERNEL);
	if (!nd) {
		printk(KERN_ERR "kmalloc struct dtmf failed\n");
		return -ENOMEM;
	}
	/* default enabled */
	test_and_set_bit(FLG_DTMF_ACTIV, &nd->Flags);
	dtmf_reset(nd);
	nd->ch.send = dtmf_function;
	nd->ch.ctrl = dtmf_ctrl;
	nd->up = crq->ch;
	crq->ch = &nd->ch;
	crq->protocol = ISDN_P_B_RAW;
	if (!try_module_get(THIS_MODULE))
		printk(KERN_WARNING "%s:cannot get module\n",
		    __func__);
	return 0;
}

static struct Bprotocol DTMF = {
	.Bprotocols = (1 << (ISDN_P_B_L2DTMF & ISDN_P_B_MASK)),
	.name = "dtmf",
	.create = dtmfcreate
};

static int dtmf_init(void)
{
	int err;

	printk(KERN_INFO "DTMF modul %s\n", DTMF_VERSION);
	err = mISDN_register_Bprotocol(&DTMF);
	if (err)
		printk(KERN_ERR "Can't register %s err(%d)\n", DTMF.name, err);
	return err;
}

static void dtmf_cleanup(void)
{
	 mISDN_unregister_Bprotocol(&DTMF);
}

module_init(dtmf_init);
module_exit(dtmf_cleanup);

/*
 * dsp_cancel.h: worker functions for various echo cancellers
 *
 * Copyright (C) 2007, Nadi Sarrar
 *
 * Nadi Sarrar <nadi@beronet.com>
 *
 * Derived from dsp_cancel.c written by Chrisian Richter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 */

#ifdef ARCH_I386
#include <asm/i387.h>
#else
#define kernel_fpu_begin()
#define kernel_fpu_end()
#endif

#define EC_TIMER 2000

#define __ECHO_STATE_MUTE		(1 << 8)
#define ECHO_STATE_IDLE			(0)
#define ECHO_STATE_PRETRAINING		(1 | (__ECHO_STATE_MUTE))
#define ECHO_STATE_STARTTRAINING	(2 | (__ECHO_STATE_MUTE))
#define ECHO_STATE_AWAITINGECHO		(3 | (__ECHO_STATE_MUTE))
#define ECHO_STATE_TRAINING		(4 | (__ECHO_STATE_MUTE))
#define ECHO_STATE_ACTIVE		(5)
#define AMI_MASK			0x55

struct ec_prv {
	struct echo_can_state *ec;
	uint16_t echotimer;
	uint16_t echostate;
	uint16_t echolastupdate;
	u8 txbuff[ECHOCAN_BUFF_SIZE];
	int  tx_W;
	int  underrun;
	int  overflow;
};

static inline void *
dsp_cancel_new(int deftaps, int training)
{
	struct ec_prv *p;

	p = kzalloc(sizeof(struct ec_prv), GFP_ATOMIC);
	if (!p)
		goto err1;

	p->ec = echo_can_create(deftaps > 0 ? deftaps : 128, 0);
	if (!p->ec)
		goto err2;

	p->echotimer = training ? training : 0;
	p->echostate = training ? ECHO_STATE_PRETRAINING : ECHO_STATE_IDLE;
	p->echolastupdate = 0;
	memset(p->txbuff, dsp_silence, ECHOCAN_BUFF_SIZE);
	p->tx_W = 0;
	p->underrun = 0;
	p->overflow = 0;

	return p;

err2:
	kfree(p);
err1:
	return NULL;
}

static inline void
dsp_cancel_free(struct ec_prv *p)
{
	if (!p)
		return;
	echo_can_free(p->ec);
	kfree(p);
}

static inline void dsp_cancel_tx(struct ec_prv *p, u8 *data, int len)
{
	u8 *d;
	int w;

	if (!p || !data)
		return;

	d = p->txbuff;
	w = p->tx_W;
	while (len--) {
		d[w] = *data++;
		w = (w + 1) & ECHOCAN_BUFF_MASK;
	}
	p->tx_W = w;
}

/** Processes one TX- and one RX-packet with echocancellation */
static inline void dsp_cancel_rx(struct ec_prv *p, u8 *data, int len, unsigned int txlen)
{
	int16_t	rxlin, txlin;
	int	r;
	u8	*s;

	if (!p || !data)
		return;

	if (txlen > 0xf000)
		txlen = 0; /* if not supported */

	s = p->txbuff;
	/* calculation V0.1 : 'len' and 'txlen' samples off the end */
	r = (p->tx_W - len - txlen) & ECHOCAN_BUFF_MASK;
	kernel_fpu_begin();
	if (p->echostate & __ECHO_STATE_MUTE) {
		/* Special stuff for training the echo can */
		while (len--) {
			rxlin = dsp_audio_law_to_s32[*data];
			txlin = dsp_audio_law_to_s32[s[r]];
			if (p->echostate == ECHO_STATE_PRETRAINING) {
				if (--p->echotimer <= 0) {
					p->echotimer = 0;
					p->echostate = ECHO_STATE_STARTTRAINING;
				}
			}
			if ((p->echostate == ECHO_STATE_AWAITINGECHO) &&
			    (txlin > 8000)) {
				p->echolastupdate = 0;
				p->echostate = ECHO_STATE_TRAINING;
			}
			if (p->echostate == ECHO_STATE_TRAINING) {
				if (echo_can_traintap(p->ec,
				    p->echolastupdate++, rxlin)) {
					p->echostate = ECHO_STATE_ACTIVE;
				}
			}
			rxlin = 0;
			*data++ = dsp_audio_s16_to_law[rxlin & 0xffff];
			r = (r + 1) & ECHOCAN_BUFF_MASK;
		}
	} else {
		while (len--) {
			rxlin = dsp_audio_law_to_s32[*data];
			txlin = dsp_audio_law_to_s32[s[r]];
			rxlin = echo_can_update(p->ec, txlin, rxlin);
			*data++ = dsp_audio_s16_to_law[rxlin & 0xffff];
			r = (r + 1) & ECHOCAN_BUFF_MASK;
		}
	}
	kernel_fpu_end();
}

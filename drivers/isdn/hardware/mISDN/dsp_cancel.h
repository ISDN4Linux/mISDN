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

#include "layer1.h"
#include "helper.h"
#include "debug.h"
#include "dsp.h"
#ifdef ARCH_I386
#include <asm/i387.h>
#else
#define kernel_fpu_begin()
#define kernel_fpu_end()
#endif

#define EC_TIMER 2000

#define __ECHO_STATE_MUTE         (1 << 8)
#define ECHO_STATE_IDLE           (0)
#define ECHO_STATE_PRETRAINING    (1 | (__ECHO_STATE_MUTE))
#define ECHO_STATE_STARTTRAINING  (2 | (__ECHO_STATE_MUTE))
#define ECHO_STATE_AWAITINGECHO   (3 | (__ECHO_STATE_MUTE))
#define ECHO_STATE_TRAINING       (4 | (__ECHO_STATE_MUTE))
#define ECHO_STATE_ACTIVE         (5)
#define AMI_MASK                  0x55

typedef struct _ec_prv_t {
	struct echo_can_state * ec;
	uint16_t echotimer;
	uint16_t echostate;
	uint16_t echolastupdate;
	char txbuf[ECHOCAN_BUFLEN];
	int  txbuflen;
	char rxbuf[ECHOCAN_BUFLEN];
	int  rxbuflen;
	int  underrun;
	int  overflow;
} ec_prv_t;

static inline void *dsp_cancel_new (int deftaps, int training)
{
	ec_prv_t *p;

	p = kmalloc(sizeof(ec_prv_t), GFP_KERNEL);
	if (!p)
		goto _err1;
	
	p->ec = echo_can_create(deftaps > 0 ? deftaps : 128, 0);
	if (!p->ec)
		goto _err2;
	
	p->echotimer = training ? training : 0;
	p->echostate = training ? ECHO_STATE_PRETRAINING : ECHO_STATE_IDLE;
	p->echolastupdate = 0;
	p->txbuflen = 0;
	p->rxbuflen = 0;
	p->underrun = 0;
	p->overflow = 0;

	return p;

_err2:
	kfree(p);
_err1:
	return NULL;
}

static inline void dsp_cancel_free (ec_prv_t *p)
{
	if (!p)
		return;
	echo_can_free(p->ec);
	kfree(p);
}

/** Processes one TX- and one RX-packet with echocancellation */
static inline void echocancel_chunk(ec_prv_t* p, uint8_t *rxchunk, uint8_t *txchunk, uint16_t size)
{
	int16_t rxlin, txlin;
	uint16_t x;

	if (p->echostate & __ECHO_STATE_MUTE) {
		/* Special stuff for training the echo can */
		for (x=0;x<size;x++) {
			rxlin = dsp_audio_law_to_s32[rxchunk[x]];
			txlin = dsp_audio_law_to_s32[txchunk[x]];
			if (p->echostate == ECHO_STATE_PRETRAINING) {
				if (--p->echotimer <= 0) {
					p->echotimer = 0;
					p->echostate = ECHO_STATE_STARTTRAINING;
				}
			}
			if ((p->echostate == ECHO_STATE_AWAITINGECHO) && (txlin > 8000)) {
				p->echolastupdate = 0;
				p->echostate = ECHO_STATE_TRAINING;
			}
			if (p->echostate == ECHO_STATE_TRAINING) {
				if (echo_can_traintap(p->ec, p->echolastupdate++, rxlin)) {
					p->echostate = ECHO_STATE_ACTIVE;
				}
			}
			rxlin = 0;
			rxchunk[x] = dsp_audio_s16_to_law[(int)rxlin];
		}
	} else {
		for (x=0;x<size;x++) {
			rxlin = dsp_audio_law_to_s32[rxchunk[x]&0xff];
			txlin = dsp_audio_law_to_s32[txchunk[x]&0xff];
			rxlin = echo_can_update(p->ec, txlin, rxlin);
			rxchunk[x] = dsp_audio_s16_to_law[rxlin &0xffff];
		}
	}
}

static inline void dsp_cancel_tx (ec_prv_t *p, u8 *data, int len)
{
	if (!p || !data)
		return;

	if (p->txbuflen + len < ECHOCAN_BUFLEN) {
		memcpy(&p->txbuf[p->txbuflen], data, len);
		p->txbuflen += len;
	} else {
		if (p->overflow >= 4000) {
			printk("ECHOCAN: TXBUF Overflow:%d txbuflen:%d txcancellen:%d\n", p->overflow, p->txbuflen, len);
			p->overflow = 0;
		}
		p->overflow += len;
		p->txbuflen = 0;
	}
}

static inline void dsp_cancel_rx (ec_prv_t *p, u8 *data, int len)
{
	if (!p || !data)
		return;

	if (len <= p->txbuflen) {
		char tmp[ECHOCAN_BUFLEN];
		int delta = p->txbuflen - len;

		memcpy(tmp, &p->txbuf[len], delta);
		kernel_fpu_begin();
		echocancel_chunk(p, data, p->txbuf, len);
		kernel_fpu_end();
		memcpy(p->txbuf, tmp, delta);
		p->txbuflen = delta;
	} else {
		if (p->underrun >= 4000) {
			printk("ECHOCAN: TXBUF Underrun:%d txbuflen:%d rxcancellen:%d\n", p->underrun, p->txbuflen,len);
			p->underrun = 0;
		}
		p->underrun += len;
	}
}


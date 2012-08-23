/*
 * dsp_oslec.c: mISDN dsp pipeline element for the oslec echo canceller
 *
 * Copyright (C) 2007, Nadi Sarrar
 *
 * Nadi Sarrar <nadi@beronet.com>
 *
 * Slightly patched by Peter Schlaile <peter_at_schlaile_dot_de>
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

#include <linux/mISDNif.h>
#include <linux/mISDNdsp.h>
#include <linux/module.h>
#include "core.h"
#include "dsp.h"
#include "oslec.h"
#define echo_can_create oslec_echo_can_create
#define echo_can_free oslec_echo_can_free
#define echo_can_update oslec_echo_can_update
#define echo_can_traintap oslec_echo_can_traintap
#include "dsp_cancel.h"

/*#define DEBUG */

static void *new(const char *arg)
{
	int deftaps = 128,
		training = 0,
		len;

	if (!arg)
		goto _out;

	len = strlen(arg);
	if (!len)
		goto _out;

	{
		char _dup[len + 1];
		char *dup, *tok, *name, *val;
		int tmp;

		strcpy(_dup, arg);
		dup = _dup;

		while ((tok = strsep(&dup, ","))) {
			if (!strlen(tok))
				continue;
			name = strsep(&tok, "=");
			val = tok;

			if (!val)
				continue;

			if (!strcmp(name, "deftaps")) {
				if (sscanf(val, "%d", &tmp) == 1)
					deftaps = tmp;
			} else if (!strcmp(name, "training")) {
				if (sscanf(val, "%d", &tmp) == 1)
					training = tmp;
			}
		}
	}

_out:
#ifdef DEBUG
	printk(KERN_DEBUG "%s: creating %s with deftaps=%d and training=%d\n",
		__func__, EC_TYPE, deftaps, training);
#endif
	return dsp_cancel_new(deftaps, training);
}

static void free(void *p)
{
	dsp_cancel_free(p);
}

static void process_tx(void *p, u8 *data, int len)
{
	dsp_cancel_tx(p, data, len);
}

static void process_rx(void *p, u8 *data, int len, unsigned int txlen)
{
	dsp_cancel_rx(p, data, len, txlen);
}

static struct mISDN_dsp_element_arg args[] = {
	{ "deftaps", "128", "Set the number of taps of cancellation." },
	{ "training", "0", "Enable echotraining (0: disabled, 1: enabled)." },
};

static struct mISDN_dsp_element dsp_oslec = {
	.name = "oslec",
	.new = new,
	.free = free,
	.process_tx = process_tx,
	.process_rx = process_rx,
	.num_args = sizeof(args) / sizeof(struct mISDN_dsp_element_arg),
	.args = args,
};

#ifdef MODULE
static int __init dsp_oslec_init(void)
{
	mISDN_dsp_element_register(&dsp_oslec);

	return 0;
}

static void __exit dsp_oslec_exit(void)
{
	mISDN_dsp_element_unregister(&dsp_oslec);
}

module_init(dsp_oslec_init);
module_exit(dsp_oslec_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nadi Sarrar <nadi@beronet.com>");
#endif


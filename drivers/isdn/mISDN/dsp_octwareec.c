/*
 * dsp_octwareec.c: mISDN dsp pipeline element for the octware echo canceller
 *
 * Copyright (C) 2007, Nadi Sarrar
 *
 * Nadi Sarrar <nadi@beronet.com>
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
#include "dsp_octwareec.h"
#include "dsp_cancel.h"

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
	printk(KERN_DEBUG "%s: creating %s with deftaps=%d and training=%d\n",
		__func__, EC_TYPE, deftaps, training);
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

static struct mISDN_dsp_element dsp_octwareec = {
	.name = "octwareec",
	.new = new,
	.free = free,
	.process_tx = process_tx,
	.process_rx = process_rx,
	.num_args = sizeof(args) / sizeof(struct mISDN_dsp_element_arg),
	.args = args,
};

#ifdef MODULE
static int __init dsp_octwareec_init(void)
{
	mISDN_dsp_element_register(&dsp_octwareec);

	return 0;
}

static void __exit dsp_octwareec_exit(void)
{
	mISDN_dsp_element_unregister(&dsp_octwareec);
}

module_init(dsp_octwareec_init);
module_exit(dsp_octwareec_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nadi Sarrar <nadi@beronet.com>");
#endif


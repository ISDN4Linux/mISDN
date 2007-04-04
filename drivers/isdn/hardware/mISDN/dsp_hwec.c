/*
 * dsp_hwec.c: builtin mISDN dsp pipeline element for enabling the hw echocanceller
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

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mISDNdsp.h>
#include <linux/mISDNif.h>
#include "dsp.h"
#include "helper.h"

static mISDN_dsp_element_t dsp_hwec_p = {
	.new = NULL,
	.free = NULL,
	.process_tx = NULL,
	.process_rx = NULL,
	.name = "hwec",
};
mISDN_dsp_element_t *dsp_hwec = &dsp_hwec_p;

void dsp_hwec_enable (dsp_t *dsp, const char *arg)
{
	struct sk_buff *skb;
	int deftaps = 128,
		len;

	if (!dsp) {
		printk(KERN_ERR "%s: failed to enable hwec: dsp is NULL\n", __FUNCTION__);
		return;
	}

	if (!arg)
		goto _do;

	len = strlen(arg);
	if (!len)
		goto _do;

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
			}
		}
	}

_do:
	skb = create_link_skb(PH_CONTROL | REQUEST, HW_ECHOCAN_ON, sizeof(deftaps), &deftaps, 0);
	if (!skb) {
		printk(KERN_ERR "%s: failed to enable hwec: out of memory\n", __FUNCTION__);
		return;
	}

	printk(KERN_DEBUG "%s: enabling hwec with deftaps=%d\n", __FUNCTION__, deftaps);
	if (mISDN_queue_down(&dsp->inst, 0, skb))
		dev_kfree_skb(skb);
}

void dsp_hwec_disable (dsp_t *dsp)
{
	struct sk_buff *skb;

	if (!dsp) {
		printk(KERN_ERR "%s: failed to disable hwec: dsp is NULL\n", __FUNCTION__);
		return;
	}

	skb = create_link_skb(PH_CONTROL | REQUEST, HW_ECHOCAN_OFF, 0, 0, 0);
	if (!skb) {
		printk(KERN_ERR "%s: failed to disable hwec: out of memory\n", __FUNCTION__);
		return;
	}
	printk(KERN_DEBUG "%s: disabling hwec\n", __FUNCTION__);
	if (mISDN_queue_down(&dsp->inst, 0, skb))
		dev_kfree_skb(skb);
}

int dsp_hwec_init (void)
{
	mISDN_dsp_element_register(dsp_hwec);

	return 0;
}

void dsp_hwec_exit (void)
{
	mISDN_dsp_element_unregister(dsp_hwec);
}


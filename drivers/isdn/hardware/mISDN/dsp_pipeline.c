/*
 * dsp_pipeline.c: pipelined audio processing
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
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/mISDNif.h>
#include <linux/mISDNdsp.h>
#include "layer1.h"
#include "dsp.h"

/* Structures */
typedef struct _dsp_pipeline_entry {
	mISDN_dsp_element_t *elem;
	void                *p;
	struct list_head     list;
} dsp_pipeline_entry_t;

typedef struct _dsp_element_entry {
	mISDN_dsp_element_t *elem;
	struct list_head     list;
} dsp_element_entry_t;

/* Static Variables */
rwlock_t dsp_elements_lock;
static LIST_HEAD(dsp_elements);

/* Code */
int mISDN_dsp_element_register (mISDN_dsp_element_t *elem)
{
	dsp_element_entry_t *entry;
	u_long flags;

	if (!elem)
		return -EINVAL;

	entry = kmalloc(sizeof(dsp_element_entry_t), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->elem = elem;
	
	write_lock_irqsave(&dsp_elements_lock, flags);
	list_add_tail(&entry->list, &dsp_elements);
	write_unlock_irqrestore(&dsp_elements_lock, flags);
	
	printk(KERN_DEBUG "%s: %s registered\n", __FUNCTION__, elem->name);

	return 0;
}

void mISDN_dsp_element_unregister (mISDN_dsp_element_t *elem)
{
	dsp_element_entry_t *entry, *n;
	u_long flags;

	if (!elem)
		return;

	write_lock_irqsave(&dsp_elements_lock, flags);
	
	list_for_each_entry_safe(entry, n, &dsp_elements, list)
		if (entry->elem == elem) {
			list_del(&entry->list);
			write_unlock_irqrestore(&dsp_elements_lock, flags);
			kfree(entry);
			printk(KERN_DEBUG "%s: %s unregistered\n", __FUNCTION__, elem->name);
			return;
		}
	
	write_unlock_irqrestore(&dsp_elements_lock, flags);
}

int dsp_pipeline_module_init (void)
{
	rwlock_init(&dsp_elements_lock);

	printk(KERN_DEBUG "%s: dsp pipeline module initialized\n", __FUNCTION__);

	return 0;
}

void dsp_pipeline_module_exit (void)
{
	dsp_element_entry_t *entry, *n;
	u_long flags;

	write_lock_irqsave(&dsp_elements_lock, flags);
	list_for_each_entry_safe(entry, n, &dsp_elements, list) {
		list_del(&entry->list);
		printk(KERN_DEBUG "%s: element was still registered: %s\n", __FUNCTION__, entry->elem->name);
		kfree(entry);
	}
	write_unlock_irqrestore(&dsp_elements_lock, flags);
	
	printk(KERN_DEBUG "%s: dsp pipeline module exited\n", __FUNCTION__);
}

int dsp_pipeline_init (dsp_pipeline_t *pipeline)
{
	if (!pipeline)
		return -EINVAL;

	INIT_LIST_HEAD(&pipeline->list);
	rwlock_init(&pipeline->lock);

	printk(KERN_DEBUG "%s: dsp pipeline ready\n", __FUNCTION__);

	return 0;
}

static inline void _dsp_pipeline_destroy (dsp_pipeline_t *pipeline)
{
	dsp_pipeline_entry_t *entry, *n;

	list_for_each_entry_safe(entry, n, &pipeline->list, list) {
		list_del(&entry->list);
		entry->elem->free(entry->p);
		kfree(entry);
	}
}

void dsp_pipeline_destroy (dsp_pipeline_t *pipeline)
{
	u_long flags;

	if (!pipeline)
		return;

	write_lock_irqsave(&pipeline->lock, flags);
	_dsp_pipeline_destroy(pipeline);
	write_unlock_irqrestore(&pipeline->lock, flags);

	printk(KERN_DEBUG "%s: dsp pipeline destroyed\n", __FUNCTION__);
}

int dsp_pipeline_build (dsp_pipeline_t *pipeline, const char *cfg)
{
	int len, incomplete = 0, found = 0;
	char *dup, *tok, *name, *args;
	dsp_element_entry_t *entry, *n;
	dsp_pipeline_entry_t *pipeline_entry;
	mISDN_dsp_element_t *elem;
	u_long elements_flags, pipeline_flags;

	if (!pipeline)
		return -EINVAL;

	write_lock_irqsave(&pipeline->lock, pipeline_flags);
	if (!list_empty(&pipeline->list))
		_dsp_pipeline_destroy(pipeline);

	if (!cfg) {
		write_unlock_irqrestore(&pipeline->lock, pipeline_flags);
		return 0;
	}

	len = strlen(cfg);
	if (!len) {
		write_unlock_irqrestore(&pipeline->lock, pipeline_flags);
		return 0;
	}

	char _dup[len + 1];
	strcpy(_dup, cfg);
	dup = _dup;

	while ((tok = strsep(&dup, "|"))) {
		if (!strlen(tok))
			continue;
		name = strsep(&tok, "(");
		args = strsep(&tok, ")");
		if (args && !*args)
			args = 0;

		read_lock_irqsave(&dsp_elements_lock, elements_flags);
		list_for_each_entry_safe(entry, n, &dsp_elements, list)
			if (!strcmp(entry->elem->name, name)) {
				elem = entry->elem;
				read_unlock_irqrestore(&dsp_elements_lock, elements_flags);

				pipeline_entry = kmalloc(sizeof(dsp_pipeline_entry_t), GFP_KERNEL);
				if (!pipeline_entry) {
					printk(KERN_DEBUG "%s: failed to add entry to pipeline: %s (out of memory)\n", __FUNCTION__, elem->name);
					incomplete = 1;
					goto _out;
				}
				pipeline_entry->elem = elem;
				pipeline_entry->p = elem->new(args);
				list_add_tail(&pipeline_entry->list, &pipeline->list);
				
				printk(KERN_DEBUG "%s: created instance of %s%s%s\n", __FUNCTION__, name, args ? " with args " : "", args ? args : "");
				found = 1;
				break;
			}

		if (found)
			found = 0;
		else {
			read_unlock_irqrestore(&dsp_elements_lock, elements_flags);
			printk(KERN_DEBUG "%s: element not found, skipping: %s\n", __FUNCTION__, name);
			incomplete = 1;
		}
	}

_out:
	write_unlock_irqrestore(&pipeline->lock, pipeline_flags);
	printk(KERN_DEBUG "%s: dsp pipeline built%s: %s\n", __FUNCTION__, incomplete ? " incomplete" : "", cfg);

	return 0;
}

void dsp_pipeline_process_tx (dsp_pipeline_t *pipeline, u8 *data, int len)
{
	dsp_pipeline_entry_t *entry;

	if (!pipeline)
		return;

	if (!read_trylock(&pipeline->lock)) {
		printk(KERN_DEBUG "%s: bypassing pipeline because it is locked (TX)\n", __FUNCTION__);
		return;
	}
	list_for_each_entry(entry, &pipeline->list, list)
		entry->elem->process_tx(entry->p, data, len);
	read_unlock(&pipeline->lock);
}

void dsp_pipeline_process_rx (dsp_pipeline_t *pipeline, u8 *data, int len)
{
	dsp_pipeline_entry_t *entry;

	if (!pipeline)
		return;

	if (!read_trylock(&pipeline->lock)) {
		printk(KERN_DEBUG "%s: bypassing pipeline because it is locked (RX)\n", __FUNCTION__);
		return;
	}
	list_for_each_entry_reverse(entry, &pipeline->list, list)
		entry->elem->process_rx(entry->p, data, len);
	read_unlock(&pipeline->lock);
}

EXPORT_SYMBOL(mISDN_dsp_element_register);
EXPORT_SYMBOL(mISDN_dsp_element_unregister);


/* $Id: debug.c,v 1.4 2004/01/28 09:40:04 keil Exp $
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#include <linux/module.h>
#include <linux/mISDNif.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include "debug.h"

#define mISDN_STATUS_BUFSIZE 4096

static char tmpbuf[mISDN_STATUS_BUFSIZE];

void
vmISDN_debug(int id, char *head, char *fmt, va_list args)
{
/* if head == NULL the fmt contains the full info */
	char *p = tmpbuf;

	p += sprintf(p,"%d ", id);
	if (head)
		p += sprintf(p, "%s ", head);
	p += vsprintf(p, fmt, args);
	printk(KERN_DEBUG "%s\n", tmpbuf);
} 

void
mISDN_debug(int id, char *head, char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vmISDN_debug(id, head, fmt, args);
	va_end(args);
}

void
mISDN_debugprint(mISDNinstance_t *inst, char *fmt, ...)
{
	logdata_t log;

	va_start(log.args, fmt);
	log.head = inst->name;
	log.fmt = fmt;
	inst->obj->ctrl(inst, MGR_DEBUGDATA | REQUEST, &log);
	va_end(log.args);
}

char *
mISDN_getrev(const char *revision)
{
	char *rev;
	char *p;

	if ((p = strchr(revision, ':'))) {
		rev = p + 2;
		p = strchr(rev, '$');
		if (p)
			*--p = 0;
	} else
		rev = "???";
	return rev;
}

int
mISDN_QuickHex(char *txt, u_char * p, int cnt)
{
	register int i;
	register char *t = txt;
	register u_char w;

	for (i = 0; i < cnt; i++) {
		*t++ = ' ';
		w = (p[i] >> 4) & 0x0f;
		if (w < 10)
			*t++ = '0' + w;
		else
			*t++ = 'A' - 10 + w;
		w = p[i] & 0x0f;
		if (w < 10)
			*t++ = '0' + w;
		else
			*t++ = 'A' - 10 + w;
	}
	*t++ = 0;
	return (t - txt);
}

EXPORT_SYMBOL(vmISDN_debug);
EXPORT_SYMBOL(mISDN_debug);
EXPORT_SYMBOL(mISDN_getrev);
EXPORT_SYMBOL(mISDN_debugprint);
EXPORT_SYMBOL(mISDN_QuickHex);

/* $Id: debug.c,v 1.1 2003/07/21 12:00:04 kkeil Exp $
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#define __NO_VERSION__
#include <linux/mISDNif.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include "debug.h"

#define mISDN_STATUS_BUFSIZE 4096

static char tmpbuf[mISDN_STATUS_BUFSIZE];

void
vmISDNdebug(int id, char *head, char *fmt, va_list args)
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
mISDNdebug(int id, char *head, char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vmISDNdebug(id, head, fmt, args);
	va_end(args);
}

void
debugprint(mISDNinstance_t *inst, char *fmt, ...)
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
		*--p = 0;
	} else
		rev = "???";
	return rev;
}

int
QuickHex(char *txt, u_char * p, int cnt)
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

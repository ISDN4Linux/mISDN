/* $Id: debug.c,v 0.2 2001/02/11 22:57:23 kkeil Exp $
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#define __NO_VERSION__
#include "hisax.h"

#define HISAX_STATUS_BUFSIZE 4096

static char tmpbuf[HISAX_STATUS_BUFSIZE];

void
vhisaxdebug(int id, char *head, char *fmt, va_list args)
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
hisaxdebug(int id, char *head, char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vhisaxdebug(id, head, fmt, args);
	va_end(args);
}

void
debugprint(hisaxinstance_t *inst, char *fmt, ...)
{
	logdata_t log;

	va_start(log.args, fmt);
	log.head = inst->id;
	log.fmt = fmt;
	inst->obj->ctrl(inst, MGR_DEBUGDATA | REQUEST, &log);
	va_end(log.args);
}

char *
HiSax_getrev(const char *revision)
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

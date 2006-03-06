/* $Id: debug.h,v 1.3 2006/03/06 12:52:07 keil Exp $
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */
#ifndef MISDN_DEBUG_H

#define MISDN_DEBUG_MANAGER	0x10000

extern void vmISDN_debug(int id, char *head, char *fmt, va_list args);
extern void mISDN_debug(int id, char *head, char *fmt, ...);
extern char * mISDN_getrev(const char *revision);
extern void mISDN_debugprint(mISDNinstance_t *inst, char *fmt, ...);
extern int mISDN_QuickHex(char *, u_char *, int);
#endif

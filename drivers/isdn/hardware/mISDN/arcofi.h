/* $Id: arcofi.h,v 1.2 2006/03/06 12:52:07 keil Exp $
 *
 * arcofi.h   Ansteuerung ARCOFI 2165
 *
 * Author     Karsten Keil (keil@isdn4linux.de)
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */
 
#define ARCOFI_USE	1

/* states */
#define ARCOFI_NOP	0
#define ARCOFI_TRANSMIT	1
#define ARCOFI_RECEIVE	2
/* events */
#define ARCOFI_START	1
#define ARCOFI_TX_END	2
#define ARCOFI_RX_END	3
#define ARCOFI_TIMEOUT	4

struct arcofi_msg {
	struct arcofi_msg	*next;
	u_char			receive;
	u_char			len;
	u_char			msg[10];
};

extern int arcofi_fsm(channel_t *, int, void *);
extern void init_arcofi(channel_t *);
extern void clear_arcofi(channel_t *);

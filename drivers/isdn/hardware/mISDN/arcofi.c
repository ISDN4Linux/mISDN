/* $Id: arcofi.c,v 1.0 2001/11/02 23:42:26 kkeil Exp $
 *
 * arcofi.c   Ansteuerung ARCOFI 2165
 *
 * Author     Karsten Keil (keil@isdn4linux.de)
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */
 
#define __NO_VERSION__
#include "hisax_hw.h"
#include "hisaxl1.h"
#include "isac.h"
#include "arcofi.h"
#include "debug.h"

#define ARCOFI_TIMER_VALUE	20

static void
add_arcofi_timer(dchannel_t *dch) {
	if (test_and_set_bit(FLG_ARCOFI_TIMER, &dch->DFlags)) {
		del_timer(&dch->hw.isac.arcofitimer);
	}	
	init_timer(&dch->hw.isac.arcofitimer);
	dch->hw.isac.arcofitimer.expires = jiffies + ((ARCOFI_TIMER_VALUE * HZ)/1000);
	add_timer(&dch->hw.isac.arcofitimer);
}

static void
send_arcofi(dchannel_t *dch) {
	u_char val;
	
	add_arcofi_timer(dch);
	dch->hw.isac.mon_txp = 0;
	dch->hw.isac.mon_txc = dch->hw.isac.arcofi_list->len;
	memcpy(dch->hw.isac.mon_tx, dch->hw.isac.arcofi_list->msg, dch->hw.isac.mon_txc);
	switch(dch->hw.isac.arcofi_bc) {
		case 0: break;
		case 1: dch->hw.isac.mon_tx[1] |= 0x40;
			break;
		default: break;
	}
	dch->hw.isac.mocr &= 0x0f;
	dch->hw.isac.mocr |= 0xa0;
	dch->writeisac(dch->inst.data, ISAC_MOCR, dch->hw.isac.mocr);
	val = dch->readisac(dch->inst.data, ISAC_MOSR);
	dch->writeisac(dch->inst.data, ISAC_MOX1, dch->hw.isac.mon_tx[dch->hw.isac.mon_txp++]);
	dch->hw.isac.mocr |= 0x10;
	dch->writeisac(dch->inst.data, ISAC_MOCR, dch->hw.isac.mocr);
}

int
arcofi_fsm(dchannel_t *dch, int event, void *data) {
	if (dch->debug & L1_DEB_MONITOR) {
		debugprint(&dch->inst, "arcofi state %d event %d", dch->hw.isac.arcofi_state, event);
	}
	if (event == ARCOFI_TIMEOUT) {
		dch->hw.isac.arcofi_state = ARCOFI_NOP;
		test_and_set_bit(FLG_ARCOFI_ERROR, &dch->DFlags);
		wake_up(&dch->hw.isac.arcofi_wait);
 		return(1);
	}
	switch (dch->hw.isac.arcofi_state) {
		case ARCOFI_NOP:
			if (event == ARCOFI_START) {
				dch->hw.isac.arcofi_list = data;
				dch->hw.isac.arcofi_state = ARCOFI_TRANSMIT;
				send_arcofi(dch);
			}
			break;
		case ARCOFI_TRANSMIT:
			if (event == ARCOFI_TX_END) {
				if (dch->hw.isac.arcofi_list->receive) {
					add_arcofi_timer(dch);
					dch->hw.isac.arcofi_state = ARCOFI_RECEIVE;
				} else {
					if (dch->hw.isac.arcofi_list->next) {
						dch->hw.isac.arcofi_list =
							dch->hw.isac.arcofi_list->next;
						send_arcofi(dch);
					} else {
						if (test_and_clear_bit(FLG_ARCOFI_TIMER, &dch->DFlags)) {
							del_timer(&dch->hw.isac.arcofitimer);
						}
						dch->hw.isac.arcofi_state = ARCOFI_NOP;
						wake_up(&dch->hw.isac.arcofi_wait);
					}
				}
			}
			break;
		case ARCOFI_RECEIVE:
			if (event == ARCOFI_RX_END) {
				if (dch->hw.isac.arcofi_list->next) {
					dch->hw.isac.arcofi_list =
						dch->hw.isac.arcofi_list->next;
					dch->hw.isac.arcofi_state = ARCOFI_TRANSMIT;
					send_arcofi(dch);
				} else {
					if (test_and_clear_bit(FLG_ARCOFI_TIMER, &dch->DFlags)) {
						del_timer(&dch->hw.isac.arcofitimer);
					}
					dch->hw.isac.arcofi_state = ARCOFI_NOP;
					wake_up(&dch->hw.isac.arcofi_wait);
				}
			}
			break;
		default:
			debugprint(&dch->inst, "Arcofi unknown state %x", dch->hw.isac.arcofi_state);
			return(2);
	}
	return(0);
}

static void
arcofi_timer(dchannel_t *dch) {
	arcofi_fsm(dch, ARCOFI_TIMEOUT, NULL);
}

void
clear_arcofi(dchannel_t *dch) {
	if (test_and_clear_bit(FLG_ARCOFI_TIMER, &dch->DFlags)) {
		del_timer(&dch->hw.isac.arcofitimer);
	}
}

void
init_arcofi(dchannel_t *dch) {
	dch->hw.isac.arcofitimer.function = (void *) arcofi_timer;
	dch->hw.isac.arcofitimer.data = (long) dch;
	init_timer(&dch->hw.isac.arcofitimer);
	test_and_set_bit(HW_ARCOFI, &dch->DFlags);
}

/* $Id: arcofi.c,v 1.6 2004/01/26 22:21:30 keil Exp $
 *
 * arcofi.c   Ansteuerung ARCOFI 2165
 *
 * Author     Karsten Keil (keil@isdn4linux.de)
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */
 
#include "dchannel.h"
#include "layer1.h"
#include "isac.h"
#include "arcofi.h"
#include "debug.h"

#define ARCOFI_TIMER_VALUE	20

static void
add_arcofi_timer(dchannel_t *dch) {
	isac_chip_t	*isac = dch->hw;

	if (test_and_set_bit(FLG_ARCOFI_TIMER, &dch->DFlags)) {
		del_timer(&isac->arcofitimer);
	}	
	init_timer(&isac->arcofitimer);
	isac->arcofitimer.expires = jiffies + ((ARCOFI_TIMER_VALUE * HZ)/1000);
	add_timer(&isac->arcofitimer);
}

static void
send_arcofi(dchannel_t *dch) {
	u_char		val;
	isac_chip_t	*isac = dch->hw;
	
	add_arcofi_timer(dch);
	isac->mon_txp = 0;
	isac->mon_txc = isac->arcofi_list->len;
	memcpy(isac->mon_tx, isac->arcofi_list->msg, isac->mon_txc);
	switch(isac->arcofi_bc) {
		case 0: break;
		case 1: isac->mon_tx[1] |= 0x40;
			break;
		default: break;
	}
	isac->mocr &= 0x0f;
	isac->mocr |= 0xa0;
	dch->write_reg(dch->inst.data, ISAC_MOCR, isac->mocr);
	val = dch->read_reg(dch->inst.data, ISAC_MOSR);
	dch->write_reg(dch->inst.data, ISAC_MOX1, isac->mon_tx[isac->mon_txp++]);
	isac->mocr |= 0x10;
	dch->write_reg(dch->inst.data, ISAC_MOCR, isac->mocr);
}

int
arcofi_fsm(dchannel_t *dch, int event, void *data) {
	isac_chip_t	*isac = dch->hw;

	if (dch->debug & L1_DEB_MONITOR) {
		mISDN_debugprint(&dch->inst, "arcofi state %d event %d", isac->arcofi_state, event);
	}
	if (event == ARCOFI_TIMEOUT) {
		isac->arcofi_state = ARCOFI_NOP;
		test_and_set_bit(FLG_ARCOFI_ERROR, &dch->DFlags);
		wake_up(&isac->arcofi_wait);
 		return(1);
	}
	switch (isac->arcofi_state) {
		case ARCOFI_NOP:
			if (event == ARCOFI_START) {
				isac->arcofi_list = data;
				isac->arcofi_state = ARCOFI_TRANSMIT;
				send_arcofi(dch);
			}
			break;
		case ARCOFI_TRANSMIT:
			if (event == ARCOFI_TX_END) {
				if (isac->arcofi_list->receive) {
					add_arcofi_timer(dch);
					isac->arcofi_state = ARCOFI_RECEIVE;
				} else {
					if (isac->arcofi_list->next) {
						isac->arcofi_list =
							isac->arcofi_list->next;
						send_arcofi(dch);
					} else {
						if (test_and_clear_bit(FLG_ARCOFI_TIMER, &dch->DFlags)) {
							del_timer(&isac->arcofitimer);
						}
						isac->arcofi_state = ARCOFI_NOP;
						wake_up(&isac->arcofi_wait);
					}
				}
			}
			break;
		case ARCOFI_RECEIVE:
			if (event == ARCOFI_RX_END) {
				if (isac->arcofi_list->next) {
					isac->arcofi_list =
						isac->arcofi_list->next;
					isac->arcofi_state = ARCOFI_TRANSMIT;
					send_arcofi(dch);
				} else {
					if (test_and_clear_bit(FLG_ARCOFI_TIMER, &dch->DFlags)) {
						del_timer(&isac->arcofitimer);
					}
					isac->arcofi_state = ARCOFI_NOP;
					wake_up(&isac->arcofi_wait);
				}
			}
			break;
		default:
			mISDN_debugprint(&dch->inst, "Arcofi unknown state %x", isac->arcofi_state);
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
	isac_chip_t	*isac = dch->hw;

	if (test_and_clear_bit(FLG_ARCOFI_TIMER, &dch->DFlags)) {
		del_timer(&isac->arcofitimer);
	}
}

void
init_arcofi(dchannel_t *dch) {
	isac_chip_t	*isac = dch->hw;

	isac->arcofitimer.function = (void *) arcofi_timer;
	isac->arcofitimer.data = (long) dch;
	init_timer(&isac->arcofitimer);
	dch->type |= ISAC_TYPE_ARCOFI;
}

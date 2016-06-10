/* xhfc_su.h
 * mISDN driver for Cologne Chip' XHFC
 *
 * (c) 2007,2008 Copyright Cologne Chip AG
 * Authors : Martin Bachem, Joerg Ciesielski
 * Contact : info@colognechip.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef _XHFC_SU_H_
#define _XHFC_SU_H_

#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/mISDNhw.h>
#include "xhfc24sucd.h"

#define DRIVER_NAME "XHFC"

#ifndef CHIP_ID_2S4U
#define CHIP_ID_2S4U	0x62
#endif
#ifndef CHIP_ID_4SU
#define CHIP_ID_4SU	0x63
#endif
#ifndef CHIP_ID_1SU
#define CHIP_ID_1SU	0x60
#endif
#ifndef CHIP_ID_2SU
#define CHIP_ID_2SU	0x61
#endif


/* define bridge for chip register access */
#define BRIDGE_UNKWOWN	0
#define BRIDGE_PCI2PI	1 /* used at Cologne Chip AG's Evaluation Card */
#define BRIDGE		BRIDGE_PCI2PI

#define MAX_PORT	4
#define CHAN_PER_PORT	4	/* D, B1, B2, PCM */
#define MAX_CHAN	MAX_PORT * CHAN_PER_PORT

/* flags in _u16  port->portmode */
#define PORT_UNUSED		0x0000
#define PORT_MODE_NT		0x0001
#define PORT_MODE_TE		0x0002
#define PORT_MODE_S0		0x0004
#define PORT_MODE_UP0		0x0008
#define PORT_MODE_EXCH_POL	0x0010
#define PORT_MODE_LOOP_B1	0x0020
#define PORT_MODE_LOOP_B2	0x0040
#define PORT_MODE_LOOP_D	0x0080

/* flags in _u8 port->timers */
#define NT_ACTIVATION_TIMER	0x0001

#define PORT_MODE_LOOPS		0xE0	/* mask port mode Loop B1/B2/D */


/* NT / TE defines */
#define NT_T1_COUNT	250	/* number of 4ms interrupts for G2 timeout */
#define CLK_DLY_TE	0x0e	/* CLKDEL in TE mode */
#define CLK_DLY_NT	0x6c	/* CLKDEL in NT mode */
#define STA_ACTIVATE	0x60	/* start activation   in A_SU_WR_STA */
#define STA_DEACTIVATE	0x40	/* start deactivation in A_SU_WR_STA */
#define STA_LOAD	0x10	/* load state in A_SU_WR_STA */
#define LIF_MODE_NT	0x04	/* Line Interface NT mode */
#define XHFC_TIMER_T3	8000	/* 8s activation timer T3 */
#define XHFC_TIMER_T4	500	/* 500ms deactivation timer T4 */

/* xhfc Layer1 physical commands */
#define L1_ACTIVATE_TE		0x00
#define L1_FORCE_DEACTIVATE_TE	0x01
#define L1_ACTIVATE_NT		0x02
#define L1_DEACTIVATE_NT	0x03
#define L1_SET_TESTLOOP_B1		0x04
#define L1_SET_TESTLOOP_B2		0x05
#define L1_SET_TESTLOOP_D		0x06
#define L1_UNSET_TESTLOOP_B1		0x07
#define L1_UNSET_TESTLOOP_B2		0x08
#define L1_UNSET_TESTLOOP_D		0x09


/* xhfc Layer1 Flags (stored in xhfc_port_t->l1_flags) */
#define HFC_L1_ACTIVATING	1
#define HFC_L1_ACTIVATED	2
#define HFC_L1_DEACTTIMER	4
#define HFC_L1_ACTTIMER		8

/* XHFC l1 meta states */
#define XHFC_L1_F0		HW_RESET_IND
#define XHFC_L1_F3		HW_DEACT_IND
#define XHFC_L1_F7		INFO4_P8

#define FIFO_MASK_TX	0x55555555
#define FIFO_MASK_RX	0xAAAAAAAA


/* DEBUG flags, use combined value for module parameter debug=x */
#define DEBUG_HFC_FIFO_ERR	0x00010000
#define DEBUG_HFC_FIFO_STAT	0x00020000
#define DEBUG_HFC_FIFO_DATA	0x00040000
#define DEBUG_HFC_IRQ		0x00100000

#define USE_F0_COUNTER	1
#define DEFAULT_TRANSP_BURST_SZ 128


struct xhfc_port;
struct xhfc_pi;
struct xhfc;

/* port struct for each S/U port */
struct port {
	struct xhfc * xhfc;
	spinlock_t lock;

	int idx;
	char name[20];	/* XHFC_PI0_0_0 = PI no-0 XJFC no 0, port no 0 */

	struct dchannel dch;
	struct dchannel ech;
	struct bchannel bch[2];

	__u8 dpid;	/* D-channel Protocoll ID */
	__u16 mode;	/* NT/TE + ST/U */
	__u8 timers;
	__u8 initdone;
	int nt_timer;
	struct timer_list f7_timer;

	u_long	l1_flags;
	struct timer_list t3_timer; /* timer 3 for activation/deactivation */
	struct timer_list t4_timer; /* timer 4 for activation/deactivation */

	/* chip registers */
	__u8 su_ctrl0;
	__u8 su_ctrl1;
	__u8 su_ctrl2;
	__u8 st_ctrl3;
};

/* hardware structure */
struct xhfc {
	struct xhfc_pi * pi;		/* backpointer to xhfc_pi */
	struct port *port;		/* one for each Line interface */
	char name[15];		/* XHFC_PI0_0 = PI no 0, XHFC no 0 */
	__u8 chipnum;		/* global chip no */
	__u8 chipidx;		/* index in pi->xhfcs[NUM_XHFCS] */
	__u8 param_idx;		/* used to access module param arrays */

	spinlock_t lock;
	spinlock_t lock_irq;
	struct tasklet_struct tasklet;	/* interrupt bottom half */

	__u8 testirq;

	int num_ports;	/* number of S and U interfaces */
	int channels;	/* 4 channels per port: B1/B2/D/E */
	__u8 max_z;	/* fifo depth -1 */

	__u32 irq_cnt;	/* count irqs */
	__u32 f0_cnt;	/* last F0 counter value */
	__u32 f0_akku;	/* akkumulated f0 counter deltas */

	/* chip registers */
	__u8 irq_ctrl;
	__u8 misc_irqmsk;	/* mask of enabled interrupt sources */
	__u8 misc_irq;		/* collect interrupt status bits */

	__u8 su_irqmsk;		/* mask of line intf state change interrupts */
	__u8 su_irq;		/* collect interrupt status bits */
	__u8 ti_wd;		/* timer interval */
	__u8 pcm_md0;
	__u8 pcm_md1;

	__u32 fifo_irq;		/* fifo bl irq */
	__u32 fifo_irqmsk;	/* fifo bl irq */
};


/*
 * interface prototypes exportet for PI implementation, e.g. xhfc_pci2pi
 */
int setup_instance(struct xhfc *hw, struct device *parent);
int release_instance(struct xhfc *hw);
void enable_interrupts(struct xhfc *xhfc);
void disable_interrupts(struct xhfc *xhfc);
irqreturn_t xhfc_interrupt(int intno, void *dev_id);

#endif /* _XHFC_SU_H_ */

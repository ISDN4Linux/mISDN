/* $Id: xhfc_su.h,v 1.2 2006/03/06 12:58:31 keil Exp $
 *
 * mISDN driver for Colognechip xHFC chip
 *
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
#include "channel.h"
#include "xhfc24succ.h"

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

/* flags in _u16  port mode */
#define PORT_UNUSED		0x0000
#define PORT_MODE_NT		0x0001
#define PORT_MODE_TE		0x0002
#define PORT_MODE_S0		0x0004
#define PORT_MODE_UP		0x0008
#define PORT_MODE_EXCH_POL	0x0010
#define PORT_MODE_LOOP		0x0020
#define NT_TIMER		0x8000


/* NT / TE defines */
#define NT_T1_COUNT	25	/* number of 4ms interrupts for G2 timeout */
#define CLK_DLY_TE	0x0e	/* CLKDEL in TE mode */
#define CLK_DLY_NT	0x6c	/* CLKDEL in NT mode */
#define STA_ACTIVATE	0x60	/* start activation   in A_SU_WR_STA */
#define STA_DEACTIVATE	0x40	/* start deactivation in A_SU_WR_STA */
#define LIF_MODE_NT	0x04	/* Line Interface NT mode */
#define XHFC_TIMER_T3	8000	/* 8s activation timer T3 */
#define XHFC_TIMER_T4	500	/* 500ms deactivation timer T4 */

/* xhfc Layer1 physical commands */
#define HFC_L1_ACTIVATE_TE		0x01
#define HFC_L1_FORCE_DEACTIVATE_TE	0x02
#define HFC_L1_ACTIVATE_NT		0x03
#define HFC_L1_DEACTIVATE_NT		0x04
#define HFC_L1_TESTLOOP_B1		0x05
#define HFC_L1_TESTLOOP_B2		0x06

/* xhfc Layer1 Flags (stored in xhfc_port_t->l1_flags) */
#define HFC_L1_ACTIVATING	1
#define HFC_L1_ACTIVATED	2
#define HFC_L1_DEACTTIMER	4
#define HFC_L1_ACTTIMER		8

#define FIFO_MASK_TX	0x55555555
#define FIFO_MASK_RX	0xAAAAAAAA


/* DEBUG flags, use combined value for module parameter debug=x */
#define DEBUG_HFC_INIT		0x0001
#define DEBUG_HFC_MODE		0x0002
#define DEBUG_HFC_S0_STATES	0x0004
#define DEBUG_HFC_IRQ		0x0008
#define DEBUG_HFC_FIFO_ERR	0x0010
#define DEBUG_HFC_DTRACE	0x2000
#define DEBUG_HFC_BTRACE	0x4000	/* very(!) heavy messageslog load */
#define DEBUG_HFC_FIFO		0x8000	/* very(!) heavy messageslog load */

#define USE_F0_COUNTER	1	/* akkumulate F0 counter diff every irq */
#define TRANSP_PACKET_SIZE 0	/* minium tranparent packet size for transmittion to upper layer */


/* private driver_data */
typedef struct {
	int chip_id;
	char *device_name;
} xhfc_param;


/* port struct for each S/U port */
typedef struct {
	int idx;

	__u8 dpid;		/* DChannel Protocoll ID */
	__u16 mode;		/* NT/TE + ST/U */
	int nt_timer;

	u_long	l1_flags;
	struct timer_list t3_timer;	/* timer 3 for activation/deactivation */
	struct timer_list t4_timer;	/* timer 4 for activation/deactivation */

	/* chip registers */
	reg_a_su_ctrl0 su_ctrl0;
	reg_a_su_ctrl1 su_ctrl1;
	reg_a_su_ctrl2 su_ctrl2;
	reg_a_st_ctrl3 st_ctrl3;
} xhfc_port_t;


/* channel struct for each fifo */
typedef struct {
	channel_t   ch;
	xhfc_port_t * port;
} xhfc_chan_t;


struct _xhfx_hw;

/**********************/
/* hardware structure */
/**********************/
typedef struct _xhfx_hw {

	struct list_head list;
	spinlock_t lock;
	struct tasklet_struct tasklet;	/* interrupt bottom half */

	int cardnum;
	__u8 param_idx;		/* used to access module param arrays */
	int ifnum;
	__u8 testirq;
	int irq;
	int iobase;
	int nt_mode;
	u_char *membase;
	u_char *hw_membase;
	
#if BRIDGE == BRIDGE_PCI2PI
	struct pci_dev *pdev;
#endif

	xhfc_param driver_data;
	char card_name[60];

	int chip_id;
	int num_ports;		/* number of S and U interfaces */
	int max_fifo;		/* always 4 fifos per port */
	__u8 max_z;		/* fifo depth -1 */

	xhfc_port_t port[MAX_PORT]; /* one for each Line intercace */
	xhfc_chan_t chan[MAX_CHAN]; /* one each D/B/PCM channel */

	__u32 irq_cnt;	/* count irqs */
	__u32 f0_cnt;	/* last F0 counter value */
	__u32 f0_akku;	/* akkumulated f0 counter deltas */

	/* chip registers */
	reg_r_irq_ctrl 		irq_ctrl;
	reg_r_misc_irqmsk	misc_irqmsk;	/* mask of enabled interrupt sources */
	reg_r_misc_irq		misc_irq;	/* collect interrupt status bits */

	reg_r_su_irqmsk		su_irqmsk;	/* mask of line interface state change interrupts */
	reg_r_su_irq		su_irq;		/* collect interrupt status bits */
	reg_r_ti_wd		ti_wd;		/* timer interval */

	reg_r_pcm_md0		pcm_md0;
	reg_r_pcm_md1		pcm_md1;

	__u32 fifo_irq;		/* fifo bl irq */
	__u32 fifo_irqmsk;	/* fifo bl irq */
	
} xhfc_hw;


/* function prototypes */
int setup_channel(xhfc_hw * hw, __u8 channel, int protocol);
void xhfc_write_fifo(xhfc_hw * hw, __u8 channel);
void xhfc_read_fifo(xhfc_hw * hw, __u8 channel);
void print_fc(xhfc_hw * hw, __u8 fifo);
void setup_fifo(xhfc_hw * hw, __u8 fifo, __u8 conhdlc, __u8 subcfg,
	__u8 fifoctrl, __u8 enable);
void setup_su(xhfc_hw * hw, __u8 pt, __u8 bc, __u8 enable);
           
#endif				/* _XHFC_SU_H_ */

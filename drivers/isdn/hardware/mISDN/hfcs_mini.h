/* $Id: hfcs_mini.h,v 1.2 2006/03/06 12:58:31 keil Exp $
 *
 * mISDN driver for Colognechip HFC-S mini Evaluation Card
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

#ifndef __HFCMINI_H__
#define __HFCMINI_H__

#include "channel.h"
#include "hfcsmcc.h"


#define BRIDGE_UNKWOWN	0
#define BRIDGE_HFCPCI	1

/* use HFC-S PCI as PCI Bridge */
#define HFCBRIDGE	BRIDGE_HFCPCI
#define SPIN_LOCK_HFCSMINI_REGISTER

#define DRIVER_NAME "HFCMINI"
#define CHIP_ID_HFCSMINI CHIP_ID

#define MAX_CHAN	4	/* D, B1, B2, PCM */

/* flags in _u16  port mode */
#define PORT_UNUSED		0x0000
#define PORT_MODE_NT		0x0001
#define PORT_MODE_TE		0x0002
#define PORT_MODE_BUS_MASTER	0x0004
#define PORT_MODE_UP		0x0008
#define PORT_MODE_EXCH_POL	0x0010
#define PORT_MODE_LOOP		0x0020
#define NT_TIMER		0x8000


/* NT / TE defines */
#define NT_T1_COUNT	12	/* number of 8ms interrupts for G2 timeout */
#define CLK_DLY_TE	0x0e	/* CLKDEL in TE mode */
#define CLK_DLY_NT	0x6c	/* CLKDEL in NT mode */
#define STA_ACTIVATE	0x60	/* start activation   in A_SU_WR_STA */
#define STA_DEACTIVATE	0x40	/* start deactivation in A_SU_WR_STA */
#define LIF_MODE_NT	0x04	/* Line Interface NT mode */


/* HFC-S mini Layer1 physical commands */
#define HFC_L1_ACTIVATE_TE		0x01
#define HFC_L1_FORCE_DEACTIVATE_TE	0x02
#define HFC_L1_ACTIVATE_NT		0x03
#define HFC_L1_DEACTIVATE_NT		0x04
#define HFC_L1_TESTLOOP_B1		0x05
#define HFC_L1_TESTLOOP_B2		0x06

/* FIFO handling support values */
#define FIFO_IRQ_OFF	0
#define FIFO_IRQ_ON	1
#define FIFO_DISABLE	0
#define FIFO_ENABLE	1
#define FIFO_MASK_TX	0x55
#define FIFO_MASK_RX	0xAA
#define HDLC_PAR_BCH      0 				/* init value for all B-channel fifos */
#define HDLC_PAR_DCH      (M1_BIT_CNT*2)		/* same for D- and E-channel */
#define CON_HDLC_B_TRANS  (M_HDLC_TRP | M1_TRP_IRQ*2)	/* transparent mode B-channel 32 byte threshold */ 
#define CON_HDLC_B_HDLC   (M1_TRP_IRQ*2)		/* HDLC mode b-channel */
#define CON_HDLC_D_HDLC   (M1_TRP_IRQ*2 | M_IFF)	/* HDLC mode D-channel, 1 fill mode */
#define CON_HDLC_B_LOOP   (M1_TRP_IRQ*2 | M1_DATA_FLOW*6) /* B-channel loopback mode */
#define HFCSMINI_RX_THRESHOLD 32
#define HFCSMINI_TX_THRESHOLD 96
#define DCH_RX_SIZE 267
#define BCH_RX_SIZE 2051
#define BCH_RX_SIZE_TRANS 64

/* DEBUG flags, use combined value for module parameter debug=x */
#define DEBUG_HFC_INIT		0x0001
#define DEBUG_HFC_MODE		0x0002
#define DEBUG_HFC_S0_STATES	0x0004
#define DEBUG_HFC_IRQ		0x0008
#define DEBUG_HFC_FIFO_ERR	0x0010
#define DEBUG_HFC_DTRACE	0x2000
#define DEBUG_HFC_BTRACE	0x4000	/* very(!) heavy messageslog load */
#define DEBUG_HFC_FIFO		0x8000	/* very(!) heavy messageslog load */


/* private driver_data */
typedef struct {
	int chip_id;
	char *device_name;
} hfcsmini_param;

struct _hfcmini_hw;

/**********************/
/* hardware structure */
/**********************/
typedef struct _hfcmini_hw {

	struct list_head list;
	__u32 irq_cnt;	/* count irqs */
	struct tasklet_struct tasklet;	/* interrupt bottom half */	
	spinlock_t mlock; /* mISDN mq lock */
	spinlock_t rlock; /* register access lock */

	int cardnum;
	__u8 param_idx;		/* used to access module param arrays */
	__u8 testirq;
	int irq;
	int iobase;
	u_char *membase;
	u_char *hw_membase;
	struct pci_dev *pdev;
	hfcsmini_param driver_data;
	char card_name[60];

	int max_fifo;		/* always 4 fifos per port */
	__u8 max_z;		/* fifo depth -1 */
	
	channel_t chan[MAX_CHAN];	/* line interfaces */
	
	__u8 fifomask;
	
	/* HFC-S MINI regsister */
	reg_r_chip_id chip_id;	/* Chip ID */
	
	reg_r_pcm_md0 pcm_md0;	/* PCM config */
	reg_r_pcm_md1 pcm_md1;	/* PCM config */
	reg_r_pcm_md2 pcm_md2;	/* PCM config */
	
	reg_r_ti ti;		/* timer interrupt configuration */

	reg_r_fifo_irqmsk	fifo_irqmsk;	/* FIFO interrupt mask */ 
	reg_r_fifo_irq		fifo_irq;	/* FIFO interrupt state */

	reg_r_misc_irqmsk	misc_irqmsk;	/* MISC interrupt mask */
	reg_r_misc_irq		misc_irq;	/* MISC interrupt state */

	reg_r_st_ctrl0		st_ctrl0;
	reg_r_st_ctrl2		st_ctrl2;	
	
	int nt_timer;
	__u8 dpid;		/* DChannel Protocoll ID */
	__u16 portmode;		/* NT/TE */

} hfcsmini_hw;


/* function prototypes */
int setup_channel(hfcsmini_hw * hw, __u8 channel, int protocol);
void hfcsmini_write_fifo(hfcsmini_hw * hw, __u8 channel);
void hfcsmini_read_fifo(hfcsmini_hw * hw, __u8 channel);
void print_fc(hfcsmini_hw * hw, __u8 fifo);
void setup_fifo(hfcsmini_hw * hw, int fifo, __u8 hdlcreg, __u8 con_reg, __u8 irq_enable, __u8 enable);
void setup_s(hfcsmini_hw * hw, __u8 bc, __u8 enable);
void disable_interrupts(hfcsmini_hw * hw);
void enable_interrupts(hfcsmini_hw * hw);
static void release_card(hfcsmini_hw * hw);


#if HFCBRIDGE == BRIDGE_HFCPCI
int init_pci_bridge(hfcsmini_hw * hw);
#endif

/* HFC-S MINI register access functions */
static inline void hfcsmini_sel_reg(hfcsmini_hw * hw, __u8 reg_addr);
static inline __u8 read_hfcsmini(hfcsmini_hw * hw, __u8 reg_addr);
static inline __u8 read_hfcsmini_irq(hfcsmini_hw * hw, __u8 reg_addr);
static inline __u8 read_hfcsmini_stable(hfcsmini_hw * hw, __u8 reg_addr);
static inline void write_hfcsmini(hfcsmini_hw * hw, __u8 reg_addr, __u8 value);


#endif				/* __hfcsmini_H__ */

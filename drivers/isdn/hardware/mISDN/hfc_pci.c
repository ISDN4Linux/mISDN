/* $Id: hfc_pci.c,v 1.38 2004/06/17 12:31:12 keil Exp $

 * hfc_pci.c     low level driver for CCD's hfc-pci based cards
 *
 * Author     Werner Cornelius (werner@isdn4linux.de)
 *            based on existing driver for CCD hfc ISA cards
 *            type approval valid for HFC-S PCI A based card 
 *
 * Copyright 1999  by Werner Cornelius (werner@isdn-development.de)
 * Copyright 2001  by Karsten Keil (keil@isdn4linux.de)
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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#include "dchannel.h"
#include "bchannel.h"
#include "hfc_pci.h"
#include "layer1.h"
#include "helper.h"
#include "debug.h"
#include <linux/isdn_compat.h>

#define SPIN_DEBUG
#define LOCK_STATISTIC
#include "hw_lock.h"

#define HFC_INFO(txt)	printk(KERN_DEBUG txt)

extern const char *CardType[];

static const char *hfcpci_revision = "$Revision: 1.38 $";

/* table entry in the PCI devices list */
typedef struct {
	int vendor_id;
	int device_id;
	char *vendor_name;
	char *card_name;
} PCI_ENTRY;

#define NT_T1_COUNT	20	/* number of 3.125ms interrupts for G2 timeout */
#define CLKDEL_TE	0x0e	/* CLKDEL in TE mode */
#define CLKDEL_NT	0x6c	/* CLKDEL in NT mode */

#ifndef PCI_VENDOR_ID_CCD
#define PCI_VENDOR_ID_CCD		0x1397
#define PCI_DEVICE_ID_CCD_2BD0		0x2BD0
#define PCI_DEVICE_ID_CCD_B000		0xB000
#define PCI_DEVICE_ID_CCD_B006		0xB006
#define PCI_DEVICE_ID_CCD_B007		0xB007
#define PCI_DEVICE_ID_CCD_B008		0xB008
#define PCI_DEVICE_ID_CCD_B009		0xB009
#define PCI_DEVICE_ID_CCD_B00A		0xB00A
#define PCI_DEVICE_ID_CCD_B00B		0xB00B
#define PCI_DEVICE_ID_CCD_B00C		0xB00C
#define PCI_DEVICE_ID_CCD_B100		0xB100

#define PCI_VENDOR_ID_ASUSTEK		0x1043
#define PCI_DEVICE_ID_ASUSTEK_0675	0x0675

#define PCI_VENDOR_ID_BERKOM		0x0871
#define PCI_DEVICE_ID_BERKOM_A1T	0xFFA1
#define PCI_DEVICE_ID_BERKOM_T_CONCEPT	0xFFA2

#define PCI_VENDOR_ID_ANIGMA		0x1051
#define PCI_DEVICE_ID_ANIGMA_MC145575	0x0100

#define PCI_VENDOR_ID_ZOLTRIX		0x15b0
#define PCI_DEVICE_ID_ZOLTRIX_2BD0	0x2BD0

#define PCI_DEVICE_ID_DIGI_DF_M_IOM2_E	0x0070
#define PCI_DEVICE_ID_DIGI_DF_M_E	0x0071
#define PCI_DEVICE_ID_DIGI_DF_M_IOM2_A	0x0072
#define PCI_DEVICE_ID_DIGI_DF_M_A	0x0073

#define PCI_VENDOR_ID_ABOCOM		0x13D1
#define PCI_DEVICE_ID_ABOCOM_2BD1	0x2BD1
#endif

static const PCI_ENTRY id_list[] =
{
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_2BD0, "CCD/Billion/Asuscom", "2BD0"},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B000, "Billion", "B000"},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B006, "Billion", "B006"},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B007, "Billion", "B007"},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B008, "Billion", "B008"},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B009, "Billion", "B009"},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B00A, "Billion", "B00A"},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B00B, "Billion", "B00B"},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B00C, "Billion", "B00C"},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B100, "Seyeon", "B100"},
	{PCI_VENDOR_ID_ABOCOM, PCI_DEVICE_ID_ABOCOM_2BD1, "Abocom/Magitek", "2BD1"},
	{PCI_VENDOR_ID_ASUSTEK, PCI_DEVICE_ID_ASUSTEK_0675, "Asuscom/Askey", "675"},
	{PCI_VENDOR_ID_BERKOM, PCI_DEVICE_ID_BERKOM_T_CONCEPT, "German telekom", "T-Concept"},
	{PCI_VENDOR_ID_BERKOM, PCI_DEVICE_ID_BERKOM_A1T, "German telekom", "A1T"},
	{PCI_VENDOR_ID_ANIGMA, PCI_DEVICE_ID_ANIGMA_MC145575, "Motorola MC145575", "MC145575"},
	{PCI_VENDOR_ID_ZOLTRIX, PCI_DEVICE_ID_ZOLTRIX_2BD0, "Zoltrix", "2BD0"},
	{PCI_VENDOR_ID_DIGI, PCI_DEVICE_ID_DIGI_DF_M_IOM2_E,"Digi International", "Digi DataFire Micro V IOM2 (Europe)"},
	{PCI_VENDOR_ID_DIGI, PCI_DEVICE_ID_DIGI_DF_M_E,"Digi International", "Digi DataFire Micro V (Europe)"},
	{PCI_VENDOR_ID_DIGI, PCI_DEVICE_ID_DIGI_DF_M_IOM2_A,"Digi International", "Digi DataFire Micro V IOM2 (North America)"},
	{PCI_VENDOR_ID_DIGI, PCI_DEVICE_ID_DIGI_DF_M_A,"Digi International", "Digi DataFire Micro V (North America)"},
	{0, 0, NULL, NULL},
};


struct hfcPCI_hw {
	unsigned char		cirm;
	unsigned char		ctmt;
	unsigned char		clkdel;
	unsigned char		states;
	unsigned char		conn;
	unsigned char		mst_m;
	unsigned char		int_m1;
	unsigned char		int_m2;
	unsigned char		sctrl;
	unsigned char		sctrl_r;
	unsigned char		sctrl_e;
	unsigned char		trm;
	unsigned char		fifo_en;
	unsigned char		bswapped;
	unsigned char		nt_mode;
	int			nt_timer;
	struct pci_dev		*dev;
	unsigned char		*pci_io; /* start of PCI IO memory */
	void			*share_start; /* shared memory for Fifos start */
	void			*fifos; /* FIFO memory */ 
	int			last_bfifo_cnt[2]; /* marker saving last b-fifo frame count */
	struct timer_list	timer;
};

typedef struct hfcPCI_hw	hfcPCI_hw_t;

#define SPIN_DEBUG
#define	HFC_CFG_MASTER		1
#define HFC_CFG_SLAVE		2
#define	HFC_CFG_PCM		3
#define HFC_CFG_2HFC		4
#define HFC_CFG_SLAVEHFC	5
#define HFC_CFG_NEG_F0		6
#define HFC_CFG_SW_DD_DU	7

typedef struct _hfc_pci {
	struct list_head	list;
	u_char			subtyp;
	u_char			chanlimit;
	u_long			cfg;
	u_int			irq;
	u_int			irqcnt;
	hfcPCI_hw_t		hw;
	mISDN_HWlock_t		lock;
	dchannel_t		dch;
	bchannel_t		bch[2];
} hfc_pci_t;


static int lock_dev(void *data, int nowait)
{
	register mISDN_HWlock_t	*lock = &((hfc_pci_t *)data)->lock;
	
	return(lock_HW(lock, nowait));
} 

static void unlock_dev(void *data)
{
	register mISDN_HWlock_t	*lock = &((hfc_pci_t *)data)->lock;

	unlock_HW(lock);
}

/* Interface functions */

/******************************************/
/* free hardware resources used by driver */
/******************************************/
void
release_io_hfcpci(hfc_pci_t *hc)
{
	hc->hw.int_m2 = 0;	/* interrupt output off ! */
	Write_hfc(hc, HFCPCI_INT_M2, hc->hw.int_m2);
	Write_hfc(hc, HFCPCI_CIRM, HFCPCI_RESET);		/* Reset On */
	mdelay(10);						/* Timeout 10ms */
	hc->hw.cirm = 0; /* Reset Off */
	Write_hfc(hc, HFCPCI_CIRM, hc->hw.cirm);
	pci_write_config_word(hc->hw.dev, PCI_COMMAND, 0);	/* disable memory mapped ports + busmaster */
	del_timer(&hc->hw.timer);
	kfree(hc->hw.share_start);
	hc->hw.share_start = NULL;
	iounmap((void *)hc->hw.pci_io);
}


/********************************************************************************/
/* function called to reset the HFC PCI chip. A complete software reset of chip */
/* and fifos is done.                                                           */
/********************************************************************************/
static void
reset_hfcpci(hfc_pci_t *hc)
{
	u_char	val;
	int	cnt = 0;

	HFC_INFO("reset_hfcpci: entered\n");
	val = Read_hfc(hc, HFCPCI_CHIP_ID);
	printk(KERN_INFO "HFC_PCI: resetting HFC ChipId(%x)\n", val);
	pci_write_config_word(hc->hw.dev, PCI_COMMAND, PCI_ENA_MEMIO);	/* enable memory mapped ports, disable busmaster */
	hc->hw.int_m2 = 0;		/* interrupt output off ! */
	Write_hfc(hc, HFCPCI_INT_M2, hc->hw.int_m2);
	pci_write_config_word(hc->hw.dev, PCI_COMMAND, PCI_ENA_MEMIO + PCI_ENA_MASTER);	/* enable memory ports + busmaster */
	val = Read_hfc(hc, HFCPCI_STATUS);
	printk(KERN_DEBUG "HFC-PCI status(%x) before reset\n", val);
	hc->hw.cirm = HFCPCI_RESET;	/* Reset On */
	Write_hfc(hc, HFCPCI_CIRM, hc->hw.cirm);
	set_current_state(TASK_UNINTERRUPTIBLE);
	mdelay(10);			/* Timeout 10ms */
	hc->hw.cirm = 0;		/* Reset Off */
	Write_hfc(hc, HFCPCI_CIRM, hc->hw.cirm);
	val = Read_hfc(hc, HFCPCI_STATUS);
	printk(KERN_DEBUG "HFC-PCI status(%x) after reset\n", val);
	while (cnt < 50000) { /* max 50000 us */
 		udelay(5);
		cnt += 5;
		val = Read_hfc(hc, HFCPCI_STATUS);
		if (!(val & 2))
			break;
	}
	printk(KERN_DEBUG "HFC-PCI status(%x) after %dus\n", val, cnt);

	hc->hw.fifo_en = 0x30;	/* only D fifos enabled */

	hc->hw.bswapped = 0;	/* no exchange */
	hc->hw.ctmt = HFCPCI_TIM3_125 | HFCPCI_AUTO_TIMER;
	hc->hw.trm = HFCPCI_BTRANS_THRESMASK;	/* no echo connect , threshold */
	hc->hw.sctrl = 0x40;	/* set tx_lo mode, error in datasheet ! */
	hc->hw.sctrl_r = 0;
	hc->hw.sctrl_e = HFCPCI_AUTO_AWAKE;	/* S/T Auto awake */
	hc->hw.mst_m = 0;
	if (test_bit(HFC_CFG_MASTER, &hc->cfg))
		hc->hw.mst_m |= HFCPCI_MASTER;	/* HFC Master Mode */
	if (test_bit(HFC_CFG_NEG_F0, &hc->cfg))
		hc->hw.mst_m |= HFCPCI_F0_NEGATIV;
	if (hc->hw.nt_mode) {
		hc->hw.clkdel = CLKDEL_NT;	/* ST-Bit delay for NT-Mode */
		hc->hw.sctrl |= SCTRL_MODE_NT;	/* NT-MODE */
		hc->hw.states = 1;		/* G1 */
	} else {
		hc->hw.clkdel = CLKDEL_TE;	/* ST-Bit delay for TE-Mode */
		hc->hw.states = 2;		/* F2 */
	}
	Write_hfc(hc, HFCPCI_FIFO_EN, hc->hw.fifo_en);
	Write_hfc(hc, HFCPCI_TRM, hc->hw.trm);
	Write_hfc(hc, HFCPCI_CLKDEL, hc->hw.clkdel);
	Write_hfc(hc, HFCPCI_SCTRL_E, hc->hw.sctrl_e);
	Write_hfc(hc, HFCPCI_CTMT, hc->hw.ctmt);

	hc->hw.int_m1 = HFCPCI_INTS_DTRANS | HFCPCI_INTS_DREC |
	    HFCPCI_INTS_L1STATE | HFCPCI_INTS_TIMER;
	Write_hfc(hc, HFCPCI_INT_M1, hc->hw.int_m1);

	/* Clear already pending ints */
	if (Read_hfc(hc, HFCPCI_INT_S1));

	Write_hfc(hc, HFCPCI_STATES, HFCPCI_LOAD_STATE | hc->hw.states);
	udelay(10);
	Write_hfc(hc, HFCPCI_STATES, hc->hw.states);

	Write_hfc(hc, HFCPCI_MST_MODE, hc->hw.mst_m);
	Write_hfc(hc, HFCPCI_SCTRL, hc->hw.sctrl);
	Write_hfc(hc, HFCPCI_SCTRL_R, hc->hw.sctrl_r);

	/* Init GCI/IOM2 in master mode */
	/* Slots 0 and 1 are set for B-chan 1 and 2 */
	/* D- and monitor/CI channel are not enabled */
	/* STIO1 is used as output for data, B1+B2 from ST->IOM+HFC */
	/* STIO2 is used as data input, B1+B2 from IOM->ST */
	/* ST B-channel send disabled -> continous 1s */
	/* The IOM slots are always enabled */
	if (test_bit(HFC_CFG_PCM, &hc->cfg)) {
		/* set data flow directions: connect B1,B2: HFC to/from PCM */
		hc->hw.conn = 0x09;
	} else {
		hc->hw.conn = 0x36;	/* set data flow directions */ 
		if (test_bit(HFC_CFG_SW_DD_DU, &hc->cfg)) {
			Write_hfc(hc, HFCPCI_B1_SSL, 0xC0);
			Write_hfc(hc, HFCPCI_B2_SSL, 0xC1);
			Write_hfc(hc, HFCPCI_B1_RSL, 0xC0);
			Write_hfc(hc, HFCPCI_B2_RSL, 0xC1);
		} else {
			Write_hfc(hc, HFCPCI_B1_SSL, 0x80);
			Write_hfc(hc, HFCPCI_B2_SSL, 0x81);
			Write_hfc(hc, HFCPCI_B1_RSL, 0x80);
			Write_hfc(hc, HFCPCI_B2_RSL, 0x81);
		}
	}
	Write_hfc(hc, HFCPCI_CONNECT, hc->hw.conn);
	val = Read_hfc(hc, HFCPCI_INT_S2);
}

/***************************************************/
/* Timer function called when kernel timer expires */
/***************************************************/
static void
hfcpci_Timer(hfc_pci_t *hc)
{
	hc->hw.timer.expires = jiffies + 75;
	/* WD RESET */
/*      WriteReg(hc, HFCD_DATA, HFCD_CTMT, hc->hw.ctmt | 0x80);
   add_timer(&hc->hw.timer);
 */
}


/************************************************/
/* select a b-channel entry matching and active */
/************************************************/
static
bchannel_t *
Sel_BCS(hfc_pci_t *hc, int channel)
{
	if (hc->bch[0].protocol && (hc->bch[0].channel & channel))
		return (&hc->bch[0]);
	else if (hc->bch[1].protocol && (hc->bch[1].channel & channel))
		return (&hc->bch[1]);
	else
		return (NULL);
}

/***************************************/
/* clear the desired B-channel rx fifo */
/***************************************/
static void hfcpci_clear_fifo_rx(hfc_pci_t *hc, int fifo)
{       u_char fifo_state;
        bzfifo_type *bzr;

	if (fifo) {
	        bzr = &((fifo_area *) (hc->hw.fifos))->b_chans.rxbz_b2;
		fifo_state = hc->hw.fifo_en & HFCPCI_FIFOEN_B2RX;
	} else {
	        bzr = &((fifo_area *) (hc->hw.fifos))->b_chans.rxbz_b1;
		fifo_state = hc->hw.fifo_en & HFCPCI_FIFOEN_B1RX;
	}
	if (fifo_state)
	        hc->hw.fifo_en ^= fifo_state;
	Write_hfc(hc, HFCPCI_FIFO_EN, hc->hw.fifo_en);
	hc->hw.last_bfifo_cnt[fifo] = 0;
	bzr->f1 = MAX_B_FRAMES;
	bzr->f2 = bzr->f1;	/* init F pointers to remain constant */
	bzr->za[MAX_B_FRAMES].z1 = B_FIFO_SIZE + B_SUB_VAL - 1;
	bzr->za[MAX_B_FRAMES].z2 = bzr->za[MAX_B_FRAMES].z1;
	if (fifo_state)
	        hc->hw.fifo_en |= fifo_state;
	Write_hfc(hc, HFCPCI_FIFO_EN, hc->hw.fifo_en);
}   

/***************************************/
/* clear the desired B-channel tx fifo */
/***************************************/
static void hfcpci_clear_fifo_tx(hfc_pci_t *hc, int fifo)
{       u_char fifo_state;
        bzfifo_type *bzt;

	if (fifo) {
	        bzt = &((fifo_area *) (hc->hw.fifos))->b_chans.txbz_b2;
		fifo_state = hc->hw.fifo_en & HFCPCI_FIFOEN_B2TX;
	} else {
	        bzt = &((fifo_area *) (hc->hw.fifos))->b_chans.txbz_b1;
		fifo_state = hc->hw.fifo_en & HFCPCI_FIFOEN_B1TX;
	}
	if (fifo_state)
	        hc->hw.fifo_en ^= fifo_state;
	Write_hfc(hc, HFCPCI_FIFO_EN, hc->hw.fifo_en);
	if (hc->bch[fifo].debug & L1_DEB_HSCX)
		mISDN_debugprint(&hc->bch[fifo].inst, "hfcpci_clear_fifo_tx%d f1(%x) f2(%x) z1(%x) z2(%x) state(%x)",
				fifo, bzt->f1, bzt->f2, bzt->za[MAX_B_FRAMES].z1, bzt->za[MAX_B_FRAMES].z2, fifo_state);
	bzt->f2 = MAX_B_FRAMES;
	bzt->f1 = bzt->f2;	/* init F pointers to remain constant */
	bzt->za[MAX_B_FRAMES].z1 = B_FIFO_SIZE + B_SUB_VAL - 1;
	bzt->za[MAX_B_FRAMES].z2 = bzt->za[MAX_B_FRAMES].z1 - 1;
	if (fifo_state)
	        hc->hw.fifo_en |= fifo_state;
	Write_hfc(hc, HFCPCI_FIFO_EN, hc->hw.fifo_en);
	if (hc->bch[fifo].debug & L1_DEB_HSCX)
		mISDN_debugprint(&hc->bch[fifo].inst, "hfcpci_clear_fifo_tx%d f1(%x) f2(%x) z1(%x) z2(%x)",
				fifo, bzt->f1, bzt->f2, bzt->za[MAX_B_FRAMES].z1, bzt->za[MAX_B_FRAMES].z2);
}   

/*********************************************/
/* read a complete B-frame out of the buffer */
/*********************************************/
static struct sk_buff
*
hfcpci_empty_fifo(bchannel_t *bch, bzfifo_type * bz, u_char * bdata, int count)
{
	u_char		*ptr, *ptr1, new_f2;
	struct sk_buff	*skb;
	int		total, maxlen, new_z2;
	z_type		*zp;

	if ((bch->debug & L1_DEB_HSCX) && !(bch->debug & L1_DEB_HSCX_FIFO))
		mISDN_debugprint(&bch->inst, "hfcpci_empty_fifo");
	zp = &bz->za[bz->f2];	/* point to Z-Regs */
	new_z2 = zp->z2 + count;	/* new position in fifo */
	if (new_z2 >= (B_FIFO_SIZE + B_SUB_VAL))
		new_z2 -= B_FIFO_SIZE;	/* buffer wrap */
	new_f2 = (bz->f2 + 1) & MAX_B_FRAMES;
	if ((count > MAX_DATA_SIZE + 3) || (count < 4) ||
	    (*(bdata + (zp->z1 - B_SUB_VAL)))) {
		if (bch->debug & L1_DEB_WARN)
			mISDN_debugprint(&bch->inst, "hfcpci_empty_fifo: incoming packet invalid length %d or crc", count);
#ifdef ERROR_STATISTIC
		bch->err_inv++;
#endif
		bz->za[new_f2].z2 = new_z2;
		bz->f2 = new_f2;	/* next buffer */
		skb = NULL;
	} else if (!(skb = alloc_stack_skb(count - 3, bch->up_headerlen)))
		printk(KERN_WARNING "HFCPCI: receive out of memory\n");
	else {
		total = count;
		count -= 3;
		ptr = skb_put(skb, count);

		if (zp->z2 + count <= B_FIFO_SIZE + B_SUB_VAL)
			maxlen = count;		/* complete transfer */
		else
			maxlen = B_FIFO_SIZE + B_SUB_VAL - zp->z2;	/* maximum */

		ptr1 = bdata + (zp->z2 - B_SUB_VAL);	/* start of data */
		memcpy(ptr, ptr1, maxlen);	/* copy data */
		count -= maxlen;

		if (count) {	/* rest remaining */
			ptr += maxlen;
			ptr1 = bdata;	/* start of buffer */
			memcpy(ptr, ptr1, count);	/* rest */
		}
		bz->za[new_f2].z2 = new_z2;
		bz->f2 = new_f2;	/* next buffer */

	}
	return (skb);
}

/*******************************/
/* D-channel receive procedure */
/*******************************/
static
int
receive_dmsg(hfc_pci_t *hc)
{
	struct sk_buff	*skb;
	dchannel_t	*dch = &hc->dch;
	int		maxlen;
	int		rcnt, total;
	int		count = 5;
	u_char		*ptr, *ptr1;
	dfifo_type	*df;
	z_type		*zp;

	df = &((fifo_area *) (hc->hw.fifos))->d_chan.d_rx;
	while (((df->f1 & D_FREG_MASK) != (df->f2 & D_FREG_MASK)) && count--) {
		zp = &df->za[df->f2 & D_FREG_MASK];
		rcnt = zp->z1 - zp->z2;
		if (rcnt < 0)
			rcnt += D_FIFO_SIZE;
		rcnt++;
		if (dch->debug & L1_DEB_ISAC)
			mISDN_debugprint(&dch->inst, "hfcpci recd f1(%d) f2(%d) z1(%x) z2(%x) cnt(%d)",
				df->f1, df->f2, zp->z1, zp->z2, rcnt);

		if ((rcnt > MAX_DFRAME_LEN + 3) || (rcnt < 4) ||
		    (df->data[zp->z1])) {
			if (dch->debug & L1_DEB_WARN)
				mISDN_debugprint(&dch->inst, "empty_fifo hfcpci paket inv. len %d or crc %d", rcnt, df->data[zp->z1]);
#ifdef ERROR_STATISTIC
			cs->err_rx++;
#endif
			df->f2 = ((df->f2 + 1) & MAX_D_FRAMES) | (MAX_D_FRAMES + 1);	/* next buffer */
			df->za[df->f2 & D_FREG_MASK].z2 = (zp->z2 + rcnt) & (D_FIFO_SIZE - 1);
		} else if ((skb = alloc_stack_skb(rcnt - 3, dch->up_headerlen))) {
			total = rcnt;
			rcnt -= 3;
			ptr = skb_put(skb, rcnt);

			if (zp->z2 + rcnt <= D_FIFO_SIZE)
				maxlen = rcnt;	/* complete transfer */
			else
				maxlen = D_FIFO_SIZE - zp->z2;	/* maximum */

			ptr1 = df->data + zp->z2;	/* start of data */
			memcpy(ptr, ptr1, maxlen);	/* copy data */
			rcnt -= maxlen;

			if (rcnt) {	/* rest remaining */
				ptr += maxlen;
				ptr1 = df->data;	/* start of buffer */
				memcpy(ptr, ptr1, rcnt);	/* rest */
			}
			df->f2 = ((df->f2 + 1) & MAX_D_FRAMES) | (MAX_D_FRAMES + 1);	/* next buffer */
			df->za[df->f2 & D_FREG_MASK].z2 = (zp->z2 + total) & (D_FIFO_SIZE - 1);

			if (dch->debug & L1_DEB_ISAC_FIFO) {
				char *t = dch->dlog;

				count = skb->len;
				ptr = skb->data;
				t += sprintf(t, "hfcD_empty_fifo cnt %d", count);
				mISDN_QuickHex(t, ptr, count);
				mISDN_debugprint(&dch->inst, dch->dlog);
			}
			skb_queue_tail(&dch->rqueue, skb);
			dchannel_sched_event(dch, D_RCVBUFREADY);
		} else
			printk(KERN_WARNING "HFC-PCI: D receive out of memory\n");
	}
	return (1);
}

/*******************************************************************************/
/* check for transparent receive data and read max one threshold size if avail */
/*******************************************************************************/
int
hfcpci_empty_fifo_trans(bchannel_t *bch, bzfifo_type * bz, u_char * bdata)
{
	unsigned short	*z1r, *z2r;
	int		new_z2, fcnt, maxlen;
	struct sk_buff	*skb;
	u_char		*ptr, *ptr1;

	z1r = &bz->za[MAX_B_FRAMES].z1;		/* pointer to z reg */
	z2r = z1r + 1;

	if (!(fcnt = *z1r - *z2r))
		return (0);	/* no data avail */

	if (fcnt <= 0)
		fcnt += B_FIFO_SIZE;	/* bytes actually buffered */
	if (fcnt > HFCPCI_BTRANS_THRESHOLD)
		fcnt = HFCPCI_BTRANS_THRESHOLD;		/* limit size */

	new_z2 = *z2r + fcnt;	/* new position in fifo */
	if (new_z2 >= (B_FIFO_SIZE + B_SUB_VAL))
		new_z2 -= B_FIFO_SIZE;	/* buffer wrap */

	if (!(skb = alloc_stack_skb(fcnt, bch->up_headerlen)))
		printk(KERN_WARNING "HFCPCI: receive out of memory\n");
	else {
		ptr = skb_put(skb, fcnt);
		if (*z2r + fcnt <= B_FIFO_SIZE + B_SUB_VAL)
			maxlen = fcnt;	/* complete transfer */
		else
			maxlen = B_FIFO_SIZE + B_SUB_VAL - *z2r;	/* maximum */

		ptr1 = bdata + (*z2r - B_SUB_VAL);	/* start of data */
		memcpy(ptr, ptr1, maxlen);	/* copy data */
		fcnt -= maxlen;

		if (fcnt) {	/* rest remaining */
			ptr += maxlen;
			ptr1 = bdata;	/* start of buffer */
			memcpy(ptr, ptr1, fcnt);	/* rest */
		}
		skb_queue_tail(&bch->rqueue, skb);
		bch_sched_event(bch, B_RCVBUFREADY);
	}

	*z2r = new_z2;		/* new position */
	return (1);
}				/* hfcpci_empty_fifo_trans */

/**********************************/
/* B-channel main receive routine */
/**********************************/
void
main_rec_hfcpci(bchannel_t *bch)
{
	hfc_pci_t	*hc = bch->inst.data;
	int		rcnt, real_fifo;
	int		receive, count = 5;
	struct sk_buff	*skb;
	bzfifo_type	*bz;
	u_char		*bdata;
	z_type		*zp;


	if ((bch->channel & 2) && (!hc->hw.bswapped)) {
		bz = &((fifo_area *) (hc->hw.fifos))->b_chans.rxbz_b2;
		bdata = ((fifo_area *) (hc->hw.fifos))->b_chans.rxdat_b2;
		real_fifo = 1;
	} else {
		bz = &((fifo_area *) (hc->hw.fifos))->b_chans.rxbz_b1;
		bdata = ((fifo_area *) (hc->hw.fifos))->b_chans.rxdat_b1;
		real_fifo = 0;
	}
      Begin:
	count--;
	if (bz->f1 != bz->f2) {
		if (bch->debug & L1_DEB_HSCX)
			mISDN_debugprint(&bch->inst, "hfcpci rec ch(%x) f1(%d) f2(%d)",
				bch->channel, bz->f1, bz->f2);
		zp = &bz->za[bz->f2];

		rcnt = zp->z1 - zp->z2;
		if (rcnt < 0)
			rcnt += B_FIFO_SIZE;
		rcnt++;
		if (bch->debug & L1_DEB_HSCX)
			mISDN_debugprint(&bch->inst, "hfcpci rec ch(%x) z1(%x) z2(%x) cnt(%d)",
				bch->channel, zp->z1, zp->z2, rcnt);
		if ((skb = hfcpci_empty_fifo(bch, bz, bdata, rcnt))) {
			skb_queue_tail(&bch->rqueue, skb);
			bch_sched_event(bch, B_RCVBUFREADY);
		}
		rcnt = bz->f1 - bz->f2;
		if (rcnt < 0)
			rcnt += MAX_B_FRAMES + 1;
		if (hc->hw.last_bfifo_cnt[real_fifo] > rcnt + 1) {
		        rcnt = 0;
			hfcpci_clear_fifo_rx(hc, real_fifo);
		}
		hc->hw.last_bfifo_cnt[real_fifo] = rcnt;
		if (rcnt > 1)
			receive = 1;
		else
			receive = 0;
	} else if (bch->protocol == ISDN_PID_L1_B_64TRANS)
		receive = hfcpci_empty_fifo_trans(bch, bz, bdata);
	else
		receive = 0;
	if (count && receive)
		goto Begin;
	return;
}

/**************************/
/* D-channel send routine */
/**************************/
static void
hfcpci_fill_dfifo(hfc_pci_t *hc)
{
	dchannel_t	*dch = &hc->dch;
	int		fcnt;
	int		count, new_z1, maxlen;
	dfifo_type	*df;
	u_char		*src, *dst, new_f1;

	if ((dch->debug & L1_DEB_ISAC) && !(dch->debug & L1_DEB_ISAC_FIFO))
		mISDN_debugprint(&dch->inst, "hfcpci_fill_dfifo");

	count = dch->tx_len - dch->tx_idx;
	if (count <= 0)
		return;
	df = &((fifo_area *) (hc->hw.fifos))->d_chan.d_tx;

	if (dch->debug & L1_DEB_ISAC_FIFO)
		mISDN_debugprint(&dch->inst, "hfcpci_fill_Dfifo f1(%d) f2(%d) z1(f1)(%x)",
			df->f1, df->f2,
			df->za[df->f1 & D_FREG_MASK].z1);
	fcnt = df->f1 - df->f2;	/* frame count actually buffered */
	if (fcnt < 0)
		fcnt += (MAX_D_FRAMES + 1);	/* if wrap around */
	if (fcnt > (MAX_D_FRAMES - 1)) {
		if (dch->debug & L1_DEB_ISAC)
			mISDN_debugprint(&dch->inst, "hfcpci_fill_Dfifo more as 14 frames");
#ifdef ERROR_STATISTIC
		cs->err_tx++;
#endif
		return;
	}
	/* now determine free bytes in FIFO buffer */
	maxlen = df->za[df->f2 & D_FREG_MASK].z2 - df->za[df->f1 & D_FREG_MASK].z1 - 1;
	if (maxlen <= 0)
		maxlen += D_FIFO_SIZE;	/* count now contains available bytes */

	if (dch->debug & L1_DEB_ISAC)
		mISDN_debugprint(&dch->inst, "hfcpci_fill_Dfifo count(%ld/%d)",
			count, maxlen);
	if (count > maxlen) {
		if (dch->debug & L1_DEB_ISAC)
			mISDN_debugprint(&dch->inst, "hfcpci_fill_Dfifo no fifo mem");
		return;
	}
	new_z1 = (df->za[df->f1 & D_FREG_MASK].z1 + count) & (D_FIFO_SIZE - 1);
	new_f1 = ((df->f1 + 1) & D_FREG_MASK) | (D_FREG_MASK + 1);
	src = dch->tx_buf + dch->tx_idx;	/* source pointer */
	dst = df->data + df->za[df->f1 & D_FREG_MASK].z1;
	maxlen = D_FIFO_SIZE - df->za[df->f1 & D_FREG_MASK].z1;		/* end fifo */
	if (maxlen > count)
		maxlen = count;	/* limit size */
	memcpy(dst, src, maxlen);	/* first copy */

	count -= maxlen;	/* remaining bytes */
	if (count) {
		dst = df->data;	/* start of buffer */
		src += maxlen;	/* new position */
		memcpy(dst, src, count);
	}
	df->za[new_f1 & D_FREG_MASK].z1 = new_z1;	/* for next buffer */
	df->za[df->f1 & D_FREG_MASK].z1 = new_z1;	/* new pos actual buffer */
	df->f1 = new_f1;	/* next frame */
	if (dch->debug & L1_DEB_ISAC_FIFO) {
		char *t = dch->dlog;

		count = dch->tx_len - dch->tx_idx;
		src = dch->tx_buf + dch->tx_idx;
		t += sprintf(t, "hfcD_fill_fifo cnt %d", count);
		mISDN_QuickHex(t, src, count);
		mISDN_debugprint(&dch->inst, dch->dlog);
	}
	dch->tx_idx = dch->tx_len;
	return;
}

/**************************/
/* B-channel send routine */
/**************************/
static void
hfcpci_fill_fifo(bchannel_t *bch)
{
	hfc_pci_t 	*hc = bch->inst.data;
	int		maxlen, fcnt;
	int		count, new_z1;
	bzfifo_type	*bz;
	u_char		*bdata;
	u_char		new_f1, *src, *dst;
	unsigned short	*z1t, *z2t;

	if ((bch->debug & L1_DEB_HSCX) && !(bch->debug & L1_DEB_HSCX_FIFO))
		mISDN_debugprint(&bch->inst, "%s", __FUNCTION__);
	count = bch->tx_len - bch->tx_idx;
	if (bch->tx_len <= 0)
		return;
	
	if ((bch->channel & 2) && (!hc->hw.bswapped)) {
		bz = &((fifo_area *) (hc->hw.fifos))->b_chans.txbz_b2;
		bdata = ((fifo_area *) (hc->hw.fifos))->b_chans.txdat_b2;
	} else {
		bz = &((fifo_area *) (hc->hw.fifos))->b_chans.txbz_b1;
		bdata = ((fifo_area *) (hc->hw.fifos))->b_chans.txdat_b1;
	}

	if (bch->protocol == ISDN_PID_L1_B_64TRANS) {
		z1t = &bz->za[MAX_B_FRAMES].z1;
		z2t = z1t + 1;
		if (bch->debug & L1_DEB_HSCX)
			mISDN_debugprint(&bch->inst, "hfcpci_fill_fifo_trans ch(%x) cnt(%d) z1(%x) z2(%x)",
				bch->channel, count, *z1t, *z2t);
		fcnt = *z2t - *z1t;
		if (fcnt <= 0)
			fcnt += B_FIFO_SIZE;	/* fcnt contains available bytes in fifo */
		fcnt = B_FIFO_SIZE - fcnt;	/* remaining bytes to send */
next_t_frame:
		if (fcnt < (2 * HFCPCI_BTRANS_THRESHOLD)) {
			count = bch->tx_len - bch->tx_idx;
			if (count < B_FIFO_SIZE - fcnt) {
				/* data is suitable for fifo */
				new_z1 = *z1t + count;	/* new buffer Position */
				if (new_z1 >= (B_FIFO_SIZE + B_SUB_VAL))
					new_z1 -= B_FIFO_SIZE;	/* buffer wrap */
				src = bch->tx_buf;	/* source pointer */
				dst = bdata + (*z1t - B_SUB_VAL);
				maxlen = (B_FIFO_SIZE + B_SUB_VAL) - *z1t;	/* end of fifo */
				if (bch->debug & L1_DEB_HSCX_FIFO)
					mISDN_debugprint(&bch->inst, "hfcpci_FFt fcnt(%d) maxl(%d) nz1(%x) dst(%p)",
						fcnt, maxlen, new_z1, dst);
				if (maxlen > count)
					maxlen = count;		/* limit size */
				memcpy(dst, src, maxlen);	/* first copy */

				count -= maxlen;	/* remaining bytes */
				if (count) {
					dst = bdata;	/* start of buffer */
					src += maxlen;	/* new position */
					memcpy(dst, src, count);
				}
				fcnt += bch->tx_len;
				*z1t = new_z1;	/* now send data */
			} else if (bch->debug & L1_DEB_HSCX)
				mISDN_debugprint(&bch->inst, "hfcpci_fill_fifo_trans ch(%x) frame length %d discarded",
					bch->channel, bch->tx_len);
			if (test_and_clear_bit(BC_FLG_TX_NEXT, &bch->Flag)) {
				if (bch->next_skb) {
					bch->tx_idx = 0;
					bch->tx_len = bch->next_skb->len;
					memcpy(bch->tx_buf,
						bch->next_skb->data,
						bch->tx_len);
					bch_sched_event(bch, B_XMTBUFREADY);
					goto next_t_frame;
				} else
					printk(KERN_WARNING "hfcB tx irq TX_NEXT without skb\n");
			}
			bch->tx_len = 0;
			test_and_clear_bit(BC_FLG_TX_BUSY, &bch->Flag);
			bch->tx_idx = bch->tx_len;
		}
		return;
	}
	if (bch->debug & L1_DEB_HSCX)
		mISDN_debugprint(&bch->inst, "%s: ch(%x) f1(%d) f2(%d) z1(f1)(%x)",
			__FUNCTION__, bch->channel, bz->f1, bz->f2, bz->za[bz->f1].z1);
	fcnt = bz->f1 - bz->f2;	/* frame count actually buffered */
	if (fcnt < 0)
		fcnt += (MAX_B_FRAMES + 1);	/* if wrap around */
	if (fcnt > (MAX_B_FRAMES - 1)) {
		if (bch->debug & L1_DEB_HSCX)
			mISDN_debugprint(&bch->inst, "hfcpci_fill_Bfifo more as 14 frames");
		return;
	}
	/* now determine free bytes in FIFO buffer */
	maxlen = bz->za[bz->f2].z2 - bz->za[bz->f1].z1 - 1;
	if (maxlen <= 0)
		maxlen += B_FIFO_SIZE;	/* count now contains available bytes */

	if (bch->debug & L1_DEB_HSCX)
		mISDN_debugprint(&bch->inst, "hfcpci_fill_fifo ch(%x) count(%ld/%d),%lx",
			bch->channel, count,
			maxlen, current->state);

	if (maxlen < count) {
		if (bch->debug & L1_DEB_HSCX)
			mISDN_debugprint(&bch->inst, "hfcpci_fill_fifo no fifo mem");
		return;
	}
	new_z1 = bz->za[bz->f1].z1 + count;	/* new buffer Position */
	if (new_z1 >= (B_FIFO_SIZE + B_SUB_VAL))
		new_z1 -= B_FIFO_SIZE;	/* buffer wrap */

	new_f1 = ((bz->f1 + 1) & MAX_B_FRAMES);
	src = bch->tx_buf + bch->tx_idx;	/* source pointer */
	dst = bdata + (bz->za[bz->f1].z1 - B_SUB_VAL);
	maxlen = (B_FIFO_SIZE + B_SUB_VAL) - bz->za[bz->f1].z1;		/* end fifo */
	if (maxlen > count)
		maxlen = count;	/* limit size */
	memcpy(dst, src, maxlen);	/* first copy */

	count -= maxlen;	/* remaining bytes */
	if (count) {
		dst = bdata;	/* start of buffer */
		src += maxlen;	/* new position */
		memcpy(dst, src, count);
	}
	bz->za[new_f1].z1 = new_z1;	/* for next buffer */
	bz->f1 = new_f1;	/* next frame */
	bch->tx_idx = bch->tx_len;
	test_and_clear_bit(BC_FLG_TX_BUSY, &bch->Flag);
	return;
}


#if 0
/**********************************************/
/* D-channel l1 state call for leased NT-mode */
/**********************************************/
static void
dch_nt_l2l1(struct PStack *st, int pr, void *arg)
{
	hfc_pci_t *hc = (struct IsdnCardState *) st->l1.hardware;

	switch (pr) {
		case (PH_DATA | REQUEST):
		case (PH_PULL | REQUEST):
		case (PH_PULL | INDICATION):
			st->l1.l1hw(st, pr, arg);
			break;
		case (PH_ACTIVATE | REQUEST):
			st->l1.l1l2(st, PH_ACTIVATE | CONFIRM, NULL);
			break;
		case (PH_TESTLOOP | REQUEST):
			if (1 & (long) arg)
				debugl1(hc, "PH_TEST_LOOP B1");
			if (2 & (long) arg)
				debugl1(hc, "PH_TEST_LOOP B2");
			if (!(3 & (long) arg))
				debugl1(hc, "PH_TEST_LOOP DISABLED");
			st->l1.l1hw(st, HW_TESTLOOP | REQUEST, arg);
			break;
		default:
			if (hc->debug)
				debugl1(hc, "dch_nt_l2l1 msg %04X unhandled", pr);
			break;
	}
}



/***********************/
/* set/reset echo mode */
/***********************/
static int
hfcpci_auxcmd(hfc_pci_t *hc, isdn_ctrl * ic)
{
	int i = *(unsigned int *) ic->parm.num;

	if ((ic->arg == 98) &&
	    (!(hc->hw.int_m1 & (HFCPCI_INTS_B2TRANS + HFCPCI_INTS_B2REC + HFCPCI_INTS_B1TRANS + HFCPCI_INTS_B1REC)))) {
		hc->hw.clkdel = CLKDEL_NT; /* ST-Bit delay for NT-Mode */
		Write_hfc(hc, HFCPCI_CLKDEL, hc->hw.clkdel);
		Write_hfc(hc, HFCPCI_STATES, HFCPCI_LOAD_STATE | 0);	/* HFC ST G0 */
		udelay(10);
		hc->hw.sctrl |= SCTRL_MODE_NT;
		Write_hfc(hc, HFCPCI_SCTRL, hc->hw.sctrl);	/* set NT-mode */
		udelay(10);
		Write_hfc(hc, HFCPCI_STATES, HFCPCI_LOAD_STATE | 1);	/* HFC ST G1 */
		udelay(10);
		Write_hfc(hc, HFCPCI_STATES, 1 | HFCPCI_ACTIVATE | HFCPCI_DO_ACTION);
		cs->dc.hfcpci.ph_state = 1;
		hc->hw.nt_mode = 1;
		hc->hw.nt_timer = 0;
		cs->stlist->l2.l2l1 = dch_nt_l2l1;
		debugl1(hc, "NT mode activated");
		return (0);
	}
	if ((hc->chanlimit > 1) || (hc->hw.bswapped) ||
	    (hc->hw.nt_mode) || (ic->arg != 12))
		return (-EINVAL);

	if (i) {
		cs->logecho = 1;
		hc->hw.trm |= 0x20;	/* enable echo chan */
		hc->hw.int_m1 |= HFCPCI_INTS_B2REC;
		hc->hw.fifo_en |= HFCPCI_FIFOEN_B2RX;
	} else {
		cs->logecho = 0;
		hc->hw.trm &= ~0x20;	/* disable echo chan */
		hc->hw.int_m1 &= ~HFCPCI_INTS_B2REC;
		hc->hw.fifo_en &= ~HFCPCI_FIFOEN_B2RX;
	}
	hc->hw.sctrl_r &= ~SCTRL_B2_ENA;
	hc->hw.sctrl &= ~SCTRL_B2_ENA;
	hc->hw.conn |= 0x10;	/* B2-IOM -> B2-ST */
	hc->hw.ctmt &= ~2;
	Write_hfc(hc, HFCPCI_CTMT, hc->hw.ctmt);
	Write_hfc(hc, HFCPCI_SCTRL_R, hc->hw.sctrl_r);
	Write_hfc(hc, HFCPCI_SCTRL, hc->hw.sctrl);
	Write_hfc(hc, HFCPCI_CONNECT, hc->hw.conn);
	Write_hfc(hc, HFCPCI_TRM, hc->hw.trm);
	Write_hfc(hc, HFCPCI_FIFO_EN, hc->hw.fifo_en);
	Write_hfc(hc, HFCPCI_INT_M1, hc->hw.int_m1);
	return (0);
}				/* hfcpci_auxcmd */

/*****************************/
/* E-channel receive routine */
/*****************************/
static void
receive_emsg(hfc_pci_t *hc)
{
	int rcnt;
	int receive, count = 5;
	bzfifo_type *bz;
	u_char *bdata;
	z_type *zp;
	u_char *ptr, *ptr1, new_f2;
	int total, maxlen, new_z2;
	u_char e_buffer[256];

	bz = &((fifo_area *) (hc->hw.fifos))->b_chans.rxbz_b2;
	bdata = ((fifo_area *) (hc->hw.fifos))->b_chans.rxdat_b2;
      Begin:
	count--;
	if (bz->f1 != bz->f2) {
		if (hc->debug & L1_DEB_ISAC)
			debugl1(hc, "hfcpci e_rec f1(%d) f2(%d)",
				bz->f1, bz->f2);
		zp = &bz->za[bz->f2];

		rcnt = zp->z1 - zp->z2;
		if (rcnt < 0)
			rcnt += B_FIFO_SIZE;
		rcnt++;
		if (hc->debug & L1_DEB_ISAC)
			debugl1(hc, "hfcpci e_rec z1(%x) z2(%x) cnt(%d)",
				zp->z1, zp->z2, rcnt);
		new_z2 = zp->z2 + rcnt;		/* new position in fifo */
		if (new_z2 >= (B_FIFO_SIZE + B_SUB_VAL))
			new_z2 -= B_FIFO_SIZE;	/* buffer wrap */
		new_f2 = (bz->f2 + 1) & MAX_B_FRAMES;
		if ((rcnt > 256 + 3) || (count < 4) ||
		    (*(bdata + (zp->z1 - B_SUB_VAL)))) {
			if (hc->debug & L1_DEB_WARN)
				debugl1(hc, "hfcpci_empty_echan: incoming packet invalid length %d or crc", rcnt);
			bz->za[new_f2].z2 = new_z2;
			bz->f2 = new_f2;	/* next buffer */
		} else {
			total = rcnt;
			rcnt -= 3;
			ptr = e_buffer;

			if (zp->z2 <= B_FIFO_SIZE + B_SUB_VAL)
				maxlen = rcnt;	/* complete transfer */
			else
				maxlen = B_FIFO_SIZE + B_SUB_VAL - zp->z2;	/* maximum */

			ptr1 = bdata + (zp->z2 - B_SUB_VAL);	/* start of data */
			memcpy(ptr, ptr1, maxlen);	/* copy data */
			rcnt -= maxlen;

			if (rcnt) {	/* rest remaining */
				ptr += maxlen;
				ptr1 = bdata;	/* start of buffer */
				memcpy(ptr, ptr1, rcnt);	/* rest */
			}
			bz->za[new_f2].z2 = new_z2;
			bz->f2 = new_f2;	/* next buffer */
			if (hc->debug & DEB_DLOG_HEX) {
				ptr = cs->dlog;
				if ((total - 3) < MAX_DLOG_SPACE / 3 - 10) {
					*ptr++ = 'E';
					*ptr++ = 'C';
					*ptr++ = 'H';
					*ptr++ = 'O';
					*ptr++ = ':';
					ptr += mISDN_QuickHex(ptr, e_buffer, total - 3);
					ptr--;
					*ptr++ = '\n';
					*ptr = 0;
					mISDN_putstatus(hc, NULL, cs->dlog);
				} else
					mISDN_putstatus(hc, "LogEcho: ", "warning Frame too big (%d)", total - 3);
			}
		}

		rcnt = bz->f1 - bz->f2;
		if (rcnt < 0)
			rcnt += MAX_B_FRAMES + 1;
		if (rcnt > 1)
			receive = 1;
		else
			receive = 0;
	} else
		receive = 0;
	if (count && receive)
		goto Begin;
	return;
}				/* receive_emsg */

#endif

/*********************/
/* Interrupt handler */
/*********************/
static irqreturn_t
hfcpci_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	hfc_pci_t	*hc = dev_id;
	u_char		exval;
	bchannel_t	*bch;
	u_long		flags;
	u_char		val, stat;

	spin_lock_irqsave(&hc->lock.lock, flags);
#ifdef SPIN_DEBUG
	hc->lock.spin_adr = (void *)0x3001;
#endif
	if (!(hc->hw.int_m2 & 0x08)) {
#ifdef SPIN_DEBUG
		hc->lock.spin_adr = NULL;
#endif
		spin_unlock_irqrestore(&hc->lock.lock, flags);
		return IRQ_NONE; /* not initialised */
	}

	if (HFCPCI_ANYINT & (stat = Read_hfc(hc, HFCPCI_STATUS))) {
		val = Read_hfc(hc, HFCPCI_INT_S1);
		if (hc->dch.debug & L1_DEB_ISAC)
			mISDN_debugprint(&hc->dch.inst, "HFC-PCI: stat(%02x) s1(%02x)",
				stat, val);
	} else {
		/* shared */
#ifdef SPIN_DEBUG
		hc->lock.spin_adr = NULL;
#endif
		spin_unlock_irqrestore(&hc->lock.lock, flags);
		return IRQ_NONE;
	}
	hc->irqcnt++;
	if (test_and_set_bit(STATE_FLAG_BUSY, &hc->lock.state)) {
		printk(KERN_ERR "%s: STATE_FLAG_BUSY allready activ, should never happen state:%lx\n",
			__FUNCTION__, hc->lock.state);
#ifdef SPIN_DEBUG
		printk(KERN_ERR "%s: previous lock:%p\n",
			__FUNCTION__, hc->lock.busy_adr);
#endif
#ifdef LOCK_STATISTIC
		hc->lock.irq_fail++;
#endif
	} else {
#ifdef LOCK_STATISTIC
		hc->lock.irq_ok++;
#endif
#ifdef SPIN_DEBUG
		hc->lock.busy_adr = hfcpci_interrupt;
#endif
	}

	test_and_set_bit(STATE_FLAG_INIRQ, &hc->lock.state);
#ifdef SPIN_DEBUG
	hc->lock.spin_adr= NULL;
#endif
	spin_unlock_irqrestore(&hc->lock.lock, flags);

	if (hc->dch.debug & L1_DEB_ISAC)
		mISDN_debugprint(&hc->dch.inst, "HFC-PCI irq %x", val);
	val &= hc->hw.int_m1;
	if (val & 0x40) {	/* state machine irq */
		exval = Read_hfc(hc, HFCPCI_STATES) & 0xf;
		if (hc->dch.debug & L1_DEB_ISAC)
			mISDN_debugprint(&hc->dch.inst, "ph_state chg %d->%d",
				hc->dch.ph_state, exval);
		hc->dch.ph_state = exval;
		dchannel_sched_event(&hc->dch, D_L1STATECHANGE);
		val &= ~0x40;
	}
	if (val & 0x80) {	/* timer irq */
		if (hc->hw.nt_mode) {
			if ((--hc->hw.nt_timer) < 0)
				dchannel_sched_event(&hc->dch, D_L1STATECHANGE);
		}
		val &= ~0x80;
		Write_hfc(hc, HFCPCI_CTMT, hc->hw.ctmt | HFCPCI_CLTIMER);
	}
	if (val & 0x08) {
		if (!(bch = Sel_BCS(hc, hc->hw.bswapped ? 2 : 1))) {
			if (hc->dch.debug)
				mISDN_debugprint(&hc->dch.inst, "hfcpci spurious 0x08 IRQ");
		} else
			main_rec_hfcpci(bch);
	}
	if (val & 0x10) {
//		if (hc->logecho)
//			receive_emsg(hc);
//		else 
		if (!(bch = Sel_BCS(hc, 2))) {
			if (hc->dch.debug)
				mISDN_debugprint(&hc->dch.inst, "hfcpci spurious 0x10 IRQ");
		} else
			main_rec_hfcpci(bch);
	}
	if (val & 0x01) {
		if (!(bch = Sel_BCS(hc, hc->hw.bswapped ? 2 : 1))) {
			if (hc->dch.debug)
				mISDN_debugprint(&hc->dch.inst, "hfcpci spurious 0x01 IRQ");
		} else {
			if (bch->tx_idx < bch->tx_len) {
				hfcpci_fill_fifo(bch);
			} else {
				bch->tx_idx = 0;
				if (test_and_clear_bit(BC_FLG_TX_NEXT, &bch->Flag)) {
					if (bch->next_skb) {
						bch->tx_len = bch->next_skb->len;
						memcpy(bch->tx_buf,
							bch->next_skb->data,
							bch->tx_len);
						hfcpci_fill_fifo(bch);
						bch_sched_event(bch, B_XMTBUFREADY);
					} else {
						printk(KERN_WARNING "hfcB tx irq TX_NEXT without skb\n");
						test_and_clear_bit(BC_FLG_TX_BUSY, &bch->Flag);
						bch->tx_len = 0;
					}
				} else {
					test_and_clear_bit(BC_FLG_TX_BUSY, &bch->Flag);
					bch_sched_event(bch, B_XMTBUFREADY);
					bch->tx_len = 0;
				}
			}
		}
	}
	if (val & 0x02) {
		if (!(bch = Sel_BCS(hc, 2))) {
			if (hc->dch.debug)
				mISDN_debugprint(&hc->dch.inst, "hfcpci spurious 0x02 IRQ");
		} else {
			if (bch->tx_idx < bch->tx_len) {
				hfcpci_fill_fifo(bch);
			} else {
				bch->tx_idx = 0;
				if (test_and_clear_bit(BC_FLG_TX_NEXT, &bch->Flag)) {
					if (bch->next_skb) {
						bch->tx_len = bch->next_skb->len;
						memcpy(bch->tx_buf,
							bch->next_skb->data,
							bch->tx_len);
						hfcpci_fill_fifo(bch);
						bch_sched_event(bch, B_XMTBUFREADY);
					} else {
						printk(KERN_WARNING "hfcB tx irq TX_NEXT without skb\n");
						test_and_clear_bit(BC_FLG_TX_BUSY, &bch->Flag);
						bch->tx_len = 0;
					}
				} else {
					test_and_clear_bit(BC_FLG_TX_BUSY, &bch->Flag);
					bch_sched_event(bch, B_XMTBUFREADY);
					bch->tx_len = 0;
				}
			}
		}
	}
	if (val & 0x20) {	/* receive dframe */
		receive_dmsg(hc);
	}
	if (val & 0x04) {	/* dframe transmitted */
		if (test_and_clear_bit(FLG_DBUSY_TIMER, &hc->dch.DFlags))
			del_timer(&hc->dch.dbusytimer);
		if (test_and_clear_bit(FLG_L1_DBUSY, &hc->dch.DFlags))
			dchannel_sched_event(&hc->dch, D_CLEARBUSY);
		if (hc->dch.tx_idx < hc->dch.tx_len) {
			hfcpci_fill_dfifo(hc);
		} else {
			if (test_and_clear_bit(FLG_TX_NEXT, &hc->dch.DFlags)) {
				if (hc->dch.next_skb) {
					hc->dch.tx_len = hc->dch.next_skb->len;
					memcpy(hc->dch.tx_buf,
						hc->dch.next_skb->data,
						hc->dch.tx_len);
					hc->dch.tx_idx = 0;
					hfcpci_fill_dfifo(hc);
					dchannel_sched_event(&hc->dch, D_XMTBUFREADY);
				} else {
					printk(KERN_WARNING "hfcd tx irq TX_NEXT without skb\n");
					test_and_clear_bit(FLG_TX_BUSY, &hc->dch.DFlags);
				}
			} else
				test_and_clear_bit(FLG_TX_BUSY, &hc->dch.DFlags);
		}
	}
	spin_lock_irqsave(&hc->lock.lock, flags);
#ifdef SPIN_DEBUG
	hc->lock.spin_adr = (void *)0x3002;
#endif
	if (!test_and_clear_bit(STATE_FLAG_INIRQ, &hc->lock.state)) {
	}
	if (!test_and_clear_bit(STATE_FLAG_BUSY, &hc->lock.state)) {
		printk(KERN_ERR "%s: STATE_FLAG_BUSY not locked state(%lx)\n",
			__FUNCTION__, hc->lock.state);
	}
#ifdef SPIN_DEBUG
	hc->lock.busy_adr = NULL;
	hc->lock.spin_adr = NULL;
#endif
	spin_unlock_irqrestore(&hc->lock.lock, flags);
	return IRQ_HANDLED;
}

/********************************************************************/
/* timer callback for D-chan busy resolution. Currently no function */
/********************************************************************/
static void
hfcpci_dbusy_timer(hfc_pci_t *hc)
{
}

/*************************************/
/* Layer 1 D-channel hardware access */
/*************************************/
static int
HFCD_l1hw(mISDNif_t *hif, struct sk_buff *skb)
{
	dchannel_t	*dch;
	hfc_pci_t	*hc;
	int		ret = -EINVAL;
	mISDN_head_t	*hh;

	if (!hif || !skb)
		return(ret);
	hh = mISDN_HEAD_P(skb);
	dch = hif->fdata;
	hc = dch->inst.data;
	ret = 0;
	if (hh->prim == PH_DATA_REQ) {
		if (dch->next_skb) {
			printk(KERN_WARNING "%s: next_skb exist ERROR\n", __FUNCTION__);
			return(-EBUSY);
		}
		dch->inst.lock(dch->inst.data, 0);
		if (test_and_set_bit(FLG_TX_BUSY, &dch->DFlags)) {
			test_and_set_bit(FLG_TX_NEXT, &dch->DFlags);
			dch->next_skb = skb;
			dch->inst.unlock(dch->inst.data);
			return(0);
		} else {
			dch->tx_len = skb->len;
			memcpy(dch->tx_buf, skb->data, dch->tx_len);
			dch->tx_idx = 0;
			hfcpci_fill_dfifo(dch->inst.data);
			dch->inst.unlock(dch->inst.data);
			skb_trim(skb, 0);
			return(if_newhead(&dch->inst.up, PH_DATA_CNF,
				hh->dinfo, skb));
		}
	} else if (hh->prim == (PH_SIGNAL | REQUEST)) {
		dch->inst.lock(dch->inst.data, 0);
		if ((hh->dinfo == INFO3_P8) || (hh->dinfo == INFO3_P10)) {
			if (test_bit(HFC_CFG_MASTER, &hc->cfg))
				hc->hw.mst_m |= HFCPCI_MASTER;
			Write_hfc(hc, HFCPCI_MST_MODE, hc->hw.mst_m);
		} else
			ret = -EINVAL;
		dch->inst.unlock(dch->inst.data);
	} else if (hh->prim == (PH_CONTROL | REQUEST)) {
		dch->inst.lock(dch->inst.data, 0);
		if (hh->dinfo == HW_RESET) {
			Write_hfc(hc, HFCPCI_STATES, HFCPCI_LOAD_STATE | 3);	/* HFC ST 3 */
			udelay(6);
			Write_hfc(hc, HFCPCI_STATES, 3);	/* HFC ST 2 */
			if (test_bit(HFC_CFG_MASTER, &hc->cfg))
				hc->hw.mst_m |= HFCPCI_MASTER;
			Write_hfc(hc, HFCPCI_MST_MODE, hc->hw.mst_m);
			Write_hfc(hc, HFCPCI_STATES, HFCPCI_ACTIVATE | HFCPCI_DO_ACTION);
//			l1_msg(hc, HW_POWERUP | CONFIRM, NULL);
		} else if (hh->dinfo == HW_DEACTIVATE) {
			hc->hw.mst_m &= ~HFCPCI_MASTER;
			Write_hfc(hc, HFCPCI_MST_MODE, hc->hw.mst_m);
			discard_queue(&dch->rqueue);
			if (dch->next_skb) {
				dev_kfree_skb(dch->next_skb);
				dch->next_skb = NULL;
			}
			test_and_clear_bit(FLG_TX_NEXT, &dch->DFlags);
			test_and_clear_bit(FLG_TX_BUSY, &dch->DFlags);
			if (test_and_clear_bit(FLG_DBUSY_TIMER, &dch->DFlags))
				del_timer(&dch->dbusytimer);
			if (test_and_clear_bit(FLG_L1_DBUSY, &dch->DFlags))
				dchannel_sched_event(&hc->dch, D_CLEARBUSY);
		} else if (hh->dinfo == HW_POWERUP) {
			Write_hfc(hc, HFCPCI_STATES, HFCPCI_DO_ACTION);
		} else if ((hh->dinfo & HW_TESTLOOP) == HW_TESTLOOP) {
			u_char	slot;
			if (1 & hh->dinfo) {
				if (test_bit(HFC_CFG_SW_DD_DU, &hc->cfg))
					slot = 0xC0;
				else
					slot = 0x80;
				printk(KERN_DEBUG  "%s: Write_hfc: B1_SSL/RSL 0x%x\n",
					__FUNCTION__, slot);
				Write_hfc(hc, HFCPCI_B1_SSL, slot);
				Write_hfc(hc, HFCPCI_B1_RSL, slot);
				hc->hw.conn = (hc->hw.conn & ~7) | 1;
				Write_hfc(hc, HFCPCI_CONNECT, hc->hw.conn);
			}
			if (2 & hh->dinfo) {
				if (test_bit(HFC_CFG_SW_DD_DU, &hc->cfg))
					slot = 0xC1;
				else
					slot = 0x81;
				printk(KERN_DEBUG "%s: Write_hfc: B2_SSL/RSL 0x%x\n",
					__FUNCTION__, slot);
				Write_hfc(hc, HFCPCI_B2_SSL, slot);
				Write_hfc(hc, HFCPCI_B2_RSL, slot);
				hc->hw.conn = (hc->hw.conn & ~0x38) | 0x08;
				Write_hfc(hc, HFCPCI_CONNECT, hc->hw.conn);
			}
			if (3 & hh->dinfo)
				hc->hw.trm |= 0x80;	/* enable IOM-loop */
			else
				hc->hw.trm &= 0x7f;	/* disable IOM-loop */
			Write_hfc(hc, HFCPCI_TRM, hc->hw.trm);
		} else {
			if (dch->debug & L1_DEB_WARN)
				mISDN_debugprint(&dch->inst, "%s: unknown ctrl %x",
					__FUNCTION__, hh->dinfo);
			ret = -EINVAL;
		}
		dch->inst.unlock(dch->inst.data);
	} else if (hh->prim == (PH_ACTIVATE | REQUEST)) {
		if (hc->hw.nt_mode) {
			dch->inst.lock(dch->inst.data, 0);
			Write_hfc(hc, HFCPCI_STATES, HFCPCI_LOAD_STATE | 0); /* G0 */
			udelay(6);
			Write_hfc(hc, HFCPCI_STATES, HFCPCI_LOAD_STATE | 1); /* G1 */
			udelay(6);
			if (test_bit(HFC_CFG_MASTER, &hc->cfg))
				hc->hw.mst_m |= HFCPCI_MASTER;
			Write_hfc(hc, HFCPCI_MST_MODE, hc->hw.mst_m);
			udelay(6);
			Write_hfc(hc, HFCPCI_STATES, HFCPCI_ACTIVATE | HFCPCI_DO_ACTION | 1);
			dch->inst.unlock(dch->inst.data);
		} else {
			if (dch->debug & L1_DEB_WARN)
				mISDN_debugprint(&dch->inst, "%s: PH_ACTIVATE none NT mode",
					__FUNCTION__);
			ret = -EINVAL;
		}
	} else if (hh->prim == (PH_DEACTIVATE | REQUEST)) {
		if (hc->hw.nt_mode) {
			dch->inst.lock(dch->inst.data, 0);
			hc->hw.mst_m &= ~HFCPCI_MASTER;
			Write_hfc(hc, HFCPCI_MST_MODE, hc->hw.mst_m);
			discard_queue(&dch->rqueue);
			if (dch->next_skb) {
				dev_kfree_skb(dch->next_skb);
				dch->next_skb = NULL;
			}
			test_and_clear_bit(FLG_TX_NEXT, &dch->DFlags);
			test_and_clear_bit(FLG_TX_BUSY, &dch->DFlags);
			if (test_and_clear_bit(FLG_DBUSY_TIMER, &dch->DFlags))
				del_timer(&dch->dbusytimer);
			if (test_and_clear_bit(FLG_L1_DBUSY, &dch->DFlags))
				dchannel_sched_event(&hc->dch, D_CLEARBUSY);
			dch->inst.unlock(dch->inst.data);
		} else {
			if (dch->debug & L1_DEB_WARN)
				mISDN_debugprint(&dch->inst, "%s: PH_DEACTIVATE none NT mode",
					__FUNCTION__);
			ret = -EINVAL;
		}
	} else {
		if (dch->debug & L1_DEB_WARN)
			mISDN_debugprint(&dch->inst, "%s: unknown prim %x",
				__FUNCTION__, hh->prim);
		ret = -EINVAL;
	}
	if (!ret)
		dev_kfree_skb(skb);
	return(ret);
}

/***************************************************************/
/* activate/deactivate hardware for selected channels and mode */
/***************************************************************/
static int
mode_hfcpci(bchannel_t *bch, int bc, int protocol)
{
	hfc_pci_t	*hc = bch->inst.data;
	int		fifo2;
	u_char		rx_slot = 0, tx_slot = 0, pcm_mode;

	if (bch->debug & L1_DEB_HSCX)
		mISDN_debugprint(&bch->inst, "HFCPCI bchannel protocol %x-->%x ch %x-->%x",
			bch->protocol, protocol, bch->channel, bc);
	
	fifo2 = bc;
	pcm_mode = (bc>>24) & 0xff;
	if (pcm_mode) { /* PCM SLOT USE */
		if (!test_bit(HFC_CFG_PCM, &hc->cfg))
			printk(KERN_WARNING "%s: pcm channel id without HFC_CFG_PCM\n",
				__FUNCTION__);
		rx_slot = (bc>>8) & 0xff;
		tx_slot = (bc>>16) & 0xff;
		bc = bc & 0xff;
	} else if (test_bit(HFC_CFG_PCM, &hc->cfg) && (protocol > ISDN_PID_NONE))
		printk(KERN_WARNING "%s: no pcm channel id but HFC_CFG_PCM\n",
				__FUNCTION__);
	if (hc->chanlimit > 1) {
		hc->hw.bswapped = 0;	/* B1 and B2 normal mode */
		hc->hw.sctrl_e &= ~0x80;
	} else {
		if (bc & 2) {
			if (protocol != ISDN_PID_NONE) {
				hc->hw.bswapped = 1;	/* B1 and B2 exchanged */
				hc->hw.sctrl_e |= 0x80;
			} else {
				hc->hw.bswapped = 0;	/* B1 and B2 normal mode */
				hc->hw.sctrl_e &= ~0x80;
			}
			fifo2 = 1;
		} else {
			hc->hw.bswapped = 0;	/* B1 and B2 normal mode */
			hc->hw.sctrl_e &= ~0x80;
		}
	}
	switch (protocol) {
		case (-1): /* used for init */
			bch->protocol = -1;
			bch->channel = bc;
		case (ISDN_PID_NONE):
			if (bch->protocol == ISDN_PID_NONE) {
				return(0);
			}
			if (bc & 2) {
				hc->hw.sctrl &= ~SCTRL_B2_ENA;
				hc->hw.sctrl_r &= ~SCTRL_B2_ENA;
			} else {
				hc->hw.sctrl &= ~SCTRL_B1_ENA;
				hc->hw.sctrl_r &= ~SCTRL_B1_ENA;
			}
			if (fifo2 & 2) {
				hc->hw.fifo_en &= ~HFCPCI_FIFOEN_B2;
				hc->hw.int_m1 &= ~(HFCPCI_INTS_B2TRANS + HFCPCI_INTS_B2REC);
			} else {
				hc->hw.fifo_en &= ~HFCPCI_FIFOEN_B1;
				hc->hw.int_m1 &= ~(HFCPCI_INTS_B1TRANS + HFCPCI_INTS_B1REC);
			}
#ifdef REVERSE_BITORDER
			if (bch->channel & 2)
				hc->hw.cirm &= 0x7f;
			else
				hc->hw.cirm &= 0xbf;
#endif
			bch->protocol = ISDN_PID_NONE;
			bch->channel = bc;
			break;
		case (ISDN_PID_L1_B_64TRANS):
			bch->protocol = protocol;
			bch->channel = bc;
		        hfcpci_clear_fifo_rx(hc, (fifo2 & 2)?1:0);
		        hfcpci_clear_fifo_tx(hc, (fifo2 & 2)?1:0);
			if (bc & 2) {
				hc->hw.sctrl |= SCTRL_B2_ENA;
				hc->hw.sctrl_r |= SCTRL_B2_ENA;
#ifdef REVERSE_BITORDER
				hc->hw.cirm |= 0x80;
#endif
			} else {
				hc->hw.sctrl |= SCTRL_B1_ENA;
				hc->hw.sctrl_r |= SCTRL_B1_ENA;
#ifdef REVERSE_BITORDER
				hc->hw.cirm |= 0x40;
#endif
			}
			if (fifo2 & 2) {
				hc->hw.fifo_en |= HFCPCI_FIFOEN_B2;
				hc->hw.int_m1 |= (HFCPCI_INTS_B2TRANS + HFCPCI_INTS_B2REC);
				hc->hw.ctmt |= 2;
				hc->hw.conn &= ~0x18;
			} else {
				hc->hw.fifo_en |= HFCPCI_FIFOEN_B1;
				hc->hw.int_m1 |= (HFCPCI_INTS_B1TRANS + HFCPCI_INTS_B1REC);
				hc->hw.ctmt |= 1;
				hc->hw.conn &= ~0x03;
			}
			break;
		case (ISDN_PID_L1_B_64HDLC):
			bch->protocol = protocol;
			bch->channel = bc;
		        hfcpci_clear_fifo_rx(hc, (fifo2 & 2)?1:0);
		        hfcpci_clear_fifo_tx(hc, (fifo2 & 2)?1:0);
			if (bc & 2) {
				hc->hw.sctrl |= SCTRL_B2_ENA;
				hc->hw.sctrl_r |= SCTRL_B2_ENA;
			} else {
				hc->hw.sctrl |= SCTRL_B1_ENA;
				hc->hw.sctrl_r |= SCTRL_B1_ENA;
			}
			if (fifo2 & 2) {
			        hc->hw.last_bfifo_cnt[1] = 0;  
				hc->hw.fifo_en |= HFCPCI_FIFOEN_B2;
				hc->hw.int_m1 |= (HFCPCI_INTS_B2TRANS + HFCPCI_INTS_B2REC);
				hc->hw.ctmt &= ~2;
				hc->hw.conn &= ~0x18;
			} else {
			        hc->hw.last_bfifo_cnt[0] = 0;  
				hc->hw.fifo_en |= HFCPCI_FIFOEN_B1;
				hc->hw.int_m1 |= (HFCPCI_INTS_B1TRANS + HFCPCI_INTS_B1REC);
				hc->hw.ctmt &= ~1;
				hc->hw.conn &= ~0x03;
			}
			break;
#if 0
		case (L1_MODE_EXTRN):
			if (bc) {
				hc->hw.conn |= 0x10;
				hc->hw.sctrl |= SCTRL_B2_ENA;
				hc->hw.sctrl_r |= SCTRL_B2_ENA;
				hc->hw.fifo_en &= ~HFCPCI_FIFOEN_B2;
				hc->hw.int_m1 &= ~(HFCPCI_INTS_B2TRANS + HFCPCI_INTS_B2REC);
			} else {
				hc->hw.conn |= 0x02;
				hc->hw.sctrl |= SCTRL_B1_ENA;
				hc->hw.sctrl_r |= SCTRL_B1_ENA;
				hc->hw.fifo_en &= ~HFCPCI_FIFOEN_B1;
				hc->hw.int_m1 &= ~(HFCPCI_INTS_B1TRANS + HFCPCI_INTS_B1REC);
			}
			break;
#endif
		default:
			mISDN_debugprint(&bch->inst, "prot not known %x", protocol);
			return(-ENOPROTOOPT);
	}
	if (test_bit(HFC_CFG_PCM, &hc->cfg)) {
		if ((protocol == ISDN_PID_NONE) ||
			(protocol == -1)) {	/* init case */
			rx_slot = 0;
			tx_slot = 0;
		} else {
			if (test_bit(HFC_CFG_SW_DD_DU, &hc->cfg)) {
				rx_slot |= 0xC0;
				tx_slot |= 0xC0;
			} else {
				rx_slot |= 0x80;
				tx_slot |= 0x80;
			}
		}
		if (bc & 2) {
			hc->hw.conn &= 0xc7;
			hc->hw.conn |= 0x08;
			printk(KERN_DEBUG "%s: Write_hfc: B2_SSL 0x%x\n",
				__FUNCTION__, tx_slot);
			printk(KERN_DEBUG "%s: Write_hfc: B2_RSL 0x%x\n",
				__FUNCTION__, rx_slot);
			Write_hfc(hc, HFCPCI_B2_SSL, tx_slot);
			Write_hfc(hc, HFCPCI_B2_RSL, rx_slot);
		} else {
			hc->hw.conn &= 0xf8;
			hc->hw.conn |= 0x01;
			printk(KERN_DEBUG "%s: Write_hfc: B1_SSL 0x%x\n",
				__FUNCTION__, tx_slot);
			printk(KERN_DEBUG "%s: Write_hfc: B1_RSL 0x%x\n",
				__FUNCTION__, rx_slot);
			Write_hfc(hc, HFCPCI_B1_SSL, tx_slot);
			Write_hfc(hc, HFCPCI_B1_RSL, rx_slot);
		}
	}
	Write_hfc(hc, HFCPCI_SCTRL_E, hc->hw.sctrl_e);
	Write_hfc(hc, HFCPCI_INT_M1, hc->hw.int_m1);
	Write_hfc(hc, HFCPCI_FIFO_EN, hc->hw.fifo_en);
	Write_hfc(hc, HFCPCI_SCTRL, hc->hw.sctrl);
	Write_hfc(hc, HFCPCI_SCTRL_R, hc->hw.sctrl_r);
	Write_hfc(hc, HFCPCI_CTMT, hc->hw.ctmt);
	Write_hfc(hc, HFCPCI_CONNECT, hc->hw.conn);
#ifdef REVERSE_BITORDER
	Write_hfc(hc, HFCPCI_CIRM, hc->hw.cirm);
#endif
	if (bch->protocol)
		bch_sched_event(bch, B_XMTBUFREADY);
	return(0);
}

static int
set_hfcpci_rxtest(bchannel_t *bch, int protocol, struct sk_buff *skb)
{
	hfc_pci_t	*hc = bch->inst.data;
	int		*chan = (int *)skb->data;

	if (skb->len <4) {
		mISDN_debugprint(&bch->inst, "HFCPCI rxtest no channel parameter");
		return(-EINVAL);
	}
	if (bch->debug & L1_DEB_HSCX)
		mISDN_debugprint(&bch->inst, "HFCPCI bchannel test rx protocol %x-->%x ch %x-->%x",
			bch->protocol, protocol, bch->channel, *chan);
	if (bch->channel != *chan) {
		mISDN_debugprint(&bch->inst, "HFCPCI rxtest wrong channel parameter %x/%x",
			bch->channel, *chan);
		return(-EINVAL);
	}
	switch (protocol) {
		case (ISDN_PID_L1_B_64TRANS):
			bch->protocol = protocol;
		        hfcpci_clear_fifo_rx(hc, (*chan & 2)?1:0);
			if (*chan & 2) {
				hc->hw.sctrl_r |= SCTRL_B2_ENA;
				hc->hw.fifo_en |= HFCPCI_FIFOEN_B2RX;
				hc->hw.int_m1 |= HFCPCI_INTS_B2REC;
				hc->hw.ctmt |= 2;
				hc->hw.conn &= ~0x18;
#ifdef REVERSE_BITORDER
				hc->hw.cirm |= 0x80;
#endif
			} else {
				hc->hw.sctrl_r |= SCTRL_B1_ENA;
				hc->hw.fifo_en |= HFCPCI_FIFOEN_B1RX;
				hc->hw.int_m1 |= HFCPCI_INTS_B1REC;
				hc->hw.ctmt |= 1;
				hc->hw.conn &= ~0x03;
#ifdef REVERSE_BITORDER
				hc->hw.cirm |= 0x40;
#endif
			}
			break;
		case (ISDN_PID_L1_B_64HDLC):
			bch->protocol = protocol;
		        hfcpci_clear_fifo_rx(hc, (*chan & 2)?1:0);
			if (*chan & 2) {
				hc->hw.sctrl_r |= SCTRL_B2_ENA;
				hc->hw.last_bfifo_cnt[1] = 0;
				hc->hw.fifo_en |= HFCPCI_FIFOEN_B2RX;
				hc->hw.int_m1 |= HFCPCI_INTS_B2REC;
				hc->hw.ctmt &= ~2;
				hc->hw.conn &= ~0x18;
			} else {
				hc->hw.sctrl_r |= SCTRL_B1_ENA;
			        hc->hw.last_bfifo_cnt[0] = 0;  
				hc->hw.fifo_en |= HFCPCI_FIFOEN_B1RX;
				hc->hw.int_m1 |= HFCPCI_INTS_B1REC;
				hc->hw.ctmt &= ~1;
				hc->hw.conn &= ~0x03;
			}
			break;
		default:
			mISDN_debugprint(&bch->inst, "prot not known %x", protocol);
			return(-ENOPROTOOPT);
	}
	Write_hfc(hc, HFCPCI_INT_M1, hc->hw.int_m1);
	Write_hfc(hc, HFCPCI_FIFO_EN, hc->hw.fifo_en);
	Write_hfc(hc, HFCPCI_SCTRL_R, hc->hw.sctrl_r);
	Write_hfc(hc, HFCPCI_CTMT, hc->hw.ctmt);
	Write_hfc(hc, HFCPCI_CONNECT, hc->hw.conn);
#ifdef REVERSE_BITORDER
	Write_hfc(hc, HFCPCI_CIRM, hc->hw.cirm);
#endif
	return(0);
}

/******************************/
/* Layer2 -> Layer 1 Transfer */
/******************************/
static int
hfcpci_l2l1(mISDNif_t *hif, struct sk_buff *skb)
{
	bchannel_t	*bch;
	int		ret = -EINVAL;
	mISDN_head_t	*hh;

	if (!hif || !skb)
		return(ret);
	hh = mISDN_HEAD_P(skb);
	bch = hif->fdata;
	if ((hh->prim == PH_DATA_REQ) ||
		(hh->prim == (DL_DATA | REQUEST))) {
#warning TODO: hier muss abgefragt werden, ob skb->len <= 0 ist, und ggf. ein -EINVAL zurückliefern, sonst wird zwar einmal confirmed, aber es regt sich nichts mehr. dies bitte auch für den d-kanal überdenken, sowie für alle andere kartentreiber.
		if (bch->next_skb) {
			printk(KERN_WARNING "%s: next_skb exist ERROR\n",
				__FUNCTION__);
			return(-EBUSY);
		}
		bch->inst.lock(bch->inst.data, 0);
		if (test_and_set_bit(BC_FLG_TX_BUSY, &bch->Flag)) {
			test_and_set_bit(BC_FLG_TX_NEXT, &bch->Flag);
			bch->next_skb = skb;
			bch->inst.unlock(bch->inst.data);
			return(0);
		} else {
			bch->tx_len = skb->len;
			memcpy(bch->tx_buf, skb->data, bch->tx_len);
			bch->tx_idx = 0;
			hfcpci_fill_fifo(bch);
			bch->inst.unlock(bch->inst.data);
			if ((bch->inst.pid.protocol[2] == ISDN_PID_L2_B_RAWDEV)
				&& bch->dev)
				hif = &bch->dev->rport.pif;
			else
				hif = &bch->inst.up;
			skb_trim(skb, 0);
			return(if_newhead(hif, hh->prim | CONFIRM,
				hh->dinfo, skb));
		}
	} else if ((hh->prim == (PH_ACTIVATE | REQUEST)) ||
		(hh->prim == (DL_ESTABLISH  | REQUEST))) {
		if (test_and_set_bit(BC_FLG_ACTIV, &bch->Flag))
			ret = 0;
		else {
			bch->inst.lock(bch->inst.data, 0);
			ret = mode_hfcpci(bch, bch->channel,
				bch->inst.pid.protocol[1]);
			bch->inst.unlock(bch->inst.data);
		}
		if (bch->inst.pid.protocol[2] == ISDN_PID_L2_B_RAWDEV)
			if (bch->dev)
				if_link(&bch->dev->rport.pif,
					hh->prim | CONFIRM, 0, 0, NULL, 0);
		skb_trim(skb, 0);
		return(if_newhead(&bch->inst.up, hh->prim | CONFIRM, ret, skb));
	} else if ((hh->prim == (PH_DEACTIVATE | REQUEST)) ||
		(hh->prim == (DL_RELEASE | REQUEST)) ||
		(hh->prim == (MGR_DISCONNECT | REQUEST))) {
		bch->inst.lock(bch->inst.data, 0);
		if (test_and_clear_bit(BC_FLG_TX_NEXT, &bch->Flag)) {
			dev_kfree_skb(bch->next_skb);
			bch->next_skb = NULL;
		}
		test_and_clear_bit(BC_FLG_TX_BUSY, &bch->Flag);
		mode_hfcpci(bch, bch->channel, ISDN_PID_NONE);
		test_and_clear_bit(BC_FLG_ACTIV, &bch->Flag);
		bch->inst.unlock(bch->inst.data);
		skb_trim(skb, 0);
		if (hh->prim != (MGR_DISCONNECT | REQUEST)) {
			if (bch->inst.pid.protocol[2] == ISDN_PID_L2_B_RAWDEV)
				if (bch->dev)
					if_link(&bch->dev->rport.pif,
						hh->prim | CONFIRM, 0, 0, NULL, 0);
			if (!if_newhead(&bch->inst.up, hh->prim | CONFIRM, 0, skb))
				return(0);
		}
		ret = 0;
	} else if (hh->prim == (PH_CONTROL | REQUEST)) {
		bch->inst.lock(bch->inst.data, 0);
		if (hh->dinfo == HW_TESTRX_RAW) {
			ret = set_hfcpci_rxtest(bch, ISDN_PID_L1_B_64TRANS, skb);
		} else if (hh->dinfo == HW_TESTRX_HDLC) {
			ret = set_hfcpci_rxtest(bch, ISDN_PID_L1_B_64HDLC, skb);
		} else if (hh->dinfo == HW_TESTRX_OFF) {
			mode_hfcpci(bch, bch->channel, ISDN_PID_NONE);
			ret = 0;
		} else
			ret = -EINVAL;
		bch->inst.unlock(bch->inst.data);
		if (!ret) {
			skb_trim(skb, 0);
			if (!if_newhead(&bch->inst.up, hh->prim | CONFIRM, hh->dinfo, skb))
				return(0);
		}
	} else {
		printk(KERN_WARNING "%s: unknown prim(%x)\n",
			__FUNCTION__, hh->prim);
		ret = -EINVAL;
	}
	if (!ret)
		dev_kfree_skb(skb);
	return(ret);
}

/***************************/
/* handle L1 state changes */
/***************************/

static void
HW_hfcD_bh(dchannel_t *dch)
{
	hfc_pci_t	*hc = dch->inst.data;
	u_int		prim = PH_SIGNAL | INDICATION;
	u_int		para = 0;
	mISDNif_t	*upif = &dch->inst.up;

	if (test_and_clear_bit(D_L1STATECHANGE, &dch->event)) {
		if (!hc->hw.nt_mode) {
			if (dch->debug)
				printk(KERN_DEBUG "%s: TE newstate %x\n",
					__FUNCTION__, dch->ph_state);
			switch (dch->ph_state) {
				case (0):
					prim = PH_CONTROL | INDICATION;
					para = HW_RESET;
					break;
				case (3):
					prim = PH_CONTROL | INDICATION;
					para = HW_DEACTIVATE;
					break;
				case (5):
				case (8):
					para = ANYSIGNAL;
					break;
				case (6):
					para = INFO2;
					break;
				case (7):
					para = INFO4_P8;
					break;
				default:
					return;
			}
		} else {
			if (dch->debug)
				printk(KERN_DEBUG "%s: NT newstate %x\n",
					__FUNCTION__, dch->ph_state);
			dch->inst.lock(dch->inst.data, 0);
			switch (dch->ph_state) {
				case (2):
					if (hc->hw.nt_timer < 0) {
						hc->hw.nt_timer = 0;
						hc->hw.int_m1 &= ~HFCPCI_INTS_TIMER;
						Write_hfc(hc, HFCPCI_INT_M1, hc->hw.int_m1);
						/* Clear already pending ints */
						if (Read_hfc(hc, HFCPCI_INT_S1));
	
						Write_hfc(hc, HFCPCI_STATES, 4 | HFCPCI_LOAD_STATE);
						udelay(10);
						Write_hfc(hc, HFCPCI_STATES, 4);
						dch->ph_state = 4;
					} else {
						hc->hw.int_m1 |= HFCPCI_INTS_TIMER;
						Write_hfc(hc, HFCPCI_INT_M1, hc->hw.int_m1);
						hc->hw.ctmt &= ~HFCPCI_AUTO_TIMER;
						hc->hw.ctmt |= HFCPCI_TIM3_125;
						Write_hfc(hc, HFCPCI_CTMT, hc->hw.ctmt | HFCPCI_CLTIMER);
						Write_hfc(hc, HFCPCI_CTMT, hc->hw.ctmt | HFCPCI_CLTIMER);
						hc->hw.nt_timer = NT_T1_COUNT;
						Write_hfc(hc, HFCPCI_STATES, 2 | HFCPCI_NT_G2_G3);	/* allow G2 -> G3 transition */
					}
					upif = NULL;
					break;
				case (1):
					prim = PH_DEACTIVATE | INDICATION;
					para = 0;
					hc->hw.nt_timer = 0;
					hc->hw.int_m1 &= ~HFCPCI_INTS_TIMER;
					Write_hfc(hc, HFCPCI_INT_M1, hc->hw.int_m1);
					break;
				case (4):
					hc->hw.nt_timer = 0;
					hc->hw.int_m1 &= ~HFCPCI_INTS_TIMER;
					Write_hfc(hc, HFCPCI_INT_M1, hc->hw.int_m1);
					upif = NULL;
					break;
				case (3):
					prim = PH_ACTIVATE | INDICATION;
					para = 0;
					hc->hw.nt_timer = 0;
					hc->hw.int_m1 &= ~HFCPCI_INTS_TIMER;
					Write_hfc(hc, HFCPCI_INT_M1, hc->hw.int_m1);
					break;
				default:
					break;
			}
			dch->inst.unlock(dch->inst.data);
		}
		while(upif) {
			if_link(upif, prim, para, 0, NULL, 0);
			upif = upif->clone;
		}
	}
}

/********************************/
/* called for card init message */
/********************************/

void
inithfcpci(hfc_pci_t *hc)
{
	HFC_INFO("inithfcpci: entered\n");
	hc->dch.hw_bh = HW_hfcD_bh;
	hc->dch.dbusytimer.function = (void *) hfcpci_dbusy_timer;
	hc->dch.dbusytimer.data = (long) &hc->dch;
	init_timer(&hc->dch.dbusytimer);
	hc->chanlimit = 2;
	mode_hfcpci(&hc->bch[0], 1, -1);
	mode_hfcpci(&hc->bch[1], 2, -1);
}

#if 0
/*******************************************/
/* handle card messages from control layer */
/*******************************************/
static int
hfcpci_card_msg(hfc_pci_t *hc, int mt, void *arg)
{
	long flags;

	if (hc->debug & L1_DEB_ISAC)
		debugl1(hc, "HFCPCI: card_msg %x", mt);
	switch (mt) {
		case CARD_RESET:
			HFC_INFO("hfcpci_card_msg: CARD_RESET\n");
			reset_hfcpci(hc);
			return (0);
		case CARD_RELEASE:
			HFC_INFO("hfcpci_card_msg: CARD_RELEASE\n");
			release_io_hfcpci(hc);
			return (0);
		case CARD_INIT:
			HFC_INFO("hfcpci_card_msg: CARD_INIT\n");
			inithfcpci(hc);
			save_flags(flags);
			sti();
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout((80 * HZ) / 1000);	/* Timeout 80ms */
			/* now switch timer interrupt off */
			hc->hw.int_m1 &= ~HFCPCI_INTS_TIMER;
			Write_hfc(hc, HFCPCI_INT_M1, hc->hw.int_m1);
			/* reinit mode reg */
			Write_hfc(hc, HFCPCI_MST_MODE, hc->hw.mst_m);
			restore_flags(flags);
			return (0);
		case CARD_TEST:
			HFC_INFO("hfcpci_card_msg: CARD_TEST\n");
			return (0);
	}
	return (0);
}

#endif

static int init_card(hfc_pci_t *hc)
{
	int	cnt = 3;

	HFC_INFO("init_card: entered\n");

	lock_dev(hc, 0);
	if (request_irq(hc->irq, hfcpci_interrupt, SA_SHIRQ, "HFC PCI", hc)) {
		printk(KERN_WARNING "mISDN: couldn't get interrupt %d\n",
			hc->irq);
		unlock_dev(hc);
		return(-EIO);
	}
	while (cnt) {
		inithfcpci(hc);
		unlock_dev(hc);
		/* Finally enable IRQ output 
		 * this is only allowed, if an IRQ routine is allready
		 * established for this HFC, so don't do that earlier
		 */
		hc->hw.int_m2 = HFCPCI_IRQ_ENABLE;
		Write_hfc(hc, HFCPCI_INT_M2, hc->hw.int_m2);
		/* Timeout 80ms */
		current->state = TASK_UNINTERRUPTIBLE;
		schedule_timeout((80*HZ)/1000);
		printk(KERN_INFO "HFC PCI: IRQ %d count %d\n",
			hc->irq, hc->irqcnt);
		/* now switch timer interrupt off */
		hc->hw.int_m1 &= ~HFCPCI_INTS_TIMER;
		Write_hfc(hc, HFCPCI_INT_M1, hc->hw.int_m1);
		/* reinit mode reg */
		Write_hfc(hc, HFCPCI_MST_MODE, hc->hw.mst_m);
		if (!hc->irqcnt) {
			printk(KERN_WARNING
			       "HFC PCI: IRQ(%d) getting no interrupts during init %d\n",
			       hc->irq, 4 - cnt);
			if (cnt == 1) {
				return (-EIO);
			} else {
				reset_hfcpci(hc);
				cnt--;
			}
		} else {
			return(0);
		}
		lock_dev(hc, 0);
	}
	unlock_dev(hc);
	return(-EIO);
}

static int
SelFreeBChannel(hfc_pci_t *hc, channel_info_t *ci)
{
	bchannel_t		*bch;
	hfc_pci_t		*hfc;
	mISDNstack_t		*bst;
	u_int			cnr;
	struct list_head	*head;
	
	if (!ci)
		return(-EINVAL);
	ci->st.p = NULL;
	cnr=0;
	bst = hc->dch.inst.st;
	if (list_empty(&bst->childlist)) {
		if ((bst->id & FLG_CLONE_STACK) &&
			(bst->childlist.prev != &bst->childlist)) {
			head = bst->childlist.prev;
		} else {
			printk(KERN_ERR "%s: invalid empty childlist (no clone) stid(%x) childlist(%p<-%p->%p)\n",
				__FUNCTION__, bst->id, bst->childlist.prev, &bst->childlist, bst->childlist.next);
			return(-EINVAL);
		}
	} else
		head = &bst->childlist;
	list_for_each_entry(bst, head, list) {
		if(!bst->mgr) {
			int_errtxt("no mgr st(%p)", bst);
			return(-EINVAL);
		}
		hfc = bst->mgr->data;
		if (!hfc) {
			int_errtxt("no mgr->data st(%p)", bst);
			return(-EINVAL);
		}
		bch = &hfc->bch[cnr & 1];
		if (!(ci->channel & (~CHANNEL_NUMBER))) {
			/* only number is set */
			if ((ci->channel & 0x3) == (cnr + 1)) {
				if (bch->protocol != ISDN_PID_NONE)
					return(-EBUSY);
				bch->channel = (cnr & 1) ? 2 : 1;
				ci->st.p = bst;
				return(0);
			}
		} else if ((ci->channel & (~CHANNEL_NUMBER)) == 0x00a18300) {
			if (bch->protocol == ISDN_PID_NONE) {
				ci->st.p = bst;
				bch->channel = (cnr & 1) ? 2 : 1;
				bch->channel |= CHANNEL_EXT_PCM;
				bch->channel |= (ci->channel & 0x1f) << 16;
				bch->channel |= (ci->channel & 0x1f) << 8;
				ci->st.p = bst;
				return(0);
			}
		}
		cnr++;
	}
	return(-EBUSY);
}

#define MAX_CARDS	8
#define MODULE_PARM_T	"1-8i"
static int HFC_cnt;
static u_int protocol[MAX_CARDS];
static int layermask[MAX_CARDS];

static mISDNobject_t	HFC_obj;
static int debug;

#ifdef MODULE
MODULE_AUTHOR("Karsten Keil");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
MODULE_PARM(debug, "1i");
MODULE_PARM(protocol, MODULE_PARM_T);

/* short description of protocol
 * protocol=<p1>[,p2,p3...]
 *
 * Values:
 * the value has following structure
 * <bit  3 -  0>  D-channel protocol id
 * <bit 15 -  4>  Flags for special features
 * <bit 31 - 16>  Spare (set to 0)
 *
 * D-channel protocol ids
 * 1       1TR6 (not released yet)
 * 2       DSS1
 *
 * Feature Flags
 * bit 4   0x0010  Net side stack (NT mode)
 * bit 5   0x0020  point to point line
 * bit 6   0x0040  PCM slave mode
 * bit 7   0x0080  use negativ frame pulse
 * bit 8   0x0100  use setting from the previous HFC driver and add channels to
 *                 the previous stack, used for the second chip in 2 chip setups
 * bit 9   0x0200  switch DD/DU interface
 * bit 10 - 15     reserved
 */
 
MODULE_PARM(layermask, MODULE_PARM_T);
#endif

static char HFCName[] = "HFC_PCI";

/* this variable is used as card index when more than one cards are present */
static struct pci_dev *dev_hfcpci = NULL;


static int
setup_hfcpci(hfc_pci_t *hc)
{
	char tmp[64];
	int i=0;
	struct pci_dev *tmp_hfcpci = NULL;

#ifdef __BIG_ENDIAN
#error "not running on big endian machines now"
#endif
	strcpy(tmp, hfcpci_revision);
	printk(KERN_INFO "mISDN: HFC-PCI driver Rev. %s\n", mISDN_getrev(tmp));
	hc->hw.cirm = 0;
	hc->dch.ph_state = 0;
	while (id_list[i].vendor_id) {
		tmp_hfcpci = pci_find_device(id_list[i].vendor_id,
				id_list[i].device_id, dev_hfcpci);
		i++;
		if (tmp_hfcpci) {
			if (pci_enable_device(tmp_hfcpci))
				continue;
			pci_set_master(tmp_hfcpci);
			break;
		}
	}
	if (tmp_hfcpci) {
		i--;
		dev_hfcpci = tmp_hfcpci;	/* old device */
		hc->hw.dev = dev_hfcpci;
		hc->irq = dev_hfcpci->irq;
		if (!hc->irq) {
			printk(KERN_WARNING "HFC-PCI: No IRQ for PCI card found\n");
			return (1);
		}
		hc->hw.pci_io = (char *) get_pcibase(dev_hfcpci, 1);
		printk(KERN_INFO "mISDN: HFC-PCI card manufacturer: %s card name: %s\n", id_list[i].vendor_name, id_list[i].card_name);
	} else {
		printk(KERN_WARNING "HFC-PCI: No more PCI cards found\n");
		return (1);
	}
	if (!hc->hw.pci_io) {
		printk(KERN_WARNING "HFC-PCI: No IO-Mem for PCI card found\n");
		return (1);
	}
	/* Allocate memory for FIFOS */
	/* Because the HFC-PCI needs a 32K physical alignment, we */
	/* need to allocate the double mem and align the address */
	if (!(hc->hw.share_start = kmalloc(65536, GFP_KERNEL))) {
		printk(KERN_WARNING "HFC-PCI: Error allocating memory for FIFO!\n");
		return 1;
	}
	hc->hw.fifos = (void *)
		((((ulong) hc->hw.share_start) & ~0x7FFF) + 0x8000);
	pci_write_config_dword(hc->hw.dev, 0x80, (u_int) virt_to_bus(hc->hw.fifos));
	hc->hw.pci_io = ioremap((ulong) hc->hw.pci_io, 256);
	printk(KERN_INFO
		"HFC-PCI: defined at mem %#x fifo %#x(%#x) IRQ %d HZ %d\n",
		(u_int) hc->hw.pci_io, (u_int) hc->hw.fifos,
		(u_int) virt_to_bus(hc->hw.fifos),
		hc->irq, HZ);
	pci_write_config_word(hc->hw.dev, PCI_COMMAND, PCI_ENA_MEMIO);	/* enable memory mapped ports, disable busmaster */
	hc->hw.int_m2 = 0;	/* disable alle interrupts */
	Write_hfc(hc, HFCPCI_INT_M2, hc->hw.int_m2);
	hc->hw.int_m1 = 0;
	Write_hfc(hc, HFCPCI_INT_M1, hc->hw.int_m1);
	/* At this point the needed PCI config is done */
	/* fifos are still not enabled */
	hc->hw.timer.function = (void *) hfcpci_Timer;
	hc->hw.timer.data = (long) hc;
	init_timer(&hc->hw.timer);
	lock_dev(hc, 0);
#ifdef SPIN_DEBUG
	printk(KERN_ERR "spin_lock_adr=%p now(%p)\n", &hc->lock.spin_adr, hc->lock.spin_adr);
	printk(KERN_ERR "busy_lock_adr=%p now(%p)\n", &hc->lock.busy_adr, hc->lock.busy_adr);
#endif
	unlock_dev(hc);
	reset_hfcpci(hc);
	return (0);
}

static void
release_card(hfc_pci_t *hc) {

#ifdef LOCK_STATISTIC
	printk(KERN_INFO "try_ok(%d) try_wait(%d) try_mult(%d) try_inirq(%d)\n",
		hc->lock.try_ok, hc->lock.try_wait, hc->lock.try_mult, hc->lock.try_inirq);
	printk(KERN_INFO "irq_ok(%d) irq_fail(%d)\n",
		hc->lock.irq_ok, hc->lock.irq_fail);
#endif
	lock_dev(hc, 0);
	free_irq(hc->irq, hc);
	mode_hfcpci(&hc->bch[0], 1, ISDN_PID_NONE);
	mode_hfcpci(&hc->bch[1], 2, ISDN_PID_NONE);
	if (hc->dch.dbusytimer.function != NULL) {
		del_timer(&hc->dch.dbusytimer);
		hc->dch.dbusytimer.function = NULL;
	}
	release_io_hfcpci(hc);
	mISDN_free_bch(&hc->bch[1]);
	mISDN_free_bch(&hc->bch[0]);
	mISDN_free_dch(&hc->dch);
	HFC_obj.ctrl(hc->dch.inst.up.peer, MGR_DISCONNECT | REQUEST, &hc->dch.inst.up);
	HFC_obj.ctrl(&hc->dch.inst, MGR_UNREGLAYER | REQUEST, NULL);
	list_del(&hc->list);
	unlock_dev(hc);
	kfree(hc);
}

static int
HFC_manager(void *data, u_int prim, void *arg) {
	hfc_pci_t	*card;
	mISDNinstance_t	*inst = data;
	struct sk_buff	*skb;
	int		channel = -1;

	if (!data) {
		MGR_HASPROTOCOL_HANDLER(prim,arg,&HFC_obj)
		printk(KERN_ERR "%s: no data prim %x arg %p\n",
			__FUNCTION__, prim, arg);
		return(-EINVAL);
	}
	list_for_each_entry(card, &HFC_obj.ilist, list) {
		if (&card->dch.inst == inst) {
			channel = 2;
			break;
		}
		if (&card->bch[0].inst == inst) {
			channel = 0;
			break;
		}
		if (&card->bch[1].inst == inst) {
			inst = &card->bch[1].inst;
			channel = 1;
			break;
		}
	}
	if (channel<0) {
		printk(KERN_ERR "%s: no channel data %p prim %x arg %p\n",
			__FUNCTION__, data, prim, arg);
		return(-EINVAL);
	}

	switch(prim) {
	    case MGR_REGLAYER | CONFIRM:
		if (channel == 2)
			dch_set_para(&card->dch, &inst->st->para);
		else
			bch_set_para(&card->bch[channel], &inst->st->para);
		break;
	    case MGR_UNREGLAYER | REQUEST:
		if (channel == 2) {
			inst->down.fdata = &card->dch;
			if ((skb = create_link_skb(PH_CONTROL | REQUEST,
				HW_DEACTIVATE, 0, NULL, 0))) {
				if (HFCD_l1hw(&inst->down, skb))
					dev_kfree_skb(skb);
			}
		} else {
			inst->down.fdata = &card->bch[channel];
			if ((skb = create_link_skb(MGR_DISCONNECT | REQUEST,
				0, 0, NULL, 0))) {
				if (hfcpci_l2l1(&inst->down, skb))
					dev_kfree_skb(skb);
			}
		}
		HFC_obj.ctrl(inst->up.peer, MGR_DISCONNECT | REQUEST, &inst->up);
		HFC_obj.ctrl(inst, MGR_UNREGLAYER | REQUEST, NULL);
		break;
	    case MGR_CLRSTPARA | INDICATION:
		arg = NULL;
	    case MGR_ADDSTPARA | INDICATION:
		if (channel == 2)
			dch_set_para(&card->dch, arg);
		else
			bch_set_para(&card->bch[channel], arg);
		break;
	    case MGR_RELEASE | INDICATION:
		if (channel == 2) {
			release_card(card);
		} else {
			HFC_obj.refcnt--;
		}
		break;
	    case MGR_CONNECT | REQUEST:
		return(mISDN_ConnectIF(inst, arg));
	    case MGR_SETIF | REQUEST:
	    case MGR_SETIF | INDICATION:
		if (channel==2)
			return(mISDN_SetIF(inst, arg, prim, HFCD_l1hw, NULL,
				&card->dch));
		else
			return(mISDN_SetIF(inst, arg, prim, hfcpci_l2l1, NULL,
				&card->bch[channel]));
	    case MGR_DISCONNECT | REQUEST:
	    case MGR_DISCONNECT | INDICATION:
		return(mISDN_DisConnectIF(inst, arg));
	    case MGR_SELCHANNEL | REQUEST:
		if (channel != 2) {
			printk(KERN_WARNING "%s: selchannel not dinst\n",
				__FUNCTION__);
			return(-EINVAL);
		}
		return(SelFreeBChannel(card, arg));
	    case MGR_SETSTACK | CONFIRM:
		if ((channel!=2) && (inst->pid.global == 2)) {
			inst->down.fdata = &card->bch[channel];
			if ((skb = create_link_skb(PH_ACTIVATE | REQUEST,
				0, 0, NULL, 0))) {
				if (hfcpci_l2l1(&inst->down, skb))
					dev_kfree_skb(skb);
			}
			if (inst->pid.protocol[2] == ISDN_PID_L2_B_TRANS)
				if_link(&inst->up, DL_ESTABLISH | INDICATION,
					0, 0, NULL, 0);
			else
				if_link(&inst->up, PH_ACTIVATE | INDICATION,
					0, 0, NULL, 0);
		}
		break;
	    PRIM_NOT_HANDLED(MGR_CTRLREADY | INDICATION);
	    PRIM_NOT_HANDLED(MGR_GLOBALOPT | REQUEST);
	    default:
		printk(KERN_WARNING "%s: prim %x not handled\n",
			__FUNCTION__, prim);
		return(-EINVAL);
	}
	return(0);
}

static int __init HFC_init(void)
{
	int		err,i;
	hfc_pci_t	*card, *prev;
	mISDN_pid_t	pid;
	mISDNstack_t	*dst;

#ifdef MODULE
	HFC_obj.owner = THIS_MODULE;
#endif
	INIT_LIST_HEAD(&HFC_obj.ilist);
	HFC_obj.name = HFCName;
	HFC_obj.own_ctrl = HFC_manager;
	HFC_obj.DPROTO.protocol[0] = ISDN_PID_L0_TE_S0 |
				     ISDN_PID_L0_NT_S0;
	HFC_obj.DPROTO.protocol[1] = ISDN_PID_L1_NT_S0;
	HFC_obj.BPROTO.protocol[1] = ISDN_PID_L1_B_64TRANS |
				     ISDN_PID_L1_B_64HDLC;
	HFC_obj.BPROTO.protocol[2] = ISDN_PID_L2_B_TRANS |
				     ISDN_PID_L2_B_RAWDEV;
	if ((err = mISDN_register(&HFC_obj))) {
		printk(KERN_ERR "Can't register HFC PCI error(%d)\n", err);
		return(err);
	}
	while (HFC_cnt < MAX_CARDS) {
		if (!(card = kmalloc(sizeof(hfc_pci_t), GFP_ATOMIC))) {
			printk(KERN_ERR "No kmem for HFCcard\n");
			mISDN_unregister(&HFC_obj);
			return(-ENOMEM);
		}
		memset(card, 0, sizeof(hfc_pci_t));
		list_add_tail(&card->list, &HFC_obj.ilist);
		card->dch.debug = debug;
		lock_HW_init(&card->lock);
		card->dch.inst.lock = lock_dev;
		card->dch.inst.unlock = unlock_dev;
		mISDN_init_instance(&card->dch.inst, &HFC_obj, card);
		card->dch.inst.pid.layermask = ISDN_LAYER(0);
		sprintf(card->dch.inst.name, "HFC%d", HFC_cnt+1);
		mISDN_init_dch(&card->dch);
		for (i=0; i<2; i++) {
			card->bch[i].channel = i + 1;
			mISDN_init_instance(&card->bch[i].inst, &HFC_obj, card);
			card->bch[i].inst.pid.layermask = ISDN_LAYER(0);
			card->bch[i].inst.lock = lock_dev;
			card->bch[i].inst.unlock = unlock_dev;
			card->bch[i].debug = debug;
			sprintf(card->bch[i].inst.name, "%s B%d",
				card->dch.inst.name, i+1);
			mISDN_init_bch(&card->bch[i]);
			if (card->bch[i].dev) {
				card->bch[i].dev->wport.pif.func =
					hfcpci_l2l1;
				card->bch[i].dev->wport.pif.fdata =
					&card->bch[i];
			}
		}
		if (protocol[HFC_cnt] == 0x100) {
			if (card->list.prev == &HFC_obj.ilist)
				prev = NULL;
			else
				prev = list_entry(card->list.prev, hfc_pci_t, list);

			if (!prev) {
				int_errtxt("card(%d) no previous HFC",
					HFC_cnt);
				if (!HFC_cnt)
					mISDN_unregister(&HFC_obj);
				else
					err = 0;
				return(err);
			}
			i = HFC_cnt - 1;
			test_and_set_bit(HFC_CFG_2HFC, &prev->cfg);
			test_and_set_bit(HFC_CFG_2HFC, &card->cfg);
			test_and_set_bit(HFC_CFG_SLAVEHFC, &card->cfg);
		} else {
			prev = NULL;
			i = HFC_cnt;
		}
		mISDN_set_dchannel_pid(&pid, protocol[i], layermask[i]);
		test_and_set_bit(HFC_CFG_MASTER, &card->cfg);
		if (protocol[i] & 0x10) {
			card->dch.inst.pid.protocol[0] = ISDN_PID_L0_NT_S0;
			card->dch.inst.pid.protocol[1] = ISDN_PID_L1_NT_S0;
			pid.protocol[0] = ISDN_PID_L0_NT_S0;
			pid.protocol[1] = ISDN_PID_L1_NT_S0;
			card->dch.inst.pid.layermask |= ISDN_LAYER(1);
			pid.layermask |= ISDN_LAYER(1);
			if (layermask[i] & ISDN_LAYER(2))
				pid.protocol[2] = ISDN_PID_L2_LAPD_NET;
			card->hw.nt_mode = 1;
		} else {
			card->dch.inst.pid.protocol[0] = ISDN_PID_L0_TE_S0;
			card->hw.nt_mode = 0;
		}
		if (protocol[i] & 0x40) {
			if (pid.layermask & ISDN_LAYER(3))
				pid.protocol[3] |= ISDN_PID_L3_DF_EXTCID;
			test_and_set_bit(HFC_CFG_PCM, &card->cfg);
			test_and_set_bit(HFC_CFG_SLAVE, &card->cfg);
			test_and_clear_bit(HFC_CFG_MASTER, &card->cfg);
		}
		if (protocol[i] & 0x80) {
			test_and_set_bit(HFC_CFG_NEG_F0, &card->cfg);
		}
		if (protocol[i] & 0x200) {
			test_and_set_bit(HFC_CFG_SW_DD_DU, &card->cfg);
		}
		printk(KERN_DEBUG "HFC card %p dch %p bch1 %p bch2 %p\n",
			card, &card->dch, &card->bch[0], &card->bch[1]);
		if (setup_hfcpci(card)) {
			err = 0;
			mISDN_free_dch(&card->dch);
			mISDN_free_bch(&card->bch[1]);
			mISDN_free_bch(&card->bch[0]);
			list_del(&card->list);
			kfree(card);
			if (!HFC_cnt) {
				mISDN_unregister(&HFC_obj);
				err = -ENODEV;
			} else
				printk(KERN_INFO "HFC %d cards installed\n",
					HFC_cnt);
			return(err);
		}
		HFC_cnt++;
		if (prev) {
			dst = prev->dch.inst.st;
		} else {
			if ((err = HFC_obj.ctrl(NULL, MGR_NEWSTACK | REQUEST,
				&card->dch.inst))) {
				printk(KERN_ERR  "MGR_ADDSTACK REQUEST dch err(%d)\n", err);
				release_card(card);
				if (!HFC_cnt)
					mISDN_unregister(&HFC_obj);
				else
					err = 0;
				return(err);
			}
			dst = card->dch.inst.st;
		}
		for (i = 0; i < 2; i++) {
			if ((err = HFC_obj.ctrl(dst,
				MGR_NEWSTACK | REQUEST, &card->bch[i].inst))) {
				printk(KERN_ERR "MGR_ADDSTACK bchan error %d\n", err);
				HFC_obj.ctrl(card->dch.inst.st,
					MGR_DELSTACK | REQUEST, NULL);
				if (!HFC_cnt)
					mISDN_unregister(&HFC_obj);
				else
					err = 0;
				return(err);
			}
			card->bch[i].st = card->bch[i].inst.st;
		}
		if (protocol[HFC_cnt] != 0x100) { /* next not second HFC */
			if ((err = HFC_obj.ctrl(dst, MGR_SETSTACK | REQUEST,
				&pid))) {
				printk(KERN_ERR "MGR_SETSTACK REQUEST dch err(%d)\n",
					err);
				HFC_obj.ctrl(dst, MGR_DELSTACK | REQUEST, NULL);
				if (!HFC_cnt)
					mISDN_unregister(&HFC_obj);
				else
					err = 0;
				return(err);
			}
		}
		if ((err = init_card(card))) {
			HFC_obj.ctrl(dst, MGR_DELSTACK | REQUEST, NULL);
			if (!HFC_cnt)
				mISDN_unregister(&HFC_obj);
			else
				err = 0;
			return(err);
		}
		HFC_obj.ctrl(dst, MGR_CTRLREADY | INDICATION, NULL);
	}
	printk(KERN_INFO "HFC %d cards installed\n", HFC_cnt);
	return(0);
}

#ifdef MODULE
static void __exit HFC_cleanup(void)
{
	hfc_pci_t	*card, *next;
	int		err;

	if ((err = mISDN_unregister(&HFC_obj))) {
		printk(KERN_ERR "Can't unregister HFC PCI error(%d)\n", err);
	}
	list_for_each_entry_safe(card, next, &HFC_obj.ilist, list) {
		printk(KERN_ERR "HFC PCI card struct not empty refs %d\n",
			HFC_obj.refcnt);
		release_card(card);
	}
	return;
}

module_init(HFC_init);
module_exit(HFC_cleanup);
#endif

/* $Id: hfc_pci.c,v 0.3 2001/09/29 20:05:00 kkeil Exp $

 * hfc_pci.c     low level driver for CCD´s hfc-pci based cards
 *
 * Author     Werner Cornelius (werner@isdn4linux.de)
 *            based on existing driver for CCD hfc ISA cards
 *            type approval valid for HFC-S PCI A based card 
 *
 * Copyright 1999  by Werner Cornelius (werner@isdn-development.de)
 * Copyright 1999  by Karsten Keil (keil@isdn4linux.de)
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
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#include "hisax_hw.h"
#include "hfc_pci.h"
#include "hisaxl1.h"
#include "helper.h"
#include "debug.h"


extern const char *CardType[];

static const char *hfcpci_revision = "$Revision: 0.3 $";

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
	unsigned char cirm;
	unsigned char ctmt;
	unsigned char clkdel;
	unsigned char states;
	unsigned char conn;
	unsigned char mst_m;
	unsigned char int_m1;
	unsigned char int_m2;
	unsigned char int_s1;
	unsigned char sctrl;
        unsigned char sctrl_r;
        unsigned char sctrl_e;
        unsigned char trm;
	unsigned char stat;
	unsigned char fifo;
        unsigned char fifo_en;
        unsigned char bswapped;
        unsigned char nt_mode;
        int nt_timer;
	unsigned char pci_bus;
        unsigned char pci_device_fn;
        unsigned char *pci_io; /* start of PCI IO memory */
        void *share_start; /* shared memory for Fifos start */
        void *fifos; /* FIFO memory */ 
        int last_bfifo_cnt[2]; /* marker saving last b-fifo frame count */
	struct timer_list timer;
};

#define SPIN_DEBUG

typedef struct _hfc_pci {
	struct _hfc_pci		*prev;
	struct _hfc_pci		*next;
	u_char			subtyp;
	u_char			chanlimit;
	u_int			irq;
	u_int			addr;
	struct hfcPCI_hw	hw;
	spinlock_t		devlock;
	u_long			flags;
#ifdef SPIN_DEBUG
	void			*lock_adr;
#endif
	dchannel_t		dch;
	bchannel_t		bch[2];
} hfc_pci_t;


static void lock_dev(void *data)
{
	register u_long	flags;
	register hfc_pci_t *card = data;

	spin_lock_irqsave(&card->devlock, flags);
	card->flags = flags;
#ifdef SPIN_DEBUG
	card->lock_adr = __builtin_return_address(0);
#endif
} 

static void unlock_dev(void *data)
{
	register hfc_pci_t *card = data;

	spin_unlock_irqrestore(&card->devlock, card->flags);
#ifdef SPIN_DEBUG
	card->lock_adr = NULL;
#endif
}

/* Interface functions */

/******************************************/
/* free hardware resources used by driver */
/******************************************/
void
release_io_hfcpci(hfc_pci_t *hc)
{
	int flags;

	save_flags(flags);
	cli();
	hc->hw.int_m2 = 0;	/* interrupt output off ! */
	Write_hfc(hc, HFCPCI_INT_M2, hc->hw.int_m2);
	restore_flags(flags);
	Write_hfc(hc, HFCPCI_CIRM, HFCPCI_RESET);	/* Reset On */
	sti();
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((30 * HZ) / 1000);	/* Timeout 30ms */
	hc->hw.cirm = 0; /* Reset Off */
	Write_hfc(hc, HFCPCI_CIRM, hc->hw.cirm);
	pcibios_write_config_word(hc->hw.pci_bus, hc->hw.pci_device_fn, PCI_COMMAND, 0);	/* disable memory mapped ports + busmaster */
	del_timer(&hc->hw.timer);
	kfree(hc->hw.share_start);
	hc->hw.share_start = NULL;
	vfree(hc->hw.pci_io);
}


/********************************************************************************/
/* function called to reset the HFC PCI chip. A complete software reset of chip */
/* and fifos is done.                                                           */
/********************************************************************************/
static void
reset_hfcpci(hfc_pci_t *hc)
{
	long flags;

	save_flags(flags);
	cli();
	pcibios_write_config_word(hc->hw.pci_bus, hc->hw.pci_device_fn, PCI_COMMAND, PCI_ENA_MEMIO);	/* enable memory mapped ports, disable busmaster */
	hc->hw.int_m2 = 0;	/* interrupt output off ! */
	Write_hfc(hc, HFCPCI_INT_M2, hc->hw.int_m2);

	printk(KERN_INFO "HFC_PCI: resetting card\n");
	pcibios_write_config_word(hc->hw.pci_bus, hc->hw.pci_device_fn, PCI_COMMAND, PCI_ENA_MEMIO + PCI_ENA_MASTER);	/* enable memory ports + busmaster */
	hc->hw.cirm = HFCPCI_RESET;		/* Reset On */
	Write_hfc(hc, HFCPCI_CIRM, hc->hw.cirm);
	sti();
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((30 * HZ) / 1000);	/* Timeout 30ms */
	hc->hw.cirm = 0;			/* Reset Off */
	Write_hfc(hc, HFCPCI_CIRM, hc->hw.cirm);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((20 * HZ) / 1000);	/* Timeout 20ms */
	if (Read_hfc(hc, HFCPCI_STATUS) & 2)
		printk(KERN_WARNING "HFC-PCI init bit busy\n");

	hc->hw.fifo_en = 0x30;	/* only D fifos enabled */

	hc->hw.bswapped = 0;	/* no exchange */
	hc->hw.ctmt = HFCPCI_TIM3_125 | HFCPCI_AUTO_TIMER;
	hc->hw.trm = HFCPCI_BTRANS_THRESMASK;	/* no echo connect , threshold */
	hc->hw.sctrl = 0x40;	/* set tx_lo mode, error in datasheet ! */
	hc->hw.sctrl_r = 0;
	hc->hw.sctrl_e = HFCPCI_AUTO_AWAKE;	/* S/T Auto awake */
	hc->hw.mst_m = HFCPCI_MASTER;		/* HFC Master Mode */
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
	hc->hw.conn = 0x36;	/* set data flow directions */
	Write_hfc(hc, HFCPCI_CONNECT, hc->hw.conn);
	Write_hfc(hc, HFCPCI_B1_SSL, 0x80);	/* B1-Slot 0 STIO1 out enabled */
	Write_hfc(hc, HFCPCI_B2_SSL, 0x81);	/* B2-Slot 1 STIO1 out enabled */
	Write_hfc(hc, HFCPCI_B1_RSL, 0x80);	/* B1-Slot 0 STIO2 in enabled */
	Write_hfc(hc, HFCPCI_B2_RSL, 0x81);	/* B2-Slot 1 STIO2 in enabled */

	/* Finally enable IRQ output */
	hc->hw.int_m2 = HFCPCI_IRQ_ENABLE;
	Write_hfc(hc, HFCPCI_INT_M2, hc->hw.int_m2);
	if (Read_hfc(hc, HFCPCI_INT_S2));
	restore_flags(flags);
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


/*********************************/
/* schedule a new D-channel task */
/*********************************/
static void
sched_event_D_pci(hfc_pci_t *hc, int event)
{
	test_and_set_bit(event, &hc->dch.event);
	queue_task(&hc->dch.tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

/*********************************/
/* schedule a new b_channel task */
/*********************************/
static void
hfcpci_sched_event(bchannel_t *bch, int event)
{
	bch->event |= 1 << event;
	queue_task(&bch->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

/************************************************/
/* select a b-channel entry matching and active */
/************************************************/
static
bchannel_t *
Sel_BCS(hfc_pci_t *hc, int channel)
{
	if (hc->bch[0].protocol && (hc->bch[0].channel == channel))
		return (&hc->bch[0]);
	else if (hc->bch[1].protocol && (hc->bch[1].channel == channel))
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
		debugprint(&hc->bch[fifo].inst, "hfcpci_clear_fifo_tx%d f1(%x) f2(%x) z1(%x) z2(%x) state(%x)",
				fifo, bzt->f1, bzt->f2, bzt->za[MAX_B_FRAMES].z1, bzt->za[MAX_B_FRAMES].z2, fifo_state);
	bzt->f2 = MAX_B_FRAMES;
	bzt->f1 = bzt->f1;	/* init F pointers to remain constant */
	bzt->za[MAX_B_FRAMES].z1 = B_FIFO_SIZE + B_SUB_VAL - 1;
	bzt->za[MAX_B_FRAMES].z2 = bzt->za[MAX_B_FRAMES].z1 - 1;
	if (fifo_state)
	        hc->hw.fifo_en |= fifo_state;
	Write_hfc(hc, HFCPCI_FIFO_EN, hc->hw.fifo_en);
	if (hc->bch[fifo].debug & L1_DEB_HSCX)
		debugprint(&hc->bch[fifo].inst, "hfcpci_clear_fifo_tx%d f1(%x) f2(%x) z1(%x) z2(%x)",
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
	int		flags, total, maxlen, new_z2;
	z_type		*zp;

	save_flags(flags);
	sti();
	if ((bch->debug & L1_DEB_HSCX) && !(bch->debug & L1_DEB_HSCX_FIFO))
		debugprint(&bch->inst, "hfcpci_empty_fifo");
	zp = &bz->za[bz->f2];	/* point to Z-Regs */
	new_z2 = zp->z2 + count;	/* new position in fifo */
	if (new_z2 >= (B_FIFO_SIZE + B_SUB_VAL))
		new_z2 -= B_FIFO_SIZE;	/* buffer wrap */
	new_f2 = (bz->f2 + 1) & MAX_B_FRAMES;
	if ((count > MAX_DATA_SIZE + 3) || (count < 4) ||
	    (*(bdata + (zp->z1 - B_SUB_VAL)))) {
		if (bch->debug & L1_DEB_WARN)
			debugprint(&bch->inst, "hfcpci_empty_fifo: incoming packet invalid length %d or crc", count);
#ifdef ERROR_STATISTIC
		bch->err_inv++;
#endif
		bz->za[new_f2].z2 = new_z2;
		bz->f2 = new_f2;	/* next buffer */
		skb = NULL;
	} else if (!(skb = dev_alloc_skb(count - 3)))
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
	restore_flags(flags);
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
	if (test_and_set_bit(FLG_LOCK_ATOMIC, &dch->DFlags)) {
		debugprint(&dch->inst, "rec_dmsg blocked");
		return (1);
	}
	while (((df->f1 & D_FREG_MASK) != (df->f2 & D_FREG_MASK)) && count--) {
		zp = &df->za[df->f2 & D_FREG_MASK];
		rcnt = zp->z1 - zp->z2;
		if (rcnt < 0)
			rcnt += D_FIFO_SIZE;
		rcnt++;
		if (dch->debug & L1_DEB_ISAC)
			debugprint(&dch->inst, "hfcpci recd f1(%d) f2(%d) z1(%x) z2(%x) cnt(%d)",
				df->f1, df->f2, zp->z1, zp->z2, rcnt);

		if ((rcnt > MAX_DFRAME_LEN + 3) || (rcnt < 4) ||
		    (df->data[zp->z1])) {
			if (dch->debug & L1_DEB_WARN)
				debugprint(&dch->inst, "empty_fifo hfcpci paket inv. len %d or crc %d", rcnt, df->data[zp->z1]);
#ifdef ERROR_STATISTIC
			cs->err_rx++;
#endif
			df->f2 = ((df->f2 + 1) & MAX_D_FRAMES) | (MAX_D_FRAMES + 1);	/* next buffer */
			df->za[df->f2 & D_FREG_MASK].z2 = (zp->z2 + rcnt) & (D_FIFO_SIZE - 1);
		} else if ((skb = dev_alloc_skb(rcnt - 3))) {
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
				QuickHex(t, ptr, count);
				debugprint(&dch->inst, dch->dlog);
			}
			skb_queue_tail(&dch->rqueue, skb);
			sched_event_D_pci(hc, D_RCVBUFREADY);
		} else
			printk(KERN_WARNING "HFC-PCI: D receive out of memory\n");
	}
	test_and_clear_bit(FLG_LOCK_ATOMIC, &dch->DFlags);
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

	if (!(skb = dev_alloc_skb(fcnt)))
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
		cli();
		skb_queue_tail(&bch->rqueue, skb);
		sti();
		hfcpci_sched_event(bch, B_RCVBUFREADY);
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
	long		flags;
	hfc_pci_t	*hc = bch->inst.data;
	int		rcnt, real_fifo;
	int		receive, count = 5;
	struct sk_buff	*skb;
	bzfifo_type	*bz;
	u_char		*bdata;
	z_type		*zp;


	save_flags(flags);
	if ((bch->channel) && (!hc->hw.bswapped)) {
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
	cli();
	if (test_and_set_bit(FLG_LOCK_ATOMIC, &hc->dch.DFlags)) {
		debugprint(&bch->inst, "rec_data %d blocked", bch->channel);
		restore_flags(flags);
		return;
	}
	sti();
	if (bz->f1 != bz->f2) {
		if (bch->debug & L1_DEB_HSCX)
			debugprint(&bch->inst, "hfcpci rec %d f1(%d) f2(%d)",
				bch->channel, bz->f1, bz->f2);
		zp = &bz->za[bz->f2];

		rcnt = zp->z1 - zp->z2;
		if (rcnt < 0)
			rcnt += B_FIFO_SIZE;
		rcnt++;
		if (bch->debug & L1_DEB_HSCX)
			debugprint(&bch->inst, "hfcpci rec %d z1(%x) z2(%x) cnt(%d)",
				bch->channel, zp->z1, zp->z2, rcnt);
		if ((skb = hfcpci_empty_fifo(bch, bz, bdata, rcnt))) {
			cli();
			skb_queue_tail(&bch->rqueue, skb);
			sti();
			hfcpci_sched_event(bch, B_RCVBUFREADY);
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
	test_and_clear_bit(FLG_LOCK_ATOMIC, &hc->dch.DFlags);
	if (count && receive)
		goto Begin;
	restore_flags(flags);
	return;
}

/**************************/
/* D-channel send routine */
/**************************/
static void
hfcpci_fill_dfifo(hfc_pci_t *hc)
{
	dchannel_t	*dch = &hc->dch;
	long		flags;
	int		fcnt;
	int		count, new_z1, maxlen;
	dfifo_type	*df;
	u_char		*src, *dst, new_f1;

	if ((dch->debug & L1_DEB_ISAC) && !(dch->debug & L1_DEB_ISAC_FIFO))
		debugprint(&dch->inst, "hfcpci_fill_dfifo");

	count = dch->tx_len - dch->tx_idx;
	if (count <= 0)
		return;
	df = &((fifo_area *) (hc->hw.fifos))->d_chan.d_tx;

	if (dch->debug & L1_DEB_ISAC_FIFO)
		debugprint(&dch->inst, "hfcpci_fill_Dfifo f1(%d) f2(%d) z1(f1)(%x)",
			df->f1, df->f2,
			df->za[df->f1 & D_FREG_MASK].z1);
	fcnt = df->f1 - df->f2;	/* frame count actually buffered */
	if (fcnt < 0)
		fcnt += (MAX_D_FRAMES + 1);	/* if wrap around */
	if (fcnt > (MAX_D_FRAMES - 1)) {
		if (dch->debug & L1_DEB_ISAC)
			debugprint(&dch->inst, "hfcpci_fill_Dfifo more as 14 frames");
#ifdef ERROR_STATISTIC
		cs->err_tx++;
#endif
		return;
	}
	/* now determine free bytes in FIFO buffer */
	maxlen = df->za[df->f1 & D_FREG_MASK].z2 - df->za[df->f1 & D_FREG_MASK].z1;
	if (maxlen <= 0)
		maxlen += D_FIFO_SIZE;	/* count now contains available bytes */

	if (dch->debug & L1_DEB_ISAC)
		debugprint(&dch->inst, "hfcpci_fill_Dfifo count(%ld/%d)",
			count, maxlen);
	if (count > maxlen) {
		if (dch->debug & L1_DEB_ISAC)
			debugprint(&dch->inst, "hfcpci_fill_Dfifo no fifo mem");
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
	save_flags(flags);
	cli();
	df->za[new_f1 & D_FREG_MASK].z1 = new_z1;	/* for next buffer */
	df->za[df->f1 & D_FREG_MASK].z1 = new_z1;	/* new pos actual buffer */
	df->f1 = new_f1;	/* next frame */
	restore_flags(flags);
	if (dch->debug & L1_DEB_ISAC_FIFO) {
		char *t = dch->dlog;

		count = dch->tx_len - dch->tx_idx;
		src = dch->tx_buf + dch->tx_idx;
		t += sprintf(t, "hfcD_fill_fifo cnt %d", count);
		QuickHex(t, src, count);
		debugprint(&dch->inst, dch->dlog);
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
	int		flags, maxlen, fcnt;
	int		count, new_z1;
	bzfifo_type	*bz;
	u_char		*bdata;
	u_char		new_f1, *src, *dst;
	unsigned short	*z1t, *z2t;

	if ((bch->debug & L1_DEB_HSCX) && !(bch->debug & L1_DEB_HSCX_FIFO))
		debugprint(&bch->inst, __FUNCTION__);
	count = bch->tx_len - bch->tx_idx;
	if (bch->tx_len <= 0)
		return;
	
	save_flags(flags);
	sti();

	if ((bch->channel) && (!hc->hw.bswapped)) {
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
			debugprint(&bch->inst, "hfcpci_fill_fifo_trans%d cnt(%d) z1(%x) z2(%x)",
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
					debugprint(&bch->inst, "hfcpci_FFt fcnt(%d) maxl(%d) nz1(%x) dst(%p)",
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
				debugprint(&bch->inst, "hfcpci_fill_fifo_trans %d frame length %d discarded",
					bch->channel, bch->tx_len);
			if (test_and_clear_bit(FLG_TX_NEXT, &bch->Flag)) {
				if (bch->next_skb) {
					bch->tx_idx = 0;
					bch->tx_len = bch->next_skb->len;
					memcpy(bch->tx_buf,
						bch->next_skb->data,
						bch->tx_len);
					hfcpci_sched_event(bch, B_XMTBUFREADY);
					goto next_t_frame;
				} else
					printk(KERN_WARNING "hfcB tx irq TX_NEXT without skb\n");
			}
			test_and_clear_bit(FLG_TX_BUSY, &bch->Flag);
			bch->tx_idx = bch->tx_len;
		}
		restore_flags(flags);
		return;
	}
	if (bch->debug & L1_DEB_HSCX)
		debugprint(&bch->inst, __FUNCTION__": %d f1(%d) f2(%d) z1(f1)(%x)",
			bch->channel, bz->f1, bz->f2, bz->za[bz->f1].z1);
	fcnt = bz->f1 - bz->f2;	/* frame count actually buffered */
	if (fcnt < 0)
		fcnt += (MAX_B_FRAMES + 1);	/* if wrap around */
	if (fcnt > (MAX_B_FRAMES - 1)) {
		if (bch->debug & L1_DEB_HSCX)
			debugprint(&bch->inst, "hfcpci_fill_Bfifo more as 14 frames");
		restore_flags(flags);
		return;
	}
	/* now determine free bytes in FIFO buffer */
	maxlen = bz->za[bz->f1].z2 - bz->za[bz->f1].z1;
	if (maxlen <= 0)
		maxlen += B_FIFO_SIZE;	/* count now contains available bytes */

	if (bch->debug & L1_DEB_HSCX)
		debugprint(&bch->inst, "hfcpci_fill_fifo %d count(%ld/%d),%lx",
			bch->channel, count,
			maxlen, current->state);

	if (maxlen < count) {
		if (bch->debug & L1_DEB_HSCX)
			debugprint(&bch->inst, "hfcpci_fill_fifo no fifo mem");
		restore_flags(flags);
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
	cli();
	bz->za[new_f1].z1 = new_z1;	/* for next buffer */
	bz->f1 = new_f1;	/* next frame */
	restore_flags(flags);
	bch->tx_idx = bch->tx_len;
	test_and_clear_bit(FLG_TX_BUSY, &bch->Flag);
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
	int flags;
	int i = *(unsigned int *) ic->parm.num;

	if ((ic->arg == 98) &&
	    (!(hc->hw.int_m1 & (HFCPCI_INTS_B2TRANS + HFCPCI_INTS_B2REC + HFCPCI_INTS_B1TRANS + HFCPCI_INTS_B1REC)))) {
		save_flags(flags);
		cli();
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
		restore_flags(flags);
		debugl1(hc, "NT mode activated");
		return (0);
	}
	if ((hc->chanlimit > 1) || (hc->hw.bswapped) ||
	    (hc->hw.nt_mode) || (ic->arg != 12))
		return (-EINVAL);

	save_flags(flags);
	cli();
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
	restore_flags(flags);
	return (0);
}				/* hfcpci_auxcmd */

/*****************************/
/* E-channel receive routine */
/*****************************/
static void
receive_emsg(hfc_pci_t *hc)
{
	long flags;
	int rcnt;
	int receive, count = 5;
	bzfifo_type *bz;
	u_char *bdata;
	z_type *zp;
	u_char *ptr, *ptr1, new_f2;
	int total, maxlen, new_z2;
	u_char e_buffer[256];

	save_flags(flags);
	bz = &((fifo_area *) (hc->hw.fifos))->b_chans.rxbz_b2;
	bdata = ((fifo_area *) (hc->hw.fifos))->b_chans.rxdat_b2;
      Begin:
	count--;
	cli();
	if (test_and_set_bit(FLG_LOCK_ATOMIC, &dch->DFlags)) {
		debugl1(hc, "echo_rec_data blocked");
		restore_flags(flags);
		return;
	}
	sti();
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
					ptr += QuickHex(ptr, e_buffer, total - 3);
					ptr--;
					*ptr++ = '\n';
					*ptr = 0;
					HiSax_putstatus(hc, NULL, cs->dlog);
				} else
					HiSax_putstatus(hc, "LogEcho: ", "warning Frame too big (%d)", total - 3);
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
	test_and_clear_bit(FLG_LOCK_ATOMIC, &dch->DFlags);
	if (count && receive)
		goto Begin;
	restore_flags(flags);
	return;
}				/* receive_emsg */

#endif

/*********************/
/* Interrupt handler */
/*********************/
static void
hfcpci_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	hfc_pci_t	*hc = dev_id;
	u_char		exval;
	bchannel_t	*bch;
	int		count = 15;
	long		flags;
	u_char		val, stat;

	if (!hc) {
		printk(KERN_WARNING "HFC-PCI: Spurious interrupt!\n");
		return;
	}
	if (!(hc->hw.int_m2 & 0x08))
		return;		/* not initialised */

	if (HFCPCI_ANYINT & (stat = Read_hfc(hc, HFCPCI_STATUS))) {
		val = Read_hfc(hc, HFCPCI_INT_S1);
		if (hc->dch.debug & L1_DEB_ISAC)
			debugprint(&hc->dch.inst, "HFC-PCI: stat(%02x) s1(%02x)", stat, val);
	} else
		return;

	if (hc->dch.debug & L1_DEB_ISAC)
		debugprint(&hc->dch.inst, "HFC-PCI irq %x %s", val,
			test_bit(FLG_LOCK_ATOMIC, &hc->dch.DFlags) ?
			"locked" : "unlocked");
	val &= hc->hw.int_m1;
	if (val & 0x40) {	/* state machine irq */
		exval = Read_hfc(hc, HFCPCI_STATES) & 0xf;
		if (hc->dch.debug & L1_DEB_ISAC)
			debugprint(&hc->dch.inst, "ph_state chg %d->%d",
				hc->dch.hw.hfcpci.ph_state, exval);
		hc->dch.hw.hfcpci.ph_state = exval;
		sched_event_D_pci(hc, D_L1STATECHANGE);
		val &= ~0x40;
	}
	if (val & 0x80) {	/* timer irq */
		if (hc->hw.nt_mode) {
			if ((--hc->hw.nt_timer) < 0)
				sched_event_D_pci(hc, D_L1STATECHANGE);
		}
		val &= ~0x80;
		Write_hfc(hc, HFCPCI_CTMT, hc->hw.ctmt | HFCPCI_CLTIMER);
	}
	while (val) {
		save_flags(flags);
		cli();
		if (test_bit(FLG_LOCK_ATOMIC, &hc->dch.DFlags)) {
			hc->hw.int_s1 |= val;
			restore_flags(flags);
			return;
		}
		if (hc->hw.int_s1 & 0x18) {
			exval = val;
			val = hc->hw.int_s1;
			hc->hw.int_s1 = exval;
		}
		if (val & 0x08) {
			if (!(bch = Sel_BCS(hc, hc->hw.bswapped ? 1 : 0))) {
				if (hc->dch.debug)
					debugprint(&hc->dch.inst, "hfcpci spurious 0x08 IRQ");
			} else
				main_rec_hfcpci(bch);
		}
		if (val & 0x10) {
//			if (hc->logecho)
//				receive_emsg(hc);
//			else 
			if (!(bch = Sel_BCS(hc, 1))) {
				if (hc->dch.debug)
					debugprint(&hc->dch.inst, "hfcpci spurious 0x10 IRQ");
			} else
				main_rec_hfcpci(bch);
		}
		if (val & 0x01) {
			if (!(bch = Sel_BCS(hc, hc->hw.bswapped ? 1 : 0))) {
				if (hc->dch.debug)
					debugprint(&hc->dch.inst, "hfcpci spurious 0x01 IRQ");
			} else {
				if (bch->tx_idx < bch->tx_len) {
					if (!test_and_set_bit(FLG_LOCK_ATOMIC, &hc->dch.DFlags)) {
						hfcpci_fill_fifo(bch);
						test_and_clear_bit(FLG_LOCK_ATOMIC, &hc->dch.DFlags);
					} else
						debugprint(&bch->inst, "fill_data %d blocked", bch->channel);
				} else {
					bch->tx_idx = 0;
					if (test_and_clear_bit(FLG_TX_NEXT, &bch->Flag)) {
						if (bch->next_skb) {
							bch->tx_len = bch->next_skb->len;
							memcpy(bch->tx_buf,
								bch->next_skb->data,
								bch->tx_len);
							if (!test_and_set_bit(FLG_LOCK_ATOMIC, &hc->dch.DFlags)) {
								hfcpci_fill_fifo(bch);
								test_and_clear_bit(FLG_LOCK_ATOMIC, &hc->dch.DFlags);
							} else
								debugprint(&bch->inst, "fill_data %d blocked", bch->channel);
							hfcpci_sched_event(bch, B_XMTBUFREADY);
						} else {
							printk(KERN_WARNING "hfcB tx irq TX_NEXT without skb\n");
							test_and_clear_bit(FLG_TX_BUSY, &bch->Flag);
							bch->tx_len = 0;
						}
					} else {
						test_and_clear_bit(FLG_TX_BUSY, &bch->Flag);
						hfcpci_sched_event(bch, B_XMTBUFREADY);
						bch->tx_len = 0;
					}
				}
			}
		}
		if (val & 0x02) {
			if (!(bch = Sel_BCS(hc, 1))) {
				if (hc->dch.debug)
					debugprint(&hc->dch.inst, "hfcpci spurious 0x02 IRQ");
			} else {
				if (bch->tx_idx < bch->tx_len) {
					if (!test_and_set_bit(FLG_LOCK_ATOMIC, &hc->dch.DFlags)) {
						hfcpci_fill_fifo(bch);
						test_and_clear_bit(FLG_LOCK_ATOMIC, &hc->dch.DFlags);
					} else
						debugprint(&bch->inst, "fill_data %d blocked", bch->channel);
				} else {
					bch->tx_idx = 0;
					if (test_and_clear_bit(FLG_TX_NEXT, &bch->Flag)) {
						if (bch->next_skb) {
							bch->tx_len = bch->next_skb->len;
							memcpy(bch->tx_buf,
								bch->next_skb->data,
								bch->tx_len);
							if (!test_and_set_bit(FLG_LOCK_ATOMIC, &hc->dch.DFlags)) {
								hfcpci_fill_fifo(bch);
								test_and_clear_bit(FLG_LOCK_ATOMIC, &hc->dch.DFlags);
							} else
								debugprint(&bch->inst, "fill_data %d blocked", bch->channel);
							hfcpci_sched_event(bch, B_XMTBUFREADY);
						} else {
							printk(KERN_WARNING "hfcB tx irq TX_NEXT without skb\n");
							test_and_clear_bit(FLG_TX_BUSY, &bch->Flag);
							bch->tx_len = 0;
						}
					} else {
						test_and_clear_bit(FLG_TX_BUSY, &bch->Flag);
						hfcpci_sched_event(bch, B_XMTBUFREADY);
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
				sched_event_D_pci(hc, D_CLEARBUSY);
			if (hc->dch.tx_idx < hc->dch.tx_len) {
				if (!test_and_set_bit(FLG_LOCK_ATOMIC, &hc->dch.DFlags)) {
					hfcpci_fill_dfifo(hc);
					test_and_clear_bit(FLG_LOCK_ATOMIC, &hc->dch.DFlags);
				} else {
					debugprint(&hc->dch.inst, "hfcpci_fill_dfifo irq blocked");
				}
			} else {
				if (test_and_clear_bit(FLG_TX_NEXT, &hc->dch.DFlags)) {
					if (hc->dch.next_skb) {
						hc->dch.tx_len = hc->dch.next_skb->len;
						memcpy(hc->dch.tx_buf,
							hc->dch.next_skb->data,
							hc->dch.tx_len);
						hc->dch.tx_idx = 0;
						if (!test_and_set_bit(FLG_LOCK_ATOMIC, &hc->dch.DFlags)) {
							hfcpci_fill_dfifo(hc);
							test_and_clear_bit(FLG_LOCK_ATOMIC, &hc->dch.DFlags);
						} else {
							debugprint(&hc->dch.inst, "hfcpci_fill_dfifo irq blocked");
						}
						sched_event_D_pci(hc, D_XMTBUFREADY);
					} else {
						printk(KERN_WARNING "hfcd tx irq TX_NEXT without skb\n");
						test_and_clear_bit(FLG_TX_BUSY, &hc->dch.DFlags);
					}
				} else
					test_and_clear_bit(FLG_TX_BUSY, &hc->dch.DFlags);
			}
		}
		if (hc->hw.int_s1 && count--) {
			val = hc->hw.int_s1;
			hc->hw.int_s1 = 0;
			if (hc->dch.debug & L1_DEB_ISAC)
				debugprint(&hc->dch.inst, "HFC-PCI irq %x loop %d", val, 15 - count);
		} else
			val = 0;
		restore_flags(flags);
	}
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
HFCD_l1hw(hisaxif_t *hif, struct sk_buff *skb)
{
	dchannel_t	*dch;
	hfc_pci_t	*hc;
	int		ret = -EINVAL;
	hisax_head_t	*hh;
	long		flags;

	if (!hif || !skb)
		return(ret);
	hh = (hisax_head_t *)skb->data;
	if (skb->len < HISAX_FRAME_MIN)
		return(ret);
	dch = hif->fdata;
	hc = dch->inst.data;
	ret = 0;
	if (hh->prim == PH_DATA_REQ) {
		if (dch->next_skb) {
			debugprint(&dch->inst, " l2l1 next_skb exist this shouldn't happen");
			return(-EBUSY);
		}
		skb_pull(skb, HISAX_HEAD_SIZE);
		dch->inst.lock(dch->inst.data);
		if (test_and_set_bit(FLG_TX_BUSY, &dch->DFlags)) {
			test_and_set_bit(FLG_TX_NEXT, &dch->DFlags);
			dch->next_skb = skb;
			dch->inst.unlock(dch->inst.data);
			return(0);
		} else {
			dch->tx_len = skb->len;
			memcpy(dch->tx_buf, skb->data, dch->tx_len);
			dch->tx_idx = 0;
			if (!test_and_set_bit(FLG_LOCK_ATOMIC, &dch->DFlags)) {
				hfcpci_fill_dfifo(hc);
				test_and_clear_bit(FLG_LOCK_ATOMIC, &dch->DFlags);
			} else {
				debugprint(&dch->inst, "hfcpci_fill_dfifo blocked");
				return(-EBUSY);
			}
			dch->inst.unlock(dch->inst.data);
			return(if_addhead(&dch->inst.up, PH_DATA_CNF,
				hh->dinfo, skb));
		}
	} else if (hh->prim == (PH_SIGNAL | REQUEST)) {
		dch->inst.lock(dch->inst.data);
		if ((hh->dinfo == INFO3_P8) || (hh->dinfo == INFO3_P10)) {
			hc->hw.mst_m |= HFCPCI_MASTER;
			Write_hfc(hc, HFCPCI_MST_MODE, hc->hw.mst_m);
		} else
			ret = -EINVAL;
		dch->inst.unlock(dch->inst.data);
	} else if (hh->prim == (PH_CONTROL | REQUEST)) {
		dch->inst.lock(dch->inst.data);
		if (hh->dinfo == HW_RESET) {
			Write_hfc(hc, HFCPCI_STATES, HFCPCI_LOAD_STATE | 3);	/* HFC ST 3 */
			udelay(6);
			Write_hfc(hc, HFCPCI_STATES, 3);	/* HFC ST 2 */
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
				sched_event_D_pci(hc, D_CLEARBUSY);
		} else if (hh->dinfo == HW_POWERUP) {
			Write_hfc(hc, HFCPCI_STATES, HFCPCI_DO_ACTION);
		} else if ((hh->dinfo & HW_TESTLOOP) == HW_TESTLOOP) {
			if (1 & hh->dinfo) {
				Write_hfc(hc, HFCPCI_B1_SSL, 0x80);	/* tx slot */
				Write_hfc(hc, HFCPCI_B1_RSL, 0x80);	/* rx slot */
				save_flags(flags);
				cli();
				hc->hw.conn = (hc->hw.conn & ~7) | 1;
				Write_hfc(hc, HFCPCI_CONNECT, hc->hw.conn);
				restore_flags(flags);
			}
			if (2 & hh->dinfo) {
				Write_hfc(hc, HFCPCI_B2_SSL, 0x81);	/* tx slot */
				Write_hfc(hc, HFCPCI_B2_RSL, 0x81);	/* rx slot */
				save_flags(flags);
				cli();
				hc->hw.conn = (hc->hw.conn & ~0x38) | 0x08;
				Write_hfc(hc, HFCPCI_CONNECT, hc->hw.conn);
				restore_flags(flags);
			}
			save_flags(flags);
			cli();
			if (3 & hh->dinfo)
				hc->hw.trm |= 0x80;	/* enable IOM-loop */
			else
				hc->hw.trm &= 0x7f;	/* disable IOM-loop */
			Write_hfc(hc, HFCPCI_TRM, hc->hw.trm);
			restore_flags(flags);
		} else {
			if (dch->debug & L1_DEB_WARN)
				debugprint(&dch->inst, __FUNCTION__": unknown ctrl %x",
					hh->dinfo);
			ret = -EINVAL;
		}
		dch->inst.unlock(dch->inst.data);
	} else if (hh->prim == (PH_ACTIVATE | REQUEST)) {
		if (hc->hw.nt_mode) {
			dch->inst.lock(dch->inst.data);
			Write_hfc(hc, HFCPCI_STATES, HFCPCI_LOAD_STATE | 0); /* G0 */
			udelay(6);
			Write_hfc(hc, HFCPCI_STATES, HFCPCI_LOAD_STATE | 1); /* G1 */
			udelay(6);
			hc->hw.mst_m |= HFCPCI_MASTER;
			Write_hfc(hc, HFCPCI_MST_MODE, hc->hw.mst_m);
			udelay(6);
			Write_hfc(hc, HFCPCI_STATES, HFCPCI_ACTIVATE | HFCPCI_DO_ACTION | 1);
			dch->inst.unlock(dch->inst.data);
		} else {
			if (dch->debug & L1_DEB_WARN)
				debugprint(&dch->inst, __FUNCTION__": PH_ACTIVATE none NT mode");
			ret = -EINVAL;
		}
	} else if (hh->prim == (PH_DEACTIVATE | REQUEST)) {
		if (hc->hw.nt_mode) {
			dch->inst.lock(dch->inst.data);
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
				sched_event_D_pci(hc, D_CLEARBUSY);
			dch->inst.unlock(dch->inst.data);
		} else {
			if (dch->debug & L1_DEB_WARN)
				debugprint(&dch->inst, __FUNCTION__": PH_DEACTIVATE none NT mode");
			ret = -EINVAL;
		}
	} else {
		if (dch->debug & L1_DEB_WARN)
			debugprint(&dch->inst, __FUNCTION__": unknown prim %x",
				hh->prim);
		ret = -EINVAL;
	}
	if (!ret)
		dev_kfree_skb(skb);
	return(ret);
}

/**************************************/
/* send B-channel data if not blocked */
/**************************************/
static void
hfcpci_send_data(bchannel_t *bch)
{
	hfc_pci_t *hc = bch->inst.data;

	if (!test_and_set_bit(FLG_LOCK_ATOMIC, &hc->dch.DFlags)) {
		hfcpci_fill_fifo(bch);
		test_and_clear_bit(FLG_LOCK_ATOMIC, &hc->dch.DFlags);
	} else
		debugprint(&bch->inst, "send_data %d blocked", bch->channel);
}

/***************************************************************/
/* activate/deactivate hardware for selected channels and mode */
/***************************************************************/
static int
mode_hfcpci(bchannel_t *bch, int bc, int protocol)
{
	hfc_pci_t	*hc = bch->inst.data;
	long		flags;
	int		fifo2;

	if (bch->debug & L1_DEB_HSCX)
		debugprint(&bch->inst, "HFCPCI bchannel%d protocol %x-->%x ch %d-->%d",
			bch->channel, bch->protocol, protocol, bch->channel, bc);
	
	fifo2 = bc;
	save_flags(flags);
	cli();
	if (hc->chanlimit > 1) {
		hc->hw.bswapped = 0;	/* B1 and B2 normal mode */
		hc->hw.sctrl_e &= ~0x80;
	} else {
		if (bc) {
			if (protocol != ISDN_PID_NONE) {
				hc->hw.bswapped = 1;	/* B1 and B2 exchanged */
				hc->hw.sctrl_e |= 0x80;
			} else {
				hc->hw.bswapped = 0;	/* B1 and B2 normal mode */
				hc->hw.sctrl_e &= ~0x80;
			}
			fifo2 = 0;
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
				restore_flags(flags);
				return(0);
			}
			if (bc) {
				hc->hw.sctrl &= ~SCTRL_B2_ENA;
				hc->hw.sctrl_r &= ~SCTRL_B2_ENA;
			} else {
				hc->hw.sctrl &= ~SCTRL_B1_ENA;
				hc->hw.sctrl_r &= ~SCTRL_B1_ENA;
			}
			if (fifo2) {
				hc->hw.fifo_en &= ~HFCPCI_FIFOEN_B2;
				hc->hw.int_m1 &= ~(HFCPCI_INTS_B2TRANS + HFCPCI_INTS_B2REC);
			} else {
				hc->hw.fifo_en &= ~HFCPCI_FIFOEN_B1;
				hc->hw.int_m1 &= ~(HFCPCI_INTS_B1TRANS + HFCPCI_INTS_B1REC);
			}
			if (bch->channel)
				hc->hw.cirm &= 0x7f;
			else
				hc->hw.cirm &= 0xbf;
			bch->protocol = ISDN_PID_NONE;
			bch->channel = bc;
			break;
		case (ISDN_PID_L1_B_64TRANS):
			bch->protocol = protocol;
			bch->channel = bc;
		        hfcpci_clear_fifo_rx(hc, fifo2);
		        hfcpci_clear_fifo_tx(hc, fifo2);
			if (bc) {
				hc->hw.sctrl |= SCTRL_B2_ENA;
				hc->hw.sctrl_r |= SCTRL_B2_ENA;
				hc->hw.cirm |= 0x80;
			} else {
				hc->hw.sctrl |= SCTRL_B1_ENA;
				hc->hw.sctrl_r |= SCTRL_B1_ENA;
				hc->hw.cirm |= 0x40;
			}
			if (fifo2) {
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
		        hfcpci_clear_fifo_rx(hc, fifo2);
		        hfcpci_clear_fifo_tx(hc, fifo2);
			if (bc) {
				hc->hw.sctrl |= SCTRL_B2_ENA;
				hc->hw.sctrl_r |= SCTRL_B2_ENA;
			} else {
				hc->hw.sctrl |= SCTRL_B1_ENA;
				hc->hw.sctrl_r |= SCTRL_B1_ENA;
			}
			if (fifo2) {
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
			debugprint(&bch->inst, "prot not known %x", protocol);
			restore_flags(flags);
			return(-ENOPROTOOPT);
	}
	Write_hfc(hc, HFCPCI_SCTRL_E, hc->hw.sctrl_e);
	Write_hfc(hc, HFCPCI_INT_M1, hc->hw.int_m1);
	Write_hfc(hc, HFCPCI_FIFO_EN, hc->hw.fifo_en);
	Write_hfc(hc, HFCPCI_SCTRL, hc->hw.sctrl);
	Write_hfc(hc, HFCPCI_SCTRL_R, hc->hw.sctrl_r);
	Write_hfc(hc, HFCPCI_CTMT, hc->hw.ctmt);
	Write_hfc(hc, HFCPCI_CONNECT, hc->hw.conn);
	Write_hfc(hc, HFCPCI_CIRM, hc->hw.cirm);
	if (bch->protocol)
		hfcpci_sched_event(bch, B_XMTBUFREADY);
	restore_flags(flags);
	return(0);
}

/******************************/
/* Layer2 -> Layer 1 Transfer */
/******************************/
static int
hfcpci_l2l1(hisaxif_t *hif, struct sk_buff *skb)
{
	bchannel_t	*bch;
	int		ret = -EINVAL;
	hisax_head_t	*hh;

	if (!hif || !skb)
		return(ret);
	hh = (hisax_head_t *)skb->data;
	if (skb->len < HISAX_FRAME_MIN)
		return(ret);
	bch = hif->fdata;
	if ((hh->prim == PH_DATA_REQ) ||
		(hh->prim == (DL_DATA | REQUEST))) {
		if (bch->next_skb) {
			debugprint(&bch->inst, " l2l1 next_skb exist this shouldn't happen");
			return(-EBUSY);
		}
		skb_pull(skb, HISAX_HEAD_SIZE);
		bch->inst.lock(bch->inst.data);
		if (test_and_set_bit(FLG_TX_BUSY, &bch->Flag)) {
			test_and_set_bit(FLG_TX_NEXT, &bch->Flag);
			bch->next_skb = skb;
			bch->inst.unlock(bch->inst.data);
			return(0);
		} else {
			bch->tx_len = skb->len;
			memcpy(bch->tx_buf, skb->data, bch->tx_len);
			bch->tx_idx = 0;
			hfcpci_send_data(bch);
			bch->inst.unlock(bch->inst.data);
			if ((bch->inst.pid.protocol[2] == ISDN_PID_L2_B_RAWDEV)
				&& bch->dev)
				hif = &bch->dev->rport.pif;
			else
				hif = &bch->inst.up;
			return(if_addhead(hif, hh->prim | CONFIRM,
				hh->dinfo, skb));
		}
	} else if ((hh->prim == (PH_ACTIVATE | REQUEST)) ||
		(hh->prim == (DL_ESTABLISH  | REQUEST))) {
		if (test_and_set_bit(BC_FLG_ACTIV, &bch->Flag))
			ret = 0;
		else {
			bch->inst.lock(bch->inst.data);
			ret = mode_hfcpci(bch, bch->channel,
				bch->inst.pid.protocol[1]);
			bch->inst.unlock(bch->inst.data);
		}
		if (bch->inst.pid.protocol[2] == ISDN_PID_L2_B_RAWDEV)
			if (bch->dev)
				if_link(&bch->dev->rport.pif,
					hh->prim | CONFIRM, 0, 0, NULL, 0);
		skb_trim(skb, HISAX_HEAD_SIZE);
		return(if_newhead(&bch->inst.up, hh->prim | CONFIRM, ret, skb));
	} else if ((hh->prim == (PH_DEACTIVATE | REQUEST)) ||
		(hh->prim == (DL_RELEASE | REQUEST)) ||
		(hh->prim == (MGR_DISCONNECT | REQUEST))) {
		bch->inst.lock(bch->inst.data);
		if (test_and_clear_bit(FLG_TX_NEXT, &bch->Flag)) {
			dev_kfree_skb(bch->next_skb);
			bch->next_skb = NULL;
		}
		test_and_clear_bit(FLG_TX_BUSY, &bch->Flag);
		mode_hfcpci(bch, bch->channel, 0);
		test_and_clear_bit(BC_FLG_ACTIV, &bch->Flag);
		bch->inst.unlock(bch->inst.data);
		skb_trim(skb, HISAX_HEAD_SIZE);
		if (hh->prim != (MGR_DISCONNECT | REQUEST)) {
			if (bch->inst.pid.protocol[2] == ISDN_PID_L2_B_RAWDEV)
				if (bch->dev)
					if_link(&bch->dev->rport.pif,
						hh->prim | CONFIRM, 0, 0, NULL, 0);
			if (!if_newhead(&bch->inst.up, hh->prim | CONFIRM, 0, skb))
				return(0);
		}
		ret = 0;
	} else {
		printk(KERN_WARNING __FUNCTION__" unknown prim(%x)\n",
			hh->prim);
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
hfcD_newstate(dchannel_t *dch)
{
	hfc_pci_t	*hc = dch->inst.data;
	u_int		prim = PH_SIGNAL | INDICATION;
	u_int		para = 0;
	hisaxif_t	*upif = &dch->inst.up;

	if (!hc->hw.nt_mode) {
		printk(KERN_DEBUG __FUNCTION__": TE newstate %x\n", dch->hw.hfcpci.ph_state);
		switch (dch->hw.hfcpci.ph_state) {
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
		printk(KERN_DEBUG __FUNCTION__": NT newstate %x\n", dch->hw.hfcpci.ph_state);
		switch (dch->hw.hfcpci.ph_state) {
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
					dch->hw.hfcpci.ph_state = 4;
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
				return;
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
				return;
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
	}
	while(upif) {
		if_link(upif, prim, para, 0, NULL, 0);
		upif = upif->next;
	}
}

static void
hfcD_rcv(dchannel_t *dch)
{
	struct sk_buff	*skb;
	int		err;

	while ((skb = skb_dequeue(&dch->rqueue))) {
		err = if_addhead(&dch->inst.up, PH_DATA_IND, (int)skb, skb);
		if (err < 0) {
			printk(KERN_WARNING "HiSax: hfcD deliver err %d\n", err);
			dev_kfree_skb(skb);
		}
	}
}


static void
hfcD_bh(dchannel_t *dch)
{
	if (!dch)
		return;
	printk(KERN_DEBUG __FUNCTION__": event %x\n", dch->event);
	if (test_and_clear_bit(D_L1STATECHANGE, &dch->event))
		hfcD_newstate(dch);		
	if (test_and_clear_bit(D_XMTBUFREADY, &dch->event)) {
		struct sk_buff *skb = dch->next_skb;
		int	dinfo;

		if (skb) {
			dch->next_skb = NULL;
			skb_trim(skb, 0);
			if (skb_headroom(skb) < HISAX_HEAD_SIZE) {
				int_errtxt("skb %p %d/%d\n",
					skb, skb_headroom(skb),
					skb_tailroom(skb));
				skb_reserve(skb, HISAX_HEAD_SIZE);
			}
			dinfo = *((int *)(skb->data - 4));
			if (if_addhead(&dch->inst.up, PH_DATA_CNF, dinfo,
				skb))
				dev_kfree_skb(skb);
		}
	}
	if (test_and_clear_bit(D_RCVBUFREADY, &dch->event))
		hfcD_rcv(dch);
}

static void
hfcB_bh(bchannel_t *bch)
{
	struct sk_buff	*skb;
	u_int 		pr;
	int		ret;
	int		dinfo;
	hisaxif_t	*hif;

	if (!bch)
		return;
	if (!bch->inst.up.func) {
		printk(KERN_WARNING "HiSax: hdlc_bh without up.func\n");
		return;
	}
	printk(KERN_DEBUG __FUNCTION__": event %x\n", bch->event);
	if (bch->dev)
		printk(KERN_DEBUG __FUNCTION__": rpflg(%x) wpflg(%x)\n",
			bch->dev->rport.Flag, bch->dev->wport.Flag);
	if (test_and_clear_bit(B_XMTBUFREADY, &bch->event)) {
		skb = bch->next_skb;
		if (skb) {
			bch->next_skb = NULL;
			if (bch->inst.pid.protocol[2] == ISDN_PID_L2_B_TRANS)
				pr = DL_DATA | CONFIRM;
			else
				pr = PH_DATA | CONFIRM;
			dinfo = *((int *)(skb->data - 4));
			if ((bch->inst.pid.protocol[2] == ISDN_PID_L2_B_RAWDEV)
				&& bch->dev)
				hif = &bch->dev->rport.pif;
			else
				hif = &bch->inst.up;
			if (if_addhead(hif, pr, dinfo, skb))
				dev_kfree_skb(skb);
		}
	}
	if (test_and_clear_bit(B_RCVBUFREADY, &bch->event)) {
		if ((bch->inst.pid.protocol[2] == ISDN_PID_L2_B_RAWDEV)
			&& bch->dev)
			hif = &bch->dev->rport.pif;
		else
			hif = &bch->inst.up;
		while ((skb = skb_dequeue(&bch->rqueue))) {
			if (bch->inst.pid.protocol[2] == ISDN_PID_L2_B_TRANS)
				pr = DL_DATA | INDICATION;
			else
				pr = PH_DATA | INDICATION;
			ret = if_addhead(hif, pr, DINFO_SKB, skb);
			if (ret < 0) {
				printk(KERN_WARNING "hdlc_bh deliver err %d\n",
					ret);
				dev_kfree_skb(skb);
			}
		}
	}
}


/********************************/
/* called for card init message */
/********************************/

void
inithfcpci(hfc_pci_t *hc)
{
	hc->dch.tqueue.routine = (void *) (void *) hfcD_bh;
	hc->bch[0].tqueue.routine = (void *) (void *) hfcB_bh;
	hc->bch[1].tqueue.routine = (void *) (void *) hfcB_bh;
	hc->dch.dbusytimer.function = (void *) hfcpci_dbusy_timer;
	hc->dch.dbusytimer.data = (long) &hc->dch;
	init_timer(&hc->dch.dbusytimer);
	hc->chanlimit = 2;
	mode_hfcpci(&hc->bch[0], 0, -1);
	mode_hfcpci(&hc->bch[1], 1, -1);
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
			reset_hfcpci(hc);
			return (0);
		case CARD_RELEASE:
			release_io_hfcpci(hc);
			return (0);
		case CARD_INIT:
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
			return (0);
	}
	return (0);
}

#endif

static int init_card(hfc_pci_t *hc)
{
	int irq_cnt, cnt = 3;
	long flags;

	save_flags(flags);
	irq_cnt = kstat_irqs(hc->irq);
	printk(KERN_INFO "HFC PCI: IRQ %d count %d\n", hc->irq, irq_cnt);
	lock_dev(hc);
	if (request_irq(hc->irq, hfcpci_interrupt, SA_SHIRQ, "HFC PCI", hc)) {
		printk(KERN_WARNING "HiSax: couldn't get interrupt %d\n",
			hc->irq);
		unlock_dev(hc);
		return(-EIO);
	}
	while (cnt) {
		inithfcpci(hc);
		unlock_dev(hc);
		sti();
		/* Timeout 80ms */
		current->state = TASK_UNINTERRUPTIBLE;
		schedule_timeout((80*HZ)/1000);
		restore_flags(flags);
		printk(KERN_INFO "HFC PCI: IRQ %d count %d\n",
			hc->irq, kstat_irqs(hc->irq));
		/* now switch timer interrupt off */
		hc->hw.int_m1 &= ~HFCPCI_INTS_TIMER;
		Write_hfc(hc, HFCPCI_INT_M1, hc->hw.int_m1);
		/* reinit mode reg */
		Write_hfc(hc, HFCPCI_MST_MODE, hc->hw.mst_m);
		if (kstat_irqs(hc->irq) == irq_cnt) {
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
		lock_dev(hc);
	}
	unlock_dev(hc);
	return(-EIO);
}

#define MAX_CARDS	4
#define MODULE_PARM_T	"1-4i"
static int HFC_cnt;
static u_int protocol[MAX_CARDS];
static int layermask[MAX_CARDS];

static hisaxobject_t	HFC_obj;
static int debug;

#ifdef MODULE
MODULE_AUTHOR("Karsten Keil");
MODULE_PARM(debug, "1i");
MODULE_PARM(protocol, MODULE_PARM_T);
MODULE_PARM(layermask, MODULE_PARM_T);
#define HFC_init init_module
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
	printk(KERN_INFO "HiSax: HFC-PCI driver Rev. %s\n", HiSax_getrev(tmp));
	hc->hw.int_s1 = 0;
	hc->hw.cirm = 0;
	hc->dch.hw.hfcpci.ph_state = 0;
	hc->hw.fifo = 255;
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
		hc->hw.pci_bus = dev_hfcpci->bus->number;
		hc->hw.pci_device_fn = dev_hfcpci->devfn;
		hc->irq = dev_hfcpci->irq;
		if (!hc->irq) {
			printk(KERN_WARNING "HFC-PCI: No IRQ for PCI card found\n");
			return (1);
		}
		hc->hw.pci_io = (char *) dev_hfcpci->base_address[ 1];
		printk(KERN_INFO "HiSax: HFC-PCI card manufacturer: %s card name: %s\n", id_list[i].vendor_name, id_list[i].card_name);
	} else {
		printk(KERN_WARNING "HFC-PCI: No PCI card found\n");
		return (1);
	}
	if (!hc->hw.pci_io) {
		printk(KERN_WARNING "HFC-PCI: No IO-Mem for PCI card found\n");
		return (1);
	}
	/* Allocate memory for FIFOS */
	/* Because the HFC-PCI needs a 32K physical alignment, we */
	/* need to allocate the double mem and align the address */
	if (!((void *) hc->hw.share_start = kmalloc(65536, GFP_KERNEL))) {
		printk(KERN_WARNING "HFC-PCI: Error allocating memory for FIFO!\n");
		return 1;
	}
	(ulong) hc->hw.fifos =
		(((ulong) hc->hw.share_start) & ~0x7FFF) + 0x8000;
	pcibios_write_config_dword(hc->hw.pci_bus,
		hc->hw.pci_device_fn, 0x80,
		(u_int) virt_to_bus(hc->hw.fifos));
	hc->hw.pci_io = ioremap((ulong) hc->hw.pci_io, 256);
	printk(KERN_INFO
		"HFC-PCI: defined at mem %#x fifo %#x(%#x) IRQ %d HZ %d\n",
		(u_int) hc->hw.pci_io, (u_int) hc->hw.fifos,
		(u_int) virt_to_bus(hc->hw.fifos),
		hc->irq, HZ);
	pcibios_write_config_word(hc->hw.pci_bus, hc->hw.pci_device_fn, PCI_COMMAND, PCI_ENA_MEMIO);	/* enable memory mapped ports, disable busmaster */
	hc->hw.int_m2 = 0;	/* disable alle interrupts */
	hc->hw.int_m1 = 0;
	Write_hfc(hc, HFCPCI_INT_M1, hc->hw.int_m1);
	Write_hfc(hc, HFCPCI_INT_M2, hc->hw.int_m2);
	/* At this point the needed PCI config is done */
	/* fifos are still not enabled */
	hc->hw.timer.function = (void *) hfcpci_Timer;
	hc->hw.timer.data = (long) hc;
	init_timer(&hc->hw.timer);
	lock_dev(hc);
#ifdef SPIN_DEBUG
	printk(KERN_ERR "lock_adr=%p now(%p)\n", &hc->lock_adr, hc->lock_adr);
#endif
	unlock_dev(hc);
	reset_hfcpci(hc);
	return (0);
}

static void
release_card(hfc_pci_t *hc) {

	lock_dev(hc);
	free_irq(hc->irq, hc);
	mode_hfcpci(&hc->bch[0], 0, ISDN_PID_NONE);
	mode_hfcpci(&hc->bch[1], 1, ISDN_PID_NONE);
	if (hc->dch.dbusytimer.function != NULL) {
		del_timer(&hc->dch.dbusytimer);
		hc->dch.dbusytimer.function = NULL;
	}
	release_io_hfcpci(hc);
	free_bchannel(&hc->bch[1]);
	free_bchannel(&hc->bch[0]);
	free_dchannel(&hc->dch);
	REMOVE_FROM_LISTBASE(hc, ((hfc_pci_t *)HFC_obj.ilist));
	unlock_dev(hc);
	kfree(hc);
	HFC_obj.refcnt--;
}

static int
HFC_manager(void *data, u_int prim, void *arg) {
	hfc_pci_t	*card = HFC_obj.ilist;
	hisaxinstance_t	*inst = data;
	struct sk_buff	*skb;
	int		channel = -1;

	if (!data) {
		printk(KERN_ERR __FUNCTION__": no data prim %x arg %p\n",
			prim, arg);
		return(-EINVAL);
	}
	while(card) {
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
		card = card->next;
	}
	if (channel<0) {
		printk(KERN_ERR __FUNCTION__": no channel data %p prim %x arg %p\n",
			data, prim, arg);
		return(-EINVAL);
	}

	switch(prim) {
	    case MGR_REGLAYER | CONFIRM:
		if (!card) {
			printk(KERN_WARNING __FUNCTION__": no card found\n");
			return(-ENODEV);
		}
		break;
	    case MGR_UNREGLAYER | REQUEST:
		if (!card) {
			printk(KERN_WARNING __FUNCTION__": no card found\n");
			return(-ENODEV);
		} else {
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
			HFC_obj.ctrl(inst->up.peer, MGR_DISCONNECT | REQUEST,
				&inst->up);
			HFC_obj.ctrl(inst, MGR_UNREGLAYER | REQUEST, NULL);
		}
		break;
	    case MGR_RELEASE | INDICATION:
		if (!card) {
			printk(KERN_WARNING __FUNCTION__": no card found\n");
			return(-ENODEV);
		} else {
			if (channel == 2) {
				release_card(card);
			} else {
				HFC_obj.refcnt--;
			}
		}
		break;
	    case MGR_CONNECT | REQUEST:
		if (!card) {
			printk(KERN_WARNING __FUNCTION__": connect request failed\n");
			return(-ENODEV);
		}
		return(ConnectIF(inst, arg));
		break;
	    case MGR_SETIF | REQUEST:
	    case MGR_SETIF | INDICATION:
		if (!card) {
			printk(KERN_WARNING __FUNCTION__": setif failed\n");
			return(-ENODEV);
		}
		if (channel==2)
			return(SetIF(inst, arg, prim, HFCD_l1hw, NULL,
				&card->dch));
		else
			return(SetIF(inst, arg, prim, hfcpci_l2l1, NULL,
				&card->bch[channel]));
		break;
	    case MGR_DISCONNECT | REQUEST:
	    case MGR_DISCONNECT | INDICATION:
		if (!card) {
			printk(KERN_WARNING __FUNCTION__": del interface request failed\n");
			return(-ENODEV);
		}
		return(DisConnectIF(inst, arg));
		break;
	    case MGR_SETSTACK | CONFIRM:
		if (!card) {
			printk(KERN_WARNING __FUNCTION__": setstack failed\n");
			return(-ENODEV);
		}
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
	    default:
		printk(KERN_WARNING __FUNCTION__": prim %x not handled\n", prim);
		return(-EINVAL);
	}
	return(0);
}

int
HFC_init(void)
{
	int err,i;
	hfc_pci_t *card;
	hisax_pid_t pid;

	HFC_obj.name = HFCName;
	HFC_obj.own_ctrl = HFC_manager;
	HFC_obj.DPROTO.protocol[0] = ISDN_PID_L0_TE_S0 |
				     ISDN_PID_L0_NT_S0;
	HFC_obj.DPROTO.protocol[1] = ISDN_PID_L1_NT_S0;
	HFC_obj.BPROTO.protocol[1] = ISDN_PID_L1_B_64TRANS |
				     ISDN_PID_L1_B_64HDLC;
	HFC_obj.BPROTO.protocol[2] = ISDN_PID_L2_B_TRANS |
				     ISDN_PID_L2_B_RAWDEV;
	HFC_obj.prev = NULL;
	HFC_obj.next = NULL;
	if ((err = HiSax_register(&HFC_obj))) {
		printk(KERN_ERR "Can't register HFC PCI error(%d)\n", err);
		return(err);
	}
	while (HFC_cnt < MAX_CARDS) {
		if (!(card = kmalloc(sizeof(hfc_pci_t), GFP_ATOMIC))) {
			printk(KERN_ERR "No kmem for HFCcard\n");
			HiSax_unregister(&HFC_obj);
			return(-ENOMEM);
		}
		memset(card, 0, sizeof(hfc_pci_t));
		APPEND_TO_LIST(card, ((hfc_pci_t *)HFC_obj.ilist));
		card->dch.debug = debug;
		card->dch.inst.obj = &HFC_obj;
		spin_lock_init(&card->devlock);
		card->dch.inst.lock = lock_dev;
		card->dch.inst.unlock = unlock_dev;
		card->dch.inst.data = card;
		card->dch.inst.pid.layermask = ISDN_LAYER(0);
		set_dchannel_pid(&pid, protocol[HFC_cnt] &0xf,
			layermask[HFC_cnt]);
		if (protocol[HFC_cnt] & 0x10) {
			card->dch.inst.pid.protocol[0] = ISDN_PID_L0_NT_S0;
			card->dch.inst.pid.protocol[1] = ISDN_PID_L1_NT_S0;
			pid.protocol[0] = ISDN_PID_L0_NT_S0;
			pid.protocol[1] = ISDN_PID_L1_NT_S0;
			card->dch.inst.pid.layermask |= ISDN_LAYER(1);
			pid.layermask |= ISDN_LAYER(1);
			if (layermask[HFC_cnt] & ISDN_LAYER(2))
				pid.protocol[2] = ISDN_PID_L2_LAPD_NET;
			card->hw.nt_mode = 1;
		} else {
			card->dch.inst.pid.protocol[0] = ISDN_PID_L0_TE_S0;
			card->hw.nt_mode = 0;
		}
		card->dch.inst.up.owner = &card->dch.inst;
		card->dch.inst.down.owner = &card->dch.inst;
		HFC_obj.ctrl(NULL, MGR_DISCONNECT | REQUEST,
			&card->dch.inst.up);
		HFC_obj.ctrl(NULL, MGR_DISCONNECT | REQUEST,
			&card->dch.inst.down);
		sprintf(card->dch.inst.name, "HFC%d", HFC_cnt+1);
		init_dchannel(&card->dch);
		for (i=0; i<2; i++) {
			card->bch[i].channel = i;
			card->bch[i].inst.obj = &HFC_obj;
			card->bch[i].inst.data = card;
			card->bch[i].inst.pid.layermask = ISDN_LAYER(0);
			card->bch[i].inst.up.owner = &card->bch[i].inst;
			card->bch[i].inst.down.owner = &card->bch[i].inst;
			HFC_obj.ctrl(NULL, MGR_DISCONNECT | REQUEST,
				&card->bch[i].inst.down);
			card->bch[i].inst.lock = lock_dev;
			card->bch[i].inst.unlock = unlock_dev;
			card->bch[i].debug = debug;
			sprintf(card->bch[i].inst.name, "%s B%d",
				card->dch.inst.name, i+1);
			init_bchannel(&card->bch[i]);
			if (card->bch[i].dev) {
				card->bch[i].dev->wport.pif.func =
					hfcpci_l2l1;
				card->bch[i].dev->wport.pif.fdata =
					&card->bch[i];
			}
		}
		printk(KERN_DEBUG "HFC card %p dch %p bch1 %p bch2 %p\n",
			card, &card->dch, &card->bch[0], &card->bch[1]);
		if (setup_hfcpci(card)) {
			err = 0;
			free_dchannel(&card->dch);
			free_bchannel(&card->bch[1]);
			free_bchannel(&card->bch[0]);
			REMOVE_FROM_LISTBASE(card, ((hfc_pci_t *)HFC_obj.ilist));
			kfree(card);
			if (!HFC_cnt) {
				HiSax_unregister(&HFC_obj);
				err = -ENODEV;
			} else
				printk(KERN_INFO "HFC %d cards installed\n",
					HFC_cnt);
			return(err);
		}
		HFC_cnt++;
		if ((err = HFC_obj.ctrl(NULL, MGR_NEWSTACK | REQUEST, &card->dch.inst))) {
			printk(KERN_ERR  "MGR_ADDSTACK REQUEST dch err(%d)\n", err);
			release_card(card);
			if (!HFC_cnt)
				HiSax_unregister(&HFC_obj);
			else
				err = 0;
			return(err);
		}
		if ((err = HFC_obj.ctrl(card->dch.inst.st, MGR_SETSTACK | REQUEST, &pid))) {
			printk(KERN_ERR  "MGR_SETSTACK REQUEST dch err(%d)\n", err);
			HFC_obj.ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
			if (!HFC_cnt)
				HiSax_unregister(&HFC_obj);
			else
				err = 0;
			return(err);
		}
		for (i=0; i<2; i++) {
			if ((err = HFC_obj.ctrl(card->dch.inst.st, MGR_NEWSTACK | REQUEST,
				&card->bch[i].inst))) {
				printk(KERN_ERR "MGR_ADDSTACK bchan error %d\n", err);
				HFC_obj.ctrl(card->dch.inst.st,
					MGR_DELSTACK | REQUEST, NULL);
				if (!HFC_cnt)
					HiSax_unregister(&HFC_obj);
				else
					err = 0;
				return(err);
			}
		}
		if ((err = init_card(card))) {
			HFC_obj.ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
			if (!HFC_cnt)
				HiSax_unregister(&HFC_obj);
			else
				err = 0;
			return(err);
		}
	}
	printk(KERN_INFO "HFC %d cards installed\n", HFC_cnt);
	return(0);
}

#ifdef MODULE
int
cleanup_module(void)
{
	int err;
	if ((err = HiSax_unregister(&HFC_obj))) {
		printk(KERN_ERR "Can't unregister HFC PCI error(%d)\n", err);
		return(err);
	}
	while(HFC_obj.ilist) {
		printk(KERN_ERR "HFC PCI card struct not empty refs %d\n",
			HFC_obj.refcnt);
		release_card(HFC_obj.ilist);
	}
	return(0);
}
#endif

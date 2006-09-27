/*
 * hfc_multi.c  low level driver for hfc-4s/hfc-8s/hfc-e1 based cards
 *
 * Author	Andreas Eversberg (jolly@jolly.de)
 * ported to mqueue mechanism:
 * 		Peter Sprenger (sprengermoving-bytes.de)
 *
 * inspired by existing hfc-pci driver:
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
 *
 * Thanks to Cologne Chip AG for this great controller!
 */

/* module parameters:
 * type:
	Value 1	= HFC-E1 (1 port) 0x01
	Value 4	= HFC-4S (4 ports) 0x04
	Value 8	= HFC-8S (8 ports) 0x08
	Bit 8	= uLaw (instead of aLaw)
	Bit 9	= Enable DTMF detection on all B-channels
	Bit 10	= spare
	Bit 11	= Set PCM bus into slave mode.
	Bit 12	= Ignore missing frame clock on PCM bus.
	Bit 13	= Use direct RX clock for PCM sync rather than PLL. (E1 only)
	Bit 14	= Use external ram (128K)
	Bit 15	= Use external ram (512K)
	Bit 16	= Use 64 timeslots instead of 32
	Bit 17	= Use 128 timeslots instead of anything else
	Bit 18	= Use crystal clock for PCM and E1, for autarc clocking.
	Bit 19	= Send the Watchdog a Signal (Dual E1 with Watchdog)

 * protocol:
	NOTE: Must be given for all ports, not for the number of cards.
	HFC-4S/HFC-8S/HFC-E1 bits:
 	Bit 0-3 = protocol
	Bit 4	= NT-Mode
	Bit 5	= PTP (instead of multipoint)

	HFC-4S/HFC-8S only bits:
	Bit 16	= Use master clock for this S/T interface (ony once per chip).
	Bit 17	= transmitter line setup (non capacitive mode) DONT CARE!
	Bit 18	= Disable E-channel. (No E-channel processing)

	HFC-E1 only bits:
	Bit 16	= interface: 0=copper, 1=optical
	Bit 17	= reserved (later for 32 B-channels transparent mode)
	Bit 18	= Report LOS
	Bit 19	= Report AIS
	Bit 20	= Report SLIP
	Bit 21-22 = elastic jitter buffer (1-3), Use 0 for default.
	Bit 23  = Turn off CRC-4 Multiframe Mode, use double frame mode instead.
(all other bits are reserved and shall be 0)

 * layermask:
	NOTE: Must be given for all ports, not for the number of cards.
	mask of layers to be used for D-channel stack

 * debug:
	NOTE: only one debug value must be given for all cards
	enable debugging (see hfc_multi.h for debug options)

 * poll:
	NOTE: only one poll value must be given for all cards
	Give the number of samples for each fifo process.
	By default 128 is used. Decrease to reduce delay, increase to
	reduce cpu load. If unsure, don't mess with it!
	Valid is 8, 16, 32, 64, 128, 256.

 * pcm:
	NOTE: only one pcm value must be given for all cards
	Give the id of the PCM bus. All PCM busses with the same ID
	are expected to be connected and have equal slots.
	Only one chip of the PCM bus must be master, the others slave.
	-1 means no support of PCM bus.
 */

/* debug using register map (never use this, it will flood your system log) */
//#define HFC_REGISTER_MAP

#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#include "channel.h"
#include "layer1.h"
#include "dsp.h"
#include "debug.h"
#include <linux/isdn_compat.h>

//#warning
//#define IRQCOUNT_DEBUG

#include "hfc_multi.h"
#ifdef ECHOPREP
#include "gaintab.h"
#endif

//#warning

#define bugtest {}
#if 0
#define bugtest \
	if (hc->irq) free_irq(hc->irq, hc); \
	hc->irq = 0; \
	if (request_irq(hc->pci_dev->irq, hfcmulti_interrupt, SA_SHIRQ, "HFC-multi", hc)) { \
		printk(KERN_WARNING "mISDN: Could not get interrupt %d.\n", hc->pci_dev->irq); \
	hc->irq = hc->pci_dev->irq; }
#endif
		
static void ph_state_change(channel_t *ch);

extern const char *CardType[];

static const char *hfcmulti_revision = "$Revision: 1.55 $";

static int HFC_cnt, HFC_idx;

static mISDNobject_t	HFCM_obj;

static char HFCName[] = "HFC_multi";

/* table entry in the PCI devices list */
typedef struct {
	int vendor_id;
	int vendor_sub;
	int device_id;
	int device_sub;
	char *vendor_name;
	char *card_name;
	int type;
	int clock2;
	int leds;
} PCI_ENTRY;

static int poll_timer = 6;	/* default = 128 samples = 16ms */
/* number of POLL_TIMER interrupts for G2 timeout (min 120ms) */
static int nt_t1_count[] = { 480, 240, 120, 60, 30, 15, 8, 4 };
#define CLKDEL_TE	0x0f	/* CLKDEL in TE mode */
#define CLKDEL_NT	0x0c	/* CLKDEL in NT mode (0x60 MUST not be included!) */
static u_char silence =	0xff;	/* silence by LAW */

/* enable 32 bit fifo access (PC usage) */
#define FIFO_32BIT_ACCESS

#define VENDOR_CCD "Cologne Chip AG"
#define CCAG_VID 0x1397      // Cologne Chip Vendor ID
#define HFC4S_ID 0x08B4
#define HFC8S_ID 0x16B8
#define HFCE1_ID 0x30B1

static const PCI_ENTRY id_list[] =
{
#if 0
	{CCAG_VID, 0xffffffff, HFC4S_ID, 0xffffffff, VENDOR_CCD,
	 "HFC-4S CCAG Eval", 4, 1, 2},
	{CCAG_VID, 0xffffffff, HFC8S_ID, 0xffffffff, VENDOR_CCD,
	 "HFC-8S CCAG Eval", 8, 1, 0},
	{CCAG_VID, 0xffffffff, HFCE1_ID, 0xffffffff, VENDOR_CCD,
	 "HFC-E1 CCAG Eval", 1, 0, 1}, /* E1 only supports single clock */
#endif
	{CCAG_VID, CCAG_VID, HFC4S_ID, 0x08B4, VENDOR_CCD,
	 "HFC-4S CCAG Eval (old)", 4, 0, 0},
	{CCAG_VID, CCAG_VID, HFC8S_ID, 0x16B8, VENDOR_CCD,
	 "HFC-8S CCAG Eval (old)", 8, 0, 0},
	{CCAG_VID, CCAG_VID, HFCE1_ID, 0x30B1, VENDOR_CCD,
	 "HFC-E1 CCAG Eval (old)", 1, 0, 0},
	{CCAG_VID, CCAG_VID, HFC4S_ID, 0xB520, VENDOR_CCD,
	 "HFC-4S IOB4ST", 4, 1, 2},
	{CCAG_VID, CCAG_VID, HFC4S_ID, 0xB620, VENDOR_CCD,
	 "HFC-4S", 4, 1, 2},
	{CCAG_VID, CCAG_VID, HFC4S_ID, 0xB560, VENDOR_CCD,
	 "HFC-4S Beronet Card", 4, 1, 2},
	{CCAG_VID, CCAG_VID, HFC4S_ID, 0xB568, VENDOR_CCD,
	 "HFC-4S Beronet Card (mini PCI)", 4, 1, 2},
	{0xD161, 0xD161, 0xB410, 0xB410, VENDOR_CCD,
	 "HFC-4S Digium Card", 4, 0, 2},
	{CCAG_VID, CCAG_VID, HFC8S_ID, 0xB521, VENDOR_CCD,
	 "HFC-8S IOB4ST Recording", 8, 1, 0},
	{CCAG_VID, CCAG_VID, HFC8S_ID, 0xB522, VENDOR_CCD,
	 "HFC-8S IOB8ST", 8, 1, 0},
	{CCAG_VID, CCAG_VID, HFC8S_ID, 0xB552, VENDOR_CCD,
	 "HFC-8S", 8, 1, 0},
	{CCAG_VID, CCAG_VID, HFC8S_ID, 0xB622, VENDOR_CCD,
	 "HFC-8S", 8, 1, 0},
	{CCAG_VID, CCAG_VID, HFC8S_ID, 0xB562, VENDOR_CCD,
	 "HFC-8S Beronet Card", 8, 1, 0},
	{CCAG_VID, CCAG_VID, HFCE1_ID, 0xB523, VENDOR_CCD,
	 "HFC-E1 IOB1E1", 1, 0, 1}, /* E1 only supports single clock */
	{CCAG_VID, CCAG_VID, HFCE1_ID, 0xC523, VENDOR_CCD,
	 "HFC-E1", 1, 0, 1}, /* E1 only supports single clock */
	{CCAG_VID, CCAG_VID, HFCE1_ID, 0xB56A, VENDOR_CCD,
	 "HFC-E1 Beronet Card (mini PCI)", 1, 0, 1}, /* E1 only supports single clock */
	{CCAG_VID, CCAG_VID, HFCE1_ID, 0xB563, VENDOR_CCD,
	 "HFC-E1 Beronet Card", 1, 0, 1}, /* E1 only supports single clock */
	{CCAG_VID, CCAG_VID, HFCE1_ID, 0xB565, VENDOR_CCD,
	 "HFC-E1+ Beronet Card (Dual)", 1, 0, 1}, /* E1 only supports single clock */
	{CCAG_VID, CCAG_VID, HFCE1_ID, 0xB564, VENDOR_CCD,
	 "HFC-E1 Beronet Card (Dual)", 1, 0, 1}, /* E1 only supports single clock */
	{0x10B5, CCAG_VID, 0x9030, 0x3136, VENDOR_CCD,
	 "HFC-4S PCIBridgeEval", 4, 0, 0},      // PLX PCI-Bridge
	{CCAG_VID, CCAG_VID, HFC4S_ID, 0xB566, VENDOR_CCD,
	 "HFC-2S Beronet Card", 2, 1, 3},
	{CCAG_VID, CCAG_VID, HFC4S_ID, 0xB569, VENDOR_CCD,
	 "HFC-2S Beronet Card (mini PCI)", 2, 1, 3},
	{0, 0, 0, 0, NULL, NULL, 0, 0, 0},
};


/****************/
/* module stuff */
/****************/

/* NOTE: MAX_PORTS must be 8*MAX_CARDS */
#define MAX_CARDS	16
#define MAX_PORTS	128
static u_int type[MAX_CARDS];
static BYTE allocated[MAX_CARDS];  // remember if card is found
static int pcm[MAX_PORTS];
static u_int protocol[MAX_PORTS];
static int layermask[MAX_PORTS];
static int debug;
static int poll;


#ifdef MODULE
MODULE_AUTHOR("Andreas Eversberg");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
#ifdef OLD_MODULE_PARAM
MODULE_PARM(debug, "1i");
MODULE_PARM(poll, "1i");
#define MODULE_PARM_T   "1-128i"
MODULE_PARM(protocol, MODULE_PARM_T);
MODULE_PARM(layermask, MODULE_PARM_T);
MODULE_PARM(type, MODULE_PARM_T);
MODULE_PARM(pcm, MODULE_PARM_T);
#else
module_param(debug, uint, S_IRUGO | S_IWUSR);
module_param(poll, uint, S_IRUGO | S_IWUSR);


#ifdef OLD_MODULE_PARAM_ARRAY
static int num_type=0, num_pcm=0, num_protocol=0, num_layermask=0;
module_param_array(type, uint, num_type, S_IRUGO | S_IWUSR);
module_param_array(pcm, uint, num_pcm, S_IRUGO | S_IWUSR);
module_param_array(protocol, uint, num_protocol, S_IRUGO | S_IWUSR);
module_param_array(layermask, uint, num_layermask, S_IRUGO | S_IWUSR);
#else
module_param_array(type, uint, NULL, S_IRUGO | S_IWUSR);
module_param_array(pcm, uint, NULL, S_IRUGO | S_IWUSR);
module_param_array(protocol, uint, NULL, S_IRUGO | S_IWUSR);
module_param_array(layermask, uint, NULL, S_IRUGO | S_IWUSR);
#endif
#endif
#endif


#define HFCM_FIX_IRQS

static void
enable_hwirq(hfc_multi_t *hc)
{
	hc->hw.r_irq_ctrl |= V_GLOB_IRQ_EN;
	HFC_outb(hc, R_IRQ_CTRL, hc->hw.r_irq_ctrl);
}

static void
disable_hwirq(hfc_multi_t *hc)
{
	hc->hw.r_irq_ctrl &= ~((u_char)V_GLOB_IRQ_EN);
	HFC_outb(hc, R_IRQ_CTRL, hc->hw.r_irq_ctrl);
}


#define B410P_CARD

#define NUM_EC 2
#define MAX_TDM_CHAN 32


#ifdef B410P_CARD
inline void enablepcibridge(hfc_multi_t *c)
{
	HFC_outb(c, R_BRG_PCM_CFG, (0x0 << 6) | 0x3); /*was _io before*/
}

inline void disablepcibridge(hfc_multi_t *c)
{
	HFC_outb(c, R_BRG_PCM_CFG, (0x0 << 6) | 0x2); /*was _io before*/
}

inline unsigned char readpcibridge(hfc_multi_t *c, unsigned char address)
{
	unsigned short cipv;
	unsigned char data;
	
	// slow down a PCI read access by 1 PCI clock cycle
	HFC_outb(c, R_CTRL, 0x4); /*was _io before*/
	
	if (address == 0)
		cipv=0x4000;
	else
		cipv=0x5800;
	
	// select local bridge port address by writing to CIP port
	//data = HFC_inb(c, cipv); /*was _io before*/
	outw(cipv, c->pci_iobase + 4);
	data = inb(c->pci_iobase);
	
	// restore R_CTRL for normal PCI read cycle speed
	HFC_outb(c, R_CTRL, 0x0); /*was _io before*/
	
	return data;
}

inline void writepcibridge(hfc_multi_t *hc, unsigned char address, unsigned char data)
{
	unsigned short cipv;
	unsigned int datav;

	if (address == 0)
		cipv=0x4000;
	else
		cipv=0x5800;

	// select local bridge port address by writing to CIP port
	outw(cipv, hc->pci_iobase + 4);
	
	// define a 32 bit dword with 4 identical bytes for write sequence
	datav=data | ( (__u32) data <<8) | ( (__u32) data <<16) | ( (__u32) data <<24);

	
	// write this 32 bit dword to the bridge data port
	// this will initiate a write sequence of up to 4 writes to the same address on the local bus
	// interface
	// the number of write accesses is undefined but >=1 and depends on the next PCI transaction
	// during write sequence on the local bus
	outl(datav, hc->pci_iobase);
}
	
inline void cpld_set_reg(hfc_multi_t *hc, unsigned char reg)
{
	/* Do data pin read low byte */
	HFC_outb(hc, R_GPIO_OUT1, reg);
}

inline void cpld_write_reg(hfc_multi_t *hc, unsigned char reg, unsigned char val)
{
	cpld_set_reg(hc, reg);

	enablepcibridge(hc);
	writepcibridge(hc, 1, val);
	disablepcibridge(hc);

	return;
}

inline unsigned char cpld_read_reg(hfc_multi_t *hc, unsigned char reg)
{
	unsigned char bytein;

	cpld_set_reg(hc, reg);

	/* Do data pin read low byte */
	HFC_outb(hc, R_GPIO_OUT1, reg);

	enablepcibridge(hc);
	bytein = readpcibridge(hc, 1);
	disablepcibridge(hc);

	return bytein;
}

inline void vpm_write_address(hfc_multi_t *hc, unsigned short addr)
{
	cpld_write_reg(hc, 0, 0xff & addr);
	cpld_write_reg(hc, 1, 0x01 & (addr >> 8));
}

inline unsigned short vpm_read_address(hfc_multi_t *c)
{
	unsigned short addr;
	unsigned short highbit;
	
	addr = cpld_read_reg(c, 0);
	highbit = cpld_read_reg(c, 1);

	addr = addr | (highbit << 8);

	return addr & 0x1ff;
}

inline unsigned char vpm_in(hfc_multi_t *c, int which, unsigned short addr)
{
	unsigned char res;

	vpm_write_address(c, addr);

	if (!which)
		cpld_set_reg(c, 2);
	else
		cpld_set_reg(c, 3);

	enablepcibridge(c);
	res = readpcibridge(c, 1);
	disablepcibridge(c);

	cpld_set_reg(c, 0);

	return res;
}

inline void vpm_out(hfc_multi_t *c, int which, unsigned short addr, unsigned char data)
{
	vpm_write_address(c, addr);

	enablepcibridge(c);

	if (!which)
		cpld_set_reg(c, 2);
	else
		cpld_set_reg(c, 3);

	writepcibridge(c, 1, data);

	cpld_set_reg(c, 0);

	disablepcibridge(c);

	{
	unsigned char regin;
	regin = vpm_in(c, which, addr);
	if (regin != data)
		printk("Wrote 0x%x to register 0x%x but got back 0x%x\n", data, addr, regin);
	}
	return;
}


void vpm_init(hfc_multi_t *wc)
{
	unsigned char reg;
	unsigned int mask;
	unsigned int i, x, y;
	unsigned int ver;

	for (x=0;x<NUM_EC;x++) {
		/* Setup GPIO's */
		if (!x) {
			ver = vpm_in(wc, x, 0x1a0);
			printk("VPM: Chip %d: ver %02x\n", x, ver);
		}

		for (y=0;y<4;y++) {
			vpm_out(wc, x, 0x1a8 + y, 0x00); /* GPIO out */
			vpm_out(wc, x, 0x1ac + y, 0x00); /* GPIO dir */
			vpm_out(wc, x, 0x1b0 + y, 0x00); /* GPIO sel */
		}

		/* Setup TDM path - sets fsync and tdm_clk as inputs */
		reg = vpm_in(wc, x, 0x1a3); /* misc_con */
		vpm_out(wc, x, 0x1a3, reg & ~2);

		/* Setup Echo length (256 taps) */
		vpm_out(wc, x, 0x022, 1);
		vpm_out(wc, x, 0x023, 0xff);

		/* Setup timeslots */
		vpm_out(wc, x, 0x02f, 0x00);
		mask = 0x02020202 << (x * 4);

		/* Setup the tdm channel masks for all chips*/
		for (i = 0; i < 4; i++)
			vpm_out(wc, x, 0x33 - i, (mask >> (i << 3)) & 0xff);

		/* Setup convergence rate */
		printk("VPM: A-law mode\n");
		reg = 0x00 | 0x10 | 0x01;
		vpm_out(wc,x,0x20,reg);
		printk("VPM reg 0x20 is %x\n", reg);
		//vpm_out(wc,x,0x20,(0x00 | 0x08 | 0x20 | 0x10));

		vpm_out(wc, x, 0x24, 0x02);
		reg = vpm_in(wc, x, 0x24);
		printk("NLP Thresh is set to %d (0x%x)\n", reg, reg);

		/* Initialize echo cans */
		for (i = 0 ; i < MAX_TDM_CHAN; i++) {
			if (mask & (0x00000001 << i))
				vpm_out(wc,x,i,0x00);
		}

		udelay(10000);

		/* Put in bypass mode */
		for (i = 0 ; i < MAX_TDM_CHAN ; i++) {
			if (mask & (0x00000001 << i)) {
				vpm_out(wc,x,i,0x01);
			}
		}

		/* Enable bypass */
		for (i = 0 ; i < MAX_TDM_CHAN ; i++) {
			if (mask & (0x00000001 << i))
				vpm_out(wc,x,0x78 + i,0x01);
		}
      
	} 
}

void vpm_check(hfc_multi_t *hctmp)
{
	unsigned char gpi2;

	gpi2 = HFC_inb(hctmp, R_GPI_IN2);

	if ((gpi2 & 0x3) != 0x3) {
		printk("Got interrupt 0x%x from VPM!\n", gpi2);
	}
}


/*
 * Interface to enable/disable the HW Echocan
 *
 * these functions are called within a spin_lock_irqsave on
 * the channel instance lock, so we are not disturbed by irqs 
 *
 * we can later easily change the interface to make  other 
 * things configurable, for now we configure the taps
 *
 */
	
void vpm_echocan_on(hfc_multi_t *hc, int ch, int taps) 
{
	unsigned int timeslot;
	unsigned int unit;
	channel_t *bch = hc->chan[ch].ch;
	struct sk_buff *skb;
#ifdef TXADJ
	int txadj = -4;
#endif
	if (hc->chan[ch].protocol != ISDN_PID_L1_B_64TRANS)
		return;

	if (!bch)
		return;
#ifdef TXADJ
	skb = create_link_skb(PH_CONTROL | INDICATION, VOL_CHANGE_TX, sizeof(int), &txadj, 0);

	if (mISDN_queue_up(&bch->inst, 0, skb))
		dev_kfree_skb(skb);
#endif

	timeslot = ((ch/4)*8) + ((ch%4)*4) + 1;
	unit = ch % 4;

	printk(KERN_NOTICE "vpm_echocan_on called taps [%d] on timeslot %d\n", taps, timeslot);

	vpm_out(hc, unit, timeslot, 0x7e);
}

void vpm_echocan_off(hfc_multi_t *hc, int ch) 
{
	unsigned int timeslot;
	unsigned int unit;
	channel_t *bch = hc->chan[ch].ch;
	struct sk_buff *skb;
	int txadj = 0;

	if (hc->chan[ch].protocol != ISDN_PID_L1_B_64TRANS)
		return;

	if (!bch)
		return;

	skb = create_link_skb(PH_CONTROL | INDICATION, VOL_CHANGE_TX, sizeof(int), &txadj, 0);

	if (mISDN_queue_up(&bch->inst, 0, skb))
		dev_kfree_skb(skb);

	timeslot = ((ch/4)*8) + ((ch%4)*4) + 1;
	unit = ch % 4;

	printk(KERN_NOTICE "vpm_echocan_off called on timeslot %d\n", timeslot);
	/*FILLME*/
	vpm_out(hc, unit, timeslot, 0x01);
}

#endif  /* B410_CARD */



/******************************************/
/* free hardware resources used by driver */
/******************************************/

static void
release_io_hfcmulti(hfc_multi_t *hc)
{
	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: entered\n", __FUNCTION__);

	/* soft reset also masks all interrupts */
	hc->hw.r_cirm |= V_SRES;
	HFC_outb(hc, R_CIRM, hc->hw.r_cirm);
	udelay(1000);
	hc->hw.r_cirm &= ~V_SRES;
	HFC_outb(hc, R_CIRM, hc->hw.r_cirm);
	udelay(1000); /* instead of 'wait' that may cause locking */

	/* disable memory mapped ports / io ports */
	pci_write_config_word(hc->pci_dev, PCI_COMMAND, 0);
#ifdef CONFIG_HFCMULTI_PCIMEM
	if (hc->pci_membase) iounmap((void *)hc->pci_membase);
	if (hc->plx_membase) iounmap((void *)hc->plx_membase);
#else
	if (hc->pci_iobase)
		release_region(hc->pci_iobase, 8);
#endif

#if 1
	if (hc->pci_dev) {
		pci_disable_device(hc->pci_dev);
		pci_set_drvdata(hc->pci_dev, NULL);
	}
#endif

	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: done\n", __FUNCTION__);
}

/****************************************************************************/
/* function called to reset the HFC chip. A complete software reset of chip */
/* and fifos is done. All configuration of the chip is done.                */
/****************************************************************************/

static int
init_chip(hfc_multi_t *hc)
{
	u_long 	flags, val, val2 = 0, rev;
	int	cnt = 0;
	int	i, err = 0;
	u_char	r_conf_en,rval;

	spin_lock_irqsave(&hc->lock, flags);
	/* reset all registers */
	memset(&hc->hw, 0, sizeof(hfcmulti_hw_t));

	/* revision check */
	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: entered\n", __FUNCTION__);
	val = HFC_inb(hc, R_CHIP_ID)>>4;
	if(val!=0x8 && val!=0xc && val!=0xe)
	{
		printk(KERN_INFO "HFC_multi: unknown CHIP_ID:%x\n",(u_int)val);
		err = -EIO;
		goto out;
	}
	rev = HFC_inb(hc, R_CHIP_RV);
	printk(KERN_INFO "HFC_multi: resetting HFC with chip ID=0x%lx revision=%ld%s\n", val, rev, (rev==0)?" (old FIFO handling)":"");
	if (rev == 0) {
		test_and_set_bit(HFC_CHIP_REVISION0, &hc->chip);
		printk(KERN_WARNING "HFC_multi: NOTE: Your chip is revision 0, ask Cologne Chip for update. Newer chips have a better FIFO handling. Old chips still work but may have slightly lower HDLC transmit performance.\n");
	}
	if (rev > 1) {
		printk(KERN_WARNING "HFC_multi: WARNING: This driver doesn't consider chip revision = %ld. The chip / bridge may not work.\n", rev);
	}

/* set s-ram size */
	hc->Flen = 0x10;
	hc->Zmin = 0x80;
	hc->Zlen = 384;
	hc->DTMFbase = 0x1000;
	if (test_bit(HFC_CHIP_EXRAM_128, &hc->chip)) {
		if (debug & DEBUG_HFCMULTI_INIT)
			printk(KERN_DEBUG "%s: changing to 128K extenal RAM\n", __FUNCTION__);
		hc->hw.r_ctrl |= V_EXT_RAM;
		hc->hw.r_ram_sz = 1;
		hc->Flen = 0x20;
		hc->Zmin = 0xc0;
		hc->Zlen = 1856;
		hc->DTMFbase = 0x2000;
	}
	if (test_bit(HFC_CHIP_EXRAM_512, &hc->chip)) {
		if (debug & DEBUG_HFCMULTI_INIT)
			printk(KERN_DEBUG "%s: changing to 512K extenal RAM\n", __FUNCTION__);
		hc->hw.r_ctrl |= V_EXT_RAM;
		hc->hw.r_ram_sz = 2;
		hc->Flen = 0x20;
		hc->Zmin = 0xc0;
		hc->Zlen = 8000;
		hc->DTMFbase = 0x2000;
	}

	/* we only want the real Z2 read-pointer for revision > 0 */
	if (!test_bit(HFC_CHIP_REVISION0, &hc->chip))
		hc->hw.r_ram_sz |= V_FZ_MD;

	/* soft reset */
	HFC_outb(hc, R_CTRL, hc->hw.r_ctrl);
	HFC_outb(hc, R_RAM_SZ, hc->hw.r_ram_sz);
	HFC_outb(hc, R_FIFO_MD, 0);
	hc->hw.r_cirm = V_SRES | V_HFCRES | V_PCMRES | V_STRES | V_RLD_EPR;
	HFC_outb(hc, R_CIRM, hc->hw.r_cirm);
	udelay(100);
	hc->hw.r_cirm = 0;
	HFC_outb(hc, R_CIRM, hc->hw.r_cirm);
	udelay(100);
	HFC_outb(hc, R_RAM_SZ, hc->hw.r_ram_sz);


	/* set pcm mode & reset */
	if (test_bit(HFC_CHIP_PCM_SLAVE, &hc->chip)) {
		if (debug & DEBUG_HFCMULTI_INIT) printk(KERN_DEBUG "%s: setting PCM into slave mode\n", __FUNCTION__);
	} else {
		if (debug & DEBUG_HFCMULTI_INIT) printk(KERN_DEBUG "%s: setting PCM into master mode\n", __FUNCTION__);
		hc->hw.r_pcm_md0 |= V_PCM_MD;
	}

	// RAM access test
	HFC_outb(hc, R_RAM_ADDR0, 0);
	HFC_outb(hc, R_RAM_ADDR1, 0);
	HFC_outb(hc, R_RAM_ADDR2, 0);

	for(i=0;i<256;i++) {
		HFC_outb(hc, R_RAM_ADDR0,i);
		HFC_outb(hc, R_RAM_DATA,((i*3)&0xff));
		//udelay(5);
		//HFC_outb(hc, R_RAM_DATA,((i*3)&0xff));
	}

	for(i=0;i<256;i++) {
		HFC_outb(hc, R_RAM_ADDR0,i);
		HFC_inb(hc, R_RAM_DATA);
		rval=HFC_inb(hc, R_INT_DATA);
		if(rval!=((i*3)&0xff))
		{
			printk(KERN_DEBUG "addr:%x val:%x should:%x\n",i,rval,(i*3)&0xff);
			err++;
		}
	}

	if (err) {
		printk(KERN_DEBUG "aborting.1 - %d RAM access errors\n",err);
		err = -EIO;
		goto out;
	}

	if (test_bit(HFC_CHIP_DIGICARD,&hc->chip)) {
		HFC_outb(hc, R_BRG_PCM_CFG, 0x2);
		HFC_outb(hc, R_PCM_MD0, (0x9<<4) | 0x1);
		HFC_outb(hc, R_PCM_MD1, 0);
	
		printk(KERN_NOTICE "Setting GPIOs\n");
		HFC_outb(hc, R_GPIO_SEL, 0x30);
		HFC_outb(hc, R_GPIO_EN1, 0x3);

		udelay(1000);
		
		printk(KERN_NOTICE "calling vpm_init\n");
		
		vpm_init(hc);

	} else {
		HFC_outb(hc, R_PCM_MD0, hc->hw.r_pcm_md0 | 0x90);
		if (hc->slots == 32)
			HFC_outb(hc, R_PCM_MD1, 0x00);
		if (hc->slots == 64)
			HFC_outb(hc, R_PCM_MD1, 0x10);
		if (hc->slots == 128)
			HFC_outb(hc, R_PCM_MD1, 0x20);
		HFC_outb(hc, R_PCM_MD0, hc->hw.r_pcm_md0 | 0xa0);
		HFC_outb(hc, R_PCM_MD2, 0x00);
		HFC_outb(hc, R_PCM_MD0, hc->hw.r_pcm_md0 | 0x00);
		
	}

	i = 0;
	while (i < 256) {
		HFC_outb_(hc, R_SLOT, i);
		HFC_outb_(hc, A_SL_CFG, 0);
		HFC_outb_(hc, A_CONF, 0);
		hc->slot_owner[i] = -1;
		i++;
	}

	/* set clock speed */
	if (test_bit(HFC_CHIP_CLOCK2, &hc->chip)) {
		if (debug & DEBUG_HFCMULTI_INIT)
			printk(KERN_DEBUG "%s: setting double clock\n", __FUNCTION__);
		HFC_outb(hc, R_BRG_PCM_CFG, V_PCM_CLK);
	}

	/* check if R_F0_CNT counts */
	val = HFC_inb(hc, R_F0_CNTL);
	val += HFC_inb(hc, R_F0_CNTH) << 8;
	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "HFC_multi F0_CNT %ld after status ok\n", val);
	set_current_state(TASK_UNINTERRUPTIBLE);
	while (cnt < 50) { /* max 50 ms */
		spin_unlock_irqrestore(&hc->lock, flags);
		schedule_timeout((HZ*10)/1000); /* Timeout 10ms */
		spin_lock_irqsave(&hc->lock, flags);
		cnt+=10;
		val2 = HFC_inb(hc, R_F0_CNTL);
		val2 += HFC_inb(hc, R_F0_CNTH) << 8;
		if (val2 >= val+4) /* wait 4 pulses */
			break;
	}
	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "HFC_multi F0_CNT %ld after %dms\n", val2, cnt);

	if (val2 < val+4) {
		printk(KERN_ERR "HFC_multi ERROR 125us pulse not counting.\n");
		if (test_bit(HFC_CHIP_PCM_SLAVE, &hc->chip)) {
			printk(KERN_ERR "HFC_multi This happens in PCM slave mode without connected master.\n");
		}
		if (test_bit(HFC_CHIP_DIGICARD, &hc->chip)) {
			printk(KERN_ERR "HFC_multi ingoring PCM clock for digicard.\n");
			
		} else {
			if (!test_bit(HFC_CHIP_CLOCK_IGNORE, &hc->chip) ){ 
				err = -EIO;
				goto out;
			}
		}
	}

	/* set up timer */
	HFC_outb(hc, R_TI_WD, poll_timer);
	hc->hw.r_irqmsk_misc |= V_TI_IRQMSK;

	/* set E1 state machine IRQ */
	if (hc->type == 1)
		hc->hw.r_irqmsk_misc |= V_STA_IRQMSK;

	/* set DTMF detection */
	if (test_bit(HFC_CHIP_DTMF, &hc->chip)) {
		if (debug & DEBUG_HFCMULTI_INIT)
			printk(KERN_DEBUG "%s: enabling DTMF detection for all B-channel\n", __FUNCTION__);
		hc->hw.r_dtmf = V_DTMF_EN | V_DTMF_STOP;
		if (test_bit(HFC_CHIP_ULAW, &hc->chip))
			hc->hw.r_dtmf |= V_ULAW_SEL;
		HFC_outb(hc, R_DTMF_N, 102-1);
		hc->hw.r_irqmsk_misc |= V_DTMF_IRQMSK;
	}

	/* conference engine */
	if (test_bit(HFC_CHIP_ULAW, &hc->chip))
		r_conf_en = V_CONF_EN | V_ULAW;
	else
		r_conf_en = V_CONF_EN;
	HFC_outb(hc, R_CONF_EN, r_conf_en);

	/* setting leds */
	switch(hc->leds) {
		case 1: /* HFC-E1 OEM */
		if (test_bit(HFC_CHIP_WATCHDOG, &hc->chip)) 
			HFC_outb(hc, R_GPIO_SEL, 0x32);
		else
			HFC_outb(hc, R_GPIO_SEL, 0x30);

		HFC_outb(hc, R_GPIO_EN1, 0x0f);
		HFC_outb(hc, R_GPIO_OUT1, 0x00);

		HFC_outb(hc, R_GPIO_EN0, V_GPIO_EN2 | V_GPIO_EN3);
		break;

		case 2: /* HFC-4S OEM */
		case 3:
		HFC_outb(hc, R_GPIO_SEL, 0xf0);
		HFC_outb(hc, R_GPIO_EN1, 0xff);
		HFC_outb(hc, R_GPIO_OUT1, 0x00);
		break;
	}

	/* set master clock */
	if (hc->masterclk >= 0) {
		if (debug & DEBUG_HFCMULTI_INIT)
			printk(KERN_DEBUG "%s: setting ST master clock to port %d (0..%d)\n", __FUNCTION__, hc->masterclk, hc->type-1);
		hc->hw.r_st_sync = hc->masterclk | V_AUTO_SYNC;
		HFC_outb(hc, R_ST_SYNC, hc->hw.r_st_sync);
	}

	/* setting misc irq */
	HFC_outb(hc, R_IRQMSK_MISC, hc->hw.r_irqmsk_misc);
	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "r_irqmsk_misc.2: 0x%x\n", hc->hw.r_irqmsk_misc);

	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: done\n", __FUNCTION__);
out:
	spin_unlock_irqrestore(&hc->lock, flags);
	return(err);
}


/************************/
/* control the watchdog */
/************************/
static void
hfcmulti_watchdog(hfc_multi_t *hc)
{
	hc->wdcount++;

	if (hc->wdcount > 10 ) {
		hc->wdcount=0;
		hc->wdbyte = hc->wdbyte==V_GPIO_OUT2?V_GPIO_OUT3:V_GPIO_OUT2;

	/**	printk("Sending Watchdog Kill %x\n",hc->wdbyte); **/
		HFC_outb(hc, R_GPIO_EN0, V_GPIO_EN2 | V_GPIO_EN3);
		HFC_outb(hc, R_GPIO_OUT0, hc->wdbyte);
	}
}



/***************/
/* output leds */
/***************/
static void
hfcmulti_leds(hfc_multi_t *hc)
{
	int i, state, active;
	channel_t *dch;
	int led[4];

	hc->ledcount += poll;
	if (hc->ledcount > 4096)
		hc->ledcount -= 4096;

	switch(hc->leds) {
		case 1: /* HFC-E1 OEM */
		/* 2 red blinking: LOS
		   1 red: AIS
		   left green: PH_ACTIVATE
		   right green flashing: FIFO activity
		*/
		i = HFC_inb(hc, R_GPI_IN0) & 0xf0;
		if (!(i & 0x40)) { /* LOS */
			if (hc->e1_switch != i) {
				hc->e1_switch = i;
				hc->hw.r_tx0 &= ~V_OUT_EN;
				HFC_outb(hc, R_TX0, hc->hw.r_tx0);
			}
			if (hc->ledcount & 512)
				led[0] = led[1] = 1;
			else
				led[0] = led[1] = 0;
			led[2] = led[3] = 0;
		} else
		if (!(i & 0x80)) { /* AIS */
			if (hc->e1_switch != i) {
				hc->e1_switch = i;
				hc->hw.r_tx0 |= V_OUT_EN;
				hc->hw.r_tx1 |= V_AIS_OUT;
				HFC_outb(hc, R_TX0, hc->hw.r_tx0);
				HFC_outb(hc, R_TX1, hc->hw.r_tx1);
			}
			if (hc->ledcount & 512)
				led[2] = led[3] = 1;
			else
				led[2] = led[3] = 0;
			led[0] = led[1] = 0;
		} else {
			if (hc->e1_switch != i) {
				/* reset LOS/AIS */
				hc->e1_switch = i;
				hc->hw.r_tx0 |= V_OUT_EN;
				hc->hw.r_tx1 &= ~V_AIS_OUT;
				HFC_outb(hc, R_TX0, hc->hw.r_tx0);
				HFC_outb(hc, R_TX1, hc->hw.r_tx1);
			}
			if (HFC_inb_(hc, R_RX_STA0) & V_SIG_LOS) {
				if (hc->ledcount>>11)
					led[0] = led[1] = 1; /* both red blinking */
				else
					led[0] = led[1] = 0;
			} else
			if (HFC_inb_(hc, R_RX_STA0) & V_AIS) {
				led[0] = led[1] = 1; /* both red */
			} else {
				led[0] = led[1] = 0; /* no red */
			}
			state = 0;
			active = 1;
			dch = hc->chan[16].ch;
			if (dch && test_bit(FLG_DCHANNEL, &dch->Flags))
				state = dch->state;
			if (state == active) {
				led[2] = 1; /* left green */
				if (hc->activity[0]) {
					led[3] = 1; /* right green */
					hc->activity[0] = 0;
				} else
					led[3] = 0; /* no right green */

			} else
				led[2] = led[3] = 0; /* no green */
		}
		HFC_outb(hc, R_GPIO_OUT1,
			(led[0] | (led[1]<<2) | (led[2]<<1) | (led[3]<<3))^0xf); /* leds are inverted */

		break;

		case 2: /* HFC-4S OEM */
		/* red blinking = PH_DEACTIVATE
		   red steady = PH_ACTIVATE
		   green flashing = fifo activity
		*/
		i = 0;
		while(i < 4) {
			state = 0;
			active = -1;
			dch = hc->chan[(i<<2)|2].ch;
			if (dch && test_bit(FLG_DCHANNEL, &dch->Flags)) {
				state = dch->state;
				active = test_bit(HFC_CFG_NTMODE, &hc->chan[dch->channel].cfg)?3:7;
			}
			if (state) {
				if (state==active) {
					if (hc->activity[i]) {
						led[i] = 1; /* led green */
						hc->activity[i] = 0;
					} else
						led[i] = 2; /* led red */
				} else if (hc->ledcount>>11)
					led[i] = 2; /* led red */
				else
					led[i] = 0; /* led off */
			} else
				led[i] = 0; /* led off */
			i++;
		}

		if (test_bit(HFC_CHIP_DIGICARD, &hc->chip)) {
			int leds=0;
			for (i=0; i<4; i++) {
				if (led[i]==1) {
					/*green*/
					leds |=( 0x2 <<(i*2));
				} else if (led[i]==2) {
					/*red*/
					leds |=( 0x1 <<(i*2));
				}
			}
			vpm_out(hc, 0, 0x1a8+3,leds);
		} else {
			HFC_outb(hc, R_GPIO_EN1,
				 ((led[0]>0)<<0) | ((led[1]>0)<<1) |
				 ((led[2]>0)<<2) | ((led[3]>0)<<3));
			HFC_outb(hc, R_GPIO_OUT1,
				 ((led[0]&1)<<0) | ((led[1]&1)<<1) |
				 ((led[2]&1)<<2) | ((led[3]&1)<<3));
		}
		break;

		case 3:
		/* red blinking = PH_DEACTIVATE
		   red steady = PH_ACTIVATE
		   green flashing = fifo activity
		*/
		for(i=0;i<2;i++) {
			state = 0;
			active = -1;
			dch = hc->chan[(i<<2)|2].ch;
			if (dch && test_bit(FLG_DCHANNEL, &dch->Flags)) {
				state = dch->state;
				active = test_bit(HFC_CFG_NTMODE, &hc->chan[dch->channel].cfg)?3:7;
			}
			if (state) {
				if (state==active) {
					if (hc->activity[i]) {
						led[i] = 1; /* led green */
						hc->activity[i] = 0;
					} else
						led[i] = 2; /* led red */
				} else if (hc->ledcount>>11)
					led[i] = 2; /* led red */
				else
					led[i] = 0; /* led off */
			} else
				led[i] = 0; /* led off */
		}

	//	printk("leds %d %d\n", led[0], led[1]);
	//LEDME

		HFC_outb(hc, R_GPIO_EN1,
			((led[0]>0)<<2) | ((led[1]>0)<<3) );
		HFC_outb(hc, R_GPIO_OUT1,
			((led[0]&1)<<2) | ((led[1]&1)<<3) );

		break;
	}
}
/**************************/
/* read dtmf coefficients */
/**************************/

static void
hfcmulti_dtmf(hfc_multi_t *hc)
{
	signed long coeff[16];
	DWORD mantissa;
	int co, ch;
	channel_t *bch = NULL;
	BYTE exponent;
	int dtmf = 0;
	int addr;
	WORD w_float;
	struct sk_buff *skb;

	if (debug & DEBUG_HFCMULTI_DTMF)
		printk(KERN_DEBUG "%s: dtmf detection irq\n", __FUNCTION__);
	ch = 0;
	while(ch < 32) {
		// only process enabled B-channels
		bch = hc->chan[ch].ch;
		if ((!bch) || !test_bit(FLG_BCHANNEL, &bch->Flags)) {
			ch++;
			continue;
		}
		if (!hc->created[hc->chan[ch].port]) {
			ch++;
			continue;
		}
		if (!test_bit(FLG_TRANSPARENT, &bch->Flags)) {
			ch++;
			continue;
		}
		if (debug & DEBUG_HFCMULTI_DTMF)
			printk(KERN_DEBUG "%s: dtmf channel %d:", __FUNCTION__, ch);
		dtmf = 1;
		co = 0;
		while(co < 8) {
			// read W(n-1) coefficient
			addr = hc->DTMFbase + ((co<<7) | (ch<<2));
			HFC_outb_(hc, R_RAM_ADDR0, addr);
			HFC_outb_(hc, R_RAM_ADDR1, addr>>8);
			HFC_outb_(hc, R_RAM_ADDR2, (addr>>16) | V_ADDR_INC);
			w_float = HFC_inb_(hc, R_RAM_DATA);
#ifdef CONFIG_HFCMULTI_PCIMEM
			w_float |= (HFC_inb_(hc, R_RAM_DATA) << 8);
#else
			w_float |= (HFC_getb(hc) << 8);
#endif
			if (debug & DEBUG_HFCMULTI_DTMF)
				printk(" %04x", w_float);

			// decode float (see chip doc)
			mantissa = w_float & 0x0fff;
			if (w_float & 0x8000)
				mantissa |= 0xfffff000;
			exponent = (w_float>>12) & 0x7;
			if (exponent) {
				mantissa ^= 0x1000;
				mantissa <<= (exponent-1);
			}

			// store coefficient
			coeff[co<<1] = mantissa;

			// read W(n) coefficient
			w_float = HFC_inb_(hc, R_RAM_DATA);
#ifdef CONFIG_HFCMULTI_PCIMEM
			w_float |= (HFC_inb_(hc, R_RAM_DATA) << 8);
#else
			w_float |= (HFC_getb(hc) << 8);
#endif
			if (debug & DEBUG_HFCMULTI_DTMF)
				printk(" %04x", w_float);

			// decode float (see chip doc)
			mantissa = w_float & 0x0fff;
			if (w_float & 0x8000)
				mantissa |= 0xfffff000;
			exponent = (w_float>>12) & 0x7;
			if (exponent) {
				mantissa ^= 0x1000;
				mantissa <<= (exponent-1);
			}

			// store coefficient
			coeff[(co<<1)|1] = mantissa;
			co++;
		}
		skb = create_link_skb(PH_CONTROL | INDICATION, HW_HFC_COEFF, sizeof(coeff), coeff, 0);
		if (!skb) {
			printk(KERN_WARNING "%s: No memory for skb\n", __FUNCTION__);
			ch++;
			continue;
		}
		if (debug & DEBUG_HFCMULTI_DTMF) {
			printk("\n");
			printk("%s: DTMF ready %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx len=%d\n", __FUNCTION__,
			coeff[0], coeff[1], coeff[2], coeff[3], coeff[4], coeff[5], coeff[6], coeff[7], skb->len);
		}

#ifdef FIXME   // TODO changed
				if ((bch->inst.pid.protocol[2] == ISDN_PID_L2_B_RAWDEV) && bch->dev)
					hif = &bch->dev->rport.pif;
				else
					hif = &bch->inst.up;
#endif
		if (mISDN_queue_up(&bch->inst, 0, skb))
			dev_kfree_skb(skb);

		ch++;
	}

	// restart DTMF processing
	hc->dtmf = dtmf;
	if (dtmf)
		HFC_outb_(hc, R_DTMF, hc->hw.r_dtmf | V_RST_DTMF);
}

#ifdef CONFIG_HFCMULTI_PCIMEM
/*******************/
/* write fifo data */
/*******************/
void write_fifo_data(hfc_multi_t *hc,BYTE *dest,int len)
{
	int i, remain;

	remain = len;

#ifdef FIFO_32BIT_ACCESS
	for (i = 0; i < len/4; i++) {
		HFC_outl_(hc, A_FIFO_DATA0, *((DWORD *)dest));
		remain -= 4;
		dest += 4;
	}
#endif

#ifdef FIFO_16BIT_ACCESS
	for (i = 0; i < len/2; i++) {
		HFC_outw_(hc, A_FIFO_DATA0, *((WORD *)dest));
		remain -= 2;
		dest += 2;
	}
#endif

	for (i = 0; i < remain; i++)
		HFC_outb_(hc, A_FIFO_DATA0, *dest++);

}
#endif

#ifndef CONFIG_HFCMULTI_PCIMEM
/*******************/
/* write fifo data */
/*******************/
void 
write_fifo_data(hfc_multi_t *hc, BYTE *dest, int len)
{
	int i, remain;

	remain = len;
	HFC_set(hc, A_FIFO_DATA0);

#ifdef FIFO_32BIT_ACCESS
	for (i = 0; i < len/4; i++) {
		HFC_putl(hc, *((DWORD *)dest));
		remain -= 4;
		dest += 4;
	}
#endif

#ifdef FIFO_16BIT_ACCESS
	for(i = 0; i < len/2; i++) {
		HFC_putw(hc, *((WORD *)dest));
		remain -= 2;
		dest += 2;
	}
#endif

	for (i = 0; i < remain; i++)
		HFC_putb(hc, *dest++);
}
#endif

/*********************************/
/* fill fifo as much as possible */
/*********************************/

static void
hfcmulti_tx(hfc_multi_t *hc, int ch, channel_t *chan)
{
	int i, ii, temp, len;
	int Zspace, z1, z2;
	int Fspace, f1, f2;
	BYTE *d;
	int txpending, slot_tx;

	f1 = HFC_inb_(hc, A_F1);
	f2 = HFC_inb_(hc, A_F2);
	//printk(KERN_DEBUG "txf12:%x %x\n",f1,f2);
	/* get skb, fifo & mode */
	
	txpending = hc->chan[ch].txpending;
	slot_tx = hc->chan[ch].slot_tx;
	len = chan->tx_skb ? chan->tx_skb->len : 0;
	if ((!len) && txpending != 1)
		return; /* no data */

	//printk("debug: data: len = %d, txpending = %d!!!!\n", *len, txpending);
	/* lets see how much data we will have left in buffer */
	if (test_bit(HFC_CHIP_DIGICARD, &hc->chip) && (hc->chan[ch].protocol == ISDN_PID_L1_B_64TRANS) && (hc->chan[ch].slot_rx < 0) && (hc->chan[ch].bank_rx == 0)
			&& (hc->chan[ch].slot_tx < 0) && (hc->chan[ch].bank_tx == 0)) {
		HFC_outb_(hc, R_FIFO, 0x20 | (ch << 1));
	} else
		HFC_outb_(hc, R_FIFO, ch << 1);

	HFC_wait_(hc);
	if (txpending == 2) {
		/* reset fifo */
		HFC_outb_(hc, R_INC_RES_FIFO, V_RES_F);
		HFC_wait_(hc);
		HFC_outb(hc, A_SUBCH_CFG, 0);
		txpending = 1;
	}
next_frame:
	if (test_bit(FLG_HDLC, &chan->Flags)) {
		f1 = HFC_inb_(hc, A_F1);
		f2 = HFC_inb_(hc, A_F2);
		while (f2 != (temp = HFC_inb_(hc, A_F2))) {
			if (debug & DEBUG_HFCMULTI_FIFO)
				printk(KERN_DEBUG "%s: reread f2 because %d!=%d\n",
					__FUNCTION__, temp, f2);
			f2 = temp; /* repeat until F2 is equal */
		}
		Fspace = f2 - f1 - 1;
		if (Fspace < 0)
			Fspace += hc->Flen;
		/* Old FIFO handling doesn't give us the current Z2 read
		 * pointer, so we cannot send the next frame before the fifo
		 * is empty. It makes no difference except for a slightly
		 * lower performance.
		 */
		if (test_bit(HFC_CHIP_REVISION0, &hc->chip)) {
			if (f1 != f2)
				Fspace = 0;
			else
				Fspace = 1;
		}
		/* one frame only for ST D-channels, to allow resending */
		if (hc->type != 1 && test_bit(FLG_DCHANNEL, &chan->Flags)) {
			if (f1 != f2)
				Fspace = 0;
		}
		/* F-counter full condition */
		if (Fspace == 0)
			return;
	}
	z1 = HFC_inw_(hc, A_Z1) - hc->Zmin;
	z2 = HFC_inw_(hc, A_Z2) - hc->Zmin;
	while (z2 != (temp = (HFC_inw_(hc, A_Z2) - hc->Zmin))) {
		if (debug & DEBUG_HFCMULTI_FIFO)
			printk(KERN_DEBUG "%s: reread z2 because %d!=%d\n",
				__FUNCTION__, temp, z2);
		z2 = temp; /* repeat unti Z2 is equal */
	}
	Zspace = z2 - z1 - 1;
	if (Zspace < 0)
		Zspace += hc->Zlen;
	/* buffer too full, there must be at least one more byte for 0-volume */
	if (Zspace < 4) /* just to be safe */
		return;

	/* if no data */
	if (!len) {
		if (z1 == z2) { /* empty */
			/* if done with FIFO audio data during PCM connection */
			if (!test_bit(FLG_HDLC, &chan->Flags) && txpending && slot_tx >= 0) {
				if (debug & DEBUG_HFCMULTI_MODE)
					printk(KERN_DEBUG "%s: reconnecting PCM due to no more FIFO data: channel %d slot_tx %d\n",
						__FUNCTION__, ch, slot_tx);
				/* connect slot */
				HFC_outb(hc, A_CON_HDLC, 0xc0 | 0x00 | V_HDLC_TRP | V_IFF);
				HFC_outb_(hc, R_FIFO, ch<<1 | 1);
				HFC_wait_(hc);
				HFC_outb(hc, A_CON_HDLC, 0xc0 | 0x00 | V_HDLC_TRP | V_IFF);
				HFC_outb_(hc, R_FIFO, ch<<1);
				HFC_wait_(hc);
			}
			txpending = hc->chan[ch].txpending = 0;
		}
		return; /* no data */
	}

	/* if audio data */
	if (!test_bit(FLG_HDLC, &chan->Flags) && !txpending && slot_tx >= 0) {
		if (debug & DEBUG_HFCMULTI_MODE)
			printk(KERN_DEBUG "%s: disconnecting PCM due to FIFO data: channel %d slot_tx %d\n",
				__FUNCTION__, ch, slot_tx);
		/* disconnect slot */
		HFC_outb(hc, A_CON_HDLC, 0x80 | 0x00 | V_HDLC_TRP | V_IFF);
		HFC_outb_(hc, R_FIFO, ch<<1 | 1);
		HFC_wait_(hc);
		HFC_outb(hc, A_CON_HDLC, 0x80 | 0x00 | V_HDLC_TRP | V_IFF);
		HFC_outb_(hc, R_FIFO, ch<<1);
		HFC_wait_(hc);
	}
	txpending = hc->chan[ch].txpending = 1;

	/* show activity */
	hc->activity[hc->chan[ch].port] = 1;

	/* fill fifo to what we have left */
	i = chan->tx_idx;
	ii = len;
	d = chan->tx_skb->data + i;
	if (ii - i > Zspace)
		ii = Zspace + i;
	if (debug & DEBUG_HFCMULTI_FIFO)
		printk(KERN_DEBUG "%s: fifo(%d) has %d bytes space left (z1=%04x, z2=%04x) sending %d of %d bytes %s\n",
			__FUNCTION__, ch, Zspace, z1, z2, ii-i, len-i,
			test_bit(FLG_HDLC, &chan->Flags) ? "HDLC":"TRANS");

	/* Have to prep the audio data */
	write_fifo_data(hc, d, ii - i);
	chan->tx_idx = ii;

	/* if not all data has been written */
	if (ii != len) {
		/* NOTE: fifo is started by the calling function */
		return;
	}

	/* if all data has been written */
	if (test_bit(FLG_HDLC, &chan->Flags)) {
		/* increment f-counter */
		HFC_outb_(hc, R_INC_RES_FIFO, V_INC_F);
		HFC_wait_(hc);
	}
	// check for next frame
	if (chan->tx_skb) {
		dev_kfree_skb(chan->tx_skb);
	} 

	if (test_bit(FLG_TX_NEXT, &chan->Flags)) {
		chan->tx_skb = chan->next_skb;
		if (chan->tx_skb) {
			mISDN_head_t	*hh = mISDN_HEAD_P(chan->tx_skb);
			chan->next_skb = NULL;
			test_and_clear_bit(FLG_TX_NEXT, &chan->Flags);
			chan->tx_idx = 0;
			len = chan->tx_skb->len;
			queue_ch_frame(chan, CONFIRM, hh->dinfo, NULL);
			goto next_frame;
		} else {
			test_and_clear_bit(FLG_TX_NEXT, &chan->Flags);
			printk(KERN_WARNING "%s: tx irq TX_NEXT without skb (dch ch=%d)\n",
				__FUNCTION__, ch);
		}
	} else
		chan->tx_skb = NULL;
	test_and_clear_bit(FLG_TX_BUSY, &chan->Flags);
	chan->tx_idx = 0;
	/* now we have no more data, so in case of transparent,
	 * we set the last byte in fifo to 'silence' in case we will get
	 * no more data at all. this prevents sending an undefined value.
	 */
	if (!test_bit(FLG_HDLC, &chan->Flags))
		HFC_outb_(hc, A_FIFO_DATA0_NOINC, silence);
}


#ifdef CONFIG_HFCMULTI_PCIMEM
/******************/
/* read fifo data */
/******************/
void read_fifo_data(hfc_multi_t *hc, BYTE *dest, int len)
{
	int i, remain;

	remain = len;

#ifdef FIFO_32BIT_ACCESS
	for(i = 0; i < len/4; i++) {
		*((DWORD *)dest) = HFC_inl_(hc, A_FIFO_DATA0);
		remain -= 4;
		dest += 4;
	}
#endif

#ifdef FIFO_16BIT_ACCESS
	for(i = 0; i < len/2; i++) {
		*((WORD *)dest) = HFC_inw_(hc, A_FIFO_DATA0);
		remain -= 2;
		dest += 2;
	}
#endif

	for(i = 0; i < remain; i++)
		*dest++ = HFC_inb_(hc, A_FIFO_DATA0);

}
#endif

#ifndef CONFIG_HFCMULTI_PCIMEM
/******************/
/* read fifo data */
/******************/
void read_fifo_data(hfc_multi_t *hc, BYTE *dest, int len)
{
	int i, remain;

	remain = len;
	HFC_set(hc, A_FIFO_DATA0);

#ifdef FIFO_32BIT_ACCESS
	for(i = 0; i < len/4; i++) {
		*((DWORD *)dest) = HFC_getl(hc);
		remain -= 4;
		dest += 4;
	}
#endif

#ifdef FIFO_16BIT_ACCESS
	for(i = 0; i < len/2; i++) {
		*((WORD *)dest) = HFC_getw(hc);
		remain -= 2;
		dest += 2;
	}
#endif

	for(i = 0;i < remain; i++)
		*dest++ = HFC_getb(hc);
}
#endif


static void
hfcmulti_rx(hfc_multi_t *hc, int ch, channel_t *chan)
{
	int temp;
	int Zsize, z1, z2 = 0; /* = 0, to make GCC happy */
	int f1 = 0, f2 = 0; /* = 0, to make GCC happy */
	struct sk_buff *skb;

	/* lets see how much data we received */
	if (test_bit(HFC_CHIP_DIGICARD, &hc->chip) && (hc->chan[ch].protocol == ISDN_PID_L1_B_64TRANS) && (hc->chan[ch].slot_rx < 0) && (hc->chan[ch].bank_rx == 0)
			&& (hc->chan[ch].slot_tx < 0) && (hc->chan[ch].bank_tx == 0)) {
		HFC_outb_(hc, R_FIFO, 0x20 | (ch<<1) | 1);
	} else 
		HFC_outb_(hc, R_FIFO, (ch<<1)|1);
	HFC_wait_(hc);
next_frame:
	if (test_bit(FLG_HDLC, &chan->Flags)) {
		f1 = HFC_inb_(hc, A_F1);
		while (f1 != (temp = HFC_inb_(hc, A_F1))) {
			if (debug & DEBUG_HFCMULTI_FIFO)
				printk(KERN_DEBUG "%s: reread f1 because %d!=%d\n", __FUNCTION__, temp, f1);
			f1 = temp; /* repeat until F1 is equal */
		}
		f2 = HFC_inb_(hc, A_F2);
	}
	//if(f1!=f2) printk(KERN_DEBUG "got a chan:%d framef12:%x %x!!!!\n",ch,f1,f2);
	//printk(KERN_DEBUG "got a chan:%d framef12:%x %x!!!!\n",ch,f1,f2);
	z1 = HFC_inw_(hc, A_Z1) - hc->Zmin;
	while(z1 != (temp = (HFC_inw_(hc, A_Z1) - hc->Zmin))) {
		if (debug & DEBUG_HFCMULTI_FIFO)
			printk(KERN_DEBUG "%s: reread z2 because %d!=%d\n", __FUNCTION__, temp, z2);
		z1 = temp; /* repeat until Z1 is equal */
	}
	z2 = HFC_inw_(hc, A_Z2) - hc->Zmin;
	Zsize = z1 - z2;
	if (test_bit(FLG_HDLC, &chan->Flags) && f1 != f2) /* complete hdlc frame */
		Zsize++;
	if (Zsize < 0)
		Zsize += hc->Zlen;
	/* if buffer is empty */
	if (Zsize <= 0)
		return;

	if (!chan->rx_skb) {
		chan->rx_skb = alloc_stack_skb(chan->maxlen + 3, chan->up_headerlen);
		if (!chan->rx_skb) {
			printk(KERN_DEBUG "%s: No mem for rx_skb\n", __FUNCTION__);
			return;
		}
	}
	/* show activity */
	hc->activity[hc->chan[ch].port] = 1;

	/* empty fifo with what we have */
	if (test_bit(FLG_HDLC, &chan->Flags)) {
		if (debug & DEBUG_HFCMULTI_FIFO)
			printk(KERN_DEBUG "%s: fifo(%d) reading %d bytes (z1=%04x, z2=%04x) HDLC %s (f1=%d, f2=%d) got=%d\n",
				__FUNCTION__, ch, Zsize, z1, z2,
				(f1 == f2) ? "fragment" : "COMPLETE",
				f1, f2, Zsize + chan->rx_skb->len);
		/* HDLC */
		if ((Zsize + chan->rx_skb->len) > (chan->maxlen + 3)) {
			if (debug & DEBUG_HFCMULTI_FIFO)
				printk(KERN_DEBUG "%s: hdlc-frame too large.\n", __FUNCTION__);
			skb_trim(chan->rx_skb, 0);
			HFC_outb_(hc, R_INC_RES_FIFO, V_RES_F);
			HFC_wait_(hc);
			return;
		}

		read_fifo_data(hc, skb_put(chan->rx_skb, Zsize), Zsize);

		if (f1 != f2) {
			/* increment Z2,F2-counter */
			HFC_outb_(hc, R_INC_RES_FIFO, V_INC_F);
			HFC_wait_(hc);
			/* check size */
			if (chan->rx_skb->len < 4) {
				if (debug & DEBUG_HFCMULTI_FIFO)
					printk(KERN_DEBUG "%s: Frame below minimum size\n", __FUNCTION__);
				skb_trim(chan->rx_skb, 0);
				goto next_frame;
			}
			/* there is at least one complete frame, check crc */
			if (chan->rx_skb->data[chan->rx_skb->len - 1]) {
				if (debug & DEBUG_HFCMULTI_CRC)
					printk(KERN_DEBUG "%s: CRC-error\n", __FUNCTION__);
				skb_trim(chan->rx_skb, 0);
				goto next_frame;
			}
			/* only send dchannel if in active state */
			if (test_bit(FLG_DCHANNEL, &chan->Flags) &&
				hc->type == 1 && hc->chan[ch].e1_state != 1) {
				skb_trim(chan->rx_skb, 0);
				goto next_frame;
			}
			skb_trim(chan->rx_skb, chan->rx_skb->len - 3);
			if (chan->rx_skb->len < MISDN_COPY_SIZE) {
				skb = alloc_stack_skb(chan->rx_skb->len, chan->up_headerlen);
				if (skb) {
					memcpy(skb_put(skb, chan->rx_skb->len),
						chan->rx_skb->data, chan->rx_skb->len);
					skb_trim(chan->rx_skb, 0);
				} else {
					skb = chan->rx_skb;
					chan->rx_skb = NULL;
				}
			} else {
				skb = chan->rx_skb;
				chan->rx_skb = NULL;
			}
			if (debug & DEBUG_HFCMULTI_FIFO) {
				temp = 0;
				while(temp < skb->len)
					printk("%02x ", skb->data[temp++]);
				printk("\n");
			}
			queue_ch_frame(chan, INDICATION, MISDN_ID_ANY, skb);
			goto next_frame;
		}
		/* there is an incomplete frame */
	} else {
		/* transparent */
		if (Zsize > skb_tailroom(chan->rx_skb))
			Zsize = skb_tailroom(chan->rx_skb);
		if (Zsize < MISDN_COPY_SIZE) {
			skb = alloc_stack_skb(Zsize, chan->up_headerlen);
			if (!skb) {
				skb = chan->rx_skb;
				chan->rx_skb = NULL;
			}
		} else {
			skb = chan->rx_skb;
			chan->rx_skb = NULL;
		}
		if (debug & DEBUG_HFCMULTI_FIFO)
			printk(KERN_DEBUG "%s: fifo(%d) reading %d bytes (z1=%04x, z2=%04x) TRANS\n",
				__FUNCTION__, ch, Zsize, z1, z2);
		read_fifo_data(hc, skb_put(skb, Zsize), Zsize);
		queue_ch_frame(chan, INDICATION, MISDN_ID_ANY, skb);
	}
}


/*********************/
/* Interrupt handler */
/*********************/

static void signal_state_up(channel_t *dch, int dinfo, char *msg)
{
	struct sk_buff *skb;

	if (debug & DEBUG_HFCMULTI_STATE)
		 printk(KERN_DEBUG "%s: %s\n", __FUNCTION__,msg);

	skb = create_link_skb(PH_CONTROL | INDICATION, dinfo, 0, NULL, 0);
	if(!skb) return;

	if (mISDN_queue_up(&dch->inst, 0, skb))
		dev_kfree_skb(skb);
}

static inline void
handle_timer_irq(hfc_multi_t *hc)
{
	int		ch, temp;
	channel_t	*chan;

	ch = 0;
	while(ch < 32) {
		chan = hc->chan[ch].ch;
		if (chan && hc->created[hc->chan[ch].port]) {
			hfcmulti_tx(hc, ch, chan);
			/* fifo is started when switching to rx-fifo */
			hfcmulti_rx(hc, ch, chan);
			if (test_bit(FLG_DCHANNEL, &chan->Flags) &&
				hc->chan[ch].nt_timer > -1) {
				if (!(--hc->chan[ch].nt_timer)) {
					ph_state_change(chan);
					if (debug & DEBUG_HFCMULTI_STATE)
						printk(KERN_DEBUG "%s: nt_timer at state %x\n",
							__FUNCTION__, chan->state);
				}
			}
		}
		ch++;
	}
	if (hc->type == 1 && hc->created[0]) {
		chan = hc->chan[16].ch;
		if (test_bit(HFC_CFG_REPORT_LOS, &hc->chan[16].cfg)) {
			if (debug & DEBUG_HFCMULTI_SYNC)
				printk(KERN_DEBUG "%s: (id=%d) E1 got LOS\n",
					__FUNCTION__, hc->id);
					/* LOS */
			temp = HFC_inb_(hc, R_RX_STA0) & V_SIG_LOS;
			if (!temp && hc->chan[16].los)
				signal_state_up(chan, HW_LOS, "LOS detected");
			if (temp && !hc->chan[16].los)
				signal_state_up(chan, HW_LOS_OFF, "LOS gone");
			hc->chan[16].los = temp;
		}
		if (test_bit(HFC_CFG_REPORT_AIS, &hc->chan[16].cfg)) {
			if (debug & DEBUG_HFCMULTI_SYNC)
				printk(KERN_DEBUG "%s: (id=%d) E1 got AIS\n",
					__FUNCTION__, hc->id);
			/* AIS */
			temp = HFC_inb_(hc, R_RX_STA0) & V_AIS;
			if (!temp && hc->chan[16].ais)
				signal_state_up(chan, HW_AIS, "AIS detected");
			if (temp && !hc->chan[16].ais)
				signal_state_up(chan, HW_AIS_OFF, "AIS gone");
			hc->chan[16].ais = temp;
		}
		if (test_bit(HFC_CFG_REPORT_SLIP, &hc->chan[16].cfg)) {
			if (debug & DEBUG_HFCMULTI_SYNC)
				printk(KERN_DEBUG "%s: (id=%d) E1 got SLIP (RX)\n",
					__FUNCTION__, hc->id);
			/* SLIP */
			temp = HFC_inb_(hc, R_SLIP) & V_FOSLIP_RX;
			if (!temp && hc->chan[16].slip_rx)
				signal_state_up(chan, HW_SLIP_RX, " bit SLIP detected RX");
			hc->chan[16].slip_rx = temp;
			temp = HFC_inb_(hc, R_SLIP) & V_FOSLIP_TX;
			if (!temp && hc->chan[16].slip_tx)
				signal_state_up(chan, HW_SLIP_TX, " bit SLIP detected TX");
			hc->chan[16].slip_tx = temp;
		}
		temp = HFC_inb_(hc, R_JATT_DIR);
		switch(hc->chan[16].sync) {
			case 0:
				if ((temp & 0x60) == 0x60) {
					if (debug & DEBUG_HFCMULTI_SYNC)
						printk(KERN_DEBUG "%s: (id=%d) E1 now in clock sync\n",
							__FUNCTION__, hc->id);
					HFC_outb(hc, R_RX_OFF, hc->chan[16].jitter | V_RX_INIT);
					HFC_outb(hc, R_TX_OFF, hc->chan[16].jitter | V_RX_INIT);
					hc->chan[16].sync = 1;
					goto check_framesync;
				}
				break;
			case 1:
				if ((temp & 0x60) != 0x60) {
					if (debug & DEBUG_HFCMULTI_SYNC)
						printk(KERN_DEBUG "%s: (id=%d) E1 lost clock sync\n",
							__FUNCTION__, hc->id);
					hc->chan[16].sync = 0;
					break;
				}
				check_framesync:
				temp = HFC_inb_(hc, R_RX_STA0);
				if (temp == 0x27) {
					if (debug & DEBUG_HFCMULTI_SYNC)
						printk(KERN_DEBUG "%s: (id=%d) E1 now in frame sync\n",
							__FUNCTION__, hc->id);
					hc->chan[16].sync = 2;
				}
				break;
			case 2:
				if ((temp & 0x60) != 0x60) {
					if (debug & DEBUG_HFCMULTI_SYNC)
						printk(KERN_DEBUG "%s: (id=%d) E1 lost clock & frame sync\n",
							__FUNCTION__, hc->id);
					hc->chan[16].sync = 0;
					break;
				}
				temp = HFC_inb_(hc, R_RX_STA0);
				if (temp != 0x27) {
					if (debug & DEBUG_HFCMULTI_SYNC)
						printk(KERN_DEBUG "%s: (id=%d) E1 lost frame sync\n",
							__FUNCTION__, hc->id);
					hc->chan[16].sync = 1;
				}
				break;
		}
	}

	if (test_bit(HFC_CHIP_WATCHDOG, &hc->chip)) 
		hfcmulti_watchdog(hc);

	if (hc->leds)
		hfcmulti_leds(hc);
}

static irqreturn_t
hfcmulti_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
#ifdef IRQCOUNT_DEBUG
	static int iq1=0,iq2=0,iq3=0,iq4=0,iq5=0,iq6=0,iqcnt=0;
#endif
	hfc_multi_t	*hc = dev_id;
	channel_t	*chan;
	u_char 		r_irq_statech, status, r_irq_misc, r_irq_oview, r_irq_fifo_bl;
	//u_char bl1,bl2;
#ifdef CONFIG_PLX_PCI_BRIDGE
	u_short *plx_acc,wval;
#endif
	int ch,i,j;

#ifdef CONFIG_PLX_PCI_BRIDGE
	plx_acc=(u_short*)(hc->plx_membase+0x4c);
	wval=*plx_acc;
	if(!(wval&0x04))
	{
		if(wval&0x20)
		{
			//printk(KERN_WARNING "NO irq  LINTI1:%x\n",wval);
			printk(KERN_WARNING "got irq  LINTI2\n");
		}
		return(IRQ_NONE);
	}
#endif

	spin_lock(&hc->lock);

	if (!hc) {
		printk(KERN_WARNING "HFC-multi: Spurious interrupt!\n");
		irq_notforus:

#ifdef CONFIG_PLX_PCI_BRIDGE
	//plx_acc=(u_short*)(hc->plx_membase+0x4c);
	//*plx_acc=0xc00;  // clear LINTI1 & LINTI2
	//*plx_acc=0xc41;
#endif
		spin_unlock(&hc->lock);
		return(IRQ_NONE);
	}
	status = HFC_inb_(hc, R_STATUS);
	r_irq_statech = HFC_inb_(hc, R_IRQ_STATECH);
#ifdef IRQCOUNT_DEBUG
	if (r_irq_statech)
		iq1++;
	if (status&V_DTMF_STA)
		iq2++;
	if (status&V_LOST_STA)
		iq3++;
	if (status&V_EXT_IRQSTA)
		iq4++;
	if (status&V_MISC_IRQSTA)
		iq5++;
	if (status&V_FR_IRQSTA)
		iq6++;
	if (iqcnt++ > 5000) {
		printk(KERN_ERR "iq1:%x iq2:%x iq3:%x iq4:%x iq5:%x iq6:%x\n",iq1,iq2,iq3,iq4,iq5,iq6);
		iqcnt=0;
	}
#endif
	if (!r_irq_statech && !(status & (V_DTMF_STA | V_LOST_STA | V_EXT_IRQSTA | V_MISC_IRQSTA | V_FR_IRQSTA))) {
		/* irq is not for us */
		//if(status) printk(KERN_WARNING "nofus:%x\n",status);
		goto irq_notforus;
	}
	hc->irqcnt++;
	if (r_irq_statech) {
		if (hc->type != 1) {
			/* state machine */
			ch = 0;
			while(ch < 32) {
				chan = hc->chan[ch].ch;
				if (chan && test_bit(FLG_DCHANNEL, &chan->Flags)) {
					if (r_irq_statech & 1) {
						HFC_outb_(hc, R_ST_SEL, hc->chan[ch].port);
						chan->state = HFC_inb(hc, A_ST_RD_STATE) & 0x0f;
						if (chan->state == (test_bit(HFC_CFG_NTMODE,
							&hc->chan[chan->channel].cfg) ? 3: 7)) {
							HFC_outb_(hc, R_FIFO, (ch<<1) | 1);
							HFC_wait_(hc);
							HFC_outb_(hc, R_INC_RES_FIFO, V_RES_F);
							HFC_wait_(hc);
							chan->tx_idx=0;
						}
						ph_state_change(chan);
						if (debug & DEBUG_HFCMULTI_STATE)
							printk(KERN_DEBUG "%s: S/T newstate %x port %d\n",
								__FUNCTION__, chan->state, hc->chan[ch].port);
					}
					r_irq_statech >>= 1;
				}
				ch++;
			}
		}
	}
	if (status & V_EXT_IRQSTA) {
		/* external IRQ */
	}
	if (status & V_LOST_STA) {
		/* LOST IRQ */
		HFC_outb(hc, R_INC_RES_FIFO, V_RES_LOST); /* clear irq! */
	}
	if (status & V_MISC_IRQSTA) {
		/* misc IRQ */
		r_irq_misc = HFC_inb_(hc, R_IRQ_MISC);
		if (r_irq_misc & V_STA_IRQ) {
			if (hc->type == 1) {
				/* state machine */
				chan = hc->chan[16].ch;
				chan->state = HFC_inb_(hc, R_E1_RD_STA) & 0x7;
				ph_state_change(chan);
				if (debug & DEBUG_HFCMULTI_STATE)
					printk(KERN_DEBUG "%s: E1 newstate %x\n",
						__FUNCTION__, chan->state);
			}
		}
		if (r_irq_misc & V_TI_IRQ)
			handle_timer_irq(hc);

		if (r_irq_misc & V_DTMF_IRQ) {
			/* -> DTMF IRQ */
			hfcmulti_dtmf(hc);
		}
	}
	if (status & V_FR_IRQSTA) {
		/* FIFO IRQ */
		r_irq_oview = HFC_inb_(hc, R_IRQ_OVIEW);
		//if(r_irq_oview) printk(KERN_DEBUG "OV:%x\n",r_irq_oview);
		i = 0;
		while(i < 8) {
			if (r_irq_oview & (1 << i)) {
				r_irq_fifo_bl = HFC_inb_(hc, R_IRQ_FIFO_BL0 + i);
				//r_irq_fifo_bl = HFC_inb_(hc, R_INT_DATA);
				//if(r_irq_fifo_bl) printk(KERN_DEBUG "BL%d:%x\n",i,r_irq_fifo_bl);
				//bl1 = HFC_inb_(hc, R_IRQ_FIFO_BL0);
				//bl2 = HFC_inb_(hc, R_IRQ_FIFO_BL0);
				//printk(KERN_DEBUG "zero:%x :%x\n",bl1,bl2);
				r_irq_fifo_bl = HFC_inb_(hc, R_IRQ_FIFO_BL0 + i);
				j = 0;
				while(j < 8) {
					ch = (i<<2) + (j>>1);
					if (ch >= 16) {
						if (ch == 16)
							printk("Shouldn't be servicing high FIFOs.  Continuing.\n");
						continue;
					}
					chan = hc->chan[ch].ch;
					if (r_irq_fifo_bl & (1 << j)) {
						if (chan && hc->created[hc->chan[ch].port] &&
							test_bit(FLG_ACTIVE, &chan->Flags)) {
							//printk(KERN_DEBUG "txchan:%d\n",ch);
							hfcmulti_tx(hc, ch, chan);
							/* start fifo */
							HFC_outb_(hc, R_FIFO, 0);
							HFC_wait_(hc);
						}
					}
					j++;
					if (r_irq_fifo_bl & (1 << j)) {
						if (chan && hc->created[hc->chan[ch].port] &&
							test_bit(FLG_ACTIVE, &chan->Flags)) {
							hfcmulti_rx(hc, ch, chan);
							//printk(KERN_DEBUG "rxchan:%d\n",ch);
						}
					}
					j++;
				}
			}
			i++;
		}
	}

#ifdef CONFIG_PLX_PCI_BRIDGE
	//plx_acc=(u_short*)(hc->plx_membase+0x4c);
	//*plx_acc=0xc00;  // clear LINTI1 & LINTI2
	//*plx_acc=0xc41;
#endif
	spin_unlock(&hc->lock);
	return(IRQ_HANDLED);
}


/********************************************************************/
/* timer callback for D-chan busy resolution. Currently no function */
/********************************************************************/

static void
hfcmulti_dbusy_timer(hfc_multi_t *hc)
{
}


/***************************************************************/
/* activate/deactivate hardware for selected channels and mode */
/***************************************************************/

/* configure B-channel with the given protocol
 * ch eqals to the HFC-channel (0-31)
 * ch is the number of channel (0-4,4-7,8-11,12-15,16-19,20-23,24-27,28-31 for S/T, 1-31 for E1)
 * the hdlc interrupts will be set/unset
 *
 */
static int
mode_hfcmulti(hfc_multi_t *hc, int ch, int protocol, int slot_tx, int bank_tx, int slot_rx, int bank_rx)
{
	int flow_tx = 0, flow_rx = 0, routing = 0;
	int oslot_tx = hc->chan[ch].slot_tx;
	int oslot_rx = hc->chan[ch].slot_rx;
	int conf = hc->chan[ch].conf;

	if (debug & DEBUG_HFCMULTI_MODE)
		printk(KERN_DEBUG "%s: channel %d protocol %x slot %d bank %d (TX) slot %d bank %d (RX)\n",
			__FUNCTION__, ch, protocol, slot_tx, bank_tx, slot_rx, bank_rx);

	if (oslot_tx>=0 && slot_tx!=oslot_tx) {
		/* remove from slot */
		if (debug & DEBUG_HFCMULTI_MODE)
			printk(KERN_DEBUG "%s: remove from slot %d (TX)\n", __FUNCTION__, oslot_tx);
		if (hc->slot_owner[oslot_tx<<1] == ch) {
			HFC_outb(hc, R_SLOT, oslot_tx<<1);
			HFC_outb(hc, A_SL_CFG, 0);
			HFC_outb(hc, A_CONF, 0);
			hc->slot_owner[oslot_tx<<1] = -1;
		} else {
			if (debug & DEBUG_HFCMULTI_MODE)
				printk(KERN_DEBUG "%s: we are not owner of this slot anymore, channel %d is.\n", __FUNCTION__, hc->slot_owner[oslot_tx<<1]);
		}
	}

	if (oslot_rx>=0 && slot_rx!=oslot_rx) {
		/* remove from slot */
		if (debug & DEBUG_HFCMULTI_MODE)
			printk(KERN_DEBUG "%s: remove from slot %d (RX)\n", __FUNCTION__, oslot_rx);
		if (hc->slot_owner[(oslot_rx<<1)|1] == ch) {
			HFC_outb(hc, R_SLOT, (oslot_rx<<1) | V_SL_DIR);
			HFC_outb(hc, A_SL_CFG, 0);
			hc->slot_owner[(oslot_rx<<1)|1] = -1;
		} else {
			if (debug & DEBUG_HFCMULTI_MODE)
				printk(KERN_DEBUG "%s: we are not owner of this slot anymore, channel %d is.\n", __FUNCTION__, hc->slot_owner[(oslot_rx<<1)|1]);
		}
	}

	if (slot_tx < 0) {
		flow_tx = 0x80; /* FIFO->ST */
		/* disable pcm slot */
		hc->chan[ch].slot_tx = -1;
		hc->chan[ch].bank_tx = 0;
	} else {
		/* set pcm slot */
		if (hc->chan[ch].txpending)
			flow_tx = 0x80; /* FIFO->ST */
		else
			flow_tx = 0xc0; /* PCM->ST */
		/* put on slot */
		routing = bank_tx?0xc0:0x80;
		if (conf>=0 || bank_tx>1)
			routing = 0x40; /* loop */
		if (debug & DEBUG_HFCMULTI_MODE)
			printk(KERN_DEBUG "%s: put to slot %d bank %d flow %02x routing %02x conf %d (TX)\n", __FUNCTION__, slot_tx, bank_tx, flow_tx, routing, conf);
		HFC_outb(hc, R_SLOT, slot_tx<<1);
		HFC_outb(hc, A_SL_CFG, (ch<<1) | routing);
		HFC_outb(hc, A_CONF, (conf<0)?0:(conf|V_CONF_SL));
		hc->slot_owner[slot_tx<<1] = ch;
		hc->chan[ch].slot_tx = slot_tx;
		hc->chan[ch].bank_tx = bank_tx;
	}
	if (slot_rx < 0) {
		/* disable pcm slot */
		flow_rx = 0x80; /* ST->FIFO */
		hc->chan[ch].slot_rx = -1;
		hc->chan[ch].bank_rx = 0;
	} else {
		/* set pcm slot */
		if (hc->chan[ch].txpending)
			flow_rx = 0x80; /* ST->FIFO */
		else
			flow_rx = 0xc0; /* ST->(FIFO,PCM) */
		/* put on slot */
		routing = bank_rx?0x80:0xc0; /* reversed */
		if (conf >= 0 || bank_rx > 1)
			routing = 0x40; /* loop */
		if (debug & DEBUG_HFCMULTI_MODE)
			printk(KERN_DEBUG "%s: put to slot %d bank %d flow %02x routing %02x conf %d (RX)\n",
				__FUNCTION__, slot_rx, bank_rx, flow_rx, routing, conf);
		HFC_outb(hc, R_SLOT, (slot_rx<<1) | V_SL_DIR);
		HFC_outb(hc, A_SL_CFG, (ch<<1) | V_CH_DIR | routing);
		hc->slot_owner[(slot_rx<<1)|1] = ch;
		hc->chan[ch].slot_rx = slot_rx;
		hc->chan[ch].bank_rx = bank_rx;
	}

	switch (protocol) {
		case (ISDN_PID_NONE):
			/* disable TX fifo */
			HFC_outb(hc, R_FIFO, ch << 1);
			HFC_wait(hc);
			HFC_outb(hc, A_CON_HDLC, flow_tx | 0x00 | V_IFF);
			HFC_outb(hc, A_SUBCH_CFG, 0);
			HFC_outb(hc, A_IRQ_MSK, 0);
			HFC_outb(hc, R_INC_RES_FIFO, V_RES_F);
			HFC_wait(hc);
			/* disable RX fifo */
			HFC_outb(hc, R_FIFO, (ch<<1)|1);
			HFC_wait(hc);
			HFC_outb(hc, A_CON_HDLC, flow_rx | 0x00);
			HFC_outb(hc, A_SUBCH_CFG, 0);
			HFC_outb(hc, A_IRQ_MSK, 0);
			HFC_outb(hc, R_INC_RES_FIFO, V_RES_F);
			HFC_wait(hc);
			if (hc->type == 1) {
			} else if ((ch & 0x3) < 2) {
				hc->hw.a_st_ctrl0[hc->chan[ch].port] &= ((ch & 0x3) == 0)? ~V_B1_EN: ~V_B2_EN;
				HFC_outb(hc, R_ST_SEL, hc->chan[ch].port);
				HFC_outb(hc, A_ST_CTRL0,  hc->hw.a_st_ctrl0[hc->chan[ch].port]);
			}
			if (test_bit(FLG_BCHANNEL, &hc->chan[ch].ch->Flags)) {
				test_and_clear_bit(FLG_HDLC, &hc->chan[ch].ch->Flags);
				test_and_clear_bit(FLG_TRANSPARENT, &hc->chan[ch].ch->Flags);
			}
			break;
		case (ISDN_PID_L1_B_64TRANS): /* B-channel */
			if (test_bit(HFC_CHIP_DIGICARD, &hc->chip) && (hc->chan[ch].slot_rx < 0) && (hc->chan[ch].bank_rx == 0)
					&& (hc->chan[ch].slot_tx < 0) && (hc->chan[ch].bank_tx == 0)) {

				printk("Setting B-channel %d to echo cancelable state on PCM slot %d\n", ch,
						((ch/4)*8) + ((ch%4)*4) + 1);
				printk("Enabling pass through for channel\n");
				vpm_out(hc, ch, ((ch/4)*8) + ((ch%4)*4) + 1, 0x01);
				/* rx path */
				/* S/T -> PCM */
				HFC_outb(hc, R_FIFO, (ch << 1));
				HFC_wait(hc);
				HFC_outb(hc, A_CON_HDLC, 0xc0 | V_HDLC_TRP | V_IFF);
				HFC_outb(hc, R_SLOT, (((ch/4)*8) + ((ch%4)*4) + 1) << 1);
				HFC_outb(hc, A_SL_CFG, 0x80 | (ch << 1));

				/* PCM -> FIFO */
				HFC_outb(hc, R_FIFO, 0x20 | (ch << 1) | 1);
				HFC_wait(hc);
				HFC_outb(hc, A_CON_HDLC, 0x20 | V_HDLC_TRP | V_IFF);
				HFC_outb(hc, A_SUBCH_CFG, 0);
				HFC_outb(hc, A_IRQ_MSK, 0);
				HFC_outb(hc, R_INC_RES_FIFO, V_RES_F);
				HFC_wait(hc);
				HFC_outb(hc, R_SLOT, ((((ch/4)*8) + ((ch%4)*4) + 1) << 1) | 1);
				HFC_outb(hc, A_SL_CFG, 0x80 | 0x20 | (ch << 1) | 1);

				/* tx path */
				/* PCM -> S/T */
				HFC_outb(hc, R_FIFO, (ch << 1) | 1);
				HFC_wait(hc);
				HFC_outb(hc, A_CON_HDLC, 0xc0 | V_HDLC_TRP | V_IFF);
				HFC_outb(hc, R_SLOT, ((((ch/4)*8) + ((ch%4)*4)) << 1) | 1);
				HFC_outb(hc, A_SL_CFG, 0x80 | 0x40 | (ch << 1) | 1);

				/* FIFO -> PCM */
				HFC_outb(hc, R_FIFO, 0x20 | (ch << 1));
				HFC_wait(hc);
				HFC_outb(hc, A_CON_HDLC, 0x20 | V_HDLC_TRP | V_IFF);
				HFC_outb(hc, A_SUBCH_CFG, 0);
				HFC_outb(hc, A_IRQ_MSK, 0);
				HFC_outb(hc, R_INC_RES_FIFO, V_RES_F);
				HFC_wait(hc);
				HFC_outb_(hc, A_FIFO_DATA0_NOINC, silence); /* tx silence */
				HFC_outb(hc, R_SLOT, (((ch/4)*8) + ((ch%4)*4)) << 1);
				HFC_outb(hc, A_SL_CFG, 0x80 | 0x20 | (ch << 1));
			} else {
				/* enable TX fifo */
				HFC_outb(hc, R_FIFO, ch<<1);
				HFC_wait(hc);
				HFC_outb(hc, A_CON_HDLC, flow_tx | 0x00 | V_HDLC_TRP | V_IFF);
				HFC_outb(hc, A_SUBCH_CFG, 0);
				HFC_outb(hc, A_IRQ_MSK, 0);
				HFC_outb(hc, R_INC_RES_FIFO, V_RES_F);
				HFC_wait(hc);
				HFC_outb_(hc, A_FIFO_DATA0_NOINC, silence); /* tx silence */
				/* enable RX fifo */
				HFC_outb(hc, R_FIFO, (ch<<1)|1);
				HFC_wait(hc);
				HFC_outb(hc, A_CON_HDLC, flow_rx | 0x00 | V_HDLC_TRP);
				HFC_outb(hc, A_SUBCH_CFG, 0);
				HFC_outb(hc, A_IRQ_MSK, 0);
				HFC_outb(hc, R_INC_RES_FIFO, V_RES_F);
				HFC_wait(hc);
			}
			if (hc->type != 1) {
				hc->hw.a_st_ctrl0[hc->chan[ch].port] |= ((ch&0x3)==0)?V_B1_EN:V_B2_EN;
				HFC_outb(hc, R_ST_SEL, hc->chan[ch].port);
				HFC_outb(hc, A_ST_CTRL0,  hc->hw.a_st_ctrl0[hc->chan[ch].port]);
			}
			if (test_bit(FLG_BCHANNEL, &hc->chan[ch].ch->Flags))
				test_and_set_bit(FLG_TRANSPARENT, &hc->chan[ch].ch->Flags);
			break;
		case (ISDN_PID_L1_B_64HDLC): /* B-channel */
		case (ISDN_PID_L1_TE_E1): /* D-channel E1 */
		case (ISDN_PID_L1_NT_E1):
			/* enable TX fifo */
			HFC_outb(hc, R_FIFO, ch<<1);
			HFC_wait(hc);
			HFC_outb(hc, A_CON_HDLC, flow_tx | 0x04);
			HFC_outb(hc, A_SUBCH_CFG, 0);
			HFC_outb(hc, A_IRQ_MSK, V_IRQ);
			HFC_outb(hc, R_INC_RES_FIFO, V_RES_F);
			HFC_wait(hc);
			/* enable RX fifo */
			HFC_outb(hc, R_FIFO, (ch<<1)|1);
			HFC_wait(hc);
			HFC_outb(hc, A_CON_HDLC, flow_rx | 0x04);
			HFC_outb(hc, A_SUBCH_CFG, 0);
			HFC_outb(hc, A_IRQ_MSK, V_IRQ);
			HFC_outb(hc, R_INC_RES_FIFO, V_RES_F);
			HFC_wait(hc);
			if (hc->type == 1) {
			} else {
				hc->hw.a_st_ctrl0[hc->chan[ch].port] |= ((ch&0x3)==0)?V_B1_EN:V_B2_EN;
				HFC_outb(hc, R_ST_SEL, hc->chan[ch].port);
				HFC_outb(hc, A_ST_CTRL0,  hc->hw.a_st_ctrl0[hc->chan[ch].port]);
			}
			if (test_bit(FLG_BCHANNEL, &hc->chan[ch].ch->Flags))
				test_and_set_bit(FLG_HDLC, &hc->chan[ch].ch->Flags);
			break;

		case (ISDN_PID_L1_TE_S0): /* D-channel S0 */
		case (ISDN_PID_L1_NT_S0):
			/* enable TX fifo */
			HFC_outb(hc, R_FIFO, ch<<1);
			HFC_wait(hc);
			HFC_outb(hc, A_CON_HDLC, flow_tx | 0x04 | V_IFF);
			HFC_outb(hc, A_SUBCH_CFG, 2);
			HFC_outb(hc, A_IRQ_MSK, V_IRQ);
			HFC_outb(hc, R_INC_RES_FIFO, V_RES_F);
			HFC_wait(hc);
			/* enable RX fifo */
			HFC_outb(hc, R_FIFO, (ch<<1)|1);
			HFC_wait(hc);
			HFC_outb(hc, A_CON_HDLC, flow_rx | 0x04);
			HFC_outb(hc, A_SUBCH_CFG, 2);
			HFC_outb(hc, A_IRQ_MSK, V_IRQ);
			HFC_outb(hc, R_INC_RES_FIFO, V_RES_F);
			HFC_wait(hc);
			break;

		default:
			printk(KERN_DEBUG "%s: protocol not known %x\n", __FUNCTION__, protocol);
			hc->chan[ch].protocol = ISDN_PID_NONE;
			return(-ENOPROTOOPT);
	}
	hc->chan[ch].protocol = protocol;
	return(0);
}


/**************************/
/* connect/disconnect PCM */
/**************************/

static void
hfcmulti_pcm(hfc_multi_t *hc, int ch, int slot_tx, int bank_tx, int slot_rx, int bank_rx)
{
	if (slot_rx<0 || slot_rx<0 || bank_tx<0 || bank_rx<0) {
		/* disable PCM */
		mode_hfcmulti(hc, ch, hc->chan[ch].protocol, -1, 0, -1, 0);
		return;
	}

	/* enable pcm */
	mode_hfcmulti(hc, ch, hc->chan[ch].protocol, slot_tx, bank_tx,
		slot_rx, bank_rx);
}


/**************************/
/* set/disable conference */
/**************************/

static void
hfcmulti_conf(hfc_multi_t *hc, int ch, int num)
{
	if (num>=0 && num<=7)
		hc->chan[ch].conf = num;
	else
		hc->chan[ch].conf = -1;
	mode_hfcmulti(hc, ch, hc->chan[ch].protocol, hc->chan[ch].slot_tx,
		hc->chan[ch].bank_tx, hc->chan[ch].slot_rx, hc->chan[ch].bank_rx);
}


/***************************/
/* set/disable sample loop */
/***************************/
// NOTE: this function is experimental and therefore disabled
static void
hfcmulti_splloop(hfc_multi_t *hc, int ch, u_char *data, int len)
{
	channel_t *bch = hc->chan[ch].ch;

	/* flush pending TX data */
	if (bch->next_skb) {
		test_and_clear_bit(FLG_TX_NEXT, &bch->Flags);
		dev_kfree_skb(bch->next_skb);
		bch->next_skb = NULL;
	}
	bch->tx_idx = 0;

	/* prevent overflow */
	if (len > hc->Zlen-1)
		len = hc->Zlen-1;

	/* select fifo */
	HFC_outb_(hc, R_FIFO, ch<<1);
	HFC_wait_(hc);

	/* reset fifo */
	HFC_outb(hc, A_SUBCH_CFG, 0);
	udelay(500);
	HFC_outb_(hc, R_INC_RES_FIFO, V_RES_F);
	HFC_wait_(hc);
	udelay(500);

	/* if off */
	if (len <= 0) {
		HFC_outb_(hc, A_FIFO_DATA0_NOINC, silence);
		if (hc->chan[ch].slot_tx>=0) {
			if (debug & DEBUG_HFCMULTI_MODE)
				printk(KERN_DEBUG "%s: connecting PCM due to no more TONE: channel %d slot_tx %d\n", __FUNCTION__, ch, hc->chan[ch].slot_tx);
			/* connect slot */
			HFC_outb(hc, A_CON_HDLC, 0xc0 | 0x00 | V_HDLC_TRP | V_IFF);
			HFC_outb(hc, R_FIFO, ch<<1 | 1);
			HFC_wait(hc);
			HFC_outb(hc, A_CON_HDLC, 0xc0 | 0x00 | V_HDLC_TRP | V_IFF);
		}
		hc->chan[ch].txpending = 0;
		return;
	}

	/* loop fifo */

	/* set mode */
	hc->chan[ch].txpending = 2;

//printk("len=%d %02x %02x %02x\n", len, data[0], data[1], data[2]);
	/* write loop data */
	write_fifo_data(hc,data,len);

	udelay(500);
	HFC_outb(hc, A_SUBCH_CFG, V_LOOP_FIFO);
	udelay(500);

	/* disconnect slot */
	if (hc->chan[ch].slot_tx>=0) {
		if (debug & DEBUG_HFCMULTI_MODE)
			printk(KERN_DEBUG "%s: disconnecting PCM due to TONE: channel %d slot_tx %d\n", __FUNCTION__, ch, hc->chan[ch].slot_tx);
		HFC_outb(hc, A_CON_HDLC, 0x80 | 0x00 | V_HDLC_TRP | V_IFF);
		HFC_outb(hc, R_FIFO, ch<<1 | 1);
		HFC_wait(hc);
		HFC_outb(hc, A_CON_HDLC, 0x80 | 0x00 | V_HDLC_TRP | V_IFF);
		HFC_outb(hc, R_FIFO, ch<<1);
		HFC_wait(hc);
	} else {
		/* change fifo */
		HFC_outb(hc, R_FIFO, ch<<1);
		HFC_wait(hc);
	}

//udelay(300);
}

/******************************/
/* Layer2 -> Layer 1 Transfer */
/******************************/

static int
handle_dmsg(channel_t *ch, struct sk_buff *skb)
{

	mISDN_head_t	*hh = mISDN_HEAD_P(skb);
	hfc_multi_t	*hc = ch->inst.privat;
	int		slot_tx, slot_rx, bank_tx, bank_rx;
	int		ret = -EAGAIN;
	u_long		flags;

	if (hh->prim == (PH_SIGNAL | REQUEST)) {
		spin_lock_irqsave(ch->inst.hwlock, flags);
		switch (hh->dinfo) {
			case INFO3_P8:
			case INFO3_P10:
				break;
			default:
				printk(KERN_DEBUG "%s: unknown PH_SIGNAL info %x\n",
					__FUNCTION__, hh->dinfo);
				ret = -EINVAL;
		}
		spin_unlock_irqrestore(ch->inst.hwlock, flags);
	} else if (hh->prim == (PH_CONTROL | REQUEST)) {
		spin_lock_irqsave(ch->inst.hwlock, flags);
		ret = 0;
		switch (hh->dinfo) {
			case HW_RESET:
				/* start activation */
				if (hc->type == 1) {
					HFC_outb(hc, R_E1_WR_STA, V_E1_LD_STA | 0);
					udelay(6); /* wait at least 5,21us */
					HFC_outb(hc, R_E1_WR_STA, 0);
				} else {
					HFC_outb(hc, R_ST_SEL, hc->chan[ch->channel].port);
					HFC_outb(hc, A_ST_WR_STATE, V_ST_LD_STA | 3); /* G1 */
					udelay(6); /* wait at least 5,21us */
					HFC_outb(hc, A_ST_WR_STATE, 3);
					HFC_outb(hc, A_ST_WR_STATE, 3 | (V_ST_ACT*3)); /* activate */
				}
				spin_unlock_irqrestore(ch->inst.hwlock, flags);
				skb_trim(skb, 0);
				return(mISDN_queueup_newhead(&ch->inst, 0, PH_CONTROL | INDICATION,HW_POWERUP, skb));
			case HW_DEACTIVATE:
				if (debug & DEBUG_HFCMULTI_MSG)
					printk(KERN_DEBUG "%s: HW_DEACTIVATE\n",
						__FUNCTION__);
				goto hw_deactivate; /* after lock */
			case HW_PCM_CONN: /* connect interface to pcm timeslot (0..N) */
				if (skb->len < 4*sizeof(u_long)) {
					printk(KERN_WARNING "%s: HW_PCM_CONN lacks parameters\n",
						__FUNCTION__);
					break;
				}
				slot_tx = ((int *)skb->data)[0];
				bank_tx = ((int *)skb->data)[1];
				slot_rx = ((int *)skb->data)[2];
				bank_rx = ((int *)skb->data)[3];
				if (debug & DEBUG_HFCMULTI_MSG)
					printk(KERN_INFO "%s: HW_PCM_CONN slot %d bank %d (TX) slot %d bank %d (RX)\n",
						__FUNCTION__, slot_tx, bank_tx, slot_rx, bank_rx);
				if (slot_tx <= hc->slots && bank_tx <=2 &&
					slot_rx <= hc->slots && bank_rx <= 2)
					hfcmulti_pcm(hc, ch->channel, slot_tx,
						bank_tx, slot_rx, bank_rx);
				else
					printk(KERN_WARNING "%s: HW_PCM_CONN slot %d bank %d (TX) slot %d bank %d (RX) out of range\n",
						__FUNCTION__, slot_tx, bank_tx, slot_rx, bank_rx);
				break;
			case HW_PCM_DISC: /* release interface from pcm timeslot */
				if (debug & DEBUG_HFCMULTI_MSG)
					printk(KERN_INFO "%s: HW_PCM_DISC\n",
						__FUNCTION__);
				hfcmulti_pcm(hc, ch->channel, -1, -1, -1, -1);
				break;
			case HW_POWERUP:
				HFC_outb(hc, R_ST_SEL, hc->chan[ch->channel].port);
				HFC_outb(hc, A_ST_WR_STATE, 3 | 0x10); /* activate */
				udelay(6); /* wait at least 5,21us */
				HFC_outb(hc, A_ST_WR_STATE, 3); /* activate */
				break;
			default:
				printk(KERN_DEBUG "%s: unknown PH_CONTROL info %x\n",
					__FUNCTION__, hh->dinfo);
				ret = -EINVAL;
		}
		spin_unlock_irqrestore(ch->inst.hwlock, flags);
	} else if (hh->prim == (PH_ACTIVATE | REQUEST)) {
		if (test_bit(HFC_CFG_NTMODE, &hc->chan[ch->channel].cfg)) {
			if (debug & DEBUG_HFCMULTI_MSG)
				printk(KERN_DEBUG "%s: PH_ACTIVATE port %d (0..%d)\n",
					__FUNCTION__, hc->chan[ch->channel].port, hc->type-1);
			spin_lock_irqsave(ch->inst.hwlock, flags);
			/* start activation */
			if (hc->type == 1) {
				//chanannel_sched_event(chan, D_L1STATECHANGE);
				ph_state_change(ch);
				if (debug & DEBUG_HFCMULTI_STATE)
					printk(KERN_DEBUG "%s: E1 report state %x \n",
						__FUNCTION__, ch->state);
			} else {
				HFC_outb(hc, R_ST_SEL, hc->chan[ch->channel].port);
				HFC_outb(hc, A_ST_WR_STATE, V_ST_LD_STA | 1); /* G1 */
				udelay(6); /* wait at least 5,21us */
				HFC_outb(hc, A_ST_WR_STATE, 1);
				HFC_outb(hc, A_ST_WR_STATE, 1 | (V_ST_ACT*3)); /* activate */
				ch->state = 1;
			}
			spin_unlock_irqrestore(ch->inst.hwlock, flags);
			ret = 0;
		} else {
			if (debug & DEBUG_HFCMULTI_MSG)
				printk(KERN_DEBUG "%s: PH_ACTIVATE no NT-mode port %d (0..%d)\n",
					__FUNCTION__, hc->chan[ch->channel].port, hc->type-1);
			ret = -EINVAL;
		}
	} else if (hh->prim == (PH_DEACTIVATE | REQUEST)) {
		if (test_bit(HFC_CFG_NTMODE, &hc->chan[ch->channel].cfg)) {
			if (debug & DEBUG_HFCMULTI_MSG)
				printk(KERN_DEBUG "%s: PH_DEACTIVATE port %d (0..%d)\n",
					__FUNCTION__, hc->chan[ch->channel].port, hc->type-1);
			spin_lock_irqsave(ch->inst.hwlock, flags);
hw_deactivate:
			//ch->state = 0;
			ch->state = 1;

			/* start deactivation */
			if (hc->type == 1) {
				if (debug & DEBUG_HFCMULTI_MSG)
					printk(KERN_DEBUG "%s: PH_DEACTIVATE no BRI\n",
						__FUNCTION__);
			} else {
				HFC_outb(hc, R_ST_SEL, hc->chan[ch->channel].port);
				HFC_outb(hc, A_ST_WR_STATE, V_ST_ACT*2); /* deactivate */
			}
			if (ch->next_skb) {
				dev_kfree_skb(ch->next_skb);
				ch->next_skb = NULL;
			}
			if (ch->rx_skb) {
				dev_kfree_skb(ch->rx_skb);
				ch->rx_skb = NULL;
			}
			ch->tx_idx = 0;
			if (ch->tx_skb) {
				dev_kfree_skb(ch->tx_skb);
				ch->tx_skb = NULL;
			}
			test_and_clear_bit(FLG_TX_NEXT, &ch->Flags);
			test_and_clear_bit(FLG_TX_BUSY, &ch->Flags);
			if (test_and_clear_bit(FLG_BUSY_TIMER, &ch->Flags))
				del_timer(&ch->timer);
			spin_unlock_irqrestore(ch->inst.hwlock, flags);
			ret = 0;
		} else {
			if (debug & DEBUG_HFCMULTI_MSG)
				printk(KERN_DEBUG "%s: PH_DEACTIVATE no NT-mode port %d (0..%d)\n",
					__FUNCTION__, hc->chan[ch->channel].port, hc->type-1);
			ret = -EINVAL;
		}
	} else if ((hh->prim & MISDN_CMD_MASK) == MGR_SHORTSTATUS) {
		u_int		temp = hh->dinfo & SSTATUS_ALL; // remove SSTATUS_BROADCAST_BIT
		if (((hc->type == 1) || test_bit(HFC_CFG_NTMODE,
			&hc->chan[ch->channel].cfg)) &&
			(temp == SSTATUS_ALL || temp == SSTATUS_L1)) {
			if (hh->dinfo & SSTATUS_BROADCAST_BIT)
				temp = ch->inst.id | MSG_BROADCAST;
			else
				temp = hh->addr | FLG_MSG_TARGET;
			skb_trim(skb, 0);
			hh->dinfo = test_bit(FLG_ACTIVE, &ch->Flags) ?
				SSTATUS_L1_ACTIVATED : SSTATUS_L1_DEACTIVATED;
			hh->prim = MGR_SHORTSTATUS | CONFIRM;
			return(mISDN_queue_message(&ch->inst, temp, skb));
		}
		ret = -EOPNOTSUPP;
	}
	if (!ret)
		dev_kfree_skb(skb);
	return(ret);
}

static int
handle_bmsg(channel_t *ch, struct sk_buff *skb)
{
	mISDN_head_t	*hh = mISDN_HEAD_P(skb);
	hfc_multi_t	*hc = ch->inst.privat;
	u_long		flags, num;
	int		slot_tx, slot_rx, bank_tx, bank_rx;
	int		ret = -EAGAIN;
	struct		dsp_features *features;
	int taps;

	if ((hh->prim == (PH_ACTIVATE | REQUEST)) ||
		(hh->prim == (DL_ESTABLISH  | REQUEST))) {
		/* activate B-channel if not already activated */
		if (debug & DEBUG_HFCMULTI_MSG)
			printk(KERN_DEBUG "%s: PH_ACTIVATE ch %d (0..32)\n",
				__FUNCTION__, ch->channel);
		if (!test_and_set_bit(FLG_ACTIVE, &ch->Flags)) {
			spin_lock_irqsave(ch->inst.hwlock, flags);
			if (ch->inst.pid.protocol[2] == ISDN_PID_L2_B_TRANS)
				test_and_set_bit(FLG_L2DATA, &ch->Flags);
			ret = mode_hfcmulti(hc, ch->channel,
				ch->inst.pid.protocol[1],
				hc->chan[ch->channel].slot_tx,
				hc->chan[ch->channel].bank_tx,
				hc->chan[ch->channel].slot_rx,
				hc->chan[ch->channel].bank_rx);
			if (!ret) {
				if (ch->inst.pid.protocol[1] ==
					 ISDN_PID_L1_B_64TRANS && !hc->dtmf) {
					/* start decoder */
					hc->dtmf = 1;
					if (debug & DEBUG_HFCMULTI_DTMF)
						printk(KERN_DEBUG "%s: start dtmf decoder\n",
							__FUNCTION__);
					HFC_outb(hc, R_DTMF, hc->hw.r_dtmf | V_RST_DTMF);
				}
			}
			spin_unlock_irqrestore(ch->inst.hwlock, flags);
		} else
			ret = 0;
		skb_trim(skb, 0);
		return(mISDN_queueup_newhead(&ch->inst, 0, hh->prim | CONFIRM, ret, skb));
	} 
	if ((hh->prim == (PH_DEACTIVATE | REQUEST)) ||
		(hh->prim == (DL_RELEASE | REQUEST)) ||
		((hh->prim == (PH_CONTROL | REQUEST) &&
		(hh->dinfo == HW_DEACTIVATE)))) {
		if (debug & DEBUG_HFCMULTI_MSG)
			printk(KERN_DEBUG "%s: PH_DEACTIVATE ch %d (0..32)\n",
				__FUNCTION__, ch->channel);
		/* deactivate B-channel if not already deactivated */
		spin_lock_irqsave(ch->inst.hwlock, flags);
		if (ch->next_skb) {
			test_and_clear_bit(FLG_TX_NEXT, &ch->Flags);
			dev_kfree_skb(ch->next_skb);
			ch->next_skb = NULL;
		}
		if (ch->tx_skb) {
			dev_kfree_skb(ch->tx_skb);
			ch->tx_skb = NULL;
		}
		ch->tx_idx = 0;
		if (ch->rx_skb) {
			dev_kfree_skb(ch->rx_skb);
			ch->rx_skb = NULL;
		}
		test_and_clear_bit(FLG_TX_BUSY, &ch->Flags);
		hc->chan[ch->channel].slot_tx = -1;
		hc->chan[ch->channel].slot_rx = -1;
		hc->chan[ch->channel].conf = -1;
		mode_hfcmulti(hc, ch->channel, ISDN_PID_NONE,
			hc->chan[ch->channel].slot_tx,
			hc->chan[ch->channel].bank_tx,
			hc->chan[ch->channel].slot_rx,
			hc->chan[ch->channel].bank_rx);
		test_and_clear_bit(FLG_L2DATA, &ch->Flags);
		test_and_clear_bit(FLG_ACTIVE, &ch->Flags);
		spin_unlock_irqrestore(ch->inst.hwlock, flags);
		skb_trim(skb, 0);
		ret = 0;
		if (hh->prim != (PH_CONTROL | REQUEST))
			return(mISDN_queueup_newhead(&ch->inst, 0,
				hh->prim | CONFIRM, ret, skb));
	} else if (hh->prim == (PH_CONTROL | REQUEST)) {
		spin_lock_irqsave(ch->inst.hwlock, flags);
		switch (hh->dinfo) {
			case HW_FEATURES: /* fill features structure */
				#warning this is dangerous, the skb should never used to transfer a pointer please use a message
				if (skb->len != sizeof(void *)) {
					printk(KERN_WARNING "%s: HW_FEATURES lacks parameters\n",
						__FUNCTION__);
					break;
				}
				if (debug & DEBUG_HFCMULTI_MSG)
					printk(KERN_DEBUG "%s: HW_FEATURE request\n",
						__FUNCTION__);
				features = *((struct dsp_features **)skb->data);
				features->hfc_id = hc->id;
				if (test_bit(HFC_CHIP_DTMF, &hc->chip))
					features->hfc_dtmf = 1;
				features->hfc_loops = 0;
				features->pcm_id = hc->pcm;
				features->pcm_slots = hc->slots;
				features->pcm_banks = 2;
				
				if (test_bit(HFC_CHIP_DIGICARD, &hc->chip))
					features->hfc_echocanhw=1;

				ret = 0;
				break;
			case HW_PCM_CONN: /* connect interface to pcm timeslot (0..N) */
				if (skb->len < 4*sizeof(s32)) {
					printk(KERN_WARNING "%s: HW_PCM_CONN lacks parameters\n",
						__FUNCTION__);
					break;
				}
				slot_tx = ((s32 *)skb->data)[0];
				bank_tx = ((s32 *)skb->data)[1];
				slot_rx = ((s32 *)skb->data)[2];
				bank_rx = ((s32 *)skb->data)[3];
				if (debug & DEBUG_HFCMULTI_MSG)
					printk(KERN_DEBUG "%s: HW_PCM_CONN slot %d bank %d (TX) slot %d bank %d (RX)\n",
						__FUNCTION__, slot_tx, bank_tx, slot_rx, bank_rx);
				if (slot_tx <= hc->slots && bank_tx <= 2 &&
					slot_rx <= hc->slots && bank_rx <= 2)
					hfcmulti_pcm(hc, ch->channel, slot_tx, bank_tx, slot_rx, bank_rx);
				else
					printk(KERN_WARNING "%s: HW_PCM_CONN slot %d bank %d (TX) slot %d bank %d (RX) out of range\n",
						__FUNCTION__, slot_tx, bank_tx, slot_rx, bank_rx);
				ret = 0;
				break;
			case HW_PCM_DISC: /* release interface from pcm timeslot */
				if (debug & DEBUG_HFCMULTI_MSG)
					printk(KERN_DEBUG "%s: HW_PCM_DISC\n",
						__FUNCTION__);
				hfcmulti_pcm(hc, ch->channel, -1, -1, -1, -1);
				ret = 0;
				break;
			case HW_CONF_JOIN: /* join conference (0..7) */
				if (skb->len < sizeof(u32)) {
					printk(KERN_WARNING "%s: HW_CONF_JOIN lacks parameters\n", __FUNCTION__);
					break;
				}
				num = ((u32 *)skb->data)[0];
				if (debug & DEBUG_HFCMULTI_MSG)
					printk(KERN_DEBUG "%s: HW_CONF_JOIN conf %ld\n",
						__FUNCTION__, num);
				if (num <= 7) {
					hfcmulti_conf(hc, ch->channel, num);
					ret = 0;
				} else
					printk(KERN_WARNING "%s: HW_CONF_JOIN conf %ld out of range\n",
						__FUNCTION__, num);
				break;
			case HW_CONF_SPLIT: /* split conference */
				if (debug & DEBUG_HFCMULTI_MSG)
					printk(KERN_DEBUG "%s: HW_CONF_SPLIT\n",
						__FUNCTION__);
				hfcmulti_conf(hc, ch->channel, -1);
				ret = 0;
				break;
			case HW_SPL_LOOP_ON: /* set sample loop */
				if (debug & DEBUG_HFCMULTI_MSG)
					printk(KERN_DEBUG "%s: HW_SPL_LOOP_ON (len = %d)\n",
						__FUNCTION__, skb->len);
				hfcmulti_splloop(hc, ch->channel, skb->data, skb->len);
				ret = 0;
				break;
			case HW_SPL_LOOP_OFF: /* set silence */
				if (debug & DEBUG_HFCMULTI_MSG)
					printk(KERN_DEBUG "%s: HW_SPL_LOOP_OFF\n",
						__FUNCTION__);
				hfcmulti_splloop(hc, ch->channel, NULL, 0);
				ret = 0;
				break;

			case HW_ECHOCAN_ON:
				
				if (skb->len < sizeof(u32)) {
					printk(KERN_WARNING "%s: HW_ECHOCAN_ON lacks parameters\n",
					       __FUNCTION__);
				}
				
				taps = ((u32 *)skb->data)[0];
				
				vpm_echocan_on(hc, ch->channel, taps);
				ret=0;
				break;

			case HW_ECHOCAN_OFF:
				vpm_echocan_off(hc, ch->channel);
				ret=0;
				break;

			default:
				printk(KERN_DEBUG "%s: unknown PH_CONTROL info %x\n",
					__FUNCTION__, hh->dinfo);
				ret = -EINVAL;
		}
		spin_unlock_irqrestore(ch->inst.hwlock, flags);
	} 
	if (!ret)
		dev_kfree_skb(skb);
	return(ret);
}

/* 
 * message transfer from layer 1 to hardware.
 */
static int
hfcmulti_l2l1(mISDNinstance_t *inst, struct sk_buff *skb)
{
	channel_t	*chan = container_of(inst, channel_t, inst);
	hfc_multi_t	*hc;
	int		ret = 0;
	mISDN_head_t	*hh = mISDN_HEAD_P(skb);
	u_long		flags;

	hc = chan->inst.privat;
	if ((hh->prim == PH_DATA_REQ) || (hh->prim == DL_DATA_REQ)) {
		spin_lock_irqsave(inst->hwlock, flags);
		ret = channel_senddata(chan, hh->dinfo, skb);
		if (ret > 0) { /* direct TX */
			hfcmulti_tx(hc, chan->channel, chan);
			/* start fifo */
			HFC_outb(hc, R_FIFO, 0);
			HFC_wait(hc);
			ret = 0;
		}
		spin_unlock_irqrestore(inst->hwlock, flags);
		return(ret);
	} 
	if (test_bit(FLG_DCHANNEL, &chan->Flags)) {
		ret = handle_dmsg(chan, skb);
		if (ret != -EAGAIN)
			return(ret);
		ret = -EINVAL;
	}
	if (test_bit(FLG_BCHANNEL, &chan->Flags)) {
		ret = handle_bmsg(chan, skb);
		if (ret != -EAGAIN)
			return(ret);
		ret = -EINVAL;
	}
	if (!ret)
		dev_kfree_skb(skb);
	return(ret);
}

/***************************/
/* handle D-channel events */
/***************************/

/* handle state change event
 */
static void ph_state_change(channel_t *dch)
{
	hfc_multi_t *hc = dch->inst.privat;
	u_int prim = PH_SIGNAL | INDICATION;
	u_int para = 0;
	int ch;

	if (!dch) {
		printk(KERN_WARNING "%s: ERROR given dch is NULL\n", __FUNCTION__);
		return;
	}
	ch = dch->channel;

	if (hc->type == 1) {
		if (!test_bit(HFC_CFG_NTMODE, &hc->chan[ch].cfg)) {
			if (debug & DEBUG_HFCMULTI_STATE)
				printk(KERN_DEBUG "%s: E1 TE newstate %x\n", __FUNCTION__, dch->state);
		} else {
			if (debug & DEBUG_HFCMULTI_STATE)
				printk(KERN_DEBUG "%s: E1 NT newstate %x\n", __FUNCTION__, dch->state);
		}
		switch (dch->state) {
			case (1):
				prim = PH_ACTIVATE | INDICATION;
				para = 0;
				test_and_set_bit(FLG_ACTIVE, &dch->Flags);
				break;

			default:
				if (hc->chan[ch].e1_state != 1)
					return;
				prim = PH_DEACTIVATE | INDICATION;
				para = 0;
				test_and_clear_bit(FLG_ACTIVE, &dch->Flags);
		}
		hc->chan[ch].e1_state = dch->state;
	} else {
		if (!test_bit(HFC_CFG_NTMODE, &hc->chan[ch].cfg)) {
			if (debug & DEBUG_HFCMULTI_STATE)
				printk(KERN_DEBUG "%s: S/T TE newstate %x\n", __FUNCTION__, dch->state);
			switch (dch->state) {
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
			if (debug & DEBUG_HFCMULTI_STATE)
				printk(KERN_DEBUG "%s: S/T NT newstate %x\n", __FUNCTION__, dch->state);
			switch (dch->state) {
				case (2):
					if (hc->chan[ch].nt_timer == 0) {
						hc->chan[ch].nt_timer = -1;
						HFC_outb(hc, R_ST_SEL, hc->chan[ch].port);
						HFC_outb(hc, A_ST_WR_STATE, 4 | V_ST_LD_STA); /* G4 */
						udelay(6); /* wait at least 5,21us */
						HFC_outb(hc, A_ST_WR_STATE, 4);
						dch->state = 4;
					} else {
						/* one extra count for the next event */
						hc->chan[ch].nt_timer = nt_t1_count[poll_timer] + 1;
						HFC_outb(hc, R_ST_SEL, hc->chan[ch].port);
						HFC_outb(hc, A_ST_WR_STATE, 2 | V_SET_G2_G3); /* allow G2 -> G3 transition */
					}
					return;

				case (1):
					prim = PH_DEACTIVATE | INDICATION;
					para = 0;
					hc->chan[ch].nt_timer = -1;
					test_and_clear_bit(FLG_ACTIVE, &dch->Flags);
					break;

				case (4):
					hc->chan[ch].nt_timer = -1;
					return;

				case (3):
					prim = PH_ACTIVATE | INDICATION;
					para = 0;
					hc->chan[ch].nt_timer = -1;
					test_and_set_bit(FLG_ACTIVE, &dch->Flags);
					break;

				default:
					return;
			}
		}
	}

	mISDN_queue_data(&dch->inst, FLG_MSG_UP, prim, para, 0, NULL, 0);
	if ((hc->type == 1) || test_bit(HFC_CFG_NTMODE, &hc->chan[ch].cfg))
		mISDN_queue_data(&dch->inst, dch->inst.id | MSG_BROADCAST,
			MGR_SHORTSTATUS | INDICATION, test_bit(FLG_ACTIVE, &dch->Flags) ?
			SSTATUS_L1_ACTIVATED : SSTATUS_L1_DEACTIVATED,
			0, NULL, 0);
}

/*************************************/
/* called for card mode init message */
/*************************************/

static void
hfcmulti_initmode(hfc_multi_t *hc)
{
	int		nt_mode;
	BYTE		r_sci_msk, a_st_wr_state, r_e1_wr_sta;
	int		i, port;
	channel_t	*dch;
//	u_long		flags;

	if (debug & DEBUG_HFCMULTI_INIT)
		printk("%s: entered\n", __FUNCTION__);

	if (hc->type == 1) {
		nt_mode = test_bit(HFC_CFG_NTMODE, &hc->chan[16].cfg);
		hc->chan[16].slot_tx = -1;
		hc->chan[16].slot_rx = -1;
		hc->chan[16].conf = -1;
		mode_hfcmulti(hc, 16, nt_mode?ISDN_PID_L1_NT_E1:ISDN_PID_L1_TE_E1, -1, 0, -1, 0);
		hc->chan[16].ch->timer.function = (void *) hfcmulti_dbusy_timer;
		hc->chan[16].ch->timer.data = (long) &hc->chan[16].ch;
		init_timer(&hc->chan[16].ch->timer);

		i = 0;
		while (i < 30) {
			hc->chan[i+1+(i>=15)].slot_tx = -1;
			hc->chan[i+1+(i>=15)].slot_rx = -1;
			hc->chan[i+1+(i>=15)].conf = -1;
			mode_hfcmulti(hc, i+1+(i>=15), ISDN_PID_NONE, -1, 0, -1, 0);
			i++;
		}
	} else {
		i = 0;
		while (i < hc->type) {
			nt_mode = test_bit(HFC_CFG_NTMODE, &hc->chan[(i<<2)+2].cfg);
			hc->chan[(i<<2)+2].slot_tx = -1;
			hc->chan[(i<<2)+2].slot_rx = -1;
			hc->chan[(i<<2)+2].conf = -1;
			mode_hfcmulti(hc, (i<<2)+2, nt_mode?ISDN_PID_L1_NT_S0:ISDN_PID_L1_TE_S0, -1, 0, -1, 0);
			hc->chan[(i<<2)+2].ch->timer.function = (void *) hfcmulti_dbusy_timer;
			hc->chan[(i<<2)+2].ch->timer.data = (long) &hc->chan[(i<<2)+2].ch;
			init_timer(&hc->chan[(i<<2)+2].ch->timer);

			hc->chan[i<<2].slot_tx = -1;
			hc->chan[i<<2].slot_rx = -1;
			hc->chan[i<<2].conf = -1;
			mode_hfcmulti(hc, i<<2, ISDN_PID_NONE, -1, 0, -1, 0);
			hc->chan[(i<<2)+1].slot_tx = -1;
			hc->chan[(i<<2)+1].slot_rx = -1;
			hc->chan[(i<<2)+1].conf = -1;
			mode_hfcmulti(hc, (i<<2)+1, ISDN_PID_NONE, -1, 0, -1, 0);
			i++;
		}
	}

	/* set up interface */
	if (hc->type != 1) {
		/* ST */
		r_sci_msk = 0;
		i = 0;
		while(i < 32) {
			dch = hc->chan[i].ch;
			if (!dch || !test_bit(FLG_DCHANNEL, &dch->Flags)) {
				i++;
				continue;
			}
			port = hc->chan[i].port;
			/* select interface */
			HFC_outb(hc, R_ST_SEL, port);
			if (test_bit(HFC_CFG_NTMODE, &hc->chan[i].cfg)) {
				if (debug & DEBUG_HFCMULTI_INIT)
					printk(KERN_DEBUG "%s: ST port %d is NT-mode\n", __FUNCTION__, port);
				/* clock delay */
				HFC_outb(hc, A_ST_CLK_DLY, CLKDEL_NT | 0x60);
				a_st_wr_state = 1; /* G1 */
				hc->hw.a_st_ctrl0[port] = V_ST_MD;
			} else {
				if (debug & DEBUG_HFCMULTI_INIT)
					printk(KERN_DEBUG "%s: ST port %d is TE-mode\n", __FUNCTION__, port);
				/* clock delay */
				HFC_outb(hc, A_ST_CLK_DLY, CLKDEL_TE);
				a_st_wr_state = 2; /* F2 */
				hc->hw.a_st_ctrl0[port] = 0;
			}
			if (!test_bit(HFC_CFG_NONCAP_TX, &hc->chan[i].cfg)) {
				hc->hw.a_st_ctrl0[port] |= V_TX_LI;
			}
			/* line setup */
			HFC_outb(hc, A_ST_CTRL0,  hc->hw.a_st_ctrl0[port]);
			/* disable E-channel */
			if (test_bit(HFC_CFG_NTMODE, &hc->chan[i].cfg)
			 || test_bit(HFC_CFG_DIS_ECHANNEL, &hc->chan[i].cfg))
				HFC_outb(hc, A_ST_CTRL1, V_E_IGNO);
			/* enable B-channel receive */
			HFC_outb(hc, A_ST_CTRL2,  V_B1_RX_EN | V_B2_RX_EN);
			/* state machine setup */
			HFC_outb(hc, A_ST_WR_STATE, a_st_wr_state | V_ST_LD_STA);
			udelay(6); /* wait at least 5,21us */
			HFC_outb(hc, A_ST_WR_STATE, a_st_wr_state);
			r_sci_msk |= 1 << port;
			i++;
		}
		/* state machine interrupts */
		HFC_outb(hc, R_SCI_MSK, r_sci_msk);
	} else {
		/* E1 */
		if (test_bit(HFC_CFG_REPORT_LOS, &hc->chan[16].cfg)) {
			HFC_outb(hc, R_LOS0, 255); /* 2 ms */
			HFC_outb(hc, R_LOS1, 255); /* 512 ms */
		}
		if (test_bit(HFC_CFG_OPTICAL, &hc->chan[16].cfg)) {
			HFC_outb(hc, R_RX0, 0);
			hc->hw.r_tx0 = 0 | V_OUT_EN;
		} else {
			HFC_outb(hc, R_RX0, 1);
			hc->hw.r_tx0 = 1 | V_OUT_EN;
		}
		hc->hw.r_tx1 = V_ATX | V_NTRI;
		HFC_outb(hc, R_TX0, hc->hw.r_tx0);
		HFC_outb(hc, R_TX1, hc->hw.r_tx1);
		HFC_outb(hc, R_TX_FR0, 0x00);
		HFC_outb(hc, R_TX_FR1, 0xf8);

		if (test_bit(HFC_CFG_CRC4, &hc->chan[16].cfg)) 
			HFC_outb(hc, R_TX_FR2, V_TX_MF | V_TX_E | V_NEG_E);

		HFC_outb(hc, R_RX_FR0, V_AUTO_RESYNC | V_AUTO_RECO | 0);

		if (test_bit(HFC_CFG_CRC4, &hc->chan[16].cfg)) 
			HFC_outb(hc, R_RX_FR1, V_RX_MF | V_RX_MF_SYNC);

		if (test_bit(HFC_CHIP_PCM_SLAVE, &hc->chip)) {
			/* SLAVE (clock master) */
			if (debug & DEBUG_HFCMULTI_INIT)
				printk(KERN_DEBUG "%s: E1 port is clock master (clock from PCM)\n", __FUNCTION__);
			HFC_outb(hc, R_SYNC_CTRL, V_EXT_CLK_SYNC | V_PCM_SYNC);
		} else {
			if (test_bit(HFC_CHIP_CRYSTAL_CLOCK, &hc->chip)) {
				/* MASTER (clock master) */
				if (debug & DEBUG_HFCMULTI_INIT)
					printk(KERN_DEBUG "%s: E1 port is clock master (clock from crystal)\n", __FUNCTION__);
				HFC_outb(hc, R_SYNC_CTRL, V_EXT_CLK_SYNC | V_PCM_SYNC | V_JATT_OFF);
			} else {
				/* MASTER (clock slave) */
				if (debug & DEBUG_HFCMULTI_INIT)
					printk(KERN_DEBUG "%s: E1 port is clock slave (clock to PCM)\n", __FUNCTION__);
				HFC_outb(hc, R_SYNC_CTRL, V_SYNC_OFFS);
			}
		}
		if (test_bit(HFC_CFG_NTMODE, &hc->chan[(i<<2)+2].cfg)) {
			if (debug & DEBUG_HFCMULTI_INIT)
				printk(KERN_DEBUG "%s: E1 port is NT-mode\n", __FUNCTION__);
			r_e1_wr_sta = 0; /* G0 */
		}else {
			if (debug & DEBUG_HFCMULTI_INIT)
				printk(KERN_DEBUG "%s: E1 port is TE-mode\n", __FUNCTION__);
			r_e1_wr_sta = 0; /* F0 */
		}
		HFC_outb(hc, R_JATT_ATT, 0x9c); /* undoc register */
		if (test_bit(HFC_CHIP_RX_SYNC, &hc->chip)) {
			HFC_outb(hc, R_SYNC_OUT, V_SYNC_E1_RX | V_IPATS0 | V_IPATS1 | V_IPATS2);
		} else {
			HFC_outb(hc, R_SYNC_OUT, V_IPATS0 | V_IPATS1 | V_IPATS2);
		}
		HFC_outb(hc, R_PWM_MD, V_PWM0_MD);
		HFC_outb(hc, R_PWM0, 0x50);
		HFC_outb(hc, R_PWM1, 0xff);
		/* state machine setup */
		HFC_outb(hc, R_E1_WR_STA, r_e1_wr_sta | V_E1_LD_STA);
		udelay(6); /* wait at least 5,21us */
		HFC_outb(hc, R_E1_WR_STA, r_e1_wr_sta);

	}
	if (debug & DEBUG_HFCMULTI_INIT)
		printk("%s: done\n", __FUNCTION__);
}


/***********************/
/* initialize the card */
/***********************/

/* start timer irq, wait some time and check if we have interrupts.
 * if not, reset chip and try again.
 */
static int
init_card(hfc_multi_t *hc)
{
	int 	cnt = 1; /* as long as there is no trouble */
	int 	err = -EIO;
	u_long		flags;
#ifdef CONFIG_PLX_PCI_BRIDGE
	u_short	*plx_acc;
#endif

	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: entered\n", __FUNCTION__);

	spin_lock_irqsave(&hc->lock, flags);
	/* set interrupts but let global interrupt disabled*/
	hc->hw.r_irq_ctrl = V_FIFO_IRQ;
	disable_hwirq(hc);
	spin_unlock_irqrestore(&hc->lock, flags);

	if (request_irq(hc->pci_dev->irq, hfcmulti_interrupt, SA_SHIRQ, "HFC-multi", hc)) {
		printk(KERN_WARNING "mISDN: Could not get interrupt %d.\n", hc->pci_dev->irq);
		return(-EIO);
	}
	hc->irq = hc->pci_dev->irq;

#ifdef CONFIG_PLX_PCI_BRIDGE
	plx_acc=(u_short*)(hc->plx_membase+0x4c);
	*plx_acc=0x41;  // enable PCI & LINT1 irq
#endif

	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: IRQ %d count %d\n", __FUNCTION__, hc->irq, hc->irqcnt);
	while (cnt) {
		if ((err = init_chip(hc))) {
			goto error;
		}
		/* Finally enable IRQ output
		 * this is only allowed, if an IRQ routine is allready
		 * established for this HFC, so don't do that earlier
		 */
		spin_lock_irqsave(&hc->lock, flags);
		enable_hwirq(hc);
		spin_unlock_irqrestore(&hc->lock, flags);
		//printk(KERN_DEBUG "no master irq set!!!\n");
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout((100*HZ)/1000); /* Timeout 100ms */
		/* turn IRQ off until chip is completely initialized */
		spin_lock_irqsave(&hc->lock, flags);
		disable_hwirq(hc);
		spin_unlock_irqrestore(&hc->lock, flags);
		if (debug & DEBUG_HFCMULTI_INIT)
			printk(KERN_DEBUG "%s: IRQ %d count %d\n", __FUNCTION__, hc->irq, hc->irqcnt);
		if (hc->irqcnt) {
			if (debug & DEBUG_HFCMULTI_INIT)
				printk(KERN_DEBUG "%s: done\n", __FUNCTION__);
			return(0);
		} 
		printk(KERN_WARNING "HFC PCI: IRQ(%d) getting no interrupts during init (try %d)\n", hc->irq, cnt);

		if (test_bit(HFC_CHIP_CLOCK_IGNORE, &hc->chip) || test_bit(HFC_CHIP_DIGICARD, &hc->chip)) {
			printk(KERN_WARNING "HFC PCI: Ignoring Clock so we go on here\n");
			return 0;
		}
		
#ifdef CONFIG_PLX_PCI_BRIDGE
		plx_acc=(u_short*)(hc->plx_membase+0x4c);
		*plx_acc=0x00;  // disable PCI & LINT1 irq
#endif
		cnt--;
		err = -EIO;
	}

	error:
	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_WARNING "%s: free irq %d\n", __FUNCTION__, hc->irq);
	if (hc->irq) {
		free_irq(hc->irq, hc);
		hc->irq = 0;
	}

	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: done (err=%d)\n", __FUNCTION__, err);
	return(err);
}


/*********************************************************/
/* select free channel and return OK(0), -EBUSY, -EINVAL */
/*********************************************************/

static int
SelFreeBChannel(hfc_multi_t *hc, int ch, channel_info_t *ci)
{
	channel_t		*bch;
	hfc_multi_t		*hfc;
	mISDNstack_t		*bst;
	struct list_head	*head;
	int			cnr;
	int			port = hc->chan[ch].port;

	if (port < 0 || port >= hc->type) {
		printk(KERN_WARNING "%s: port(%d) out of range", __FUNCTION__, port);
		return(-EINVAL);
	}

	if (!ci)
		return(-EINVAL);
	ci->st.p = NULL;
	cnr=0;
	bst = hc->chan[ch].ch->inst.st;
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
		if (cnr == ((hc->type == 1) ? 30: 2)) /* 30 or 2 B-channels */ {
			printk(KERN_WARNING "%s: fatal error: more b-stacks than ports", __FUNCTION__);
			return(-EINVAL);
		}
		if(!bst->mgr) {
			int_errtxt("no mgr st(%p)", bst);
			return(-EINVAL);
		}
		hfc = bst->mgr->privat;
		if (!hfc) {
			int_errtxt("no mgr->data st(%p)", bst);
			return(-EINVAL);
		}
		if (hc->type == 1)
			bch = hc->chan[cnr + 1 + (cnr>=15)].ch;
		else
			bch = hc->chan[(port<<2) + cnr].ch;
		if (!(ci->channel & (~CHANNEL_NUMBER))) {
			/* only number is set */
			if ((ci->channel & 0x3) == (cnr + 1)) {
				if (test_bit(FLG_ACTIVE, &bch->Flags))
					return(-EBUSY);
				ci->st.p = bst;
				return(0);
			}
		}
		if ((ci->channel & (~CHANNEL_NUMBER)) == 0x00a18300) {
			if (!test_bit(FLG_ACTIVE, &bch->Flags)) {
				ci->st.p = bst;
				return(0);
			}
		}
		cnr++;
	}
	return(-EBUSY);
}


/*********************************/
/* find pci device and set it up */
/*********************************/

static int
setup_pci(hfc_multi_t *hc, struct pci_dev *pdev, int id_idx)
{
	int i;

	printk(KERN_INFO "HFC-multi: card manufacturer: '%s' card name: '%s' clock: %s\n", id_list[id_idx].vendor_name, id_list[id_idx].card_name, (id_list[id_idx].clock2)?"double":"normal");

	/* go into 0-state (we might already be due to zero-filled-object */
	for (i = 0; i < 32; i++) {
		if (hc->chan[i].ch && test_bit(FLG_DCHANNEL, &hc->chan[i].ch->Flags))
			hc->chan[i].ch->state = 0;
	}

	hc->pci_dev = pdev;
	if (id_list[id_idx].clock2)
		test_and_set_bit(HFC_CHIP_CLOCK2, &hc->chip);

#if 1
	if (id_list[id_idx].device_id == 0xB410)
		test_and_set_bit(HFC_CHIP_DIGICARD, &hc->chip);
#endif

	if (hc->pci_dev->irq <= 0) {
		printk(KERN_WARNING "HFC-multi: No IRQ for PCI card found.\n");
		return (-EIO);
	}
	if (pci_enable_device(hc->pci_dev)) {
		printk(KERN_WARNING "HFC-multi: Error enabling PCI card.\n");
		return (-EIO);
	}
	hc->leds = id_list[id_idx].leds;
	
#ifdef CONFIG_HFCMULTI_PCIMEM
	hc->pci_membase = NULL;
	hc->plx_membase = NULL;

#ifdef CONFIG_PLX_PCI_BRIDGE
	hc->plx_origmembase =  get_pcibase(hc->pci_dev, 0);  // MEMBASE 1 is PLX PCI Bridge

	if (!hc->plx_origmembase) {
		printk(KERN_WARNING "HFC-multi: No IO-Memory for PCI PLX bridge found\n");
		pci_disable_device(hc->pci_dev);
		return (-EIO);
	}

	if (!(hc->plx_membase = ioremap(hc->plx_origmembase, 128))) {
		printk(KERN_WARNING "HFC-multi: failed to remap plx address space. (internal error)\n");
		hc->plx_membase = NULL;
		pci_disable_device(hc->pci_dev);
		return (-EIO);
	}
	printk(KERN_WARNING "HFC-multi: plx_membase:%#x plx_origmembase:%#x\n",(u_int) hc->plx_membase, (u_int)hc->plx_origmembase);

	hc->pci_origmembase =  get_pcibase(hc->pci_dev, 2);  // MEMBASE 1 is PLX PCI Bridge
	if (!hc->pci_origmembase) {
		printk(KERN_WARNING "HFC-multi: No IO-Memory for PCI card found\n");
		pci_disable_device(hc->pci_dev);
		return (-EIO);
	}

	if (!(hc->pci_membase = ioremap(hc->pci_origmembase, 0x400))) {
		printk(KERN_WARNING "HFC-multi: failed to remap io address space. (internal error)\n");
		hc->pci_membase = NULL;
		pci_disable_device(hc->pci_dev);
		return (-EIO);
	}

	printk(KERN_INFO "%s: defined at MEMBASE %#x (%#x) IRQ %d HZ %d leds-type %d\n", hc->name, (u_int) hc->pci_membase, (u_int) hc->pci_origmembase, hc->pci_dev->irq, HZ, hc->leds);
	pci_write_config_word(hc->pci_dev, PCI_COMMAND, PCI_ENA_MEMIO);
#else // CONFIG_PLX_PCI_BRIDGE
	hc->pci_origmembase = get_pcibase(hc->pci_dev, 1);
	if (!hc->pci_origmembase) {
		printk(KERN_WARNING "HFC-multi: No IO-Memory for PCI card found\n");
		pci_disable_device(hc->pci_dev);
		return (-EIO);
	}

	if (!(hc->pci_membase = ioremap(hc->pci_origmembase, 256))) {
		printk(KERN_WARNING "HFC-multi: failed to remap io address space. (internal error)\n");
		hc->pci_membase = NULL;
		pci_disable_device(hc->pci_dev);
		return (-EIO);
	}
	printk(KERN_INFO "%s: defined at MEMBASE %#x (%#x) IRQ %d HZ %d leds-type %d\n", hc->name, (u_int) hc->pci_membase, (u_int) hc->pci_origmembase, hc->pci_dev->irq, HZ, hc->leds);
	pci_write_config_word(hc->pci_dev, PCI_COMMAND, PCI_ENA_MEMIO);
#endif // CONFIG_PLX_PCI_BRIDGE
#else
	hc->pci_iobase = (u_int) get_pcibase(hc->pci_dev, 0);
	if (!hc->pci_iobase) {
		printk(KERN_WARNING "HFC-multi: No IO for PCI card found\n");
		pci_disable_device(hc->pci_dev);
		return (-EIO);
	}

	if (!request_region(hc->pci_iobase, 8, "hfcmulti")) {
		printk(KERN_WARNING "HFC-multi: failed to rquest address space at 0x%04x (internal error)\n", hc->pci_iobase);
		hc->pci_iobase = 0;
		pci_disable_device(hc->pci_dev);
		return (-EIO);
	}

	printk(KERN_INFO "%s: defined at IOBASE %#x IRQ %d HZ %d leds-type %d\n", hc->name, (u_int) hc->pci_iobase, hc->pci_dev->irq, HZ, hc->leds);
	pci_write_config_word(hc->pci_dev, PCI_COMMAND, PCI_ENA_REGIO);
#endif

	pci_set_drvdata(hc->pci_dev, hc);

	/* At this point the needed PCI config is done */
	/* fifos are still not enabled */
	return (0);
}




static void release_ports_hw(hfc_multi_t *hc)
{
	u_long flags;

	printk(KERN_INFO "release_ports_hw called type=%d\n",hc->type);

	spin_lock_irqsave(&hc->lock, flags);
	
	/*first we disable all the hw stuff*/
	disable_hwirq(hc);

	spin_unlock_irqrestore(&hc->lock, flags);
	
	udelay(1000);
	
	/* disable D-channels & B-channels */
	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: disable all channels (d and b)\n", __FUNCTION__);


	/* dimm leds */
	if (hc->leds)
		hfcmulti_leds(hc);

#if 0
	if (hc->type == 1) {
		hc->chan[16].slot_tx = -1;
		hc->chan[16].slot_rx = -1;
		hc->chan[16].conf = -1;
		mode_hfcmulti(hc, 16, ISDN_PID_NONE, -1, 0, -1, 0);//d
		i = 0;
		while(i < 30) {
			hc->chan[i+1+(i>=15)].slot_tx = -1;
			hc->chan[i+1+(i>=15)].slot_rx = -1;
			hc->chan[i+1+(i>=15)].conf = -1;
			mode_hfcmulti(hc, i+1+(i>=15), ISDN_PID_NONE, -1, 0, -1, 0); //b
			i++;
		}
	} else {
		i = 0;
		while(i < hc->type) {
			if (hc->created[i]) {
				hc->chan[(i<<2)+2].slot_tx = -1;
				hc->chan[(i<<2)+2].slot_rx = -1;
				hc->chan[(i<<2)+2].conf = -1;
				mode_hfcmulti(hc, (i<<2)+2, ISDN_PID_NONE, -1, 0, -1, 0); //d
				hc->chan[i<<2].slot_tx = -1;
				hc->chan[i<<2].slot_rx = -1;
				hc->chan[i<<2].conf = -1;
				mode_hfcmulti(hc, i<<2, ISDN_PID_NONE, -1, 0, -1, 0); //b1
				hc->chan[(i<<2)+1].slot_tx = -1;
				hc->chan[(i<<2)+1].slot_rx = -1;
				hc->chan[(i<<2)+1].conf = -1;
				mode_hfcmulti(hc, (i<<2)+1, ISDN_PID_NONE, -1, 0, -1, 0); //b2
			}
			i++;
		}
	}
#endif

	release_io_hfcmulti(hc);

	if (hc->irq) {
		if (debug & DEBUG_HFCMULTI_INIT)
			printk(KERN_WARNING "%s: free irq %d\n", __FUNCTION__, hc->irq);
		free_irq(hc->irq, hc);
		hc->irq = 0;

	}
	udelay(1000);	
	
	/*now we finish off the lists and stuff*/
	/* remove us from list and delete */
	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_WARNING "%s: remove instance from list\n", __FUNCTION__);

#if 1
	u_long flags2;
	spin_lock_irqsave(&HFCM_obj.lock,flags2);
	list_del(&hc->list);
	spin_unlock_irqrestore(&HFCM_obj.lock,flags2);
#endif

	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_WARNING "%s: delete instance\n", __FUNCTION__);
	

	kfree(hc);
	hc=NULL;
	HFC_cnt--;
	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_WARNING "%s: card successfully removed\n", __FUNCTION__);


	printk(KERN_INFO "release_ports_hw finished \n");
	
}

/***************************
 * remove port  from stack *
 ***************************/

static void
release_port(hfc_multi_t *hc, int port)
{
	int	i = 0;
	u_long	flags;

	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: entered\n", __FUNCTION__);
	
	if (port >= hc->type) {
		printk(KERN_WARNING "%s: ERROR port out of range (%d).\n", __FUNCTION__, port);
		return;
	}

	spin_lock_irqsave(&hc->lock, flags);

	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: releasing port=%d\n", __FUNCTION__, port);
	
	if ( !hc->created[port]) {
		printk(KERN_WARNING "%s: ERROR given stack is not used by card (port=%d).\n", __FUNCTION__, port);
		spin_unlock_irqrestore(&hc->lock,flags);
		return;
	}
	


	for (i=0;i<32;i++) {
		if (hc->chan[i].ch && test_bit(FLG_DCHANNEL, &hc->chan[i].ch->Flags)  &&
		    hc->chan[i].ch->timer.function != NULL ) {
			del_timer(&hc->chan[i].ch->timer);
			hc->chan[i].ch->timer.function = NULL;
		}
	}
	
	/* free channels */
	i = 0;
	while(i < 32) {
		if (hc->chan[i].ch) {
			if (debug & DEBUG_HFCMULTI_INIT)
				printk(KERN_DEBUG "%s: free port %d %c-channel %d (1..32)\n",
				       __FUNCTION__, hc->chan[i].port,
				       test_bit(FLG_DCHANNEL, &hc->chan[i].ch->Flags) ?
				       'D' : 'B', i);
			mISDN_freechannel(hc->chan[i].ch);

			spin_unlock_irqrestore(&hc->lock,flags);
			
			if (test_bit(FLG_DCHANNEL, &hc->chan[i].ch->Flags))
				mISDN_ctrl(&hc->chan[i].ch->inst, MGR_UNREGLAYER | REQUEST, NULL);
			spin_lock_irqsave(&hc->lock,flags);
			
			kfree(hc->chan[i].ch);
			hc->chan[i].ch = NULL;
		}
		i++;
	}
	
	hc->created[port]=0;

	spin_unlock_irqrestore(&hc->lock,flags);
	
}

static int
HFC_manager(void *data, u_int prim, void *arg)
{
	hfc_multi_t	*hc;
	mISDNinstance_t	*inst = data;
	struct sk_buff	*skb;
	channel_t	*chan = NULL;
	int		ch = -1;
	int		i;
	u_long		flags;

	if (!data) {
		MGR_HASPROTOCOL_HANDLER(prim,arg,&HFCM_obj)
		printk(KERN_ERR "%s: no data prim %x arg %p\n", __FUNCTION__, prim, arg);
		return(-EINVAL);
	}

	/* find channel and card */
	spin_lock_irqsave(&HFCM_obj.lock, flags);
	list_for_each_entry(hc, &HFCM_obj.ilist, list) {
		i = 0;
		while(i < 32) {
//printk(KERN_DEBUG "comparing (D-channel) card=%08x inst=%08x with inst=%08x\n", hc, &hc->dch[i].inst, inst);
			if ((hc->chan[i].ch) &&
				(&hc->chan[i].ch->inst == inst)) {
				ch = i;
				chan = hc->chan[i].ch;
				break;
			}
			i++;
		}
		if (ch >= 0)
			break;
	}
	spin_unlock_irqrestore(&HFCM_obj.lock, flags);
	if (ch < 0) {
		printk(KERN_ERR "%s: no card/channel found  data %p prim %x arg %p\n", __FUNCTION__, data, prim, arg);
		return(-EINVAL);
	}
	if (debug & DEBUG_HFCMULTI_MGR)
		printk(KERN_DEBUG "%s: channel %d (0..31)  data %p prim %x arg %p\n", __FUNCTION__, ch, data, prim, arg);

	switch(prim) {
		case MGR_REGLAYER | CONFIRM:
bugtest
		if (debug & DEBUG_HFCMULTI_MGR)
			printk(KERN_DEBUG "%s: MGR_REGLAYER\n", __FUNCTION__);
		mISDN_setpara(chan, &inst->st->para);
bugtest
		break;

		case MGR_UNREGLAYER | REQUEST:
bugtest
		if (debug & DEBUG_HFCMULTI_MGR)
			printk(KERN_DEBUG "%s: MGR_UNREGLAYER\n", __FUNCTION__);
		i = test_bit(FLG_DCHANNEL, &chan->Flags) ? HW_DEACTIVATE : 0;
		if ((skb = create_link_skb(PH_CONTROL | REQUEST, i, 0, NULL, 0))) {
			if (hfcmulti_l2l1(inst, skb))
				dev_kfree_skb(skb);
		}
		mISDN_ctrl(inst, MGR_UNREGLAYER | REQUEST, NULL);
bugtest
		break;

		case MGR_CLRSTPARA | INDICATION:
		arg = NULL;
		// fall through
		case MGR_ADDSTPARA | INDICATION:
		if (debug & DEBUG_HFCMULTI_MGR)
			printk(KERN_DEBUG "%s: MGR_***STPARA\n", __FUNCTION__);
		mISDN_setpara(chan, arg);
		break;

		case MGR_RELEASE | INDICATION:
		if (debug & DEBUG_HFCMULTI_MGR)
			printk(KERN_DEBUG "%s: MGR_RELEASE = remove port from mISDN\n", __FUNCTION__);
		break;
#ifdef FIXME
		case MGR_CONNECT | REQUEST:
		if (debug & DEBUG_HFCMULTI_MGR)
			printk(KERN_DEBUG "%s: MGR_CONNECT\n", __FUNCTION__);
		return(mISDN_ConnectIF(inst, arg));

		case MGR_SETIF | REQUEST:
		case MGR_SETIF | INDICATION:
		if (debug & DEBUG_HFCMULTI_MGR)
			printk(KERN_DEBUG "%s: MGR_SETIF\n", __FUNCTION__);
		if (dch)
			return(mISDN_SetIF(inst, arg, prim, hfcmulti_l1hw, NULL, dch));
		if (bch)
			return(mISDN_SetIF(inst, arg, prim, hfcmulti_l2l1, NULL, bch));
		break;

		case MGR_DISCONNECT | REQUEST:
		case MGR_DISCONNECT | INDICATION:
		if (debug & DEBUG_HFCMULTI_MGR)
			printk(KERN_DEBUG "%s: MGR_DISCONNECT\n", __FUNCTION__);
		return(mISDN_DisConnectIF(inst, arg));
#endif
		case MGR_SELCHANNEL | REQUEST:
		if (debug & DEBUG_HFCMULTI_MGR)
			printk(KERN_DEBUG "%s: MGR_SELCHANNEL\n", __FUNCTION__);
		if (!test_bit(FLG_DCHANNEL, &chan->Flags)) {
			printk(KERN_WARNING "%s(MGR_SELCHANNEL|REQUEST): selchannel not dinst\n", __FUNCTION__);
			return(-EINVAL);
		}
		return(SelFreeBChannel(hc, ch, arg));

		case MGR_SETSTACK | INDICATION:
bugtest
		if (debug & DEBUG_HFCMULTI_MGR)
			printk(KERN_DEBUG "%s: MGR_SETSTACK\n", __FUNCTION__);
		if (test_bit(FLG_BCHANNEL, &chan->Flags) && inst->pid.global==2) {
			if ((skb = create_link_skb(PH_ACTIVATE | REQUEST, 0, 0, NULL, 0))) {
				if (hfcmulti_l2l1(inst, skb))
					dev_kfree_skb(skb);
			}
			if (inst->pid.protocol[2] == ISDN_PID_L2_B_TRANS)
				mISDN_queue_data(inst, FLG_MSG_UP, DL_ESTABLISH | INDICATION, 0, 0, NULL, 0);
			else
				mISDN_queue_data(inst, FLG_MSG_UP, PH_ACTIVATE | INDICATION, 0, 0, NULL, 0);
		}
bugtest
		break;

		PRIM_NOT_HANDLED(MGR_CTRLREADY | INDICATION);
		PRIM_NOT_HANDLED(MGR_GLOBALOPT | REQUEST);
		default:
		printk(KERN_WARNING "%s: prim %x not handled\n", __FUNCTION__, prim);
		return(-EINVAL);
	}
	return(0);
}

static void find_type_entry(int hfc_type, int *card, int *port)
{
	int i, j = 0;

	for(i=0;i<MAX_CARDS;i++)
	{
//#warning remove
//	printk(KERN_DEBUG "i=%d type[i]=%d hfc_type=%d allocated[i]=%d\n", i, type[i]&0xff,hfc_type,allocated[i]);
		if((type[i]&0xff)==hfc_type && !allocated[i])
		{
			*card = i;
			*port = j;
			return;
		}
		j = j + (type[i]&0xff);
	}
	*card = -1;
}

static int find_idlist_entry(int vendor,int subvendor, int device, int subdevice)
{
	int cnt;

	cnt=0;
	while(id_list[cnt].vendor_id)
	{
		if(id_list[cnt].vendor_id==vendor && id_list[cnt].vendor_sub==subvendor
		   && id_list[cnt].device_id==device && id_list[cnt].device_sub==subdevice) return(cnt);
		cnt++;
	}

	return(-1);
}

static int __devinit hfcpci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int		i, ret_err=0, port_idx;
	int		bchperport, pt;
	int		ch, ch2;
	int		id_idx;        // index to id_list
	int		hfc_type;      // chip type
	hfc_multi_t	*hc;
	mISDN_pid_t	pid, pids[8];
	mISDNstack_t	*dst = NULL; /* make gcc happy */
	channel_t	*chan;
	u_long		flags;
	u_char		dips=0, pmj=0; // dip settings, port mode Jumpers


	id_idx = find_idlist_entry(ent->vendor,ent->subvendor,ent->device,ent->subdevice);
	if (id_idx == -1) {
		if (ent->vendor == CCAG_VID)
			if (ent->device == HFC4S_ID
			 || ent->device == HFC8S_ID
			 || ent->device == HFCE1_ID)
				printk( KERN_ERR "unknown HFC multiport controller (vendor:%x device:%x subvendor:%x subdevice:%x) Please contact the driver maintainer for support.\n",
					ent->vendor,ent->device,ent->subvendor,ent->subdevice);
		return (-ENODEV);
	}

	hfc_type=id_list[id_idx].type;

	find_type_entry(hfc_type, &HFC_idx, &port_idx);
	if(HFC_idx == -1) {
		printk( KERN_ERR "HFC-MULTI: Card '%s' found, but not given by module's options, ignoring...\n",
			id_list[id_idx].card_name);
		return (-ENODEV);
	}

	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: Registering chip type %d (0x%x)\n",
			__FUNCTION__, type[HFC_idx] & 0xff, type[HFC_idx]);

	/* check card type */
	switch (type[HFC_idx] & 0xff) {
		case 1:
		bchperport = 30;
		break;

		case 2:
		bchperport = 2;
		break;

		case 4:
		bchperport = 2;
		break;

		case 8:
		bchperport = 2;
		break;

		default:
		printk(KERN_ERR "Card type(%d) not supported.\n", type[HFC_idx] & 0xff);
		ret_err = -EINVAL;
		goto free_object;
	}


	/* allocate card+fifo structure */
//#warning
//void *davor=kmalloc(8, GFP_ATOMIC);
	if (!(hc = kmalloc(sizeof(hfc_multi_t), GFP_ATOMIC))) {
		printk(KERN_ERR "No kmem for HFC-Multi card\n");
		ret_err = -ENOMEM;
		goto free_object;
	}
//void *danach=kmalloc(8, GFP_ATOMIC);
	memset(hc, 0, sizeof(hfc_multi_t));
//hc->davor=davor;
//hc->danach=danach;
	hc->idx = HFC_idx;
	hc->id = HFC_idx + 1;
	hc->pcm = pcm[HFC_idx];

	/* set chip specific features */
	hc->masterclk = -1;
	hc->type = type[HFC_idx] & 0xff;
	if (type[HFC_idx] & 0x100) {
		test_and_set_bit(HFC_CHIP_ULAW, &hc->chip);
		silence = 0xff; /* ulaw silence */
	} else
		silence = 0x2a; /* alaw silence */
	if (type[HFC_idx] & 0x200)
		test_and_set_bit(HFC_CHIP_DTMF, &hc->chip);
//		if ((type[HFC_idx]&0x400) && hc->type==4)
//			test_and_set_bit(HFC_CHIP_LEDS, &hc->chip);
	if (type[HFC_idx] & 0x800)
		test_and_set_bit(HFC_CHIP_PCM_SLAVE, &hc->chip);
	if (type[HFC_idx] & 0x1000)
		test_and_set_bit(HFC_CHIP_CLOCK_IGNORE, &hc->chip);
	if (type[HFC_idx] & 0x2000)
		test_and_set_bit(HFC_CHIP_RX_SYNC, &hc->chip);
	if (type[HFC_idx] & 0x4000)
		test_and_set_bit(HFC_CHIP_EXRAM_128, &hc->chip);
	if (type[HFC_idx] & 0x8000)
		test_and_set_bit(HFC_CHIP_EXRAM_512, &hc->chip);
	hc->slots = 32;
	if (type[HFC_idx] & 0x10000)
		hc->slots = 64;
	if (type[HFC_idx] & 0x20000)
		hc->slots = 128;
	if (type[HFC_idx] & 0x40000)
		test_and_set_bit(HFC_CHIP_CRYSTAL_CLOCK, &hc->chip);
	if (type[HFC_idx] & 0x80000) {
		test_and_set_bit(HFC_CHIP_WATCHDOG, &hc->chip);
		hc->wdcount=0;
		hc->wdbyte=V_GPIO_OUT2;
		printk(KERN_NOTICE "Watchdog enabled\n");
	}
	if (hc->type == 1)
		sprintf(hc->name, "HFC-E1#%d", HFC_idx+1);
	else
		sprintf(hc->name, "HFC-%dS#%d", hc->type, HFC_idx+1);

	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: (after APPEND_TO_LIST)\n", __FUNCTION__);
	
	spin_lock_irqsave(&HFCM_obj.lock, flags);
	list_add_tail(&hc->list, &HFCM_obj.ilist);
	spin_unlock_irqrestore(&HFCM_obj.lock, flags);
	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: (after APPEND_TO_LIST)\n", __FUNCTION__);

	spin_lock_init(&hc->lock);

	pt = 0;
	while (pt < hc->type) {
		if (port_idx >= MAX_PORTS) {
			printk(KERN_ERR "Invalid HFC type.\n");
			ret_err = -EINVAL;
			goto free_channels;
		}
		if (protocol[port_idx] == 0) {
			printk(KERN_ERR "Not enough 'protocol' values given.\n");
			ret_err = -EINVAL;
			goto free_channels;
		}
		if (hc->type == 1)
			ch = 16;
		else
			ch = (pt<<2)+2;
		if (debug & DEBUG_HFCMULTI_INIT)
			printk(KERN_DEBUG "%s: Registering D-channel, card(%d) ch(%d) port(%d) protocol(%x)\n", __FUNCTION__, HFC_idx+1, ch, pt+1, protocol[port_idx]);
		hc->chan[ch].port = pt;
		hc->chan[ch].nt_timer = -1;
		chan = kmalloc(sizeof(channel_t), GFP_ATOMIC);
		if (!chan) {
			ret_err = -ENOMEM;
			goto free_channels;
		}
		memset(chan, 0, sizeof(channel_t));
		chan->channel = ch;
		//chan->debug = debug;
		chan->inst.obj = &HFCM_obj;
		chan->inst.hwlock = &hc->lock;
		chan->inst.class_dev.dev = &pdev->dev;
		mISDN_init_instance(&chan->inst, &HFCM_obj, hc, hfcmulti_l2l1);
		chan->inst.pid.layermask = ISDN_LAYER(0);
		sprintf(chan->inst.name, "HFCm%d/%d", HFC_idx+1, pt+1);
		ret_err = mISDN_initchannel(chan, MSK_INIT_DCHANNEL, MAX_DFRAME_LEN_L1);
		if (ret_err)
			goto free_channels;
		hc->chan[ch].ch = chan;

		i=0;
		while(i < bchperport) {
			if (hc->type == 1)
				ch2 = i + 1 + (i>=15);
			else
				ch2 = (pt<<2)+i;
			if (debug & DEBUG_HFCMULTI_INIT)
				printk(KERN_DEBUG "%s: Registering B-channel, card(%d) ch(%d) port(%d) channel(%d)\n", __FUNCTION__, HFC_idx+1, ch2, pt+1, i);
			hc->chan[ch2].port = pt;
			chan = kmalloc(sizeof(channel_t), GFP_ATOMIC);
			if (!chan) {
				ret_err = -ENOMEM;
				goto free_channels;
			}
			memset(chan, 0, sizeof(channel_t));
			chan->channel = ch2;
			mISDN_init_instance(&chan->inst, &HFCM_obj, hc, hfcmulti_l2l1);
			chan->inst.pid.layermask = ISDN_LAYER(0);
			chan->inst.hwlock = &hc->lock;
			chan->inst.class_dev.dev = &pdev->dev;
			//bch->debug = debug;
			sprintf(chan->inst.name, "%s B%d",
				hc->chan[ch].ch->inst.name, i+1);
			ret_err = mISDN_initchannel(chan, MSK_INIT_BCHANNEL, MAX_DATA_MEM);
			if (ret_err) {
				kfree(chan);
				goto free_channels;
			}
			hc->chan[ch2].ch = chan;
#ifdef FIXME  // TODO
			if (chan->dev) {
				chan->dev->wport.pif.func = hfcmulti_l2l1;
				chan->dev->wport.pif.fdata = chan;
			}
#endif
			i++;
		}
		chan = hc->chan[ch].ch;

		/* set D-channel */
		mISDN_set_dchannel_pid(&pid, protocol[port_idx], layermask[port_idx]);

		/* set PRI */
		if (hc->type == 1) {
			if (layermask[port_idx] & ISDN_LAYER(2)) {
				pid.protocol[2] |= ISDN_PID_L2_DF_PTP;
			}
			if (layermask[port_idx] & ISDN_LAYER(3)) {
				pid.protocol[3] |= ISDN_PID_L3_DF_PTP;
				pid.protocol[3] |= ISDN_PID_L3_DF_EXTCID;
				pid.protocol[3] |= ISDN_PID_L3_DF_CRLEN2;
			}
		}

		/* set protocol type */
		if (protocol[port_idx] & 0x10) {
			/* NT-mode */
			chan->inst.pid.protocol[0] = (hc->type==1)?ISDN_PID_L0_NT_E1:ISDN_PID_L0_NT_S0;
			chan->inst.pid.protocol[1] = (hc->type==1)?ISDN_PID_L1_NT_E1:ISDN_PID_L1_NT_S0;
			pid.protocol[0] = (hc->type==1)?ISDN_PID_L0_NT_E1:ISDN_PID_L0_NT_S0;
			pid.protocol[1] = (hc->type==1)?ISDN_PID_L1_NT_E1:ISDN_PID_L1_NT_S0;
			chan->inst.pid.layermask |= ISDN_LAYER(1);
			pid.layermask |= ISDN_LAYER(1);
			if (layermask[port_idx] & ISDN_LAYER(2))
				pid.protocol[2] = ISDN_PID_L2_LAPD_NET;
			test_and_set_bit(HFC_CFG_NTMODE, &hc->chan[ch].cfg);
		} else {
			/* TE-mode */
			chan->inst.pid.protocol[0] = (hc->type==1)?ISDN_PID_L0_TE_E1:ISDN_PID_L0_TE_S0;
			pid.protocol[0] = (hc->type==1)?ISDN_PID_L0_TE_E1:ISDN_PID_L0_TE_S0;
			if (hc->type == 1) {
				/* own E1 for E1 */
				chan->inst.pid.protocol[1] = ISDN_PID_L1_TE_E1;
				pid.protocol[1] = ISDN_PID_L1_TE_E1;
				chan->inst.pid.layermask |= ISDN_LAYER(1);
				pid.layermask |= ISDN_LAYER(1);
			}
		}


		if (hc->type != 1) {
			/* S/T */
			/* set master clock */
			if (protocol[port_idx] & 0x10000) {
				if (debug & DEBUG_HFCMULTI_INIT)
					printk(KERN_DEBUG "%s: PROTOCOL set master clock: card(%d) port(%d)\n", __FUNCTION__, HFC_idx+1, pt);
				if (test_bit(HFC_CFG_NTMODE, &hc->chan[ch].cfg)) {
					printk(KERN_ERR "Error: Master clock for port(%d) of card(%d) is only possible with TE-mode\n", pt+1, HFC_idx+1);
					ret_err = -EINVAL;
					goto free_channels;
				}
				if (hc->masterclk >= 0) {
					printk(KERN_ERR "Error: Master clock for port(%d) of card(%d) already defined for port(%d)\n", pt+1, HFC_idx+1, hc->masterclk+1);
					ret_err = -EINVAL;
					goto free_channels;
				}
				hc->masterclk = pt;
			}

			/* set transmitter line to non capacitive */
			if (protocol[port_idx] & 0x20000) {
				if (debug & DEBUG_HFCMULTI_INIT)
					printk(KERN_DEBUG "%s: PROTOCOL set non capacitive transmitter: card(%d) port(%d)\n", __FUNCTION__, HFC_idx+1, pt);
				test_and_set_bit(HFC_CFG_NONCAP_TX, &hc->chan[ch].cfg);
			}

			/* disable E-channel */
			if (protocol[port_idx] & 0x40000) {
				if (debug & DEBUG_HFCMULTI_INIT)
					printk(KERN_DEBUG "%s: PROTOCOL disable E-channel: card(%d) port(%d)\n", __FUNCTION__, HFC_idx+1, pt);
				test_and_set_bit(HFC_CFG_DIS_ECHANNEL, &hc->chan[ch].cfg);
			}
			/* register E-channel */
			if (protocol[port_idx] & 0x80000) {
				if (debug & DEBUG_HFCMULTI_INIT)
					printk(KERN_DEBUG "%s: PROTOCOL register E-channel: card(%d) port(%d)\n", __FUNCTION__, HFC_idx+1, pt);
				test_and_set_bit(HFC_CFG_REG_ECHANNEL, &hc->chan[ch].cfg);
			}
		} else {
			/* E1 */
			/* set optical line type */
			if (protocol[port_idx] & 0x10000) {
				if (debug & DEBUG_HFCMULTI_INIT)
					printk(KERN_DEBUG "%s: PROTOCOL set optical interfacs: card(%d) port(%d)\n", __FUNCTION__, HFC_idx+1, pt);
				test_and_set_bit(HFC_CFG_OPTICAL, &hc->chan[ch].cfg);
			}

			/* set LOS report */
			if (protocol[port_idx] & 0x40000) {
				if (debug & DEBUG_HFCMULTI_INIT)
					printk(KERN_DEBUG "%s: PROTOCOL set LOS report: card(%d) port(%d)\n", __FUNCTION__, HFC_idx+1, pt);
				test_and_set_bit(HFC_CFG_REPORT_LOS, &hc->chan[ch].cfg);
			}

			/* set AIS report */
			if (protocol[port_idx] & 0x80000) {
				if (debug & DEBUG_HFCMULTI_INIT)
					printk(KERN_DEBUG "%s: PROTOCOL set AIS report: card(%d) port(%d)\n", __FUNCTION__, HFC_idx+1, pt);
				test_and_set_bit(HFC_CFG_REPORT_AIS, &hc->chan[ch].cfg);
			}

			/* set SLIP report */
			if (protocol[port_idx] & 0x100000) {
				if (debug & DEBUG_HFCMULTI_INIT)
					printk(KERN_DEBUG "%s: PROTOCOL set SLIP report: card(%d) port(%d)\n", __FUNCTION__, HFC_idx+1, pt);
				test_and_set_bit(HFC_CFG_REPORT_SLIP, &hc->chan[ch].cfg);
			}

			/* set elastic jitter buffer */
			if (protocol[port_idx] & 0x600000) {
				if (debug & DEBUG_HFCMULTI_INIT)
					printk(KERN_DEBUG "%s: PROTOCOL set elastic buffer to %d: card(%d) port(%d)\n", __FUNCTION__, hc->chan[ch].jitter, HFC_idx+1, pt);
				hc->chan[ch].jitter = (protocol[port_idx]>>21) & 0x3;
			} else
				hc->chan[ch].jitter = 2; /* default */

			
			/* set CRC-4 Mode */
			if (! (protocol[port_idx] & 0x800000)) {
				if (debug & DEBUG_HFCMULTI_INIT)
					printk(KERN_DEBUG "%s: PROTOCOL turn on CRC4 report: card(%d) port(%d)\n", __FUNCTION__, HFC_idx+1, pt);
				test_and_set_bit(HFC_CFG_CRC4, &hc->chan[ch].cfg);
				
				printk(KERN_DEBUG "%s: PROTOCOL turn on CRC4 report: card(%d) port(%d)\n", __FUNCTION__, HFC_idx+1, pt);
			} else {
				printk(KERN_DEBUG "%s: PROTOCOL turn off CRC4 report: card(%d) port(%d)\n", __FUNCTION__, HFC_idx+1, pt);
			}

		}

		memcpy(&pids[pt], &pid, sizeof(pid));

		pt++;
		port_idx++;
	}

	/* run card setup */
	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: Setting up card(%d)\n", __FUNCTION__, HFC_idx+1);
	if ((ret_err = setup_pci(hc,pdev,id_idx))) {
		goto free_channels;
	}
	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: Initializing card(%d)\n", __FUNCTION__, HFC_idx+1);
	if ((ret_err = init_card(hc))) {
		if (debug & DEBUG_HFCMULTI_INIT) {
			printk(KERN_DEBUG "%s: do release_io_hfcmulti\n", __FUNCTION__);
			release_io_hfcmulti(hc);
		}
		goto free_channels;
	}

	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: Init modes card(%d)\n", __FUNCTION__, HFC_idx+1);

	hfcmulti_initmode(hc);
	
	/* check if Port Jumper config matches module param 'protocol' */
	switch(hc->type) {
		// E1
		case 1:
			break;

		// HFC 4S
		case 2:
		case 4:
			// Dip Setting: (collect GPIO 13/14/15 (R_GPIO_IN1) + GPI 19/20/23 (R_GPI_IN2))
			dips = ((HFC_inb(hc, R_GPIO_IN1) >> 5)  & 0x7) | (HFC_inb(hc, R_GPI_IN2) & 0x98);

			// Port mode (TE/NT) jumpers
			pmj = ((HFC_inb(hc, R_GPI_IN3) >> 4)  & 0xf);

			if (test_bit(HFC_CHIP_DIGICARD, &hc->chip))
				pmj = ~pmj & 0xf;

			printk(KERN_INFO "%s: DIPs(0x%x) jumpers(0x%x)\n", __FUNCTION__, dips, pmj);

			pt = 0;
			while(pt < hc->type) {
				chan = hc->chan[(pt<<2)+2].ch;
				// check for protocol param mismatch
				if (((pmj & (1 << pt)) && (chan->inst.pid.protocol[0] == ISDN_PID_L0_TE_S0)) ||
				    ((!(pmj & (1 << pt))) && (chan->inst.pid.protocol[0] == ISDN_PID_L0_NT_S0))) {
					printk ("%s: protocol WARNING: port %i is jumpered for %s mode!\n",
					        __FUNCTION__,
					        pt,
					        (pmj & (1 << pt)?"NT":"TE")
					        );
				}
				pt++;
			}
			break;

		// HFC 8S
		case 8:
			break;

		default:
			break;
	}

	/* add stacks */
	pt = 0;
	while(pt < hc->type) {
		if (debug & DEBUG_HFCMULTI_INIT)
			printk(KERN_DEBUG "%s: Adding d-stack: card(%d) port(%d)\n", __FUNCTION__, HFC_idx+1, pt+1);
		if (hc->type == 1)
			chan = hc->chan[16].ch;
		else
			chan = hc->chan[(pt<<2)+2].ch;
		if ((ret_err = mISDN_ctrl(NULL, MGR_NEWSTACK | REQUEST, &chan->inst))) {
			printk(KERN_ERR  "MGR_ADDSTACK REQUEST dch err(%d)\n", ret_err);

			int i=0;
free_release:
			/* all ports, hc is free */
			for (i=0; i<hc->type; i++)
				release_port(hc, i); 
			goto free_object;
		}
		/* indicate that this stack is created */
		hc->created[pt] = 1;

		dst = chan->inst.st;

		i = 0;
		while(i < bchperport) {
			if (debug & DEBUG_HFCMULTI_INIT)
				printk(KERN_DEBUG "%s: Adding b-stack: card(%d) port(%d) B-channel(%d)\n", __FUNCTION__, HFC_idx+1, pt+1, i+1);
			if (hc->type == 1)
				chan = hc->chan[i + 1 + (i>=15)].ch;
			else
				chan = hc->chan[(pt<<2) + i].ch;
			if ((ret_err = mISDN_ctrl(dst, MGR_NEWSTACK | REQUEST, &chan->inst))) {
				printk(KERN_ERR "MGR_ADDSTACK bchan error %d\n", ret_err);
free_delstack:
				mISDN_ctrl(dst, MGR_DELSTACK | REQUEST, NULL);
				goto free_release;
			}
			i++;
		}
		if (debug & DEBUG_HFCMULTI_INIT)
			printk(KERN_DEBUG "%s: (before MGR_SETSTACK REQUEST) layermask=0x%x\n", __FUNCTION__, pids[pt].layermask);

		if ((ret_err = mISDN_ctrl(dst, MGR_SETSTACK | REQUEST, &pids[pt]))) {
			printk(KERN_ERR "MGR_SETSTACK REQUEST dch err(%d)\n", ret_err);
			goto free_delstack;
		}
		if (debug & DEBUG_HFCMULTI_INIT)
			printk(KERN_DEBUG "%s: (after MGR_SETSTACK REQUEST)\n", __FUNCTION__);

		/* delay some time */
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout((100*HZ)/1000); /* Timeout 100ms */

		/* tell stack, that we are ready */
		mISDN_ctrl(dst, MGR_CTRLREADY | INDICATION, NULL);

		pt++;
	}

	/* now turning on irq */
	spin_lock_irqsave(&hc->lock, flags);
	enable_hwirq(hc);
	/* we are on air! */
	allocated[HFC_idx] = 1;
	HFC_cnt++;
	spin_unlock_irqrestore(&hc->lock, flags);
	return(0);

	/* if an error ocurred */
	free_channels:
	i = 0;
	while(i < 32) {
		if (hc->chan[i].ch) {
			if (debug & DEBUG_HFCMULTI_INIT)
				printk(KERN_DEBUG "%s: free %c-channel %d (1..32)\n",
					__FUNCTION__, test_bit(FLG_DCHANNEL, &hc->chan[i].ch->Flags) ?
					'D' : 'B', i);
			mISDN_freechannel(hc->chan[i].ch);
			kfree(hc->chan[i].ch);
			hc->chan[i].ch = NULL;
		}
		i++;
	}
	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: before REMOVE_FROM_LIST (refcnt = %d)\n", __FUNCTION__, HFCM_obj.refcnt);
	spin_lock_irqsave(&HFCM_obj.lock, flags);
	list_del(&hc->list);
	spin_unlock_irqrestore(&HFCM_obj.lock, flags);
	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: after REMOVE_FROM_LIST (refcnt = %d)\n", __FUNCTION__, HFCM_obj.refcnt);
	kfree(hc);

	free_object:
	return(ret_err);
}

static void __devexit hfc_remove_pci(struct pci_dev *pdev)
{
	int i,ch;
	hfc_multi_t	*card = pci_get_drvdata(pdev);

	printk( KERN_INFO "removing hfc_multi card vendor:%x device:%x subvendor:%x subdevice:%x\n",
			pdev->vendor,pdev->device,pdev->subsystem_vendor,pdev->subsystem_device);
	if (card) {

		printk( KERN_INFO "releasing card\n");
#if 1
		for(i=0;i<card->type;i++) { // type is also number of d-channel
			if(card->created[i]) {
				if (card->type == 1)
					ch = 16;
				else
					ch = (i*4)+2;
				// if created elete stack
				if (card->chan[ch].ch &&
					test_bit(FLG_DCHANNEL, &card->chan[ch].ch->Flags))
					mISDN_ctrl(card->chan[ch].ch->inst.st,
						MGR_DELSTACK | REQUEST, NULL);
			}
		}
#endif
		// relase all ports
		allocated[card->idx] = 0;
	}
	else printk(KERN_WARNING "%s: drvdata allready removed\n", __FUNCTION__);
	printk(KERN_INFO "hfcmulti card removed\n");
}

static struct pci_device_id hfmultipci_ids[] __devinitdata = {

	/** Cards with HFC-4S Chip**/
	{ CCAG_VID, 0x08B4   , CCAG_VID, 0xB566, 0, 0, 0 }, //BN2S
	{ CCAG_VID, 0x08B4   , CCAG_VID, 0xB569, 0, 0, 0 }, //BN2S mini PCI
	{ CCAG_VID, 0x08B4   , CCAG_VID, 0xB560, 0, 0, 0 }, //BN4S
	{ CCAG_VID, 0x08B4   , CCAG_VID, 0xB568, 0, 0, 0 }, //BN4S mini PCI
	{ CCAG_VID, 0x08B4   , CCAG_VID, 0x08B4, 0, 0, 0 }, //Old Eval
	{ CCAG_VID, 0x08B4   , CCAG_VID, 0xB520, 0, 0, 0 }, //IOB4ST
	{ CCAG_VID, 0x08B4   , CCAG_VID, 0xB620, 0, 0, 0 }, //4S
	{ 0xD161, 0xB410   , 0xD161, 0xB410, 0, 0, 0 }, //4S - Digium
	
	/** Cards with HFC-8S Chip**/
	{ CCAG_VID, 0x16B8   , CCAG_VID, 0xB562, 0, 0, 0 }, //BN8S
	{ CCAG_VID, 0x16B8   , CCAG_VID, 0x16B8, 0, 0, 0 }, //old Eval
	{ CCAG_VID, 0x16B8   , CCAG_VID, 0xB521, 0, 0, 0 }, //IOB8ST Recording
	{ CCAG_VID, 0x16B8   , CCAG_VID, 0xB522, 0, 0, 0 }, //IOB8ST 
	{ CCAG_VID, 0x16B8   , CCAG_VID, 0xB552, 0, 0, 0 }, //IOB8ST 
	{ CCAG_VID, 0x16B8   , CCAG_VID, 0xB622, 0, 0, 0 }, //8S


	/** Cards with HFC-E1 Chip**/
	{ CCAG_VID, 0x30B1   , CCAG_VID, 0xB563, 0, 0, 0 }, //BNE1
	{ CCAG_VID, 0x30B1   , CCAG_VID, 0xB56A, 0, 0, 0 }, //BNE1 mini PCI
	{ CCAG_VID, 0x30B1   , CCAG_VID, 0xB565, 0, 0, 0 }, //BNE1 + (Dual)
	{ CCAG_VID, 0x30B1   , CCAG_VID, 0xB564, 0, 0, 0 }, //BNE1 (Dual)

	{ CCAG_VID, 0x30B1   , CCAG_VID, 0x30B1, 0, 0, 0 }, //Old Eval
	{ CCAG_VID, 0x30B1   , CCAG_VID, 0xB523, 0, 0, 0 }, //IOB1E1
	{ CCAG_VID, 0x30B1   , CCAG_VID, 0xC523, 0, 0, 0 }, //E1
	
#if 0
	{ CCAG_VID, 0x08B4   , PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ CCAG_VID, 0x16B8   , PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ CCAG_VID, 0x30B1   , PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
#endif
	{ 0x10B5,   0x9030   , CCAG_VID,   0x3136 ,    0, 0, 0 },  // PLX PCI Bridge
	{ 0x10B5,   0x9030   , PCI_ANY_ID,   PCI_ANY_ID ,  0, 0, 0 },  // PLX PCI Bridge
	{0, }
};
MODULE_DEVICE_TABLE(pci, hfmultipci_ids);

static struct pci_driver hfcmultipci_driver = {
	name:     "hfc_multi",
	probe:    hfcpci_probe,
	remove:   __devexit_p(hfc_remove_pci),
	id_table: hfmultipci_ids,
};

static void __exit
HFCmulti_cleanup(void)
{
	hfc_multi_t *hc,*next;
	int err;

	/* unregister mISDN object */
	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: entered (refcnt = %d HFC_cnt = %d)\n", __FUNCTION__, HFCM_obj.refcnt, HFC_cnt);

	if ((err = mISDN_unregister(&HFCM_obj))) {
		printk(KERN_ERR "Can't unregister HFC-Multi cards error(%d)\n", err);
	}
	
	/* remove remaining devices, but this should never happen */
	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: now checking ilist (refcnt = %d)\n", __FUNCTION__, HFCM_obj.refcnt);

	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: done (refcnt = %d HFC_cnt = %d)\n", __FUNCTION__, HFCM_obj.refcnt, HFC_cnt);


	list_for_each_entry_safe(hc, next, &HFCM_obj.ilist, list) {
		int i;
		printk(KERN_ERR "HFC PCI card struct not empty refs %d\n", HFCM_obj.refcnt);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10)
		for (i=0;i<hc->type;i++) {
				release_port(hc, i);
		}
#endif
		release_ports_hw(hc); /* all ports, hc is free */
		udelay(1000);
	}
	
	printk(KERN_NOTICE "HFC Before unregistering from PCI\n");
	/* get rid of all devices of this driver */
	pci_unregister_driver(&hfcmultipci_driver);

	printk(KERN_NOTICE "HFC PCI card Unregistered from PCI\n");
}

static int __init
HFCmulti_init(void)
{
	int err, i;
	char tmpstr[64];

#if !defined(CONFIG_HOTPLUG) || !defined(MODULE)
#error	"CONFIG_HOTPLUG and MODULE are not defined."
#endif
	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: init entered\n", __FUNCTION__);

#ifdef __BIG_ENDIAN
#error "not running on big endian machines now"
#endif
	strcpy(tmpstr, hfcmulti_revision);
	printk(KERN_INFO "mISDN: HFC-multi driver Rev. %s\n", mISDN_getrev(tmpstr));

	switch(poll) {
		case 0:
		poll_timer = 6;
		poll = 128;
		break; /* wenn dieses break nochmal verschwindet, gibt es heisse ohren :-) */
		case 8:
		poll_timer = 2;
		break;
		case 16:
		poll_timer = 3;
		break;
		case 32:
		poll_timer = 4;
		break;
		case 64:
		poll_timer = 5;
		break;
		case 128:
		poll_timer = 6;
		break;
		case 256:
		poll_timer = 7;
		break;
		default:
		printk(KERN_ERR "%s: Wrong poll value (%d).\n", __FUNCTION__, poll);
		err = -EINVAL;
		return(err);

	}

	memset(&HFCM_obj, 0, sizeof(HFCM_obj));
#ifdef MODULE
	HFCM_obj.owner = THIS_MODULE;
#endif
	spin_lock_init(&HFCM_obj.lock);
	INIT_LIST_HEAD(&HFCM_obj.ilist);
	HFCM_obj.name = HFCName;
	HFCM_obj.own_ctrl = HFC_manager;
	HFCM_obj.DPROTO.protocol[0] = ISDN_PID_L0_TE_S0 | ISDN_PID_L0_NT_S0
				| ISDN_PID_L0_TE_E1 | ISDN_PID_L0_NT_E1;
	HFCM_obj.DPROTO.protocol[1] = ISDN_PID_L1_NT_S0
				| ISDN_PID_L1_TE_E1 | ISDN_PID_L1_NT_E1;
	HFCM_obj.BPROTO.protocol[1] = ISDN_PID_L1_B_64TRANS | ISDN_PID_L1_B_64HDLC;
	HFCM_obj.BPROTO.protocol[2] = ISDN_PID_L2_B_TRANS | ISDN_PID_L2_B_RAWDEV;

	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: registering HFCM_obj\n", __FUNCTION__);
	if ((err = mISDN_register(&HFCM_obj))) {
		printk(KERN_ERR "Can't register HFC-Multi cards error(%d)\n", err);
		return(err);
	}
	if (debug & DEBUG_HFCMULTI_INIT)
		printk(KERN_DEBUG "%s: new mISDN object (refcnt = %d)\n", __FUNCTION__, HFCM_obj.refcnt);

	for(i=0;i<MAX_CARDS;i++) allocated[i]=0;
	HFC_cnt = HFC_idx = 0;

#if 1
	err = pci_register_driver(&hfcmultipci_driver);
	if (err < 0)
	{
		printk(KERN_ERR "error registering pci driver:%x\n",err);
		HFCmulti_cleanup();
		return(err);
	}
#endif
	printk(KERN_INFO "%d devices registered\n", HFC_cnt);

	return(0);
}


#ifdef MODULE
module_init(HFCmulti_init);
module_exit(HFCmulti_cleanup);
#endif


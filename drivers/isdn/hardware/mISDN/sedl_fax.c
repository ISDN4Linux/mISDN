/* $Id: sedl_fax.c,v 1.30 2007/02/13 10:43:45 crich Exp $
 *
 * sedl_fax.c  low level stuff for Sedlbauer Speedfax + cards
 *
 * Copyright  (C) 2000,2001 Karsten Keil (kkeil@suse.de)
 *
 * Author     Karsten Keil (kkeil@suse.de)
 *
 *
 * Thanks to  Sedlbauer AG for informations
 *            Marcus Niemann
 *            Edgar Toernig
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

/* Supported cards:
 * Card:	Chip:		Configuration:	Comment:
 * ---------------------------------------------------------------------
 * Speed Fax+	ISAC_ISAR	ISAPNP		Full analog support
 * Speed Fax+ 	ISAC_ISAR	PCI PNP		Full analog support
 *
 * Important:
 * For the sedlbauer speed fax+ to work properly you have to download 
 * the firmware onto the card.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <asm/semaphore.h>
#ifdef NEW_ISAPNP
#include <linux/pnp.h>
#else
#include <linux/isapnp.h>
#endif
#include "core.h"
#include "channel.h"
#include "isac.h"
#include "isar.h"
#include "layer1.h"
#include "helper.h"
#include "debug.h"

extern const char *CardType[];

const char *Sedlfax_revision = "$Revision: 1.30 $";

const char *Sedlbauer_Types[] =
	{"None", "speed fax+", "speed fax+ pyramid", "speed fax+ pci"};

#ifndef PCI_VENDOR_ID_TIGERJET
#define PCI_VENDOR_ID_TIGERJET		0xe159
#endif
#ifndef PCI_DEVICE_ID_TIGERJET_100
#define PCI_DEVICE_ID_TIGERJET_100	0x0002
#endif
#define PCI_SUBVENDOR_SPEEDFAX_PYRAMID	0x51
#define PCI_SUBVENDOR_SPEEDFAX_PCI	0x54
#define PCI_SUB_ID_SEDLBAUER		0x01
 
#define SEDL_SPEEDFAX_ISA	1
#define SEDL_SPEEDFAX_PYRAMID	2
#define SEDL_SPEEDFAX_PCI	3

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

#define SEDL_ISA_ISAC	4
#define SEDL_ISA_ISAR	6
#define SEDL_ISA_ADR	8
#define SEDL_ISA_RESET_ON	10
#define SEDL_ISA_RESET_OFF	12

#define SEDL_PCI_ADR	0xc8
#define SEDL_PCI_ISAC	0xd0
#define SEDL_PCI_ISAR	0xe0

/* TIGER 100 Registers */

#define TIGER_RESET_ADDR	0x00
#define TIGER_EXTERN_RESET_ON	0x01
#define TIGER_EXTERN_RESET_OFF	0x00
#define TIGER_AUX_CTRL		0x02
#define TIGER_AUX_DATA		0x03
#define TIGER_AUX_IRQMASK	0x05
#define TIGER_AUX_STATUS	0x07

/* Tiger AUX BITs */
#define SEDL_AUX_IOMASK		0xdd	/* 1 and 5 are inputs */
#define SEDL_ISAR_RESET_BIT_OFF 0x00
#define SEDL_ISAR_RESET_BIT_ON	0x01
#define SEDL_TIGER_IRQ_BIT	0x02
#define SEDL_ISAR_PCI_LED1_BIT	0x08
#define SEDL_ISAR_PCI_LED2_BIT	0x10

#define SEDL_PCI_RESET_ON	(SEDL_ISAR_RESET_BIT_ON)
#define SEDL_PCI_RESET_OFF	(SEDL_ISAR_PCI_LED1_BIT | SEDL_ISAR_PCI_LED2_BIT)


#define SEDL_RESET      0x3	/* same as DOS driver */

/* data struct */

typedef struct _sedl_fax {
	struct list_head	list;
	union {
#if defined(CONFIG_PNP)
#ifdef NEW_ISAPNP
		struct pnp_dev		*pnp;
#else
		struct pci_dev		*pnp;
#endif
#endif
		struct pci_dev		*pci;
	}			dev;
	u_int			subtyp;
	u_int			irq;
	u_int			irqcnt;
	u_int			cfg;
	u_int			addr;
	u_int			isac;
	u_int			isar;
	spinlock_t		lock;
	isar_reg_t		ir;
	isac_chip_t		isac_hw;
	isar_hw_t		isar_hw[2];
	channel_t		dch;
	channel_t		bch[2];
} sedl_fax;

static inline u_char
readreg(unsigned int ale, unsigned int adr, u_char off)
{
	byteout(ale, off);
	return (bytein(adr));
}

static inline void
readfifo(unsigned int ale, unsigned int adr, u_char off, u_char * data, int size)
{
	byteout(ale, off);
	insb(adr, data, size);
}


static inline void
writereg(unsigned int ale, unsigned int adr, u_char off, u_char data)
{
	byteout(ale, off);
	byteout(adr, data);
}

static inline void
writefifo(unsigned int ale, unsigned int adr, u_char off, u_char * data, int size)
{
	byteout(ale, off);
	outsb(adr, data, size);
}

/* Interface functions */

static u_char
ReadISAC(void *p, u_char offset)
{
	return (readreg(((sedl_fax *)p)->addr, ((sedl_fax *)p)->isac, offset));
}

static void
WriteISAC(void *p, u_char offset, u_char value)
{
	writereg(((sedl_fax *)p)->addr, ((sedl_fax *)p)->isac, offset, value);
}

static void
ReadISACfifo(void *p, u_char * data, int size)
{
	readfifo(((sedl_fax *)p)->addr, ((sedl_fax *)p)->isac, 0, data, size);
}

static void
WriteISACfifo(void *p, u_char * data, int size)
{
	writefifo(((sedl_fax *)p)->addr, ((sedl_fax *)p)->isac, 0, data, size);
}

/* ISAR access routines
 * mode = 0 access with IRQ on
 * mode = 1 access with IRQ off
 * mode = 2 access with IRQ off and using last offset
 */
  
static u_char
ReadISAR(void *p, u_char offset)
{	
	return (readreg(((sedl_fax *)p)->addr, ((sedl_fax *)p)->isar, offset));
}

static void
WriteISAR(void *p, u_char offset, u_char value)
{
	writereg(((sedl_fax *)p)->addr, ((sedl_fax *)p)->isar, offset, value);
}

static void
ReadISARfifo(void *p, u_char * data, int size)
{
	readfifo(((sedl_fax *)p)->addr, ((sedl_fax *)p)->isar, ISAR_MBOX, data, size);
}

static void
WriteISARfifo(void *p, u_char * data, int size)
{
	writefifo(((sedl_fax *)p)->addr, ((sedl_fax *)p)->isar, ISAR_MBOX, data, size);
}

inline void
do_sedl_interrupt(sedl_fax *sf)
{
	u_char val;
	int cnt = 8;

	val = readreg(sf->addr, sf->isar, ISAR_IRQBIT);
      Start_ISAR:
	if (val & ISAR_IRQSTA)
		isar_int_main(&sf->bch[0]);
	val = readreg(sf->addr, sf->isac, ISAC_ISTA);
      Start_ISAC:
	if (val)
		mISDN_isac_interrupt(&sf->dch, val);
	val = readreg(sf->addr, sf->isar, ISAR_IRQBIT);
	if ((val & ISAR_IRQSTA) && cnt) {
		cnt--;
		if (sf->dch.debug & L1_DEB_HSCX)
			printk(KERN_DEBUG "ISAR IntStat after IntRoutine cpu%d\n",
				smp_processor_id());
		goto Start_ISAR;
	}
	val = readreg(sf->addr, sf->isac, ISAC_ISTA);
	if (val && cnt) {
		cnt--;
		if (sf->dch.debug & L1_DEB_ISAC)
			printk(KERN_DEBUG "ISAC IntStat after IntRoutine cpu%d\n",
				smp_processor_id());
		goto Start_ISAC;
	}
	if (!cnt)
		if (sf->dch.debug & L1_DEB_ISAC)
			printk(KERN_DEBUG "Sedlbauer IRQ LOOP\n");
	writereg(sf->addr, sf->isar, ISAR_IRQBIT, 0);
	writereg(sf->addr, sf->isac, ISAC_MASK, 0xFF);
	writereg(sf->addr, sf->isac, ISAC_MASK, 0x0);
	writereg(sf->addr, sf->isar, ISAR_IRQBIT, ISAR_IRQMSK);
}

static irqreturn_t
speedfax_isa_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	sedl_fax	*sf = dev_id;
	
	spin_lock(&sf->lock);
	sf->irqcnt++;
	do_sedl_interrupt(sf);
	spin_unlock(&sf->lock);
	return IRQ_HANDLED;
}

static irqreturn_t
speedfax_pci_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	sedl_fax	*sf = dev_id;
	u_char		val;

	spin_lock(&sf->lock);
	val = bytein(sf->cfg + TIGER_AUX_STATUS);
	if (val & SEDL_TIGER_IRQ_BIT) { /* for us or shared ? */
		spin_unlock(&sf->lock);
		return IRQ_NONE; /* shared */
	}
	sf->irqcnt++;
	do_sedl_interrupt(sf);
	spin_unlock(&sf->lock);
	return IRQ_HANDLED;
}

static void
enable_hwirq(sedl_fax *sf)
{
	WriteISAC(sf, ISAC_MASK, 0);
	WriteISAR(sf, ISAR_IRQBIT, ISAR_IRQMSK);
	if (sf->subtyp != SEDL_SPEEDFAX_ISA)
		byteout(sf->cfg + TIGER_AUX_IRQMASK, SEDL_TIGER_IRQ_BIT);
}

static void
disable_hwirq(sedl_fax *sf)
{
	WriteISAC(sf, ISAC_MASK, 0xFF);
	WriteISAR(sf, ISAR_IRQBIT, 0);
	if (sf->subtyp != SEDL_SPEEDFAX_ISA)
		byteout(sf->cfg + TIGER_AUX_IRQMASK, 0);
}

void
release_sedlbauer(sedl_fax *sf)
{
	int bytecnt = 256;

	if (sf->subtyp == SEDL_SPEEDFAX_ISA)
		bytecnt = 16;
	if (sf->cfg)
		release_region(sf->cfg, bytecnt);
}

static void
reset_speedfax(sedl_fax *sf)
{

	printk(KERN_INFO "Sedlbauer: resetting card\n");

	if (sf->subtyp == SEDL_SPEEDFAX_ISA) {
		byteout(sf->cfg + SEDL_ISA_RESET_ON, SEDL_RESET);
		mdelay(1);
		byteout(sf->cfg + SEDL_ISA_RESET_OFF, 0);
		mdelay(1);
	} else {
		byteout(sf->cfg + TIGER_RESET_ADDR, TIGER_EXTERN_RESET_ON);
		byteout(sf->cfg + TIGER_AUX_DATA, SEDL_PCI_RESET_ON);
		mdelay(1);
		byteout(sf->cfg + TIGER_RESET_ADDR, TIGER_EXTERN_RESET_OFF);
		byteout(sf->cfg + TIGER_AUX_DATA, SEDL_PCI_RESET_OFF);
		mdelay(1);
	}
}

static int init_card(sedl_fax *sf)
{
	int	cnt = 3;
	u_long	flags;
	u_int	shared = SA_SHIRQ;
	void	*irq_func = speedfax_pci_interrupt;

	if (sf->subtyp == SEDL_SPEEDFAX_ISA) {
		irq_func = speedfax_isa_interrupt;
		shared = 0;
	}
	if (request_irq(sf->irq, irq_func, shared, "speedfax", sf)) {
		printk(KERN_WARNING "mISDN: couldn't get interrupt %d\n",
			sf->irq);
		return(-EIO);
	}
	spin_lock_irqsave(&sf->lock, flags);
	while (cnt) {
		int	ret;

		mISDN_clear_isac(&sf->dch);
		if ((ret=mISDN_isac_init(&sf->dch))) {
			printk(KERN_WARNING "mISDN: mISDN_isac_init failed with %d\n", ret);
			break;
		}
		init_isar(&sf->bch[0]);
		init_isar(&sf->bch[1]);
		enable_hwirq(sf);
		/* RESET Receiver and Transmitter */
		WriteISAC(sf, ISAC_CMDR, 0x41);
		spin_unlock_irqrestore(&sf->lock, flags);
		current->state = TASK_UNINTERRUPTIBLE;
		/* Timeout 10ms */
		schedule_timeout((10*HZ)/1000);
		printk(KERN_INFO "%s: IRQ %d count %d\n",
			sf->dch.inst.name, sf->irq, sf->irqcnt);
		if (!sf->irqcnt) {
			printk(KERN_WARNING
			       "Sedlbauer speedfax: IRQ(%d) getting no interrupts during init %d\n",
			       sf->irq, 4 - cnt);
			if (cnt == 1) {
				return (-EIO);
			} else {
				spin_lock_irqsave(&sf->lock, flags);
				reset_speedfax(sf);
				cnt--;
			}
		} else {
			return(0);
		}
	}
	spin_unlock_irqrestore(&sf->lock, flags);
	return(-EIO);
}


#define MAX_CARDS	4
static int sedl_cnt;
static mISDNobject_t	speedfax;
static uint debug;
static uint protocol[MAX_CARDS];
static uint layermask[MAX_CARDS];

#ifdef MODULE
MODULE_AUTHOR("Karsten Keil");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
#ifdef OLD_MODULE_PARAM
MODULE_PARM(debug, "1i");
#define MODULE_PARM_T   "1-4i"
MODULE_PARM(protocol, MODULE_PARM_T);
MODULE_PARM(layermask, MODULE_PARM_T);
#else
module_param (debug, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC (debug, "sedlfax debug mask");
static uint protocol_num;
static uint layermask_num;
#ifdef OLD_MODULE_PARAM_ARRAY
module_param_array(protocol, uint, protocol_num, S_IRUGO | S_IWUSR);
module_param_array(layermask, uint, layermask_num, S_IRUGO | S_IWUSR);
#else
module_param_array(protocol, uint, &protocol_num, S_IRUGO | S_IWUSR);
module_param_array(layermask, uint, &layermask_num, S_IRUGO | S_IWUSR);
#endif
MODULE_PARM_DESC (protocol, "sedlfax protcol (DSS1 := 2)");
MODULE_PARM_DESC(layermask, "sedlfax layer mask");
#endif
#endif
static char SpeedfaxName[] = "Speedfax";

int
setup_speedfax(sedl_fax *sf)
{
	int	bytecnt, ver;
	u_long	flags;

	bytecnt = (sf->subtyp == SEDL_SPEEDFAX_ISA) ? 16 : 256;
	if (!request_region(sf->cfg, bytecnt, (sf->subtyp == SEDL_SPEEDFAX_ISA) ? "sedl PnP" : "sedl PCI")) {
		printk(KERN_WARNING
		       "mISDN: %s config port %x-%x already in use\n",
		       "Speedfax +",
		       sf->cfg,
		       sf->cfg + bytecnt - 1);
		return(-EIO);
	}
	sf->dch.read_reg = &ReadISAC;
	sf->dch.write_reg = &WriteISAC;
	sf->dch.read_fifo = &ReadISACfifo;
	sf->dch.write_fifo = &WriteISACfifo;
	sf->dch.hw = &sf->isac_hw;
	if (sf->subtyp != SEDL_SPEEDFAX_ISA) {
		sf->addr = sf->cfg + SEDL_PCI_ADR;
		sf->isac = sf->cfg + SEDL_PCI_ISAC;
		sf->isar = sf->cfg + SEDL_PCI_ISAR;
		byteout(sf->cfg + TIGER_RESET_ADDR, 0xff);
		mdelay(1);
		byteout(sf->cfg + TIGER_RESET_ADDR, 0x00);
		mdelay(1);
		byteout(sf->cfg + TIGER_AUX_CTRL, SEDL_AUX_IOMASK);
		byteout(sf->cfg + TIGER_AUX_IRQMASK, 0);
		byteout(sf->cfg + TIGER_AUX_DATA, SEDL_PCI_RESET_ON);
		mdelay(1);
		byteout(sf->cfg + TIGER_AUX_DATA, SEDL_PCI_RESET_OFF);
		mdelay(1);
	} else {
		sf->addr = sf->cfg + SEDL_ISA_ADR;
		sf->isac = sf->cfg + SEDL_ISA_ISAC;
		sf->isar = sf->cfg + SEDL_ISA_ISAR;
	}
	sf->isar_hw[0].reg = &sf->ir;
	sf->isar_hw[1].reg = &sf->ir;
	sf->bch[0].hw = &sf->isar_hw[0];
	sf->bch[1].hw = &sf->isar_hw[1];
	sf->bch[0].read_reg = &ReadISAR;
	sf->bch[0].write_reg = &WriteISAR;
	sf->bch[0].read_fifo = &ReadISARfifo;
	sf->bch[0].write_fifo = &WriteISARfifo;
	sf->bch[1].read_reg = &ReadISAR;
	sf->bch[1].write_reg = &WriteISAR;
	sf->bch[1].read_fifo = &ReadISARfifo;
	sf->bch[1].write_fifo = &WriteISARfifo;
	spin_lock_irqsave(&sf->lock, flags);
	disable_hwirq(sf);
	ver = ISARVersion(&sf->bch[0], "Sedlbauer:");
	spin_unlock_irqrestore(&sf->lock, flags);
	if (ver < 0) {
		printk(KERN_WARNING
			"Sedlbauer: wrong ISAR version (ret = %d)\n", ver);
		release_sedlbauer(sf);
		return (-EIO);
	}
	return (0);
}

static void
release_card(sedl_fax *card) {
	u_long	flags;

	spin_lock_irqsave(&card->lock, flags);	
	disable_hwirq(card);
	spin_unlock_irqrestore(&card->lock, flags);
	free_irq(card->irq, card);
	spin_lock_irqsave(&card->lock, flags);
	free_isar(&card->bch[1]);
	free_isar(&card->bch[0]);
	mISDN_isac_free(&card->dch);
	release_sedlbauer(card);
	mISDN_freechannel(&card->bch[1]);
	mISDN_freechannel(&card->bch[0]);
	mISDN_freechannel(&card->dch);
	spin_unlock_irqrestore(&card->lock, flags);
	mISDN_ctrl(&card->dch.inst, MGR_UNREGLAYER | REQUEST, NULL);
	spin_lock_irqsave(&speedfax.lock, flags);
	list_del(&card->list);
	spin_unlock_irqrestore(&speedfax.lock, flags);
	if (card->subtyp == SEDL_SPEEDFAX_ISA) {
#if defined(CONFIG_PNP)
		pnp_disable_dev(card->dev.pnp);
		pnp_set_drvdata(card->dev.pnp, NULL);
#endif
	} else {
		pci_disable_device(card->dev.pci);
		pci_set_drvdata(card->dev.pci, NULL);
	}
	kfree(card);
	sedl_cnt--;
}

static int
speedfax_manager(void *data, u_int prim, void *arg) {
	sedl_fax	*card;
	mISDNinstance_t	*inst=data;
	int		channel = -1;
	struct sk_buff	*skb;
	u_long		flags;

	if (debug & MISDN_DEBUG_MANAGER)
		printk(KERN_DEBUG "%s: data:%p prim:%x arg:%p\n",
			__FUNCTION__, data, prim, arg);
	if (!data) {
		MGR_HASPROTOCOL_HANDLER(prim,arg,&speedfax)
		printk(KERN_ERR "speedfax_manager no data prim %x arg %p\n",
			prim, arg);
		return(-EINVAL);
	}
	spin_lock_irqsave(&speedfax.lock, flags);
	list_for_each_entry(card, &speedfax.ilist, list) {
		if (&card->dch.inst == inst) {
			channel = 2;
			break;
		}
		if (&card->bch[0].inst == inst) {
			channel = 0;
			break;
		}
		if (&card->bch[1].inst == inst) {
			channel = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&speedfax.lock, flags);
	if (channel<0) {
		printk(KERN_ERR "speedfax_manager no channel data %p prim %x arg %p\n",
			data, prim, arg);
		return(-EINVAL);
	}
	if (debug & MISDN_DEBUG_MANAGER)
		printk(KERN_DEBUG "%s: channel %d\n", __FUNCTION__, channel);
	switch(prim) {
	    case MGR_REGLAYER | CONFIRM:
		if (channel == 2)
			mISDN_setpara(&card->dch, &inst->st->para);
		else
			mISDN_setpara(&card->bch[channel], &inst->st->para);
		break;
	    case MGR_UNREGLAYER | REQUEST:
		if ((skb = create_link_skb(PH_CONTROL | REQUEST,
			HW_DEACTIVATE, 0, NULL, 0))) {
			if (channel == 2) {
				if (mISDN_ISAC_l1hw(inst, skb))
					dev_kfree_skb(skb);
			} else {
				if (isar_down(inst, skb))
					dev_kfree_skb(skb);
			}
		} else
			printk(KERN_WARNING "no SKB in %s MGR_UNREGLAYER | REQUEST\n", __FUNCTION__);
		mISDN_ctrl(inst, MGR_UNREGLAYER | REQUEST, NULL);
		break;
	    case MGR_CLRSTPARA | INDICATION:
		arg = NULL;
	    case MGR_ADDSTPARA | INDICATION:
		if (channel == 2)
			mISDN_setpara(&card->dch, arg);
		else
			mISDN_setpara(&card->bch[channel], arg);
		break;
	    case MGR_RELEASE | INDICATION:
		if (channel == 2) {
			release_card(card);
		} else {
			speedfax.refcnt--;
		}
		break;
#ifdef OBSOLETE
	    case MGR_CONNECT | REQUEST:
		return(mISDN_ConnectIF(inst, arg));
	    case MGR_SETIF | REQUEST:
	    case MGR_SETIF | INDICATION:
		if (channel==2)
			return(mISDN_SetIF(inst, arg, prim, mISDN_ISAC_l1hw, NULL, &card->dch));
		else
			return(mISDN_SetIF(inst, arg, prim, isar_down, NULL, &card->bch[channel]));
	    case MGR_DISCONNECT | REQUEST:
	    case MGR_DISCONNECT | INDICATION:
		return(mISDN_DisConnectIF(inst, arg));
#endif
	    case MGR_LOADFIRM | REQUEST:
	    	{
			struct firm {
				int	len;
				void	*data;
			} *firm = arg;
			
			if (!arg)
				return(-EINVAL);
			return(isar_load_firmware(&card->bch[0], firm->data, firm->len));
		}
	    case MGR_LOADFIRM | CONFIRM:
		mISDN_ctrl(card->dch.inst.st, MGR_CTRLREADY | INDICATION, NULL);
		break;
	    case MGR_SETSTACK | INDICATION:
		if ((channel!=2) && (inst->pid.global == 2)) {
//			inst->down.fdata = &card->bch[channel];
			if ((skb = create_link_skb(PH_ACTIVATE | REQUEST,
				0, 0, NULL, 0))) {
				if (isar_down(inst, skb))
					dev_kfree_skb(skb);
			}
			if ((inst->pid.protocol[2] == ISDN_PID_L2_B_TRANS) ||
				(inst->pid.protocol[2] == ISDN_PID_L2_B_TRANSDTMF))
				mISDN_queue_data(inst, FLG_MSG_UP, DL_ESTABLISH | INDICATION,
					0, 0, NULL, 0);
			else
				mISDN_queue_data(inst, FLG_MSG_UP, PH_ACTIVATE | INDICATION,
					0, 0, NULL, 0);
		}
		break;
	    PRIM_NOT_HANDLED(MGR_CTRLREADY | INDICATION);
	    PRIM_NOT_HANDLED(MGR_GLOBALOPT | REQUEST);
	    default:
		printk(KERN_WARNING "speedfax_manager prim %x not handled\n", prim);
		return(-EINVAL);
	}
	return(0);
}

static int __devinit setup_instance(sedl_fax *card)
{
	int		i, err;
	mISDN_pid_t	pid;
	struct device	*dev;
	u_long		flags;
	
	if (sedl_cnt >= MAX_CARDS) {
		kfree(card);
		return(-EINVAL);
	}
	if (card->subtyp == SEDL_SPEEDFAX_ISA) {
#if defined(CONFIG_PNP)
		dev = &card->dev.pnp->dev;
#else
		dev = NULL;
#endif
	} else {
		dev = &card->dev.pci->dev;
	}
	spin_lock_irqsave(&speedfax.lock, flags);
	list_add_tail(&card->list, &speedfax.ilist);
	spin_unlock_irqrestore(&speedfax.lock, flags);
	card->dch.debug = debug;
	spin_lock_init(&card->lock);
	card->dch.inst.hwlock = &card->lock;
	card->dch.inst.pid.layermask = ISDN_LAYER(0);
	card->dch.inst.pid.protocol[0] = ISDN_PID_L0_TE_S0;
	card->dch.inst.class_dev.dev = dev;
	mISDN_init_instance(&card->dch.inst, &speedfax, card, mISDN_ISAC_l1hw);
	sprintf(card->dch.inst.name, "SpeedFax%d", sedl_cnt+1);
	mISDN_set_dchannel_pid(&pid, protocol[sedl_cnt], layermask[sedl_cnt]);
	mISDN_initchannel(&card->dch, MSK_INIT_DCHANNEL, MAX_DFRAME_LEN_L1);
	for (i=0; i<2; i++) {
		card->bch[i].channel = i;
		mISDN_init_instance(&card->bch[i].inst, &speedfax, card, isar_down);
		card->bch[i].inst.pid.layermask = ISDN_LAYER(0);
		card->bch[i].inst.hwlock = &card->lock;
		card->bch[i].debug = debug;
		card->bch[i].inst.class_dev.dev = dev;
		sprintf(card->bch[i].inst.name, "%s B%d", card->dch.inst.name, i+1);
		mISDN_initchannel(&card->bch[i], MSK_INIT_BCHANNEL, MAX_DATA_MEM);
	}
	printk(KERN_DEBUG "sfax card %p dch %p bch1 %p bch2 %p\n",
		card, &card->dch, &card->bch[0], &card->bch[1]);
	err = setup_speedfax(card);
	if (err) {
		mISDN_freechannel(&card->dch);
		mISDN_freechannel(&card->bch[1]);
		mISDN_freechannel(&card->bch[0]);
		spin_lock_irqsave(&speedfax.lock, flags);
		list_del(&card->list);
		spin_unlock_irqrestore(&speedfax.lock, flags);
		kfree(card);
		return(err);
	}
	sedl_cnt++;
	err = mISDN_ctrl(NULL, MGR_NEWSTACK | REQUEST, &card->dch.inst);
	if (err) {
		release_card(card);
		return(err);
	}
	mISDN_ctrl(card->dch.inst.st, MGR_STOPSTACK | REQUEST, NULL);
	for (i=0; i<2; i++) {
		err = mISDN_ctrl(card->dch.inst.st, MGR_NEWSTACK | REQUEST, &card->bch[i].inst);
		if (err) {
			printk(KERN_ERR "MGR_ADDSTACK bchan error %d\n", err);
			mISDN_ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
			return(err);
		}
	}
	err = mISDN_ctrl(card->dch.inst.st, MGR_SETSTACK | REQUEST, &pid);
	if (err) {
		printk(KERN_ERR  "MGR_SETSTACK REQUEST dch err(%d)\n", err);
		mISDN_ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
		return(err);
	}
	err = init_card(card);
	if (err) {
		mISDN_ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
		return(err);
	}
	mISDN_ctrl(card->dch.inst.st, MGR_STARTSTACK | REQUEST, NULL);
	printk(KERN_INFO "SpeedFax %d cards installed\n", sedl_cnt);
	return(0);
}

static int __devinit sedlpci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int		err = -ENOMEM;
	sedl_fax	*card;

	if (!(card = kmalloc(sizeof(sedl_fax), GFP_ATOMIC))) {
		printk(KERN_ERR "No kmem for Speedfax + PCI\n");
		return(err);
	}
	memset(card, 0, sizeof(sedl_fax));
	card->dev.pci = pdev;
	if (PCI_SUBVENDOR_SPEEDFAX_PYRAMID == pdev->subsystem_vendor)
		card->subtyp = SEDL_SPEEDFAX_PYRAMID;
	else
		card->subtyp = SEDL_SPEEDFAX_PCI;
	err = pci_enable_device(pdev);
	if (err) {
		kfree(card);
		return(err);
	}

	printk(KERN_INFO "mISDN: sedlpci found adapter %s at %s\n",
	       (char *) ent->driver_data, pci_name(pdev));

	card->cfg = pci_resource_start(pdev, 0);
	card->irq = pdev->irq;
	pci_set_drvdata(pdev, card);
	err = setup_instance(card);
	if (err)
		pci_set_drvdata(pdev, NULL);
	return(err);
}

#if defined(CONFIG_PNP)
#ifdef NEW_ISAPNP
static int __devinit sedlpnp_probe(struct pnp_dev *pdev, const struct pnp_device_id *dev_id)
#else
static int __devinit sedlpnp_probe(struct pci_dev *pdev, const struct isapnp_device_id *dev_id)
#endif
{
	int		err;
	sedl_fax	*card;

	if (!pdev)
		return(-ENODEV);

	if (!(card = kmalloc(sizeof(sedl_fax), GFP_ATOMIC))) {
		printk(KERN_ERR "No kmem for Speedfax + PnP\n");
		return(-ENOMEM);
	}
	memset(card, 0, sizeof(sedl_fax));
	card->subtyp = SEDL_SPEEDFAX_ISA;
	card->dev.pnp = pdev;
	pnp_disable_dev(pdev);
	err = pnp_activate_dev(pdev);
	if (err<0) {
		printk(KERN_WARNING "%s: pnp_activate_dev(%s) ret(%d)\n", __FUNCTION__,
			(char *)dev_id->driver_data, err);
		kfree(card);
		return(err);
	}
	card->cfg = pnp_port_start(pdev, 0);
	card->irq = pnp_irq(pdev, 0);

	printk(KERN_INFO "mISDN: sedlpnp_probe found adapter %s at IO %#x irq %d\n",
	       (char *)dev_id->driver_data, card->addr, card->irq);

	pnp_set_drvdata(pdev, card);
	err = setup_instance(card);
	if (err)
		pnp_set_drvdata(pdev, NULL);
	return(err);
}
#endif /* CONFIG_PNP */

static void __devexit sedl_remove_pci(struct pci_dev *pdev)
{
	sedl_fax	*card = pci_get_drvdata(pdev);

	if (card)
		mISDN_ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
	else
		if (debug)
			printk(KERN_WARNING "%s: drvdata allready removed\n", __FUNCTION__);
}

#if defined(CONFIG_PNP)
#ifdef NEW_ISAPNP
static void __devexit sedl_remove_pnp(struct pnp_dev *pdev)
#else
static void __devexit sedl_remove_pnp(struct pci_dev *pdev)
#endif
{
	sedl_fax	*card = pnp_get_drvdata(pdev);

	if (card)
		mISDN_ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
	else
		if (debug)
			printk(KERN_WARNING "%s: drvdata allready removed\n", __FUNCTION__);
}
#endif

static struct pci_device_id sedlpci_ids[] __devinitdata = {
	{ PCI_VENDOR_ID_TIGERJET, PCI_DEVICE_ID_TIGERJET_100, PCI_SUBVENDOR_SPEEDFAX_PYRAMID, PCI_SUB_ID_SEDLBAUER,
	  0, 0, (unsigned long) "Pyramid Speedfax + PCI" },
	{ PCI_VENDOR_ID_TIGERJET, PCI_DEVICE_ID_TIGERJET_100, PCI_SUBVENDOR_SPEEDFAX_PCI, PCI_SUB_ID_SEDLBAUER,
	  0, 0, (unsigned long) "Sedlbauer Speedfax + PCI" },
	{ }
};
MODULE_DEVICE_TABLE(pci, sedlpci_ids);

static struct pci_driver sedlpci_driver = {
	name:     "speedfax pci",
	probe:    sedlpci_probe,
	remove:   __devexit_p(sedl_remove_pci),
	id_table: sedlpci_ids,
};

#if defined(CONFIG_PNP)
#ifdef NEW_ISAPNP
static struct pnp_device_id sedlpnp_ids[] __devinitdata = {
	{ 
		.id		= "SAG0002",
		.driver_data	= (unsigned long) "Speedfax + PnP",
	},
};

static struct pnp_driver sedlpnp_driver = {
#else
static struct isapnp_device_id sedlpnp_ids[] __devinitdata = {
	{ ISAPNP_VENDOR('S', 'A', 'G'), ISAPNP_FUNCTION(0x02),
	  ISAPNP_VENDOR('S', 'A', 'G'), ISAPNP_FUNCTION(0x02), 
	  (unsigned long) "Speedfax + PnP" },
	{ }
};
MODULE_DEVICE_TABLE(isapnp, sedlpnp_ids);

static struct isapnp_driver sedlpnp_driver = {
#endif
	name:     "speedfax pnp",
	probe:    sedlpnp_probe,
	remove:   __devexit_p(sedl_remove_pnp),
	id_table: sedlpnp_ids,
};
#endif /* CONFIG_PNP */

static int __init Speedfax_init(void)
{
	int	err;
#ifdef OLD_PCI_REGISTER_DRIVER
	int	pci_nr_found;
#endif

#ifdef MODULE
	speedfax.owner = THIS_MODULE;
#endif
	spin_lock_init(&speedfax.lock);
	INIT_LIST_HEAD(&speedfax.ilist);
	speedfax.name = SpeedfaxName;
	speedfax.own_ctrl = speedfax_manager;
	speedfax.DPROTO.protocol[0] = ISDN_PID_L0_TE_S0;
	speedfax.BPROTO.protocol[1] = ISDN_PID_L1_B_64TRANS |
				      ISDN_PID_L1_B_64HDLC |
				      ISDN_PID_L1_B_T30FAX |
				      ISDN_PID_L1_B_MODEM_ASYNC;
	speedfax.BPROTO.protocol[2] = ISDN_PID_L2_B_TRANS |
				      ISDN_PID_L2_B_T30;

	if ((err = mISDN_register(&speedfax))) {
		printk(KERN_ERR "Can't register Speedfax error(%d)\n", err);
		return(err);
	}
	err = pci_register_driver(&sedlpci_driver);
	if (err < 0)
		goto out;
#ifdef OLD_PCI_REGISTER_DRIVER
	pci_nr_found = err;
#endif
#if defined(CONFIG_PNP)
	err = pnp_register_driver(&sedlpnp_driver);
	if (err < 0)
		goto out_unregister_pci;
#endif
#ifdef OLD_PCI_REGISTER_DRIVER
#if !defined(CONFIG_HOTPLUG) || defined(MODULE)
	if (pci_nr_found + err == 0) {
		err = -ENODEV;
		goto out_unregister_isapnp;
	}
#endif
#endif

	mISDN_module_register(THIS_MODULE);

	return 0;

#ifdef OLD_PCI_REGISTER_DRIVER
#if !defined(CONFIG_HOTPLUG) || defined(MODULE)
 out_unregister_isapnp:
#if defined(CONFIG_PNP)
	pnp_unregister_driver(&sedlpnp_driver);
#endif
#endif
#endif
#if defined(CONFIG_PNP)
 out_unregister_pci:
#endif
	pci_unregister_driver(&sedlpci_driver);
 out:
 	return err;
}

static void __exit Speedfax_cleanup(void)
{
	int		err;
	sedl_fax	*card, *next;

	mISDN_module_unregister(THIS_MODULE);

	if ((err = mISDN_unregister(&speedfax))) {
		printk(KERN_ERR "Can't unregister Speedfax PCI error(%d)\n", err);
	}
	list_for_each_entry_safe(card, next, &speedfax.ilist, list) {
		if (debug)
			printk(KERN_ERR "Speedfax PCI card struct not empty refs %d\n",
				   speedfax.refcnt);
		release_card(card);
	}
#if defined(CONFIG_PNP)
	pnp_unregister_driver(&sedlpnp_driver);
#endif
	pci_unregister_driver(&sedlpci_driver);
}

module_init(Speedfax_init);
module_exit(Speedfax_cleanup);

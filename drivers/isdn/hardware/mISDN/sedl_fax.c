/* $Id: sedl_fax.c,v 1.21 2004/06/17 12:31:12 keil Exp $
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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <asm/semaphore.h>
#ifdef NEW_ISAPNP
#include <linux/pnp.h>
#else
#include <linux/isapnp.h>
#endif
#include "dchannel.h"
#include "bchannel.h"
#include "isac.h"
#include "isar.h"
#include "layer1.h"
#include "helper.h"
#include "debug.h"

#define SPIN_DEBUG
#define LOCK_STATISTIC
#include "hw_lock.h"

extern const char *CardType[];

const char *Sedlfax_revision = "$Revision: 1.21 $";

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
	void			*pdev;
	u_int			subtyp;
	u_int			irq;
	u_int			irqcnt;
	u_int			cfg;
	u_int			addr;
	u_int			isac;
	u_int			isar;
	mISDN_HWlock_t		lock;
	isar_reg_t		ir;
	isac_chip_t		isac_hw;
	isar_hw_t		isar_hw[2];
	dchannel_t		dch;
	bchannel_t		bch[2];
} sedl_fax;

static int lock_dev(void *data, int nowait)
{
	register mISDN_HWlock_t	*lock = &((sedl_fax *)data)->lock;
	
	return(lock_HW(lock, nowait));
} 

static void unlock_dev(void *data)
{
	register mISDN_HWlock_t	*lock = &((sedl_fax *)data)->lock;

	unlock_HW(lock);
}

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
ReadISAR(void *p, int mode, u_char offset)
{	
	if (mode == 0)
		return (readreg(((sedl_fax *)p)->addr, ((sedl_fax *)p)->isar, offset));
	else if (mode == 1)
		byteout(((sedl_fax *)p)->addr, offset);
	return(bytein(((sedl_fax *)p)->isar));
}

static void
WriteISAR(void *p, int mode, u_char offset, u_char value)
{
	if (mode == 0)
		writereg(((sedl_fax *)p)->addr, ((sedl_fax *)p)->isar, offset, value);
	else {
		if (mode == 1)
			byteout(((sedl_fax *)p)->addr, offset);
		byteout(((sedl_fax *)p)->isar, value);
	}
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
	u_long		flags;
	
	spin_lock_irqsave(&sf->lock.lock, flags);
#ifdef SPIN_DEBUG
	sf->lock.spin_adr = (void *)0x2001;
#endif
	if (test_and_set_bit(STATE_FLAG_BUSY, &sf->lock.state)) {
		printk(KERN_ERR "%s: STATE_FLAG_BUSY allready activ, should never happen state:%lx\n",
			__FUNCTION__, sf->lock.state);
#ifdef SPIN_DEBUG
		printk(KERN_ERR "%s: previous lock:%p\n",
			__FUNCTION__, sf->lock.busy_adr);
#endif
#ifdef LOCK_STATISTIC
		sf->lock.irq_fail++;
#endif
	} else {
#ifdef LOCK_STATISTIC
		sf->lock.irq_ok++;
#endif
#ifdef SPIN_DEBUG
		sf->lock.busy_adr = speedfax_isa_interrupt;
#endif
	}

	test_and_set_bit(STATE_FLAG_INIRQ, &sf->lock.state);
#ifdef SPIN_DEBUG
	sf->lock.spin_adr = NULL;
#endif
	spin_unlock_irqrestore(&sf->lock.lock, flags);
	sf->irqcnt++;
	do_sedl_interrupt(sf);
	spin_lock_irqsave(&sf->lock.lock, flags);
#ifdef SPIN_DEBUG
	sf->lock.spin_adr = (void *)0x2002;
#endif
	if (!test_and_clear_bit(STATE_FLAG_INIRQ, &sf->lock.state)) {
	}
	if (!test_and_clear_bit(STATE_FLAG_BUSY, &sf->lock.state)) {
		printk(KERN_ERR "%s: STATE_FLAG_BUSY not locked state(%lx)\n",
			__FUNCTION__, sf->lock.state);
	}
#ifdef SPIN_DEBUG
	sf->lock.busy_adr = NULL;
	sf->lock.spin_adr = NULL;
#endif
	spin_unlock_irqrestore(&sf->lock.lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t
speedfax_pci_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	sedl_fax	*sf = dev_id;
	u_long		flags;
	u_char		val;

	spin_lock_irqsave(&sf->lock.lock, flags);
#ifdef SPIN_DEBUG
	sf->lock.spin_adr = (void *)0x3001;
#endif
	val = bytein(sf->cfg + TIGER_AUX_STATUS);
	if (val & SEDL_TIGER_IRQ_BIT) { /* for us or shared ? */
#ifdef SPIN_DEBUG
		sf->lock.spin_adr = NULL;
#endif
		spin_unlock_irqrestore(&sf->lock.lock, flags);
		return IRQ_NONE; /* shared */
	}
	sf->irqcnt++;
	if (test_and_set_bit(STATE_FLAG_BUSY, &sf->lock.state)) {
		printk(KERN_ERR "%s: STATE_FLAG_BUSY allready activ, should never happen state:%lx\n",
			__FUNCTION__, sf->lock.state);
#ifdef SPIN_DEBUG
		printk(KERN_ERR "%s: previous lock:%p\n",
			__FUNCTION__, sf->lock.busy_adr);
#endif
#ifdef LOCK_STATISTIC
		sf->lock.irq_fail++;
#endif
	} else {
#ifdef LOCK_STATISTIC
		sf->lock.irq_ok++;
#endif
#ifdef SPIN_DEBUG
		sf->lock.busy_adr = speedfax_pci_interrupt;
#endif
	}

	test_and_set_bit(STATE_FLAG_INIRQ, &sf->lock.state);
#ifdef SPIN_DEBUG
	sf->lock.spin_adr= NULL;
#endif
	spin_unlock_irqrestore(&sf->lock.lock, flags);
	do_sedl_interrupt(sf);
	spin_lock_irqsave(&sf->lock.lock, flags);
#ifdef SPIN_DEBUG
	sf->lock.spin_adr = (void *)0x3002;
#endif
	if (!test_and_clear_bit(STATE_FLAG_INIRQ, &sf->lock.state)) {
	}
	if (!test_and_clear_bit(STATE_FLAG_BUSY, &sf->lock.state)) {
		printk(KERN_ERR "%s: STATE_FLAG_BUSY not locked state(%lx)\n",
			__FUNCTION__, sf->lock.state);
	}
#ifdef SPIN_DEBUG
	sf->lock.busy_adr = NULL;
	sf->lock.spin_adr = NULL;
#endif
	spin_unlock_irqrestore(&sf->lock.lock, flags);
	return IRQ_HANDLED;
}

void
release_sedlbauer(sedl_fax *sf)
{
	int bytecnt = 256;

	if (sf->subtyp == SEDL_SPEEDFAX_ISA)
		bytecnt = 16;
	else
		byteout(sf->cfg + TIGER_AUX_IRQMASK, 0);
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
	u_int	shared = SA_SHIRQ;
	void	*irq_func = speedfax_pci_interrupt;

	if (sf->subtyp == SEDL_SPEEDFAX_ISA) {
		irq_func = speedfax_isa_interrupt;
		shared = 0;
	}
	lock_dev(sf, 0);
	if (request_irq(sf->irq, irq_func, shared, "speedfax", sf)) {
		printk(KERN_WARNING "mISDN: couldn't get interrupt %d\n",
			sf->irq);
		unlock_dev(sf);
		return(-EIO);
	}
	while (cnt) {
		int	ret;

		mISDN_clear_isac(&sf->dch);
		if ((ret=mISDN_isac_init(&sf->dch))) {
			printk(KERN_WARNING "mISDN: mISDN_isac_init failed with %d\n", ret);
			break;
		}
		init_isar(&sf->bch[0]);
		init_isar(&sf->bch[1]);
		if (sf->subtyp != SEDL_SPEEDFAX_ISA)
			byteout(sf->cfg + TIGER_AUX_IRQMASK, SEDL_TIGER_IRQ_BIT);
		WriteISAC(sf, ISAC_MASK, 0);
		WriteISAR(sf, 0, ISAR_IRQBIT, ISAR_IRQMSK);
		/* RESET Receiver and Transmitter */
		WriteISAC(sf, ISAC_CMDR, 0x41);
		unlock_dev(sf);
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
				lock_dev(sf, 0);
				reset_speedfax(sf);
				cnt--;
			}
		} else {
			return(0);
		}
	}
	unlock_dev(sf);
	return(-EIO);
}


#define MAX_CARDS	4
#define MODULE_PARM_T	"1-4i"
static int sedl_cnt;
static mISDNobject_t	speedfax;
static int debug;
static u_int protocol[MAX_CARDS];
static int layermask[MAX_CARDS];

#ifdef MODULE
MODULE_AUTHOR("Karsten Keil");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
MODULE_PARM(debug, "1i");
MODULE_PARM(protocol, MODULE_PARM_T);
MODULE_PARM(layermask, MODULE_PARM_T);
#endif

static char SpeedfaxName[] = "Speedfax";

int
setup_speedfax(sedl_fax *sf)
{
	int bytecnt, ver;

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
	sf->bch[0].Read_Reg = &ReadISAR;
	sf->bch[0].Write_Reg = &WriteISAR;
	sf->bch[1].Read_Reg = &ReadISAR;
	sf->bch[1].Write_Reg = &WriteISAR;
	lock_dev(sf, 0);
#ifdef SPIN_DEBUG
	printk(KERN_ERR "spin_lock_adr=%p now(%p)\n", &sf->lock.spin_adr, sf->lock.spin_adr);
	printk(KERN_ERR "busy_lock_adr=%p now(%p)\n", &sf->lock.busy_adr, sf->lock.busy_adr);
#endif
	writereg(sf->addr, sf->isar, ISAR_IRQBIT, 0);
	writereg(sf->addr, sf->isac, ISAC_MASK, 0xFF);
	ver = ISARVersion(&sf->bch[0], "Sedlbauer:");
	unlock_dev(sf);
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

#ifdef LOCK_STATISTIC
	printk(KERN_INFO "try_ok(%d) try_wait(%d) try_mult(%d) try_inirq(%d)\n",
		card->lock.try_ok, card->lock.try_wait, card->lock.try_mult, card->lock.try_inirq);
	printk(KERN_INFO "irq_ok(%d) irq_fail(%d)\n",
		card->lock.irq_ok, card->lock.irq_fail);
#endif
	lock_dev(card, 0);
	free_irq(card->irq, card);
	free_isar(&card->bch[1]);
	free_isar(&card->bch[0]);
	mISDN_isac_free(&card->dch);
	WriteISAR(card, 0, ISAR_IRQBIT, 0);
	WriteISAC(card, ISAC_MASK, 0xFF);
	reset_speedfax(card);
	WriteISAR(card, 0, ISAR_IRQBIT, 0);
	WriteISAC(card, ISAC_MASK, 0xFF);
	release_sedlbauer(card);
	mISDN_free_bch(&card->bch[1]);
	mISDN_free_bch(&card->bch[0]);
	mISDN_free_dch(&card->dch);
	speedfax.ctrl(card->dch.inst.up.peer, MGR_DISCONNECT | REQUEST, &card->dch.inst.up);
	speedfax.ctrl(&card->dch.inst, MGR_UNREGLAYER | REQUEST, NULL);
	list_del(&card->list);
	unlock_dev(card);
	if (card->subtyp == SEDL_SPEEDFAX_ISA) {
		pnp_disable_dev(card->pdev);
		pnp_set_drvdata(card->pdev, NULL);
	} else {
		pci_disable_device(card->pdev);
		pci_set_drvdata(card->pdev, NULL);
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

	printk(KERN_DEBUG "%s: data:%p prim:%x arg:%p\n",
		__FUNCTION__, data, prim, arg);
	if (!data) {
		MGR_HASPROTOCOL_HANDLER(prim,arg,&speedfax)
		printk(KERN_ERR "speedfax_manager no data prim %x arg %p\n",
			prim, arg);
		return(-EINVAL);
	}
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
	if (channel<0) {
		printk(KERN_ERR "speedfax_manager no channel data %p prim %x arg %p\n",
			data, prim, arg);
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
				if (mISDN_ISAC_l1hw(&inst->down, skb))
					dev_kfree_skb(skb);
			}
		} else {
			inst->down.fdata = &card->bch[channel];
			if ((skb = create_link_skb(MGR_DISCONNECT | REQUEST,
				0, 0, NULL, 0))) {
				if (isar_down(&inst->down, skb))
					dev_kfree_skb(skb);
			}
		}
		speedfax.ctrl(inst->up.peer, MGR_DISCONNECT | REQUEST, &inst->up);
		speedfax.ctrl(inst, MGR_UNREGLAYER | REQUEST, NULL);
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
			speedfax.refcnt--;
		}
		break;
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
		speedfax.ctrl(card->dch.inst.st, MGR_CTRLREADY | INDICATION, NULL);
		break;
	    case MGR_SETSTACK | CONFIRM:
		if ((channel!=2) && (inst->pid.global == 2)) {
			inst->down.fdata = &card->bch[channel];
			if ((skb = create_link_skb(PH_ACTIVATE | REQUEST,
				0, 0, NULL, 0))) {
				if (isar_down(&inst->down, skb))
					dev_kfree_skb(skb);
			}
			if ((inst->pid.protocol[2] == ISDN_PID_L2_B_TRANS) ||
				(inst->pid.protocol[2] == ISDN_PID_L2_B_TRANSDTMF))
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
		printk(KERN_WARNING "speedfax_manager prim %x not handled\n", prim);
		return(-EINVAL);
	}
	return(0);
}

static int __devinit setup_instance(sedl_fax *card)
{
	int		i, err;
	mISDN_pid_t	pid;
	
	if (sedl_cnt >= MAX_CARDS) {
		kfree(card);
		return(-EINVAL);
	}
	list_add_tail(&card->list, &speedfax.ilist);
	card->dch.debug = debug;
	lock_HW_init(&card->lock);
	card->dch.inst.lock = lock_dev;
	card->dch.inst.unlock = unlock_dev;
	card->dch.inst.pid.layermask = ISDN_LAYER(0);
	card->dch.inst.pid.protocol[0] = ISDN_PID_L0_TE_S0;
	mISDN_init_instance(&card->dch.inst, &speedfax, card);
	sprintf(card->dch.inst.name, "SpeedFax%d", sedl_cnt+1);
	mISDN_set_dchannel_pid(&pid, protocol[sedl_cnt], layermask[sedl_cnt]);
	mISDN_init_dch(&card->dch);
	for (i=0; i<2; i++) {
		card->bch[i].channel = i;
		mISDN_init_instance(&card->bch[i].inst, &speedfax, card);
		card->bch[i].inst.pid.layermask = ISDN_LAYER(0);
		card->bch[i].inst.lock = lock_dev;
		card->bch[i].inst.unlock = unlock_dev;
		card->bch[i].debug = debug;
		sprintf(card->bch[i].inst.name, "%s B%d", card->dch.inst.name, i+1);
		mISDN_init_bch(&card->bch[i]);
	}
	printk(KERN_DEBUG "sfax card %p dch %p bch1 %p bch2 %p\n",
		card, &card->dch, &card->bch[0], &card->bch[1]);
	err = setup_speedfax(card);
	if (err) {
		mISDN_free_dch(&card->dch);
		mISDN_free_bch(&card->bch[1]);
		mISDN_free_bch(&card->bch[0]);
		list_del(&card->list);
		kfree(card);
		return(err);
	}
	sedl_cnt++;
	err = speedfax.ctrl(NULL, MGR_NEWSTACK | REQUEST, &card->dch.inst);
	if (err) {
		release_card(card);
		return(err);
	}
	for (i=0; i<2; i++) {
		err = speedfax.ctrl(card->dch.inst.st, MGR_NEWSTACK | REQUEST, &card->bch[i].inst);
		if (err) {
			printk(KERN_ERR "MGR_ADDSTACK bchan error %d\n", err);
			speedfax.ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
			return(err);
		}
	}
	err = speedfax.ctrl(card->dch.inst.st, MGR_SETSTACK | REQUEST, &pid);
	if (err) {
		printk(KERN_ERR  "MGR_SETSTACK REQUEST dch err(%d)\n", err);
		speedfax.ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
		return(err);
	}
	err = init_card(card);
	if (err) {
		speedfax.ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
		return(err);
	}
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
	card->pdev = pdev;
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
	       (char *) ent->driver_data, pdev->slot_name);

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
	card->pdev = pdev;
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
		speedfax.ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
	else
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
		speedfax.ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
	else
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
	int err, pci_nr_found;

#ifdef MODULE
	speedfax.owner = THIS_MODULE;
#endif
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
	pci_nr_found = err;
#if defined(CONFIG_PNP)
	err = pnp_register_driver(&sedlpnp_driver);
	if (err < 0)
		goto out_unregister_pci;
#endif
#if !defined(CONFIG_HOTPLUG) || defined(MODULE)
	if (pci_nr_found + err == 0) {
		err = -ENODEV;
		goto out_unregister_isapnp;
	}
#endif
	return 0;

#if !defined(CONFIG_HOTPLUG) || defined(MODULE)
 out_unregister_isapnp:
#if defined(CONFIG_PNP)
	pnp_unregister_driver(&sedlpnp_driver);
#endif
#endif
 out_unregister_pci:
	pci_unregister_driver(&sedlpci_driver);
 out:
 	return err;
}

static void __exit Speedfax_cleanup(void)
{
	int		err;
	sedl_fax	*card, *next;

	if ((err = mISDN_unregister(&speedfax))) {
		printk(KERN_ERR "Can't unregister Speedfax PCI error(%d)\n", err);
	}
	list_for_each_entry_safe(card, next, &speedfax.ilist, list) {
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

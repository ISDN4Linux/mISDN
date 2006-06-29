/*
 * NETJet mISDN driver
 *
 * Author Daniel Potts <daniel.potts@senaware.com>
 * Copyright (C) 2005 Traverse Technologies P/L
 *
 * Based on HiSax NETJet driver by Karsten Keil
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include "channel.h"
#include "isac.h"
#include "layer1.h"
#include "debug.h"

#include <linux/ppp_defs.h>

#include "netjet.h"

//#define DPRINT(...)
//#define DTRACE(...)
#define DTRACE printk
#define DPRINT printk

static const char *netjet_rev = "$Revision: 1.2 $";

#define MAX_CARDS	4
#define MODULE_PARM_T	"1-4i"
static int netjet_cnt;
static u_int protocol[MAX_CARDS];
static int layermask[MAX_CARDS];

static mISDNobject_t netjet_mISDN;
static int debug;

enum {
	NETJET_S_TJ300,
	NETJET_S_TJ320,
};

typedef struct {
	long size;
	u_int32_t *dmabuf; 	// purposely 32-bit. (virt)
	dma_addr_t dmaaddr; 	// (system phys)
} tiger_dmabuf_t;

struct tiger_hw {
	tiger_dmabuf_t send;
	u_int32_t *s_irq;
	u_int32_t *s_end;
	u_int32_t *sendp;
	tiger_dmabuf_t rec; 
	int free;
	u_char *rcvbuf; 
	u_char *sendbuf;
	u_char *sp;
	int sendcnt;
	u_int s_tot;
	u_int r_bitcnt;
	u_int r_tot;
	u_int r_err;
	u_int r_fcs;
	u_char r_state;
	u_char r_one;
	u_char r_val;
	u_char s_state;

	u_char	*tx_buf;
	u_char	*rx_buf;

};


typedef struct {
	struct list_head	list;
	struct pci_dev		*pdev;
	u_int			subtype;
	u_int			irq;
	u_int			irqcnt;
	u_long			base;
	spinlock_t		lock;
	isac_chip_t		isac;
	struct tiger_hw		tiger[2]; // bch hdlc's
	channel_t		dch;
	channel_t		bch[2];
	u_char			ctrlreg;

	u_char			dmactrl;
	u_char			auxd;

	unsigned char last_is0;

	unsigned char irqmask0;
} netjet_t;


/* Local Prototypes */
static int tiger_l2l1B (mISDNinstance_t *inst, struct sk_buff *skb);
static void nj_release_card(netjet_t *card);
static void netjet_fill_dma(channel_t *bch);
static void write_raw(channel_t *bch, u_int *buf, int cnt);
static void write_tiger_bch(channel_t *bch, u_int *buf, int cnt);

static void
nj_disable_hwirq(netjet_t *card)
{
	outb (0, card->base + NETJET_IRQMASK0);
	outb (0, card->base + NETJET_IRQMASK1);
}


static u_char
nj_readISAC(void *cs, u_char offset)
{
	netjet_t *card = (netjet_t *)cs;
	u_char ret;
	
	card->auxd &= 0xfc;
	card->auxd |= (offset>>4) & 3;
	outb (card->auxd, card->base + NETJET_AUXDATA);
	ret = inb ((card->base | NETJET_ISAC_OFF) |
		   ((offset & NETJET_HA_MASK) << NETJET_HA_OFFSET));
	return (ret);
}


void
nj_writeISAC(void *cs, u_char offset, u_char value)
{
	netjet_t *card = (netjet_t *)cs;

	card->auxd &= 0xfc;
	card->auxd |= (offset>>4) & 3;
	outb (card->auxd, card->base + NETJET_AUXDATA);
	outb (value, (card->base | NETJET_ISAC_OFF)  |
		   ((offset & NETJET_HA_MASK) << NETJET_HA_OFFSET));
}


void
nj_readISACfifo(void *cs, u_char *data, int size)
{
	netjet_t *card = (netjet_t *)cs;

	card->auxd &= 0xfc;
	outb (card->auxd, card->base + NETJET_AUXDATA);
	insb((card->base | NETJET_ISAC_OFF), data, size);
}


void 
nj_writeISACfifo(void *cs, u_char *data, int size)
{
	netjet_t *card = (netjet_t *)cs;

	card->auxd &= 0xfc;
	outb (card->auxd, card->base + NETJET_AUXDATA);
	outsb((card->base | NETJET_ISAC_OFF), data, size);
}


void 
fill_mem(channel_t *bch, u_int *pos, u_int cnt, int chan, u_char fill)
{
	u_int mask=0x000000ff, val = 0, *p=pos;
	u_int i;
	struct tiger_hw *tiger;

	tiger = bch->hw;
	
	val |= fill;
	if (chan) {
		val  <<= 8;
		mask <<= 8;
	}
	mask ^= 0xffffffff;
	for (i=0; i<cnt; i++) {
		*p   &= mask;
		*p++ |= val;
		if (p > tiger->s_end)
			p = tiger->send.dmabuf;
	}
}


int
mode_tiger(channel_t *bch, int bc, int protocol)
{
	struct tiger_hw *tiger;
	netjet_t *card;

	tiger = bch->hw;
	card = bch->inst.privat;

//        u_char led;

	if (bch->debug & L1_DEB_HSCX)
		mISDN_debugprint(&bch->inst, 
				 "Tiger bchan %d/%d protocol %x-->%x",
				 bc, bch->channel, bch->state, protocol);
	if ((protocol != -1) && (bc != bch->channel))
		printk(KERN_WARNING "%s: netjet mismatch channel(%d/%d)\n", 
		       __FUNCTION__, bch->channel, bc);

	switch (protocol) {
	case (-1): /* used for init */
		bch->state = protocol; //james
		bch->channel = bc;
	case (ISDN_PID_NONE):
		if (bch->state == ISDN_PID_NONE) {
			break;
		}
		fill_mem(bch, tiger->send.dmabuf,
			 NETJET_DMA_TXSIZE, bc, 0xff);
		if (bch->debug & L1_DEB_HSCX)
			mISDN_debugprint(&bch->inst, 
					 "Tiger stat rec %d/%d send %d",
					 tiger->r_tot, tiger->r_err,
					 tiger->s_tot); 
		bch->state = protocol; //james
		/* only stop dma and interrupts if both channels NULL */
		if ((card->bch[0].state == ISDN_PID_NONE) &&
		    (card->bch[1].state == ISDN_PID_NONE))
		{
			card->dmactrl = 0;
			outb(card->dmactrl, card->base + NETJET_DMACTRL);
			outb(0, card->base + NETJET_IRQMASK0);
		}
		test_and_clear_bit(FLG_HDLC, &bch->Flags);
		test_and_clear_bit(FLG_TRANSPARENT, &bch->Flags);
#if 0 // led stuff
		if (cs->typ == ISDN_CTYPE_NETJET_S)
		{
			// led off
			led = bc & 0x01;
			led = 0x01 << (6 + led); // convert to mask
			led = ~led;
			cs->hw.njet.auxd &= led;
			byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);
		}
#endif
		break;
	case (ISDN_PID_L1_B_64TRANS):
		/* fall through for transparancy (eg voice).. */
		test_and_set_bit(FLG_TRANSPARENT, &bch->Flags);

//	case (L1_MODE_HDLC_56K):
	case (ISDN_PID_L1_B_64HDLC):
		bch->state = protocol; //james
		if (protocol == ISDN_PID_L1_B_64HDLC)
			test_and_set_bit(FLG_HDLC, &bch->Flags);
		fill_mem(bch, tiger->send.dmabuf,
			 NETJET_DMA_TXSIZE, bc, 0xff);
		tiger->r_state = HDLC_ZERO_SEARCH;
		tiger->r_tot = 0;
		tiger->r_bitcnt = 0;
		tiger->r_one = 0;
		tiger->r_err = 0;
		tiger->s_tot = 0;
		if (! card->dmactrl) {
			fill_mem(bch, tiger->send.dmabuf,
				 NETJET_DMA_TXSIZE, !bc, 0xff);
			card->dmactrl = 1;
			outb(card->dmactrl, card->base + NETJET_DMACTRL);
			outb(0x0f, card->base + NETJET_IRQMASK0);
			/* was 0x3f now 0x0f for TJ300 and TJ320 GE 13/07/00 */
		}
		tiger->sendp = tiger->send.dmabuf;
		tiger->free = NETJET_DMA_TXSIZE;
		test_and_set_bit(FLG_EMPTY, &bch->Flags);
#if 0 // old led stuff
		if (cs->typ == ISDN_CTYPE_NETJET_S)
		{
			// led on
			led = bc & 0x01;
			led = 0x01 << (6 + led); // convert to mask
			cs->hw.njet.auxd |= led;
			byteout(cs->hw.njet.auxa, cs->hw.njet.auxd);
		}
#endif
		break;
	default:
		mISDN_debugprint(&bch->inst, "nj prot not known %x", protocol);
		return(-ENOPROTOOPT);
	}

	if (bch->debug & L1_DEB_HSCX)
	 	mISDN_debugprint(&bch->inst, 
				 "tiger: set %x %x %x  %x/%x  pulse=%d",
				 inb(card->base + NETJET_DMACTRL),
				 inb(card->base + NETJET_IRQMASK0),
				 inb(card->base + NETJET_IRQSTAT0),
				 inl(card->base + NETJET_DMA_READ_ADR),
				 inl(card->base + NETJET_DMA_WRITE_ADR),
				 inb(card->base + NETJET_PULSE_CNT));
	return 0;
}


static void
nj_reset (netjet_t *card)
{
	outb (0xff, card->base + NETJET_CTRL); /* Reset On */
	mdelay(10);

	/* now edge triggered for TJ320 GE 13/07/00 */
	/* see comment in IRQ function */
	if (card->subtype == NETJET_S_TJ320) /* TJ320 */
		card->ctrlreg = 0x40;  /* Reset Off and status read clear */
	else
		card->ctrlreg = 0x00;  /* Reset Off and status read clear */
	outb (card->ctrlreg, card->base + NETJET_CTRL);
	mdelay(10);

	/* configure AUX pins (all output except ISAC IRQ pin) */
	card->auxd = 0;
	card->dmactrl = 0;
	outb (~NETJET_ISACIRQ, card->base + NETJET_AUXCTRL);
	outb (NETJET_ISACIRQ,  card->base + NETJET_IRQMASK1);
	outb (card->auxd, card->base + NETJET_AUXDATA);	
}


static int
nj_manager (void *data, u_int prim, void *arg)
{
	mISDNinstance_t *inst = data;
	netjet_t *card;
	struct sk_buff *skb;
	u_long		flags;
	int channel = -1;

	if (!data) {
		MGR_HASPROTOCOL_HANDLER(prim,arg,&netjet_mISDN)
		printk(KERN_ERR "%s: no data prim %x arg %p\n",
			__FUNCTION__, prim, arg);
		return(-EINVAL);
	}	

	spin_lock_irqsave(&netjet_mISDN.lock, flags);
	list_for_each_entry(card, &netjet_mISDN.ilist, list) {
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
	spin_unlock_irqrestore(&netjet_mISDN.lock, flags);
	if (channel<0) {
		printk(KERN_WARNING "%s: no channel data %p prim %x arg %p\n",
			__FUNCTION__, data, prim, arg);
		return(-EINVAL);
	}

	switch (prim) {
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
				if (tiger_l2l1B(inst, skb))
					dev_kfree_skb(skb);
			}
		} else {
			printk(KERN_WARNING "no SKB in %s MGR_UNREGLAYER | REQUEST\n", __FUNCTION__);
		}
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
			nj_release_card(card);
		} else {
			netjet_mISDN.refcnt--;
		}
		break;

	case MGR_SETSTACK | INDICATION:
		if ((channel!=2) && (inst->pid.global == 2)) {
			if ((skb = create_link_skb(PH_ACTIVATE | REQUEST,
				0, 0, NULL, 0))) {
				if (tiger_l2l1B(inst, skb))
					dev_kfree_skb(skb);
			}
			if (inst->pid.protocol[2] == ISDN_PID_L2_B_TRANS)
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
		printk(KERN_WARNING "%s: prim %x not handled\n",
		       __FUNCTION__, prim);
		return (-EINVAL);
	}

	return 0;
}


static int
tiger_l2l1B(mISDNinstance_t *inst, struct sk_buff *skb)
{
	channel_t	*bch;
	int		ret = 0;
	mISDN_head_t	*hh = mISDN_HEAD_P(skb);
	u_long		flags;

	bch = container_of(inst, channel_t, inst);

	if ((hh->prim == PH_DATA_REQ) || (hh->prim == DL_DATA_REQ)) {
		spin_lock_irqsave(inst->hwlock, flags);
		ret = channel_senddata(bch, hh->dinfo, skb);
		if (ret > 0) { /* direct TX */
			netjet_fill_dma(bch);
			ret = 0;
		}
		spin_unlock_irqrestore(inst->hwlock, flags);
		return(ret);
	} 
	if ((hh->prim == (PH_ACTIVATE | REQUEST)) ||
		(hh->prim == (DL_ESTABLISH  | REQUEST))) {
		if (!test_and_set_bit(FLG_ACTIVE, &bch->Flags)) {
			spin_lock_irqsave(inst->hwlock, flags);
			ret = mode_tiger(bch, bch->channel, bch->inst.pid.protocol[1]);
			spin_unlock_irqrestore(inst->hwlock, flags);
		}
		skb_trim(skb, 0);
		return(mISDN_queueup_newhead(inst, 0, hh->prim | CONFIRM, ret, skb));
	} else if ((hh->prim == (PH_DEACTIVATE | REQUEST)) ||
		(hh->prim == (DL_RELEASE | REQUEST)) ||
		((hh->prim == (PH_CONTROL | REQUEST) && (hh->dinfo == HW_DEACTIVATE)))) {
		spin_lock_irqsave(inst->hwlock, flags);
		if (test_and_clear_bit(FLG_TX_NEXT, &bch->Flags)) {
			if (bch->next_skb)
				printk(KERN_WARNING "%s: TX_NEXT set with no next_skb\n", __FUNCTION__);
			else {
				dev_kfree_skb(bch->next_skb);
				bch->next_skb = NULL;
			}
		}
		if (bch->tx_skb) {
			dev_kfree_skb(bch->tx_skb);
			bch->tx_skb = NULL;
			bch->tx_idx = 0;
		}
		test_and_clear_bit(FLG_TX_BUSY, &bch->Flags);
		mode_tiger(bch, bch->channel, 0);
		test_and_clear_bit(FLG_ACTIVE, &bch->Flags);
		spin_unlock_irqrestore(inst->hwlock, flags);
		skb_trim(skb, 0);
		if (hh->prim != (PH_CONTROL | REQUEST))
			if (!mISDN_queueup_newhead(inst, 0, hh->prim | CONFIRM, 0, skb))
				return 0;
	} else {
		printk(KERN_WARNING "tiger_l2l1B unknown %x prim(%x)\n", (int)skb, hh->prim);
		ret = -EINVAL;
	}
	if (!ret) {
		dev_kfree_skb(skb);
	}
	return(ret);
}


void __init
inittiger(netjet_t *card)
{
	struct tiger_hw *tiger0, *tiger1;

	/* NOTE: I believe hisax tiger driver is wrong.
	 *  It allocates 4 times too much memory (unsigned int) and does not
	 *  use pci consistent, which won't work on all systems.
	 */

	tiger0 = &card->tiger[0];
	tiger1 = &card->tiger[1];

	/* XXX review buffer size */
	tiger0->tx_buf = kmalloc(4 * NETJET_DMA_TXSIZE * sizeof (u_int32_t), GFP_ATOMIC);
	tiger1->tx_buf = kmalloc(4 * NETJET_DMA_TXSIZE * sizeof (u_int32_t), GFP_ATOMIC);

	tiger0->send.size = NETJET_DMA_TXSIZE * sizeof (u_int32_t);
	tiger0->send.dmabuf = 
		pci_alloc_consistent ((struct pci_dev *) card->pdev,
				      tiger0->send.size,
				      &tiger0->send.dmaaddr);
	if (!tiger0->send.dmabuf) {
		printk(KERN_WARNING "mISDN: No memory for tiger.send\n");
		return;
	}

	/* 
	 * NOTE: This code is seedy int ptr arithmetic.
	 *       You have been warned.
	 */

	tiger0->s_irq = tiger0->send.dmabuf + NETJET_DMA_TXSIZE/2 - 1;
	tiger0->s_end = tiger0->send.dmabuf + NETJET_DMA_TXSIZE - 1;
	tiger1->send.dmabuf = tiger0->send.dmabuf;
	tiger1->s_irq = tiger0->s_irq;
	tiger1->s_end = tiger0->s_end;
	
	memset(tiger0->send.dmabuf, 0xff, NETJET_DMA_TXSIZE *sizeof(uint32_t));
	mISDN_debugprint(&card->bch[0].inst, "tiger: send buf %p - %p", 
			 tiger0->send.dmabuf,
			 tiger0->send.dmabuf + NETJET_DMA_TXSIZE - 1);
	outl(tiger0->send.dmaaddr, card->base + NETJET_DMA_READ_START);
	outl(virt_to_bus(tiger0->s_irq), card->base + NETJET_DMA_READ_IRQ);
	outl(virt_to_bus(tiger0->s_end), card->base + NETJET_DMA_READ_END);

	tiger0->rec.size = NETJET_DMA_RXSIZE * sizeof (u_int32_t);
	tiger0->rec.dmabuf = pci_alloc_consistent ((struct pci_dev *) 
						   card->pdev,
						   tiger0->rec.size,
						   &tiger0->rec.dmaaddr);
	if (!tiger0->rec.dmabuf) {
		printk(KERN_WARNING "mISDN: No memory for tiger.rec\n");
		return;
	}

	mISDN_debugprint(&card->bch[0].inst, "tiger: rec buf %p - %p", 
			 tiger0->rec.dmabuf,
			 tiger0->rec.dmabuf + NETJET_DMA_RXSIZE - 1);
	tiger1->rec.dmabuf = tiger0->rec.dmabuf;
	memset(tiger0->rec.dmabuf, 0xff, NETJET_DMA_RXSIZE *sizeof(u_int32_t));
	outl(tiger0->rec.dmaaddr, card->base + NETJET_DMA_WRITE_START);
	outl(virt_to_bus(tiger0->rec.dmabuf + NETJET_DMA_RXSIZE/2 - 1),
	     card->base + NETJET_DMA_WRITE_IRQ);
	outl(virt_to_bus(tiger0->rec.dmabuf + NETJET_DMA_RXSIZE - 1),
	     card->base + NETJET_DMA_WRITE_END);
	mISDN_debugprint(&card->bch[0].inst, "tiger: dmacfg  %x/%x  pulse=%d",
			 inl(card->base + NETJET_DMA_WRITE_ADR),
			 inl(card->base + NETJET_DMA_READ_ADR),
			 inb(card->base + NETJET_PULSE_CNT));
	card->last_is0 = 0;
}


static 
void printframe(mISDNinstance_t *cs, u_char *buf, int count, char *s) 
{
	char tmp[128];
	char *t = tmp;
	int i=count,j;
	u_char *p = buf;

	t += sprintf(t, "tiger %s(%4d)", s, count);
	while (i>0) {
		if (i>16)
			j=16;
		else
			j=i;

		mISDN_QuickHex(t, p, j);
		mISDN_debugprint(cs, tmp);
		p += j;
		i -= j;
		t = tmp;
		t += sprintf(t, "tiger %s      ", s);
	}
}


// macro for 64k
#define MAKE_RAW_BYTE for (j=0; j<8; j++) { \
			bitcnt++;\
			s_val >>= 1;\
			if (val & 1) {\
				s_one++;\
				s_val |= 0x80;\
			} else {\
				s_one = 0;\
				s_val &= 0x7f;\
			}\
			if (bitcnt==8) {\
				tiger->tx_buf[s_cnt++] = s_val;\
				bitcnt = 0;\
			}\
			if (s_one == 5) {\
				s_val >>= 1;\
				s_val &= 0x7f;\
				bitcnt++;\
				s_one = 0;\
			}\
			if (bitcnt==8) {\
				tiger->tx_buf[s_cnt++] = s_val;\
				bitcnt = 0;\
			}\
			val >>= 1;\
		}


static int 
make_raw_data_transparent(channel_t *bch, struct sk_buff *skb) 
{
	// this make_raw is for transparent (Voice)
	u_int i,s_cnt=0;
	u_char val;
	struct tiger_hw *tiger;
	
	tiger = bch->hw;
	
	if (!skb) {
		mISDN_debugprint(&bch->inst, "tiger make_raw_trans: NULL skb");
		return(1);
	}

	for (i=0; i<skb->len; i++) {
		val = skb->data[i]; 		// input
		tiger->tx_buf[s_cnt++] = val;	// output
	}
	tiger->sendcnt = s_cnt;
	tiger->sp = tiger->tx_buf;
	return(0);
}


static int 
make_raw_data(channel_t *bch, struct sk_buff *skb) 
{
	// this make_raw is for 64k
	u_int i,s_cnt=0;
	u_char j;
	u_char val;
	u_char s_one = 0;
	u_char s_val = 0;
	u_char bitcnt = 0;
	u_int fcs;

	struct tiger_hw *tiger;

	tiger = bch->hw;

	if (!skb) {
		mISDN_debugprint(&bch->inst, "tiger make_raw: NULL skb");
		return(1);
	}

	tiger->tx_buf[s_cnt++] = HDLC_FLAG_VALUE;
	fcs = PPP_INITFCS;
	for (i=0; i<skb->len; i++) {
		val = skb->data[i];
		fcs = PPP_FCS (fcs, val);
		MAKE_RAW_BYTE;
	}
	fcs ^= 0xffff;
	val = fcs & 0xff;
	MAKE_RAW_BYTE;
	val = (fcs>>8) & 0xff;
	MAKE_RAW_BYTE;
	val = HDLC_FLAG_VALUE;
	for (j=0; j<8; j++) { 
		bitcnt++;
		s_val >>= 1;
		if (val & 1)
			s_val |= 0x80;
		else
			s_val &= 0x7f;
		if (bitcnt==8) {
			tiger->tx_buf[s_cnt++] = s_val;
			bitcnt = 0;
		}
		val >>= 1;
	}
	if (bch->debug & L1_DEB_HSCX)
		mISDN_debugprint(&bch->inst,"tiger make_raw: in %ld out %d.%d",
				 skb->len, s_cnt, bitcnt);
	if (bitcnt) {
		while (8>bitcnt++) {
			s_val >>= 1;
			s_val |= 0x80;
		}
		tiger->tx_buf[s_cnt++] = s_val;
		tiger->tx_buf[s_cnt++] = 0xff;	// NJ<->NJ thoughput bug fix
	}
	tiger->sendcnt = s_cnt;
	tiger->sp = tiger->tx_buf;
	return(0);
}


// macro for 56k
#define MAKE_RAW_BYTE_56K for (j=0; j<8; j++) { \
			bitcnt++;\
			s_val >>= 1;\
			if (val & 1) {\
				s_one++;\
				s_val |= 0x80;\
			} else {\
				s_one = 0;\
				s_val &= 0x7f;\
			}\
			if (bitcnt==7) {\
				s_val >>= 1;\
				s_val |= 0x80;\
				tiger->tx_buf[s_cnt++] = s_val;\
				bitcnt = 0;\
			}\
			if (s_one == 5) {\
				s_val >>= 1;\
				s_val &= 0x7f;\
				bitcnt++;\
				s_one = 0;\
			}\
			if (bitcnt==7) {\
				s_val >>= 1;\
				s_val |= 0x80;\
				tiger->tx_buf[s_cnt++] = s_val;\
				bitcnt = 0;\
			}\
			val >>= 1;\
		}


static int 
make_raw_data_56k(channel_t *bch, struct sk_buff *skb)
{
	// this make_raw is for 56k
	u_int i,s_cnt=0;
	u_char j;
	u_char val;
	u_char s_one = 0;
	u_char s_val = 0;
	u_char bitcnt = 0;
	u_int fcs;

	struct tiger_hw *tiger;

	tiger = bch->hw;

	if (!skb) {
		mISDN_debugprint(&bch->inst, "tiger make_raw_56k: NULL skb");
		return(1);
	}

	val = HDLC_FLAG_VALUE;
	for (j=0; j<8; j++) { 
		bitcnt++;
		s_val >>= 1;
		if (val & 1)
			s_val |= 0x80;
		else
			s_val &= 0x7f;
		if (bitcnt==7) {
			s_val >>= 1;
			s_val |= 0x80;
			tiger->tx_buf[s_cnt++] = s_val;
			bitcnt = 0;
		}
		val >>= 1;
	};
	fcs = PPP_INITFCS;
	for (i=0; i<skb->len; i++) {
		val = skb->data[i];
		fcs = PPP_FCS (fcs, val);
		MAKE_RAW_BYTE_56K;
	}
	fcs ^= 0xffff;
	val = fcs & 0xff;
	MAKE_RAW_BYTE_56K;
	val = (fcs>>8) & 0xff;
	MAKE_RAW_BYTE_56K;
	val = HDLC_FLAG_VALUE;
	for (j=0; j<8; j++) { 
		bitcnt++;
		s_val >>= 1;
		if (val & 1)
			s_val |= 0x80;
		else
			s_val &= 0x7f;
		if (bitcnt==7) {
			s_val >>= 1;
			s_val |= 0x80;
			tiger->tx_buf[s_cnt++] = s_val;
			bitcnt = 0;
		}
		val >>= 1;
	}
	if (bch->debug & L1_DEB_HSCX)
		mISDN_debugprint(&bch->inst,
				 "tiger make_raw_56k: in %ld out %d.%d",
				 skb->len, s_cnt, bitcnt);
	if (bitcnt) {
		while (8>bitcnt++) {
			s_val >>= 1;
			s_val |= 0x80;
		}
		tiger->tx_buf[s_cnt++] = s_val;
		tiger->tx_buf[s_cnt++] = 0xff;	// NJ<->NJ thoughput bug fix
	}
	tiger->sendcnt = s_cnt;
	tiger->sp = tiger->tx_buf;
	return(0);
}


static void 
netjet_fill_dma(channel_t *bch)
{
	u_int *p, *sp;
	int cnt;
	struct tiger_hw *tiger;
	netjet_t *card;
	struct sk_buff *skb;

	tiger = bch->hw;
	card = bch->inst.privat;
	skb = bch->tx_skb;

	if (!skb) {
		printk(KERN_WARNING "%s: called with no skb\n", __FUNCTION__);
		return;
	}

	if (bch->debug & L1_DEB_HSCX)
		mISDN_debugprint(&bch->inst,"tiger fill_dma1: c%d %4x", 
				 bch->channel, bch->Flags);

	if (test_bit(FLG_HDLC, &bch->Flags)) {		// it's 64k
		if (make_raw_data(bch, skb))
			return;		
	} else if (test_bit(FLG_TRANSPARENT, &bch->Flags)) {
		if (make_raw_data_transparent(bch, skb))
			return;
	} else { 						// it's 56k
		if (make_raw_data_56k(bch, skb))
			return;		
	}
	if (bch->debug & L1_DEB_HSCX)
		mISDN_debugprint(&bch->inst,"tiger fill_dma2: c%d %4x", 
				 bch->channel, bch->Flags);

	if (test_and_clear_bit(FLG_NOFRAME, &bch->Flags)) {
		write_raw(bch, tiger->sendp, tiger->free);
	} else if (test_and_clear_bit(FLG_HALF, &bch->Flags)) {
		/* urky, don't we know this already? */
		p = bus_to_virt(inl(card->base + NETJET_DMA_READ_ADR));
		sp = tiger->sendp;
		if (p == tiger->s_end)
			p = tiger->send.dmabuf -1;
		if (sp == tiger->s_end)
			sp = tiger->send.dmabuf -1;
		cnt = p - sp;
		if (cnt <0) {
			write_raw(bch, tiger->sendp, tiger->free);
		} else {
			p++;
			cnt++;
			if (p > tiger->s_end)
				p = tiger->send.dmabuf;
			p++;
			cnt++;
			if (p > tiger->s_end)
				p = tiger->send.dmabuf;
			write_raw(bch, p, tiger->free - cnt);
		}
	} else if (test_and_clear_bit(FLG_EMPTY, &bch->Flags)) {
		p = bus_to_virt(inl(card->base + NETJET_DMA_READ_ADR));
		cnt = tiger->s_end - p;
		if (cnt < 2) {
			p = tiger->send.dmabuf + 1;
			cnt = NETJET_DMA_TXSIZE/2 - 2;
		} else {
			p++;
			p++;
			if (cnt <= (NETJET_DMA_TXSIZE/2))
				cnt += NETJET_DMA_TXSIZE/2;
			cnt--;
			cnt--;
		}
		write_raw(bch, p, cnt);
	}
	if (bch->debug & L1_DEB_HSCX)
		mISDN_debugprint(&bch->inst,"tiger fill_dma3: c%d %4x", 
				 bch->channel, bch->Flags);
}


static void 
write_raw (channel_t *bch, u_int *buf, int cnt) 
{
	u_int mask, val, *p=buf;
	u_int i, s_cnt;
	struct tiger_hw *tiger;

	tiger = bch->hw;
        
        if (cnt <= 0) {
        	return;
	}
	if (test_bit(FLG_TX_BUSY, &bch->Flags)) {
		if (tiger->sendcnt > cnt) {
			s_cnt = cnt;
			tiger->sendcnt -= cnt;
		} else {
			s_cnt = tiger->sendcnt;
			tiger->sendcnt = 0;
		}
		if (bch->channel)
			mask = 0xffff00ff;
		else
			mask = 0xffffff00;
		for (i=0; i<s_cnt; i++) {
			val = bch->channel ? ((tiger->sp[i] <<8) & 0xff00) :
				(tiger->sp[i]);
			*p   &= mask;
			*p++ |= val;
			if (p > tiger->s_end)
				p = tiger->send.dmabuf;
		}
		tiger->s_tot += s_cnt;
		if (bch->debug & L1_DEB_HSCX)
			mISDN_debugprint(&bch->inst,
					 "tiger write_raw: c%d %p-%p %d/%d %d N/A", 
					 bch->channel, buf, p, s_cnt, cnt,
					 tiger->sendcnt/*, bcs->cs->hw.njet.irqstat0*/);
		if (bch->debug & L1_DEB_HSCX_FIFO)
			printframe(&bch->inst, tiger->sp, s_cnt, "snd");
		tiger->sp += s_cnt;
		tiger->sendp = p;

		/* Test to see if we can send more */
		if (tiger->sendcnt == 0) {

			/* XXX old block, all needed? */
			tiger->free = cnt - s_cnt;
			if (tiger->free > (NETJET_DMA_TXSIZE/2))
				test_and_set_bit(FLG_HALF, &bch->Flags);
			else {
				test_and_clear_bit(FLG_HALF, &bch->Flags);
				test_and_set_bit(FLG_NOFRAME, &bch->Flags);
			}
			/* end old block */

			if (bch->tx_skb) {
				dev_kfree_skb(bch->tx_skb);
				bch->tx_skb = NULL;
				test_and_clear_bit(FLG_TX_BUSY, &bch->Flags);
			}
#if 0
			if (test_and_clear_bit(FLG_TX_NEXT, &bch->Flags)) {
				bch->tx_skb = bch->next_skb;
				bch->tx_idx = 0;
				bch->next_skb = NULL;
				if (bch->tx_skb) {
					mISDN_head_t *hh = mISDN_HEAD_P(bch->tx_skb);
					queue_ch_frame(bch, CONFIRM, hh->dinfo, NULL);
					netjet_fill_dma(bch);
				} else {
					//bch->tx_len = 0;
					printk(KERN_WARNING "hdlc tx irq TX_NEXT without skb\n");
					test_and_clear_bit(FLG_TX_BUSY, &bch->Flags);
				}
			} else {
				bch->tx_skb = NULL;
				mask ^= 0xffffffff;
				if (s_cnt < cnt) {
					for (i=s_cnt; i<cnt;i++) {
						*p++ |= mask;
						if (p>tiger->s_end)
							p = tiger->send.dmabuf;
					}
					if (bch->debug & L1_DEB_HSCX)
						mISDN_debugprint(&bch->inst, "tiger write_raw: fill rest %d",
								 cnt - s_cnt);
				}
				test_and_clear_bit(FLG_TX_BUSY, &bch->Flags);
			}
#endif
		}
	} else if (test_and_clear_bit(FLG_NOFRAME, &bch->Flags)) {
		test_and_set_bit(FLG_HALF, &bch->Flags);
		fill_mem(bch, buf, cnt, bch->channel, 0xff);
		tiger->free += cnt;
		if (bch->debug & L1_DEB_HSCX)
			mISDN_debugprint(&bch->inst,
					 "tiger write_raw: fill half");
	} else if (test_and_clear_bit(FLG_HALF, &bch->Flags)) {
		test_and_set_bit(FLG_EMPTY, &bch->Flags);
		fill_mem(bch, buf, cnt, bch->channel, 0xff);
		if (bch->debug & L1_DEB_HSCX)
			mISDN_debugprint(&bch->inst,
					 "tiger write_raw: fill full");
	}
}


static void 
got_frame(channel_t *bch, int count) 
{
	struct sk_buff *skb;
		
	if (bch->rx_skb == NULL)
		return;

	if (bch->rx_skb->len < MISDN_COPY_SIZE) {
		skb = alloc_stack_skb(bch->rx_skb->len, bch->up_headerlen);
		if (skb) {
			memcpy(skb_put(skb, bch->rx_skb->len),
			       bch->rx_skb->data, bch->rx_skb->len);
			skb_trim(bch->rx_skb, 0);
		} else {
			skb = bch->rx_skb;
			bch->rx_skb = NULL;
		}
	} else {
		skb = bch->rx_skb;
		bch->rx_skb = NULL;
	}

	queue_ch_frame(bch, INDICATION, MISDN_ID_ANY, skb);
}


static void 
read_raw_transparent(channel_t *bch, u_int *buf, int cnt)
{
	struct tiger_hw *tiger;
	int i;
	u_char val;
	u_int  *pend;
	u_int *p = buf;
	
	u_char *skb_ptr;

	tiger = bch->hw;
	pend = tiger->rec.dmabuf + NETJET_DMA_RXSIZE -1;

	if (!bch->rx_skb) {
		if (!(bch->rx_skb = alloc_stack_skb(bch->maxlen, bch->up_headerlen))) {
			printk(KERN_WARNING "mISDN: B receive out of memory\n");
			return;
		}
	}
	if ((bch->rx_skb->len + cnt) > bch->maxlen) {
		if (bch->debug & L1_DEB_WARN)
			mISDN_debugprint(&bch->inst, "read_raw_transparent overrun %d",
				bch->rx_skb->len + cnt);
		return;
	}

	skb_ptr = skb_put(bch->rx_skb, cnt);

	for (i=0;i<cnt;i++) {
		val = bch->channel ? ((*p>>8) & 0xff) : (*p & 0xff);
		p++;
		if (p > pend)
			p = tiger->rec.dmabuf;
		skb_ptr[i] = val;
	}
        got_frame(bch, cnt);
}


static void 
read_raw(channel_t *bch, u_int *buf, int cnt)
{
	int i;
	u_char j;
	u_char val;
	struct tiger_hw *tiger;
	u_int  *pend;

	u_char state;
	u_char r_one;
	u_char r_val;
	u_int bitcnt;

	int bits;
	u_char mask;

	u_int *p = buf;

	u_char *skb_ptr;

	if (!bch->rx_skb) {
		if (!(bch->rx_skb = alloc_stack_skb(bch->maxlen, bch->up_headerlen))) {
			printk(KERN_WARNING "mISDN: B receive out of memory\n");
			return;
		}
	}
	if ((bch->rx_skb->len + cnt) > bch->maxlen) {
		if (bch->debug & L1_DEB_WARN)
			mISDN_debugprint(&bch->inst, "read_raw_transparent overrun %d",
				bch->rx_skb->len + cnt);
		return;
	}

	// XXX not sure about size of count!
	skb_ptr = skb_put (bch->rx_skb, cnt);

	tiger = bch->hw;
	pend = tiger->rec.dmabuf + NETJET_DMA_RXSIZE - 1;
	state = tiger->r_state;
	r_one = tiger->r_one;
	r_val = tiger->r_val;
	bitcnt = tiger->r_bitcnt;

        if (test_bit(FLG_HDLC, &bch->Flags)) {
		mask = 0xff;
		bits = 8;
	}
	else { // it's 56K
		mask = 0x7f;
		bits = 7;
	};
	for (i=0;i<cnt;i++) {
		val = bch->channel ? ((*p>>8) & 0xff) : (*p & 0xff);
		p++;
		if (p > pend)
			p = tiger->rec.dmabuf;
		if ((val & mask) == mask) {
			state = HDLC_ZERO_SEARCH;
			tiger->r_tot++;
			bitcnt = 0;
			r_one = 0;
			continue;
		}
		for (j=0;j<bits;j++) {
			if (state == HDLC_ZERO_SEARCH) {
				if (val & 1) {
					r_one++;
				} else {
					r_one = 0;
					state = HDLC_FLAG_SEARCH;
					if (bch->debug & L1_DEB_HSCX)
						mISDN_debugprint(&bch->inst,"tiger read_raw: zBit(%d,%d,%d) %x",
								 tiger->r_tot,i,j,val);
				}
			} else if (state == HDLC_FLAG_SEARCH) { 
				if (val & 1) {
					r_one++;
					if (r_one>6) {
						state = HDLC_ZERO_SEARCH;
					}
				} else {
					if (r_one==6) {
						bitcnt = 0;
						r_val = 0;
						state = HDLC_FLAG_FOUND;
						if (bch->debug & L1_DEB_HSCX)
							mISDN_debugprint(&bch->inst,"tiger read_raw: flag(%d,%d,%d) %x",
									 tiger->r_tot,i,j,val);
					}
					r_one=0;
				}
			} else if (state ==  HDLC_FLAG_FOUND) {
				if (val & 1) {
					r_one++;
					if (r_one>6) {
						state=HDLC_ZERO_SEARCH;
					} else {
						r_val >>= 1;
						r_val |= 0x80;
						bitcnt++;
					}
				} else {
					if (r_one==6) {
						bitcnt=0;
						r_val=0;
						r_one=0;
						val >>= 1;
						continue;
					} else if (r_one!=5) {
						r_val >>= 1;
						r_val &= 0x7f;
						bitcnt++;
					}
					r_one=0;	
				}
				if ((state != HDLC_ZERO_SEARCH) &&
					!(bitcnt & 7)) {
					state=HDLC_FRAME_FOUND;
					tiger->r_fcs = PPP_INITFCS;
					skb_ptr[0] = r_val;
					tiger->r_fcs = PPP_FCS (tiger->r_fcs, r_val);
					if (bch->debug & L1_DEB_HSCX)
						mISDN_debugprint(&bch->inst,"tiger read_raw: byte1(%d,%d,%d) rval %x val %x i (N/A)",
								 tiger->r_tot,i,j,r_val,val
								 /*,bcs->cs->hw.njet.irqstat0*/);
				}
			} else if (state ==  HDLC_FRAME_FOUND) {
				if (val & 1) {
					r_one++;
					if (r_one>6) {
						state=HDLC_ZERO_SEARCH;
						bitcnt=0;
					} else {
						r_val >>= 1;
						r_val |= 0x80;
						bitcnt++;
					}
				} else {
					if (r_one==6) {
						r_val=0; 
						r_one=0;
						bitcnt++;
						if (bitcnt & 7) {
							mISDN_debugprint(&bch->inst, "tiger: frame not byte aligned");
							state=HDLC_FLAG_SEARCH;
							tiger->r_err++;
#ifdef ERROR_STATISTIC
							bcs->err_inv++;
#endif
						} else {
							if (bch->debug & L1_DEB_HSCX)
								mISDN_debugprint(&bch->inst,"tiger frame end(%d,%d): fcs(%x) i (N/A)",
										 i,j,tiger->r_fcs/*, bcs->cs->hw.njet.irqstat0*/);
							if (tiger->r_fcs == PPP_GOODFCS) {
								got_frame(bch, (bitcnt>>3)-3);
							} else {
								if (bch->debug) {
									mISDN_debugprint(&bch->inst, "tiger FCS error");
									tiger->r_err++;
								}
#ifdef ERROR_STATISTIC
							bcs->err_crc++;
#endif
							}
							state=HDLC_FLAG_FOUND;
						}
						bitcnt=0;
					} else if (r_one==5) {
						val >>= 1;
						r_one=0;
						continue;
					} else {
						r_val >>= 1;
						r_val &= 0x7f;
						bitcnt++;
					}
					r_one=0;	
				}
				if ((state == HDLC_FRAME_FOUND) &&
					!(bitcnt & 7)) {
					if ((bitcnt>>3)>=MAX_DATA_MEM) {
						mISDN_debugprint(&bch->inst, "tiger: frame too big");
						r_val=0; 
						state=HDLC_FLAG_SEARCH;
						tiger->r_err++;
#ifdef ERROR_STATISTIC
						bcs->err_inv++;
#endif
					} else {
						skb_ptr[(bitcnt>>3)-1] = r_val;
						tiger->r_fcs = 
							PPP_FCS (tiger->r_fcs, r_val);
					}
				}
			}
			val >>= 1;
		}
		tiger->r_tot++;
	}
	tiger->r_state = state;
	tiger->r_one = r_one;
	tiger->r_val = r_val;
	tiger->r_bitcnt = bitcnt;
}

static void
read_tiger (netjet_t *card, uint8_t irq_stat)
{
	u_int *p;
	int cnt = NETJET_DMA_RXSIZE/2;
	channel_t *bch;
	u_long flags;
	
	if ((irq_stat & card->last_is0) & NETJET_IRQM0_READ_MASK) {
		printk ("netjet: tiger warn read double dma %x/%x",
			irq_stat, card->last_is0);
		return;
	} else {
		card->last_is0 &= ~NETJET_IRQM0_READ_MASK;
		card->last_is0 |= (irq_stat & NETJET_IRQM0_READ_MASK);
	}	
	if (irq_stat & NETJET_IRQM0_READ_1) {
		p = card->tiger[0].rec.dmabuf + NETJET_DMA_RXSIZE - 1;
	} else {
		p = card->tiger[0].rec.dmabuf + cnt - 1;
	}

	/* Note: Unhandled 56K */

	bch = &card->bch[0];
	spin_lock_irqsave(bch->inst.hwlock, flags);
	if (test_bit(FLG_HDLC, &bch->Flags)) {
		read_raw(bch, p, cnt);
	}
	if (test_bit(FLG_TRANSPARENT, &bch->Flags)) {
		read_raw_transparent(bch, p, cnt);
	}
	spin_unlock_irqrestore(bch->inst.hwlock, flags);

	bch = &card->bch[1];
	spin_lock_irqsave(bch->inst.hwlock, flags);
	if (test_bit(FLG_HDLC, &bch->Flags)) {
		read_raw(bch, p, cnt);
	}
	if (test_bit(FLG_TRANSPARENT, &bch->Flags)) {
		read_raw_transparent(bch, p, cnt);
	}
	spin_unlock_irqrestore(bch->inst.hwlock, flags);
}


void
write_tiger (netjet_t *card, uint8_t irq_stat)
{
	u_int *p, cnt = NETJET_DMA_TXSIZE/2;

	if ((irq_stat & card->last_is0) & NETJET_IRQM0_WRITE_MASK) {
		DPRINT ("netjet: tiger warn write double dma %x/%x",
			irq_stat, card->last_is0);
		DPRINT(KERN_WARNING "%s: done A\n", __FUNCTION__);
		return;
	} else {
		card->last_is0 &= ~NETJET_IRQM0_WRITE_MASK;
		card->last_is0 |= (irq_stat & NETJET_IRQM0_WRITE_MASK);
	}	
	if (irq_stat & NETJET_IRQM0_WRITE_1)
		p = card->tiger[0].send.dmabuf + NETJET_DMA_TXSIZE - 1;
	else
		p = card->tiger[0].send.dmabuf + cnt - 1;

	write_tiger_bch(&card->bch[0], p, cnt);
	write_tiger_bch(&card->bch[1], p, cnt);

}

static void
write_tiger_bch(channel_t *bch, u_int *buf, int cnt) {
	u_long flags;
	mISDN_head_t *hh;

	spin_lock_irqsave(bch->inst.hwlock, flags);

	if (!test_bit(FLG_TRANSPARENT, &bch->Flags)
	    && !test_bit(FLG_HDLC, &bch->Flags)) {
		spin_unlock_irqrestore(bch->inst.hwlock, flags);
		if (test_bit(FLG_TX_BUSY, &bch->Flags))
			printk(KERN_WARNING "%s: (busy)\n", __FUNCTION__);
		if (test_bit(FLG_TX_NEXT, &bch->Flags))
			printk(KERN_WARNING "%s: (next)\n", __FUNCTION__);
		return;
	}

	write_raw(bch, buf, cnt);

	if (!test_bit(FLG_TX_BUSY, &bch->Flags) && test_and_clear_bit(FLG_TX_NEXT, &bch->Flags)) {
		bch->tx_skb = bch->next_skb;
		bch->tx_idx = 0;
		bch->next_skb = NULL;
		if (bch->tx_skb) {
			test_and_set_bit(FLG_TX_BUSY, &bch->Flags);
			hh = mISDN_HEAD_P(bch->tx_skb);
			queue_ch_frame(bch, CONFIRM, hh->dinfo, NULL);
			netjet_fill_dma(bch);
		} else {
			//bch->tx_len = 0;
			printk(KERN_WARNING "hdlc tx irq TX_NEXT without skb\n");
			test_and_set_bit(FLG_TX_BUSY, &bch->Flags);
		}
	}
	spin_unlock_irqrestore(bch->inst.hwlock, flags);
}

static irqreturn_t
nj_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	netjet_t *card = dev_id;
	u_int8_t val, s1val, s0val;

	spin_lock(&card->lock);

	s0val = inb (card->base | NETJET_IRQSTAT0);
	s1val = inb (card->base | NETJET_IRQSTAT1);
	if ( ((s1val & NETJET_ISACIRQ) != 0) && (s0val == 0)) {
		/* no interrupts for us */
		/* shared IRQ */
		spin_unlock(&card->lock);
		return IRQ_NONE;
	}

	card->irqcnt++;

	if (!(s1val & NETJET_ISACIRQ)) {
		val = nj_readISAC(card, ISAC_ISTA);
		if (val) {
			mISDN_isac_interrupt(&card->dch, val);
			nj_writeISAC(card, ISAC_MASK, 0xFF);
			nj_writeISAC(card, ISAC_MASK, 0x0);
		}
	}

	if (s0val) {
		/* write to clear */
		outb (s0val, card->base | NETJET_IRQSTAT0);
	}

	/* set bits in sval to indicate which page is free */
	if (inl(card->base | NETJET_DMA_WRITE_ADR) <
	    inl(card->base | NETJET_DMA_WRITE_IRQ)) {
		/* the 2nd write page is free */
		s0val = 0x08;
	} else	{
		/* the 1st write page is free */
		s0val = 0x04;	
	}
	if (inl(card->base | NETJET_DMA_READ_ADR) <
	    inl(card->base | NETJET_DMA_READ_IRQ)) {
		/* the 2nd read page is free */
		s0val |= 0x02;
	} else {
		/* the 1st read page is free */
		s0val |= 0x01;	
	}

	/* test if we have a DMA interrupt */
	if (s0val != card->last_is0) {
		if ((s0val & NETJET_IRQM0_READ_MASK) !=
		    (card->last_is0 & NETJET_IRQM0_READ_MASK)) {
			/* got a read dma int */

			read_tiger (card, s0val);
		}
		if ((s0val & NETJET_IRQM0_WRITE_MASK) !=
		    (card->last_is0 & NETJET_IRQM0_WRITE_MASK)) {
			/* got a write dma int */
			write_tiger (card, s0val);
		}
	}

	spin_unlock(&card->lock);

	return IRQ_HANDLED;
}


static int 
nj_init_card (netjet_t *card)
{
	u_long		flags;
	int ret;

	if (request_irq(card->irq, nj_interrupt, SA_SHIRQ, "NETjet", card)) {
		printk(KERN_WARNING "mISDN: couldn't get interrupt %d\n",
		       card->irq);
		return (-EIO);
	}

	nj_reset (card);

	spin_lock_irqsave(&card->lock, flags);
	mISDN_clear_isac (&card->dch);

	if ((ret=mISDN_isac_init (&card->dch))) {
		printk(KERN_WARNING "mISDN: mISDN_isac_init failed with %d\n", ret);
		/* XXX return or retry ? */
		spin_unlock_irqrestore(&card->lock, flags);
		return (-EIO);
	}

	spin_unlock_irqrestore(&card->lock, flags);

	inittiger (card);

	mode_tiger(&card->bch[0], 0, -1);
	mode_tiger(&card->bch[1], 1, -1);

	return 0;
}


static void
nj_release_card(netjet_t *card)
{
	u_long		flags;

	nj_disable_hwirq(card);
	spin_lock_irqsave(&card->lock, flags);
 	mode_tiger(&card->bch[0], 0, ISDN_PID_NONE);
 	mode_tiger(&card->bch[1], 1, ISDN_PID_NONE);
	mISDN_isac_free(&card->dch);
	spin_unlock_irqrestore(&card->lock, flags);
	free_irq(card->irq, card);
	spin_lock_irqsave(&card->lock, flags);
	release_region(card->base, 256);
	mISDN_freechannel(&card->bch[1]);
	mISDN_freechannel(&card->bch[0]);
	mISDN_freechannel(&card->dch);
	spin_unlock_irqrestore(&card->lock, flags);
	mISDN_ctrl(&card->dch.inst, MGR_UNREGLAYER | REQUEST, NULL);
	spin_lock_irqsave(&netjet_mISDN.lock, flags);
	list_del(&card->list);
	spin_unlock_irqrestore(&netjet_mISDN.lock, flags);

	pci_disable_device(card->pdev);
	pci_set_drvdata(card->pdev, NULL);
	kfree(card);
}


static int
setup_netjet (netjet_t *card)
{
	u_int bytecnt;

	bytecnt = 256; // NOTE: we use this size in release_card

	if (!request_region(card->base, bytecnt, "netjet-s isdn")) {
		printk(KERN_WARNING
		       "mISDN: NETjet config port %#lx-%#lx already in use\n",
		       card->base,
		       card->base + bytecnt -1);
		return (-EIO);
	}

	/* Register D-Channel (ISAC) callbacks */

	card->dch.read_reg = &nj_readISAC;
	card->dch.write_reg = &nj_writeISAC;
	card->dch.read_fifo = &nj_readISACfifo;
	card->dch.write_fifo = &nj_writeISACfifo;
	card->dch.type = ISAC_TYPE_ISAC;

	card->dch.hw = &card->isac;

	return 0;
}


static int __devinit
setup_instance (netjet_t *card)
{
	int		i, err;
	mISDN_pid_t	pid;
	u_long		flags;
	
	DPRINT(KERN_WARNING "NETJet setup_instance: protocol is %x layermask is %x\n",
	       protocol[0], layermask[0]);

	spin_lock_irqsave(&netjet_mISDN.lock, flags);
	list_add_tail(&card->list, &netjet_mISDN.ilist);
	spin_unlock_irqrestore(&netjet_mISDN.lock, flags);
	card->dch.debug = debug;
	spin_lock_init(&card->lock);
	card->dch.inst.hwlock = &card->lock;
	card->dch.inst.class_dev.dev = &card->pdev->dev;
	card->dch.inst.pid.layermask = ISDN_LAYER(0);
	card->dch.inst.pid.protocol[0] = ISDN_PID_L0_TE_S0;
	mISDN_init_instance(&card->dch.inst, &netjet_mISDN, card, mISDN_ISAC_l1hw);
	sprintf(card->dch.inst.name, "NETJet%d", netjet_cnt+1);
	mISDN_set_dchannel_pid(&pid, protocol[netjet_cnt], layermask[netjet_cnt]);
	mISDN_initchannel(&card->dch, MSK_INIT_DCHANNEL, MAX_DFRAME_LEN_L1);
	for (i=0; i<2; i++) {
		card->bch[i].channel = i;
		mISDN_init_instance(&card->bch[i].inst, &netjet_mISDN, card, tiger_l2l1B);
		card->bch[i].inst.pid.layermask = ISDN_LAYER(0);
		card->bch[i].inst.hwlock = &card->lock;
		card->bch[i].inst.class_dev.dev = &card->pdev->dev;
		card->bch[i].debug = debug;
		sprintf(card->bch[i].inst.name, "%s B%d", card->dch.inst.name, i+1);
		mISDN_initchannel(&card->bch[i], MSK_INIT_BCHANNEL, MAX_DATA_MEM);
		card->bch[i].hw = &card->tiger[i];
	}
	printk(KERN_DEBUG "NETJet card %p dch %p bch1 %p bch2 %p\n",
		card, &card->dch, &card->bch[0], &card->bch[1]);
	err = setup_netjet(card);
	if (err) {
		mISDN_freechannel(&card->dch);
		mISDN_freechannel(&card->bch[1]);
		mISDN_freechannel(&card->bch[0]);
		spin_lock_irqsave(&netjet_mISDN.lock, flags);
		list_del(&card->list);
		spin_unlock_irqrestore(&netjet_mISDN.lock, flags);
		kfree(card);
		return(err);
	}
	netjet_cnt++;
	err = mISDN_ctrl(NULL, MGR_NEWSTACK | REQUEST, &card->dch.inst);

	if (err) {
		nj_release_card(card);
		return(err);
	}
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
	err = nj_init_card(card);
	if (err) {
		mISDN_ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
		return(err);
	}
	mISDN_ctrl(card->dch.inst.st, MGR_CTRLREADY | INDICATION, NULL);
	printk(KERN_INFO "NETJet %d cards installed\n", netjet_cnt);
	return(0);
}


static int __devinit 
nj_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int err = -ENOMEM;
	int cfg;
	netjet_t *card;

	if (!(card = kmalloc(sizeof(netjet_t), GFP_ATOMIC))) {
		printk(KERN_ERR "No kmem for netjet\n");
		return(err);
	}
	memset(card, 0, sizeof(netjet_t));

	card->pdev = pdev;

	err = pci_enable_device(pdev);
	if (err) {
		kfree(card);
		return(err);
	}

	printk(KERN_INFO "nj_probe(mISDN): found adapter %s at %s\n",
	       (char *) ent->driver_data, pci_name(pdev));

	pci_set_master (pdev);

	/* the TJ300 and TJ320 must be detected, the IRQ handling is different
	 * unfortunately the chips use the same device ID, but the TJ320 has
	 * the bit20 in status PCI cfg register set
	 */
	pci_read_config_dword(pdev, 0x04, &cfg);
	if (cfg & 0x00100000)
		card->subtype = NETJET_S_TJ320;
	else
		card->subtype = NETJET_S_TJ300;

	card->base = pci_resource_start(pdev, 0);
	card->irq = pdev->irq;
	pci_set_drvdata(pdev, card);
	err = setup_instance(card);
	if (err)
		pci_set_drvdata(pdev, NULL);

	return (err);
}


static void __devexit nj_remove(struct pci_dev *pdev)
{
	netjet_t *card = pci_get_drvdata(pdev);
	
	if (card)
		mISDN_ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST, NULL);
	else
		printk(KERN_WARNING "%s drvdata already removed\n", __FUNCTION__);
}

static struct pci_device_id nj_pci_ids[] __devinitdata = {
	{ PCI_VENDOR_ID_TIGERJET, PCI_DEVICE_ID_TIGERJET_300,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, (unsigned long) "NETJet S" },
	{ }
};
MODULE_DEVICE_TABLE(pci, nj_pci_ids);

static struct pci_driver nj_pci_driver = {
	name: "netjet",
	probe: nj_probe,
	remove: __devexit_p(nj_remove),
	id_table: nj_pci_ids,
};



#ifdef MODULE
MODULE_AUTHOR("Daniel Potts");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
MODULE_PARM(debug, "1i");
MODULE_PARM(protocol, MODULE_PARM_T);
MODULE_PARM(layermask, MODULE_PARM_T);
#endif

static int __init nj_init (void)
{
	int err;

	printk(KERN_INFO "Traverse Tech. NETjet-S driver, revision %s\n", mISDN_getrev(netjet_rev));

#ifdef MODULE
	netjet_mISDN.owner = THIS_MODULE;
#endif
	spin_lock_init(&netjet_mISDN.lock);
	INIT_LIST_HEAD(&netjet_mISDN.ilist);
	netjet_mISDN.name = "NETjet-S";
	netjet_mISDN.own_ctrl = nj_manager;
	netjet_mISDN.DPROTO.protocol[0] = ISDN_PID_L0_TE_S0;
	netjet_mISDN.BPROTO.protocol[1] = ISDN_PID_L1_B_64TRANS |
					   ISDN_PID_L1_B_64HDLC;
	netjet_mISDN.BPROTO.protocol[2] = ISDN_PID_L2_B_TRANS;

	if ((err = mISDN_register (&netjet_mISDN))) {
		printk(KERN_ERR "Can't register NETJet PCI error(%d)\n", err);
		return err;
	}

	err = pci_register_driver (&nj_pci_driver);
	if (err < 0)
		return err;

	return 0;
}


static void __exit nj_cleanup (void)
{
	int err;
	netjet_t *card, *next;

	if ((err = mISDN_unregister (&netjet_mISDN))) {
		printk(KERN_ERR "Can't unregister NETJet PCI error(%d)\n", err);
	}

	list_for_each_entry_safe(card, next, &netjet_mISDN.ilist, list) {
		printk(KERN_ERR "NetJet PCI card struct not empty refs %d\n",
		       netjet_mISDN.refcnt);
		nj_release_card(card);
	}

	pci_unregister_driver (&nj_pci_driver);

}

module_init (nj_init);
module_exit (nj_cleanup);

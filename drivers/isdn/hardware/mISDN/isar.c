/* $Id: isar.c,v 0.7 2001/03/03 18:17:15 kkeil Exp $
 *
 * isar.c   ISAR (Siemens PSB 7110) specific routines
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#define __NO_VERSION__
#include <linux/delay.h>
#include "hisax_hw.h"
#include "isar.h"
#include "hisaxl1.h"
#include "debug.h"

#define DBG_LOADFIRM	0
#define DUMP_MBOXFRAME	2

#define MIN(a,b) ((a<b)?a:b)

#define DLE	0x10
#define ETX	0x03


const u_char faxmodulation_s[] = "3,24,48,72,73,74,96,97,98,121,122,145,146"; 
const u_char faxmodulation[] = {3,24,48,72,73,74,96,97,98,121,122,145,146}; 
#define FAXMODCNT 13

void isar_setup(bchannel_t *);
static void isar_pump_cmd(bchannel_t *, int, u_char);
static inline void deliver_status(bchannel_t *, int);

static int firmwaresize = 0;
static u_char *firmware;
static u_char *fw_p;

static inline int
waitforHIA(bchannel_t *bch, int timeout)
{

	while ((bch->BC_Read_Reg(bch->inst.data, 0, ISAR_HIA) & 1) && timeout) {
		udelay(1);
		timeout--;
	}
	if (!timeout)
		printk(KERN_WARNING "HiSax: ISAR waitforHIA timeout\n");
	return(timeout);
}


int
sendmsg(bchannel_t *bch, u_char his, u_char creg, u_char len,
	u_char *msg)
{
	int i;
	
	if (!waitforHIA(bch, 4000))
		return(0);
#if DUMP_MBOXFRAME
	if (bch->debug & L1_DEB_HSCX)
		debugprint(&bch->inst, "sendmsg(%02x,%02x,%d)", his, creg, len);
#endif
	bch->BC_Write_Reg(bch->inst.data, 0, ISAR_CTRL_H, creg);
	bch->BC_Write_Reg(bch->inst.data, 0, ISAR_CTRL_L, len);
	bch->BC_Write_Reg(bch->inst.data, 0, ISAR_WADR, 0);
	if (msg && len) {
		bch->BC_Write_Reg(bch->inst.data, 1, ISAR_MBOX, msg[0]);
		for (i=1; i<len; i++)
			bch->BC_Write_Reg(bch->inst.data, 2, ISAR_MBOX, msg[i]);
#if DUMP_MBOXFRAME>1
		if (bch->debug & L1_DEB_HSCX_FIFO) {
			char tmp[256], *t;
			
			i = len;
			while (i>0) {
				t = tmp;
				t += sprintf(t, "sendmbox cnt %d", len);
				QuickHex(t, &msg[len-i], (i>64) ? 64:i);
				debugprint(&bch->inst, tmp);
				i -= 64;
			}
		}
#endif
	}
	bch->BC_Write_Reg(bch->inst.data, 1, ISAR_HIS, his);
	waitforHIA(bch, 10000);
	return(1);
}

/* Call only with IRQ disabled !!! */
inline void
rcv_mbox(bchannel_t *bch, struct isar_reg *ireg, u_char *msg)
{
	int i;

	bch->BC_Write_Reg(bch->inst.data, 1, ISAR_RADR, 0);
	if (msg && ireg->clsb) {
		msg[0] = bch->BC_Read_Reg(bch->inst.data, 1, ISAR_MBOX);
		for (i=1; i < ireg->clsb; i++)
			 msg[i] = bch->BC_Read_Reg(bch->inst.data, 2, ISAR_MBOX);
#if DUMP_MBOXFRAME>1
		if (bch->debug & L1_DEB_HSCX_FIFO) {
			char tmp[256], *t;
			
			i = ireg->clsb;
			while (i>0) {
				t = tmp;
				t += sprintf(t, "rcv_mbox cnt %d", ireg->clsb);
				QuickHex(t, &msg[ireg->clsb-i], (i>64) ? 64:i);
				debugprint(&bch->inst, tmp);
				i -= 64;
			}
		}
#endif
	}
	bch->BC_Write_Reg(bch->inst.data, 1, ISAR_IIA, 0);
}

/* Call only with IRQ disabled !!! */
inline void
get_irq_infos(bchannel_t *bch, struct isar_reg *ireg)
{
	ireg->iis = bch->BC_Read_Reg(bch->inst.data, 1, ISAR_IIS);
	ireg->cmsb = bch->BC_Read_Reg(bch->inst.data, 1, ISAR_CTRL_H);
	ireg->clsb = bch->BC_Read_Reg(bch->inst.data, 1, ISAR_CTRL_L);
#if DUMP_MBOXFRAME
	if (bch->debug & L1_DEB_HSCX)
		debugprint(&bch->inst, "rcv_mbox(%02x,%02x,%d)", ireg->iis, ireg->cmsb,
			ireg->clsb);
#endif
}

int
waitrecmsg(bchannel_t *bch, u_char *len,
	u_char *msg, int maxdelay)
{
	int timeout = 0;
	struct isar_reg *ir = bch->hw.isar.reg;
	
	
	while((!(bch->BC_Read_Reg(bch->inst.data, 0, ISAR_IRQBIT) & ISAR_IRQSTA)) &&
		(timeout++ < maxdelay))
		udelay(1);
	if (timeout >= maxdelay) {
		printk(KERN_WARNING"isar recmsg IRQSTA timeout\n");
		return(0);
	}
	get_irq_infos(bch, ir);
	rcv_mbox(bch, ir, msg);
	*len = ir->clsb;
	return(1);
}

int
ISARVersion(bchannel_t *bch, char *s)
{
	int ver;
	u_char msg[] = ISAR_MSG_HWVER;
	u_char tmp[64];
	u_char len;
	int debug;

//	bch->cardmsg(bch->inst.data, CARD_RESET,  NULL);
	/* disable ISAR IRQ */
	bch->BC_Write_Reg(bch->inst.data, 0, ISAR_IRQBIT, 0);
	debug = bch->debug;
	bch->debug &= ~(L1_DEB_HSCX | L1_DEB_HSCX_FIFO);
	if (!sendmsg(bch, ISAR_HIS_VNR, 0, 3, msg))
		return(-1);
	if (!waitrecmsg(bch, &len, tmp, 100000))
		 return(-2);
	bch->debug = debug;
	if (bch->hw.isar.reg->iis == ISAR_IIS_VNR) {
		if (len == 1) {
			ver = tmp[0] & 0xf;
			printk(KERN_INFO "%s ISAR version %d\n", s, ver);
			return(ver);
		}
		return(-3);
	}
	return(-4);
}

int
isar_load_firmware(bchannel_t *bch, u_char *buf, int size)
{
	int ret, cnt, debug;
	u_char len, nom, noc;
	u_short sadr, left, *sp;
	u_char *p = buf;
	u_char *msg, *tmpmsg, *mp, tmp[64];
	u_long flags;
	struct isar_reg *ireg = bch->hw.isar.reg;
	
	struct {u_short sadr;
		u_short len;
		u_short d_key;
	} *blk_head;
		
	bch->inst.lock(bch->inst.data);
#define	BLK_HEAD_SIZE 6
	if (1 != (ret = ISARVersion(bch, "Testing"))) {
		printk(KERN_ERR"isar_load_firmware wrong isar version %d\n", ret);
		return(1);
	}
	debug = bch->debug;
#if DBG_LOADFIRM<2
	bch->debug &= ~(L1_DEB_HSCX | L1_DEB_HSCX_FIFO);
#endif
	printk(KERN_DEBUG"isar_load_firmware buf %#lx\n", (u_long)buf);
	printk(KERN_DEBUG"isar_load_firmware size: %d\n", size);
	cnt = 0;
	/* disable ISAR IRQ */
	bch->BC_Write_Reg(bch->inst.data, 0, ISAR_IRQBIT, 0);
	if (!(msg = kmalloc(256, GFP_KERNEL))) {
		printk(KERN_ERR"isar_load_firmware no buffer\n");
		return (1);
	}
	while (cnt < size) {
		blk_head = (void *)p;
#ifdef __BIG_ENDIAN
		sadr = (blk_head->sadr & 0xff)*256 + blk_head->sadr/256;
		blk_head->sadr = sadr;
		sadr = (blk_head->len & 0xff)*256 + blk_head->len/256;
		blk_head->len = sadr;
		sadr = (blk_head->d_key & 0xff)*256 + blk_head->d_key/256;
		blk_head->d_key = sadr;
#endif /* __BIG_ENDIAN */
		cnt += BLK_HEAD_SIZE;
		p += BLK_HEAD_SIZE;
		printk(KERN_DEBUG"isar firmware block (%#x,%5d,%#x)\n",
			blk_head->sadr, blk_head->len, blk_head->d_key & 0xff);
		sadr = blk_head->sadr;
		left = blk_head->len;
		if (!sendmsg(bch, ISAR_HIS_DKEY, blk_head->d_key & 0xff, 0, NULL)) {
			printk(KERN_ERR"isar sendmsg dkey failed\n");
			ret = 1;goto reterror;
		}
		if (!waitrecmsg(bch, &len, tmp, 100000)) {
			printk(KERN_ERR"isar waitrecmsg dkey failed\n");
			ret = 1;goto reterror;
		}
		if ((ireg->iis != ISAR_IIS_DKEY) || ireg->cmsb || len) {
			printk(KERN_ERR"isar wrong dkey response (%x,%x,%x)\n",
				ireg->iis, ireg->cmsb, len);
			ret = 1;goto reterror;
		}
		while (left>0) {
			noc = MIN(126, left);
			nom = 2*noc;
			mp  = msg;
			*mp++ = sadr / 256;
			*mp++ = sadr % 256;
			left -= noc;
			*mp++ = noc;
			tmpmsg = p;
			p += nom;
			cnt += nom;
			nom += 3;
			sp = (u_short *)tmpmsg;
#if DBG_LOADFIRM
			printk(KERN_DEBUG"isar: load %3d words at %04x\n",
				 noc, sadr);
#endif
			sadr += noc;
			while(noc) {
#ifdef __BIG_ENDIAN
				*mp++ = *sp % 256;
				*mp++ = *sp / 256;
#else
				*mp++ = *sp / 256;
				*mp++ = *sp % 256;
#endif /* __BIG_ENDIAN */
				sp++;
				noc--;
			}
			if (!sendmsg(bch, ISAR_HIS_FIRM, 0, nom, msg)) {
				printk(KERN_ERR"isar sendmsg prog failed\n");
				ret = 1;goto reterror;
			}
			if (!waitrecmsg(bch, &len, tmp, 100000)) {
				printk(KERN_ERR"isar waitrecmsg prog failed\n");
				ret = 1;goto reterror;
			}
			if ((ireg->iis != ISAR_IIS_FIRM) || ireg->cmsb || len) {
				printk(KERN_ERR"isar wrong prog response (%x,%x,%x)\n",
					ireg->iis, ireg->cmsb, len);
				ret = 1;goto reterror;
			}
		}
		printk(KERN_DEBUG"isar firmware block %5d words loaded\n",
			blk_head->len);
	}
	/* 10ms delay */
	cnt = 10;
	while (cnt--)
		udelay(1000);
	msg[0] = 0xff;
	msg[1] = 0xfe;
	ireg->bstat = 0;
	if (!sendmsg(bch, ISAR_HIS_STDSP, 0, 2, msg)) {
		printk(KERN_ERR"isar sendmsg start dsp failed\n");
		ret = 1;goto reterror;
	}
	if (!waitrecmsg(bch, &len, tmp, 100000)) {
		printk(KERN_ERR"isar waitrecmsg start dsp failed\n");
		ret = 1;goto reterror;
	}
	if ((ireg->iis != ISAR_IIS_STDSP) || ireg->cmsb || len) {
		printk(KERN_ERR"isar wrong start dsp response (%x,%x,%x)\n",
			ireg->iis, ireg->cmsb, len);
		ret = 1;goto reterror;
	} else
		printk(KERN_DEBUG"isar start dsp success\n");
	/* NORMAL mode entered */
	/* Enable IRQs of ISAR */
	bch->BC_Write_Reg(bch->inst.data, 0, ISAR_IRQBIT, ISAR_IRQSTA);
	bch->inst.unlock(bch->inst.data);
	save_flags(flags);
	sti();
	cnt = 1000; /* max 1s */
	while ((!ireg->bstat) && cnt) {
		mdelay(1);
		cnt--;
	}
 	if (!cnt) {
		printk(KERN_ERR"isar no general status event received\n");
		ret = 1;
		goto reterrflg;
	} else {
		printk(KERN_DEBUG"isar general status event %x\n",
			ireg->bstat);
	}
	/* 10ms delay */
	cnt = 10;
	while (cnt--)
		mdelay(1);
	ireg->iis = 0;
	bch->inst.lock(bch->inst.data);
	if (!sendmsg(bch, ISAR_HIS_DIAG, ISAR_CTRL_STST, 0, NULL)) {
		printk(KERN_ERR"isar sendmsg self tst failed\n");
		ret = 1;goto reterrflg_l;
	}
	bch->inst.unlock(bch->inst.data);
	cnt = 10000; /* max 100 ms */
	while ((ireg->iis != ISAR_IIS_DIAG) && cnt) {
		udelay(10);
		cnt--;
	}
	mdelay(1);
	if (!cnt) {
		printk(KERN_ERR"isar no self tst response\n");
		ret = 1;goto reterrflg;
	}
	if ((ireg->cmsb == ISAR_CTRL_STST) && (ireg->clsb == 1)
		&& (ireg->par[0] == 0)) {
		printk(KERN_DEBUG"isar selftest OK\n");
	} else {
		printk(KERN_DEBUG"isar selftest not OK %x/%x/%x\n",
			ireg->cmsb, ireg->clsb, ireg->par[0]);
		ret = 1;goto reterrflg;
	}
	bch->inst.lock(bch->inst.data);
	ireg->iis = 0;
	if (!sendmsg(bch, ISAR_HIS_DIAG, ISAR_CTRL_SWVER, 0, NULL)) {
		printk(KERN_ERR"isar RQST SVN failed\n");
		ret = 1;goto reterrflg_l;
	}
	bch->inst.unlock(bch->inst.data);
	cnt = 30000; /* max 300 ms */
	while ((ireg->iis != ISAR_IIS_DIAG) && cnt) {
		udelay(10);
		cnt--;
	}
	mdelay(1);
	if (!cnt) {
		printk(KERN_ERR"isar no SVN response\n");
		ret = 1;goto reterrflg;
	} else {
		if ((ireg->cmsb == ISAR_CTRL_SWVER) && (ireg->clsb == 1))
			printk(KERN_DEBUG"isar software version %#x\n",
				ireg->par[0]);
		else {
			printk(KERN_ERR"isar wrong swver response (%x,%x) cnt(%d)\n",
				ireg->cmsb, ireg->clsb, cnt);
			ret = 1;goto reterrflg;
		}
	}
	bch->debug = debug;
	bch->inst.lock(bch->inst.data);
	isar_setup(bch);
	ret = 0;
reterrflg_l:
	bch->inst.unlock(bch->inst.data);
reterrflg:
	restore_flags(flags);
	bch->inst.lock(bch->inst.data);
reterror:
	bch->debug = debug;
	if (ret)
		/* disable ISAR IRQ */
		bch->BC_Write_Reg(bch->inst.data, 0, ISAR_IRQBIT, 0);
	bch->inst.unlock(bch->inst.data);
	kfree(msg);
	return(ret);
}

#define B_LL_READY	8
#define B_LL_NOCARRIER	9
#define B_LL_CONNECT	10
#define B_LL_OK		11
#define B_LL_FCERROR	12
#define B_TOUCH_TONE	13

static void
isar_bh(bchannel_t *bch)
{
	if (!bch)
		return;
	if (!bch->inst.up.func) {
		printk(KERN_WARNING "HiSax: isar_bh without up.func\n");
		return;
	}
	if (test_and_clear_bit(B_XMTBUFREADY, &bch->event)) {
		struct sk_buff *skb = bch->next_skb;

		if (skb) {
			bch->next_skb = NULL;
			bch->inst.up.func(&bch->inst.up, PH_DATA_CNF,
				DINFO_SKB, 0, skb);
		} else
			printk(KERN_WARNING "B_XMTBUFREADY without skb\n");
	}
	if (test_and_clear_bit(B_RCVBUFREADY, &bch->event)) {
		struct sk_buff	*skb;
		int		err;
		hisaxif_t	*upif;

		while ((skb = skb_dequeue(&bch->rqueue))) {
			if (!(upif = &bch->inst.up)) {
				dev_kfree_skb(skb);
				continue;
			}
			err = upif->func(upif, PH_DATA_IND, DINFO_SKB, 0, skb);
			if (err < 0) {
				printk(KERN_WARNING "HiSax: isar deliver err %d\n",
					err);
				dev_kfree_skb(skb);
			}
		}
	}
	if (test_and_clear_bit(B_LL_READY, &bch->event))
		deliver_status(bch, HW_MOD_OK);
	if (test_and_clear_bit(B_LL_NOCARRIER, &bch->event))
		deliver_status(bch, HW_MOD_NOCARR);
	if (test_and_clear_bit(B_LL_CONNECT, &bch->event))
		deliver_status(bch, HW_MOD_CONNECT);
	if (test_and_clear_bit(B_LL_OK, &bch->event))
		deliver_status(bch, HW_MOD_OK);
	if (test_and_clear_bit(B_LL_FCERROR, &bch->event))
		deliver_status(bch, HW_MOD_FCERROR);
	if (test_and_clear_bit(B_TOUCH_TONE, &bch->event)) {
		int tt = bch->conmsg[0];

		tt |= TOUCH_TONE_VAL;
		bch->inst.up.func(&bch->inst.up, PH_CONTROL | INDICATION,
			0, sizeof(int), &tt);
	}

}

static void
isar_sched_event(bchannel_t *bch, int event)
{
	test_and_set_bit(event, &bch->event);
	queue_task(&bch->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

static inline void
send_DLE_ETX(bchannel_t *bch)
{
	u_char dleetx[2] = {DLE,ETX};
	struct sk_buff *skb;
	
	if ((skb = dev_alloc_skb(2))) {
		memcpy(skb_put(skb, 2), dleetx, 2);
		skb_queue_tail(&bch->rqueue, skb);
		isar_sched_event(bch, B_RCVBUFREADY);
	} else {
		printk(KERN_WARNING "HiSax: skb out of memory\n");
	}
}

static inline int
dle_count(unsigned char *buf, int len)
{
	int count = 0;

	while (len--)
		if (*buf++ == DLE)
			count++;
	return count;
}

static inline void
insert_dle(unsigned char *dest, unsigned char *src, int count) {
	/* <DLE> in input stream have to be flagged as <DLE><DLE> */
	while (count--) {
		*dest++ = *src;
		if (*src++ == DLE)
			*dest++ = DLE;
	}
}
 
static inline void
isar_rcv_frame(bchannel_t *bch)
{
	u_char *ptr;
	struct sk_buff *skb;
	struct isar_reg *ireg = bch->hw.isar.reg;
	
	if (!ireg->clsb) {
		debugprint(&bch->inst, "isar zero len frame");
		bch->BC_Write_Reg(bch->inst.data, 1, ISAR_IIA, 0);
		return;
	}
	switch (bch->protocol) {
	    case ISDN_PID_NONE:
		debugprint(&bch->inst, "isar protocol 0 spurious IIS_RDATA %x/%x/%x",
			ireg->iis, ireg->cmsb, ireg->clsb);
		printk(KERN_WARNING"isar protocol 0 spurious IIS_RDATA %x/%x/%x\n",
			ireg->iis, ireg->cmsb, ireg->clsb);
		bch->BC_Write_Reg(bch->inst.data, 1, ISAR_IIA, 0);
		break;
	    case ISDN_PID_L1_B_64TRANS:
	    case ISDN_PID_L1_B_TRANS_TT:
	    case ISDN_PID_L1_B_TRANS_TTR:
	    case ISDN_PID_L1_B_TRANS_TTS:
	    case ISDN_PID_L1_B_V32:
		if ((skb = dev_alloc_skb(ireg->clsb))) {
			rcv_mbox(bch, ireg, (u_char *)skb_put(skb, ireg->clsb));
			skb_queue_tail(&bch->rqueue, skb);
			isar_sched_event(bch, B_RCVBUFREADY);
		} else {
			printk(KERN_WARNING "HiSax: skb out of memory\n");
			bch->BC_Write_Reg(bch->inst.data, 1, ISAR_IIA, 0);
		}
		break;
	    case ISDN_PID_L1_B_64HDLC:
		if ((bch->rx_idx + ireg->clsb) > MAX_DATA_MEM) {
			if (bch->debug & L1_DEB_WARN)
				debugprint(&bch->inst, "isar_rcv_frame: incoming packet too large");
			bch->BC_Write_Reg(bch->inst.data, 1, ISAR_IIA, 0);
			bch->rx_idx = 0;
		} else if (ireg->cmsb & HDLC_ERROR) {
			if (bch->debug & L1_DEB_WARN)
				debugprint(&bch->inst, "isar frame error %x len %d",
					ireg->cmsb, ireg->clsb);
#ifdef ERROR_STATISTIC
			if (ireg->cmsb & HDLC_ERR_RER)
				bch->err_inv++;
			if (ireg->cmsb & HDLC_ERR_CER)
				bch->err_crc++;
#endif
			bch->rx_idx = 0;
			bch->BC_Write_Reg(bch->inst.data, 1, ISAR_IIA, 0);
		} else {
			if (ireg->cmsb & HDLC_FSD)
				bch->rx_idx = 0;
			ptr = bch->rx_buf + bch->rx_idx;
			bch->rx_idx += ireg->clsb;
			rcv_mbox(bch, ireg, ptr);
			if (ireg->cmsb & HDLC_FED) {
				if (bch->rx_idx < 3) { /* last 2 bytes are the FCS */
					if (bch->debug & L1_DEB_WARN)
						debugprint(&bch->inst, "isar frame to short %d",
							bch->rx_idx);
				} else if (!(skb = dev_alloc_skb(bch->rx_idx-2))) {
					printk(KERN_WARNING "ISAR: receive out of memory\n");
				} else {
					memcpy(skb_put(skb, bch->rx_idx-2),
						bch->rx_buf, bch->rx_idx-2);
					skb_queue_tail(&bch->rqueue, skb);
					isar_sched_event(bch, B_RCVBUFREADY);
				}
				bch->rx_idx = 0;
			}
		}
		break;
	case ISDN_PID_L1_B_FAX:
		if (bch->hw.isar.state != STFAX_ACTIV) {
			if (bch->debug & L1_DEB_WARN)
				debugprint(&bch->inst, "isar_rcv_frame: not ACTIV");
			bch->BC_Write_Reg(bch->inst.data, 1, ISAR_IIA, 0);
			bch->rx_idx = 0;
			break;
		}
		if (bch->hw.isar.cmd == PCTRL_CMD_FRM) {
			rcv_mbox(bch, ireg, bch->rx_buf);
			bch->rx_idx = ireg->clsb +
				dle_count(bch->rx_buf, ireg->clsb);
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "isar_rcv_frame: raw(%d) dle(%d)",
					ireg->clsb, bch->rx_idx);
			if ((skb = dev_alloc_skb(bch->rx_idx))) {
				insert_dle((u_char *)skb_put(skb, bch->rx_idx),
					bch->rx_buf, ireg->clsb);
				skb_queue_tail(&bch->rqueue, skb);
				isar_sched_event(bch, B_RCVBUFREADY);
				if (ireg->cmsb & SART_NMD) { /* ABORT */
					if (bch->debug & L1_DEB_WARN)
						debugprint(&bch->inst, "isar_rcv_frame: no more data");
					bch->BC_Write_Reg(bch->inst.data, 1, ISAR_IIA, 0);
					bch->rx_idx = 0;
					send_DLE_ETX(bch);
					sendmsg(bch, SET_DPS(bch->hw.isar.dpath) |
						ISAR_HIS_PUMPCTRL, PCTRL_CMD_ESC,
						0, NULL);
					bch->hw.isar.state = STFAX_ESCAPE;
					isar_sched_event(bch, B_LL_NOCARRIER);
				}
			} else {
				printk(KERN_WARNING "HiSax: skb out of memory\n");
				bch->BC_Write_Reg(bch->inst.data, 1, ISAR_IIA, 0);
			}
			break;
		}
		if (bch->hw.isar.cmd != PCTRL_CMD_FRH) {
			if (bch->debug & L1_DEB_WARN)
				debugprint(&bch->inst, "isar_rcv_frame: unknown fax mode %x",
					bch->hw.isar.cmd);
			bch->BC_Write_Reg(bch->inst.data, 1, ISAR_IIA, 0);
			bch->rx_idx = 0;
			break;
		}
		/* PCTRL_CMD_FRH */
		if ((bch->rx_idx + ireg->clsb) > MAX_DATA_MEM) {
			if (bch->debug & L1_DEB_WARN)
				debugprint(&bch->inst, "isar_rcv_frame: incoming packet too large");
			bch->BC_Write_Reg(bch->inst.data, 1, ISAR_IIA, 0);
			bch->rx_idx = 0;
		} else if (ireg->cmsb & HDLC_ERROR) {
			if (bch->debug & L1_DEB_WARN)
				debugprint(&bch->inst, "isar frame error %x len %d",
					ireg->cmsb, ireg->clsb);
			bch->rx_idx = 0;
			bch->BC_Write_Reg(bch->inst.data, 1, ISAR_IIA, 0);
		} else {
			if (ireg->cmsb & HDLC_FSD)
				bch->rx_idx = 0;
			ptr = bch->rx_buf + bch->rx_idx;
			bch->rx_idx += ireg->clsb;
			rcv_mbox(bch, ireg, ptr);
			if (ireg->cmsb & HDLC_FED) {
				int len = bch->rx_idx +
					dle_count(bch->rx_buf, bch->rx_idx);
				if (bch->rx_idx < 3) { /* last 2 bytes are the FCS */
					if (bch->debug & L1_DEB_WARN)
						debugprint(&bch->inst, "isar frame to short %d",
							bch->rx_idx);
				} else if (!(skb = dev_alloc_skb(bch->rx_idx))) {
					printk(KERN_WARNING "ISAR: receive out of memory\n");
				} else {
					insert_dle((u_char *)skb_put(skb, len),
						bch->rx_buf,
						bch->rx_idx);
					skb_queue_tail(&bch->rqueue, skb);
					isar_sched_event(bch, B_RCVBUFREADY);
					send_DLE_ETX(bch);
					isar_sched_event(bch, B_LL_OK);
				}
				bch->rx_idx = 0;
			}
		}
		if (ireg->cmsb & SART_NMD) { /* ABORT */
			if (bch->debug & L1_DEB_WARN)
				debugprint(&bch->inst, "isar_rcv_frame: no more data");
			bch->BC_Write_Reg(bch->inst.data, 1, ISAR_IIA, 0);
			bch->rx_idx = 0;
			send_DLE_ETX(bch);
			sendmsg(bch, SET_DPS(bch->hw.isar.dpath) |
				ISAR_HIS_PUMPCTRL, PCTRL_CMD_ESC, 0, NULL);
			bch->hw.isar.state = STFAX_ESCAPE;
			isar_sched_event(bch, B_LL_NOCARRIER);
		}
		break;
	default:
		printk(KERN_ERR"isar_rcv_frame protocol (%x)error\n", bch->protocol);
		bch->BC_Write_Reg(bch->inst.data, 1, ISAR_IIA, 0);
		break;
	}
}

void
isar_fill_fifo(bchannel_t *bch)
{
	int count;
	u_char msb;
	u_char *ptr;

	if ((bch->debug & L1_DEB_HSCX) && !(bch->debug & L1_DEB_HSCX_FIFO))
		debugprint(&bch->inst, "isar_fill_fifo");
	count = bch->tx_len - bch->tx_idx;
	if (count <= 0)
		return;
	if (!(bch->hw.isar.reg->bstat & 
		(bch->hw.isar.dpath == 1 ? BSTAT_RDM1 : BSTAT_RDM2)))
		return;
	if (count > bch->hw.isar.mml) {
		msb = 0;
		count = bch->hw.isar.mml;
	} else {
		msb = HDLC_FED;
	}
	ptr = bch->tx_buf + bch->tx_idx;
	bch->tx_idx += count;
	if (!bch->hw.isar.txcnt) {
		msb |= HDLC_FST;
		if ((bch->protocol == ISDN_PID_L1_B_FAX) &&
			(bch->hw.isar.cmd == PCTRL_CMD_FTH)) {
			if (count > 1) {
				if ((ptr[0]== 0xff) && (ptr[1] == 0x13))
					/* last frame */
					test_and_set_bit(BC_FLG_LASTDATA,
						&bch->Flag);
			}  
		}
	}
	bch->hw.isar.txcnt += count;
	switch (bch->protocol) {
		case ISDN_PID_NONE:
			printk(KERN_ERR"isar_fill_fifo wrong protocol 0\n");
			break;
		case ISDN_PID_L1_B_64TRANS:
		case ISDN_PID_L1_B_TRANS_TTR:
		case ISDN_PID_L1_B_TRANS_TTS:
		case ISDN_PID_L1_B_V32:
			sendmsg(bch, SET_DPS(bch->hw.isar.dpath) | ISAR_HIS_SDATA,
				0, count, ptr);
			break;
		case ISDN_PID_L1_B_64HDLC:
			sendmsg(bch, SET_DPS(bch->hw.isar.dpath) | ISAR_HIS_SDATA,
				msb, count, ptr);
			break;
		case ISDN_PID_L1_B_FAX:
			if (bch->hw.isar.state != STFAX_ACTIV) {
				if (bch->debug & L1_DEB_WARN)
					debugprint(&bch->inst, "isar_fill_fifo: not ACTIV");
			} else if (bch->hw.isar.cmd == PCTRL_CMD_FTH) { 
				sendmsg(bch, SET_DPS(bch->hw.isar.dpath) | ISAR_HIS_SDATA,
					msb, count, ptr);
			} else if (bch->hw.isar.cmd == PCTRL_CMD_FTM) {
				sendmsg(bch, SET_DPS(bch->hw.isar.dpath) | ISAR_HIS_SDATA,
					0, count, ptr);
			} else {
				if (bch->debug & L1_DEB_WARN)
					debugprint(&bch->inst, "isar_fill_fifo: not FTH/FTM");
			}
			break;
		default:
			if (bch->debug)
				debugprint(&bch->inst, "isar_fill_fifo protocol(%x) error", bch->protocol);
			printk(KERN_ERR"isar_fill_fifo protocol(%x) error\n", bch->protocol);
			break;
	}
}

inline
bchannel_t *sel_bch_isar(bchannel_t *bch, u_char dpath)
{
	if ((!dpath) || (dpath == 3))
		return(NULL);
	if (bch[0].hw.isar.dpath == dpath)
		return(&bch[0]);
	if (bch[1].hw.isar.dpath == dpath)
		return(&bch[1]);
	return(NULL);
}

inline void
send_frames(bchannel_t *bch)
{
	if (bch->tx_len - bch->tx_idx) {
		isar_fill_fifo(bch);
	} else {
		if (bch->protocol == ISDN_PID_L1_B_FAX) {
			if (bch->hw.isar.cmd == PCTRL_CMD_FTH) {
				if (test_bit(BC_FLG_LASTDATA, &bch->Flag)) {
					test_and_set_bit(BC_FLG_NMD_DATA, &bch->Flag);
				}
			} else if (bch->hw.isar.cmd == PCTRL_CMD_FTM) {
				if (test_bit(BC_FLG_DLEETX, &bch->Flag)) {
					test_and_set_bit(BC_FLG_LASTDATA, &bch->Flag);
					test_and_set_bit(BC_FLG_NMD_DATA, &bch->Flag);
				}
			}
		}
		bch->hw.isar.txcnt = 0;
		if (test_and_clear_bit(FLG_TX_NEXT, &bch->Flag)) {
			if (bch->next_skb) {
				bch->tx_len = bch->next_skb->len;
				memcpy(bch->tx_buf,
					bch->next_skb->data, bch->tx_len);
				bch->tx_idx = 0;
				isar_fill_fifo(bch);
				isar_sched_event(bch, B_XMTBUFREADY);
			} else {
				printk(KERN_WARNING "isar tx irq TX_NEXT without skb\n");
				test_and_clear_bit(FLG_TX_BUSY, &bch->Flag);
			}
		} else {
			if (test_and_clear_bit(BC_FLG_DLEETX, &bch->Flag)) {
				if (test_and_clear_bit(BC_FLG_LASTDATA, &bch->Flag)) {
					if (test_and_clear_bit(BC_FLG_NMD_DATA, &bch->Flag)) {
						u_char dummy = 0;
						sendmsg(bch, SET_DPS(bch->hw.isar.dpath) |
							ISAR_HIS_SDATA, 0x01, 1, &dummy);
					}
					test_and_set_bit(BC_FLG_LL_OK, &bch->Flag);
				} else {
					isar_sched_event(bch, B_LL_CONNECT);
				}
			}
			test_and_clear_bit(FLG_TX_BUSY, &bch->Flag);
			isar_sched_event(bch, B_XMTBUFREADY);
		}
	}
}

inline void
check_send(bchannel_t *bch, u_char rdm)
{
	bchannel_t *bc;
	
	if (rdm & BSTAT_RDM1) {
		if ((bc = sel_bch_isar(bch, 1))) {
			if (bc->protocol) {
				send_frames(bc);
			}
		}
	}
	if (rdm & BSTAT_RDM2) {
		if ((bc = sel_bch_isar(bch, 2))) {
			if (bc->protocol) {
				send_frames(bc);
			}
		}
	}
	
}

const char *dmril[] = {"NO SPEED", "1200/75", "NODEF2", "75/1200", "NODEF4",
			"300", "600", "1200", "2400", "4800", "7200",
			"9600nt", "9600t", "12000", "14400", "WRONG"};
const char *dmrim[] = {"NO MOD", "NO DEF", "V32/V32b", "V22", "V21",
			"Bell103", "V23", "Bell202", "V17", "V29", "V27ter"};

static void
isar_pump_status_rsp(bchannel_t *bch, struct isar_reg *ireg) {
	u_char ril = ireg->par[0];
	u_char rim;

	if (!test_and_clear_bit(ISAR_RATE_REQ, &bch->hw.isar.reg->Flags))
		return; 
	if (ril > 14) {
		if (bch->debug & L1_DEB_WARN)
			debugprint(&bch->inst, "wrong pstrsp ril=%d",ril);
		ril = 15;
	}
	switch(ireg->par[1]) {
		case 0:
			rim = 0;
			break;
		case 0x20:
			rim = 2;
			break;
		case 0x40:
			rim = 3;
			break;
		case 0x41:
			rim = 4;
			break;
		case 0x51:
			rim = 5;
			break;
		case 0x61:
			rim = 6;
			break;
		case 0x71:
			rim = 7;
			break;
		case 0x82:
			rim = 8;
			break;
		case 0x92:
			rim = 9;
			break;
		case 0xa2:
			rim = 10;
			break;
		default:
			rim = 1;
			break;
	}
	sprintf(bch->hw.isar.conmsg,"%s %s", dmril[ril], dmrim[rim]);
	bch->conmsg = bch->hw.isar.conmsg;
	if (bch->debug & L1_DEB_HSCX)
		debugprint(&bch->inst, "pump strsp %s", bch->conmsg);
}

static void
isar_pump_statev_modem(bchannel_t *bch, u_char devt) {
	u_char dps = SET_DPS(bch->hw.isar.dpath);

	switch(devt) {
		case PSEV_10MS_TIMER:
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "pump stev TIMER");
			break;
		case PSEV_CON_ON:
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "pump stev CONNECT");
			isar_sched_event(bch, B_LL_CONNECT);
			break;
		case PSEV_CON_OFF:
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "pump stev NO CONNECT");
			sendmsg(bch, dps | ISAR_HIS_PSTREQ, 0, 0, NULL);
			isar_sched_event(bch, B_LL_NOCARRIER);
			break;
		case PSEV_V24_OFF:
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "pump stev V24 OFF");
			break;
		case PSEV_CTS_ON:
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "pump stev CTS ON");
			break;
		case PSEV_CTS_OFF:
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "pump stev CTS OFF");
			break;
		case PSEV_DCD_ON:
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "pump stev CARRIER ON");
			test_and_set_bit(ISAR_RATE_REQ, &bch->hw.isar.reg->Flags); 
			sendmsg(bch, dps | ISAR_HIS_PSTREQ, 0, 0, NULL);
			break;
		case PSEV_DCD_OFF:
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "pump stev CARRIER OFF");
			break;
		case PSEV_DSR_ON:
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "pump stev DSR ON");
			break;
		case PSEV_DSR_OFF:
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "pump stev DSR_OFF");
			break;
		case PSEV_REM_RET:
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "pump stev REMOTE RETRAIN");
			break;
		case PSEV_REM_REN:
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "pump stev REMOTE RENEGOTIATE");
			break;
		case PSEV_GSTN_CLR:
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "pump stev GSTN CLEAR", devt);
			break;
		default:
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "unknown pump stev %x", devt);
			break;
	}
}

static inline void
deliver_status(bchannel_t *bch, int status)
{
	if (bch->debug & L1_DEB_HSCX)
		debugprint(&bch->inst, "HL->LL FAXIND %x", status);
	bch->inst.up.func(&bch->inst.up, PH_STATUS | INDICATION, 0,
		sizeof(int), &status);
}

static void
isar_pump_statev_fax(bchannel_t *bch, u_char devt) {
	u_char dps = SET_DPS(bch->hw.isar.dpath);
	u_char p1;

	switch(devt) {
		case PSEV_10MS_TIMER:
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "pump stev TIMER");
			break;
		case PSEV_RSP_READY:
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "pump stev RSP_READY");
			bch->hw.isar.state = STFAX_READY;
			isar_sched_event(bch, B_LL_READY);
			if (test_bit(BC_FLG_ORIG, &bch->Flag)) {
				isar_pump_cmd(bch, HW_MOD_FRH, 3);
			} else {
				isar_pump_cmd(bch, HW_MOD_FTH, 3);
			}
			break;
		case PSEV_LINE_TX_H:
			if (bch->hw.isar.state == STFAX_LINE) {
				if (bch->debug & L1_DEB_HSCX)
					debugprint(&bch->inst, "pump stev LINE_TX_H");
				bch->hw.isar.state = STFAX_CONT;
				sendmsg(bch, dps | ISAR_HIS_PUMPCTRL, PCTRL_CMD_CONT, 0, NULL);
			} else {
				if (bch->debug & L1_DEB_WARN)
					debugprint(&bch->inst, "pump stev LINE_TX_H wrong st %x",
						bch->hw.isar.state);
			}
			break;
		case PSEV_LINE_RX_H:
			if (bch->hw.isar.state == STFAX_LINE) {
				if (bch->debug & L1_DEB_HSCX)
					debugprint(&bch->inst, "pump stev LINE_RX_H");
				bch->hw.isar.state = STFAX_CONT;
				sendmsg(bch, dps | ISAR_HIS_PUMPCTRL, PCTRL_CMD_CONT, 0, NULL);
			} else {
				if (bch->debug & L1_DEB_WARN)
					debugprint(&bch->inst, "pump stev LINE_RX_H wrong st %x",
						bch->hw.isar.state);
			}
			break;
		case PSEV_LINE_TX_B:
			if (bch->hw.isar.state == STFAX_LINE) {
				if (bch->debug & L1_DEB_HSCX)
					debugprint(&bch->inst, "pump stev LINE_TX_B");
				bch->hw.isar.state = STFAX_CONT;
				sendmsg(bch, dps | ISAR_HIS_PUMPCTRL, PCTRL_CMD_CONT, 0, NULL);
			} else {
				if (bch->debug & L1_DEB_WARN)
					debugprint(&bch->inst, "pump stev LINE_TX_B wrong st %x",
						bch->hw.isar.state);
			}
			break;
		case PSEV_LINE_RX_B:
			if (bch->hw.isar.state == STFAX_LINE) {
				if (bch->debug & L1_DEB_HSCX)
					debugprint(&bch->inst, "pump stev LINE_RX_B");
				bch->hw.isar.state = STFAX_CONT;
				sendmsg(bch, dps | ISAR_HIS_PUMPCTRL, PCTRL_CMD_CONT, 0, NULL);
			} else {
				if (bch->debug & L1_DEB_WARN)
					debugprint(&bch->inst, "pump stev LINE_RX_B wrong st %x",
						bch->hw.isar.state);
			}
			break;
		case PSEV_RSP_CONN:
			if (bch->hw.isar.state == STFAX_CONT) {
				if (bch->debug & L1_DEB_HSCX)
					debugprint(&bch->inst, "pump stev RSP_CONN");
				bch->hw.isar.state = STFAX_ACTIV;
				test_and_set_bit(ISAR_RATE_REQ, &bch->hw.isar.reg->Flags);
				sendmsg(bch, dps | ISAR_HIS_PSTREQ, 0, 0, NULL);
				if (bch->hw.isar.cmd == PCTRL_CMD_FTH) {
					/* 1s Flags before data */
					if (test_and_set_bit(BC_FLG_FTI_RUN, &bch->Flag))
						del_timer(&bch->hw.isar.ftimer);
					/* 1000 ms */
					bch->hw.isar.ftimer.expires =
						jiffies + ((1000 * HZ)/1000);
					test_and_set_bit(BC_FLG_LL_CONN,
						&bch->Flag);
					add_timer(&bch->hw.isar.ftimer);
				} else {
					isar_sched_event(bch, B_LL_CONNECT);
				}
			} else {
				if (bch->debug & L1_DEB_WARN)
					debugprint(&bch->inst, "pump stev RSP_CONN wrong st %x",
						bch->hw.isar.state);
			}
			break;
		case PSEV_FLAGS_DET:
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "pump stev FLAGS_DET");
			break;
		case PSEV_RSP_DISC:
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "pump stev RSP_DISC");
			if (bch->hw.isar.state == STFAX_ESCAPE) {
				switch(bch->hw.isar.newcmd) {
					case 0:
						bch->hw.isar.state = STFAX_READY;
						break;
					case PCTRL_CMD_FTH:
					case PCTRL_CMD_FTM:
						p1 = 10;
						sendmsg(bch, dps | ISAR_HIS_PUMPCTRL,
							PCTRL_CMD_SILON, 1, &p1);
						bch->hw.isar.state = STFAX_SILDET;
						break;
					case PCTRL_CMD_FRH:
					case PCTRL_CMD_FRM:
						p1 = bch->hw.isar.mod = bch->hw.isar.newmod;
						bch->hw.isar.newmod = 0;
						bch->hw.isar.cmd = bch->hw.isar.newcmd;
						bch->hw.isar.newcmd = 0;
						sendmsg(bch, dps | ISAR_HIS_PUMPCTRL,
							bch->hw.isar.cmd, 1, &p1);
						bch->hw.isar.state = STFAX_LINE;
						bch->hw.isar.try_mod = 3;
						break;
					default:
						if (bch->debug & L1_DEB_HSCX)
							debugprint(&bch->inst, "RSP_DISC unknown newcmd %x", bch->hw.isar.newcmd);
						break;
				}
			} else if (bch->hw.isar.state == STFAX_ACTIV) {
				if (test_and_clear_bit(BC_FLG_LL_OK, &bch->Flag)) {
					isar_sched_event(bch, B_LL_OK);
				} else if (bch->hw.isar.cmd == PCTRL_CMD_FRM) {
					send_DLE_ETX(bch);
					isar_sched_event(bch, B_LL_NOCARRIER);
				} else {
					isar_sched_event(bch, B_LL_FCERROR);
				}
				bch->hw.isar.state = STFAX_READY;
			} else {
				bch->hw.isar.state = STFAX_READY;
				isar_sched_event(bch, B_LL_FCERROR);
			}
			break;
		case PSEV_RSP_SILDET:
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "pump stev RSP_SILDET");
			if (bch->hw.isar.state == STFAX_SILDET) {
				p1 = bch->hw.isar.mod = bch->hw.isar.newmod;
				bch->hw.isar.newmod = 0;
				bch->hw.isar.cmd = bch->hw.isar.newcmd;
				bch->hw.isar.newcmd = 0;
				sendmsg(bch, dps | ISAR_HIS_PUMPCTRL,
					bch->hw.isar.cmd, 1, &p1);
				bch->hw.isar.state = STFAX_LINE;
				bch->hw.isar.try_mod = 3;
			}
			break;
		case PSEV_RSP_SILOFF:
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "pump stev RSP_SILOFF");
			break;
		case PSEV_RSP_FCERR:
			if (bch->hw.isar.state == STFAX_LINE) {
				if (bch->debug & L1_DEB_HSCX)
					debugprint(&bch->inst, "pump stev RSP_FCERR try %d",
						bch->hw.isar.try_mod);
				if (bch->hw.isar.try_mod--) {
					sendmsg(bch, dps | ISAR_HIS_PUMPCTRL,
						bch->hw.isar.cmd, 1,
						&bch->hw.isar.mod);
					break;
				}
			}
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "pump stev RSP_FCERR");
			bch->hw.isar.state = STFAX_ESCAPE;
			sendmsg(bch, dps | ISAR_HIS_PUMPCTRL, PCTRL_CMD_ESC, 0, NULL);
			isar_sched_event(bch, B_LL_FCERROR);
			break;
		default:
			break;
	}
}

static char debbuf[128];

void
isar_int_main(bchannel_t *bch)
{
	struct isar_reg *ireg = bch->hw.isar.reg;
	bchannel_t *bc;

	get_irq_infos(bch, ireg);
	switch (ireg->iis & ISAR_IIS_MSCMSD) {
		case ISAR_IIS_RDATA:
			if ((bc = sel_bch_isar(bch, ireg->iis >> 6))) {
				isar_rcv_frame(bc);
			} else {
				debugprint(&bch->inst, "isar spurious IIS_RDATA %x/%x/%x",
					ireg->iis, ireg->cmsb, ireg->clsb);
				bch->BC_Write_Reg(bch->inst.data, 1, ISAR_IIA, 0);
			}
			break;
		case ISAR_IIS_GSTEV:
			bch->BC_Write_Reg(bch->inst.data, 1, ISAR_IIA, 0);
			ireg->bstat |= ireg->cmsb;
			check_send(bch, ireg->cmsb);
			break;
		case ISAR_IIS_BSTEV:
#ifdef ERROR_STATISTIC
			if ((bc = sel_bch_isar(bch, ireg->iis >> 6))) {
				if (ireg->cmsb == BSTEV_TBO)
					bc->err_tx++;
				if (ireg->cmsb == BSTEV_RBO)
					bc->err_rdo++;
			}
#endif
			if (bch->debug & L1_DEB_WARN)
				debugprint(&bch->inst, "Buffer STEV dpath%d msb(%x)",
					ireg->iis>>6, ireg->cmsb);
			bch->BC_Write_Reg(bch->inst.data, 1, ISAR_IIA, 0);
			break;
		case ISAR_IIS_PSTEV:
			if ((bc = sel_bch_isar(bch, ireg->iis >> 6))) {
				rcv_mbox(bc, ireg, (u_char *)ireg->par);
				if (bc->protocol == ISDN_PID_L1_B_V32) {
					isar_pump_statev_modem(bc, ireg->cmsb);
				} else if (bc->protocol == ISDN_PID_L1_B_FAX) {
					isar_pump_statev_fax(bc, ireg->cmsb);
				} else if (bc->protocol == ISDN_PID_L1_B_TRANS_TTR) {
					bc->hw.isar.conmsg[0] = ireg->cmsb;
					bc->conmsg = bc->hw.isar.conmsg;
					isar_sched_event(bc, B_TOUCH_TONE);
				} else {
					if (bch->debug & L1_DEB_WARN)
						debugprint(&bch->inst, "isar IIS_PSTEV pmode %d stat %x",
							bc->protocol, ireg->cmsb);
				}
			} else {
				debugprint(&bch->inst, "isar spurious IIS_PSTEV %x/%x/%x",
					ireg->iis, ireg->cmsb, ireg->clsb);
				bch->BC_Write_Reg(bch->inst.data, 1, ISAR_IIA, 0);
			}
			break;
		case ISAR_IIS_PSTRSP:
			if ((bc = sel_bch_isar(bch, ireg->iis >> 6))) {
				rcv_mbox(bc, ireg, (u_char *)ireg->par);
				isar_pump_status_rsp(bc, ireg);
			} else {
				debugprint(&bch->inst, "isar spurious IIS_PSTRSP %x/%x/%x",
					ireg->iis, ireg->cmsb, ireg->clsb);
				bch->BC_Write_Reg(bch->inst.data, 1, ISAR_IIA, 0);
			}
			break;
		case ISAR_IIS_DIAG:
		case ISAR_IIS_BSTRSP:
		case ISAR_IIS_IOM2RSP:
			rcv_mbox(bch, ireg, (u_char *)ireg->par);
			if ((bch->debug & (L1_DEB_HSCX | L1_DEB_HSCX_FIFO))
				== L1_DEB_HSCX) {
				u_char *tp=debbuf;

				tp += sprintf(debbuf, "msg iis(%x) msb(%x)",
					ireg->iis, ireg->cmsb);
				QuickHex(tp, (u_char *)ireg->par, ireg->clsb);
				debugprint(&bch->inst, debbuf);
			}
			break;
		case ISAR_IIS_INVMSG:
			rcv_mbox(bch, ireg, debbuf);
			if (bch->debug & L1_DEB_WARN)
				debugprint(&bch->inst, "invalid msg his:%x",
					ireg->cmsb);
			break;
		default:
			rcv_mbox(bch, ireg, debbuf);
			if (bch->debug & L1_DEB_WARN)
				debugprint(&bch->inst, "unhandled msg iis(%x) ctrl(%x/%x)",
					ireg->iis, ireg->cmsb, ireg->clsb);
			break;
	}
}

static void
ftimer_handler(bchannel_t *bch) {
	if (bch->debug)
		debugprint(&bch->inst, "ftimer flags %04x",
			bch->Flag);
	test_and_clear_bit(BC_FLG_FTI_RUN, &bch->Flag);
	if (test_and_clear_bit(BC_FLG_LL_CONN, &bch->Flag)) {
		isar_sched_event(bch, B_LL_CONNECT);
	}
}

static void
setup_pump(bchannel_t *bch) {
	u_char dps = SET_DPS(bch->hw.isar.dpath);
	u_char ctrl, param[6];

	switch (bch->protocol) {
		case ISDN_PID_NONE:
		case ISDN_PID_L1_B_64TRANS:
		case ISDN_PID_L1_B_64HDLC:
			sendmsg(bch, dps | ISAR_HIS_PUMPCFG, PMOD_BYPASS, 0, NULL);
			break;
		case ISDN_PID_L1_B_TRANS_TTS:
			param[0] = 5; /* TOA 5 db */
			sendmsg(bch, dps | ISAR_HIS_PUMPCFG, PMOD_DTMF_TRANS, 1, param);
			break;
		case ISDN_PID_L1_B_TRANS_TTR:
			param[0] = 40; /* REL -46 dbm */
			sendmsg(bch, dps | ISAR_HIS_PUMPCFG, PMOD_DTMF, 1, param);
			break;
		case ISDN_PID_L1_B_V32:
			ctrl = PMOD_DATAMODEM;
			if (test_bit(BC_FLG_ORIG, &bch->Flag)) {
				ctrl |= PCTRL_ORIG;
				param[5] = PV32P6_CTN;
			} else {
				param[5] = PV32P6_ATN;
			}
			param[0] = 6; /* 6 db */
			param[1] = PV32P2_V23R | PV32P2_V22A | PV32P2_V22B |
				   PV32P2_V22C | PV32P2_V21 | PV32P2_BEL; 
			param[2] = PV32P3_AMOD | PV32P3_V32B | PV32P3_V23B;
			param[3] = PV32P4_UT144;
			param[4] = PV32P5_UT144;
			sendmsg(bch, dps | ISAR_HIS_PUMPCFG, ctrl, 6, param);
			break;
		case ISDN_PID_L1_B_FAX:
			ctrl = PMOD_FAX;
			if (test_bit(BC_FLG_ORIG, &bch->Flag)) {
				ctrl |= PCTRL_ORIG;
				param[1] = PFAXP2_CTN;
			} else {
				param[1] = PFAXP2_ATN;
			}
			param[0] = 6; /* 6 db */
			sendmsg(bch, dps | ISAR_HIS_PUMPCFG, ctrl, 2, param);
			bch->hw.isar.state = STFAX_NULL;
			bch->hw.isar.newcmd = 0;
			bch->hw.isar.newmod = 0;
			test_and_set_bit(BC_FLG_FTI_RUN, &bch->Flag);
			break;
	}
	udelay(1000);
	sendmsg(bch, dps | ISAR_HIS_PSTREQ, 0, 0, NULL);
	udelay(1000);
}

static void
setup_sart(bchannel_t *bch) {
	u_char dps = SET_DPS(bch->hw.isar.dpath);
	u_char ctrl, param[2];
	
	switch (bch->protocol) {
		case ISDN_PID_NONE:
			sendmsg(bch, dps | ISAR_HIS_SARTCFG, SMODE_DISABLE, 0,
				NULL);
			break;
		case ISDN_PID_L1_B_64TRANS:
		case ISDN_PID_L1_B_TRANS_TT:
		case ISDN_PID_L1_B_TRANS_TTR:
		case ISDN_PID_L1_B_TRANS_TTS:
			sendmsg(bch, dps | ISAR_HIS_SARTCFG, SMODE_BINARY, 2,
				"\1\0");
			break;
		case ISDN_PID_L1_B_64HDLC:
		case ISDN_PID_L1_B_FAX:
			param[0] = 0;
			sendmsg(bch, dps | ISAR_HIS_SARTCFG, SMODE_HDLC, 1,
				param);
			break;
		case ISDN_PID_L1_B_V32:
			ctrl = SMODE_V14 | SCTRL_HDMC_BOTH;
			param[0] = S_P1_CHS_8;
			param[1] = S_P2_BFT_DEF;
			sendmsg(bch, dps | ISAR_HIS_SARTCFG, ctrl, 2,
				param);
			break;
	}
	udelay(1000);
	sendmsg(bch, dps | ISAR_HIS_BSTREQ, 0, 0, NULL);
	udelay(1000);
}

static void
setup_iom2(bchannel_t *bch) {
	u_char dps = SET_DPS(bch->hw.isar.dpath);
	u_char cmsb = IOM_CTRL_ENA, msg[5] = {IOM_P1_TXD,0,0,0,0};
	
	if (bch->channel)
		msg[1] = msg[3] = 1;
	switch (bch->protocol) {
		case ISDN_PID_NONE:
			cmsb = 0;
			/* dummy slot */
			msg[1] = msg[3] = bch->hw.isar.dpath + 2;
			break;
		case ISDN_PID_L1_B_64TRANS:
		case ISDN_PID_L1_B_64HDLC:
			break;
		case ISDN_PID_L1_B_V32:
		case ISDN_PID_L1_B_FAX:
		case ISDN_PID_L1_B_TRANS_TTS:
			cmsb |= IOM_CTRL_RCV;
		case ISDN_PID_L1_B_TRANS_TTR:
			cmsb |= IOM_CTRL_ALAW;
			break;
	}
	sendmsg(bch, dps | ISAR_HIS_IOM2CFG, cmsb, 5, msg);
	udelay(1000);
	sendmsg(bch, dps | ISAR_HIS_IOM2REQ, 0, 0, NULL);
	udelay(1000);
}

static int
modeisar(bchannel_t *bch, int channel, u_int bprotocol, u_char *param)
{
	/* Here we are selecting the best datapath for requested protocol */
	if(bch->protocol == ISDN_PID_NONE) { /* New Setup */
		bch->channel = channel;
		switch (bprotocol) {
			case ISDN_PID_NONE: /* init */
				if (!bch->hw.isar.dpath)
					/* no init for dpath 0 */
					return(0);
				break;
			case ISDN_PID_L1_B_64TRANS:
			case ISDN_PID_L1_B_64HDLC:
				/* best is datapath 2 */
				if (!test_and_set_bit(ISAR_DP2_USE, 
					&bch->hw.isar.reg->Flags))
					bch->hw.isar.dpath = 2;
				else if (!test_and_set_bit(ISAR_DP1_USE,
					&bch->hw.isar.reg->Flags))
					bch->hw.isar.dpath = 1;
				else {
					printk(KERN_WARNING"isar modeisar both pathes in use\n");
					return(-EINVAL);
				}
				break;
			case ISDN_PID_L1_B_V32:
			case ISDN_PID_L1_B_FAX:
			case ISDN_PID_L1_B_TRANS_TT:
			case ISDN_PID_L1_B_TRANS_TTR:
			case ISDN_PID_L1_B_TRANS_TTS:
				/* only datapath 1 */
				if (!test_and_set_bit(ISAR_DP1_USE, 
					&bch->hw.isar.reg->Flags))
					bch->hw.isar.dpath = 1;
				else {
					printk(KERN_WARNING"isar modeisar analog funktions only with DP1\n");
					debugprint(&bch->inst, "isar modeisar analog funktions only with DP1");
					return(-EBUSY);
				}
				break;
		}
	}
	if (bch->debug & L1_DEB_HSCX)
		debugprint(&bch->inst, "isar dp%d protocol %x->%x ichan %d",
			bch->hw.isar.dpath, bch->protocol, bprotocol, channel);
	bch->protocol = bprotocol;
	setup_pump(bch);
	setup_iom2(bch);
	setup_sart(bch);
	if (bch->protocol == ISDN_PID_NONE) {
		/* Clear resources */
		if (bch->hw.isar.dpath == 1)
			test_and_clear_bit(ISAR_DP1_USE, &bch->hw.isar.reg->Flags);
		else if (bch->hw.isar.dpath == 2)
			test_and_clear_bit(ISAR_DP2_USE, &bch->hw.isar.reg->Flags);
		bch->hw.isar.dpath = 0;
	}
	return(0);
}

static void
isar_pump_cmd(bchannel_t *bch, int cmd, u_char para) 
{
	u_char dps = SET_DPS(bch->hw.isar.dpath);
	u_char ctrl = 0, nom = 0, p1 = 0;

	switch(cmd) {
		case HW_MOD_FTM:
			if (bch->hw.isar.state == STFAX_READY) {
				p1 = para;
				ctrl = PCTRL_CMD_FTM;
				nom = 1;
				bch->hw.isar.state = STFAX_LINE;
				bch->hw.isar.cmd = ctrl;
				bch->hw.isar.mod = para;
				bch->hw.isar.newmod = 0;
				bch->hw.isar.newcmd = 0;
				bch->hw.isar.try_mod = 3; 
			} else if ((bch->hw.isar.state == STFAX_ACTIV) &&
				(bch->hw.isar.cmd == PCTRL_CMD_FTM) &&
				(bch->hw.isar.mod == para)) {
				isar_sched_event(bch, B_LL_CONNECT);
			} else {
				bch->hw.isar.newmod = para;
				bch->hw.isar.newcmd = PCTRL_CMD_FTM;
				nom = 0;
				ctrl = PCTRL_CMD_ESC;
				bch->hw.isar.state = STFAX_ESCAPE; 
			}
			break;
		case HW_MOD_FTH:
			if (bch->hw.isar.state == STFAX_READY) {
				p1 = para;
				ctrl = PCTRL_CMD_FTH;
				nom = 1;
				bch->hw.isar.state = STFAX_LINE;
				bch->hw.isar.cmd = ctrl;
				bch->hw.isar.mod = para;
				bch->hw.isar.newmod = 0;
				bch->hw.isar.newcmd = 0;
				bch->hw.isar.try_mod = 3; 
			} else if ((bch->hw.isar.state == STFAX_ACTIV) &&
				(bch->hw.isar.cmd == PCTRL_CMD_FTH) &&
				(bch->hw.isar.mod == para)) {
				isar_sched_event(bch, B_LL_CONNECT);
			} else {
				bch->hw.isar.newmod = para;
				bch->hw.isar.newcmd = PCTRL_CMD_FTH;
				nom = 0;
				ctrl = PCTRL_CMD_ESC;
				bch->hw.isar.state = STFAX_ESCAPE; 
			}
			break;
		case HW_MOD_FRM:
			if (bch->hw.isar.state == STFAX_READY) {
				p1 = para;
				ctrl = PCTRL_CMD_FRM;
				nom = 1;
				bch->hw.isar.state = STFAX_LINE;
				bch->hw.isar.cmd = ctrl;
				bch->hw.isar.mod = para;
				bch->hw.isar.newmod = 0;
				bch->hw.isar.newcmd = 0;
				bch->hw.isar.try_mod = 3; 
			} else if ((bch->hw.isar.state == STFAX_ACTIV) &&
				(bch->hw.isar.cmd == PCTRL_CMD_FRM) &&
				(bch->hw.isar.mod == para)) {
				isar_sched_event(bch, B_LL_CONNECT);
			} else {
				bch->hw.isar.newmod = para;
				bch->hw.isar.newcmd = PCTRL_CMD_FRM;
				nom = 0;
				ctrl = PCTRL_CMD_ESC;
				bch->hw.isar.state = STFAX_ESCAPE; 
			}
			break;
		case HW_MOD_FRH:
			if (bch->hw.isar.state == STFAX_READY) {
				p1 = para;
				ctrl = PCTRL_CMD_FRH;
				nom = 1;
				bch->hw.isar.state = STFAX_LINE;
				bch->hw.isar.cmd = ctrl;
				bch->hw.isar.mod = para;
				bch->hw.isar.newmod = 0;
				bch->hw.isar.newcmd = 0;
				bch->hw.isar.try_mod = 3; 
			} else if ((bch->hw.isar.state == STFAX_ACTIV) &&
				(bch->hw.isar.cmd == PCTRL_CMD_FRH) &&
				(bch->hw.isar.mod == para)) {
				isar_sched_event(bch, B_LL_CONNECT);
			} else {
				bch->hw.isar.newmod = para;
				bch->hw.isar.newcmd = PCTRL_CMD_FRH;
				nom = 0;
				ctrl = PCTRL_CMD_ESC;
				bch->hw.isar.state = STFAX_ESCAPE; 
			}
			break;
		case PCTRL_CMD_TDTMF:
			p1 = para;
			nom = 1;
			ctrl = PCTRL_CMD_TDTMF;
			break;
	}
	if (ctrl)
		sendmsg(bch, dps | ISAR_HIS_PUMPCTRL, ctrl, nom, &p1);
}

void
isar_setup(bchannel_t *bch)
{
	u_char msg;
	int i;
	
	/* Dpath 1, 2 */
	msg = 61;
	for (i=0; i<2; i++) {
		/* Buffer Config */
		sendmsg(bch, (i ? ISAR_HIS_DPS2 : ISAR_HIS_DPS1) |
			ISAR_HIS_P12CFG, 4, 1, &msg);
		bch[i].hw.isar.mml = msg;
		bch[i].protocol = 0;
		bch[i].hw.isar.dpath = i + 1;
		modeisar(&bch[i], i, 0, NULL);
	}
}

int
isar_down(hisaxif_t *hif, u_int prim, int dinfo, int len, void *arg)
{
	bchannel_t *bch = hif->fdata;
	int ret = 0;

	if ((prim == PH_DATA_REQ) ||
		(prim == (DL_DATA | REQUEST))) {
		struct sk_buff *skb = arg;

		if (bch->next_skb) {
			debugprint(&bch->inst, " l2l1 next_skb exist this shouldn't happen");
			return(-EBUSY);
		}
		bch->inst.lock(bch->inst.data);
		if (test_and_set_bit(FLG_TX_BUSY, &bch->Flag)) {
			test_and_set_bit(FLG_TX_NEXT, &bch->Flag);
			bch->next_skb = skb;
			bch->inst.unlock(bch->inst.data);
		} else {
			bch->tx_len = skb->len;
			memcpy(bch->tx_buf, skb->data, bch->tx_len);
			bch->tx_idx = 0;
			isar_fill_fifo(bch);
			bch->inst.unlock(bch->inst.data);
			bch->inst.up.func(&bch->inst.up, PH_DATA_CNF,
				DINFO_SKB, 0, skb);
		}
	} else if ((prim == (PH_ACTIVATE | REQUEST)) ||
		(prim == (DL_ESTABLISH  | REQUEST))) {
		test_and_set_bit(BC_FLG_ACTIV, &bch->Flag);
		bch->inst.lock(bch->inst.data);
		ret = modeisar(bch, bch->channel,
			bch->inst.pid.protocol[1], NULL);
		bch->inst.unlock(bch->inst.data);
		if (ret)
			bch->inst.up.func(&bch->inst.up, prim | CONFIRM, 0,
				ret, NULL);
		else
			bch->inst.up.func(&bch->inst.up, prim | CONFIRM, 0,
				0, NULL);
	} else if ((prim == (PH_DEACTIVATE | REQUEST)) ||
		(prim == (DL_RELEASE | REQUEST)) ||
		(prim == (MGR_DELIF | REQUEST))) {
		bch->inst.lock(bch->inst.data);
		if (test_and_clear_bit(FLG_TX_NEXT, &bch->Flag)) {
			dev_kfree_skb(bch->next_skb);
			bch->next_skb = NULL;
		}
		test_and_clear_bit(FLG_TX_BUSY, &bch->Flag);
		modeisar(bch, bch->channel, 0, NULL);
		test_and_clear_bit(BC_FLG_ACTIV, &bch->Flag);
		bch->inst.unlock(bch->inst.data);
		if (prim != (MGR_DELIF | REQUEST))
			bch->inst.up.func(&bch->inst.up, prim | CONFIRM, 0,
				0, NULL);
	} else if (prim == (PH_CONTROL | REQUEST)) {
		int  *val = arg;
		int  len;

		if (!val)
			return(-EINVAL);
		if ((*val & ~TOUCH_TONE_MASK)==TOUCH_TONE_VAL) {
			if (bch->protocol == ISDN_PID_L1_B_TRANS_TTS) {
				bch->inst.lock(bch->inst.data);
				isar_pump_cmd(bch, PCTRL_CMD_TDTMF, (*val & 0xff));
				bch->inst.unlock(bch->inst.data);
				bch->inst.up.func(&bch->inst.up, PH_CONTROL | CONFIRM,
					0, 0, 0);
			} else {
				printk(KERN_WARNING "isar_down TOUCH_TONE_SEND wrong protocol %x\n",
					bch->protocol);
			}
		} else if (*val == HW_FIRM_START) {
			val++;
			firmwaresize = *val;
			val++;
			if (!(firmware = vmalloc(firmwaresize))) {
				firmwaresize = 0;
				return(-ENOMEM);
			}
			fw_p = firmware;
			bch->inst.up.func(&bch->inst.up, PH_CONTROL | RESPONSE,
				0, 0, NULL);
		} else if (*val == HW_FIRM_DATA) {
			val++;
			len = *val++;
			memcpy(fw_p, val, len);
			fw_p += len;
			bch->inst.up.func(&bch->inst.up, PH_CONTROL | RESPONSE,
				0, 0, NULL);
		} else if (*val == HW_FIRM_END) {
			if ((fw_p - firmware) == firmwaresize)
				ret = isar_load_firmware(bch, firmware, firmwaresize);
			else {
				printk(KERN_WARNING "wrong firmware size %d/%d\n",
					fw_p - firmware, firmwaresize);
				ret = -EINVAL;
			}
			vfree(firmware);
			fw_p = firmware = NULL;
			firmwaresize = 0;
			bch->inst.up.func(&bch->inst.up, PH_CONTROL | RESPONSE,
				0, 0, NULL);
		}
	} else {
		printk(KERN_WARNING "isar_down unknown prim(%x)\n", prim);
		ret = -EINVAL;
	}
	return(ret);
}
#if 0
int
isar_auxcmd(bchannel_t *bch, isdn_ctrl *ic) {
	u_long adr;
	int features, i;
	bchannel_t *bch;

	if (bch->debug & L1_DEB_HSCX)
		debugprint(&bch->inst, "isar_auxcmd cmd/ch %x/%d", ic->command, ic->arg);
	switch (ic->command) {
		case (ISDN_CMD_FAXCMD):
			bch = cs->channel[ic->arg].bch;
			if (bch->debug & L1_DEB_HSCX)
				debugprint(&bch->inst, "isar_auxcmd cmd/subcmd %d/%d",
					ic->parm.aux.cmd, ic->parm.aux.subcmd);
			switch(ic->parm.aux.cmd) {
				case HW_MOD_CTRL:
					if (ic->parm.aux.subcmd == ETX)
						test_and_set_bit(BC_FLG_DLEETX,
							&bch->Flag);
					break;
				case HW_MOD_FRM:
				case HW_MOD_FRH:
				case HW_MOD_FTM:
				case HW_MOD_FTH:
					if (ic->parm.aux.subcmd == AT_QUERY) {
						sprintf(ic->parm.aux.para,
							"%d", bch->hw.isar.mod);
						ic->command = ISDN_STAT_FAXIND;
						ic->parm.aux.cmd = HW_MOD_QUERY;
						cs->iif.statcallb(ic);
						return(0);
					} else if (ic->parm.aux.subcmd == AT_EQ_QUERY) {
						strcpy(ic->parm.aux.para, faxmodulation_s);
						ic->command = ISDN_STAT_FAXIND;
						ic->parm.aux.cmd = HW_MOD_QUERY;
						cs->iif.statcallb(ic);
						return(0);
					} else if (ic->parm.aux.subcmd == AT_EQ_VALUE) {
						for(i=0;i<FAXMODCNT;i++)
							if (faxmodulation[i]==ic->parm.aux.para[0])
								break;
						if ((FAXMODCNT > i) && 
							test_bit(BC_FLG_INIT, &bch->Flag)) {
							isar_pump_cmd(bch,
								ic->parm.aux.cmd,
								ic->parm.aux.para[0]);
							return(0);
						}
					}
					/* wrong modulation or not activ */
					/* fall through */
				default:
					ic->command = ISDN_STAT_FAXIND;
					ic->parm.aux.cmd = HW_MOD_ERROR;
					cs->iif.statcallb(ic);
			}
			break;
		default:
			return(-EINVAL);
	}
	return(0);
}
#endif

void
free_isar(bchannel_t *bch)
{
	modeisar(bch, bch->channel, 0, NULL);
	del_timer(&bch->hw.isar.ftimer);
}


int init_isar(bchannel_t *bch)
{
	bch->tqueue.routine = (void *) (void *) isar_bh;
	bch->hw.isar.ftimer.function = (void *) ftimer_handler;
	bch->hw.isar.ftimer.data = (long) bch;
	init_timer(&bch->hw.isar.ftimer);
	return (0);
}

/*

 * l1oip.c  low level driver for tunneling layer 1 over IP
 *
 * NOTE: It is not compatible with TDMoIP nor "ISDN over IP".
 *
 * Author	Andreas Eversberg (jolly@jolly.de)
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

/* module parameters:
 * type:
	Value 1	= BRI
	Value 2	= PRI
	Value 3 = BRI (multi channel frame)
	Value 4 = PRI (multi channel frame)
	A multi channel frame reduces overhead to a single frame for all
       	b-channels, but increases delay.

 * codec:
	Value 0 = aLaw transparent
	Value 1	= uLaw transparent (instead of aLaw)
	Value 2 = aLaw to TADPCM
	Value 3	= uLaw to TADPCM

 * protocol:
 	Bit 0-3 = protocol
	Bit 4	= NT-Mode
	Bit 5	= PTP (instead of multipoint)

 * layermask:
	NOTE: Must be given for all ports, not for the number of cards.
	mask of layers to be used for D-channel stack

 * limit:
	limitation of bchannels to control bandwidth (1...29)

 * ip:
	binary representation of remote ip address (127.0.0.0 -> 0x7f000001)
	If not given, no remote address is set.

 * port:
	port number
	If not given or 0, port 931 is used.

 * id:
	mandatory value to identify frames. This value must be equal on both
	peers and should be random.

 * debug:
	NOTE: only one debug value must be given for all cards
	enable debugging (see l1oip.h for debug options)


Special PH_CONTROL messages:

 dinfo = L1OIP_SETPEER
 data bytes 0-3 : IP address in network order (MSB first)
 data bytes 4-5 : local port in network order
 data bytes 6-7 : remote port in network order

 dinfo = L1OIP_UNSETPEER

 * Use l1oipctrl for setting or removin ip address
	

L1oIP-Protocol
--------------

The Layer 1 over IP protocol tunnels frames and audio streams over IP. It will
be directly attached to the layer 2 or interconnected to layer 1 of a different
stack.

It also provides layer 1 control and keeps dynamic IP connectivity up.

Frame structure:

+---------------------------------------------------------------+
|                              ID                               |
+---------------+---------------+-------------------------------+
|    Coding     |B|  Channel    |  Time Base / Layer 1 message  |
+---------------+---------------+-------------------------------+
|                          Channel Map                          |
+---------------------------------------------------------------+
|                                                               |
.                             Data                              .
.                                                               .


The "ID" should be a random number. It makes shure that missrouted frames get
dropped due to wrong id. Also it provides simple security agains DOS attacs.

The "Coding" byte defines the data format. It can be

0	HDLC-data
1	TADPCM (table ADPCM)
2	A-law
3	u-law

The "B"-Flag shows the interface type:
0	BRI
1	PRI 

The "Channel" will give the timeslot or channel number.

0	Layer 1 message
1-2	B-Channel for BRI interface
1-15	B-Channel 1-15 for PRI interface
16	D-Channel for PRI and BRI interface
17-31	B-Channel 17-31 for PRI interface
127	B-Channels as given by "Channel Map"

The "Time Base" is used to rearange packets and to detect packet loss.
The 16 bits are sent in network order (MSB first) and count 1/8000 th of a
second. This causes a wrap arround each 8,192 seconds. There is no requirement
for the initial "Time Base", but 0 should be used for the first packet.

The "Channel Map" are 4 bytes in network order (MSB first). They only exist, if
the Channel Map was selected with the Channel value. Bits 1-31 represent the
existance of data for each channel Bit 0 is not used and shall be 0.
The length of each channel data is defined by the total number of bytes
divided by the number of bits set in the Channel Map. The coding and length must
be equal for all existing channels.
NOTE: D-Channel data must be sent via seperate frame, because length and
coding are differen. Also packet mode data should be sent in a seperate frame.

The total length of data is defined by the maximum packet size (without header).


Validity check at the receiver: Packets will be dropped if

- the length is less than 4 bytes.
- there is no data in the packet.
- the Coding is not supported. False coding should produce a warning once.
- the B-flag does not equal the expected interface type.
- the channel is out of range.
- the channel does not exists by interface. A warning should be produced once.
- the channel map is selected, but length is less than 8 bytes.
- the channel map's bit 0 is set if channel map is selected.
- the channel map is completely 0, but the packet has data anyway.
- the length of data is not a multiple of the channels indicated by channel map.
- the data exceeds the maximum frame length of the ISDN driver.
- the layer 1 message is unknown.

Layer 1 Message:

This is a special type of frame. In this case the "Time Base" contains two
bytes with the message. The first byte (MSB) contains the sequence number and the
second byte the message.

The sequence number is used to detect the reception of a message. If the message
is received, the new sequence number is acknowledged using message 0 (keepalive)
If the keepalive is received with the sequence number last sent, the next
message can be sent with incremented sequence number.

If no message is to be sent, the sequence number is not incremented and the 
last sequence number is repeated.

The keepalive is sent every 10 seconds. If a message is about to be sent, the
message is repeated every second until the keepalive is received with the 
incremented sequence number.

0,x	IP link keepalive. X is a sequence to detect packet loss.
1,1	Activate layer 1
1,0	Deactivate layer 1
2,1	AIS on (alarm on the remote interface)
2,0	AIS off
3,1	Maintainance blocked
3,0	Maintainance unblocked
16,x	Application specific information.
32,0	Announce new IP
32,1	Acknowledge new IP

IP Announcement:

One or both sides may have dynamic IP address. A simple trick is used to get
the remote IP if it changes. It is assumed, that both sides don't change their
IP at the same time.

If IP changes, the peer must announce it's new IP address. A layer 1 message
with the new IP address (4 extra bytes) and the "peer's password" (more extra
bytes).
The transmission interval is one second. 
The remote peer will receive the new IP address with the password. If the
password matches, the new IP will be used. The passwort is used to prevent
"take over" connections. An acknowledge will be generated, by the remote peer
to make the local peer stop sending "Announces".

The initial value will be given by application. It is only required for one
peer to give the initial IP address. After an IP address is given, it will be
announced.

*/


#include <linux/config.h>
#include <linux/module.h>
#include <linux/delay.h>

#include "dchannel.h"
#include "bchannel.h"
#include "layer1.h"
#include "dsp.h"
#include "debug.h"
#include <linux/isdn_compat.h>

#include "l1oip.h"

//static void ph_state_change(dchannel_t *dch);

extern const char *CardType[];

static const char *l1oip_revision = "$Revision: 1.2 $";

static int l1oip_cnt;

static mISDNobject_t	l1oip_obj;

static char l1oipName[] = "Layer1oIP";


/****************/
/* module stuff */
/****************/

#define MAX_CARDS	16
#define MODULE_CARDS_T	"1-16i"
static u_int type[MAX_CARDS];
static u_int codec[MAX_CARDS];
static u_int protocol[MAX_CARDS];
static int layermask[MAX_CARDS];
static int debug;

#ifdef MODULE
MODULE_AUTHOR("Andreas Eversberg");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
MODULE_PARM(type, MODULE_CARDS_T);
MODULE_PARM(codec, MODULE_CARDS_T);
MODULE_PARM(protocol, MODULE_CARDS_T);
MODULE_PARM(layermask, MODULE_CARDS_T);
MODULE_PARM(debug, "1i");
#endif


/********************/
/* D-channel access */
/********************/

locking bedenken
/* message transfer from layer 2
 */
static int l1oip_dchannel(mISDNinstance_t *inst, struct sk_buff *skb)
{
	dchannel_t	*dch = container_of(inst, dchannel_t, inst);
	l1oip_t		*hc;
	int		ret = 0;
	mISDN_head_t	*hh;
	u_long		flags;

	hh = mISDN_HEAD_P(skb);
	hc = dch->inst.privat;
	if (hh->prim == PH_DATA_REQ) {
		/* check oversize */
		if (skb->len <= 0) {
			printk(KERN_WARNING "%s: skb too small\n", __FUNCTION__);
			return(-EINVAL);
		}
		if (skb->len > MAX_DFRAME_LEN_L1 || skb->len > MAX_L1OIP_LEN) {
			printk(KERN_WARNING "%s: skb too large\n", __FUNCTION__);
			return(-EINVAL);
		}
		/* check for pending next_skb */
		spin_lock_irqsave(inst->hwlock, flags);
		if (dch->next_skb) {
			printk(KERN_WARNING "%s: next_skb exist ERROR (skb->len=%d next_skb->len=%d)\n",
				__FUNCTION__, skb->len, dch->next_skb->len);
			spin_unlock_irqrestore(inst->hwlock, flags);
			return(-EBUSY);
		}
		if (test_and_set_bit(FLG_TX_BUSY, &dch->DFlags)) {
			test_and_set_bit(FLG_TX_NEXT, &dch->DFlags);
			dch->next_skb = skb;
			spin_unlock_irqrestore(inst->hwlock, flags);
			return(0);
		}
		/* send/queue frame */
		l1oip_tx(hc, 16, skb, CODEC_L1OIP_DATA);
		spin_unlock_irqrestore(inst->hwlock, flags);
		skb_trim(skb, 0);
		return(mISDN_queueup_newhead(inst, 0, PH_DATA_CNF,hh->dinfo, skb));
	} else if (hh->prim == (PH_CONTROL | REQUEST)) {
		spin_lock_irqsave(inst->hwlock, flags);
		switch (hh->dinfo) {
			case L1OIP_SETPEER:
				lkkllkö
			return(mISDN_queueup_newhead(inst, 0, PH_CONTROL | INDICATION, L1OIP_SETPEER, skb));
			break;

			case L1OIP_UNSETPEER:
				lkkllkö
			return(mISDN_queueup_newhead(inst, 0, PH_CONTROL | INDICATION, L1OIP_UNSETPEER, skb));
			break;

			default:
			printk(KERN_DEBUG "%s: unknown PH_CONTROL info %x\n", __FUNCTION__, hh->dinfo);
			ret = -EINVAL;
		}
		spin_unlock_irqrestore(inst->hwlock, flags);
	} else if (hh->prim == (PH_ACTIVATE | REQUEST)) {
		if (test_bit(HFC_CFG_NTMODE, &hc->chan[dch->channel].cfg)) {
			if (debug & DEBUG_L1OIP_MSG)
				printk(KERN_DEBUG "%s: PH_ACTIVATE port %d (0..%d)\n", __FUNCTION__, hc->chan[dch->channel].port, hc->pri?30:2);
			spin_lock_irqsave(inst->hwlock, flags);
			/* start activation */
			if (pri) {
				//dchannel_sched_event(dch, D_L1STATECHANGE);
				ph_state_change(dch);
				if (debug & DEBUG_L1OIP_STATE)
					printk(KERN_DEBUG "%s: E1 report state %x \n", __FUNCTION__, dch->ph_state);
			} else {
				HFC_outb(hc, R_ST_SEL, hc->chan[dch->channel].port);
				HFC_outb(hc, A_ST_WR_STATE, V_ST_LD_STA | 1); /* G1 */
				udelay(6); /* wait at least 5,21us */
				HFC_outb(hc, A_ST_WR_STATE, 1);
				HFC_outb(hc, A_ST_WR_STATE, 1 | (V_ST_ACT*3)); /* activate */
				dch->ph_state = 1;
			}
			spin_unlock_irqrestore(inst->hwlock, flags);
		} else {
			if (debug & DEBUG_L1OIP_MSG)
				printk(KERN_DEBUG "%s: PH_ACTIVATE no NT-mode port %d (0..%d)\n", __FUNCTION__, hc->chan[dch->channel].port, hc->pri?30:2);
			ret = -EINVAL;
		}
	} else if (hh->prim == (PH_DEACTIVATE | REQUEST)) {
		if (test_bit(HFC_CFG_NTMODE, &hc->chan[dch->channel].cfg)) {
			if (debug & DEBUG_L1OIP_MSG)
				printk(KERN_DEBUG "%s: PH_DEACTIVATE port %d (0..%d)\n", __FUNCTION__, hc->chan[dch->channel].port, hc->pri?30:2);
			spin_lock_irqsave(inst->hwlock, flags);
			hw_deactivate: /* after lock */
			dch->ph_state = 0;
			/* start deactivation */
			if (hc->pri) {
			if (debug & DEBUG_L1OIP_MSG)
				printk(KERN_DEBUG "%s: PH_DEACTIVATE no BRI\n", __FUNCTION__);
			} else {
				HFC_outb(hc, R_ST_SEL, hc->chan[dch->channel].port);
				HFC_outb(hc, A_ST_WR_STATE, V_ST_ACT*2); /* deactivate */
			}
			if (dch->next_skb) {
				dev_kfree_skb(dch->next_skb);
				dch->next_skb = NULL;
			}
			dch->tx_idx = dch->tx_len = hc->chan[dch->channel].rx_idx = 0;
			test_and_clear_bit(FLG_TX_NEXT, &dch->DFlags);
			test_and_clear_bit(FLG_TX_BUSY, &dch->DFlags);
			if (test_and_clear_bit(FLG_DBUSY_TIMER, &dch->DFlags))
				del_timer(&dch->dbusytimer);
			spin_unlock_irqrestore(inst->hwlock, flags);
		} else {
			if (debug & DEBUG_L1OIP_MSG)
				printk(KERN_DEBUG "%s: PH_DEACTIVATE no NT-mode port %d (0..%d)\n", __FUNCTION__, hc->chan[dch->channel].port, hc->pri?30:2);
			ret = -EINVAL;
		}
	} else
	if (hh->prim == MGR_SHORTSTATUS) {
		if(hh->dinfo==SSTATUS_ALL || hh->dinfo==SSTATUS_L1) {
			int new_addr;
			if(hh->dinfo&SSTATUS_BROADCAST_BIT) new_addr= dch->inst.id | MSG_BROADCAST;
			else new_addr=hh->addr | FLG_MSG_TARGET;
			return(mISDN_queueup_newhead(inst, new_addr, MGR_SHORTSTATUS,(dch->l1_up) ? SSTATUS_L1_ACTIVATED : SSTATUS_L1_DEACTIVATED, skb));
		}
	} else {
		if (debug & DEBUG_L1OIP_MSG)
			printk(KERN_DEBUG "%s: unknown prim %x\n", __FUNCTION__, hh->prim);
		ret = -EINVAL;
	}
	if (!ret) {
//		printk("1\n");
		dev_kfree_skb(skb);
//		printk("2\n");
	}
	return(ret);
}


/******************************/
/* Layer2 -> Layer 1 Transfer */
/******************************/

/* messages from layer 2 to layer 1 are processed here.
 */
static int
l1oip_bchannel(mISDNinstance_t *inst, struct sk_buff *skb)
{
	u_long		flags, num;
	int		slot_tx, slot_rx, bank_tx, bank_rx;
	bchannel_t	*bch = container_of(inst, bchannel_t, inst);
	int		ret = -EINVAL;
	mISDN_head_t	*hh;
	hfc_multi_t	*hc;
	struct		dsp_features *features;

	hh = mISDN_HEAD_P(skb);
	hc = bch->inst.privat;

	if ((hh->prim == PH_DATA_REQ)
	 || (hh->prim == (DL_DATA | REQUEST))) {
		if (skb->len <= 0) {
			printk(KERN_WARNING "%s: skb too small\n", __FUNCTION__);
			return(-EINVAL);
		}
		if (skb->len > MAX_DATA_MEM) {
			printk(KERN_WARNING "%s: skb too large\n", __FUNCTION__);
			return(-EINVAL);
		}
		/* check for pending next_skb */
		spin_lock_irqsave(inst->hwlock, flags);
		if (bch->next_skb) {
			printk(KERN_WARNING "%s: next_skb exist ERROR (skb->len=%d next_skb->len=%d)\n", __FUNCTION__, skb->len, bch->next_skb->len);
			spin_unlock_irqrestore(inst->hwlock, flags);
			return(-EBUSY);
		}
		/* if we have currently a pending tx skb */
		if (test_and_set_bit(BC_FLG_TX_BUSY, &bch->Flag)) {
			test_and_set_bit(BC_FLG_TX_NEXT, &bch->Flag);
			bch->next_skb = skb;
			spin_unlock_irqrestore(inst->hwlock, flags);
			return(0);
		}
		/* write to fifo */
		bch->tx_len = skb->len;
		memcpy(bch->tx_buf, skb->data, bch->tx_len);
		bch->tx_idx = 0;
		hfcmulti_tx(hc, bch->channel, NULL, bch);
		/* start fifo */
		HFC_outb_(hc, R_FIFO, 0);
		HFC_wait_(hc);
		spin_unlock_irqrestore(inst->hwlock, flags);
#ifdef FIXME   // TODO changed
		if ((bch->inst.pid.protocol[2] == ISDN_PID_L2_B_RAWDEV)
			&& bch->dev)
			hif = &bch->dev->rport.pif;
		else
			hif = &bch->inst.up;
#endif
		skb_trim(skb, 0);
		return(mISDN_queueup_newhead(inst, 0, hh->prim | CONFIRM, hh->dinfo, skb));
	} else if ((hh->prim == (PH_ACTIVATE | REQUEST))
	 || (hh->prim == (DL_ESTABLISH  | REQUEST))) {
		/* activate B-channel if not already activated */
		if (debug & DEBUG_L1OIP_MSG)
			printk(KERN_DEBUG "%s: PH_ACTIVATE ch %d (0..32)\n", __FUNCTION__, bch->channel);
		if (test_and_set_bit(BC_FLG_ACTIV, &bch->Flag))
			ret = 0;
		else {
			spin_lock_irqsave(inst->hwlock, flags);
			ret = mode_hfcmulti(hc, bch->channel, bch->inst.pid.protocol[1], hc->chan[bch->channel].slot_tx, hc->chan[bch->channel].bank_tx, hc->chan[bch->channel].slot_rx, hc->chan[bch->channel].bank_rx);
			if (!ret) {
				bch->protocol = bch->inst.pid.protocol[1];
				if (bch->protocol==ISDN_PID_L1_B_64TRANS && !hc->dtmf) {
					/* start decoder */
					hc->dtmf = 1;
					if (debug & DEBUG_L1OIP_DTMF)
						printk(KERN_DEBUG "%s: start dtmf decoder\n", __FUNCTION__);
					HFC_outb(hc, R_DTMF, hc->hw.r_dtmf | V_RST_DTMF);
				}
			}
			spin_unlock_irqrestore(inst->hwlock, flags);
		}
#ifdef FIXME  // TODO changed
		if (bch->inst.pid.protocol[2] == ISDN_PID_L2_B_RAWDEV)
			if (bch->dev)
				if_link(&bch->dev->rport.pif, hh->prim | CONFIRM, 0, 0, NULL, 0);
#endif
		skb_trim(skb, 0);
		return(mISDN_queueup_newhead(inst, 0, hh->prim | CONFIRM, ret, skb));
	} else if ((hh->prim == (PH_DEACTIVATE | REQUEST))
	 || (hh->prim == (DL_RELEASE | REQUEST))
	 || ((hh->prim == (PH_CONTROL | REQUEST) && (hh->dinfo == HW_DEACTIVATE)))) {
		if (debug & DEBUG_L1OIP_MSG)
			printk(KERN_DEBUG "%s: PH_DEACTIVATE ch %d (0..32)\n", __FUNCTION__, bch->channel);
		/* deactivate B-channel if not already deactivated */
		spin_lock_irqsave(inst->hwlock, flags);
		if (bch->next_skb) {
			test_and_clear_bit(BC_FLG_TX_NEXT, &bch->Flag);
			dev_kfree_skb(bch->next_skb);
			bch->next_skb = NULL;
		}
		bch->tx_idx = bch->tx_len = bch->rx_idx = 0;
		test_and_clear_bit(BC_FLG_TX_BUSY, &bch->Flag);
		hc->chan[bch->channel].slot_tx = -1;
		hc->chan[bch->channel].slot_rx = -1;
		hc->chan[bch->channel].conf = -1;
		mode_hfcmulti(hc, bch->channel, ISDN_PID_NONE, hc->chan[bch->channel].slot_tx, hc->chan[bch->channel].bank_tx, hc->chan[bch->channel].slot_rx, hc->chan[bch->channel].bank_rx);
		bch->protocol = ISDN_PID_NONE;
		test_and_clear_bit(BC_FLG_ACTIV, &bch->Flag);
		spin_unlock_irqrestore(inst->hwlock, flags);
		skb_trim(skb, 0);
//printk("5\n");
		if (hh->prim != (PH_CONTROL | REQUEST)) {
#ifdef FIXME  // TODO changed
			if (bch->inst.pid.protocol[2] == ISDN_PID_L2_B_RAWDEV)
				if (bch->dev)
					if_link(&bch->dev->rport.pif, hh->prim | CONFIRM, 0, 0, NULL, 0);
#endif
			return(mISDN_queueup_newhead(inst, 0, hh->prim | CONFIRM, ret, skb));
//printk("6\n");
		}
//printk("7\n");
		ret = 0;
	} else
	if (hh->prim == (PH_CONTROL | REQUEST)) {
		spin_lock_irqsave(inst->hwlock, flags);
		switch (hh->dinfo) {
			/* fill features structure */
			case HW_FEATURES:
			if (skb->len != sizeof(void *)) {
				printk(KERN_WARNING "%s: HW_FEATURES lacks parameters\n", __FUNCTION__);
				break;
			}
			if (debug & DEBUG_L1OIP_MSG)
				printk(KERN_DEBUG "%s: HW_FEATURE request\n", __FUNCTION__);
			features = *((struct dsp_features **)skb->data);
			features->hfc_id = hc->id;
			if (test_bit(HFC_CHIP_DTMF, &hc->chip))
				features->hfc_dtmf = 1;
			features->hfc_loops = 0;
			features->pcm_id = hc->pcm;
			features->pcm_slots = hc->slots;
			features->pcm_banks = 2;
			ret = 0;
			break;

			/* connect interface to pcm timeslot (0..N) */
			case HW_PCM_CONN:
			if (skb->len < 4*sizeof(u_long)) {
				printk(KERN_WARNING "%s: HW_PCM_CONN lacks parameters\n", __FUNCTION__);
				break;
			}
			slot_tx = ((int *)skb->data)[0];
			bank_tx = ((int *)skb->data)[1];
			slot_rx = ((int *)skb->data)[2];
			bank_rx = ((int *)skb->data)[3];
			if (debug & DEBUG_L1OIP_MSG)
				printk(KERN_DEBUG "%s: HW_PCM_CONN slot %d bank %d (TX) slot %d bank %d (RX)\n", __FUNCTION__, slot_tx, bank_tx, slot_rx, bank_rx);
			if (slot_tx<=hc->slots && bank_tx<=2 && slot_rx<=hc->slots && bank_rx<=2)
				hfcmulti_pcm(hc, bch->channel, slot_tx, bank_tx, slot_rx, bank_rx);
			else
				printk(KERN_WARNING "%s: HW_PCM_CONN slot %d bank %d (TX) slot %d bank %d (RX) out of range\n", __FUNCTION__, slot_tx, bank_tx, slot_rx, bank_rx);
			ret = 0;
			break;

			/* release interface from pcm timeslot */
			case HW_PCM_DISC:
			if (debug & DEBUG_L1OIP_MSG)
				printk(KERN_DEBUG "%s: HW_PCM_DISC\n", __FUNCTION__);
			hfcmulti_pcm(hc, bch->channel, -1, -1, -1, -1);
			ret = 0;
			break;

			/* join conference (0..7) */
			case HW_CONF_JOIN:
			if (skb->len < sizeof(u_long)) {
				printk(KERN_WARNING "%s: HW_CONF_JOIN lacks parameters\n", __FUNCTION__);
				break;
			}
			num = ((u_long *)skb->data)[0];
			if (debug & DEBUG_L1OIP_MSG)
				printk(KERN_DEBUG "%s: HW_CONF_JOIN conf %ld\n", __FUNCTION__, num);
			if (num <= 7) {
				hfcmulti_conf(hc, bch->channel, num);
				ret = 0;
			} else
				printk(KERN_WARNING "%s: HW_CONF_JOIN conf %ld out of range\n", __FUNCTION__, num);
			break;

			/* split conference */
			case HW_CONF_SPLIT:
			if (debug & DEBUG_L1OIP_MSG)
				printk(KERN_DEBUG "%s: HW_CONF_SPLIT\n", __FUNCTION__);
			hfcmulti_conf(hc, bch->channel, -1);
			ret = 0;
			break;

			/* set sample loop */
			case HW_SPL_LOOP_ON:
			if (debug & DEBUG_L1OIP_MSG)
				printk(KERN_DEBUG "%s: HW_SPL_LOOP_ON (len = %d)\n", __FUNCTION__, skb->len);
			hfcmulti_splloop(hc, bch->channel, skb->data, skb->len);
			ret = 0;
			break;

			/* set silence */
			case HW_SPL_LOOP_OFF:
			if (debug & DEBUG_L1OIP_MSG)
				printk(KERN_DEBUG "%s: HW_SPL_LOOP_OFF\n", __FUNCTION__);
			hfcmulti_splloop(hc, bch->channel, NULL, 0);
			ret = 0;
			break;

			default:
			printk(KERN_DEBUG "%s: unknown PH_CONTROL info %x\n", __FUNCTION__, hh->dinfo);
			ret = -EINVAL;
		}
		spin_unlock_irqrestore(inst->hwlock, flags);
	} else {
		printk(KERN_WARNING "%s: unknown prim(%x)\n", __FUNCTION__, hh->prim);
		ret = -EINVAL;
	}
	if (!ret) {
//		printk("3\n");
		dev_kfree_skb(skb);
//		printk("4\n");
	}
	return(ret);
}



/* MGR stuff */

static int
l1oip_manager(void *data, u_int prim, void *arg)
{
	hfc_multi_t	*hc;
	mISDNinstance_t	*inst = data;
	struct sk_buff	*skb;
	dchannel_t	*dch = NULL;
	bchannel_t	*bch = NULL;
	int		ch;
	int		i;
	u_long		flags;

	if (!data) {
		MGR_HASPROTOCOL_HANDLER(prim,arg,&HFCM_obj)
		printk(KERN_ERR "%s: no data prim %x arg %p\n", __FUNCTION__, prim, arg);
		return(-EINVAL);
	}

	/* find channel and card */
	spin_lock_irqsave(&HFCM_obj.lock, flags);
	if (hc->dch[i])
	if (&hc->dch[i]->inst == inst) {
		dch = hc->dch[i];
		ch = dch->channel;
		break;
	}
	list_for_each_entry(hc, &HFCM_obj.ilist, list) {
		i = 0;
		while(i < 30) {
//printk(KERN_DEBUG "comparing (D-channel) card=%08x inst=%08x with inst=%08x\n", hc, &hc->dch[i].inst, inst);
			if (hc->bch[i])
			if (&hc->bch[i]->inst == inst) {
				bch = hc->bch[i];
				ch = dch->channel;
				goto found;
			}
			i++;
		}
	}
	spin_unlock_irqrestore(&HFCM_obj.lock, flags);
	printk(KERN_ERR "%s: no card/channel found  data %p prim %x arg %p\n", __FUNCTION__, data, prim, arg);
	return(-EINVAL);
	
	found:
	spin_unlock_irqrestore(&HFCM_obj.lock, flags);
	if (debug & DEBUG_L1OIP_MGR)
		printk(KERN_DEBUG "%s: channel %d (0..31)  data %p prim %x arg %p\n", __FUNCTION__, ch, data, prim, arg);

	switch(prim) {
		case MGR_REGLAYER | CONFIRM:
		if (debug & DEBUG_L1OIP_MGR)
			printk(KERN_DEBUG "%s: MGR_REGLAYER\n", __FUNCTION__);
		if (dch)
			dch_set_para(dch, &inst->st->para);
		if (bch)
			bch_set_para(bch, &inst->st->para);
		break;

		case MGR_UNREGLAYER | REQUEST:
		if (debug & DEBUG_L1OIP_MGR)
			printk(KERN_DEBUG "%s: MGR_UNREGLAYER\n", __FUNCTION__);
		if (dch) {
			if ((skb = create_link_skb(PH_CONTROL | REQUEST, HW_DEACTIVATE, 0, NULL, 0))) {
				if (hfcmulti_l1hw(inst, skb)) dev_kfree_skb(skb);
			}
		} else
		if (bch) {
			if ((skb = create_link_skb(PH_CONTROL | REQUEST, 0, 0, NULL, 0))) {
				if (hfcmulti_l2l1(inst, skb)) dev_kfree_skb(skb);
			}
		}
		HFCM_obj.ctrl(inst, MGR_UNREGLAYER | REQUEST, NULL);
		break;

		case MGR_CLRSTPARA | INDICATION:
		arg = NULL;
		// fall through
		case MGR_ADDSTPARA | INDICATION:
		if (debug & DEBUG_L1OIP_MGR)
			printk(KERN_DEBUG "%s: MGR_***STPARA\n", __FUNCTION__);
		if (dch)
			dch_set_para(dch, arg);
		if (bch)
			bch_set_para(bch, arg);
		break;

		case MGR_RELEASE | INDICATION:
		if (debug & DEBUG_L1OIP_MGR)
			printk(KERN_DEBUG "%s: MGR_RELEASE = remove port from mISDN\n", __FUNCTION__);
		if (dch) {
			release_network(hc);
			release_card(hc);
		}
		break;
#ifdef FIXME
		case MGR_CONNECT | REQUEST:
		if (debug & DEBUG_L1OIP_MGR)
			printk(KERN_DEBUG "%s: MGR_CONNECT\n", __FUNCTION__);
		return(mISDN_ConnectIF(inst, arg));

		case MGR_SETIF | REQUEST:
		case MGR_SETIF | INDICATION:
		if (debug & DEBUG_L1OIP_MGR)
			printk(KERN_DEBUG "%s: MGR_SETIF\n", __FUNCTION__);
		if (dch)
			return(mISDN_SetIF(inst, arg, prim, hfcmulti_l1hw, NULL, dch));
		if (bch)
			return(mISDN_SetIF(inst, arg, prim, hfcmulti_l2l1, NULL, bch));
		break;

		case MGR_DISCONNECT | REQUEST:
		case MGR_DISCONNECT | INDICATION:
		if (debug & DEBUG_L1OIP_MGR)
			printk(KERN_DEBUG "%s: MGR_DISCONNECT\n", __FUNCTION__);
		return(mISDN_DisConnectIF(inst, arg));
#endif
		case MGR_SELCHANNEL | REQUEST:
		if (debug & DEBUG_L1OIP_MGR)
			printk(KERN_DEBUG "%s: MGR_SELCHANNEL\n", __FUNCTION__);
		if (!dch) {
			printk(KERN_WARNING "%s(MGR_SELCHANNEL|REQUEST): selchannel not dinst\n", __FUNCTION__);
			return(-EINVAL);
		}
		return(SelFreeBChannel(hc, ch, arg));

		case MGR_SETSTACK | INDICATION:
		if (debug & DEBUG_L1OIP_MGR)
			printk(KERN_DEBUG "%s: MGR_SETSTACK\n", __FUNCTION__);
		if (bch && inst->pid.global==2) {
			if ((skb = create_link_skb(PH_ACTIVATE | REQUEST, 0, 0, NULL, 0))) {
				if (hfcmulti_l2l1(inst, skb)) dev_kfree_skb(skb);
			}
			if (inst->pid.protocol[2] == ISDN_PID_L2_B_TRANS)
			mISDN_queue_data(inst, FLG_MSG_UP, DL_ESTABLISH | INDICATION, 0, 0, NULL, 0);
		else mISDN_queue_data(inst, FLG_MSG_UP, PH_ACTIVATE | INDICATION, 0, 0, NULL, 0);
		}
		break;

		PRIM_NOT_HANDLED(MGR_CTRLREADY | INDICATION);
		PRIM_NOT_HANDLED(MGR_GLOBALOPT | REQUEST);
		default:
		printk(KERN_WARNING "%s: prim %x not handled\n", __FUNCTION__, prim);
		return(-EINVAL);
	}
	return(0);
}


/**************************
 * remove card from stack *
 **************************/

static void
release_card(hfc_multi_t *hc)
{
	int	i = 0;
	u_long	flags;

	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: entered\n", __FUNCTION__);

	if (hc->dch) {
		if (debug & DEBUG_L1OIP_INIT)
			printk(KERN_DEBUG "%s: free port %d D-channel %d (1..32)\n", __FUNCTION__, hc->chan[i].port, i);
		mISDN_free_dch(hc->chan[i].dch);
		HFCM_obj.ctrl(&hc->chan[i].dch->inst, MGR_UNREGLAYER | REQUEST, NULL);
		kfree(hc->chan[i].dch);
		hc->chan[i].dch = NULL;
	}
//	if (hc->chan[i].rx_buf) {
//		kfree(hc->chan[i].rx_buf);
//		hc->chan[i].rx_buf = NULL;
//	}
	i = 0;
	while(i < 30) {
		if (hc->bch[i]) {
			if (debug & DEBUG_L1OIP_INIT)
				printk(KERN_DEBUG "%s: free port %d B-channel %d (1..32)\n", __FUNCTION__, hc->chan[i].port, hc->bch[i].channel);
			mISDN_free_bch(hc->bch[i]);
			kfree(hc->bch[i]);
			hc->bch[i] = NULL;
		}
		i++;
	}

	/* remove us from list and delete */
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_WARNING "%s: remove instance from list\n", __FUNCTION__);
	spin_lock_irqsave(&HFCM_obj.lock, flags);
	list_del(&hc->list);
	spin_unlock_irqrestore(&HFCM_obj.lock, flags);
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_WARNING "%s: delete instance\n", __FUNCTION__);
	kfree(hc);
	HFC_cnt--;
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_WARNING "%s: card successfully removed\n", __FUNCTION__);
}

static void __exit
l1oip_cleanup(void)
{
	l1oip_t *hc,*next;
	int err;

	/* unregister mISDN object */
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: entered (refcnt = %d l1oip_cnt = %d)\n", __FUNCTION__, l1oip_obj.refcnt, l1oip_cnt);
	if ((err = mISDN_unregister(&l1oip_obj))) {
		printk(KERN_ERR "Can't unregister L1oIP error(%d)\n", err);
	}

	/* remove remaining devices, but this should never happen */
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: now checking ilist (refcnt = %d)\n", __FUNCTION__, l1oip_obj.refcnt);

	list_for_each_entry_safe(hc, next, &l1oip_obj.ilist, list) {
		printk(KERN_ERR "L1oIP devices struct not empty refs %d\n", l1oip_obj.refcnt);
		release_network(hc);
		release_card(hc);
	}
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: done (refcnt = %d l1oip_cnt = %d)\n", __FUNCTION__, l1oip_obj.refcnt, l1oip_cnt);
}

static int __init
l1oip_init(void)
{
	int err, i;
	char tmpstr[64];

#if !defined(CONFIG_HOTPLUG) || !defined(MODULE)
#error	"CONFIG_HOTPLUG and MODULE are not defined."
#endif
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: init entered\n", __FUNCTION__);

#ifdef __BIG_ENDIAN
#error "not running on big endian machines now"
#endif
	strcpy(tmpstr, l1oip_revision);
	printk(KERN_INFO "mISDN: Layer-1-over-IP driver Rev. %s\n", mISDN_getrev(tmpstr));

	memset(&l1oip_obj, 0, sizeof(l1oip_obj));
#ifdef MODULE
	l1oip_obj.owner = THIS_MODULE;
#endif
	spin_lock_init(&l1oip_obj.lock);
	INIT_LIST_HEAD(&l1oip_obj.ilist);
	l1oip_obj.name = l1oipName;
	l1oip_obj.own_ctrl = l1oip_manager;
	l1oip_obj.DPROTO.protocol[0] = ISDN_PID_L0_TE_S0 | ISDN_PID_L0_NT_S0
				| ISDN_PID_L0_TE_E1 | ISDN_PID_L0_NT_E1;
	l1oip_obj.DPROTO.protocol[1] = ISDN_PID_L1_TE_S0 | ISDN_PID_L1_NT_S0
				| ISDN_PID_L1_TE_E1 | ISDN_PID_L1_NT_E1;
	l1oip_obj.BPROTO.protocol[1] = ISDN_PID_L1_B_64TRANS | ISDN_PID_L1_B_64HDLC;
	l1oip_obj.BPROTO.protocol[2] = ISDN_PID_L2_B_TRANS | ISDN_PID_L2_B_RAWDEV;

	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: registering l1oip_obj\n", __FUNCTION__);
	if ((err = mISDN_register(&l1oip_obj))) {
		printk(KERN_ERR "Can't register L1oIP error(%d)\n", err);
		return(err);
	}
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: new mISDN object (refcnt = %d)\n", __FUNCTION__, l1oip_obj.refcnt);

	l1oip_cnt = 0;

	/* check card type */
	switch (type[l1oip_cnt] & 0xff) {
		case 1:
		pri = 0;
		multichannel = 0;
		break;

		case 2:
		pri = 1;
		multichannel = 0;
		break;

		case 3:
		pri = 0;
		multichannel = 1;
		break;

		case 4:
		pri = 1;
		multichannel = 1;
		break;

		case 0:
		printk(KERN_INFO "%d virtual devices registered\n", l1oip_cnt);
		return(0);

		default:
		printk(KERN_ERR "Card type(%d) not supported.\n", type[HFC_idx] & 0xff);
		ret_err = -EINVAL;
		goto free_object;
	}


	/* allocate card+fifo structure */
	if (!(hc = kmalloc(sizeof(l1oip_t), GFP_ATOMIC))) {
		printk(KERN_ERR "No kmem for L1-over-IP driver.\n");
		ret_err = -ENOMEM;
		goto free_object;
	}
	memset(hc, 0, sizeof(hfc_multi_t));
	hc->idx = l1oip_cnt;
	hc->pri = pri;
	hc->multichannel = multichannel;
	hc->limit = limit[l1oip_cnt];

	if (hc->pri)
		sprintf(hc->name, "L1oIP-E1#%d", HFC_cnt+1);
	else
		sprintf(hc->name, "L1oIP-S0#%d", HFC_cnt+1);

	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: (after APPEND_TO_LIST)\n", __FUNCTION__);
	
	spin_lock_irqsave(&HFCM_obj.lock, flags);
	list_add_tail(&hc->list, &HFCM_obj.ilist);
	spin_unlock_irqrestore(&HFCM_obj.lock, flags);
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: (after APPEND_TO_LIST)\n", __FUNCTION__);

	spin_lock_init(&hc->lock);

	/* check codec */
	switch (codec[l1oip_cnt]) {
		case 0:
		break;

		case 1:
		hc->ulaw = 1;
		break;

		case 2:
		hc->tadpcm = 1;
		break;

		case 3:
		hc->ulaw = 1;
		hc->tadpcm = 1;
		break;

		default:
		printk(KERN_ERR "Codec(%d) not supported.\n", codec[l1oip_cnt]);
		ret_err = -EINVAL;
		goto free_channels;
	}

	if (id[l1oip_cnt] == 0) {
		printk(KERN_ERR "No 'id' value given. Please use 32 bit randmom number 0x...\n");
		ret_err = -EINVAL;
		goto free_channels;
	}

	/* check protocol */
	if (protocol[l1oip_cnt] == 0) {
		printk(KERN_ERR "No 'protocol' value given.\n");
		ret_err = -EINVAL;
		goto free_channels;
	}
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: Registering D-channel, card(%d) protocol(%x)\n", __FUNCTION__, l1oip_cnt+1, protocol[l1oip_cnt]);
	dch = kmalloc(sizeof(dchannel_t), GFP_ATOMIC);
	if (!dch) {
		ret_err = -ENOMEM;
		goto free_channels;
	}
	memset(dch, 0, sizeof(dchannel_t));
	if (hc->pri)
		dch->channel = 16;
	//dch->debug = debug;
	dch->inst.obj = &l1oip_obj;
	dch->inst.hwlock = &hc->lock;
	dch->inst.class_dev.dev = &pdev->dev;
	mISDN_init_instance(&dch->inst, &l1oip_obj, hc, l1oip_dchannel);
	dch->inst.pid.layermask = ISDN_LAYER(0);
	sprintf(dch->inst.name, "L1OIP%d", l1oip_cnt);
//	if (!(hc->chan[ch].rx_buf = kmalloc(MAX_DFRAME_LEN_L1, GFP_ATOMIC))) {
//		ret_err = -ENOMEM;
//		goto free_channels;
//	}
	if (mISDN_init_dch(dch)) {
		ret_err = -ENOMEM;
		goto free_channels;
	}
	hc->dch = dch;

	i=0;
	while(i < ((hc->pri)?30:2)) {
		if (hc->pri)
			ch = i + 1 + (i>=15);
		else
			ch = i + 1;
		if (debug & DEBUG_L1OIP_INIT)
			printk(KERN_DEBUG "%s: Registering B-channel, card(%d) channel(%d)\n", __FUNCTION__, l1oip_cnt, ch);
		bch = kmalloc(sizeof(bchannel_t), GFP_ATOMIC);
		if (!bch) {
			ret_err = -ENOMEM;
			goto free_channels;
		}
		memset(bch, 0, sizeof(bchannel_t));
		bch->channel = ch;
		mISDN_init_instance(&bch->inst, &l1oip_obj, hc, l1oip_bchannel);
		bch->inst.pid.layermask = ISDN_LAYER(0);
		bch->inst.hwlock = &hc->lock;
		bch->inst.class_dev.dev = &pdev->dev;
		//bch->debug = debug;
		sprintf(bch->inst.name, "%s B%d",
			dch->inst.name, i+1);
		if (mISDN_init_bch(bch)) {
			kfree(bch);
			ret_err = -ENOMEM;
			goto free_channels;
		}
		hc->bch[i] = bch;
#ifdef FIXME  // TODO
		if (bch->dev) {
			bch->dev->wport.pif.func = l1oip_bchannel;
			bch->dev->wport.pif.fdata = bch;
		}
#endif
		i++;
	}

	/* set D-channel */
	mISDN_set_dchannel_pid(&pid, protocol[l1oip_cnt], layermask[l1oip_cnt]);

	/* set PRI */
	if (hc->pri == 1) {
		if (layermask[l1oip_cnt] & ISDN_LAYER(2)) {
			pid.protocol[2] |= ISDN_PID_L2_DF_PTP;
		}
		if (layermask[l1oip_cnt] & ISDN_LAYER(3)) {
			pid.protocol[3] |= ISDN_PID_L3_DF_PTP;
			pid.protocol[3] |= ISDN_PID_L3_DF_EXTCID;
			pid.protocol[3] |= ISDN_PID_L3_DF_CRLEN2;
		}
	}

	/* set protocol type */
	dch->inst.pid.protocol[0] = (hc->pri)?ISDN_PID_L0_IP_E1:ISDN_PID_L0_IP_S0;
	pid.protocol[0] = (hc->pri)?ISDN_PID_L0_IP_E1:ISDN_PID_L0_IP_S0;
	if (protocol[l1oip_cnt] & 0x10) {
		/* NT-mode */
		dch->inst.pid.protocol[1] = (hc->pri)?ISDN_PID_L1_NT_E1:ISDN_PID_L1_NT_S0;
		pid.protocol[1] = (hc->pri)?ISDN_PID_L1_NT_E1:ISDN_PID_L1_NT_S0;
		if (layermask[l1oip_cnt] & ISDN_LAYER(2))
			pid.protocol[2] = ISDN_PID_L2_LAPD_NET;
	} else {
		/* TE-mode */
		dch->inst.pid.protocol[1] = (hc->pri)?ISDN_PID_L1_TE_E1:ISDN_PID_L1_TE_S0;
		pid.protocol[1] = (hc->pri)?ISDN_PID_L1_TE_E1:ISDN_PID_L1_TE_S0;
	}
	dch->inst.pid.layermask |= ISDN_LAYER(1);
	pid.layermask |= ISDN_LAYER(1);


	/* run card setup */
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: Setting up network(%d)\n", __FUNCTION__, l1oip_cnt+1);
	if ((ret_err = setup_network(hc))) {
		goto free_channels;
	}
	/* add stacks */
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: Adding d-stack: card(%d)\n", __FUNCTION__, l1oip_cnt+1);
	if ((ret_err = l1oip_obj.ctrl(NULL, MGR_NEWSTACK | REQUEST, &dch->inst))) {
		printk(KERN_ERR  "MGR_ADDSTACK REQUEST dch err(%d)\n", ret_err);
		free_release:
		release_network(hc);
		goto free_object;
	}
	dst = dch->inst.st;
	i=0;
	while(i < ((hc->pri)?30:2)) {
		bch = hc->bch;
		if (debug & DEBUG_L1OIP_INIT)
			printk(KERN_DEBUG "%s: Adding b-stack: card(%d) B-channel(%d)\n", __FUNCTION__, l1oip_cnt+1, bch->channel);
		if ((ret_err = l1oip_obj.ctrl(dst, MGR_NEWSTACK | REQUEST, &bch->inst))) {
			printk(KERN_ERR "MGR_ADDSTACK bchan error %d\n", ret_err);
			free_delstack:
			l1oip_obj.ctrl(dst, MGR_DELSTACK | REQUEST, NULL);
			goto free_release;
		}
		bch->st = bch->inst.st;
		i++;
	}
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: (before MGR_SETSTACK REQUEST) layermask=0x%x\n", __FUNCTION__, pids[pt].layermask);

	if ((ret_err = l1oip_obj.ctrl(dst, MGR_SETSTACK | REQUEST, &pids[pt]))) {
		printk(KERN_ERR "MGR_SETSTACK REQUEST dch err(%d)\n", ret_err);
		goto free_delstack;
	}
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: (after MGR_SETSTACK REQUEST)\n", __FUNCTION__);

	/* delay some time */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((100*HZ)/1000); /* Timeout 100ms */

	/* tell stack, that we are ready */
	l1oip_obj.ctrl(dst, MGR_CTRLREADY | INDICATION, NULL);

	HFC_cnt++;
	goto next_card;

	/* if an error ocurred */

	free_channels:
	if (hc->dch) {
		if (debug & DEBUG_L1OIP_INIT)
			printk(KERN_DEBUG "%s: free D-channel %d (1..32)\n", __FUNCTION__, i);
		mISDN_free_dch(hc->dch);
		kfree(hc->dch);
		hc->dch = NULL;
	}
//	if (hc->rx_buf) {
//		kfree(hc->rx_buf);
//		hc->rx_buf = NULL;
//	}
	i = 0;
	while(i < 30) {
		if (hc->bch[i]) {
			if (debug & DEBUG_L1OIP_INIT)
				printk(KERN_DEBUG "%s: free B-channel %d (1..32)\n", __FUNCTION__, hc->bch[i].channel);
			mISDN_free_bch(hc->bch[i]);
			kfree(hc->bch[i]);
			hc->bch[i] = NULL;
		}
		i++;
	}
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: before REMOVE_FROM_LIST (refcnt = %d)\n", __FUNCTION__, l1oip_obj.refcnt);
	spin_lock_irqsave(&l1oip_obj.lock, flags);
	list_del(&hc->list);
	spin_unlock_irqrestore(&l1oip_obj.lock, flags);
	if (debug & DEBUG_L1OIP_INIT)
		printk(KERN_DEBUG "%s: after REMOVE_FROM_LIST (refcnt = %d)\n", __FUNCTION__, l1oip_obj.refcnt);
	kfree(hc);

	free_object:
	l1oip_cleanup();
	return(ret_err);
}


#ifdef MODULE
module_init(l1oip_init);
module_exit(l1oip_cleanup);
#endif



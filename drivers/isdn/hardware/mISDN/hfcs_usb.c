/* $Id: hfcs_usb.c,v 1.3 2005/05/04 15:38:31 mbachem Exp $
 *
 * mISDN driver for Colognechip HFC-S USB chip
 *
 * Author : Martin Bachem   (info@colognechip.com)
 *  - based on the HiSax hfcusb.c driver by Peter Sprenger
 *  - based on a mISDN skel driver by Karten Keil
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
 * TODO
 *   - hotplug disconnect the USB TA does not unregister mISDN Controller
 *     /proc/capi/controller is still "ready"...
 *     --> use rmmod before disconnecting the TA
 *   - E channel features
 *   
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/usb.h>
#include "dchannel.h"
#include "bchannel.h"
#include "layer1.h"
#include "helper.h"
#include "debug.h"
#include "hw_lock.h"
#include "hfcs_usb.h"

#define DRIVER_NAME "mISDN_hfcsusb"
const char *hfcsusb_rev = "$Revision: 1.3 $";

#define MAX_CARDS	8
#define MODULE_PARM_T	"1-8i"
static int hfcsusb_cnt;
static u_int protocol[MAX_CARDS] = {2,2,2,2,2,2,2,2};
static int layermask[MAX_CARDS];

static mISDNobject_t hw_mISDNObj;
static int debug = 0x1FFFF; // 0;

#ifdef MODULE
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
MODULE_PARM(debug, "1i");
MODULE_PARM(protocol, MODULE_PARM_T);
MODULE_PARM(layermask, MODULE_PARM_T);
#endif


struct _hfcsusb_t;		/* forward definition */

/***************************************************************/
/* structure defining input+output fifos (interrupt/bulk mode) */
/***************************************************************/
struct usb_fifo;		/* forward definition */
typedef struct iso_urb_struct {
	struct urb *purb;
	__u8 buffer[ISO_BUFFER_SIZE];	/* buffer incoming/outgoing data */
	struct usb_fifo *owner_fifo;	/* pointer to owner fifo */
} iso_urb_struct;

typedef struct usb_fifo {
	int fifonum;		/* fifo index attached to this structure */
	int active;		/* fifo is currently active */
	struct _hfcsusb_t *card;	/* pointer to main structure */
	int pipe;		/* address of endpoint */
	__u8 usb_packet_maxlen;	/* maximum length for usb transfer */
	unsigned int max_size;	/* maximum size of receive/send packet */
	__u8 intervall;		/* interrupt interval */
	struct sk_buff *skbuff;	/* actual used buffer */
	struct urb *urb;	/* transfer structure for usb routines */
	__u8 buffer[128];	/* buffer incoming/outgoing data */
	int bit_line;		/* how much bits are in the fifo? */

	volatile __u8 usb_transfer_mode;	/* switched between ISO and INT */
	iso_urb_struct iso[2];	/* need two urbs to have one always for pending */
	__u8 bch_idx;		/* link BChannel Fifos to bch[bch_idx] */
	int delete_flg;		/* only delete skbuff once */
	int last_urblen;	/* remember length of last packet */
} usb_fifo;

typedef struct _hfcsusb_t {
	struct list_head list;
	mISDN_HWlock_t lock;
	dchannel_t dch;
	bchannel_t bch[2];

	struct usb_device *dev;	/* our device */
	int if_used;		/* used interface number */
	int alt_used;		/* used alternate config */
	int cfg_used;		/* configuration index used */
	int vend_idx;		/* index in hfcsusb_idtab */
	int packet_size;
	int iso_packet_size;
	int disc_flag;		/* 1 if device was disonnected to avoid some USB actions */

	usb_fifo fifos[HFCUSB_NUM_FIFOS];	/* structure holding all fifo data */

	/* control pipe background handling */
	ctrl_buft ctrl_buff[HFC_CTRL_BUFSIZE];	/* buffer holding queued data */
	volatile int ctrl_in_idx, ctrl_out_idx, ctrl_cnt;	/* input/output pointer + count */
	struct urb *ctrl_urb;	/* transfer structure for control channel */
	struct usb_ctrlrequest ctrl_write;	/* buffer for control write request */
	struct usb_ctrlrequest ctrl_read;	/* same for read request */
	int ctrl_paksize;	/* control pipe packet size */
	int ctrl_in_pipe, ctrl_out_pipe;	/* handles for control pipe */

	volatile __u8 threshold_mask;	/* threshold in fifo flow control */
	__u8 old_led_state, led_state;
	
	__u8 hw_mode; /* TE ?, NT ?, NT Timer runnning? */
	int nt_timer;
} hfcsusb_t;

/* private vendor specific data */
typedef struct {
	__u8 led_scheme;	// led display scheme
	signed short led_bits[8];	// array of 8 possible LED bitmask settings
	char *vend_name;	// device name
} hfcsusb_vdata;

/****************************************/
/* data defining the devices to be used */
/****************************************/
static struct usb_device_id hfcsusb_idtab[] = {
	{
	 .idVendor = 0x0959,
	 .idProduct = 0x2bd0,
	 .driver_info = (unsigned long) &((hfcsusb_vdata)
			  {LED_OFF, {4, 0, 2, 1},
			   "ISDN USB TA (Cologne Chip HFC-S USB based)"}),
	},
	{
	 .idVendor = 0x0675,
	 .idProduct = 0x1688,
	 .driver_info = (unsigned long) &((hfcsusb_vdata)
			  {LED_SCHEME1, {1, 2, 0, 0},
			   "DrayTek miniVigor 128 USB ISDN TA"}),
	},
	{
	 .idVendor = 0x07b0,
	 .idProduct = 0x0007,
	 .driver_info = (unsigned long) &((hfcsusb_vdata)
			  {LED_SCHEME1, {0x80, -64, -32, -16},
			   "Billion tiny USB ISDN TA 128"}),
	},
	{
	 .idVendor = 0x0742,
	 .idProduct = 0x2008,
	 .driver_info = (unsigned long) &((hfcsusb_vdata)
			  {LED_SCHEME1, {4, 0, 2, 1},
			   "Stollmann USB TA"}),
	 },
	{
	 .idVendor = 0x0742,
	 .idProduct = 0x2009,
	 .driver_info = (unsigned long) &((hfcsusb_vdata)
			  {LED_SCHEME1, {4, 0, 2, 1},
			   "Aceex USB ISDN TA"}),
	 },
	{
	 .idVendor = 0x0742,
	 .idProduct = 0x200A,
	 .driver_info = (unsigned long) &((hfcsusb_vdata)
			  {LED_SCHEME1, {4, 0, 2, 1},
			   "OEM USB ISDN TA"}),
	 },
	{
	 .idVendor = 0x08e3,
	 .idProduct = 0x0301,
	 .driver_info = (unsigned long) &((hfcsusb_vdata)
			  {LED_SCHEME1, {2, 0, 1, 4},
			   "Olitec USB RNIS"}),
	 },
	{
	 .idVendor = 0x07fa,
	 .idProduct = 0x0846,
	 .driver_info = (unsigned long) &((hfcsusb_vdata)
			  {LED_SCHEME1, {0x80, -64, -32, -16},
			   "Bewan Modem RNIS USB"}),
	 },
	{
	 .idVendor = 0x07fa,
	 .idProduct = 0x0847,
	 .driver_info = (unsigned long) &((hfcsusb_vdata)
			  {LED_SCHEME1, {0x80, -64, -32, -16},
			   "Djinn Numeris USB"}),
	 },
	{
	 .idVendor = 0x07b0,
	 .idProduct = 0x0006,
	 .driver_info = (unsigned long) &((hfcsusb_vdata)
			  {LED_SCHEME1, {0x80, -64, -32, -16},
			   "Twister ISDN TA"}),
	 },
};

/* some function prototypes */
static int hfcsusb_l1hwD(mISDNif_t * hif, struct sk_buff *skb);
static int hfcsusb_l2l1B(mISDNif_t * hif, struct sk_buff *skb);
static int mode_bchannel(bchannel_t * bch, int bc, int protocol);
static void hfcsusb_ph_command(hfcsusb_t * card, u_char command);


/******************************************************/
/* start next background transfer for control channel */
/******************************************************/
static void
ctrl_start_transfer(hfcsusb_t * card)
{
	if (card->ctrl_cnt) {
		card->ctrl_urb->pipe = card->ctrl_out_pipe;
		card->ctrl_urb->setup_packet =
		    (u_char *) & card->ctrl_write;
		card->ctrl_urb->transfer_buffer = NULL;
		card->ctrl_urb->transfer_buffer_length = 0;
		card->ctrl_write.wIndex =
		    card->ctrl_buff[card->ctrl_out_idx].hfcs_reg;
		card->ctrl_write.wValue =
		    card->ctrl_buff[card->ctrl_out_idx].reg_val;

		usb_submit_urb(card->ctrl_urb, GFP_ATOMIC);	/* start transfer */
	}
}				/* ctrl_start_transfer */

/************************************/
/* queue a control transfer request */
/* to write HFC-S USB register      */
/* return 0 on success.             */
/************************************/
static int
queued_Write_hfc(hfcsusb_t * card, __u8 reg, __u8 val)
{
	ctrl_buft *buf;

	if (card->ctrl_cnt >= HFC_CTRL_BUFSIZE)
		return (1);	/* no space left */
	buf = &card->ctrl_buff[card->ctrl_in_idx];	/* pointer to new index */
	buf->hfcs_reg = reg;
	buf->reg_val = val;
	if (++card->ctrl_in_idx >= HFC_CTRL_BUFSIZE)
		card->ctrl_in_idx = 0;	/* pointer wrap */
	if (++card->ctrl_cnt == 1)
		ctrl_start_transfer(card);
	return (0);
}

/***************************************************************/
/* control completion routine handling background control cmds */
/***************************************************************/
static void
ctrl_complete(struct urb *urb, struct pt_regs *regs)
{
	hfcsusb_t *card = (hfcsusb_t *) urb->context;
	ctrl_buft *buf;

	urb->dev = card->dev;
	if (card->ctrl_cnt) {
		buf = &card->ctrl_buff[card->ctrl_out_idx];
		card->ctrl_cnt--;	/* decrement actual count */
		if (++card->ctrl_out_idx >= HFC_CTRL_BUFSIZE)
			card->ctrl_out_idx = 0;	/* pointer wrap */

		ctrl_start_transfer(card);	/* start next transfer */
	}
}

/***************************************************/
/* write led data to auxport & invert if necessary */
/***************************************************/
static void
write_led(hfcsusb_t * card, __u8 led_state)
{
	if (led_state != card->old_led_state) {
		card->old_led_state = led_state;
		queued_Write_hfc(card, HFCUSB_P_DATA, led_state);
	}
}

/*********************/
/* handle LED bits   */
/*********************/
static void
set_led_bit(hfcsusb_t * card, signed short led_bits, int unset)
{
	if (unset) {
		if (led_bits < 0)
			card->led_state |= abs(led_bits);
		else
			card->led_state &= ~led_bits;
	} else {
		if (led_bits < 0)
			card->led_state &= ~abs(led_bits);
		else
			card->led_state |= led_bits;
	}
}

/************************/
/* handle LED requests  */
/************************/
static void
handle_led(hfcsusb_t * card, int event)
{
	hfcsusb_vdata *driver_info =
	    (hfcsusb_vdata *) hfcsusb_idtab[card->vend_idx].driver_info;

	if (driver_info->led_scheme == LED_OFF) {
		return;
	}

	switch (event) {
		case LED_POWER_ON:
			set_led_bit(card, driver_info->led_bits[0], 0);
			set_led_bit(card, driver_info->led_bits[1], 1);
			set_led_bit(card, driver_info->led_bits[2], 1);
			set_led_bit(card, driver_info->led_bits[3], 1);
			break;
		case LED_POWER_OFF:
			set_led_bit(card, driver_info->led_bits[0], 1);
			set_led_bit(card, driver_info->led_bits[1], 1);
			set_led_bit(card, driver_info->led_bits[2], 1);
			set_led_bit(card, driver_info->led_bits[3], 1);
			break;
		case LED_S0_ON:
			set_led_bit(card, driver_info->led_bits[1], 0);
			break;
		case LED_S0_OFF:
			set_led_bit(card, driver_info->led_bits[1], 1);
			break;
		case LED_B1_ON:
			set_led_bit(card, driver_info->led_bits[2], 0);
			break;
		case LED_B1_OFF:
			set_led_bit(card, driver_info->led_bits[2], 1);
			break;
		case LED_B2_ON:
			set_led_bit(card, driver_info->led_bits[3], 0);
			break;
		case LED_B2_OFF:
			set_led_bit(card, driver_info->led_bits[3], 1);
			break;
	}
	write_led(card, card->led_state);
}

/* HW lock/unlock functions */
static int
lock_dev(void *data, int nowait)
{
	register mISDN_HWlock_t *lock = &((hfcsusb_t *) data)->lock;
	return (lock_HW(lock, nowait));
}

static void
unlock_dev(void *data)
{
	register mISDN_HWlock_t *lock = &((hfcsusb_t *) data)->lock;
	unlock_HW(lock);
}


static int
hfcsusb_manager(void *data, u_int prim, void *arg)
{
	hfcsusb_t *card;
	mISDNinstance_t *inst = data;
	struct sk_buff *skb;
	int channel = -1;

	if (debug & 0x10000)
		printk(KERN_DEBUG "%s: data(%p) prim(%x) arg(%p)\n",
		       __FUNCTION__, data, prim, arg);

	if (!data) {
		MGR_HASPROTOCOL_HANDLER(prim, arg, &hw_mISDNObj)
		    printk(KERN_ERR "%s: no data prim %x arg %p\n",
			   __FUNCTION__, prim, arg);
		return (-EINVAL);
	}
	list_for_each_entry(card, &hw_mISDNObj.ilist, list) {
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
	if (channel < 0) {
		printk(KERN_WARNING
		       "%s: no channel data %p prim %x arg %p\n",
		       __FUNCTION__, data, prim, arg);
		return (-EINVAL);
	}

	switch (prim) {
		case MGR_REGLAYER | CONFIRM:
			if (channel == 2)
				dch_set_para(&card->dch, &inst->st->para);
			else
				bch_set_para(&card->bch[channel],
					     &inst->st->para);
			break;
		case MGR_UNREGLAYER | REQUEST:
			if (channel == 2) {
				inst->down.fdata = &card->dch;
				if ((skb =
				     create_link_skb(PH_CONTROL | REQUEST,
						     HW_DEACTIVATE, 0,
						     NULL, 0))) {
					if (hfcsusb_l1hwD
					    (&inst->down, skb))
						dev_kfree_skb(skb);
				}
			} else {
				inst->down.fdata = &card->bch[channel];
				if ((skb =
				     create_link_skb(MGR_DISCONNECT |
						     REQUEST, 0, 0, NULL,
						     0))) {
					if (hfcsusb_l2l1B
					    (&inst->down, skb))
						dev_kfree_skb(skb);
				}
			}
			hw_mISDNObj.ctrl(inst->up.peer,
					 MGR_DISCONNECT | REQUEST,
					 &inst->up);
			hw_mISDNObj.ctrl(inst, MGR_UNREGLAYER | REQUEST,
					 NULL);
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
			if (debug & 0x10000)
				printk(KERN_DEBUG
				       "%s : ignoring MGR_RELEASE | INDICATION\n",
				       __FUNCTION__);
			/* card get released later at usb disconnect or module removal ...
			   if (channel == 2) {
			   release_card(card);
			   } else {
			   hfcsusb.refcnt--;
			   }
			 */
			break;
		case MGR_CONNECT | REQUEST:
			return (mISDN_ConnectIF(inst, arg));
		case MGR_SETIF | REQUEST:
		case MGR_SETIF | INDICATION:
			if (channel == 2)
				return (mISDN_SetIF
					(inst, arg, prim, hfcsusb_l1hwD,
					 NULL, &card->dch));
			else
				return (mISDN_SetIF
					(inst, arg, prim, hfcsusb_l2l1B,
					 NULL, &card->bch[channel]));
			break;
		case MGR_DISCONNECT | REQUEST:
		case MGR_DISCONNECT | INDICATION:
			return (mISDN_DisConnectIF(inst, arg));
		case MGR_SETSTACK | CONFIRM:
			if ((channel != 2) && (inst->pid.global == 2)) {
				inst->down.fdata = &card->bch[channel];
				if ((skb =
				     create_link_skb(PH_ACTIVATE | REQUEST,
						     0, 0, NULL, 0))) {
					if (hfcsusb_l2l1B
					    (&inst->down, skb))
						dev_kfree_skb(skb);
				}
				if (inst->pid.protocol[2] ==
				    ISDN_PID_L2_B_TRANS)
					if_link(&inst->up,
						DL_ESTABLISH | INDICATION,
						0, 0, NULL, 0);
				else
					if_link(&inst->up,
						PH_ACTIVATE | INDICATION,
						0, 0, NULL, 0);
			}
			break;
		case MGR_GLOBALOPT | REQUEST:
			if (arg) {
				/* FIXME: detect cards with HEADSET */
				u_int *gopt = arg;
				*gopt = GLOBALOPT_INTERNAL_CTRL |
				    GLOBALOPT_EXTERNAL_EQUIPMENT |
				    GLOBALOPT_HANDSET;
			} else
				return (-EINVAL);
			break;
		case MGR_SELCHANNEL | REQUEST:
			return (-EINVAL);
			PRIM_NOT_HANDLED(MGR_CTRLREADY | INDICATION);
		default:
			printk(KERN_WARNING "%s: prim %x not handled\n",
			       __FUNCTION__, prim);
			return (-EINVAL);
	}
	return (0);
}

/*********************************/
/* S0 state change event handler */
/*********************************/
static void
S0_new_state(dchannel_t * dch)
{
	u_int prim = PH_SIGNAL | INDICATION;
	u_int para = 0;
	mISDNif_t *upif = &dch->inst.up;
	hfcsusb_t *card = dch->hw;

	if (!test_and_clear_bit(D_L1STATECHANGE, &dch->event))
		return;

	if (card->hw_mode & HW_MODE_TE) {
		if (dch->debug)
			mISDN_debugprint(&card->dch.inst,
				 "%s: TE %d",
				 __FUNCTION__, dch->ph_state);
		
		switch (dch->ph_state) {
			case (0):
				prim = PH_CONTROL | INDICATION;
				para = HW_RESET;
				break;
			case (3):
				prim = PH_CONTROL | INDICATION;
				para = HW_DEACTIVATE;
				handle_led(card, LED_S0_OFF);
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
				handle_led(card, LED_S0_ON);
				break;
			default:
				return;
		}
	} else {
		if (dch->debug)
			mISDN_debugprint(&card->dch.inst,
				 "%s: NT %d",
				 __FUNCTION__, dch->ph_state);
				
		dch->inst.lock(dch->inst.data, 0);
		switch (dch->ph_state) {
			case (1):
				card->nt_timer = 0;
				card->hw_mode &= ~NT_ACTIVATION_TIMER;
				prim = PH_DEACTIVATE | INDICATION;
				para = 0;
				handle_led(card, LED_S0_OFF);
				break;
				
			case (2):
				if (card->nt_timer < 0) {
					card->nt_timer = 0;
					card->hw_mode &= ~NT_ACTIVATION_TIMER;
					hfcsusb_ph_command(dch->hw, HFC_L1_DEACTIVATE_NT);
				} else {
					card->hw_mode |= NT_ACTIVATION_TIMER;
					card->nt_timer = NT_T1_COUNT;
					/* allow G2 -> G3 transition */
					queued_Write_hfc(card, HFCUSB_STATES, 2 | HFCUSB_NT_G2_G3);
				}
				upif = NULL;
				break;
			case (3):
				card->nt_timer = 0;
				card->hw_mode &= ~NT_ACTIVATION_TIMER;
				prim = PH_ACTIVATE | INDICATION;
				para = 0;
				handle_led(card, LED_S0_ON);
				break;
			case (4):
				card->nt_timer = 0;
				card->hw_mode &= ~NT_ACTIVATION_TIMER;
				upif = NULL;
				break;
			default:
				break;
		}
		dch->inst.unlock(dch->inst.data);
	}		
	
	while (upif) {
		if_link(upif, prim, para, 0, NULL, 0);
		upif = upif->clone;
	}
}

/******************************/
/* trigger S0 state changes   */
/******************************/
static void
state_handler(hfcsusb_t * card, __u8 new_l1_state)
{
	if (new_l1_state == card->dch.ph_state
	    || new_l1_state < 1 || new_l1_state > 8)
		return;

	card->dch.ph_state = new_l1_state;
	dchannel_sched_event(&card->dch, D_L1STATECHANGE);
}

/*
 * B-channel setup routine, setup the selected B-channel mode for a given
 * protocol
 * It also maybe change the B-channel timeslot to match the allocated slot
 *
 * basic protocol values
 *	-1			used for first time setup during init
 *	ISDN_PID_NONE		unused channel, idle mode (disconnected)
 *	ISDN_PID_L1_B_64TRANS   64 kBit transparent
 *	ISDN_PID_L1_B_64HDLC	64 kBit HDLC framing
 *
 *	if the hardware supports more protocols, they should be handled too
 */
static int
mode_bchannel(bchannel_t * bch, int bc, int protocol)
{
	__u8 conhdlc, sctrl, sctrl_r;	/* conatainer for new register vals */

	hfcsusb_t *card = bch->inst.data;

	if (bch->debug & L1_DEB_HSCX)
		mISDN_debugprint(&bch->inst,
				 "protocol %x-->%x ch %d-->%d",
				 bch->protocol, protocol,
				 bch->channel, bc);

	/* setup val for CON_HDLC */
	conhdlc = 0;
	if (protocol > ISDN_PID_NONE)
		conhdlc = 8;	/* enable FIFO */

	switch (protocol) {
		case (-1):	/* used for init */
			bch->protocol = -1;
			bch->channel = bc;
			/* fall trough */
		case (ISDN_PID_NONE):
			if (bch->protocol == ISDN_PID_NONE)
				return (0);	/* already in idle state */
			bch->protocol = ISDN_PID_NONE;
			break;
		case (ISDN_PID_L1_B_64TRANS):
			conhdlc |= 2;
			bch->protocol = protocol;
			break;
		case (ISDN_PID_L1_B_64HDLC):
			bch->protocol = protocol;
			break;
		default:
			mISDN_debugprint(&bch->inst, "prot not known %x",
					 protocol);
			return (-ENOPROTOOPT);
	}

	if (protocol >= ISDN_PID_NONE) {
		/* set FIFO to transmit register */
		queued_Write_hfc(card, HFCUSB_FIFO,
				      (bch->channel)?2:0);
		queued_Write_hfc(card, HFCUSB_CON_HDLC, conhdlc);

		/* reset fifo */
		queued_Write_hfc(card, HFCUSB_INC_RES_F, 2);
		
		/* set FIFO to receive register */
		queued_Write_hfc(card, HFCUSB_FIFO,
				      ((bch->channel)?3:1));
		queued_Write_hfc(card, HFCUSB_CON_HDLC, conhdlc);
		
		/* reset fifo */
		queued_Write_hfc(card, HFCUSB_INC_RES_F, 2);

		sctrl = 0x40 + ((card->hw_mode & HW_MODE_TE)?0x00:0x04);
		sctrl_r = 0x0;
		
		if (card->bch[0].protocol) {
			sctrl |= ((card->bch[0].channel)?2:1);
			sctrl_r |= ((card->bch[0].channel)?2:1);
		}
		
		if (card->bch[1].protocol) {
			sctrl |= ((card->bch[1].channel)?2:1);
			sctrl_r |= ((card->bch[1].channel)?2:1);
		}
		
		card->fifos[HFCUSB_B1_RX].bch_idx = 0; /* bch[0] linked to B1 */
		card->fifos[HFCUSB_B1_TX].bch_idx = 0; 
		card->fifos[HFCUSB_B2_RX].bch_idx = 1; /* bch[1] linked to B2 */
		card->fifos[HFCUSB_B2_TX].bch_idx = 1;
		
		queued_Write_hfc(card, HFCUSB_SCTRL, sctrl);
		queued_Write_hfc(card, HFCUSB_SCTRL_R, sctrl_r);
	
		if (protocol > ISDN_PID_NONE) {
			handle_led(card, ((bch->channel)?LED_B2_ON:LED_B1_ON));
				
			/* signal the channel has space for transmit data */
			bch_sched_event(bch, B_XMTBUFREADY);
			
		} else {
			handle_led(card, ((bch->channel)?LED_B2_OFF:LED_B1_OFF));
		}
	}
	return (0);
}

static void
hfcsusb_ph_command(hfcsusb_t * card, u_char command)
{
	if (card->dch.debug & L1_DEB_ISAC)
		mISDN_debugprint(&card->dch.inst, "hfcsusb_ph_command %x",
				 command);

	switch (command) {
		case HFC_L1_ACTIVATE_TE:
			/* force sending sending INFO1 */
			queued_Write_hfc(card,
					      HFCUSB_STATES, 0x14);

			schedule_timeout((1 * HZ) / 1000);	/* sleep 1ms */

			/* start l1 activation */
			queued_Write_hfc(card,
					      HFCUSB_STATES, 0x04);
			break;

		case HFC_L1_ACTIVATE_NT:
			queued_Write_hfc(card,
					      HFCUSB_STATES,
					      HFCUSB_ACTIVATE 
					      | HFCUSB_DO_ACTION 
					      | HFCUSB_NT_G2_G3);
			break;

		case HFC_L1_DEACTIVATE_NT:
			queued_Write_hfc(card,
					      HFCUSB_STATES,
					      HFCUSB_DO_ACTION);
			break;
	}
}

/*************************************/
/* Layer 1 D-channel hardware access */
/*************************************/
static int
hfcsusb_l1hwD(mISDNif_t * hif, struct sk_buff *skb)
{
	dchannel_t *dch;;
	int ret = -EINVAL;
	mISDN_head_t *hh;
	hfcsusb_t *card;

	if (!hif || !skb)
		return (ret);
	hh = mISDN_HEAD_P(skb);
	dch = hif->fdata;
	card = dch->hw;
	ret = 0;
	
	if (hh->prim == PH_DATA_REQ) {
		if (dch->next_skb) {
			mISDN_debugprint(&dch->inst,
					 "hfcsusb l2l1 next_skb exist this shouldn't happen");
			return (-EBUSY);
		}
		dch->inst.lock(dch->inst.data, 0);
		if (test_and_set_bit(FLG_TX_BUSY, &dch->DFlags)) {
			test_and_set_bit(FLG_TX_NEXT, &dch->DFlags);
			dch->next_skb = skb;
			dch->inst.unlock(dch->inst.data);
			return (0);
		} else {
			/* prepare buffer, which is transmitted by
			   tx_iso completions later */
			dch->tx_len = skb->len;
			memcpy(dch->tx_buf, skb->data, dch->tx_len);
			dch->tx_idx = 0;
			dch->inst.unlock(dch->inst.data);
			skb_trim(skb, 0);
			return (if_newhead(&dch->inst.up, PH_DATA_CNF,
					   hh->dinfo, skb));
		}
	} else if (hh->prim == (PH_SIGNAL | REQUEST)) {
		/* do not handle INFO3_P8 and INFO3_P10 */
		ret = -EINVAL;
	} else if (hh->prim == (PH_CONTROL | REQUEST)) {
		dch->inst.lock(dch->inst.data, 0);
		if (hh->dinfo == HW_RESET) {
			if (dch->ph_state != 0)
				hfcsusb_ph_command(dch->hw,
						   HFC_L1_ACTIVATE_TE);
		} else if (hh->dinfo == HW_DEACTIVATE) {
			discard_queue(&dch->rqueue);
			if (dch->next_skb) {
				dev_kfree_skb(dch->next_skb);
				dch->next_skb = NULL;
			}
			test_and_clear_bit(FLG_TX_NEXT, &dch->DFlags);
			test_and_clear_bit(FLG_TX_BUSY, &dch->DFlags);
			if (test_and_clear_bit(FLG_L1_DBUSY, &dch->DFlags))
				dchannel_sched_event(dch, D_CLEARBUSY);
		} else if ((hh->dinfo & HW_TESTLOOP) == HW_TESTLOOP) {
			u_char val = 0;

			if (1 & hh->dinfo)
				val |= 0x0c;
			if (2 & hh->dinfo)
				val |= 0x3;
			/* !!! not implemented yet */
		} else {
			if (dch->debug & L1_DEB_WARN)
				mISDN_debugprint(&dch->inst,
						 "hfcsusb_l1hw unknown ctrl %x",
						 hh->dinfo);
			ret = -EINVAL;
		}
		dch->inst.unlock(dch->inst.data);
	} else if (hh->prim == (PH_ACTIVATE | REQUEST)) {
		dch->inst.lock(dch->inst.data, 0);
		if (card->hw_mode & HW_MODE_NT) {
			hfcsusb_ph_command(dch->hw, HFC_L1_ACTIVATE_NT);
		} else {
			if (dch->debug & L1_DEB_WARN)
				mISDN_debugprint(&dch->inst, "%s: PH_ACTIVATE none NT mode",
					__FUNCTION__);
			ret = -EINVAL;			
		}
		dch->inst.unlock(dch->inst.data);
	} else if (hh->prim == (PH_DEACTIVATE | REQUEST)) {
		dch->inst.lock(dch->inst.data, 0);
		if (card->hw_mode & HW_MODE_NT) {
			hfcsusb_ph_command(dch->hw, HFC_L1_DEACTIVATE_NT);
		} else {
			if (dch->debug & L1_DEB_WARN)
				mISDN_debugprint(&dch->inst, "%s: PH_DEACTIVATE none NT mode",
					__FUNCTION__);
			ret = -EINVAL;			
		}
		dch->inst.unlock(dch->inst.data);
	} else {
		if (dch->debug & L1_DEB_WARN)
			mISDN_debugprint(&dch->inst,
					 "hfcsusb_l1hw unknown prim %x",
					 hh->prim);
		ret = -EINVAL;
	}
	if (!ret)
		dev_kfree_skb(skb);
	return (ret);
}

/******************************/
/* Layer2 -> Layer 1 Transfer */
/******************************/
static int
hfcsusb_l2l1B(mISDNif_t * hif, struct sk_buff *skb)
{
	bchannel_t *bch;
	int ret = -EINVAL;
	mISDN_head_t *hh;
	
	hfcsusb_t *card;

	if (!hif || !skb)
		return (ret);
	hh = mISDN_HEAD_P(skb);
	bch = hif->fdata;
	
	card = bch->hw;
	
	if ((hh->prim == PH_DATA_REQ) || (hh->prim == (DL_DATA | REQUEST))) {
		if (bch->next_skb) {
			printk(KERN_WARNING "%s: next_skb exist ERROR\n",
			       __FUNCTION__);
			return (-EBUSY);
		}
		bch->inst.lock(bch->inst.data, 0);
		if (test_and_set_bit(BC_FLG_TX_BUSY, &bch->Flag)) {
			test_and_set_bit(BC_FLG_TX_NEXT, &bch->Flag);
			bch->next_skb = skb;
			bch->inst.unlock(bch->inst.data);
			return (0);
		} else {
			/* prepare buffer, wich is transmitted by
			   tx_iso completions later */
			bch->tx_len = skb->len;
			memcpy(bch->tx_buf, skb->data, bch->tx_len);
			bch->tx_idx = 0;
			bch->inst.unlock(bch->inst.data);
			if ((bch->inst.pid.protocol[2] ==
			     ISDN_PID_L2_B_RAWDEV)
			    && bch->dev)
				hif = &bch->dev->rport.pif;
			else
				hif = &bch->inst.up;
			skb_trim(skb, 0);
			return (if_newhead(hif, hh->prim | CONFIRM,
					   hh->dinfo, skb));
		}
	} else if ((hh->prim == (PH_ACTIVATE | REQUEST)) ||
		   (hh->prim == (DL_ESTABLISH | REQUEST))) {
		if (test_and_set_bit(BC_FLG_ACTIV, &bch->Flag))
			ret = 0;
		else {
			bch->inst.lock(bch->inst.data, 0);
			ret =
			    mode_bchannel(bch, bch->channel,
					  bch->inst.pid.protocol[1]);
			bch->inst.unlock(bch->inst.data);
		}
		if (bch->inst.pid.protocol[2] == ISDN_PID_L2_B_RAWDEV)
			if (bch->dev)
				if_link(&bch->dev->rport.pif,
					hh->prim | CONFIRM, 0, 0, NULL, 0);
		skb_trim(skb, 0);
		return (if_newhead
			(&bch->inst.up, hh->prim | CONFIRM, ret, skb));
	} else if ((hh->prim == (PH_DEACTIVATE | REQUEST))
		   || (hh->prim == (DL_RELEASE | REQUEST))
		   || (hh->prim == (MGR_DISCONNECT | REQUEST))) {
		   	
		bch->inst.lock(bch->inst.data, 0);
		if (test_and_clear_bit(BC_FLG_TX_NEXT, &bch->Flag)) {
			dev_kfree_skb(bch->next_skb);
			bch->next_skb = NULL;
		}
		test_and_clear_bit(BC_FLG_TX_BUSY, &bch->Flag);
		mode_bchannel(bch, bch->channel, ISDN_PID_NONE);
		test_and_clear_bit(BC_FLG_ACTIV, &bch->Flag);
		bch->inst.unlock(bch->inst.data);
		skb_trim(skb, 0);
		if (hh->prim != (MGR_DISCONNECT | REQUEST)) {
			if (bch->inst.pid.protocol[2] ==
			    ISDN_PID_L2_B_RAWDEV)
				if (bch->dev)
					if_link(&bch->dev->rport.pif,
						hh->prim | CONFIRM, 0, 0,
						NULL, 0);
			if (!if_newhead
			    (&bch->inst.up, hh->prim | CONFIRM, 0, skb))
				return (0);
		}
		
		ret = 0;
	} else if (hh->prim == (PH_CONTROL | REQUEST)) {
		// do not handle PH_CONTROL | REQUEST ??
	} else {
		printk(KERN_WARNING "%s: unknown prim(%x)\n",
		       __FUNCTION__, hh->prim);
	}
	if (!ret)
		dev_kfree_skb(skb);
	return (ret);
}

/*****************************************************/
/* collect data from interrupt or isochron in        */
/*****************************************************/
static void
collect_rx_frame(usb_fifo * fifo, __u8 * data, int len, int finish)
{
	hfcsusb_t *card = fifo->card;
	struct sk_buff *skb;
	u_char *ptr;
	int fifon;
	int i;

	fifon = fifo->fifonum;

	if (!fifo->skbuff) {
		fifo->skbuff = dev_alloc_skb(fifo->max_size + 3);
		if (!fifo->skbuff) {
			printk(KERN_INFO
			       "HFC-S USB: cannot allocate buffer (dev_alloc_skb) fifo:%d\n",
			       fifon);
			return;
		}
	}
	if (len) {
		if (fifo->skbuff->len + len < fifo->max_size) {
			memcpy(skb_put(fifo->skbuff, len), data, len);
		} else {

			if (debug & 0x10000) {
				printk(KERN_INFO "HFC-S USB: ");
				for (i = 0; i < 15; i++)
					printk("%.2x ",
					       fifo->skbuff->data[fifo->
								  skbuff->
								  len -
								  15 + i]);
				printk("\n");
			}

			printk(KERN_INFO
			       "HCF-USB: got frame exceeded fifo->max_size:%d on fifo:%d\n",
			       fifo->max_size, fifon);
		}
	}

	/* Transparent mode data to upper layer */
	if ((fifon == HFCUSB_B1_RX) || (fifon == HFCUSB_B2_RX)) {
		if (card->bch[fifo->bch_idx].protocol == ISDN_PID_L1_B_64TRANS) {
			if (fifo->skbuff->len >= 128) {
				if (!(skb = alloc_stack_skb(fifo->skbuff->len,
					card->bch->up_headerlen)))
					printk(KERN_WARNING "HFC-S USB: receive out of memory\n");
				else {
					ptr =
					    skb_put(skb,
						    fifo->skbuff->
						    len);
					memcpy(ptr, fifo->skbuff->data,
						fifo->skbuff->len);
					dev_kfree_skb(fifo->skbuff);
					fifo->skbuff=NULL;

					skb_queue_tail(&card->bch[fifo->bch_idx].
						       rqueue,
						       skb);

					bch_sched_event(&card->bch[fifo->bch_idx], B_RCVBUFREADY);
				}
			}
			return;
		}
	}

	/* we have a complete hdlc packet */
	if (finish) {
		if ((!fifo->skbuff->data[fifo->skbuff->len - 1])
		    && (fifo->skbuff->len > 3)) {
			/* remove CRC & status */
			skb_trim(fifo->skbuff, fifo->skbuff->len - 3);

			switch (fifon) {
				case HFCUSB_D_RX:
					if ((skb =
					     alloc_stack_skb(fifo->skbuff->
							     len,
							     card->dch.
							     up_headerlen)))
					{
						ptr =
						    skb_put(skb,
							    fifo->skbuff->
							    len);
						memcpy(ptr, fifo->skbuff->data, fifo->skbuff->len);
						dev_kfree_skb(fifo->skbuff);
						fifo->skbuff=NULL;
						
						skb_queue_tail(&card->dch.
							       rqueue,
							       skb);

						dchannel_sched_event
						    (&card->dch,
						     D_RCVBUFREADY);

					} else {
						printk(KERN_WARNING
						       "HFC-S USB: D receive out of memory\n");
					}
					break;
				case HFCUSB_B1_RX:
				case HFCUSB_B2_RX:
					if (card->bch[fifo->bch_idx].protocol > ISDN_PID_NONE) {
						if ((skb =
						     alloc_stack_skb(fifo->skbuff->
								     len,
								     card->
								     bch[fifo->bch_idx].
								     up_headerlen)))
						{
							ptr =
							    skb_put(skb,
								    fifo->skbuff->
								    len);
							memcpy(ptr, fifo->skbuff->data, fifo->skbuff->len);
							dev_kfree_skb(fifo->skbuff);
							fifo->skbuff=NULL;
							
							skb_queue_tail(&card->
								       bch
								       [fifo->bch_idx].
								       rqueue,
								       skb);
							bch_sched_event(&card->
									bch
									[fifo->bch_idx],
									B_RCVBUFREADY);
						} else {
							printk(KERN_WARNING
							       "HFC-S USB: B%i receive out of memory\n",
							       fifo->bch_idx);
						}
					}
					break;
				default:
					printk(KERN_WARNING
					       "HFC-S USB: FIFO UNKWON!\n");
			}

		} else {
			if (fifo->skbuff->len > 3) {
				printk(KERN_INFO
				       "HFC-S USB: got frame %d bytes but CRC ERROR on fifo:%d!!!\n",
				       fifo->skbuff->len, fifon);
				if (debug & 0x10000) {
					printk(KERN_INFO "HFC-S USB: ");
					for (i = 0; i < 15; i++)
						printk("%.2x ",
						       fifo->skbuff->
						       data[fifo->skbuff->
							    len - 15 + i]);
					printk("\n");
				}
			} else {
				printk(KERN_INFO
				       "HFC-S USB: frame to small (%d bytes)!!!\n",
				       fifo->skbuff->len);
			}
			skb_trim(fifo->skbuff, 0);
		}
	}
}

void
fill_isoc_urb(struct urb *urb, struct usb_device *dev, unsigned int pipe,
	      void *buf, int num_packets, int packet_size, int interval,
	      usb_complete_t complete, void *context)
{
	int k;

	spin_lock_init(&urb->lock);
	urb->dev = dev;
	urb->pipe = pipe;
	urb->complete = complete;
	urb->number_of_packets = num_packets;
	urb->transfer_buffer_length = packet_size * num_packets;
	urb->context = context;
	urb->transfer_buffer = buf;
	urb->transfer_flags = URB_ISO_ASAP;
	urb->actual_length = 0;
	urb->interval = interval;
	for (k = 0; k < num_packets; k++) {
		urb->iso_frame_desc[k].offset = packet_size * k;
		urb->iso_frame_desc[k].length = packet_size;
		urb->iso_frame_desc[k].actual_length = 0;
	}
}

/*****************************************************/
/* receive completion routine for all ISO tx fifos   */
/*****************************************************/
static void
rx_iso_complete(struct urb *urb, struct pt_regs *regs)
{
	iso_urb_struct *context_iso_urb = (iso_urb_struct *) urb->context;
	usb_fifo *fifo = context_iso_urb->owner_fifo;
	hfcsusb_t *card = fifo->card;
	int k, len, errcode, offset, num_isoc_packets, fifon, maxlen,
	    status;
	unsigned int iso_status;
	__u8 *buf;
	static __u8 eof[8];

	fifon = fifo->fifonum;
	status = urb->status;

	if (fifo->active && !status) {
		num_isoc_packets = iso_packets[fifon];
		maxlen = fifo->usb_packet_maxlen;
		for (k = 0; k < num_isoc_packets; ++k) {
			len = urb->iso_frame_desc[k].actual_length;
			offset = urb->iso_frame_desc[k].offset;
			buf = context_iso_urb->buffer + offset;
			iso_status = urb->iso_frame_desc[k].status;

			if (iso_status && !card->disc_flag)
				printk(KERN_INFO
				       "HFC-S USB: ISO packet failure - status:%x",
				       iso_status);

			if (fifo->last_urblen != maxlen) {
				/* the threshold mask is in the 2nd status byte */
				card->threshold_mask = buf[1];
				/* care for L1 state only for D-Channel
				   to avoid overlapped iso completions */

				if (fifon == 5) {
					/* the S0 state is in the upper half
					   of the 1st status byte */
					state_handler(card, buf[0] >> 4);
				}

				eof[fifon] = buf[0] & 1;
				if (len > 2)
					collect_rx_frame(fifo, buf + 2,
							 len - 2,
							 (len <
							  maxlen) ?
							 eof[fifon] : 0);
			} else {
				collect_rx_frame(fifo, buf, len,
						 (len <
						  maxlen) ? eof[fifon] :
						 0);
			}
			fifo->last_urblen = len;
		}

		fill_isoc_urb(urb, fifo->card->dev, fifo->pipe,
			      context_iso_urb->buffer, num_isoc_packets,
			      fifo->usb_packet_maxlen, fifo->intervall,
			      rx_iso_complete, urb->context);
		errcode = usb_submit_urb(urb, GFP_ATOMIC);
		if (errcode < 0) {
			printk(KERN_INFO
			       "HFC-S USB: error submitting ISO URB: %d \n",
			       errcode);
		}
	} else {
		if (status && !card->disc_flag) {
			printk(KERN_INFO
			       "HFC-S USB: rx_iso_complete : "
			       "urb->status %d, fifonum %d\n",
			       status, fifon);
		}
	}
}				/* rx_iso_complete */


/*********************************************************/
/* receive completion routine for all interrupt rx fifos */
/*********************************************************/
static void
rx_int_complete(struct urb *urb, struct pt_regs *regs)
{
	int len;
	int status;
	__u8 *buf, maxlen, fifon;
	usb_fifo *fifo = (usb_fifo *) urb->context;
	hfcsusb_t *card = fifo->card;
	static __u8 eof[8];

	urb->dev = card->dev;	/* security init */

	fifon = fifo->fifonum;
	if ((!fifo->active) || (urb->status)) {
		printk(KERN_INFO
		       "HFC-S USB: RX-Fifo %i is going down (%i)", fifon,
		       urb->status);

		fifo->urb->interval = 0;	/* cancel automatic rescheduling */
		if (fifo->skbuff) {
			dev_kfree_skb_any(fifo->skbuff);
			fifo->skbuff = NULL;
		}
		return;
	}
	len = urb->actual_length;
	buf = fifo->buffer;
	maxlen = fifo->usb_packet_maxlen;

	if (fifo->last_urblen != fifo->usb_packet_maxlen) {
		/* the threshold mask is in the 2nd status byte */
		card->threshold_mask = buf[1];

		/* the S0 state is in the upper half of the 1st status byte */
		state_handler(card, buf[0] >> 4);

		eof[fifon] = buf[0] & 1;
		/* if we have more than the 2 status bytes -> collect data */
		if (len > 2)
			collect_rx_frame(fifo, buf + 2,
					 urb->actual_length - 2,
					 (len < maxlen) ? eof[fifon] : 0);
	} else {
		collect_rx_frame(fifo, buf, urb->actual_length,
				 (len < maxlen) ? eof[fifon] : 0);
	}
	fifo->last_urblen = urb->actual_length;
	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status) {
		printk(KERN_INFO
		       "HFC-S USB: error resubmitting URN at rx_int_complete...\n");
	}
}				/* rx_int_complete */


void
next_d_tx_frame(hfcsusb_t * card)
{
	if (test_and_clear_bit(FLG_TX_NEXT, &card->dch.DFlags)) {
		if (card->dch.next_skb) {
			card->dch.tx_len = card->dch.next_skb->len;
			memcpy(card->dch.tx_buf,
			       card->dch.next_skb->data, card->dch.tx_len);
			card->dch.tx_idx = 0;
			dchannel_sched_event(&card->dch, D_XMTBUFREADY);
		} else {
			printk(KERN_WARNING
			       "hfcd tx irq TX_NEXT without skb\n");
			test_and_clear_bit(FLG_TX_BUSY, &card->dch.DFlags);
		}
	} else
		test_and_clear_bit(FLG_TX_BUSY, &card->dch.DFlags);
}

void
next_b_tx_frame(hfcsusb_t * card, __u8 bch_idx)
{
	if (test_and_clear_bit(BC_FLG_TX_NEXT, &card->bch[bch_idx].Flag)) {
		if (card->bch[bch_idx].next_skb) {
			card->bch[bch_idx].tx_idx = 0;
			card->bch[bch_idx].tx_len =
			    card->bch[bch_idx].next_skb->len;
			memcpy(card->bch[bch_idx].tx_buf,
			       card->bch[bch_idx].next_skb->data,
			       card->bch[bch_idx].tx_len);
			bch_sched_event(&card->bch[bch_idx],
					B_XMTBUFREADY);
		} else
			printk(KERN_WARNING
			       "hfc B%i tx irq TX_NEXT without skb\n",
			       bch_idx);
	}
}

/*****************************************************/
/* transmit completion routine for all ISO tx fifos */
/*****************************************************/
static void
tx_iso_complete(struct urb *urb, struct pt_regs *regs)
{
	iso_urb_struct *context_iso_urb = (iso_urb_struct *) urb->context;
	usb_fifo *fifo = context_iso_urb->owner_fifo;
	hfcsusb_t *card = fifo->card;
	int k, tx_offset, num_isoc_packets, sink, len, current_len,
	    errcode;
	int *tx_len, *tx_idx;
	u_char *tx_buf;
	int frame_complete, transp_mode=0, fifon, status;
	__u8 threshbit;
	__u8 threshtable[8] = { 1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80 };

	fifon = fifo->fifonum;
	status = urb->status;

	tx_offset = 0;

	if (fifo->active && !status) {
		/* is FifoFull-threshold set for our channel? */
		threshbit = threshtable[fifon] & card->threshold_mask;
		num_isoc_packets = iso_packets[fifon];

		/* predict dataflow to avoid fifo overflow */
		if (fifon >= HFCUSB_D_TX) {
			sink = (threshbit) ? SINK_DMIN : SINK_DMAX;
		} else {
			sink = (threshbit) ? SINK_MIN : SINK_MAX;
		}
		fill_isoc_urb(urb, fifo->card->dev, fifo->pipe,
			      context_iso_urb->buffer, num_isoc_packets,
			      fifo->usb_packet_maxlen, fifo->intervall,
			      tx_iso_complete, urb->context);
		memset(context_iso_urb->buffer, 0,
		       sizeof(context_iso_urb->buffer));
		frame_complete = 0;

		switch (fifon) {
			case HFCUSB_D_TX:
				tx_buf = card->dch.tx_buf;
				tx_len = &card->dch.tx_len;
				tx_idx = &card->dch.tx_idx;
				break;
			case HFCUSB_B1_TX:
			case HFCUSB_B2_TX:
				tx_buf = card->bch[fifo->bch_idx].tx_buf;
				tx_len = &card->bch[fifo->bch_idx].tx_len;
				tx_idx = &card->bch[fifo->bch_idx].tx_idx;
				transp_mode = card->bch[fifo->bch_idx].protocol == ISDN_PID_L1_B_64TRANS;
				break;
			default:
				/* should never happen ... */
				tx_buf = NULL;
				tx_len = 0;
				tx_idx = 0;
		}

		/* Generate next Iso Packets */
		for (k = 0; k < num_isoc_packets; ++k) {
			len = (*tx_len - *tx_idx);
			if (len) {
				/* we lower data margin every msec */
				fifo->bit_line -= sink;
				current_len = (0 - fifo->bit_line) / 8;
				/* maximum 15 byte for every ISO packet makes our life easier */
				if (current_len > 14)
					current_len = 14;
				current_len =
				    (len <=
				     current_len) ? len : current_len;

				/* how much bit do we put on the line? */
				fifo->bit_line += current_len * 8;

				context_iso_urb->buffer[tx_offset] = 0;
				if (current_len == len) {
					if (!transp_mode) {
						/* here frame completion */
						context_iso_urb->
						    buffer[tx_offset] = 1;
						/* add 2 byte flags and 16bit CRC at end of ISDN frame */
						fifo->bit_line += 32;
					}
					frame_complete = 1;
				}

				/* copy tx data to iso-urb buffer */
				memcpy(context_iso_urb->buffer +
				       tx_offset + 1,
				       (tx_buf + *tx_idx), current_len);
				*tx_idx += current_len;

				/* define packet delimeters within the URB buffer */
				urb->iso_frame_desc[k].offset = tx_offset;
				urb->iso_frame_desc[k].length =
				    current_len + 1;

				tx_offset += (current_len + 1);
			} else {
				urb->iso_frame_desc[k].offset =
				    tx_offset++;

				urb->iso_frame_desc[k].length = 1;
				fifo->bit_line -= sink;	/* we lower data margin every msec */

				if (fifo->bit_line < BITLINE_INF) {
					fifo->bit_line = BITLINE_INF;
				}
			}

			if (frame_complete) {
				fifo->delete_flg = 1;

				/* check for next tx data in queue */
				switch (fifon) {
					case HFCUSB_D_TX:
						next_d_tx_frame(card);
						if (fifo->skbuff
						    && fifo->delete_flg) {
							dev_kfree_skb_any
							    (fifo->skbuff);
							fifo->skbuff =
							    NULL;
							fifo->delete_flg =
							    0;
						}
						frame_complete = 0;
						break;

					case HFCUSB_B1_TX:
					case HFCUSB_B2_TX:
						next_b_tx_frame(card, fifo->bch_idx);
						
						card->bch[fifo->bch_idx].tx_len =
						    0;
						test_and_clear_bit
						    (BC_FLG_TX_BUSY,
						     &card->bch[fifo->bch_idx].
						     Flag);
						card->bch[fifo->bch_idx].tx_idx =
						    card->bch[fifo->bch_idx].
						    tx_len;
						break;
				}
			}
		}
		errcode = usb_submit_urb(urb, GFP_ATOMIC);
		if (errcode < 0) {
			printk(KERN_INFO
			       "HFC-S USB: error submitting ISO URB: %d \n",
			       errcode);
		}
		
		/*
		abuse DChannel tx iso completion to trigger NT mode state changes
		tx_iso_complete is assumed to be called every fifo->intervall ms
		*/
		if ((fifon == HFCUSB_D_TX) && (card->hw_mode & HW_MODE_NT)
		    && (card->hw_mode & NT_ACTIVATION_TIMER)) {
			if ((--card->nt_timer) < 0)
				dchannel_sched_event(&card->dch, D_L1STATECHANGE);
		}

	} else {
		if (status && !card->disc_flag) {
			printk(KERN_INFO
			       "HFC-S USB: tx_iso_complete : urb->status %s (%i), fifonum=%d\n",
			       symbolic(urb_errlist, status), status,
			       fifon);
		}
	}
}


/* allocs urbs and start isoc transfer with two pending urbs to avoid
   gaps in the transfer chain */
static int
start_isoc_chain(usb_fifo * fifo, int num_packets_per_urb,
		 usb_complete_t complete, int packet_size)
{
	int i, k, errcode;

	printk(KERN_INFO "HFC-S USB: starting ISO-chain for Fifo %i\n",
	       fifo->fifonum);

	/* allocate Memory for Iso out Urbs */
	for (i = 0; i < 2; i++) {
		if (!(fifo->iso[i].purb)) {
			fifo->iso[i].purb =
			    usb_alloc_urb(num_packets_per_urb, GFP_KERNEL);
			if (!(fifo->iso[i].purb)) {
				printk(KERN_INFO
				       "alloc urb for fifo %i failed!!!",
				       fifo->fifonum);
			}
			fifo->iso[i].owner_fifo = (struct usb_fifo *) fifo;

			/* Init the first iso */
			if (ISO_BUFFER_SIZE >=
			    (fifo->usb_packet_maxlen *
			     num_packets_per_urb)) {
				fill_isoc_urb(fifo->iso[i].purb,
					      fifo->card->dev, fifo->pipe,
					      fifo->iso[i].buffer,
					      num_packets_per_urb,
					      fifo->usb_packet_maxlen,
					      fifo->intervall, complete,
					      &fifo->iso[i]);
				memset(fifo->iso[i].buffer, 0,
				       sizeof(fifo->iso[i].buffer));
				/* defining packet delimeters in fifo->buffer */
				for (k = 0; k < num_packets_per_urb; k++) {
					fifo->iso[i].purb->
					    iso_frame_desc[k].offset =
					    k * packet_size;
					fifo->iso[i].purb->
					    iso_frame_desc[k].length =
					    packet_size;
				}
			} else {
				printk(KERN_INFO
				       "HFC-S USB: ISO Buffer size to small!\n");
			}
		}
		fifo->bit_line = BITLINE_INF;

		errcode = usb_submit_urb(fifo->iso[i].purb, GFP_KERNEL);
		fifo->active = (errcode >= 0) ? 1 : 0;
		if (errcode < 0) {
			printk(KERN_INFO "HFC-S USB: %s  URB nr:%d\n",
			       symbolic(urb_errlist, errcode), i);
		};
	}
	return (fifo->active);
}

/* stops running iso chain and frees their pending urbs */
static void
stop_isoc_chain(usb_fifo * fifo)
{
	int i;

	for (i = 0; i < 2; i++) {
		if (fifo->iso[i].purb) {
			printk(KERN_INFO
			       "HFC-S USB: %s for fifo %i.%i\n",
			       __FUNCTION__, fifo->fifonum, i);
			usb_kill_urb(fifo->iso[i].purb);
			usb_free_urb(fifo->iso[i].purb);
			fifo->iso[i].purb = NULL;
		}
	}
	if (fifo->urb) {
		usb_kill_urb(fifo->urb);
		usb_free_urb(fifo->urb);
		fifo->urb = NULL;
	}
	fifo->active = 0;
}

/***************************************************/
/* start the interrupt transfer for the given fifo */
/***************************************************/
static void
start_int_fifo(usb_fifo * fifo)
{
	int errcode;

	printk(KERN_INFO "HFC-S USB: starting intr IN fifo:%d\n",
	       fifo->fifonum);

	if (!fifo->urb) {
		fifo->urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!fifo->urb)
			return;
	}
	usb_fill_int_urb(fifo->urb, fifo->card->dev, fifo->pipe,
			 fifo->buffer, fifo->usb_packet_maxlen,
			 rx_int_complete, fifo, fifo->intervall);
	fifo->active = 1;	/* must be marked active */
	errcode = usb_submit_urb(fifo->urb, GFP_KERNEL);
	if (errcode) {
		printk(KERN_INFO
		       "HFC-S USB: submit URB error(start_int_info): status:%i\n",
		       errcode);
		fifo->active = 0;
		fifo->skbuff = NULL;
	}
}

/*
 * hardware init function, called after the basic
 * struct's and stack's were setuped
 *
 */
void
hw_init(hfcsusb_t * card)
{
	/* setup basic function pointers */
	card->dch.hw_bh = S0_new_state;

	/* init bchannel mode */
	mode_bchannel(&card->bch[0], 0, -1);
	mode_bchannel(&card->bch[1], 1, -1);
	card->dch.ph_state = 0;
}

/* Hardware Initialization */
int
setup_hfcsusb(hfcsusb_t * card)
{
	usb_fifo *fifo;
	int i, err;
	u_char b;

	/* check the chip id */
	if (read_usb(card, HFCUSB_CHIP_ID, &b) != 1) {
		printk(KERN_INFO "HFC-USB: cannot read chip id\n");
		return (1);
	}
	if (b != HFCUSB_CHIPID) {
		printk(KERN_INFO "HFC-S USB: Invalid chip id 0x%02x\n", b);
		return (1);
	}

	/* first set the needed config, interface and alternate */
	err = usb_set_interface(card->dev, card->if_used, card->alt_used);

	/* do Chip reset */
	write_usb(card, HFCUSB_CIRM, 8);
	/* aux = output, reset off */
	write_usb(card, HFCUSB_CIRM, 0x10);

	/* set USB_SIZE to match the the wMaxPacketSize for INT or BULK transfers */
	write_usb(card, HFCUSB_USB_SIZE,
		  (card->packet_size /
		   8) | ((card->packet_size / 8) << 4));

	/* set USB_SIZE_I to match the the wMaxPacketSize for ISO transfers */
	write_usb(card, HFCUSB_USB_SIZE_I, card->iso_packet_size);

	/* enable PCM/GCI master mode */
	write_usb(card, HFCUSB_MST_MODE1, 0);	/* set default values */
	write_usb(card, HFCUSB_MST_MODE0, 1);	/* enable master mode */

	/* init the fifos */
	write_usb(card, HFCUSB_F_THRES,
		  (HFCUSB_TX_THRESHOLD /
		   8) | ((HFCUSB_RX_THRESHOLD / 8) << 4));

	fifo = card->fifos;
	for (i = 0; i < HFCUSB_NUM_FIFOS; i++) {
		write_usb(card, HFCUSB_FIFO, i);	/* select the desired fifo */
		fifo[i].skbuff = NULL;	/* init buffer pointer */
		fifo[i].max_size =
		    (i <= HFCUSB_B2_RX) ? MAX_BCH_SIZE : MAX_DFRAME_LEN;
		fifo[i].last_urblen = 0;
		/* set 2 bit for D- & E-channel */
		write_usb(card, HFCUSB_HDLC_PAR,
			  ((i <= HFCUSB_B2_RX) ? 0 : 2));
		/* rx hdlc, enable IFF for D-channel */
		write_usb(card, HFCUSB_CON_HDLC,
			  ((i == HFCUSB_D_TX) ? 0x09 : 0x08));
		write_usb(card, HFCUSB_INC_RES_F, 2);	/* reset the fifo */
	}

	if (card->hw_mode & HW_MODE_NT) {
		write_usb(card, HFCUSB_SCTRL, 0x44);		/* disable B transmitters + capacitive mode, enable NT mode */
		write_usb(card, HFCUSB_CLKDEL, CLKDEL_NT);	/* clock delay value */
		write_usb(card, HFCUSB_STATES, 1 | 0x10);	/* set deactivated mode */
		write_usb(card, HFCUSB_STATES, 1);	/* enable state machine */
	} else {
		write_usb(card, HFCUSB_SCTRL, 0x40);		/* disable B transmitters + capacitive mode, enable TE mode */
		write_usb(card, HFCUSB_CLKDEL, CLKDEL_TE);	/* clock delay value */
		write_usb(card, HFCUSB_STATES, 3 | 0x10);	/* set deactivated mode */
		write_usb(card, HFCUSB_STATES, 3);	/* enable state machine */
	}

	write_usb(card, HFCUSB_SCTRL_R, 0);	/* disable both B receivers */
	
	card->disc_flag = 0;
	card->led_state = 0;
	card->old_led_state = 0;

	/* init the background machinery for control requests */
	card->ctrl_read.bRequestType = 0xc0;
	card->ctrl_read.bRequest = 1;
	card->ctrl_read.wLength = 1;
	card->ctrl_write.bRequestType = 0x40;
	card->ctrl_write.bRequest = 0;
	card->ctrl_write.wLength = 0;
	usb_fill_control_urb(card->ctrl_urb,
			     card->dev,
			     card->ctrl_out_pipe,
			     (u_char *) & card->ctrl_write,
			     NULL, 0, ctrl_complete, card);

	/* Init All Fifos */
	for (i = 0; i < HFCUSB_NUM_FIFOS; i++) {
		card->fifos[i].iso[0].purb = NULL;
		card->fifos[i].iso[1].purb = NULL;
		card->fifos[i].active = 0;
	}


	/* 3 (+1) INT IN + 3 ISO OUT */
	if (card->cfg_used == CNF_3INT3ISO
	    || card->cfg_used == CNF_4INT3ISO) {
		start_int_fifo(card->fifos + HFCUSB_D_RX);
		/*
		   if (card->fifos[HFCUSB_PCM_RX].pipe)
		   start_int_fifo(card->fifos + HFCUSB_PCM_RX);
		 */
		start_int_fifo(card->fifos + HFCUSB_B1_RX);
		start_int_fifo(card->fifos + HFCUSB_B2_RX);
	}

	/* 3 (+1) ISO IN + 3 ISO OUT */
	if (card->cfg_used == CNF_3ISO3ISO
	    || card->cfg_used == CNF_4ISO3ISO) {
		start_isoc_chain(card->fifos + HFCUSB_D_RX, ISOC_PACKETS_D,
				 rx_iso_complete, 16);

		/*
		   if (card->fifos[HFCUSB_PCM_RX].pipe)
		   start_isoc_chain(card->fifos + HFCUSB_PCM_RX,
		   ISOC_PACKETS_D, rx_iso_complete,
		   16);
		 */
		start_isoc_chain(card->fifos + HFCUSB_B1_RX,
				 ISOC_PACKETS_B, rx_iso_complete, 16);

		start_isoc_chain(card->fifos + HFCUSB_B2_RX,
				 ISOC_PACKETS_B, rx_iso_complete, 16);
	}

	start_isoc_chain(card->fifos + HFCUSB_D_TX, ISOC_PACKETS_D,
			 tx_iso_complete, 1);
	start_isoc_chain(card->fifos + HFCUSB_B1_TX, ISOC_PACKETS_B,
			 tx_iso_complete, 1);
	start_isoc_chain(card->fifos + HFCUSB_B2_TX, ISOC_PACKETS_B,
			 tx_iso_complete, 1);

	handle_led(card, LED_POWER_ON);

	return (0);
}

static void
release_card(hfcsusb_t * card)
{
	int i;

#ifdef LOCK_STATISTIC
	printk(KERN_INFO
	       "try_ok(%d) try_wait(%d) try_mult(%d) try_inirq(%d)\n",
	       card->lock.try_ok, card->lock.try_wait, card->lock.try_mult,
	       card->lock.try_inirq);
	printk(KERN_INFO "irq_ok(%d) irq_fail(%d)\n", card->lock.irq_ok,
	       card->lock.irq_fail);
#endif
	if (debug & 0x10000)
		printk(KERN_DEBUG "%s\n", __FUNCTION__);

	lock_dev(card, 0);
	mode_bchannel(&card->bch[0], 0, ISDN_PID_NONE);
	mode_bchannel(&card->bch[1], 1, ISDN_PID_NONE);
	mISDN_free_bch(&card->bch[1]);
	mISDN_free_bch(&card->bch[0]);
	mISDN_free_dch(&card->dch);
	hw_mISDNObj.ctrl(card->dch.inst.up.peer, MGR_DISCONNECT | REQUEST,
			 &card->dch.inst.up);
	hw_mISDNObj.ctrl(&card->dch.inst, MGR_UNREGLAYER | REQUEST, NULL);
	list_del(&card->list);
	unlock_dev(card);

	schedule_timeout((80 * HZ) / 1000);	/* Timeout 80ms */

	/* tell all fifos to terminate */
	for (i = 0; i < HFCUSB_NUM_FIFOS; i++) {
		if (card->fifos[i].usb_transfer_mode == USB_ISOC) {
			if (card->fifos[i].active > 0) {
				stop_isoc_chain(&card->fifos[i]);
			}
		} else {
			if (card->fifos[i].active > 0) {
				card->fifos[i].active = 0;
			}
			if (card->fifos[i].urb) {
				usb_kill_urb(card->fifos[i].urb);
				usb_free_urb(card->fifos[i].urb);
				card->fifos[i].urb = NULL;
			}
		}
		card->fifos[i].active = 0;
	}


	/* wait for all URBS to terminate */
	if (card->ctrl_urb) {
		usb_kill_urb(card->ctrl_urb);
		usb_free_urb(card->ctrl_urb);
		card->ctrl_urb = NULL;
	}

	hfcsusb_cnt--;
	kfree(card);
}

static int
setup_instance(hfcsusb_t * card)
{
	int i, err;
	mISDN_pid_t pid;

	list_add_tail(&card->list, &hw_mISDNObj.ilist);
	card->dch.debug = debug;
	lock_HW_init(&card->lock);
	card->dch.inst.lock = lock_dev;
	card->dch.inst.unlock = unlock_dev;
	card->dch.inst.pid.layermask = ISDN_LAYER(0);
	card->dch.inst.pid.protocol[0] = ISDN_PID_L0_TE_S0;
	mISDN_init_instance(&card->dch.inst, &hw_mISDNObj, card);
	sprintf(card->dch.inst.name, "hfcsusb_%d", hfcsusb_cnt + 1);
	mISDN_set_dchannel_pid(&pid, protocol[hfcsusb_cnt],
			       layermask[hfcsusb_cnt]);
	mISDN_init_dch(&card->dch);
	card->dch.hw = card;
	card->hw_mode = 0;
	
	for (i = 0; i < 2; i++) {
		card->bch[i].channel = i;
		mISDN_init_instance(&card->bch[i].inst, &hw_mISDNObj,
				    card);
		card->bch[i].inst.pid.layermask = ISDN_LAYER(0);
		card->bch[i].inst.lock = lock_dev;
		card->bch[i].inst.unlock = unlock_dev;
		card->bch[i].debug = debug;
		sprintf(card->bch[i].inst.name, "%s B%d",
			card->dch.inst.name, i + 1);
		mISDN_init_bch(&card->bch[i]);
		card->bch[i].hw = card;
		if (card->bch[i].dev) {
			card->bch[i].dev->wport.pif.func = hfcsusb_l2l1B;
			card->bch[i].dev->wport.pif.fdata = &card->bch[i];
		}
	}

	if (protocol[hfcsusb_cnt] & 0x10) {
		// NT Mode
		printk (KERN_INFO "%s wants NT Mode\n", card->dch.inst.name);
		card->dch.inst.pid.protocol[0] = ISDN_PID_L0_NT_S0;
		card->dch.inst.pid.protocol[1] = ISDN_PID_L1_NT_S0;
		pid.protocol[0] = ISDN_PID_L0_NT_S0;
		pid.protocol[1] = ISDN_PID_L1_NT_S0;
		card->dch.inst.pid.layermask |= ISDN_LAYER(1);
		pid.layermask |= ISDN_LAYER(1);
		if (layermask[i] & ISDN_LAYER(2))
			pid.protocol[2] = ISDN_PID_L2_LAPD_NET;
		/* select NT mode with activated NT Timer (T1) */
		card->hw_mode |= (HW_MODE_NT | NT_ACTIVATION_TIMER);
	} else {
		printk (KERN_INFO "%s wants TE Mode\n", card->dch.inst.name);
		// TE Mode
		card->dch.inst.pid.protocol[0] = ISDN_PID_L0_TE_S0;
		card->hw_mode |= HW_MODE_TE;
	}

	if (debug)
		printk(KERN_DEBUG
		       "hfcsusb card %p dch %p bch1 %p bch2 %p\n", card,
		       &card->dch, &card->bch[0], &card->bch[1]);

	err = setup_hfcsusb(card);
	if (err) {
		mISDN_free_dch(&card->dch);
		mISDN_free_bch(&card->bch[1]);
		mISDN_free_bch(&card->bch[0]);
		list_del(&card->list);
		kfree(card);
		return (err);
	}
	hfcsusb_cnt++;
	err =
	    hw_mISDNObj.ctrl(NULL, MGR_NEWSTACK | REQUEST,
			     &card->dch.inst);
	if (err) {
		release_card(card);
		return (err);
	}
	for (i = 0; i < 2; i++) {
		err =
		    hw_mISDNObj.ctrl(card->dch.inst.st,
				     MGR_NEWSTACK | REQUEST,
				     &card->bch[i].inst);
		if (err) {
			printk(KERN_ERR "MGR_ADDSTACK bchan error %d\n",
			       err);
			hw_mISDNObj.ctrl(card->dch.inst.st,
					 MGR_DELSTACK | REQUEST, NULL);
			return (err);
		}
	}
	err =
	    hw_mISDNObj.ctrl(card->dch.inst.st, MGR_SETSTACK | REQUEST,
			     &pid);
	if (err) {
		printk(KERN_ERR "MGR_SETSTACK REQUEST dch err(%d)\n", err);
		hw_mISDNObj.ctrl(card->dch.inst.st, MGR_DELSTACK | REQUEST,
				 NULL);
		return (err);
	}

	hw_init(card);
	hw_mISDNObj.ctrl(card->dch.inst.st, MGR_CTRLREADY | INDICATION,
			 NULL);

	return (0);
}


/*************************************************/
/* function called to probe a new plugged device */
/*************************************************/
static int
hfcsusb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	hfcsusb_t *card;
	struct usb_host_interface *iface = intf->cur_altsetting;
	struct usb_host_interface *iface_used = NULL;
	struct usb_host_endpoint *ep;
	int ifnum = iface->desc.bInterfaceNumber;
	int i, idx, alt_idx, probe_alt_setting, vend_idx, cfg_used, *vcf,
	    attr, cfg_found, cidx, ep_addr;
	int cmptbl[16], small_match, iso_packet_size, packet_size,
	    alt_used = 0;

	hfcsusb_vdata *driver_info;

	vend_idx = 0xffff;
	for (i = 0; hfcsusb_idtab[i].idVendor; i++) {
		if (dev->descriptor.idVendor == hfcsusb_idtab[i].idVendor
		    && dev->descriptor.idProduct ==
		    hfcsusb_idtab[i].idProduct) {
			vend_idx = i;
			continue;
		}
	}

	printk(KERN_INFO
	       "HFC-S USB: probing interface(%d) actalt(%d) minor(%d)\n",
	       ifnum, iface->desc.bAlternateSetting, intf->minor);

	if (vend_idx != 0xffff) {
		/* if vendor and product ID is OK, start probing alternate settings */
		alt_idx = 0;
		small_match = 0xffff;

		/* default settings */
		iso_packet_size = 16;
		packet_size = 64;

		while (alt_idx < intf->num_altsetting) {
			iface = intf->altsetting + alt_idx;
			probe_alt_setting = iface->desc.bAlternateSetting;
			cfg_used = 0;

			/* check for config EOL element */
			while (validconf[cfg_used][0]) {
				cfg_found = 1;
				vcf = validconf[cfg_used];
				/* first endpoint descriptor */
				ep = iface->endpoint;
				memcpy(cmptbl, vcf, 16 * sizeof(int));

				/* check for all endpoints in this alternate setting */
				for (i = 0; i < iface->desc.bNumEndpoints;
				     i++) {
					ep_addr =
					    ep->desc.bEndpointAddress;
					/* get endpoint base */
					idx = ((ep_addr & 0x7f) - 1) * 2;
					if (ep_addr & 0x80)
						idx++;
					attr = ep->desc.bmAttributes;
					if (cmptbl[idx] == EP_NUL) {
						cfg_found = 0;
					}
					if (attr == USB_ENDPOINT_XFER_INT
					    && cmptbl[idx] == EP_INT)
						cmptbl[idx] = EP_NUL;
					if (attr == USB_ENDPOINT_XFER_BULK
					    && cmptbl[idx] == EP_BLK)
						cmptbl[idx] = EP_NUL;
					if (attr == USB_ENDPOINT_XFER_ISOC
					    && cmptbl[idx] == EP_ISO)
						cmptbl[idx] = EP_NUL;

					/* check if all INT endpoints match minimum interval */
					if (attr == USB_ENDPOINT_XFER_INT
					    && ep->desc.bInterval <
					    vcf[17]) {
						cfg_found = 0;
					}
					ep++;
				}
				for (i = 0; i < 16; i++) {
					/* all entries must be EP_NOP or EP_NUL for a valid config */
					if (cmptbl[i] != EP_NOP
					    && cmptbl[i] != EP_NUL)
						cfg_found = 0;
				}
				if (cfg_found) {
					if (cfg_used < small_match) {
						small_match = cfg_used;
						alt_used =
						    probe_alt_setting;
						iface_used = iface;
					}
				}
				cfg_used++;
			}
			alt_idx++;
		}		/* (alt_idx < intf->num_altsetting) */

		/* found a valid USB Ta Endpint config */
		if (small_match != 0xffff) {
			iface = iface_used;
			if (!
			    (card =
			     kmalloc(sizeof(hfcsusb_t), GFP_KERNEL)))
				return (-ENOMEM);	/* got no mem */
			memset(card, 0, sizeof(hfcsusb_t));

			ep = iface->endpoint;
			vcf = validconf[small_match];

			for (i = 0; i < iface->desc.bNumEndpoints; i++) {
				ep_addr = ep->desc.bEndpointAddress;
				/* get endpoint base */
				idx = ((ep_addr & 0x7f) - 1) * 2;
				if (ep_addr & 0x80)
					idx++;
				cidx = idx & 7;
				attr = ep->desc.bmAttributes;

				/* init Endpoints */
				if (vcf[idx] != EP_NOP
				    && vcf[idx] != EP_NUL) {
					switch (attr) {
						case USB_ENDPOINT_XFER_INT:
							card->
							    fifos[cidx].
							    pipe =
							    usb_rcvintpipe
							    (dev,
							     ep->desc.
							     bEndpointAddress);
							card->
							    fifos[cidx].
							    usb_transfer_mode
							    = USB_INT;
							packet_size =
							    ep->desc.
							    wMaxPacketSize;
							break;
						case USB_ENDPOINT_XFER_BULK:
							if (ep_addr & 0x80)
								card->
								    fifos
								    [cidx].
								    pipe =
								    usb_rcvbulkpipe
								    (dev,
								     ep->
								     desc.
								     bEndpointAddress);
							else
								card->
								    fifos
								    [cidx].
								    pipe =
								    usb_sndbulkpipe
								    (dev,
								     ep->
								     desc.
								     bEndpointAddress);
							card->
							    fifos[cidx].
							    usb_transfer_mode
							    = USB_BULK;
							packet_size =
							    ep->desc.
							    wMaxPacketSize;
							break;
						case USB_ENDPOINT_XFER_ISOC:
							if (ep_addr & 0x80)
								card->
								    fifos
								    [cidx].
								    pipe =
								    usb_rcvisocpipe
								    (dev,
								     ep->
								     desc.
								     bEndpointAddress);
							else
								card->
								    fifos
								    [cidx].
								    pipe =
								    usb_sndisocpipe
								    (dev,
								     ep->
								     desc.
								     bEndpointAddress);
							card->
							    fifos[cidx].
							    usb_transfer_mode
							    = USB_ISOC;
							iso_packet_size =
							    ep->desc.
							    wMaxPacketSize;
							break;
						default:
							card->
							    fifos[cidx].
							    pipe = 0;
					}	/* switch attribute */

					if (card->fifos[cidx].pipe) {
						card->fifos[cidx].
						    fifonum = cidx;
						card->fifos[cidx].card =
						    card;
						card->fifos[cidx].
						    usb_packet_maxlen =
						    ep->desc.
						    wMaxPacketSize;
						card->fifos[cidx].
						    intervall =
						    ep->desc.bInterval;
						card->fifos[cidx].
						    skbuff = NULL;
					}
				}
				ep++;
			}
			card->dev = dev;	/* save device */
			card->if_used = ifnum;	/* save used interface */
			card->alt_used = alt_used;	/* and alternate config */
			card->ctrl_paksize = dev->descriptor.bMaxPacketSize0;	/* control size */
			card->cfg_used = vcf[16];	/* store used config */
			card->vend_idx = vend_idx;	/* store found vendor */
			card->packet_size = packet_size;
			card->iso_packet_size = iso_packet_size;

			/* create the control pipes needed for register access */
			card->ctrl_in_pipe = usb_rcvctrlpipe(card->dev, 0);
			card->ctrl_out_pipe =
			    usb_sndctrlpipe(card->dev, 0);
			card->ctrl_urb = usb_alloc_urb(0, GFP_KERNEL);


			driver_info =
			    (hfcsusb_vdata *) hfcsusb_idtab[vend_idx].
			    driver_info;
			printk(KERN_INFO "HFC-S USB: detected \"%s\"\n",
			       driver_info->vend_name);
			printk(KERN_INFO
			    "HFC-S USB: Endpoint-Config: %s (if=%d alt=%d)\n",
			    conf_str[small_match], ifnum, alt_used);			       

			if (setup_instance(card)) {
				if (card->ctrl_urb) {
					usb_kill_urb(card->ctrl_urb);
					usb_free_urb(card->ctrl_urb);
					card->ctrl_urb = NULL;
				}
				kfree(card);
				return (-EIO);
			}
			usb_set_intfdata(intf, card);

			return (0);
		}
	} else {
		printk(KERN_INFO
		       "HFC-S USB: no valid vendor found in USB descriptor\n");
	}
	return (-EIO);
}

/****************************************************/
/* function called when an active device is removed */
/****************************************************/
static void
hfcsusb_disconnect(struct usb_interface
		   *intf)
{
	hfcsusb_t *card = usb_get_intfdata(intf);
	printk(KERN_INFO "HFC-S USB: device disconnect\n");
	card->disc_flag = 1;

	if (debug & 0x10000)
		printk(KERN_DEBUG "%s\n", __FUNCTION__);

	if (!card) {
		if (debug & 0x10000)
			printk(KERN_DEBUG "%s : NO CONTEXT!\n",
			       __FUNCTION__);
		return;
	}

	release_card(card);
	usb_set_intfdata(intf, NULL);
}				/* hfcsusb_disconnect */


/************************************/
/* our driver information structure */
/************************************/
static struct usb_driver hfcsusb_drv = {
	.owner = THIS_MODULE,
	.name = DRIVER_NAME,
	.id_table = hfcsusb_idtab,
	.probe = hfcsusb_probe,
	.disconnect = hfcsusb_disconnect,
};


static int __init
hfcsusb_init(void)
{
	int err;

	printk(KERN_INFO "hfcsusb driver Rev. %s (debug=%i)\n",
	       mISDN_getrev(hfcsusb_rev), debug);

#ifdef MODULE
	hw_mISDNObj.owner = THIS_MODULE;
#endif
	INIT_LIST_HEAD(&hw_mISDNObj.ilist);
	hw_mISDNObj.name = DRIVER_NAME;
	hw_mISDNObj.own_ctrl = hfcsusb_manager;
	
	hw_mISDNObj.DPROTO.protocol[0] = ISDN_PID_L0_TE_S0 |
					 ISDN_PID_L0_NT_S0;
	hw_mISDNObj.DPROTO.protocol[1] = ISDN_PID_L1_NT_S0;
	hw_mISDNObj.BPROTO.protocol[1] = ISDN_PID_L1_B_64TRANS |
					 ISDN_PID_L1_B_64HDLC;
	hw_mISDNObj.BPROTO.protocol[2] = ISDN_PID_L2_B_TRANS |
					 ISDN_PID_L2_B_RAWDEV;
				     	
	if ((err = mISDN_register(&hw_mISDNObj))) {
		printk(KERN_ERR "Can't register hfcsusb error(%d)\n", err);
		return (err);
	}
	if (usb_register(&hfcsusb_drv)) {
		printk(KERN_INFO
		       "hfcsusb: Unable to register hfcsusb module at usb stack\n");
		goto out;
	}
	return 0;

      out:
	mISDN_unregister(&hw_mISDNObj);
	return err;
}

static void __exit
hfcsusb_cleanup(void)
{
	int err;
	hfcsusb_t *card, *next;

	if (debug & 0x10000)
		printk(KERN_DEBUG "%s\n", __FUNCTION__);

	if ((err = mISDN_unregister(&hw_mISDNObj))) {
		printk(KERN_ERR "Can't unregister hfcsusb error(%d)\n",
		       err);
	}

	list_for_each_entry_safe(card, next, &hw_mISDNObj.ilist, list) {
		handle_led(card, LED_POWER_OFF);
	}

	/* unregister Hardware */
	usb_deregister(&hfcsusb_drv);	/* release our driver */
}

module_init(hfcsusb_init);
module_exit(hfcsusb_cleanup);

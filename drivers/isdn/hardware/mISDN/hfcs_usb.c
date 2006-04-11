/* $Id: hfcs_usb.c,v 1.14 2006/04/11 13:13:30 crich Exp $
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
#include "channel.h"
#include "layer1.h"
#include "debug.h"
#include "hfcs_usb.h"


#define DRIVER_NAME "mISDN_hfcsusb"
const char *hfcsusb_rev = "$Revision: 1.14 $";

#define MAX_CARDS	8
static int hfcsusb_cnt;
static u_int protocol[MAX_CARDS] = {2,2,2,2,2,2,2,2};
static int layermask[MAX_CARDS];

static mISDNobject_t hw_mISDNObj;
static int debug = 0x1FFFF; // 0;


#ifdef MODULE
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
module_param(debug, uint, S_IRUGO | S_IWUSR);

#ifdef OLD_MODULE_PARAM_ARRAY
static int num_protocol=0, num_layermask=0;
module_param_array(protocol, uint, num_protocol, S_IRUGO | S_IWUSR);
module_param_array(layermask, uint, num_layermask, S_IRUGO | S_IWUSR);
#else
module_param_array(protocol, uint, NULL, S_IRUGO | S_IWUSR);
module_param_array(layermask, uint, NULL, S_IRUGO | S_IWUSR);
#endif

#endif


struct _hfcsusb_t;		/* forward definition */

/***************************************************************/
/* structure defining input+output fifos (interrupt/bulk mode) */
/***************************************************************/
struct usb_fifo;		/* forward definition */
typedef struct iso_urb_struct {
	struct urb *purb;
	__u8 buffer[ISO_BUFFER_SIZE];	/* buffer incoming/outgoing USB URB data */
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
	struct urb *urb;	/* transfer structure for usb routines */
	__u8 buffer[128];	/* buffer USB INT OUT URB data */
	int bit_line;		/* how much bits are in the fifo? */

	volatile __u8 usb_transfer_mode;	/* switched between ISO and INT */
	iso_urb_struct iso[2];	/* need two urbs to have one always for pending */
	__u8 ch_idx;		/* link BChannel Fifos to chan[ch_idx] */
	int last_urblen;	/* remember length of last packet */
} usb_fifo;

typedef struct _hfcsusb_t {
	struct list_head	list;
	channel_t		chan[4]; // B1,B2,D,(PCM)

	struct usb_device	*dev;		/* our device */
	struct usb_interface	*intf;		/* used interface */
	int			if_used;	/* used interface number */
	int			alt_used;	/* used alternate config */
	int			cfg_used;	/* configuration index used */
	int			vend_idx;	/* index in hfcsusb_idtab */
	int			packet_size;
	int			iso_packet_size;
	int			disc_flag;	/* 1 if device was disonnected to avoid some USB actions */
	usb_fifo		fifos[HFCUSB_NUM_FIFOS];	/* structure holding all fifo data */

	/* control pipe background handling */
	ctrl_buft		ctrl_buff[HFC_CTRL_BUFSIZE];	/* buffer holding queued data */
	volatile int		ctrl_in_idx, ctrl_out_idx, ctrl_cnt;	/* input/output pointer + count */
	struct urb		*ctrl_urb;	/* transfer structure for control channel */
	struct usb_ctrlrequest	ctrl_write;	/* buffer for control write request */
	struct usb_ctrlrequest	ctrl_read;	/* same for read request */
	int			ctrl_paksize;	/* control pipe packet size */
	int			ctrl_in_pipe, ctrl_out_pipe;	/* handles for control pipe */
	spinlock_t		ctrl_lock;	/* queueing ctrl urbs needs to be locked */

	volatile __u8		threshold_mask;	/* threshold in fifo flow control */
	__u8			old_led_state, led_state;
	
	__u8			portmode; /* TE ?, NT ?, NT Timer runnning? */
	int			nt_timer;
} hfcsusb_t;

/* private vendor specific data */
typedef struct {
	__u8		led_scheme;	// led display scheme
	signed short	led_bits[8];	// array of 8 possible LED bitmask settings
	char		*vend_name;	// device name
} hfcsusb_vdata;

/****************************************/
/* data defining the devices to be used */
/****************************************/
static struct usb_device_id hfcsusb_idtab[] = {
	{
	 USB_DEVICE(0x0959, 0x2bd0),
	 .driver_info = (unsigned long) &((hfcsusb_vdata)
			  {LED_OFF, {4, 0, 2, 1},
			   "ISDN USB TA (Cologne Chip HFC-S USB based)"}),
	},
	{
	 USB_DEVICE(0x0675, 0x1688),
	 .driver_info = (unsigned long) &((hfcsusb_vdata)
			  {LED_SCHEME1, {1, 2, 0, 0},
			   "DrayTek miniVigor 128 USB ISDN TA"}),
	},
	{
	 USB_DEVICE(0x07b0, 0x0007),
	 .driver_info = (unsigned long) &((hfcsusb_vdata)
			  {LED_SCHEME1, {0x80, -64, -32, -16},
			   "Billion tiny USB ISDN TA 128"}),
	},
	{
	 USB_DEVICE(0x0742, 0x2008),
	 .driver_info = (unsigned long) &((hfcsusb_vdata)
			  {LED_SCHEME1, {4, 0, 2, 1},
			   "Stollmann USB TA"}),
	},
	{
	 USB_DEVICE(0x0742, 0x2009),
	 .driver_info = (unsigned long) &((hfcsusb_vdata)
			  {LED_SCHEME1, {4, 0, 2, 1},
			   "Aceex USB ISDN TA"}),
	},
	{
	 USB_DEVICE(0x0742, 0x200A),
	 .driver_info = (unsigned long) &((hfcsusb_vdata)
			  {LED_SCHEME1, {4, 0, 2, 1},
			   "OEM USB ISDN TA"}),
	},
	{
	 USB_DEVICE(0x08e3, 0x0301),
	 .driver_info = (unsigned long) &((hfcsusb_vdata)
			  {LED_SCHEME1, {2, 0, 1, 4},
			   "Olitec USB RNIS"}),
	},
	{
	 USB_DEVICE(0x07fa, 0x0846),
	 .driver_info = (unsigned long) &((hfcsusb_vdata)
			  {LED_SCHEME1, {0x80, -64, -32, -16},
			   "Bewan Modem RNIS USB"}),
	},
	{
	 USB_DEVICE(0x07fa, 0x0847),
	 .driver_info = (unsigned long) &((hfcsusb_vdata)
			  {LED_SCHEME1, {0x80, -64, -32, -16},
			   "Djinn Numeris USB"}),
	},
	{
	 USB_DEVICE(0x07b0, 0x0006),
	 .driver_info = (unsigned long) &((hfcsusb_vdata)
			  {LED_SCHEME1, {0x80, -64, -32, -16},
			   "Twister ISDN TA"}),
	},
	{ }
};

/* some function prototypes */
static int	hfcsusb_l2l1(mISDNinstance_t *inst, struct sk_buff *skb);
static int	setup_bchannel(channel_t * bch, int protocol);
static void	hfcsusb_ph_command(hfcsusb_t * card, u_char command);
static void	release_card(hfcsusb_t * card);


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
		    cpu_to_le16(card->ctrl_buff[card->ctrl_out_idx].hfcs_reg);
		card->ctrl_write.wValue =
		    cpu_to_le16(card->ctrl_buff[card->ctrl_out_idx].reg_val);

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
	
	spin_lock(&card->ctrl_lock);
	if (card->ctrl_cnt >= HFC_CTRL_BUFSIZE)
		return (1);	/* no space left */
	buf = &card->ctrl_buff[card->ctrl_in_idx];	/* pointer to new index */
	buf->hfcs_reg = reg;
	buf->reg_val = val;
	if (++card->ctrl_in_idx >= HFC_CTRL_BUFSIZE)
		card->ctrl_in_idx = 0;	/* pointer wrap */
	if (++card->ctrl_cnt == 1)
		ctrl_start_transfer(card);
	spin_unlock(&card->ctrl_lock);

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


/*********************************/
/* S0 state change event handler */
/*********************************/
static void
S0_new_state(channel_t * dch)
{
	u_int		prim = PH_SIGNAL | INDICATION;
	u_int		para = 0;
	hfcsusb_t	*card = dch->inst.privat;

	if (card->portmode & PORT_MODE_TE) {
		if (dch->debug)
			mISDN_debugprint(&card->chan[D].inst,
				 "%s: TE %d",
				 __FUNCTION__, dch->state);
		
		switch (dch->state) {
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
		if (dch->state== 7)
			test_and_set_bit(FLG_ACTIVE, &dch->Flags);
		else
			test_and_clear_bit(FLG_ACTIVE, &dch->Flags);
		
	} else {
		if (dch->debug)
			mISDN_debugprint(&card->chan[D].inst,
				 "%s: NT %d",
				 __FUNCTION__, dch->state);
				
		switch (dch->state) {
			case (1):
				test_and_clear_bit(FLG_ACTIVE, &dch->Flags);
				card->nt_timer = 0;
				card->portmode &= ~NT_ACTIVATION_TIMER;
				prim = PH_DEACTIVATE | INDICATION;
				para = 0;
				handle_led(card, LED_S0_OFF);
				break;
				
			case (2):
				if (card->nt_timer < 0) {
					card->nt_timer = 0;
					card->portmode &= ~NT_ACTIVATION_TIMER;
					hfcsusb_ph_command(dch->hw, HFC_L1_DEACTIVATE_NT);
				} else {
					card->portmode |= NT_ACTIVATION_TIMER;
					card->nt_timer = NT_T1_COUNT;
					/* allow G2 -> G3 transition */
					queued_Write_hfc(card, HFCUSB_STATES, 2 | HFCUSB_NT_G2_G3);
				}
				return;
			case (3):
				test_and_set_bit(FLG_ACTIVE, &dch->Flags);
				card->nt_timer = 0;
				card->portmode &= ~NT_ACTIVATION_TIMER;
				prim = PH_ACTIVATE | INDICATION;
				para = 0;
				handle_led(card, LED_S0_ON);
				break;
			case (4):
				card->nt_timer = 0;
				card->portmode &= ~NT_ACTIVATION_TIMER;
				return;
			default:
				break;
		}
		mISDN_queue_data(&dch->inst, dch->inst.id | MSG_BROADCAST,
			MGR_SHORTSTATUS | INDICATION, test_bit(FLG_ACTIVE, &dch->Flags) ?
			SSTATUS_L1_ACTIVATED : SSTATUS_L1_DEACTIVATED,
			0, NULL, 0);		
	}		
	mISDN_queue_data(&dch->inst, FLG_MSG_UP, prim, para, 0, NULL, 0);
}

/******************************/
/* trigger S0 state changes   */
/******************************/
static void
state_handler(hfcsusb_t * card, __u8 new_l1_state)
{
	if (new_l1_state == card->chan[D].state
	    || new_l1_state < 1 || new_l1_state > 8)
		return;

	card->chan[D].state = new_l1_state;
	S0_new_state(&card->chan[D]);
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
setup_bchannel(channel_t * bch, int protocol)
{
	__u8 conhdlc, sctrl, sctrl_r;	/* conatainer for new register vals */

	hfcsusb_t *card = bch->inst.privat;

	if (bch->debug & L1_DEB_HSCX)
		mISDN_debugprint(&bch->inst,
				 "protocol %x-->%x channel(%d)",
				 bch->state, protocol,
				 bch->channel);

	/* setup val for CON_HDLC */
	conhdlc = 0;
	if (protocol > ISDN_PID_NONE)
		conhdlc = 8;	/* enable FIFO */

	switch (protocol) {
		case (-1):	/* used for init */
			bch->state = -1;
			/* fall trough */
		case (ISDN_PID_NONE):
			if (bch->state == ISDN_PID_NONE)
				return (0);	/* already in idle state */
			bch->state = ISDN_PID_NONE;
			test_and_clear_bit(FLG_HDLC, &bch->Flags);
			test_and_clear_bit(FLG_TRANSPARENT, &bch->Flags);
			break;
		case (ISDN_PID_L1_B_64TRANS):
			conhdlc |= 2;
			bch->state = protocol;
			set_bit(FLG_TRANSPARENT, &bch->Flags);
			break;
		case (ISDN_PID_L1_B_64HDLC):
			bch->state = protocol;
			set_bit(FLG_HDLC, &bch->Flags);
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

		sctrl = 0x40 + ((card->portmode & PORT_MODE_TE)?0x00:0x04);
		sctrl_r = 0x0;

		if (card->chan[B1].state) {
			sctrl |= ((card->chan[B1].channel)?2:1);
			sctrl_r |= ((card->chan[B1].channel)?2:1);
		}

		if (card->chan[B2].state) {
			sctrl |= ((card->chan[B2].channel)?2:1);
			sctrl_r |= ((card->chan[B2].channel)?2:1);
		}
		
		queued_Write_hfc(card, HFCUSB_SCTRL, sctrl);
		queued_Write_hfc(card, HFCUSB_SCTRL_R, sctrl_r);
	
		if (protocol > ISDN_PID_NONE) {
			handle_led(card, ((bch->channel)?LED_B2_ON:LED_B1_ON));
		} else {
			handle_led(card, ((bch->channel)?LED_B2_OFF:LED_B1_OFF));
		}
	}
	return (0);
}

static void
hfcsusb_ph_command(hfcsusb_t * card, u_char command)
{
	if (card->chan[D].debug & L1_DEB_ISAC)
		mISDN_debugprint(&card->chan[D].inst, "hfcsusb_ph_command %x",
				 command);

	switch (command) {
		case HFC_L1_ACTIVATE_TE:
			/* force sending sending INFO1 */
			queued_Write_hfc(card, HFCUSB_STATES, 0x14);
			/* start l1 activation */
			queued_Write_hfc(card, HFCUSB_STATES, 0x04);
			break;

		case HFC_L1_FORCE_DEACTIVATE_TE:
			queued_Write_hfc(card, HFCUSB_STATES, 0x10);
			queued_Write_hfc(card, HFCUSB_STATES, 0x03);
			break;

		case HFC_L1_ACTIVATE_NT:
			queued_Write_hfc(card, HFCUSB_STATES,
					       HFCUSB_ACTIVATE 
					       | HFCUSB_DO_ACTION 
					       | HFCUSB_NT_G2_G3);
			break;

		case HFC_L1_DEACTIVATE_NT:
			queued_Write_hfc(card, HFCUSB_STATES,
			 		       HFCUSB_DO_ACTION);
			break;
	}
}

/*************************************/
/* Layer 1 D-channel hardware access */
/*************************************/
static int
handle_dmsg(channel_t *dch, struct sk_buff *skb)
{
	int		ret = 0;
	mISDN_head_t	*hh = mISDN_HEAD_P(skb);
	hfcsusb_t	*hw = dch->hw;
		
	if (hh->prim == (PH_SIGNAL | REQUEST)) {
		ret = -EINVAL;
	} else if (hh->prim == (PH_CONTROL | REQUEST)) {
		if (hh->dinfo == HW_RESET) {
			if (dch->state != 0)
				hfcsusb_ph_command(hw, HFC_L1_ACTIVATE_TE);
			skb_trim(skb, 0);
			return(mISDN_queueup_newhead(&dch->inst, 0, PH_CONTROL | INDICATION,HW_POWERUP, skb));
		} else if (hh->dinfo == HW_DEACTIVATE) {
			if (dch->next_skb) {
				dev_kfree_skb(dch->next_skb);
				dch->next_skb = NULL;
			}
			test_and_clear_bit(FLG_TX_NEXT, &dch->Flags);
			test_and_clear_bit(FLG_TX_BUSY, &dch->Flags);
#ifdef FIXME
			if (test_and_clear_bit(FLG_L1_DBUSY, &dch->Flags))
				dchannel_sched_event(dch, D_CLEARBUSY);
#endif
		} else if (hh->dinfo == HW_POWERUP) {
			hfcsusb_ph_command(hw, HFC_L1_FORCE_DEACTIVATE_TE);
		} else {
			if (dch->debug & L1_DEB_WARN)
				mISDN_debugprint(&dch->inst,
					"hfcsusb_l1hw unknown ctrl %x",
					hh->dinfo);
			ret = -EINVAL;
		}
	} else if (hh->prim == (PH_ACTIVATE | REQUEST)) {
		if (hw->portmode & PORT_MODE_NT) {
			hfcsusb_ph_command(hw, HFC_L1_ACTIVATE_NT);
		} else {
			if (dch->debug & L1_DEB_WARN)
				mISDN_debugprint(&dch->inst,
					"%s: PH_ACTIVATE none NT mode",
					__FUNCTION__);
			ret = -EINVAL;
		}
	} else if (hh->prim == (PH_DEACTIVATE | REQUEST)) {
		if (hw->portmode & PORT_MODE_NT) {
			hfcsusb_ph_command(hw, HFC_L1_DEACTIVATE_NT);
			if (test_and_clear_bit(FLG_TX_NEXT, &dch->Flags)) {
				dev_kfree_skb(dch->next_skb);
				dch->next_skb = NULL;
			}
			if (dch->tx_skb) {
				dev_kfree_skb(dch->tx_skb);
				dch->tx_skb = NULL;
			}
			dch->tx_idx = 0;
			if (dch->rx_skb) {
				dev_kfree_skb(dch->rx_skb);
				dch->rx_skb = NULL;
			}
			test_and_clear_bit(FLG_TX_BUSY, &dch->Flags);
			test_and_clear_bit(FLG_ACTIVE, &dch->Flags);
		} else {
			if (dch->debug & L1_DEB_WARN)
				mISDN_debugprint(&dch->inst,
					"%s: PH_DEACTIVATE none NT mode",
					__FUNCTION__);
			ret = -EINVAL;
		}
	} else if ((hh->prim & MISDN_CMD_MASK) == MGR_SHORTSTATUS) {
		u_int temp = hh->dinfo & SSTATUS_ALL; // remove SSTATUS_BROADCAST_BIT
		if ((hw->portmode & PORT_MODE_NT) &&
			(temp == SSTATUS_ALL || temp == SSTATUS_L1)) {
			if (hh->dinfo & SSTATUS_BROADCAST_BIT)
				temp = dch->inst.id | MSG_BROADCAST;
			else
				temp = hh->addr | FLG_MSG_TARGET;
			skb_trim(skb, 0);
			hh->dinfo = test_bit(FLG_ACTIVE, &dch->Flags) ?
				SSTATUS_L1_ACTIVATED : SSTATUS_L1_DEACTIVATED;
			hh->prim = MGR_SHORTSTATUS | CONFIRM;
			return(mISDN_queue_message(&dch->inst, temp, skb));
		}
		ret = -EOPNOTSUPP;
	} else {
		printk(KERN_WARNING "%s %s: unknown prim(%x)\n",
		       dch->inst.name, __FUNCTION__, hh->prim);
		ret = -EAGAIN;
	}
	if (!ret)
		dev_kfree_skb(skb);
	return (ret);
}

/*************************************/
/* Layer 1 B-channel hardware access */
/*************************************/
static int
handle_bmsg(channel_t *bch, struct sk_buff *skb)
{
	int		ret = 0;
	mISDN_head_t	*hh = mISDN_HEAD_P(skb);

	if ((hh->prim == (PH_ACTIVATE | REQUEST)) ||
		(hh->prim == (DL_ESTABLISH | REQUEST))) {
		if (!test_and_set_bit(FLG_ACTIVE, &bch->Flags)) {
			ret = setup_bchannel(bch, bch->inst.pid.protocol[1]);
			if (bch->inst.pid.protocol[2] == ISDN_PID_L2_B_TRANS)
				test_and_set_bit(FLG_L2DATA, &bch->Flags);
		}
#ifdef FIXME
		if (bch->inst.pid.protocol[2] == ISDN_PID_L2_B_RAWDEV)
			if (bch->dev)
				if_link(&bch->dev->rport.pif,
					hh->prim | CONFIRM, 0, 0, NULL, 0);
#endif
		skb_trim(skb, 0);
		return(mISDN_queueup_newhead(&bch->inst, 0, hh->prim | CONFIRM, ret, skb));
	} else if ((hh->prim == (PH_DEACTIVATE | REQUEST)) ||
		(hh->prim == (DL_RELEASE | REQUEST)) ||
		((hh->prim == (PH_CONTROL | REQUEST) && (hh->dinfo == HW_DEACTIVATE)))) {

		if (test_and_clear_bit(FLG_TX_NEXT, &bch->Flags)) {
			dev_kfree_skb(bch->next_skb);
			bch->next_skb = NULL;
		}
		if (bch->tx_skb) {
			dev_kfree_skb(bch->tx_skb);
			bch->tx_skb = NULL;
		}
		bch->tx_idx = 0;
		if (bch->rx_skb) {
			dev_kfree_skb(bch->rx_skb);
			bch->rx_skb = NULL;
		}
		test_and_clear_bit(FLG_L2DATA, &bch->Flags);
		test_and_clear_bit(FLG_TX_BUSY, &bch->Flags);
		setup_bchannel(bch, ISDN_PID_NONE);
		test_and_clear_bit(FLG_ACTIVE, &bch->Flags);
		skb_trim(skb, 0);
		if (hh->prim != (PH_CONTROL | REQUEST)) {
#ifdef FIXME
			if (bch->inst.pid.protocol[2] == ISDN_PID_L2_B_RAWDEV)
				if (bch->dev)
					if_link(&bch->dev->rport.pif,
						hh->prim | CONFIRM, 0, 0, NULL, 0);
#endif
			if (!mISDN_queueup_newhead(&bch->inst, 0, hh->prim | CONFIRM, 0, skb))
				return(0);
		}
	} else if (hh->prim == (PH_CONTROL | REQUEST)) {
		// do not handle PH_CONTROL | REQUEST ??
	} else {
		printk(KERN_WARNING "%s %s: unknown prim(%x)\n",
			bch->inst.name, __FUNCTION__, hh->prim);
		ret = -EAGAIN;
	}
	if (!ret)
		dev_kfree_skb(skb);
	return (ret);
}

/******************************/
/* Layer2 -> Layer 1 Transfer */
/******************************/
static int
hfcsusb_l2l1(mISDNinstance_t *inst, struct sk_buff *skb)
{
	channel_t	*chan = container_of(inst, channel_t, inst);
	int		ret = 0;
	mISDN_head_t	*hh = mISDN_HEAD_P(skb);

	if ((hh->prim == PH_DATA_REQ) || (hh->prim == DL_DATA_REQ)) {
		ret = channel_senddata(chan, hh->dinfo, skb);
		if (ret > 0) { 
			/* data gets transmitted later in USB ISO OUT traffic */
			ret = 0;
		}
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


static int
hfcsusb_manager(void *data, u_int prim, void *arg)
{
	hfcsusb_t *hw = NULL;
	mISDNinstance_t *inst = data;
	struct sk_buff *skb;
	int channel = -1;
	int i;
	channel_t *chan = NULL;
	u_long flags;

	if (!data) {
		MGR_HASPROTOCOL_HANDLER(prim, arg, &hw_mISDNObj)
		    printk(KERN_ERR "%s %s: no data prim %x arg %p\n",
			   hw->chan[D].inst.name, __FUNCTION__, prim, arg);
		return (-EINVAL);
	}
	
	spin_lock_irqsave(&hw_mISDNObj.lock, flags);

	/* find channel and card */
	list_for_each_entry(hw, &hw_mISDNObj.ilist, list) {
		i = 0;
		while (i < MAX_CHAN) {
			if (hw->chan[i].Flags &&
				&hw->chan[i].inst == inst) {
				channel = i;
				chan = &hw->chan[i];
				break;
			}
			i++;
		}
		if (channel >= 0)
			break;
	}
	spin_unlock_irqrestore(&hw_mISDNObj.lock, flags);
	
	if (channel < 0) {
		printk(KERN_ERR
		       "%s: no card/channel found  data %p prim %x arg %p\n",
		       __FUNCTION__, data, prim, arg);
		return (-EINVAL);
	}

	switch (prim) {
		case MGR_REGLAYER | CONFIRM:
			mISDN_setpara(chan, &inst->st->para);
			break;
		case MGR_UNREGLAYER | REQUEST:
			if ((skb = create_link_skb(PH_CONTROL | REQUEST,
				HW_DEACTIVATE, 0, NULL, 0))) {
				if (hfcsusb_l2l1(inst, skb))
					dev_kfree_skb(skb);
			} else
				printk(KERN_WARNING "no SKB in %s MGR_UNREGLAYER | REQUEST\n", __FUNCTION__);
			mISDN_ctrl(inst, MGR_UNREGLAYER | REQUEST, NULL);
			break;
		case MGR_CLRSTPARA | INDICATION:
			arg = NULL;
		case MGR_ADDSTPARA | INDICATION:
			mISDN_setpara(chan, arg);
			break;
		case MGR_RELEASE | INDICATION:
			if (channel == 2) {
				release_card(hw);
			} else {
				hw_mISDNObj.refcnt--;
			}
			break;
		case MGR_SETSTACK | INDICATION:
			if ((channel != 2) && (inst->pid.global == 2)) {
				if ((skb = create_link_skb(PH_ACTIVATE | REQUEST,
					0, 0, NULL, 0))) {
					if (hfcsusb_l2l1(inst, skb))
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
		case MGR_GLOBALOPT | REQUEST:
			if (arg) {
				// FIXME: detect cards with HEADSET
				u_int *gopt = arg;
				*gopt = GLOBALOPT_INTERNAL_CTRL |
				    GLOBALOPT_EXTERNAL_EQUIPMENT |
				    GLOBALOPT_HANDSET;
			} else
				return (-EINVAL);
			break;
		case MGR_SELCHANNEL | REQUEST:
			// no special procedure
			return (-EINVAL);
			PRIM_NOT_HANDLED(MGR_CTRLREADY | INDICATION);
		default:
			printk(KERN_WARNING "%s %s: prim %x not handled\n",
			       hw->chan[D].inst.name, __FUNCTION__, prim);
			return (-EINVAL);
	}
	return (0);
}


/***********************************************/
/* collect data from interrupt or isochron in  */
/***********************************************/
static void
collect_rx_frame(usb_fifo * fifo, __u8 * data, unsigned int len, int finish)
{
	hfcsusb_t	*card = fifo->card;
	channel_t	*ch = &card->chan[fifo->ch_idx];
	struct sk_buff	*skb;	/* data buffer for upper layer */
	int		fifon;

	if (!len)
		return;
		
	fifon = fifo->fifonum;
	
	if (!ch->rx_skb) {
		ch->rx_skb = alloc_stack_skb(ch->maxlen + 3, ch->up_headerlen);
		if (!ch->rx_skb) {
			printk(KERN_DEBUG "%s: No mem for rx_skb\n", __FUNCTION__);
			return;
		}
	}
	memcpy(skb_put(ch->rx_skb, len), data, len);

	if (test_bit(FLG_HDLC, &ch->Flags)) {
		/* we have a complete hdlc packet */
		if (finish) {
			if ((ch->rx_skb->len > 3) &&
			   (!(ch->rx_skb->data[ch->rx_skb->len - 1]))) {

				/* remove CRC & status */
				skb_trim(ch->rx_skb, ch->rx_skb->len - 3);

				if (ch->rx_skb->len < MISDN_COPY_SIZE) {
					skb = alloc_stack_skb(ch->rx_skb->len, ch->up_headerlen);
					if (skb) {
						memcpy(skb_put(skb, ch->rx_skb->len),
							ch->rx_skb->data, ch->rx_skb->len);
						skb_trim(ch->rx_skb, 0);
					} else {
						skb = ch->rx_skb;
						ch->rx_skb = NULL;
					}
				} else {
					skb = ch->rx_skb;
					ch->rx_skb = NULL;
				}
				queue_ch_frame(ch, INDICATION, MISDN_ID_ANY, skb);				
				
			} else {
				printk ("HFC-S USB: CRC or minlen ERROR\n");
			}
		}
	} else {
		if (ch->rx_skb->len >= TRANSP_PACKET_SIZE) {
			/* deliver transparent data to layer2 */
			queue_ch_frame(ch, INDICATION, MISDN_ID_ANY, ch->rx_skb);
			ch->rx_skb = NULL;
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

				if (fifon == HFCUSB_D_RX) {
					/* the S0 state is in the upper half
					   of the 1st status byte */
					state_handler(card, buf[0] >> 4);
				}

				eof[fifon] = buf[0] & 1;
				if (len > 2)
					collect_rx_frame(fifo, buf + 2,
							 len - 2,
							 (len < maxlen) ? eof[fifon] : 0);
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


/***********************************/
/* check if new buffer for channel */
/* is waitinng is transmitt queue  */
/***********************************/
int
next_tx_frame(hfcsusb_t * hw, __u8 channel)
{
	channel_t *ch = &hw->chan[channel];

	if (ch->tx_skb)
		dev_kfree_skb(ch->tx_skb);
	if (test_and_clear_bit(FLG_TX_NEXT, &ch->Flags)) {
		ch->tx_skb = ch->next_skb;
		if (ch->tx_skb) {
			mISDN_head_t *hh = mISDN_HEAD_P(ch->tx_skb);
			ch->next_skb = NULL;
			test_and_clear_bit(FLG_TX_NEXT, &ch->Flags);
			ch->tx_idx = 0;
			queue_ch_frame(ch, CONFIRM, hh->dinfo, NULL);
			return (1);
		} else {
			printk(KERN_WARNING
			       "%s channel(%i) TX_NEXT without skb\n",
			       ch->inst.name, channel);
			test_and_clear_bit(FLG_TX_NEXT, &ch->Flags);
		}
	} else
		ch->tx_skb = NULL;
	test_and_clear_bit(FLG_TX_BUSY, &ch->Flags);
	return (0);
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
	channel_t *ch = &card->chan[fifo->ch_idx];
	int k, tx_offset, num_isoc_packets, sink, remain, current_len,
	    errcode;
	int frame_complete, fifon, status;
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

		/* Generate next Iso Packets */
		for (k = 0; k < num_isoc_packets; ++k) {
			if (ch->tx_skb) {
				remain = ch->tx_skb->len - ch->tx_idx;
			} else {
				remain = 0; 
			}
			if (remain>0) {
				/* we lower data margin every msec */
				fifo->bit_line -= sink;
				current_len = (0 - fifo->bit_line) / 8;
				
				/* maximum 15 byte for every ISO packet makes our life easier */
				if (current_len > 14)
					current_len = 14;
				current_len = (remain <= current_len) ? remain : current_len;

				/* how much bit do we put on the line? */
				fifo->bit_line += current_len * 8;

				context_iso_urb->buffer[tx_offset] = 0;
				if (current_len == remain) {
					if (test_bit(FLG_HDLC, &ch->Flags)) {
						/* here frame completion */
						context_iso_urb->buffer[tx_offset] = 1;
						/* add 2 byte flags and 16bit CRC at end of ISDN frame */
						fifo->bit_line += 32;
					}
					frame_complete = 1;
				}

				/* copy tx data to iso-urb buffer */
				memcpy(context_iso_urb->buffer + tx_offset + 1,
				       (ch->tx_skb->data + ch->tx_idx), current_len);
				ch->tx_idx += current_len;

				/* define packet delimeters within the URB buffer */
				urb->iso_frame_desc[k].offset = tx_offset;
				urb->iso_frame_desc[k].length = current_len + 1;

				tx_offset += (current_len + 1);
			} else {
				urb->iso_frame_desc[k].offset = tx_offset++;

				urb->iso_frame_desc[k].length = 1;
				fifo->bit_line -= sink;	/* we lower data margin every msec */

				if (fifo->bit_line < BITLINE_INF) {
					fifo->bit_line = BITLINE_INF;
				}
			}

			if (frame_complete)
				next_tx_frame(card, fifo->ch_idx);

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
		if ((fifon == HFCUSB_D_TX) && (card->portmode & PORT_MODE_NT)
		    && (card->portmode & NT_ACTIVATION_TIMER)) {
			if ((--card->nt_timer) < 0)
				S0_new_state(&card->chan[D]);
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
	}
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

	if (card->portmode & PORT_MODE_NT) {
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
	card->ctrl_read.wLength = cpu_to_le16(1);
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
	int	i;
	u_long	flags;

	if (debug & 0x10000)
		printk(KERN_DEBUG "%s\n", __FUNCTION__);

	setup_bchannel(&card->chan[B1], ISDN_PID_NONE);
	setup_bchannel(&card->chan[B2], ISDN_PID_NONE);
	mISDN_freechannel(&card->chan[B1]);
	mISDN_freechannel(&card->chan[B2]);
	mISDN_freechannel(&card->chan[D]);
	mISDN_ctrl(&card->chan[D].inst, MGR_UNREGLAYER | REQUEST, NULL);
	
	spin_lock_irqsave(&hw_mISDNObj.lock, flags);
	list_del(&card->list);
	spin_unlock_irqrestore(&hw_mISDNObj.lock, flags);
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
	if (card->intf)
		usb_set_intfdata(card->intf, NULL);
	kfree(card);
}

static int
setup_instance(hfcsusb_t * card)
{
	int		i, err;
	mISDN_pid_t	pid;
	u_long		flags;
	
	spin_lock_irqsave(&hw_mISDNObj.lock, flags);
		
	list_add_tail(&card->list, &hw_mISDNObj.ilist);
	spin_unlock_irqrestore(&hw_mISDNObj.lock, flags);
	card->chan[D].debug = debug;
	
	spin_lock_init(&card->ctrl_lock);
	
	/* link card->fifos[] to card->chan[] */
	card->fifos[HFCUSB_D_RX].ch_idx = D;
	card->fifos[HFCUSB_D_TX].ch_idx = D;
	card->fifos[HFCUSB_B1_RX].ch_idx = B1;
	card->fifos[HFCUSB_B1_TX].ch_idx = B1;
	card->fifos[HFCUSB_B2_RX].ch_idx = B2;
	card->fifos[HFCUSB_B2_TX].ch_idx = B2;
	card->fifos[HFCUSB_PCM_RX].ch_idx = PCM;
	card->fifos[HFCUSB_PCM_TX].ch_idx = PCM;
	
	card->chan[D].channel = D;
	card->chan[D].state = 0;
	card->chan[D].inst.hwlock = NULL;
	card->chan[D].inst.pid.layermask = ISDN_LAYER(0);
	card->chan[D].inst.pid.protocol[0] = ISDN_PID_L0_TE_S0;
	card->chan[D].inst.class_dev.dev = &card->dev->dev;
	mISDN_init_instance(&card->chan[D].inst, &hw_mISDNObj, card, hfcsusb_l2l1);
	sprintf(card->chan[D].inst.name, "hfcsusb_%d", hfcsusb_cnt + 1);
	mISDN_set_dchannel_pid(&pid, protocol[hfcsusb_cnt], layermask[hfcsusb_cnt]);
	mISDN_initchannel(&card->chan[D], MSK_INIT_DCHANNEL, MAX_DFRAME_LEN_L1);
	card->chan[D].hw = card;
	card->portmode = 0;
	
	for (i = B1; i <= B2; i++) {
		card->chan[i].channel = i;
		mISDN_init_instance(&card->chan[i].inst, &hw_mISDNObj, card, hfcsusb_l2l1);
		card->chan[i].inst.pid.layermask = ISDN_LAYER(0);
		card->chan[i].inst.hwlock = NULL;
		card->chan[i].inst.class_dev.dev = &card->dev->dev;
		card->chan[i].debug = debug;
		sprintf(card->chan[i].inst.name, "%s B%d",
			card->chan[D].inst.name, i + 1);
		mISDN_initchannel(&card->chan[i], MSK_INIT_BCHANNEL, MAX_DATA_MEM);
		card->chan[i].hw = card;
#ifdef FIXME
		if (card->chan[i].dev) {
			card->chan[i].dev->wport.pif.func = hfcsusb_l2l1;
			card->chan[i].dev->wport.pif.fdata = &card->chan[i];
		}
#endif
	}
	
	card->chan[PCM].channel = PCM;

	if (protocol[hfcsusb_cnt] & 0x10) {
		// NT Mode
		printk (KERN_INFO "%s wants NT Mode\n", card->chan[D].inst.name);
		card->chan[D].inst.pid.protocol[0] = ISDN_PID_L0_NT_S0;
		card->chan[D].inst.pid.protocol[1] = ISDN_PID_L1_NT_S0;
		pid.protocol[0] = ISDN_PID_L0_NT_S0;
		pid.protocol[1] = ISDN_PID_L1_NT_S0;
		card->chan[D].inst.pid.layermask |= ISDN_LAYER(1);
		pid.layermask |= ISDN_LAYER(1);
		if (layermask[i] & ISDN_LAYER(2))
			pid.protocol[2] = ISDN_PID_L2_LAPD_NET;
		/* select NT mode with activated NT Timer (T1) */
		card->portmode |= (PORT_MODE_NT | NT_ACTIVATION_TIMER);
	} else {
		printk (KERN_INFO "%s wants TE Mode\n", card->chan[D].inst.name);
		// TE Mode
		card->chan[D].inst.pid.protocol[0] = ISDN_PID_L0_TE_S0;
		card->portmode |= PORT_MODE_TE;
	}

	if (debug)
		printk(KERN_DEBUG
		       "hfcsusb card %p dch %p bch1 %p bch2 %p\n", card,
		       &card->chan[D], &card->chan[B1], &card->chan[B2]);

	err = setup_hfcsusb(card);
	if (err) {
		mISDN_freechannel(&card->chan[D]);
		mISDN_freechannel(&card->chan[B2]);
		mISDN_freechannel(&card->chan[B1]);
		spin_lock_irqsave(&hw_mISDNObj.lock, flags);
		list_del(&card->list);
		spin_unlock_irqrestore(&hw_mISDNObj.lock, flags);
		kfree(card);
		return (err);
	}
	hfcsusb_cnt++;
	err = mISDN_ctrl(NULL, MGR_NEWSTACK | REQUEST, &card->chan[D].inst);
	if (err) {
		release_card(card);
		return (err);
	}
	for (i = B1; i <= B2; i++) {
		err = mISDN_ctrl(card->chan[D].inst.st,
			MGR_NEWSTACK | REQUEST, &card->chan[i].inst);
		if (err) {
			printk(KERN_ERR "MGR_ADDSTACK bchan error %d\n", err);
			mISDN_ctrl(card->chan[D].inst.st, MGR_DELSTACK | REQUEST, NULL);
			return (err);
		}
		setup_bchannel(&card->chan[i], -1);
	}
	if (debug)
		printk(KERN_DEBUG "%s lm %x\n", __FUNCTION__, pid.layermask);
	err = mISDN_ctrl(card->chan[D].inst.st, MGR_SETSTACK | REQUEST, &pid);
	if (err) {
		printk(KERN_ERR "MGR_SETSTACK REQUEST dch err(%d)\n", err);
		mISDN_ctrl(card->chan[D].inst.st, MGR_DELSTACK | REQUEST, NULL);
		return (err);
	}

	mISDN_ctrl(card->chan[D].inst.st, MGR_CTRLREADY | INDICATION, NULL);
	usb_set_intfdata(card->intf, card);
	return (0);
}


/*************************************************/
/* function called to probe a new plugged device */
/*************************************************/

static int
hfcsusb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device		*dev = interface_to_usbdev(intf);
	hfcsusb_t			*card;
	struct usb_host_interface	*iface = intf->cur_altsetting;
	struct usb_host_interface	*iface_used = NULL;
	struct usb_host_endpoint	*ep;
	int 				ifnum = iface->desc.bInterfaceNumber;
	int				i, idx, alt_idx, probe_alt_setting, vend_idx, cfg_used, *vcf,
					attr, cfg_found, ep_addr;
	int				cmptbl[16], small_match, iso_packet_size, packet_size, alt_used = 0;
	hfcsusb_vdata			*driver_info;

	vend_idx = 0xffff;
	for (i = 0; hfcsusb_idtab[i].idVendor; i++) {
		if ((le16_to_cpu(dev->descriptor.idVendor) == hfcsusb_idtab[i].idVendor)
		    && (le16_to_cpu(dev->descriptor.idProduct) == hfcsusb_idtab[i].idProduct)) {
			vend_idx = i;
			continue;
		}
	}

	printk(KERN_INFO
	       "HFC-S USB: probing interface(%d) actalt(%d) minor(%d) vend_idx(%d)\n",
	       ifnum, iface->desc.bAlternateSetting, intf->minor, vend_idx);

	if (vend_idx == 0xffff) {
		printk(KERN_WARNING
		       "HFC-S USB: no valid vendor found in USB descriptor\n");
		return (-EIO);
	}
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
			for (i = 0; i < iface->desc.bNumEndpoints; i++) {
				ep_addr = ep->desc.bEndpointAddress;
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
				if (attr == USB_ENDPOINT_XFER_INT &&
					ep->desc.bInterval < vcf[17]) {
					cfg_found = 0;
				}
				ep++;
			}
			for (i = 0; i < 16; i++) {
				/* all entries must be EP_NOP or EP_NUL for a valid config */
				if (cmptbl[i] != EP_NOP && cmptbl[i] != EP_NUL)
					cfg_found = 0;
			}
			if (cfg_found) {
				if (cfg_used < small_match) {
					small_match = cfg_used;
					alt_used = probe_alt_setting;
					iface_used = iface;
				}
			}
			cfg_used++;
		}
		alt_idx++;
	}	/* (alt_idx < intf->num_altsetting) */

	/* not found a valid USB Ta Endpoint config */
	if (small_match == 0xffff) {
		printk(KERN_WARNING
		       "HFC-S USB: no valid endpoint found in USB descriptor\n");
		return (-EIO);
	}
	iface = iface_used;
	card = kmalloc(sizeof(hfcsusb_t), GFP_KERNEL);
	if (!card)
		return (-ENOMEM);	/* got no mem */
	memset(card, 0, sizeof(hfcsusb_t));

	ep = iface->endpoint;
	vcf = validconf[small_match];

	for (i = 0; i < iface->desc.bNumEndpoints; i++) {
		usb_fifo	*f;

		ep_addr = ep->desc.bEndpointAddress;
		/* get endpoint base */
		idx = ((ep_addr & 0x7f) - 1) * 2;
		if (ep_addr & 0x80)
			idx++;
		f = &card->fifos[idx & 7];

		/* init Endpoints */
		if (vcf[idx] == EP_NOP || vcf[idx] == EP_NUL) {
			ep++;
			continue;
		}
		switch (ep->desc.bmAttributes) {
			case USB_ENDPOINT_XFER_INT:
				f->pipe = usb_rcvintpipe(dev,
					ep->desc.bEndpointAddress);
				f->usb_transfer_mode = USB_INT;
				packet_size = le16_to_cpu(ep->desc.wMaxPacketSize);
				break;
			case USB_ENDPOINT_XFER_BULK:
				if (ep_addr & 0x80)
					f->pipe = usb_rcvbulkpipe(dev,
						ep->desc.bEndpointAddress);
				else
					f->pipe = usb_sndbulkpipe(dev,
						ep->desc.bEndpointAddress);
				f->usb_transfer_mode = USB_BULK;
				packet_size = le16_to_cpu(ep->desc.wMaxPacketSize);
				break;
			case USB_ENDPOINT_XFER_ISOC:
				if (ep_addr & 0x80)
					f->pipe = usb_rcvisocpipe(dev,
						ep->desc.bEndpointAddress);
				else
					f->pipe = usb_sndisocpipe(dev,
						ep->desc.bEndpointAddress);
				f->usb_transfer_mode = USB_ISOC;
				iso_packet_size = le16_to_cpu(ep->desc.wMaxPacketSize);
				break;
			default:
				f->pipe = 0;
		}	/* switch attribute */

		if (f->pipe) {
			f->fifonum = idx & 7;
			f->card = card;
			f->usb_packet_maxlen = le16_to_cpu(ep->desc.wMaxPacketSize);
			f->intervall = ep->desc.bInterval;
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
	card->ctrl_out_pipe = usb_sndctrlpipe(card->dev, 0);
	card->ctrl_urb = usb_alloc_urb(0, GFP_KERNEL);

	driver_info = (hfcsusb_vdata *) hfcsusb_idtab[vend_idx].driver_info;
	printk(KERN_INFO "HFC-S USB: detected \"%s\"\n",
		driver_info->vend_name);
	printk(KERN_INFO "HFC-S USB: Endpoint-Config: %s (if=%d alt=%d)\n",
		conf_str[small_match], ifnum, alt_used);			       

	card->intf = intf;
	if (setup_instance(card)) {
		return (-EIO);
	}
	return (0);
}

/****************************************************/
/* function called when an active device is removed */
/****************************************************/
static void
hfcsusb_disconnect(struct usb_interface *intf)
{
	hfcsusb_t *card = usb_get_intfdata(intf);

	printk(KERN_INFO "HFC-S USB: device disconnect\n");
	if (!card) {
		if (debug & 0x10000)
			printk(KERN_DEBUG "%s : NO CONTEXT!\n", __FUNCTION__);
		return;
	}
	if (debug & 0x10000)
		printk(KERN_DEBUG "%s\n", __FUNCTION__);
	card->disc_flag = 1;
	mISDN_ctrl(card->chan[D].inst.st, MGR_DELSTACK | REQUEST, NULL);
//	release_card(card);
	usb_set_intfdata(intf, NULL);
}				/* hfcsusb_disconnect */


/************************************/
/* our driver information structure */
/************************************/
static struct usb_driver hfcsusb_drv = {
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
	spin_lock_init(&hw_mISDNObj.lock);
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

	list_for_each_entry_safe(card, next, &hw_mISDNObj.ilist, list) {
		handle_led(card, LED_POWER_OFF);
	}
	if ((err = mISDN_unregister(&hw_mISDNObj))) {
		printk(KERN_ERR "Can't unregister hfcsusb error(%d)\n",
		       err);
	}
	/* unregister Hardware */
	usb_deregister(&hfcsusb_drv);	/* release our driver */
}

module_init(hfcsusb_init);
module_exit(hfcsusb_cleanup);

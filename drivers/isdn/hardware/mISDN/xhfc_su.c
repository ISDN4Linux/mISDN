/* xhfc_su.c
 * mISDN driver for Cologne Chip AG's XHFC
 *
 * (c) 2007,2008 Copyright Cologne Chip AG
 * Authors : Martin Bachem, Joerg Ciesielski
 * Contact : info@colognechip.com
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
 * module params
 *   debug=<n>, default=0, with n=0xHHHHGGGG
 *      H - l1 driver flags described in hfcs_usb.h
 *      G - common mISDN debug flags described at mISDNhw.h
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mISDNhw.h>
#include "xhfc_su.h"

#if BRIDGE == BRIDGE_PCI2PI
#include <linux/pci.h>
#include "xhfc_pci2pi.h"
#endif

const char *xhfc_rev = "Revision: 0.2.0 2016-06-10";

/* modules params */
static unsigned int debug = 0;

/* driver globbls */
static int xhfc_cnt;

#ifdef MODULE
MODULE_AUTHOR("Martin Bachem");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
module_param(debug, uint, S_IRUGO | S_IWUSR);
#endif

/* prototypes for static functions */
static int xhfc_l2l1B(struct mISDNchannel *ch, struct sk_buff *skb);
static int xhfc_l2l1D(struct mISDNchannel *ch, struct sk_buff *skb);
static int xhfc_bctrl(struct mISDNchannel *ch, u_int cmd, void *arg);
static int xhfc_dctrl(struct mISDNchannel *ch, u_int cmd, void *arg);
static int xhfc_setup_bch(struct bchannel *bch, int protocol);
static void xhfc_setup_dch(struct dchannel *dch);
static void xhfc_write_fifo(struct xhfc *xhfc, __u8 channel);
static void xhfc_bh_handler(unsigned long ul_hw);
static void ph_state(struct dchannel *dch);
static void f7_timer_expire(struct timer_list *t);
/*
 * Physical S/U commands to control Line Interface
 */
char *XHFC_PH_COMMANDS[] = {
	"L1_ACTIVATE_TE",
	"L1_FORCE_DEACTIVATE_TE",
	"L1_ACTIVATE_NT",
	"L1_DEACTIVATE_NT",
	"L1_SET_TESTLOOP_B1",
	"L1_SET_TESTLOOP_B2",
	"L1_SET_TESTLOOP_D",
	"L1_UNSET_TESTLOOP_B1",
	"L1_UNSET_TESTLOOP_B2",
	"L1_UNSET_TESTLOOP_D"
};


static inline void
xhfc_waitbusy(struct xhfc *xhfc)
{
	while (read_xhfc(xhfc, R_STATUS) & M_BUSY);
}

static inline void
xhfc_selfifo(struct xhfc *xhfc, __u8 fifo)
{
	write_xhfc(xhfc, R_FIFO, fifo);
	xhfc_waitbusy(xhfc);
}

static inline void
xhfc_inc_f(struct xhfc *xhfc)
{
	write_xhfc(xhfc, A_INC_RES_FIFO, M_INC_F);
	xhfc_waitbusy(xhfc);
}

static inline void
xhfc_resetfifo(struct xhfc *xhfc)
{
	write_xhfc(xhfc, A_INC_RES_FIFO, M_RES_FIFO | M_RES_FIFO_ERR);
	xhfc_waitbusy(xhfc);
}

/*
 * disable all interrupts by disabling M_GLOB_IRQ_EN
 */
void
disable_interrupts(struct xhfc *xhfc)
{
	if (debug & DEBUG_HW)
		printk(KERN_INFO "%s %s\n", xhfc->name, __FUNCTION__);
	SET_V_GLOB_IRQ_EN(xhfc->irq_ctrl, 0);
	write_xhfc(xhfc, R_IRQ_CTRL, xhfc->irq_ctrl);
}

/*
 * start interrupt and set interrupt mask
 */
void
enable_interrupts(struct xhfc *xhfc)
{
	if (debug & DEBUG_HW)
		printk(KERN_INFO "%s %s\n", xhfc->name, __FUNCTION__);

	write_xhfc(xhfc, R_SU_IRQMSK, xhfc->su_irqmsk);

	/* use defined timer interval */
	write_xhfc(xhfc, R_TI_WD, xhfc->ti_wd);
	SET_V_TI_IRQMSK(xhfc->misc_irqmsk, 1);
	write_xhfc(xhfc, R_MISC_IRQMSK, xhfc->misc_irqmsk);

	/* clear all pending interrupts bits */
	read_xhfc(xhfc, R_MISC_IRQ);
	read_xhfc(xhfc, R_SU_IRQ);
	read_xhfc(xhfc, R_FIFO_BL0_IRQ);
	read_xhfc(xhfc, R_FIFO_BL1_IRQ);
	read_xhfc(xhfc, R_FIFO_BL2_IRQ);
	read_xhfc(xhfc, R_FIFO_BL3_IRQ);

	/* enable global interrupts */
	SET_V_GLOB_IRQ_EN(xhfc->irq_ctrl, 1);
	SET_V_FIFO_IRQ_EN(xhfc->irq_ctrl, 1);
	write_xhfc(xhfc, R_IRQ_CTRL, xhfc->irq_ctrl);
}

/*
 * Setup S/U interface, enable/disable B-Channels
 */
static void
setup_su(struct xhfc *xhfc, __u8 pt, __u8 bc, __u8 enable)
{
	struct port *port = &xhfc->port[pt];

	if (!((bc == 0) || (bc == 1))) {
		printk(KERN_INFO "%s %s: pt(%i) ERROR: bc(%i) unvalid!\n",
		       xhfc->name, __FUNCTION__, pt, bc);
		return;
	}

	if (debug & DEBUG_HW)
		printk(KERN_INFO "%s %s %s pt(%i) bc(%i)\n",
		       xhfc->name, __FUNCTION__,
		       (enable) ? ("enable") : ("disable"), pt, bc);

	if (bc) {
		SET_V_B2_RX_EN(port->su_ctrl2, (enable ? 1 : 0));
		SET_V_B2_TX_EN(port->su_ctrl0, (enable ? 1 : 0));
	} else {
		SET_V_B1_RX_EN(port->su_ctrl2, (enable ? 1 : 0));
		SET_V_B1_TX_EN(port->su_ctrl0, (enable ? 1 : 0));
	}

	if (xhfc->port[pt].mode & PORT_MODE_NT)
		SET_V_SU_MD(xhfc->port[pt].su_ctrl0, 1);

	write_xhfc(xhfc, R_SU_SEL, pt);
	write_xhfc(xhfc, A_SU_CTRL0, xhfc->port[pt].su_ctrl0);
	write_xhfc(xhfc, A_SU_CTRL2, xhfc->port[pt].su_ctrl2);
}

/*
 * setup port (line interface) with SU_CRTLx
 */
static void
init_su(struct port *port)
{
	struct xhfc *xhfc = port->xhfc;

	if (debug & DEBUG_HW)
		printk(KERN_INFO "%s %s port(%i)\n", xhfc->name,
		       __FUNCTION__, port->idx);

	write_xhfc(xhfc, R_SU_SEL, port->idx);

	if (port->mode & PORT_MODE_NT)
		SET_V_SU_MD(port->su_ctrl0, 1);

	if (port->mode & PORT_MODE_EXCH_POL)
		port->su_ctrl2 = M_SU_EXCHG;

	if (port->mode & PORT_MODE_UP0) {
		SET_V_ST_SEL(port->st_ctrl3, 1);
		write_xhfc(xhfc, A_MS_TX, 0x0F);
		SET_V_ST_SQ_EN(port->su_ctrl0, 1);
	}

	/* configure end of pulse control for ST mode (TE & NT) */
	if (port->mode & PORT_MODE_S0) {
		SET_V_ST_PU_CTRL(port->su_ctrl0, 1);
		port->st_ctrl3 = 0xf8;
	}

	if (debug & DEBUG_HW)
		printk(KERN_INFO "%s %s su_ctrl0(0x%02x) "
		       "su_ctrl1(0x%02x) "
		       "su_ctrl2(0x%02x) "
		       "st_ctrl3(0x%02x)\n",
		       xhfc->name, __FUNCTION__,
		       port->su_ctrl0,
		       port->su_ctrl1, port->su_ctrl2, port->st_ctrl3);

	write_xhfc(xhfc, A_ST_CTRL3, port->st_ctrl3);
	write_xhfc(xhfc, A_SU_CTRL0, port->su_ctrl0);
	write_xhfc(xhfc, A_SU_CTRL1, port->su_ctrl1);
	write_xhfc(xhfc, A_SU_CTRL2, port->su_ctrl2);

	if (port->mode & PORT_MODE_TE)
		write_xhfc(xhfc, A_SU_CLK_DLY, CLK_DLY_TE);
	else
		write_xhfc(xhfc, A_SU_CLK_DLY, CLK_DLY_NT);

	write_xhfc(xhfc, A_SU_WR_STA, 0);
}

/*
 * Setup Fifo using A_CON_HDLC, A_SUBCH_CFG, A_FIFO_CTRL
 */
static void
setup_fifo(struct xhfc *xhfc, __u8 fifo, __u8 conhdlc, __u8 subcfg,
	   __u8 fifoctrl, __u8 enable)
{
	xhfc_selfifo(xhfc, fifo);
	write_xhfc(xhfc, A_CON_HDLC, conhdlc);
	write_xhfc(xhfc, A_SUBCH_CFG, subcfg);
	write_xhfc(xhfc, A_FIFO_CTRL, fifoctrl);

	if (enable)
		xhfc->fifo_irqmsk |= (1 << fifo);
	else
		xhfc->fifo_irqmsk &= ~(1 << fifo);

	xhfc_resetfifo(xhfc);
	xhfc_selfifo(xhfc, fifo);

	if (debug & DEBUG_HW) {
		printk(KERN_INFO
		       "%s %s: fifo(%i) conhdlc(0x%02x) "
		       "subcfg(0x%02x) fifoctrl(0x%02x)\n",
		       xhfc->name, __FUNCTION__, fifo,
		       sread_xhfc(xhfc, A_CON_HDLC),
		       sread_xhfc(xhfc, A_SUBCH_CFG),
		       sread_xhfc(xhfc, A_FIFO_CTRL)
		    );
	}
}

/*
 * init BChannel prototol
 */
static int
xhfc_setup_bch(struct bchannel *bch, int protocol)
{
	struct port *port = bch->hw;
	struct xhfc *xhfc = port->xhfc;
	__u8 channel = (port->idx * 4) + (bch->nr - 1);

	if (debug & DEBUG_HW)
		printk(KERN_INFO "channel(%i) protocol %x-->%x\n",
		       channel, bch->state, protocol);

	switch (protocol) {
		case (-1):
			bch->state = -1;
			/* fall trough */
		case (ISDN_P_NONE):
			if (bch->state == ISDN_P_NONE)
				return (0);
			bch->state = ISDN_P_NONE;
			setup_fifo(xhfc, (channel << 1), 4, 0, 0, 0);
			setup_fifo(xhfc, (channel << 1) + 1, 4, 0, 0, 0);
			setup_su(xhfc, port->idx, (channel % 4) ? 1 : 0,
				 0);
			clear_bit(FLG_HDLC, &bch->Flags);
			clear_bit(FLG_TRANSPARENT, &bch->Flags);
			break;
		case (ISDN_P_B_RAW):
			setup_fifo(xhfc, (channel << 1), 6, 0, 0, 1);
			setup_fifo(xhfc, (channel << 1) + 1, 6, 0, 0, 1);
			setup_su(xhfc, port->idx, (channel % 4) ? 1 : 0,
				 1);
			bch->state = protocol;
			set_bit(FLG_TRANSPARENT, &bch->Flags);
			break;
		case (ISDN_P_B_HDLC):
			setup_fifo(xhfc, (channel << 1), 4, 0, M_FR_ABO, 1);
			setup_fifo(xhfc, (channel << 1) + 1, 4, 0,
				   M_FR_ABO | M_FIFO_IRQMSK, 1);
			setup_su(xhfc, port->idx, (channel % 4) ? 1 : 0,
				 1);
			bch->state = protocol;
			set_bit(FLG_HDLC, &bch->Flags);
			break;
		default:
			return (-ENOPROTOOPT);
	}
	return (0);
}

/*
 * init DChannel TX/RX fifos
 */
static void
xhfc_setup_dch(struct dchannel *dch)
{
	struct port *port = dch->hw;
	struct xhfc *xhfc = port->xhfc;
	__u8 channel = (port->idx * 4) + 2;
	setup_fifo(xhfc, (channel << 1), 5, 2, M_FR_ABO, 1);
	setup_fifo(xhfc, (channel << 1) + 1, 5, 2,
		   M_FR_ABO | M_FIFO_IRQMSK, 1);
}

/*
 * init EChannel RX fifo
 */
static void
xhfc_setup_ech(struct dchannel *ech)
{
	struct port *port = ech->hw;
	struct xhfc *xhfc = port->xhfc;
	__u8 channel = (port->idx * 4) + 3;
	setup_fifo(xhfc, (channel << 1) + 1, 5, 2,
		   M_FR_ABO | M_FIFO_IRQMSK, 1);
}

static void
xhfc_ph_command(struct port *port, u_char command)
{
	struct xhfc *xhfc = port->xhfc;

	if (debug & DEBUG_HW)
		printk(KERN_INFO "%s %s: %s (%i)\n",
		       __FUNCTION__, port->name, XHFC_PH_COMMANDS[command],
		       command);

	switch (command) {
		case L1_ACTIVATE_TE:
			write_xhfc(xhfc, R_SU_SEL, port->idx);
			write_xhfc(xhfc, A_SU_WR_STA, STA_ACTIVATE);
			break;

		case L1_FORCE_DEACTIVATE_TE:
			write_xhfc(xhfc, R_SU_SEL, port->idx);
			write_xhfc(xhfc, A_SU_WR_STA, STA_DEACTIVATE);
			break;

		case L1_ACTIVATE_NT:
			write_xhfc(xhfc, R_SU_SEL, port->idx);
			write_xhfc(xhfc, A_SU_WR_STA,
				   STA_ACTIVATE | M_SU_SET_G2_G3);
			break;

		case L1_DEACTIVATE_NT:
			write_xhfc(xhfc, R_SU_SEL, port->idx);
			write_xhfc(xhfc, A_SU_WR_STA, STA_DEACTIVATE);
			break;

		case L1_SET_TESTLOOP_B1:
			/* connect B1-SU RX with PCM TX */
			setup_fifo(xhfc, port->idx * 8, 0xC6, 0, 0, 0);
			/* connect B1-SU TX with PCM RX */
			setup_fifo(xhfc, port->idx * 8 + 1, 0xC6, 0, 0, 0);

			/* PCM timeslot B1 TX */
			write_xhfc(xhfc, R_SLOT, port->idx * 8);
			/* enable B1 TX timeslot on STIO1 */
			write_xhfc(xhfc, A_SL_CFG, port->idx * 8 + 0x80);

			/* PCM timeslot B1 RX */
			write_xhfc(xhfc, R_SLOT, port->idx * 8 + 1);
			/* enable B1 RX timeslot on STIO1 */
			write_xhfc(xhfc, A_SL_CFG,
				   port->idx * 8 + 1 + 0xC0);

			setup_su(xhfc, port->idx, 0, 1);
			break;

		case L1_UNSET_TESTLOOP_B1:
			/* disable RX/TX fifos */
			setup_fifo(xhfc, port->idx * 8, 4, 0, 0, 0);
			setup_fifo(xhfc, port->idx * 8 + 1, 4, 0, 0, 0);

			/* disable PCM timeslots */
			write_xhfc(xhfc, R_SLOT, port->idx * 8);
			write_xhfc(xhfc, A_SL_CFG, 0);
			write_xhfc(xhfc, R_SLOT, port->idx * 8 + 1);
			write_xhfc(xhfc, A_SL_CFG, 0);

			setup_su(xhfc, port->idx, 0, 0);
			break;

		case L1_SET_TESTLOOP_B2:
			/* connect B2-SU RX with PCM TX */
			setup_fifo(xhfc, port->idx * 8 + 2, 0xC6, 0, 0, 0);
			/* connect B2-SU TX with PCM RX */
			setup_fifo(xhfc, port->idx * 8 + 3, 0xC6, 0, 0, 0);

			/* PCM timeslot B2 TX */
			write_xhfc(xhfc, R_SLOT, port->idx * 8 + 2);
			/* enable B2 TX timeslot on STIO1 */
			write_xhfc(xhfc, A_SL_CFG,
				   port->idx * 8 + 2 + 0x80);

			/* PCM timeslot B2 RX */
			write_xhfc(xhfc, R_SLOT, port->idx * 8 + 3);
			/* enable B2 RX timeslot on STIO1 */
			write_xhfc(xhfc, A_SL_CFG,
				   port->idx * 8 + 3 + 0xC0);

			setup_su(xhfc, port->idx, 1, 1);
			break;

		case L1_UNSET_TESTLOOP_B2:
			/* disable RX/TX fifos */
			setup_fifo(xhfc, port->idx * 8 + 2, 4, 0, 0, 0);
			setup_fifo(xhfc, port->idx * 8 + 3, 4, 0, 0, 0);

			/* disable PCM timeslots */
			write_xhfc(xhfc, R_SLOT, port->idx * 8 + 2);
			write_xhfc(xhfc, A_SL_CFG, 0);
			write_xhfc(xhfc, R_SLOT, port->idx * 8 + 3);
			write_xhfc(xhfc, A_SL_CFG, 0);

			setup_su(xhfc, port->idx, 1, 0);
			break;

		case L1_SET_TESTLOOP_D:
			/* connect D-SU RX with PCM TX */
			setup_fifo(xhfc, port->idx * 8 + 4, 0xC4, 2, 0, 0);
			/* connect D-SU TX with PCM RX */
			setup_fifo(xhfc, port->idx * 8 + 5, 0xC4, 2, 0, 0);

			/* PCM timeslot D TX */
			write_xhfc(xhfc, R_SLOT, port->idx * 8 + 4);
			/* enable D TX timeslot on STIO1 */
			write_xhfc(xhfc, A_SL_CFG, port->idx * 8 + 4 + 0x80);

			/* PCM timeslot D RX */
			write_xhfc(xhfc, R_SLOT, port->idx * 8 + 5);
			/* enable D RX timeslot on STIO1 */
			write_xhfc(xhfc, A_SL_CFG, port->idx * 8 + 5 + 0xC0);
			break;

		case L1_UNSET_TESTLOOP_D:
			// enable Fifos
			setup_fifo(xhfc, port->idx * 8 + 4, 5, 2, M_FR_ABO, 1);
			setup_fifo(xhfc, port->idx * 8 + 5, 5, 2, M_FR_ABO | M_FIFO_IRQMSK, 1);

			/* disable PCM timeslots */
			write_xhfc(xhfc, R_SLOT, port->idx * 8 + 4);
			write_xhfc(xhfc, A_SL_CFG, 0);
			write_xhfc(xhfc, R_SLOT, port->idx * 8 + 5);
			write_xhfc(xhfc, A_SL_CFG, 0);
			break;
	}
}

/*
 * xhfc reset sequence:
 */
static int
reset_xhfc(struct xhfc *xhfc)
{
	if (debug & DEBUG_HW) {
		printk(KERN_INFO "%s %s", xhfc->name, __FUNCTION__);
	}

	/* software reset to enable R_FIFO_MD setting */
	write_xhfc(xhfc, R_CIRM, M_SRES);
	udelay(5);
	write_xhfc(xhfc, R_CIRM, 0);

	write_xhfc(xhfc, R_FIFO_THRES, 0x11);

	return 0;
}

/**
 * pcm init sequence
 */
static int
init_pcm(struct xhfc *xhfc)
{
	int timeout = 0x2000;

	while ((read_xhfc(xhfc, R_STATUS) & (M_BUSY | M_PCM_INIT)) && (timeout)) {
                timeout--;
        }

        if (!(timeout)) {
                if (debug & DEBUG_HW) {
                        printk(KERN_ERR
                               "%s %s: initialization sequence "
                               "not completed!\n",
                               xhfc->name, __FUNCTION__);
                }
                return (-ENODEV);
        }

        /* set PCM master mode */
        SET_V_PCM_MD(xhfc->pcm_md0, 1);
        write_xhfc(xhfc, R_PCM_MD0, xhfc->pcm_md0);

        /* set pll adjust */
        SET_V_PCM_IDX(xhfc->pcm_md0, 0x09);
        SET_V_PLL_ADJ(xhfc->pcm_md1, 3);
        write_xhfc(xhfc, R_PCM_MD0, xhfc->pcm_md0);
	write_xhfc(xhfc, R_PCM_MD1, xhfc->pcm_md1);

	return 0;
}


/*
 * initialise the XHFC ISDN controller
 * return 0 on success.
 */
static int
init_xhfc(struct xhfc *xhfc)
{
	int err = 0;
	__u8 chip_id;

	chip_id = read_xhfc(xhfc, R_CHIP_ID);
	switch (chip_id) {
		case CHIP_ID_1SU:
			xhfc->num_ports = 1;
			xhfc->channels = 4;
			xhfc->max_z = 0xFF;

			/* timer irq interval 16 ms */
			SET_V_EV_TS(xhfc->ti_wd, 6);

			write_xhfc(xhfc, R_FIFO_MD, 2);
			SET_V_SU0_IRQMSK(xhfc->su_irqmsk, 1);
			sprintf(xhfc->name, "%s_PI%d_%i",
				CHIP_NAME_1SU,
				xhfc->pi->cardnum, xhfc->chipidx);
			break;

		case CHIP_ID_2SU:
			xhfc->num_ports = 2;
			xhfc->channels = 8;
			xhfc->max_z = 0x7F;

			/* timer irq interval 8 ms */
			SET_V_EV_TS(xhfc->ti_wd, 5);

			write_xhfc(xhfc, R_FIFO_MD, 1);
			SET_V_SU0_IRQMSK(xhfc->su_irqmsk, 1);
			SET_V_SU1_IRQMSK(xhfc->su_irqmsk, 1);
			sprintf(xhfc->name, "%s_PI%d_%i",
				CHIP_NAME_2SU,
				xhfc->pi->cardnum, xhfc->chipidx);
			break;

		case CHIP_ID_2S4U:
			xhfc->num_ports = 4;
			xhfc->channels = 16;
			xhfc->max_z = 0x3F;

			/* timer irq interval 4 ms */
			SET_V_EV_TS(xhfc->ti_wd, 4);

			write_xhfc(xhfc, R_FIFO_MD, 0);
			SET_V_SU0_IRQMSK(xhfc->su_irqmsk, 1);
			SET_V_SU1_IRQMSK(xhfc->su_irqmsk, 1);
			SET_V_SU2_IRQMSK(xhfc->su_irqmsk, 1);
			SET_V_SU3_IRQMSK(xhfc->su_irqmsk, 1);
			sprintf(xhfc->name, "%s_PI%d_%i",
				CHIP_NAME_2S4U,
				xhfc->pi->cardnum, xhfc->chipidx);

		case CHIP_ID_4SU:
			xhfc->num_ports = 4;
			xhfc->channels = 16;
			xhfc->max_z = 0x3F;

			/* timer irq interval 4 ms */
			SET_V_EV_TS(xhfc->ti_wd, 4);

			write_xhfc(xhfc, R_FIFO_MD, 0);
			SET_V_SU0_IRQMSK(xhfc->su_irqmsk, 1);
			SET_V_SU1_IRQMSK(xhfc->su_irqmsk, 1);
			SET_V_SU2_IRQMSK(xhfc->su_irqmsk, 1);
			SET_V_SU3_IRQMSK(xhfc->su_irqmsk, 1);
			sprintf(xhfc->name, "%s_PI%d_%i",
				CHIP_NAME_4SU,
				xhfc->pi->cardnum, xhfc->chipidx);
			break;
		default:
			err = -ENODEV;
	}

	if (err) {
		if (debug & DEBUG_HW)
			printk(KERN_ERR "%s %s: unkown Chip ID 0x%x\n",
			       xhfc->name, __FUNCTION__, chip_id);
		return (err);
	} else {
		if (debug & DEBUG_HW)
			printk(KERN_INFO "%s ChipID: 0x%x\n",
			       xhfc->name, chip_id);
	}

	spin_lock_init(&xhfc->lock);
	spin_lock_init(&xhfc->lock_irq);
	reset_xhfc(xhfc);

	/* init pcm */
	init_pcm(xhfc);

	/* perfom short irq test */
	xhfc->testirq = 1;
	enable_interrupts(xhfc);
	mdelay(1 << GET_V_EV_TS(xhfc->ti_wd));
	disable_interrupts(xhfc);

	if (xhfc->irq_cnt > 2) {
		xhfc->testirq = 0;
		return (0);
	} else {
		if (debug & DEBUG_HW)
			printk(KERN_INFO
			       "%s %s: ERROR getting IRQ (irq_cnt %i)\n",
			       xhfc->name, __FUNCTION__, xhfc->irq_cnt);
		return (-EIO);
	}
}

/*
 * init mISDN interface (called for each XHFC)
 */
int
setup_instance(struct xhfc *xhfc, struct device *parent)
{
	struct port *p;
	int err = 0, i, j;

	if (debug)
		printk(KERN_INFO "%s: %s\n", DRIVER_NAME, __func__);

	tasklet_init(&xhfc->tasklet, xhfc_bh_handler,
		     (unsigned long) xhfc);

	err = init_xhfc(xhfc);
	if (err)
		goto out;

	err = -ENOMEM;
	xhfc->port =
	    kzalloc(sizeof(struct port) * xhfc->num_ports, GFP_KERNEL);
	for (i = 0; i < xhfc->num_ports; i++) {
		p = xhfc->port + i;
		p->idx = i;
		p->xhfc = xhfc;
		spin_lock_init(&p->lock);

		/* init D-Channel Interface */
		mISDN_initdchannel(&p->dch, MAX_DFRAME_LEN_L1, ph_state);
		p->dch.dev.D.send = xhfc_l2l1D;
		p->dch.dev.D.ctrl = xhfc_dctrl;
		p->dch.debug = debug & 0xFFFF;
		p->dch.hw = p;
		p->dch.dev.Dprotocols =
		    (1 << ISDN_P_TE_S0) |
		    (1 << ISDN_P_NT_S0) |
		    (1 << ISDN_P_TE_UP0) |
		    (1 << ISDN_P_NT_UP0);

		/* init E-Channel Interface */
		mISDN_initdchannel(&p->ech, MAX_DFRAME_LEN_L1, NULL);
		p->ech.hw = p;
		p->ech.debug = debug & 0xFFFF;

		/* init B-Channel Interfaces */
		p->dch.dev.Bprotocols =
		    (1 << (ISDN_P_B_RAW & ISDN_P_B_MASK)) | (1 <<
							     (ISDN_P_B_HDLC
							      &
							      ISDN_P_B_MASK));
		p->dch.dev.nrbchan = 2;
		for (j = 0; j < 2; j++) {
			p->bch[j].nr = j + 1;
			set_channelmap(j + 1, p->dch.dev.channelmap);
			p->bch[j].debug = debug;
			mISDN_initbchannel(&p->bch[j], MAX_DATA_MEM, 0);
			p->bch[j].hw = p;
			p->bch[j].ch.send = xhfc_l2l1B;
			p->bch[j].ch.ctrl = xhfc_bctrl;
			p->bch[j].ch.nr = j + 1;
			list_add(&p->bch[j].ch.list,
				 &p->dch.dev.bchannels);
		}

		/*
		 * init F7 timer to delay ACTIVATE INDICATION
		 */
		timer_setup(&p->f7_timer, f7_timer_expire, 0);

		snprintf(p->name, MISDN_MAX_IDLEN - 1, "%s.%d",
			 DRIVER_NAME, xhfc_cnt + 1);
		printk(KERN_INFO "%s: registered as '%s'\n", DRIVER_NAME,
		       p->name);

		err = mISDN_register_device(&p->dch.dev, parent, p->name);
		if (err) {
			mISDN_freebchannel(&p->bch[1]);
			mISDN_freebchannel(&p->bch[0]);
			mISDN_freedchannel(&p->dch);
			mISDN_freedchannel(&p->ech);
		} else {
			xhfc_cnt++;
			xhfc_setup_dch(&p->dch);
			enable_interrupts(xhfc);
		}
	}

      out:
	return err;
}

int
release_instance(struct xhfc *hw)
{
	struct port *p;
	int i;

	if (debug)
		printk(KERN_INFO "%s: %s\n", DRIVER_NAME, __func__);

	disable_interrupts(hw);
	tasklet_disable(&hw->tasklet);
	tasklet_kill(&hw->tasklet);

	for (i = 0; i < hw->num_ports; i++) {
		p = hw->port + i;
		/* TODO
		   xhfc_setup_bch(&p->bch[0], ISDN_P_NONE);
		   xhfc_setup_bch(&p->bch[1], ISDN_P_NONE);
		 */
		if (p->mode & PORT_MODE_TE)
			l1_event(p->dch.l1, CLOSE_CHANNEL);

		mISDN_unregister_device(&p->dch.dev);
		mISDN_freebchannel(&p->bch[1]);
		mISDN_freebchannel(&p->bch[0]);
		mISDN_freedchannel(&p->dch);
		mISDN_freedchannel(&p->ech);

		if (timer_pending(&p->f7_timer)) {
			del_timer(&p->f7_timer);
		}
	}

	if (hw) {
		if (hw->port)
			kfree(hw->port);
		kfree(hw);
	}
	return 0;
}

/*
 * Layer 1 callback function
 */
static int
xhfc_l1callback(struct dchannel *dch, u_int cmd)
{
	struct port *p = dch->hw;

	if (debug & DEBUG_HW)
		printk(KERN_INFO "%s: %s cmd 0x%x\n",
		       p->name, __func__, cmd);

	switch (cmd) {
		case INFO3_P8:
		case INFO3_P10:
		case HW_RESET_REQ:
		case HW_POWERUP_REQ:
			break;

		case HW_DEACT_REQ:
			skb_queue_purge(&dch->squeue);
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
			break;
		case PH_ACTIVATE_IND:
			test_and_set_bit(FLG_ACTIVE, &dch->Flags);
			_queue_data(&dch->dev.D, cmd, MISDN_ID_ANY, 0,
				    NULL, GFP_ATOMIC);
			break;
		case PH_DEACTIVATE_IND:
			test_and_clear_bit(FLG_ACTIVE, &dch->Flags);
			_queue_data(&dch->dev.D, cmd, MISDN_ID_ANY, 0,
				    NULL, GFP_ATOMIC);
			break;
		default:
			if (dch->debug & DEBUG_HW)
				printk(KERN_INFO
				       "%s: %s: unknown cmd %x\n", p->name,
				       __func__, cmd);
			return -1;
	}
	return 0;
}

static void
deactivate_bchannel(struct bchannel *bch)
{
	struct port *p = bch->hw;

	if (bch->debug)
		printk(KERN_INFO "%s: %s: bch->nr(%i)\n",
		       p->name, __func__, bch->nr);

	spin_lock_bh(&p->lock);
	mISDN_clear_bchannel(bch);
	spin_unlock_bh(&p->lock);

	spin_lock_bh(&p->xhfc->lock);
	xhfc_setup_bch(bch, ISDN_P_NONE);
	spin_unlock_bh(&p->xhfc->lock);
}

/*
 * Layer2 -> Layer 1 Bchannel data
 */
static int
xhfc_l2l1B(struct mISDNchannel *ch, struct sk_buff *skb)
{
	struct bchannel *bch = container_of(ch, struct bchannel, ch);
	struct port *p = bch->hw;
	__u8 channel = (p->idx * 4) + (bch->nr - 1);
	int ret = -EINVAL;
	struct mISDNhead *hh = mISDN_HEAD_P(skb);

	if (debug & DEBUG_HW)
		printk(KERN_INFO "%s: %s\n", p->name, __func__);

	switch (hh->prim) {
		case PH_DATA_REQ:
			spin_lock_bh(&p->lock);
			ret = bchannel_senddata(bch, skb);
			spin_unlock_bh(&p->lock);

			if (ret > 0) {
				spin_lock_bh(&p->xhfc->lock);
				xhfc_write_fifo(p->xhfc, channel);
				spin_unlock_bh(&p->xhfc->lock);
				ret = 0;
			}
			return ret;
		case PH_ACTIVATE_REQ:
			if (!test_and_set_bit(FLG_ACTIVE, &bch->Flags)) {
				spin_lock_bh(&p->xhfc->lock);
				ret = xhfc_setup_bch(bch, ch->protocol);
				spin_unlock_bh(&p->xhfc->lock);
			} else
				ret = 0;
			if (!ret)
				_queue_data(ch, PH_ACTIVATE_IND,
					    MISDN_ID_ANY, 0, NULL,
					    GFP_KERNEL);
			break;
		case PH_DEACTIVATE_REQ:
			deactivate_bchannel(bch);
			_queue_data(ch, PH_DEACTIVATE_IND, MISDN_ID_ANY,
				    0, NULL, GFP_KERNEL);
			ret = 0;
			break;
	}
	if (!ret)
		dev_kfree_skb(skb);
	return ret;
}

/*
 * Layer2 -> Layer 1 Dchannel data
 */
static int
xhfc_l2l1D(struct mISDNchannel *ch, struct sk_buff *skb)
{
	struct mISDNdevice *dev = container_of(ch, struct mISDNdevice, D);
	struct dchannel *dch = container_of(dev, struct dchannel, dev);
	struct mISDNhead *hh = mISDN_HEAD_P(skb);
	struct port *p = dch->hw;
	int ret = -EINVAL;

	switch (hh->prim) {
		case PH_DATA_REQ:
			if (debug & DEBUG_HW)
				printk(KERN_INFO "%s: %s: PH_DATA_REQ\n",
				       p->name, __func__);

			spin_lock_bh(&p->lock);
			ret = dchannel_senddata(dch, skb);
			spin_unlock_bh(&p->lock);
			if (ret > 0) {
				ret = 0;
				queue_ch_frame(ch, PH_DATA_CNF, hh->id,
					       NULL);
			}
			return ret;

		case PH_ACTIVATE_REQ:
			if (debug & DEBUG_HW)
				printk(KERN_INFO
				       "%s: %s: PH_ACTIVATE_REQ %s\n",
				       p->name, __func__,
				       (p->mode & PORT_MODE_NT) ?
				       "NT" : "TE");

			if (p->mode & PORT_MODE_NT) {
				ret = 0;
				if (test_bit(FLG_ACTIVE, &dch->Flags)) {
					_queue_data(&dch->dev.D,
						    PH_ACTIVATE_IND,
						    MISDN_ID_ANY, 0, NULL,
						    GFP_ATOMIC);
				} else {
					spin_lock_bh(&p->xhfc->lock);
					xhfc_ph_command(p, L1_ACTIVATE_NT);
					spin_unlock_bh(&p->xhfc->lock);
					test_and_set_bit(FLG_L2_ACTIVATED,
							 &dch->Flags);
				}
			} else {
				spin_lock_bh(&p->xhfc->lock);
				xhfc_ph_command(p, L1_ACTIVATE_TE);
				spin_unlock_bh(&p->xhfc->lock);
				ret = l1_event(dch->l1, hh->prim);
			}
			break;

		case PH_DEACTIVATE_REQ:
			if (debug & DEBUG_HW)
				printk(KERN_INFO
				       "%s: %s: PH_DEACTIVATE_REQ\n",
				       p->name, __func__);
			test_and_clear_bit(FLG_L2_ACTIVATED, &dch->Flags);

			if (p->mode & PORT_MODE_NT) {
				spin_lock_bh(&p->xhfc->lock);
				xhfc_ph_command(p, L1_DEACTIVATE_NT);
				spin_unlock_bh(&p->xhfc->lock);

				spin_lock_bh(&p->lock);
				skb_queue_purge(&dch->squeue);
				if (dch->tx_skb) {
					dev_kfree_skb(dch->tx_skb);
					dch->tx_skb = NULL;
				}
				dch->tx_idx = 0;
				if (dch->rx_skb) {
					dev_kfree_skb(dch->rx_skb);
					dch->rx_skb = NULL;
				}
				test_and_clear_bit(FLG_TX_BUSY,
						   &dch->Flags);
				spin_unlock_bh(&p->lock);
				ret = 0;
			} else {
				ret = l1_event(dch->l1, hh->prim);
			}
			break;
	}
	return ret;
}

/*
 * Layer 1 B-channel hardware access
 */
static int
channel_bctrl(struct bchannel *bch, struct mISDN_ctrl_req *cq)
{
	int ret = 0;

	switch (cq->op) {
		case MISDN_CTRL_GETOP:
			cq->op = MISDN_CTRL_FILL_EMPTY;
			break;
		case MISDN_CTRL_FILL_EMPTY:
			test_and_set_bit(FLG_FILLEMPTY, &bch->Flags);
			if (debug & DEBUG_HW_OPEN)
				printk(KERN_INFO
				       "%s: FILL_EMPTY request (nr=%d "
				       "off=%d)\n", __func__, bch->nr,
				       !!cq->p1);
			break;
		default:
			printk(KERN_WARNING "%s: unknown Op %x\n",
			       __func__, cq->op);
			ret = -EINVAL;
			break;
	}
	return ret;
}

/*
 * Layer 1 B-channel hardware access
 */
static int
xhfc_bctrl(struct mISDNchannel *ch, u_int cmd, void *arg)
{
	struct bchannel *bch = container_of(ch, struct bchannel, ch);
	int ret = -EINVAL;

	if (bch->debug & DEBUG_HW)
		printk(KERN_INFO "%s: cmd:%x %p\n", __func__, cmd, arg);

	switch (cmd) {
		case HW_TESTRX_RAW:
		case HW_TESTRX_HDLC:
		case HW_TESTRX_OFF:
			ret = -EINVAL;
			break;

		case CLOSE_CHANNEL:
			test_and_clear_bit(FLG_OPEN, &bch->Flags);
			deactivate_bchannel(bch);
			ch->protocol = ISDN_P_NONE;
			ch->peer = NULL;
			module_put(THIS_MODULE);
			ret = 0;
			break;
		case CONTROL_CHANNEL:
			ret = channel_bctrl(bch, arg);
			break;
		default:
			printk(KERN_WARNING "%s: unknown prim(%x)\n",
			       __func__, cmd);
	}
	return ret;
}

static int
open_dchannel(struct port *p, struct mISDNchannel *ch,
	      struct channel_req *rq)
{
	int err = 0;

	if (debug & DEBUG_HW_OPEN)
		printk(KERN_INFO "%s: %s: dev(%d) open from %p\n",
		       p->name, __func__, p->dch.dev.id,
		       __builtin_return_address(0));
	if (rq->protocol == ISDN_P_NONE)
		return -EINVAL;

	test_and_clear_bit(FLG_ACTIVE, &p->dch.Flags);
	test_and_clear_bit(FLG_ACTIVE, &p->ech.Flags);
	p->dch.state = 0;

	spin_lock_bh(&p->xhfc->lock);

	/* E-Channel logging */
	if (rq->adr.channel == 1) {
		xhfc_setup_ech(&p->ech);
		set_bit(FLG_ACTIVE, &p->ech.Flags);
		_queue_data(&p->ech.dev.D, PH_ACTIVATE_IND,
			     MISDN_ID_ANY, 0, NULL, GFP_ATOMIC);
	}

	if (!p->initdone) {
		if (IS_ISDN_P_S0(rq->protocol))
			p->mode |= PORT_MODE_S0;
		else if (IS_ISDN_P_UP0(rq->protocol))
			p->mode |= PORT_MODE_UP0;

		if (IS_ISDN_P_TE(rq->protocol)) {
			p->mode |= PORT_MODE_TE;
			err = create_l1(&p->dch, xhfc_l1callback);
			if (err) {
				spin_unlock_bh(&p->xhfc->lock);
				return err;
			}
		} else {
			p->mode |= PORT_MODE_NT;
		}
		init_su(p);
		ch->protocol = rq->protocol;
		p->initdone = 1;
	} else {
		if (rq->protocol != ch->protocol) {
			spin_unlock_bh(&p->xhfc->lock);
			return -EPROTONOSUPPORT;
		}
	}

	/* force initial layer1 statechanges */
	p->xhfc->su_irq = p->xhfc->su_irqmsk;

	spin_unlock_bh(&p->xhfc->lock);

	if (((ch->protocol == ISDN_P_NT_S0) && (p->dch.state == 3)) ||
	    ((ch->protocol == ISDN_P_TE_S0) && (p->dch.state == 7)))
		_queue_data(ch, PH_ACTIVATE_IND, MISDN_ID_ANY,
			    0, NULL, GFP_KERNEL);

	rq->ch = ch;
	if (!try_module_get(THIS_MODULE))
		printk(KERN_WARNING "%s: %s: cannot get module\n",
		       p->name, __func__);
	return 0;
}

static int
open_bchannel(struct port *p, struct channel_req *rq)
{
	struct bchannel *bch;

	if (rq->adr.channel > 2)
		return -EINVAL;
	if (rq->protocol == ISDN_P_NONE)
		return -EINVAL;

	if (debug & DEBUG_HW)
		printk(KERN_INFO "%s: %s B%i\n",
		       p->name, __func__, rq->adr.channel);

	bch = &p->bch[rq->adr.channel - 1];
	if (test_and_set_bit(FLG_OPEN, &bch->Flags))
		return -EBUSY;	/* b-channel can be only open once */
	test_and_clear_bit(FLG_FILLEMPTY, &bch->Flags);
	bch->ch.protocol = rq->protocol;
	rq->ch = &bch->ch;

	if (!try_module_get(THIS_MODULE))
		printk(KERN_WARNING "%s: %s:cannot get module\n",
		       p->name, __func__);
	return 0;
}

static int
channel_ctrl(struct port *p, struct mISDN_ctrl_req *cq)
{
	int ret = 0;

	if (debug)
		printk(KERN_INFO "%s: %s op(0x%x) channel(0x%x)\n",
		       p->name, __func__, (cq->op), (cq->channel));

	switch (cq->op) {
		case MISDN_CTRL_GETOP:
			cq->op = MISDN_CTRL_LOOP | MISDN_CTRL_CONNECT |
			    MISDN_CTRL_DISCONNECT;
			break;

		case MISDN_CTRL_LOOP:
			/*
			 * control testloops (line RX -> line TX)
			 *
			 * cq->channel:
			 *   0 disable all
			 *   bit0 : 1: set B1 loop, 0: unset B1 loop
			 *   bit1 : 1: set B2 loop, 0: unset B2 loop
			 *   bit2 : 1: set D  loop, 0: unset D  loop
			 *
			 *   e.g. 3 enables B1 + B2 and disabled D loop
			 */
			if ((cq->channel < 0) || (cq->channel > 7)) {
				ret = -EINVAL;
				break;
			}

			spin_lock_bh(&p->xhfc->lock);
			if (cq->channel & 1) {
				xhfc_ph_command(p, L1_SET_TESTLOOP_B1);
			} else {
				xhfc_ph_command(p, L1_UNSET_TESTLOOP_B1);
			}
			if (cq->channel & 2) {
				xhfc_ph_command(p, L1_SET_TESTLOOP_B2);
			} else {
				xhfc_ph_command(p, L1_UNSET_TESTLOOP_B2);
			}
			if (cq->channel & 4) {
				xhfc_ph_command(p, L1_SET_TESTLOOP_D);
			} else {
				xhfc_ph_command(p, L1_UNSET_TESTLOOP_D);
			}
			spin_unlock_bh(&p->xhfc->lock);
			break;

		default:
			printk(KERN_WARNING "%s: %s: unknown Op %x\n",
			       p->name, __func__, cq->op);
			ret = -EINVAL;
			break;
	}
	return ret;
}

/*
 * device control function
 */
static int
xhfc_dctrl(struct mISDNchannel *ch, u_int cmd, void *arg)
{
	struct mISDNdevice *dev = container_of(ch, struct mISDNdevice, D);
	struct dchannel *dch = container_of(dev, struct dchannel, dev);
	struct port *p = dch->hw;
	struct channel_req *rq;
	int err = 0;

	if (dch->debug & DEBUG_HW)
		printk(KERN_INFO "%s: %s: cmd:%x %p\n",
		       p->name, __func__, cmd, arg);
	switch (cmd) {
		case OPEN_CHANNEL:
			rq = arg;
			if ((IS_ISDN_P_S0(rq->protocol)) ||
			    (IS_ISDN_P_UP0(rq->protocol)))
				err = open_dchannel(p, ch, rq);
			else
				err = open_bchannel(p, rq);
			break;
		case CLOSE_CHANNEL:
			if (debug & DEBUG_HW_OPEN)
				printk(KERN_INFO
				       "%s: %s: dev(%d) close from %p\n",
				       p->name, __func__, p->dch.dev.id,
				       __builtin_return_address(0));
			module_put(THIS_MODULE);
			break;
		case CONTROL_CHANNEL:
			err = channel_ctrl(p, arg);
			break;
		default:
			if (dch->debug & DEBUG_HW)
				printk(KERN_INFO
				       "%s: %s: unknown command %x\n",
				       p->name, __func__, cmd);
			return -EINVAL;
	}
	return err;
}

/*
 * send ACTIVATE INDICATION to l2
 */
static void
f7_timer_expire(struct timer_list *t)
{
	struct port *port = from_timer(port, t, f7_timer);
	l1_event(port->dch.l1, XHFC_L1_F7);
}

/*
 * S0 TE state change event handler
 */
static void
ph_state_te(struct dchannel *dch)
{
	struct port *p = dch->hw;

	if (debug)
		printk(KERN_INFO "%s: %s: TE F%d\n",
		       p->name, __func__, dch->state);

	if ((dch->state != 7) && timer_pending(&p->f7_timer))
		del_timer(&p->f7_timer);

	switch (dch->state) {
		case 0:
			l1_event(dch->l1, XHFC_L1_F0);
			break;
		case 3:
			l1_event(dch->l1, XHFC_L1_F3);
			break;
		case 5:
		case 8:
			l1_event(dch->l1, ANYSIGNAL);
			break;
		case 6:
			l1_event(dch->l1, INFO2);
			break;
		case 7:
			/* delay ACTIVATE INDICATION for 1ms */
			if (!(timer_pending(&p->f7_timer))) {
				p->f7_timer.expires = jiffies + (HZ / 1000);
				add_timer(&p->f7_timer);
			}
			break;
	}
}

/*
 * S0 NT state change event handler
 */
static void
ph_state_nt(struct dchannel *dch)
{
	struct port *p = dch->hw;

	if (debug & DEBUG_HW)
		printk(KERN_INFO "%s: %s: NT G%d\n",
		       p->name, __func__, dch->state);

	switch (dch->state) {
		case (1):
			test_and_clear_bit(FLG_ACTIVE, &dch->Flags);
			test_and_clear_bit(FLG_L2_ACTIVATED, &dch->Flags);
			p->nt_timer = 0;
			p->timers &= ~NT_ACTIVATION_TIMER;
			_queue_data(&dch->dev.D, PH_DEACTIVATE_IND,
				    MISDN_ID_ANY, 0, NULL, GFP_ATOMIC);
			break;
		case (2):
			if (p->nt_timer < 0) {
				p->nt_timer = 0;
				p->timers &= ~NT_ACTIVATION_TIMER;
				write_xhfc(p->xhfc, R_SU_SEL, p->idx);
				write_xhfc(p->xhfc, A_SU_WR_STA, 4 | STA_LOAD);
				udelay(10);
				write_xhfc(p->xhfc, A_SU_WR_STA, 4);
				dch->state = 4;
			} else {
				p->timers |= NT_ACTIVATION_TIMER;
				p->nt_timer = NT_T1_COUNT;
				write_xhfc(p->xhfc, R_SU_SEL, p->idx);
				write_xhfc(p->xhfc, A_SU_WR_STA, M_SU_SET_G2_G3);
			}
			break;
		case (3):
			p->nt_timer = 0;
			p->timers &= ~NT_ACTIVATION_TIMER;
			test_and_set_bit(FLG_ACTIVE, &dch->Flags);
			_queue_data(&dch->dev.D, PH_ACTIVATE_IND,
				    MISDN_ID_ANY, 0, NULL, GFP_ATOMIC);
			break;
		case (4):
			p->nt_timer = 0;
			p->timers &= ~NT_ACTIVATION_TIMER;
			return;
		default:
			break;
	}
}

static void
ph_state(struct dchannel *dch)
{
	struct port *p = dch->hw;
	if (p->mode & PORT_MODE_NT)
		ph_state_nt(dch);
	else if (p->mode & PORT_MODE_TE)
		ph_state_te(dch);
}

/*
 * fill fifo with TX data
 */
static void
xhfc_write_fifo(struct xhfc *xhfc, __u8 channel)
{
	__u8 fcnt, tcnt, i, free, f1, f2, fstat, *data;
	int remain, tx_busy = 0;
	int hdlc = 0;
	int *tx_idx = NULL;
	struct sk_buff **tx_skb = NULL;
	struct port *port = xhfc->port + (channel / 4);
	struct bchannel *bch = NULL;
	struct dchannel *dch = NULL;


	/* protect against layer2 down processes like l2l1D, l2l1B, etc */
	spin_lock(&port->lock);

	switch (channel % 4) {
		case 0:
		case 1:
			bch = &port->bch[channel % 4];
			tx_skb = &bch->tx_skb;
			tx_idx = &bch->tx_idx;
			tx_busy = test_bit(FLG_TX_BUSY, &bch->Flags);
			hdlc = test_bit(FLG_HDLC, &bch->Flags);
			break;
		case 2:
			dch = &port->dch;
			tx_skb = &dch->tx_skb;
			tx_idx = &dch->tx_idx;
			tx_busy = test_bit(FLG_TX_BUSY, &dch->Flags);
			hdlc = 1;
			break;
		case 3:
		default:
			spin_unlock(&port->lock);
			return;
	}
	if (!*tx_skb || !tx_busy) {
		spin_unlock(&port->lock);
		return;
	}

      send_buffer:
	remain = (*tx_skb)->len - *tx_idx;
	if (remain <= 0) {
		spin_unlock(&port->lock);
		return;
	}

	xhfc_selfifo(xhfc, (channel * 2));

	fstat = read_xhfc(xhfc, A_FIFO_STA);
	free = (xhfc->max_z - (read_xhfc(xhfc, A_USAGE)));
	tcnt = (free >= remain) ? remain : free;

	f1 = read_xhfc(xhfc, A_F1);
	f2 = read_xhfc(xhfc, A_F2);

	fcnt = 0x07 - ((f1 - f2) & 0x07); /* free frame count in tx fifo */

	if (debug & DEBUG_HFC_FIFO_STAT) {
		printk(KERN_INFO
		       "%s channel(%i) len(%i) idx(%i) f1(%i) f2(%i) fcnt(%i) "
		       "tcnt(%i) free(%i) fstat(%i)\n",
		       __FUNCTION__, channel, (*tx_skb)->len, *tx_idx,
		       f1, f2, fcnt, tcnt, free, fstat);
	}

	/* check for fifo underrun during frame transmission */
	fstat = read_xhfc(xhfc, A_FIFO_STA);
	if (fstat & M_FIFO_ERR) {
		if (debug & DEBUG_HFC_FIFO_ERR) {
			printk(KERN_INFO
			       "%s transmit fifo channel(%i) underrun idx(%i),"
			       "A_FIFO_STA(0x%02x)\n",
			       __FUNCTION__, channel, *tx_idx, fstat);
		}

		write_xhfc(xhfc, A_INC_RES_FIFO, M_RES_FIFO_ERR);

		/* restart frame transmission */
		if (hdlc && *tx_idx) {
			*tx_idx = 0;
			goto send_buffer;
		}
	}

	if (free && fcnt && tcnt) {
		data = (*tx_skb)->data + *tx_idx;
		*tx_idx += tcnt;

		if (debug & DEBUG_HFC_FIFO_DATA) {
			printk("%s channel(%i) writing: ",
			       xhfc->name, channel);

			i = 0;
			while (i < tcnt)
				printk("%02x ", *(data + (i++)));
			printk("\n");
		}

		/* write data to FIFO */
		i = 0;
		while (i < tcnt) {
                        write_xhfc(xhfc, A_FIFO_DATA, *(data + i));
                        i++;
		        /*
		        // optimized bulk writes
			if ((tcnt - i) >= 4) {
				write32_xhfc(xhfc, A_FIFO_DATA,
					     *((__u32 *) (data + i)));
				i += 4;
			} else {
				write_xhfc(xhfc, A_FIFO_DATA, *(data + i));
				i++;
			}
			*/
		}

		/* skb data complete */
		if (*tx_idx == (*tx_skb)->len) {
			if (hdlc)
				xhfc_inc_f(xhfc);
			else
				xhfc_selfifo(xhfc, (channel * 2));

			/* check for fifo underrun during frame transmission */
			fstat = read_xhfc(xhfc, A_FIFO_STA);
			if (fstat & M_FIFO_ERR) {
				if (debug & DEBUG_HFC_FIFO_ERR) {
					printk(KERN_INFO
					       "%s transmit fifo channel(%i) "
					       "underrun during transmission, "
					       "A_FIFO_STA(0x%02x)\n",
					       __FUNCTION__,
					       channel, fstat);
				}
				write_xhfc(xhfc, A_INC_RES_FIFO,
					   M_RES_FIFO_ERR);

				if (hdlc) {
					// restart frame transmission
					*tx_idx = 0;
					goto send_buffer;
				}
			}

			dev_kfree_skb(*tx_skb);
			*tx_skb = NULL;
			if (bch) {
				get_next_bframe(bch);
			}
			if (dch)
				get_next_dframe(dch);

			if (*tx_skb) {
				if (debug & DEBUG_HFC_FIFO_DATA)
					printk(KERN_INFO
					       "channel(%i) has next_tx_frame\n",
					       channel);
				if ((free - tcnt) > 8) {
					if (debug & DEBUG_HFC_FIFO_DATA)
						printk(KERN_INFO
						       "channel(%i) continue "
						       "B-TX immediatetly\n",
						       channel);
					goto send_buffer;
				}
			}

		} else {
			/* tx buffer not complete, but fifo filled to maximum */
			xhfc_selfifo(xhfc, (channel * 2));
		}
	}
	spin_unlock(&port->lock);
}

/*
 * read RX data out of fifo
 */
static void
xhfc_read_fifo(struct xhfc *xhfc, __u8 channel)
{
	__u8 f1 = 0, f2 = 0, z1 = 0, z2 = 0, pending;
	__u8 fstat = 0;
	int i;
	int rcnt; /* read rcnt bytes out of fifo */
	__u8 *data; /* new data pointer */
	struct sk_buff **rx_skb = NULL;	/* data buffer for upper layer */
	struct port *port = xhfc->port + (channel / 4);
	struct bchannel *bch = NULL;
	struct dchannel *dch = NULL;
	struct dchannel *ech = NULL;
	int maxlen = 0, hdlc = 0;


	/* protect against layer2 down processes like l2l1D, l2l1B, etc */
	spin_lock(&port->lock);

	switch (channel % 4) {
		case 0:
		case 1:
			bch = &port->bch[channel % 4];
			rx_skb = &bch->rx_skb;
			hdlc = test_bit(FLG_HDLC, &bch->Flags);
			maxlen = bch->maxlen;
			break;
		case 2:
			dch = &port->dch;
			rx_skb = &dch->rx_skb;
			hdlc = 1;
			maxlen = dch->maxlen;
			break;
		case 3:
			ech = &port->ech;
			rx_skb = &ech->rx_skb;
			maxlen = ech->maxlen;
			hdlc = 1;
			break;
		default:
			spin_unlock(&port->lock);
			return;
	}

      receive_buffer:
	xhfc_selfifo(xhfc, (channel * 2) + 1);

	fstat = read_xhfc(xhfc, A_FIFO_STA);
	if (fstat & M_FIFO_ERR) {
		if (debug & DEBUG_HFC_FIFO_ERR)
			printk(KERN_INFO
			       "RX fifo overflow channel(%i), "
			       "A_FIFO_STA(0x%02x) f0cnt(%i)\n",
			       channel, fstat, xhfc->f0_akku);
		write_xhfc(xhfc, A_INC_RES_FIFO, M_RES_FIFO_ERR);
	}

	if (hdlc) {
		/* hdlc rcnt */
		f1 = read_xhfc(xhfc, A_F1);
		f2 = read_xhfc(xhfc, A_F2);
		z1 = read_xhfc(xhfc, A_Z1);
		z2 = read_xhfc(xhfc, A_Z2);

		rcnt = (z1 - z2) & xhfc->max_z;
		if (f1 != f2)
			rcnt++;

	} else {
		/* transparent rcnt */
		rcnt = read_xhfc(xhfc, A_USAGE) - 1;
	}

	if (debug & DEBUG_HFC_FIFO_STAT) {
		if (*rx_skb)
			i = (*rx_skb)->len;
		else
			i = 0;
		printk(KERN_INFO "reading %i bytes channel(%i) "
		       "irq_cnt(%i) fstat(%i) idx(%i/%i) f1(%i) f2(%i) "
		       "z1(%i) z2(%i)\n",
		       rcnt, channel, xhfc->irq_cnt, fstat, i, maxlen,
		       f1, f2, z1, z2);
	}

	if (rcnt > 0) {
		if (!(*rx_skb)) {
			*rx_skb = mI_alloc_skb(maxlen, GFP_ATOMIC);
			if (!(*rx_skb)) {
				printk(KERN_INFO "%s: No mem for rx_skb\n",
				       __FUNCTION__);
				spin_unlock(&port->lock);
				return;
			}
		}

		if ((rcnt + (*rx_skb)->len) > maxlen) {
			if (debug & DEBUG_HFC_FIFO_ERR) {
				printk(KERN_INFO
				       "%s: channel(%i) fifo rx data exceeding skb maxlen(%d)\n",
				       __FUNCTION__, channel, maxlen);
			}
			skb_trim(*rx_skb, 0);
			xhfc_resetfifo(xhfc);
			spin_unlock(&port->lock);
			return;
		}
		data = skb_put(*rx_skb, rcnt);

		/* read data from FIFO */
		i = 0;
		while (i < rcnt) {
		        *(data + i) = read_xhfc(xhfc, A_FIFO_DATA);
		        i++;
        		/*
        		// optimized bulk reads:
			if ((rcnt - i) >= 4) {
				*((__u32 *) (data + i)) =
				    read32_xhfc(xhfc, A_FIFO_DATA);
				i += 4;
			} else {
				*(data + i) = read_xhfc(xhfc, A_FIFO_DATA);
				i++;
			}
                        */
		}
	} else {
		spin_unlock(&port->lock);
		return;
	}

	if (hdlc) {
		if (f1 != f2) {
			xhfc_inc_f(xhfc);

			if (debug & DEBUG_HFC_FIFO_DATA) {
				printk(KERN_INFO
				       "channel(%i) new RX len(%i): ",
				       channel, (*rx_skb)->len);
				i = 0;
				printk("  ");
				while (i < (*rx_skb)->len)
					printk("%02x ",
					       (*rx_skb)->data[i++]);
				printk("\n");
			}

			/* check minimum frame size */
			if ((*rx_skb)->len < 4) {
				if (debug & DEBUG_HFC_FIFO_ERR)
					printk(KERN_INFO
					       "%s: frame in channel(%i) "
					       "< minimum size\n",
					       __FUNCTION__, channel);
				goto read_exit;
			}

			/* check crc */
			if ((*rx_skb)->data[(*rx_skb)->len - 1]) {
				if (debug & DEBUG_HFC_FIFO_ERR)
					printk(KERN_INFO
					       "%s: channel(%i) CRC-error\n",
					       __FUNCTION__, channel);
				goto read_exit;
			}

			/* remove cksum */
			skb_trim(*rx_skb, (*rx_skb)->len - 3);

			/* send PH_DATA_IND */
			if (bch)
				recv_Bchannel(bch, MISDN_ID_ANY, false);
			if (dch)
				recv_Dchannel(dch);
			if (ech)
				recv_Echannel(ech, &port->dch);

		      read_exit:
			if (*rx_skb)
				skb_trim(*rx_skb, 0);

			pending = read_xhfc(xhfc, A_USAGE);
			f2++;
			f2 %= 8;
			if (pending > 8 || f1 != f2) {
				if (debug & DEBUG_HFC_FIFO_STAT)
					printk(KERN_DEBUG
					       "%s: channel(%i) continue for %d bytes f2(%d)\n",
					       __func__, channel, pending, f2);
				goto receive_buffer;
			}
			spin_unlock(&port->lock);
			return;


		} else {
			xhfc_selfifo(xhfc, (channel * 2) + 1);
		}
	} else {
		xhfc_selfifo(xhfc, (channel * 2) + 1);
		if (bch && ((*rx_skb)->len >= 128))
			recv_Bchannel(bch, MISDN_ID_ANY, false);
	}
	spin_unlock(&port->lock);
}

/*
 * IRQ work bottom half
 */
static void
xhfc_bh_handler(unsigned long ul_hw)
{
	struct xhfc *xhfc = (struct xhfc *) ul_hw;
	int i;
	__u8 su_state;
	__u8 su_irq, misc_irq;
	__u32 fifo_irq;
	struct dchannel *dch;
	unsigned long flags;

	/* collect XHFC irq sources */
	spin_lock_irqsave(&xhfc->lock_irq, flags);
	su_irq = xhfc->su_irq;
	misc_irq = xhfc->misc_irq;
	fifo_irq = xhfc->fifo_irq;
	xhfc->su_irq = xhfc->misc_irq = xhfc->fifo_irq = 0;
	spin_unlock_irqrestore(&xhfc->lock_irq, flags);

	/* timer interrupt */
	if (GET_V_TI_IRQ(misc_irq)) {
		/* Handle tx Fifos */
		for (i = 0; i < xhfc->channels; i++) {
			if ((1 << (i * 2)) & (xhfc->fifo_irqmsk)) {
				spin_lock(&xhfc->lock);
				xhfc_write_fifo(xhfc, i);
				spin_unlock(&xhfc->lock);
			}
		}

		/* handle NT Timer */
		for (i = 0; i < xhfc->num_ports; i++) {
			if ((xhfc->port[i].mode & PORT_MODE_NT)
			    && (xhfc->port[i].timers & NT_ACTIVATION_TIMER)) {
				if ((--xhfc->port[i].nt_timer) < 0) {
					spin_lock(&xhfc->lock);
					ph_state(&xhfc->port[i].dch);
					spin_unlock(&xhfc->lock);
				}
			}
		}
	}

	/* set fifo_irq when RX data over treshold */
	for (i = 0; i < xhfc->num_ports; i++)
		fifo_irq |= read_xhfc(xhfc, R_FILL_BL0 + i) << (i * 8);

	/* Handle rx Fifos */
	if ((fifo_irq & xhfc->fifo_irqmsk) & FIFO_MASK_RX) {
		for (i = 0; i < xhfc->channels; i++) {
			if ((fifo_irq & (1 << (i * 2 + 1)))
			    & (xhfc->fifo_irqmsk)) {
				spin_lock(&xhfc->lock);
				xhfc_read_fifo(xhfc, i);
				spin_unlock(&xhfc->lock);
			}
		}
	}

	/* su interrupt */
	if (su_irq & xhfc->su_irqmsk) {
		for (i = 0; i < xhfc->num_ports; i++) {
			write_xhfc(xhfc, R_SU_SEL, i);
			su_state = read_xhfc(xhfc, A_SU_RD_STA);
			dch = &xhfc->port[i].dch;
			if (GET_V_SU_STA(su_state) != dch->state) {
				dch->state = GET_V_SU_STA(su_state);
				spin_lock(&xhfc->lock);
				ph_state(dch);
				spin_unlock(&xhfc->lock);
			}
		}
	}
}

/*
 * Interrupt handler
 */
irqreturn_t
xhfc_interrupt(int intno, void *dev_id)
{
	struct xhfc_pi *pi = dev_id;
	struct xhfc *xhfc = NULL;
	__u8 i, j;
	__u32 xhfc_irqs;
	int sched_bh = 0;

#ifdef USE_F0_COUNTER
	__u32 f0_cnt;
#endif

	xhfc_irqs = 0;
	for (i = 0; i < pi->driver_data.num_xhfcs; i++) {
		xhfc = &pi->xhfc[i];
		if (GET_V_GLOB_IRQ_EN(xhfc->irq_ctrl)
		    && (read_xhfc(xhfc, R_IRQ_OVIEW)))
			/* mark this xhfc possibly had irq */
			xhfc_irqs |= (1 << i);
	}
	if (!xhfc_irqs) {
		if (debug & DEBUG_HFC_IRQ)
			printk(KERN_INFO
			       "%s %s NOT M_GLOB_IRQ_EN or R_IRQ_OVIEW \n",
			       xhfc->name, __FUNCTION__);
		return IRQ_NONE;
	}

	xhfc_irqs = 0;
	for (i = 0; i < pi->driver_data.num_xhfcs; i++) {
		xhfc = &pi->xhfc[i];

		spin_lock(&xhfc->lock_irq);

		xhfc->misc_irq |= read_xhfc(xhfc, R_MISC_IRQ);
		xhfc->su_irq |= read_xhfc(xhfc, R_SU_IRQ);

		/* get fifo IRQ states in bundle */
		for (j = 0; j < 4; j++)
			xhfc->fifo_irq |=
			    (read_xhfc(xhfc, R_FIFO_BL0_IRQ + j) <<
			     (j * 8));

		sched_bh = (xhfc->misc_irq & xhfc->misc_irqmsk)
		            || (xhfc->su_irq & xhfc->su_irqmsk)
		            || (xhfc->fifo_irq & xhfc->fifo_irqmsk);

		spin_unlock(&xhfc->lock_irq);

		/* call bottom half at events
		 *   - Timer Interrupt (or other misc_irq sources)
		 *   - SU State change
		 *   - Fifo FrameEnd interrupts (only at rx fifos enabled)
		 */
		if (sched_bh) {

			/* mark this xhfc really had irq */
			xhfc_irqs |= (1 << i);

			/* queue bottom half */
			if (!(xhfc->testirq))
				tasklet_schedule(&xhfc->tasklet);

			/* count irqs */
			xhfc->irq_cnt++;

#ifdef USE_F0_COUNTER
			/* akkumulate f0 counter diffs */
			f0_cnt = read_xhfc(xhfc, R_F0_CNTL);
			f0_cnt += read_xhfc(xhfc, R_F0_CNTH) << 8;
			xhfc->f0_akku += (f0_cnt - xhfc->f0_cnt);
			if ((f0_cnt - xhfc->f0_cnt) < 0)
				xhfc->f0_akku += 0xFFFF;
			xhfc->f0_cnt = f0_cnt;
#endif
		}
	}

	return ((xhfc_irqs) ? IRQ_HANDLED : IRQ_NONE);
}

static int __init
xhfc_init(void)
{
	int err = -ENODEV;

	printk(KERN_INFO DRIVER_NAME " driver Rev. %s debug(0x%x)\n",
	       xhfc_rev, debug);

	err = xhfc_register_pi();
	printk(KERN_INFO DRIVER_NAME ": %d interfaces installed\n",
	       xhfc_cnt);

	return err;
}

static void __exit
xhfc_cleanup(void)
{
	if (debug)
		printk(KERN_INFO DRIVER_NAME ": %s\n", __func__);

	xhfc_unregister_pi();
}

module_init(xhfc_init);
module_exit(xhfc_cleanup);

/* xhfc_pci2pi.h
 * PCI2PI Pci Bridge support for xhfc_su.c
 *
 * (C) 2007 Copyright Cologne Chip AG
 * Authors : Joerg Ciesielski
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
 */

#ifndef _XHFC_PCI2PI_H_
#define _XHFC_PCI2PI_H_

/* differnt PCI modes supported by PCI2PI */
#define PI_INTELNOMX	0
#define PI_INTELMX	1
#define PI_MOT		2
#define PI_MOTMX	3
#define PI_SPI		4
#define PI_EEPPRG	6
#define PI_AUTOEEP	7

/* PI_MODE to GPIO mapping */
#define PI_INTELMX_GPIO		PCI2PI_GPIO3_MODE1	/* ALE pulse switches to MULTIPLEXED */
#define PI_INTELNOMX_GPIO	PCI2PI_GPIO3_MODE1
#define PI_MOT_GPIO		PCI2PI_GPIO2_MODE0
#define PI_MOTMX_GPIO		PCI2PI_GPIO2_MODE0
#define PI_SPI_GPIO		0
#define PI_AUTOEEP_GPIO		PCI2PI_GPIO2_MODE0 | PCI2PI_GPIO3_MODE1


/* PCI2PI GPIO to XHFC signal mapping */
#define PCI2PI_GPIO7_NRST	0x80
#define PCI2PI_GPIO6_TLA3	0x40
#define PCI2PI_GPIO5_TLA2	0x20
#define PCI2PI_GPIO4_TLA1	0x10
#define PCI2PI_GPIO3_MODE1	0x08
#define PCI2PI_GPIO2_MODE0	0x04
#define PCI2PI_GPIO1_BOND1	0x02
#define PCI2PI_GPIO0_BOND0	0x01

/* PCI2PI GPIO XHFC Bond out selection */
#define XHFC_1SU_BOND	0
#define XHFC_2SU_BOND	PCI2PI_GPIO0_BOND0
#define XHFC_2S4U_BOND	PCI2PI_GPIO1_BOND1
#define XHFC_4SU_BOND	PCI2PI_GPIO1_BOND1 | PCI2PI_GPIO0_BOND0

/* membase offset to address XHFC controllers */
#define PCI2PI_MAX_XHFC 2
extern __u32 PCI2PI_XHFC_OFFSETS[PCI2PI_MAX_XHFC];


/*******************************************************/
/*******************************************************/

/* Select processor interface mode and Bond option     */
/* of PCI2PI bridge */
#define PI_MODE		PI_SPI
#define XHFC_BOND	XHFC_4SU_BOND

/*******************************************************/
/*******************************************************/

#if (PI_MODE == PI_INTELNOMX)
#define GPIO_OUT_VAL	XHFC_BOND | PI_INTELNOMX_GPIO

#elif (PI_MODE == PI_INTELMX)
#define GPIO_OUT_VAL	XHFC_BOND | PI_INTELMX_GPIO

#elif (PI_MODE == PI_MOT)
#define GPIO_OUT_VAL	XHFC_BOND | PI_MOT_GPIO

#elif (PI_MODE == PI_MOTMX)
#define GPIO_OUT_VAL	XHFC_BOND | PI_MOTMX_GPIO

#elif (PI_MODE == PI_SPI)
#define GPIO_OUT_VAL	XHFC_BOND | PI_SPI_GPIO

#elif (PI_MODE == PI_AUTOEEP)
#define GPIO_OUT_VAL	XHFC_BOND | PI_AUTOEEP_GPIO
#endif

/*******************************************************/


#define PCI2PI_VENDORID		0x1397

#define PCI2PI_DEVICEID		0xA003
#define MAX_PCI2PI		2

#define RV_PCI2PI_OK		0
#define RV_PCI2PI_ERROR		1

/* PCI2PI register definitions */
#define PCI2PI_OFFSET		0x00000800
#define PCI2PI_DEL_CS		4*0x00 + PCI2PI_OFFSET
#define PCI2PI_DEL_RD		4*0x01 + PCI2PI_OFFSET
#define PCI2PI_DEL_WR		4*0x02 + PCI2PI_OFFSET
#define PCI2PI_DEL_ALE		4*0x03 + PCI2PI_OFFSET
#define PCI2PI_DEL_ADR		4*0x04 + PCI2PI_OFFSET
#define PCI2PI_DEL_DOUT		4*0x05 + PCI2PI_OFFSET
#define PCI2PI_DEFAULT_ADR	4*0x06 + PCI2PI_OFFSET
#define PCI2PI_DEFAULT_DOUT	4*0x07 + PCI2PI_OFFSET


/*
 * PCI2PI_PI_MODE bit 2..0:
 * 000: Intel non multiplexed
 * 001: Intel multiplexed
 * 010: Motorola
 * 100: SPI Motorola
 * 110: EEPROM programming mode throug PCIIF00 Target
 * 111: XHFC AutoEEPROM mode
 */
#define PCI2PI_PI_MODE		4*0x08 + PCI2PI_OFFSET

#define PCI2PI_CYCLE_SHD	4*0x09 + PCI2PI_OFFSET
#define PCI2PI_ALE_ADR_WHSF	4*0x0a + PCI2PI_OFFSET
#define PCI2PI_CYCLE_PAUSE	4*0x0b + PCI2PI_OFFSET
#define PCI2PI_GPIO_OUT		4*0x0c + PCI2PI_OFFSET
#define PCI2PI_G1		4*0x0e + PCI2PI_OFFSET
#define PCI2PI_G0		4*0x0f + PCI2PI_OFFSET

/*
 * bit0: is set by PI_INT active, is reseted by reading this register
 * bit1: is set by PI_WAIT active, is reseted by reading this register
 */
#define PCI2PI_STATUS		4*0x10 + PCI2PI_OFFSET


/* bit0: enable PCI interrupt output */
#define PCI2PI_STATUS_INT_ENABLE	4*0x11 + PCI2PI_OFFSET

/*
 * bit0: 0 = low active interrupt is detected at PI_INT
 * bit0: 1 = high active interrupt is detected at PI_INT
 */
#define PCI2PI_PI_INT_POL	4*0x12 + PCI2PI_OFFSET

/* SPI registers */
/* 32 bit SPI master data output register */
#define PCI2PI_SPI_MO_DATA	4*0x20 + PCI2PI_OFFSET

/* 32 bit SPI master data input register */
#define PCI2PI_SPI_MI_DATA	4*0x21 + PCI2PI_OFFSET

/*
 * bit 0: 0 SPI bits are processing on the serial input/output
 * bit 0: 1 SPI bits are processed, new data can be written or read
 * bit 1..31: unused
 */
#define PCI2PI_SPI_STATUS	4*0x22 + PCI2PI_OFFSET

/*
 * bit 0: spi clock polarity, defines level for SPISEL1
 * bit 1: spi clock phase, defines sampling edge, 0?, 1?
 * bit 2: 0MSB first (default) , 1LSB first
 * bit 3: 1SPI clock permanent during SPISEL1
 */
#define PCI2PI_SPI_CFG0		4*0x28 + PCI2PI_OFFSET

/*
 * bit 0..3: spi clock frequency, SPI clock period  (value+1) x 2 x PCIperiod
 * 0: 2 PCIperiods
 * 1:
 */
#define PCI2PI_SPI_CFG1		4*0x29 + PCI2PI_OFFSET

/* bit 0..3: SPI Device SEL: defines level of D0..D3, XHFC SPI address */
#define PCI2PI_SPI_CFG2		4*0x2A + PCI2PI_OFFSET

/*
 * bit 0: 1spi master out permanent driven
 * bit 1: 1SPISEL remains low between bytes of a sequence
 * bit 2: 1SPISEL remains low permanent
 */
#define PCI2PI_SPI_CFG3		4*0x2B + PCI2PI_OFFSET

/* bit 0..3: default 0100 */
#define PCI2PI_EEP_RECOVER	4*0x30 + PCI2PI_OFFSET

struct PCI2PI_cfg {
	__u32 del_cs;		/* Bit 3..0, bit 3: 0.5 PCI clk,
				 * bits 2..0: gate delay
				 */
	__u32 del_rd;
	__u32 del_wr;
	__u32 del_ale;

	__u32 del_adr;		/* delay between default address
				 * value and selected address
				 */

	__u32 del_dout;		/* delay between default data
				 * value and written data
				 */

	__u32 default_adr;	/* default address value bit 0..7 */

	__u32 default_dout;	/* default data value bit 0..7 */

	__u32 pi_mode;		/* pi_mode bit 2..0:
				 *   000: Intel non multiplexed
				 *   001: Intel multiplexed
				 *   010: Motorola
				 *   100: SPI Motorola
				 *   110: EEPROM programming mode throug
				 *        PCIIF00 Target
				 *   111: XHFC AutoEEPROM mode
				 */

	__u32 setup;		/* address/data setup berfore rd/wr/cs,
				 * 0 or 1 PCI clk
				 */

	__u32 hold;		/* address/data hold after rd/wr/cs,
				 * 0 or 1 PCI clk
				 */

	__u32 cycle;		/* bit 0 only
				 * address/data adds 0 or 1 PCI clk to /rd/wr/cs
				 * access time (cycle+1) PCI clk
				 */

	__u32 ale_adr_first;	/* bit 0 = 0: address valid before ALE=1
				 * bit 0 = 1: ALE=1  before adress valid
				 */

	__u32 ale_adr_setup;	/* bit 0 = 0 setup for ALE/addr = 0s
				 * bit 0 = 1:setup for ALE/addr = 1 PCI clk
				 * ALE/addr depends on ale_adr_first setting
				 */

	__u32 ale_adr_hold;	/* bit 0 = 0 hold for address after ALE: 0s
				 * bit 0 = 1:hold for address after ALE: 1 PCI
				 * clk
				 */

	__u32 ale_adr_wait;	/* bit 0 = 0: 0 PCI clk delay between address
				 *              and data phase
				 * bit 0 = 1: 1 PCI clk delay between address
				 *              and data phase
				 */

	__u32 pause_seq;	/* bit 0..3: number of PCI clocks between
				 * read/write acceses due to DWORD/WORD
				 * burst access
				 */

	__u32 pause_end;	/* bit 0..3: number of PCI clocks after
				 * DWORD/WORD burst access /delays
				 * PCI_TRDY signal
				 */

	__u32 gpio_out;		/* bit 0..7: GPIO output value */

	__u32 status_int_enable;	/* bit 0: enables PCI interrupt for
					 *        PI_INT signal
					 * bit 1: enables PCI interrupt for
					 *        PI_NWAIT signal
					 */

	__u32 pi_int_pol;	/* bit 0: polarity of PI_INT signal */

	__u32 pi_wait_enable;	/* access length can be controled by /wait
				 * signal:  0 wait disabled, 1 wait enabled
				 */
	__u32 spi_cfg0;
	__u32 spi_cfg1;
	__u32 spi_cfg2;
	__u32 spi_cfg3;
	__u32 eep_recover;
};

/* private driver_data */
struct pi_params {
	char *device_name;
	__u8 num_xhfcs;
};

/* PCI processor interface */
struct xhfc_pi {
	struct pci_dev *pdev;
	int irq;
	int iobase;
	u_char *membase;
	u_char *hw_membase;
	int cardnum;
	char name[10]; /* 'XHFC_PI0' = ProcessorInterface no. 0 */
	struct pi_params driver_data;
	spinlock_t lock;
	__u8 num_xhfcs;
	struct list_head list;

	/* each PI may contain several XHFCs */
	struct xhfc * xhfc;
};


/* prototypes: PCI bridge management */
int init_pci_bridge(struct xhfc_pi * pi);
int xhfc_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
void xhfc_pci_remove(struct pci_dev *pdev);
int xhfc_register_pi(void);
int xhfc_unregister_pi(void);

/* prototypes: xhfc register access */
__u8 read_xhfc(struct xhfc *, __u8 reg_addr);
__u32 read32_xhfc(struct xhfc *, __u8 reg_addr);
void write_xhfc(struct xhfc *, __u8 reg_addr, __u8 value);
void write32_xhfc(struct xhfc *, __u8 reg_addr, __u32 value);
__u8 sread_xhfc(struct xhfc *, __u8 reg_addr);
void write_xhfcregptr(struct xhfc *, __u8 reg_addr);
__u8 read_xhfcregptr(struct xhfc *);

#endif /* _XHFC_PCI2PI_H_ */

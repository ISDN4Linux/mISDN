/* $Id: xhfc_pci2pi.h,v 1.3 2006/03/06 16:20:33 mbachem Exp $
 *
 * PCI2PI Pci Bridge support for xhfc_su.c
 *
 * Authors : Martin Bachem, Joerg Ciesielski
 * Contact : info@colognechip.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; eit_her version 2, or (at your option)
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

#include "xhfc24succ.h"

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
#define PI_MODE		PI_INTELMX
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
PCI2PI_PI_MODE bit 2..0:
000: Intel non multiplexed
001: Intel multiplexed
010: Motorola
100: SPI Motorola
110: EEPROM programming mode throug PCIIF00 Target
111: XHFC AutoEEPROM mode
*/
#define PCI2PI_PI_MODE		4*0x08 + PCI2PI_OFFSET

#define PCI2PI_CYCLE_SHD	4*0x09 + PCI2PI_OFFSET
#define PCI2PI_ALE_ADR_WHSF	4*0x0a + PCI2PI_OFFSET
#define PCI2PI_CYCLE_PAUSE	4*0x0b + PCI2PI_OFFSET
#define PCI2PI_GPIO_OUT		4*0x0c + PCI2PI_OFFSET
#define PCI2PI_G1		4*0x0e + PCI2PI_OFFSET
#define PCI2PI_G0		4*0x0f + PCI2PI_OFFSET

/*
bit0: is set by PI_INT active, is reseted by reading this register
bit1: is set by PI_WAIT active, is reseted by reading this register
*/
#define PCI2PI_STATUS		4*0x10 + PCI2PI_OFFSET


/* bit0: enable PCI interrupt output */
#define PCI2PI_STATUS_INT_ENABLE	4*0x11 + PCI2PI_OFFSET

/*
bit0: 0 = low active interrupt is detected at PI_INT
bit0: 1 = high active interrupt is detected at PI_INT
*/
#define PCI2PI_PI_INT_POL	4*0x12 + PCI2PI_OFFSET

/* SPI registers */
/* 32 bit SPI master data output register */
#define PCI2PI_SPI_MO_DATA	4*0x20 + PCI2PI_OFFSET

/* 32 bit SPI master data input register */
#define PCI2PI_SPI_MI_DATA	4*0x21 + PCI2PI_OFFSET

/*
bit 0: 0 SPI bits are processing on the serial input/output
bit 0: 1 SPI bits are processed, new data can be written or read
bit 1..31: unused
*/
#define PCI2PI_SPI_STATUS	4*0x22 + PCI2PI_OFFSET

/*
bit 0: spi clock polarity, defines level for SPISEL1
bit 1: spi clock phase, defines sampling edge, 0?, 1?
bit 2: 0MSB first (default) , 1LSB first
bit 3: 1SPI clock permanent during SPISEL1
*/
#define PCI2PI_SPI_CFG0		4*0x28 + PCI2PI_OFFSET

/*
bit 0..3: spi clock frequency, SPI clock period  (value+1) x 2 x PCIperiod
0: 2 PCIperiods
1:
*/
#define PCI2PI_SPI_CFG1		4*0x29 + PCI2PI_OFFSET

/* bit 0..3: SPI Device SEL: defines level of D0..D3, XHFC SPI address */
#define PCI2PI_SPI_CFG2		4*0x2A + PCI2PI_OFFSET

/*
bit 0: 1spi master out permanent driven
bit 1: 1SPISEL remains low between bytes of a sequence
bit 2: 1SPISEL remains low permanent
*/
#define PCI2PI_SPI_CFG3		4*0x2B + PCI2PI_OFFSET

/* bit 0..3: default 0100 */
#define PCI2PI_EEP_RECOVER	4*0x30 + PCI2PI_OFFSET

typedef struct _PCI2PI_cfg {
	__u32 del_cs;		/* Bit 3..0, bit 3: 0.5 PCI clk,
				   bits 2..0: gate delay */
	__u32 del_rd;
	__u32 del_wr;
	__u32 del_ale;

	__u32 del_adr;		/* delay between default address
				   value and selected address */
	__u32 del_dout;		/* delay between default data
				   value and written data */
	__u32 default_adr;	/* default address value bit 0..7 */

	__u32 default_dout;	/* default data value bit 0..7 */


	__u32 pi_mode;		/* pi_mode bit 2..0:
				   000: Intel non multiplexed
				   001: Intel multiplexed
				   010: Motorola
				   100: SPI Motorola
				   110: EEPROM programming mode throug PCIIF00 Target
				   111: XHFC AutoEEPROM mode
				 */

	__u32 setup;		/* address/data setup berfore rd/wr/cs,
				   0 or 1 PCI clk */

	__u32 hold;		/* address/data hold after rd/wr/cs,
				   0 or 1 PCI clk */

	__u32 cycle;		/* bit 0 only
				   address/data adds 0 or 1 PCI clk to /rd/wr/cs
				   access time (cycle+1) PCI clk
				 */

	__u32 ale_adr_first;	/* bit 0 = 0: address valid before ALE=1
				   bit 0 = 1: ALE=1  before adress valid */

	__u32 ale_adr_setup;	/* bit 0 = 0 setup for ALE/addr = 0s
				   bit 0 = 1:setup for ALE/addr = 1 PCI clk
				   ALE/addr depends on ale_adr_first setting */

	__u32 ale_adr_hold;	/* bit 0 = 0 hold for address after ALE: 0s
				   bit 0 = 1:hold for address after ALE: 1 PCI clk */

	__u32 ale_adr_wait;	/* bit 0 = 0: 0 PCI clk delay between address and data phase
				   bit 0 = 1: 1 PCI clk delay between address and data phase */

	__u32 pause_seq;	/* bit 0..3: number of PCI clocks between read/write
				   acceses due to DWORD/WORD burst access */

	__u32 pause_end;	/* bit 0..3: number of PCI clocks after DWORD/WORD
				   burst access /delays PCI_TRDY signal */

	__u32 gpio_out;		/* bit 0..7: GPIO output value */

	__u32 status_int_enable;	/* bit 0: enables PCI interrupt for PI_INT signal
					   bit 1: enables PCI interrupt for PI_NWAIT signal */

	__u32 pi_int_pol;	/* bit 0: polarity of PI_INT signal */

	__u32 pi_wait_enable;	/* access length can be controled by /wait  signal
				   0 wait disabled, 1 wait enabled */

	__u32 spi_cfg0;
	__u32 spi_cfg1;
	__u32 spi_cfg2;
	__u32 spi_cfg3;
	__u32 eep_recover;

} PCI2PI_cfg;


/*

read and write functions to access registers of the PCI bridge

*/

static inline __u8
ReadPCI2PI_u8(xhfc_pi * pi, __u16 reg_addr)
{
	return (*(volatile __u8 *) (pi->membase + reg_addr));
}

static inline __u16
ReadPCI2PI_u16(xhfc_pi * pi, __u16 reg_addr)
{
	return (*(volatile __u16 *) (pi->membase + reg_addr));
}

static inline __u32
ReadPCI2PI_u32(xhfc_pi * pi, __u16 reg_addr)
{
	return (*(volatile __u32 *) (pi->membase + reg_addr));
}

static inline void
WritePCI2PI_u8(xhfc_pi * pi, __u16 reg_addr, __u8 value)
{
	*((volatile __u8 *) (pi->membase + reg_addr)) = value;
}

static inline void
WritePCI2PI_u16(xhfc_pi * pi, __u16 reg_addr, __u16 value)
{
	*((volatile __u16 *) (pi->membase + reg_addr)) = value;
}

static inline void
WritePCI2PI_u32(xhfc_pi * pi, __u16 reg_addr, __u32 value)
{
	*((volatile __u32 *) (pi->membase + reg_addr)) = value;
}


/*

read and write functions to access a XHFC at the local bus interface of the PCI 
bridge

there are two sets of functions to access the XHFC in the following different 
interface modes:

multiplexed bus interface modes PI_INTELMX and PI_MOTMX
- these modes use a single (atomic) PCI cycle to read or write a XHFC register

non multiplexed bus interface modes PI_INTELNOMX, PI_MOTMX and PI_SPI

- these modes use a separate PCI cycles to select the XHFC register and to read 
or write data. That means these register accesses are non atomic and could be 
interrupted by an interrupt. The driver must take care that a register access in 
these modes is not interrupted by its own interrupt handler.


*/


/*****************************************************************************/


#if ((PI_MODE==PI_INTELMX) || (PI_MODE==PI_MOTMX))


/*
functions for multiplexed access
*/
static inline __u8
read_xhfc(xhfc_t * xhfc, __u8 reg_addr)
{
	return (*(volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + (reg_addr << 2)));
}


/*
read four bytes from the same register address
e.g. A_FIFO_DATA
this function is only defined for software compatibility here
*/
static inline __u32
read32_xhfc(xhfc_t * xhfc, __u8 reg_addr)
{
	__u32 value;

	value =  (*(volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + (reg_addr << 2)));
	value |= (*(volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + (reg_addr << 2))) << 8;
	value |= (*(volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + (reg_addr << 2))) << 16;
	value |= (*(volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + (reg_addr << 2))) << 24;

	return (value);
}

static inline void
write_xhfc(xhfc_t * xhfc, __u8 reg_addr, __u8 value)
{
	*((volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + (reg_addr << 2))) = value;
}

/*
writes four bytes to the same register address
e.g. A_FIFO_DATA
this function is only defined for software compatibility here
*/
static inline void
write32_xhfc(xhfc_t * xhfc, __u8 reg_addr, __u32 value)
{
	*((volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + (reg_addr << 2))) = value & 0xff;
	*((volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + (reg_addr << 2))) = (value >>8) & 0xff;
	*((volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + (reg_addr << 2))) = (value >>16) & 0xff;
	*((volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + (reg_addr << 2))) = (value >>24) & 0xff;
}


/* always reads a single byte with short read method
this allows to read ram based registers
that normally requires long read access times
*/
static inline __u8
sread_xhfc(xhfc_t * xhfc, __u8 reg_addr)
{
	(*(volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + (reg_addr << 2)));
	return (*(volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + (R_INT_DATA << 2)));
}

/* this function reads the currently selected regsiter from XHFC and is only 
required for non multiplexed access modes. For multiplexed access modes this 
function is only defined for for software compatibility. */

static inline __u8
read_xhfcregptr(xhfc_t * xhfc)
{
	return 0;
}

/* this function writes the XHFC register address pointer and is only 
required for non multiplexed access modes. For multiplexed access modes this 
function is only defined for for software compatibility. */

static inline void
write_xhfcregptr(xhfc_t * xhfc, __u8 reg_addr)
{
}


#endif /* PI_MODE==PI_INTELMX || PI_MODE==PI_MOTMX */



/*****************************************************************************/


#if PI_MODE==PI_INTELNOMX || PI_MODE==PI_MOT
/*
functions for non multiplexed access

XHFC register address pointer is accessed with PCI address A2=1 and XHFC data 
port is accessed with PCI address A2=0

*/

static inline __u8
read_xhfc(xhfc_t * xhfc, __u8 reg_addr)
{
	*((volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + 4)) = reg_addr;
	return (*(volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx]));
}


/*

read four bytes from the same register address by using a 32bit PCI access. The 
PCI bridge generates for 8 bit data read cycles at the local bus interface.

*/

static inline __u32
read32_xhfc(xhfc_t * xhfc, __u8 reg_addr)
{
	*((volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + 4)) = reg_addr;
	return (*(volatile __u32 *) xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx]);
}

static inline void
write_xhfc(xhfc_t * xhfc, __u8 reg_addr, __u8 value)
{
	*((volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + 4)) = reg_addr;
	*((volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx])) = value;
}

/*

writes four bytes to the same register address (e.g. A_FIFO_DATA) by using a 
32bit PCI access. The PCI bridge generates for 8 bit data write cycles at the 
local bus interface.

*/
static inline void
write32_xhfc(xhfc_t * xhfc, __u8 reg_addr, __u32 value)
{
	*((volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + 4)) = reg_addr;
	*((volatile __u32 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx])) = value;
}


/*

reads a single byte with short read method (r*). This allows to read ram based 
registers that normally requires long read access times

*/
static inline __u8
sread_xhfc(xhfc_t * xhfc, __u8 reg_addr)
{
	
	*((volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + 4)) = reg_addr;
	(*(volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx] ));
	*((volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + 4)) = R_INT_DATA;
	return (*(volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx]));
}

/*

this function reads the currently selected regsiter from XHFC

*/
static inline __u8
read_xhfcregptr(xhfc_t * xhfc)
{
	return (*(volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + 4));
}

/* this function writes the XHFC register address pointer */

static inline void
write_xhfcregptr(xhfc_t * xhfc, __u8 reg_addr)
{
    *((volatile __u8 *) (xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + 4)) = reg_addr;
}



#endif /* PI_MODE==PI_INTELNOMX || PI_MODE==PI_MOT */


/*****************************************************************************/

#if PI_MODE == PI_SPI

// SPI mode transaction bit definitions 
#define SPI_ADDR	0x40
#define SPI_DATA	0x00
#define SPI_RD		0x80
#define SPI_WR		0x00
#define SPI_BROAD	0x20
#define SPI_MULTI	0x20


/*
functions for SPI access

*/


static inline __u8
read_xhfc(xhfc_t * xhfc, __u8 reg_addr)
{
	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(hw, PCI2PI_SPI_STATUS) & 1));
	// initiate a 32 clock SPI master transfer
	WritePCI2PI_u32(hw, PCI2PI_SPI_MO_DATA, ((SPI_ADDR | SPI_WR | xhfc->chipidx) << 24) | (reg_addr << 16) | ((SPI_DATA | SPI_RD) << 8));
	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(hw, PCI2PI_SPI_STATUS) & 1));
	// read data from the SPI data receive register and return one byte
	return (__u8) (ReadPCI2PI_u32(hw, PCI2PI_SPI_MI_DATA) & 0xFF);
}


/*

read four bytes from the same register address by using a SPI multiple read access

*/

static inline __u32
read32_xhfc(xhfc_t * xhfc, __u8 reg_addr)
{
	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// initiate a 16 clock SPI master transfer
	WritePCI2PI_u16(xhfc->pi, PCI2PI_SPI_MO_DATA, ((SPI_ADDR | SPI_WR | xhfc->chipidx) << 8) | reg_addr);
	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// initiate a 8 clock SPI master transfer
	WritePCI2PI_u8(xhfc->pi, PCI2PI_SPI_MO_DATA, (SPI_DATA | SPI_RD | SPI_MULTI));
	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// initiate a 32 clock SPI master transfer
	// output data is arbitrary
	WritePCI2PI_u32(xhfc->pi, PCI2PI_SPI_MO_DATA, 0);
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// read data from the SPI data receive register and return four bytes
	return (__u32) be32_to_cpu(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_MI_DATA));
}

static inline void
write_xhfc(xhfc_t * xhfc, __u8 reg_addr, __u8 value)
{
	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// initiate a 32 clock SPI master transfer
	WritePCI2PI_u32(xhfc->pi, PCI2PI_SPI_MO_DATA, ((SPI_ADDR | SPI_WR | xhfc->chipidx) << 24) | (reg_addr << 16) | ((SPI_DATA | SPI_WR) << 8) | value);
}

/*

writes four bytes to the same register address (e.g. A_FIFO_DATA) by using a SPI 
multiple write access

*/
static inline void
write32_xhfc(xhfc_t * xhfc, __u8 reg_addr, __u32 value)
{
	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// initiate a 16 clock SPI master transfer
	WritePCI2PI_u16(xhfc->pi, PCI2PI_SPI_MO_DATA, ((SPI_ADDR | SPI_WR | xhfc->chipidx) << 8) | reg_addr);
	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// initiate a 8 clock SPI master transfer
	WritePCI2PI_u8(xhfc->pi, PCI2PI_SPI_MO_DATA, (SPI_DATA | SPI_WR | SPI_MULTI));
	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// initiate a 32 clock SPI master transfer
	WritePCI2PI_u32(xhfc->pi, PCI2PI_SPI_MO_DATA, cpu_to_be32(value));
}


/*

reads a single byte with short read method (r*). This allows to read ram based 
registers that normally requires long read access times

*/
static inline __u8
sread_xhfc(xhfc_t * xhfc, __u8 reg_addr)
{
        // wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// initiate a 32 clock SPI master transfer
	WritePCI2PI_u32(xhfc->pi, PCI2PI_SPI_MO_DATA ,((SPI_ADDR | SPI_WR | xhfc->chipidx) << 24) | (reg_addr << 16) | ((SPI_DATA | SPI_RD) << 8));
	
	
	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// initiate a 32 clock SPI master transfer to read R_INT_DATA register
	WritePCI2PI_u32(xhfc->pi, PCI2PI_SPI_MO_DATA, ((SPI_ADDR | SPI_WR | xhfc->chipidx) << 24) | (R_INT_DATA << 16) | ((SPI_DATA | SPI_RD) << 8));
	
	
	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// read data from the SPI data receive register and return one byte
	return (__u8) (ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_MI_DATA) & 0xFF);
}

/*

this function reads the currently selected regsiter from XHFC

*/
static inline __u8
read_xhfcregptr(xhfc_t * xhfc)
{
	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// initiate a 16 clock SPI master transfer
	WritePCI2PI_u16(xhfc->pi, PCI2PI_SPI_MO_DATA, ((SPI_ADDR | SPI_RD | xhfc->chipidx) << 8));
	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// read data from the SPI data receive register and return one byte
	return (__u8) (ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_MI_DATA) & 0xFF);
}

/* this function writes the XHFC register address pointer */

static inline void
write_xhfcregptr(xhfc_t * xhfc, __u8 reg_addr)
{
    	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// initiate a 16 clock SPI master transfer
	WritePCI2PI_u16(xhfc->pi, PCI2PI_SPI_MO_DATA, ((SPI_ADDR | SPI_WR | xhfc->chipidx) << 8) | reg_addr);
}

#endif	/* PI_MODE == PI_SPI */



/* Function Prototypes */
int init_pci_bridge(xhfc_pi * pi);

#endif	/* _XHFC_PCI2PI_H_ */

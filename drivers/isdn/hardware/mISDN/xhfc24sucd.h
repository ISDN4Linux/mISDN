/* xhfc24sucd.h
 * XHFC-2S4U / XHFC-4SU register definitions
 * (C) 2007, 2008 Copyright Cologne Chip AG
 * E-Mail: support@colognechip.com
 *
 * Dual-license
 * ------------
 * Cologne Chip AG, Eintrachtstr. 113, 50668 Koeln, Germany, provides this
 * header file (software) under a dual-license.
 * The licensee can choose from the following two licensing models:
 *   * For GPL (free) distributions, see the 'License - GPL'
 *   * For commercial distributions, see the 'License - Commercial'
 *
 *
 * License - GPL
 * -------------
 * This software is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2, or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details
 *
 * You should have received a copy of the GNU General Public License along with
 * this software; if not, write to the Free Software Foundation, Inc., 675 Mass
 * Ave, Cambridge, MA 02139, USA.
 *
 *
 * License - Commercial
 * --------------------
 * (C) 2007, 2008 Copyright Cologne Chip AG
 * All rights reserved.
 * Contact: support@CologneChip.com
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *     * Redistributions of source code must mark all modifications explicitly
 *       as such, if any are made.
 *     * For redistributing and use of this software in binary form, none of the
 *       provisions above applies.
 *
 * This software is provided by Cologne Chip AG "as is" and any express or
 * implied warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose are disclaimed. In no
 * event shall Cologne Chip AG be liable for any direct, indirect, incidental,
 * special, exemplary, or consequential damages (including, but not limited to,
 * procurement of substitute goods or services; loss of use, data, or profits;
 * or business interruption) however caused and on any theory of liability,
 * whether in contract, strict liability, or tort (including negligence or
 * otherwise) arising in any way out of the use of this software, even if
 * advised of the possibility of such damage.
 *
 * <END OF Dual-license>
 *
 * __________________________________________________________________________________
 *
 *   File name:     xhfc24sucd.h
 *   File content:  This file contains the XHFC-2S4U / XHFC-4SU register definitions.
 *   Creation date: 18.06.2007 15:38
 *   Creator:       Genero 3.6
 *   Data base:     HFC XML 1.9 for XHFC-1SU, XHFC-2SU, XHFC-2S4U and XHFC-4SU
 *   Address range: 0x00 - 0xFF
 * 
 *   The information presented can not be considered as assured characteristics.
 *   Data can change without notice. Please check version numbers in case of doubt.
 * 
 *   For further information or questions please contact support@CologneChip.com
 * __________________________________________________________________________________
 * 
 *   WARNING: This file has been generated automatically and should not be
 *            changed to maintain compatibility with later versions.
 * __________________________________________________________________________________
 */

#ifndef _XHFC24SUCD_H_
#define _XHFC24SUCD_H_


/*
 *  Common chip information:
 */

	#define CHIP_NAME_2S4U		"XHFC-2S4U"
	#define CHIP_NAME_4SU		"XHFC-4SU"
	#define CHIP_TITLE_2S4U		"ISDN HDLC FIFO controller 2 S/T interfaces combined with 4 Up interfaces (Universal ISDN Ports)"
	#define CHIP_TITLE_4SU		"ISDN HDLC FIFO controller with 4 S/T interfaces combined with 4 Up interfaces (Universal ISDN Ports)"
	#define CHIP_MANUFACTURER	"Cologne Chip"
	#define CHIP_ID_2S4U		0x62
	#define CHIP_ID_4SU		0x63
	#define CHIP_REGISTER_COUNT	122
	#define CHIP_DATABASE		"Version HFC-XMLHFC XML 1.9 for XHFC-1SU, XHFC-2SU, XHFC-2S4U and XHFC-4SU - GeneroGenero 3.6 "

// This register file can also be used for XHFC-2SU and XHFC-1SU programming.
// For this reason these chip names, IDs and titles are defined here as well:

	#define CHIP_NAME_2SU		"XHFC-2SU"
	#define CHIP_TITLE_2SU		"ISDN HDLC FIFO controller with 2 combined S/T and Up Interfaces"
	#define CHIP_ID_2SU		0x61

	#define CHIP_NAME_1SU		"XHFC-1SU"
	#define CHIP_TITLE_1SU		"ISDN HDLC FIFO controller with a combined S/T and Up Interface"
	#define CHIP_ID_1SU		0x60

/*
 *  Begin of XHFC-2S4U / XHFC-4SU register definitions.
 */

#define R_CIRM 0x00 // register address, write only
	#define M_CLK_OFF  0x01  // mask bit 0
	#define SET_V_CLK_OFF(R,V)  (R = (__u8)((R & (__u8)(M_CLK_OFF ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_CLK_OFF(R)    (__u8)(R & M_CLK_OFF)

	#define M_WAIT_PROC  0x02  // mask bit 1
	#define SET_V_WAIT_PROC(R,V)  (R = (__u8)((R & (__u8)(M_WAIT_PROC ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_WAIT_PROC(R)    (__u8)((R & M_WAIT_PROC) >> 1)

	#define M_WAIT_REG  0x04  // mask bit 2
	#define SET_V_WAIT_REG(R,V)  (R = (__u8)((R & (__u8)(M_WAIT_REG ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_WAIT_REG(R)    (__u8)((R & M_WAIT_REG) >> 2)

	#define M_SRES  0x08  // mask bit 3
	#define SET_V_SRES(R,V)  (R = (__u8)((R & (__u8)(M_SRES ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_SRES(R)    (__u8)((R & M_SRES) >> 3)

	#define M_HFC_RES  0x10  // mask bit 4
	#define SET_V_HFC_RES(R,V)  (R = (__u8)((R & (__u8)(M_HFC_RES ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_HFC_RES(R)    (__u8)((R & M_HFC_RES) >> 4)

	#define M_PCM_RES  0x20  // mask bit 5
	#define SET_V_PCM_RES(R,V)  (R = (__u8)((R & (__u8)(M_PCM_RES ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_PCM_RES(R)    (__u8)((R & M_PCM_RES) >> 5)

	#define M_SU_RES  0x40  // mask bit 6
	#define SET_V_SU_RES(R,V)  (R = (__u8)((R & (__u8)(M_SU_RES ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_SU_RES(R)    (__u8)((R & M_SU_RES) >> 6)


#define R_CTRL 0x01 // register address, write only
	#define M_FIFO_LPRIO  0x02  // mask bit 1
	#define SET_V_FIFO_LPRIO(R,V)  (R = (__u8)((R & (__u8)(M_FIFO_LPRIO ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_FIFO_LPRIO(R)    (__u8)((R & M_FIFO_LPRIO) >> 1)

	#define M_NT_SYNC  0x08  // mask bit 3
	#define SET_V_NT_SYNC(R,V)  (R = (__u8)((R & (__u8)(M_NT_SYNC ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_NT_SYNC(R)    (__u8)((R & M_NT_SYNC) >> 3)

	#define M_OSC_OFF  0x20  // mask bit 5
	#define SET_V_OSC_OFF(R,V)  (R = (__u8)((R & (__u8)(M_OSC_OFF ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_OSC_OFF(R)    (__u8)((R & M_OSC_OFF) >> 5)

	#define M_SU_CLK  0xC0  // mask bits 6..7
	#define SET_V_SU_CLK(R,V)  (R = (__u8)((R & (__u8)(M_SU_CLK ^ 0xFF)) | (__u8)((V & 0x03) << 6)))
	#define GET_V_SU_CLK(R)    (__u8)((R & M_SU_CLK) >> 6)


#define R_CLK_CFG 0x02 // register address, write only
	#define M_CLK_PLL  0x01  // mask bit 0
	#define SET_V_CLK_PLL(R,V)  (R = (__u8)((R & (__u8)(M_CLK_PLL ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_CLK_PLL(R)    (__u8)(R & M_CLK_PLL)

	#define M_CLKO_HI  0x02  // mask bit 1
	#define SET_V_CLKO_HI(R,V)  (R = (__u8)((R & (__u8)(M_CLKO_HI ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_CLKO_HI(R)    (__u8)((R & M_CLKO_HI) >> 1)

	#define M_CLKO_PLL  0x04  // mask bit 2
	#define SET_V_CLKO_PLL(R,V)  (R = (__u8)((R & (__u8)(M_CLKO_PLL ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_CLKO_PLL(R)    (__u8)((R & M_CLKO_PLL) >> 2)

	#define M_PCM_CLK  0x20  // mask bit 5
	#define SET_V_PCM_CLK(R,V)  (R = (__u8)((R & (__u8)(M_PCM_CLK ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_PCM_CLK(R)    (__u8)((R & M_PCM_CLK) >> 5)

	#define M_CLKO_OFF  0x40  // mask bit 6
	#define SET_V_CLKO_OFF(R,V)  (R = (__u8)((R & (__u8)(M_CLKO_OFF ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_CLKO_OFF(R)    (__u8)((R & M_CLKO_OFF) >> 6)

	#define M_CLK_F1  0x80  // mask bit 7
	#define SET_V_CLK_F1(R,V)  (R = (__u8)((R & (__u8)(M_CLK_F1 ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_CLK_F1(R)    (__u8)((R & M_CLK_F1) >> 7)


#define A_Z1 0x04 // register address, read only
	#define M_Z1  0xFF  // mask bits 0..7
	#define GET_V_Z1(R)    (__u8)(R & M_Z1)


#define A_Z2 0x06 // register address, read only
	#define M_Z2  0xFF  // mask bits 0..7
	#define GET_V_Z2(R)    (__u8)(R & M_Z2)


#define R_RAM_ADDR 0x08 // register address, write only
	#define M_RAM_ADDR0  0xFF  // mask bits 0..7
	#define SET_V_RAM_ADDR0(R,V)  (R = (__u8)((R & (__u8)(M_RAM_ADDR0 ^ 0xFF)) | (__u8)V))
	#define GET_V_RAM_ADDR0(R)    (__u8)(R & M_RAM_ADDR0)


#define R_RAM_CTRL 0x09 // register address, write only
	#define M_RAM_ADDR1  0x0F  // mask bits 0..3
	#define SET_V_RAM_ADDR1(R,V)  (R = (__u8)((R & (__u8)(M_RAM_ADDR1 ^ 0xFF)) | (__u8)(V & 0x0F)))
	#define GET_V_RAM_ADDR1(R)    (__u8)(R & M_RAM_ADDR1)

	#define M_ADDR_RES  0x40  // mask bit 6
	#define SET_V_ADDR_RES(R,V)  (R = (__u8)((R & (__u8)(M_ADDR_RES ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_ADDR_RES(R)    (__u8)((R & M_ADDR_RES) >> 6)

	#define M_ADDR_INC  0x80  // mask bit 7
	#define SET_V_ADDR_INC(R,V)  (R = (__u8)((R & (__u8)(M_ADDR_INC ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_ADDR_INC(R)    (__u8)((R & M_ADDR_INC) >> 7)


#define R_FIRST_FIFO 0x0B // register address, write only
	#define M_FIRST_FIFO_DIR  0x01  // mask bit 0
	#define SET_V_FIRST_FIFO_DIR(R,V)  (R = (__u8)((R & (__u8)(M_FIRST_FIFO_DIR ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_FIRST_FIFO_DIR(R)    (__u8)(R & M_FIRST_FIFO_DIR)

	#define M_FIRST_FIFO_NUM  0x1E  // mask bits 1..4
	#define SET_V_FIRST_FIFO_NUM(R,V)  (R = (__u8)((R & (__u8)(M_FIRST_FIFO_NUM ^ 0xFF)) | (__u8)((V & 0x0F) << 1)))
	#define GET_V_FIRST_FIFO_NUM(R)    (__u8)((R & M_FIRST_FIFO_NUM) >> 1)


#define R_FIFO_THRES 0x0C // register address, write only
	#define M_THRES_TX  0x0F  // mask bits 0..3
	#define SET_V_THRES_TX(R,V)  (R = (__u8)((R & (__u8)(M_THRES_TX ^ 0xFF)) | (__u8)(V & 0x0F)))
	#define GET_V_THRES_TX(R)    (__u8)(R & M_THRES_TX)

	#define M_THRES_RX  0xF0  // mask bits 4..7
	#define SET_V_THRES_RX(R,V)  (R = (__u8)((R & (__u8)(M_THRES_RX ^ 0xFF)) | (__u8)((V & 0x0F) << 4)))
	#define GET_V_THRES_RX(R)    (__u8)((R & M_THRES_RX) >> 4)


#define A_F1 0x0C // register address, read only
	#define M_F1  0xFF  // mask bits 0..7
	#define GET_V_F1(R)    (__u8)(R & M_F1)


#define R_FIFO_MD 0x0D // register address, write only
	#define M_FIFO_MD  0x03  // mask bits 0..1
	#define SET_V_FIFO_MD(R,V)  (R = (__u8)((R & (__u8)(M_FIFO_MD ^ 0xFF)) | (__u8)(V & 0x03)))
	#define GET_V_FIFO_MD(R)    (__u8)(R & M_FIFO_MD)

	#define M_DF_MD  0x0C  // mask bits 2..3
	#define SET_V_DF_MD(R,V)  (R = (__u8)((R & (__u8)(M_DF_MD ^ 0xFF)) | (__u8)((V & 0x03) << 2)))
	#define GET_V_DF_MD(R)    (__u8)((R & M_DF_MD) >> 2)

	#define M_UNIDIR_MD  0x10  // mask bit 4
	#define SET_V_UNIDIR_MD(R,V)  (R = (__u8)((R & (__u8)(M_UNIDIR_MD ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_UNIDIR_MD(R)    (__u8)((R & M_UNIDIR_MD) >> 4)

	#define M_UNIDIR_RX  0x20  // mask bit 5
	#define SET_V_UNIDIR_RX(R,V)  (R = (__u8)((R & (__u8)(M_UNIDIR_RX ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_UNIDIR_RX(R)    (__u8)((R & M_UNIDIR_RX) >> 5)


#define A_F2 0x0D // register address, read only
	#define M_F2  0xFF  // mask bits 0..7
	#define GET_V_F2(R)    (__u8)(R & M_F2)


#define A_INC_RES_FIFO 0x0E // register address, write only
	#define M_INC_F  0x01  // mask bit 0
	#define SET_V_INC_F(R,V)  (R = (__u8)((R & (__u8)(M_INC_F ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_INC_F(R)    (__u8)(R & M_INC_F)

	#define M_RES_FIFO  0x02  // mask bit 1
	#define SET_V_RES_FIFO(R,V)  (R = (__u8)((R & (__u8)(M_RES_FIFO ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_RES_FIFO(R)    (__u8)((R & M_RES_FIFO) >> 1)

	#define M_RES_LOST  0x04  // mask bit 2
	#define SET_V_RES_LOST(R,V)  (R = (__u8)((R & (__u8)(M_RES_LOST ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_RES_LOST(R)    (__u8)((R & M_RES_LOST) >> 2)

	#define M_RES_FIFO_ERR  0x08  // mask bit 3
	#define SET_V_RES_FIFO_ERR(R,V)  (R = (__u8)((R & (__u8)(M_RES_FIFO_ERR ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_RES_FIFO_ERR(R)    (__u8)((R & M_RES_FIFO_ERR) >> 3)


#define A_FIFO_STA 0x0E // register address, read only
	#define M_FIFO_ERR  0x01  // mask bit 0
	#define GET_V_FIFO_ERR(R)    (__u8)(R & M_FIFO_ERR)

	#define M_ABO_DONE  0x10  // mask bit 4
	#define GET_V_ABO_DONE(R)    (__u8)((R & M_ABO_DONE) >> 4)


#define R_FSM_IDX 0x0F // register address, write only
	#define M_IDX  0x1F  // mask bits 0..4
	#define SET_V_IDX(R,V)  (R = (__u8)((R & (__u8)(M_IDX ^ 0xFF)) | (__u8)(V & 0x1F)))
	#define GET_V_IDX(R)    (__u8)(R & M_IDX)


#define R_FIFO 0x0F // register address, write only
	#define M_FIFO_DIR  0x01  // mask bit 0
	#define SET_V_FIFO_DIR(R,V)  (R = (__u8)((R & (__u8)(M_FIFO_DIR ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_FIFO_DIR(R)    (__u8)(R & M_FIFO_DIR)

	#define M_FIFO_NUM  0x1E  // mask bits 1..4
	#define SET_V_FIFO_NUM(R,V)  (R = (__u8)((R & (__u8)(M_FIFO_NUM ^ 0xFF)) | (__u8)((V & 0x0F) << 1)))
	#define GET_V_FIFO_NUM(R)    (__u8)((R & M_FIFO_NUM) >> 1)

	#define M_REV  0x80  // mask bit 7
	#define SET_V_REV(R,V)  (R = (__u8)((R & (__u8)(M_REV ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_REV(R)    (__u8)((R & M_REV) >> 7)


#define R_SLOT 0x10 // register address, write only
	#define M_SL_DIR  0x01  // mask bit 0
	#define SET_V_SL_DIR(R,V)  (R = (__u8)((R & (__u8)(M_SL_DIR ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_SL_DIR(R)    (__u8)(R & M_SL_DIR)

	#define M_SL_NUM  0xFE  // mask bits 1..7
	#define SET_V_SL_NUM(R,V)  (R = (__u8)((R & (__u8)(M_SL_NUM ^ 0xFF)) | (__u8)((V & 0x7F) << 1)))
	#define GET_V_SL_NUM(R)    (__u8)((R & M_SL_NUM) >> 1)


#define R_IRQ_OVIEW 0x10 // register address, read only
	#define M_FIFO_BL0_IRQ  0x01  // mask bit 0
	#define GET_V_FIFO_BL0_IRQ(R)    (__u8)(R & M_FIFO_BL0_IRQ)

	#define M_FIFO_BL1_IRQ  0x02  // mask bit 1
	#define GET_V_FIFO_BL1_IRQ(R)    (__u8)((R & M_FIFO_BL1_IRQ) >> 1)

	#define M_FIFO_BL2_IRQ  0x04  // mask bit 2
	#define GET_V_FIFO_BL2_IRQ(R)    (__u8)((R & M_FIFO_BL2_IRQ) >> 2)

	#define M_FIFO_BL3_IRQ  0x08  // mask bit 3
	#define GET_V_FIFO_BL3_IRQ(R)    (__u8)((R & M_FIFO_BL3_IRQ) >> 3)

	#define M_MISC_IRQ  0x10  // mask bit 4
	#define GET_V_MISC_IRQ(R)    (__u8)((R & M_MISC_IRQ) >> 4)

	#define M_STUP_IRQ  0x20  // mask bit 5
	#define GET_V_STUP_IRQ(R)    (__u8)((R & M_STUP_IRQ) >> 5)


#define R_MISC_IRQMSK 0x11 // register address, write only
	#define M_SLIP_IRQMSK  0x01  // mask bit 0
	#define SET_V_SLIP_IRQMSK(R,V)  (R = (__u8)((R & (__u8)(M_SLIP_IRQMSK ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_SLIP_IRQMSK(R)    (__u8)(R & M_SLIP_IRQMSK)

	#define M_TI_IRQMSK  0x02  // mask bit 1
	#define SET_V_TI_IRQMSK(R,V)  (R = (__u8)((R & (__u8)(M_TI_IRQMSK ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_TI_IRQMSK(R)    (__u8)((R & M_TI_IRQMSK) >> 1)

	#define M_PROC_IRQMSK  0x04  // mask bit 2
	#define SET_V_PROC_IRQMSK(R,V)  (R = (__u8)((R & (__u8)(M_PROC_IRQMSK ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_PROC_IRQMSK(R)    (__u8)((R & M_PROC_IRQMSK) >> 2)

	#define M_CI_IRQMSK  0x10  // mask bit 4
	#define SET_V_CI_IRQMSK(R,V)  (R = (__u8)((R & (__u8)(M_CI_IRQMSK ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_CI_IRQMSK(R)    (__u8)((R & M_CI_IRQMSK) >> 4)

	#define M_WAK_IRQMSK  0x20  // mask bit 5
	#define SET_V_WAK_IRQMSK(R,V)  (R = (__u8)((R & (__u8)(M_WAK_IRQMSK ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_WAK_IRQMSK(R)    (__u8)((R & M_WAK_IRQMSK) >> 5)

	#define M_MON_TX_IRQMSK  0x40  // mask bit 6
	#define SET_V_MON_TX_IRQMSK(R,V)  (R = (__u8)((R & (__u8)(M_MON_TX_IRQMSK ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_MON_TX_IRQMSK(R)    (__u8)((R & M_MON_TX_IRQMSK) >> 6)

	#define M_MON_RX_IRQMSK  0x80  // mask bit 7
	#define SET_V_MON_RX_IRQMSK(R,V)  (R = (__u8)((R & (__u8)(M_MON_RX_IRQMSK ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_MON_RX_IRQMSK(R)    (__u8)((R & M_MON_RX_IRQMSK) >> 7)


#define R_MISC_IRQ 0x11 // register address, read only
	#define M_SLIP_IRQ  0x01  // mask bit 0
	#define GET_V_SLIP_IRQ(R)    (__u8)(R & M_SLIP_IRQ)

	#define M_TI_IRQ  0x02  // mask bit 1
	#define GET_V_TI_IRQ(R)    (__u8)((R & M_TI_IRQ) >> 1)

	#define M_PROC_IRQ  0x04  // mask bit 2
	#define GET_V_PROC_IRQ(R)    (__u8)((R & M_PROC_IRQ) >> 2)

	#define M_CI_IRQ  0x10  // mask bit 4
	#define GET_V_CI_IRQ(R)    (__u8)((R & M_CI_IRQ) >> 4)

	#define M_WAK_IRQ  0x20  // mask bit 5
	#define GET_V_WAK_IRQ(R)    (__u8)((R & M_WAK_IRQ) >> 5)

	#define M_MON_TX_IRQ  0x40  // mask bit 6
	#define GET_V_MON_TX_IRQ(R)    (__u8)((R & M_MON_TX_IRQ) >> 6)

	#define M_MON_RX_IRQ  0x80  // mask bit 7
	#define GET_V_MON_RX_IRQ(R)    (__u8)((R & M_MON_RX_IRQ) >> 7)


#define R_SU_IRQ 0x12 // register address, read only
	#define M_SU0_IRQ  0x01  // mask bit 0
	#define GET_V_SU0_IRQ(R)    (__u8)(R & M_SU0_IRQ)

	#define M_SU1_IRQ  0x02  // mask bit 1
	#define GET_V_SU1_IRQ(R)    (__u8)((R & M_SU1_IRQ) >> 1)

	#define M_SU2_IRQ  0x04  // mask bit 2
	#define GET_V_SU2_IRQ(R)    (__u8)((R & M_SU2_IRQ) >> 2)

	#define M_SU3_IRQ  0x08  // mask bit 3
	#define GET_V_SU3_IRQ(R)    (__u8)((R & M_SU3_IRQ) >> 3)


#define R_SU_IRQMSK 0x12 // register address, write only
	#define M_SU0_IRQMSK  0x01  // mask bit 0
	#define SET_V_SU0_IRQMSK(R,V)  (R = (__u8)((R & (__u8)(M_SU0_IRQMSK ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_SU0_IRQMSK(R)    (__u8)(R & M_SU0_IRQMSK)

	#define M_SU1_IRQMSK  0x02  // mask bit 1
	#define SET_V_SU1_IRQMSK(R,V)  (R = (__u8)((R & (__u8)(M_SU1_IRQMSK ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_SU1_IRQMSK(R)    (__u8)((R & M_SU1_IRQMSK) >> 1)

	#define M_SU2_IRQMSK  0x04  // mask bit 2
	#define SET_V_SU2_IRQMSK(R,V)  (R = (__u8)((R & (__u8)(M_SU2_IRQMSK ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_SU2_IRQMSK(R)    (__u8)((R & M_SU2_IRQMSK) >> 2)

	#define M_SU3_IRQMSK  0x08  // mask bit 3
	#define SET_V_SU3_IRQMSK(R,V)  (R = (__u8)((R & (__u8)(M_SU3_IRQMSK ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_SU3_IRQMSK(R)    (__u8)((R & M_SU3_IRQMSK) >> 3)


#define R_AF0_OVIEW 0x13 // register address, read only
	#define M_SU0_AF0  0x01  // mask bit 0
	#define GET_V_SU0_AF0(R)    (__u8)(R & M_SU0_AF0)

	#define M_SU1_AF0  0x02  // mask bit 1
	#define GET_V_SU1_AF0(R)    (__u8)((R & M_SU1_AF0) >> 1)

	#define M_SU2_AF0  0x04  // mask bit 2
	#define GET_V_SU2_AF0(R)    (__u8)((R & M_SU2_AF0) >> 2)

	#define M_SU3_AF0  0x08  // mask bit 3
	#define GET_V_SU3_AF0(R)    (__u8)((R & M_SU3_AF0) >> 3)


#define R_IRQ_CTRL 0x13 // register address, write only
	#define M_FIFO_IRQ_EN  0x01  // mask bit 0
	#define SET_V_FIFO_IRQ_EN(R,V)  (R = (__u8)((R & (__u8)(M_FIFO_IRQ_EN ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_FIFO_IRQ_EN(R)    (__u8)(R & M_FIFO_IRQ_EN)

	#define M_GLOB_IRQ_EN  0x08  // mask bit 3
	#define SET_V_GLOB_IRQ_EN(R,V)  (R = (__u8)((R & (__u8)(M_GLOB_IRQ_EN ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_GLOB_IRQ_EN(R)    (__u8)((R & M_GLOB_IRQ_EN) >> 3)

	#define M_IRQ_POL  0x10  // mask bit 4
	#define SET_V_IRQ_POL(R,V)  (R = (__u8)((R & (__u8)(M_IRQ_POL ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_IRQ_POL(R)    (__u8)((R & M_IRQ_POL) >> 4)


#define A_USAGE 0x14 // register address, read only
	#define M_USAGE  0xFF  // mask bits 0..7
	#define GET_V_USAGE(R)    (__u8)(R & M_USAGE)


#define R_PCM_MD0 0x14 // register address, write only
	#define M_PCM_MD  0x01  // mask bit 0
	#define SET_V_PCM_MD(R,V)  (R = (__u8)((R & (__u8)(M_PCM_MD ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_PCM_MD(R)    (__u8)(R & M_PCM_MD)

	#define M_C4_POL  0x02  // mask bit 1
	#define SET_V_C4_POL(R,V)  (R = (__u8)((R & (__u8)(M_C4_POL ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_C4_POL(R)    (__u8)((R & M_C4_POL) >> 1)

	#define M_F0_NEG  0x04  // mask bit 2
	#define SET_V_F0_NEG(R,V)  (R = (__u8)((R & (__u8)(M_F0_NEG ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_F0_NEG(R)    (__u8)((R & M_F0_NEG) >> 2)

	#define M_F0_LEN  0x08  // mask bit 3
	#define SET_V_F0_LEN(R,V)  (R = (__u8)((R & (__u8)(M_F0_LEN ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_F0_LEN(R)    (__u8)((R & M_F0_LEN) >> 3)

	#define M_PCM_IDX  0xF0  // mask bits 4..7
	#define SET_V_PCM_IDX(R,V)  (R = (__u8)((R & (__u8)(M_PCM_IDX ^ 0xFF)) | (__u8)((V & 0x0F) << 4)))
	#define GET_V_PCM_IDX(R)    (__u8)((R & M_PCM_IDX) >> 4)


#define R_SL_SEL0 0x15 // register address, write only
	#define M_SL_SEL0  0x7F  // mask bits 0..6
	#define SET_V_SL_SEL0(R,V)  (R = (__u8)((R & (__u8)(M_SL_SEL0 ^ 0xFF)) | (__u8)(V & 0x7F)))
	#define GET_V_SL_SEL0(R)    (__u8)(R & M_SL_SEL0)

	#define M_SH_SEL0  0x80  // mask bit 7
	#define SET_V_SH_SEL0(R,V)  (R = (__u8)((R & (__u8)(M_SH_SEL0 ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_SH_SEL0(R)    (__u8)((R & M_SH_SEL0) >> 7)


#define R_SL_SEL1 0x15 // register address, write only
	#define M_SL_SEL1  0x7F  // mask bits 0..6
	#define SET_V_SL_SEL1(R,V)  (R = (__u8)((R & (__u8)(M_SL_SEL1 ^ 0xFF)) | (__u8)(V & 0x7F)))
	#define GET_V_SL_SEL1(R)    (__u8)(R & M_SL_SEL1)

	#define M_SH_SEL1  0x80  // mask bit 7
	#define SET_V_SH_SEL1(R,V)  (R = (__u8)((R & (__u8)(M_SH_SEL1 ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_SH_SEL1(R)    (__u8)((R & M_SH_SEL1) >> 7)


#define R_SL_SEL7 0x15 // register address, write only
	#define M_SL_SEL7  0x7F  // mask bits 0..6
	#define SET_V_SL_SEL7(R,V)  (R = (__u8)((R & (__u8)(M_SL_SEL7 ^ 0xFF)) | (__u8)(V & 0x7F)))
	#define GET_V_SL_SEL7(R)    (__u8)(R & M_SL_SEL7)


#define R_MSS0 0x15 // register address, write only
	#define M_MSS_MOD  0x01  // mask bit 0
	#define SET_V_MSS_MOD(R,V)  (R = (__u8)((R & (__u8)(M_MSS_MOD ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_MSS_MOD(R)    (__u8)(R & M_MSS_MOD)

	#define M_MSS_MOD_REP  0x02  // mask bit 1
	#define SET_V_MSS_MOD_REP(R,V)  (R = (__u8)((R & (__u8)(M_MSS_MOD_REP ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_MSS_MOD_REP(R)    (__u8)((R & M_MSS_MOD_REP) >> 1)

	#define M_MSS_SRC_EN  0x04  // mask bit 2
	#define SET_V_MSS_SRC_EN(R,V)  (R = (__u8)((R & (__u8)(M_MSS_SRC_EN ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_MSS_SRC_EN(R)    (__u8)((R & M_MSS_SRC_EN) >> 2)

	#define M_MSS_SRC_GRD  0x08  // mask bit 3
	#define SET_V_MSS_SRC_GRD(R,V)  (R = (__u8)((R & (__u8)(M_MSS_SRC_GRD ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_MSS_SRC_GRD(R)    (__u8)((R & M_MSS_SRC_GRD) >> 3)

	#define M_MSS_OUT_EN  0x10  // mask bit 4
	#define SET_V_MSS_OUT_EN(R,V)  (R = (__u8)((R & (__u8)(M_MSS_OUT_EN ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_MSS_OUT_EN(R)    (__u8)((R & M_MSS_OUT_EN) >> 4)

	#define M_MSS_OUT_REP  0x20  // mask bit 5
	#define SET_V_MSS_OUT_REP(R,V)  (R = (__u8)((R & (__u8)(M_MSS_OUT_REP ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_MSS_OUT_REP(R)    (__u8)((R & M_MSS_OUT_REP) >> 5)

	#define M_MSS_SRC  0xC0  // mask bits 6..7
	#define SET_V_MSS_SRC(R,V)  (R = (__u8)((R & (__u8)(M_MSS_SRC ^ 0xFF)) | (__u8)((V & 0x03) << 6)))
	#define GET_V_MSS_SRC(R)    (__u8)((R & M_MSS_SRC) >> 6)


#define R_PCM_MD1 0x15 // register address, write only
	#define M_PCM_OD  0x02  // mask bit 1
	#define SET_V_PCM_OD(R,V)  (R = (__u8)((R & (__u8)(M_PCM_OD ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_PCM_OD(R)    (__u8)((R & M_PCM_OD) >> 1)

	#define M_PLL_ADJ  0x0C  // mask bits 2..3
	#define SET_V_PLL_ADJ(R,V)  (R = (__u8)((R & (__u8)(M_PLL_ADJ ^ 0xFF)) | (__u8)((V & 0x03) << 2)))
	#define GET_V_PLL_ADJ(R)    (__u8)((R & M_PLL_ADJ) >> 2)

	#define M_PCM_DR  0x30  // mask bits 4..5
	#define SET_V_PCM_DR(R,V)  (R = (__u8)((R & (__u8)(M_PCM_DR ^ 0xFF)) | (__u8)((V & 0x03) << 4)))
	#define GET_V_PCM_DR(R)    (__u8)((R & M_PCM_DR) >> 4)

	#define M_PCM_LOOP  0x40  // mask bit 6
	#define SET_V_PCM_LOOP(R,V)  (R = (__u8)((R & (__u8)(M_PCM_LOOP ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_PCM_LOOP(R)    (__u8)((R & M_PCM_LOOP) >> 6)

	#define M_PCM_SMPL  0x80  // mask bit 7
	#define SET_V_PCM_SMPL(R,V)  (R = (__u8)((R & (__u8)(M_PCM_SMPL ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_PCM_SMPL(R)    (__u8)((R & M_PCM_SMPL) >> 7)


#define R_PCM_MD2 0x15 // register address, write only
	#define M_SYNC_OUT1  0x02  // mask bit 1
	#define SET_V_SYNC_OUT1(R,V)  (R = (__u8)((R & (__u8)(M_SYNC_OUT1 ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_SYNC_OUT1(R)    (__u8)((R & M_SYNC_OUT1) >> 1)

	#define M_SYNC_SRC  0x04  // mask bit 2
	#define SET_V_SYNC_SRC(R,V)  (R = (__u8)((R & (__u8)(M_SYNC_SRC ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_SYNC_SRC(R)    (__u8)((R & M_SYNC_SRC) >> 2)

	#define M_SYNC_OUT2  0x08  // mask bit 3
	#define SET_V_SYNC_OUT2(R,V)  (R = (__u8)((R & (__u8)(M_SYNC_OUT2 ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_SYNC_OUT2(R)    (__u8)((R & M_SYNC_OUT2) >> 3)

	#define M_C2O_EN  0x10  // mask bit 4
	#define SET_V_C2O_EN(R,V)  (R = (__u8)((R & (__u8)(M_C2O_EN ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_C2O_EN(R)    (__u8)((R & M_C2O_EN) >> 4)

	#define M_C2I_EN  0x20  // mask bit 5
	#define SET_V_C2I_EN(R,V)  (R = (__u8)((R & (__u8)(M_C2I_EN ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_C2I_EN(R)    (__u8)((R & M_C2I_EN) >> 5)

	#define M_PLL_ICR  0x40  // mask bit 6
	#define SET_V_PLL_ICR(R,V)  (R = (__u8)((R & (__u8)(M_PLL_ICR ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_PLL_ICR(R)    (__u8)((R & M_PLL_ICR) >> 6)

	#define M_PLL_MAN  0x80  // mask bit 7
	#define SET_V_PLL_MAN(R,V)  (R = (__u8)((R & (__u8)(M_PLL_MAN ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_PLL_MAN(R)    (__u8)((R & M_PLL_MAN) >> 7)


#define R_MSS1 0x15 // register address, write only
	#define M_MSS_OFFS  0x07  // mask bits 0..2
	#define SET_V_MSS_OFFS(R,V)  (R = (__u8)((R & (__u8)(M_MSS_OFFS ^ 0xFF)) | (__u8)(V & 0x07)))
	#define GET_V_MSS_OFFS(R)    (__u8)(R & M_MSS_OFFS)

	#define M_MS_SSYNC1  0x08  // mask bit 3
	#define SET_V_MS_SSYNC1(R,V)  (R = (__u8)((R & (__u8)(M_MS_SSYNC1 ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_MS_SSYNC1(R)    (__u8)((R & M_MS_SSYNC1) >> 3)

	#define M_MSS_DLY  0xF0  // mask bits 4..7
	#define SET_V_MSS_DLY(R,V)  (R = (__u8)((R & (__u8)(M_MSS_DLY ^ 0xFF)) | (__u8)((V & 0x0F) << 4)))
	#define GET_V_MSS_DLY(R)    (__u8)((R & M_MSS_DLY) >> 4)


#define R_SH0L 0x15 // register address, write only
	#define M_SH0L  0xFF  // mask bits 0..7
	#define SET_V_SH0L(R,V)  (R = (__u8)((R & (__u8)(M_SH0L ^ 0xFF)) | (__u8)V))
	#define GET_V_SH0L(R)    (__u8)(R & M_SH0L)


#define R_SH0H 0x15 // register address, write only
	#define M_SH0H  0xFF  // mask bits 0..7
	#define SET_V_SH0H(R,V)  (R = (__u8)((R & (__u8)(M_SH0H ^ 0xFF)) | (__u8)V))
	#define GET_V_SH0H(R)    (__u8)(R & M_SH0H)


#define R_SH1L 0x15 // register address, write only
	#define M_SH1L  0xFF  // mask bits 0..7
	#define SET_V_SH1L(R,V)  (R = (__u8)((R & (__u8)(M_SH1L ^ 0xFF)) | (__u8)V))
	#define GET_V_SH1L(R)    (__u8)(R & M_SH1L)


#define R_SH1H 0x15 // register address, write only
	#define M_SH1H  0xFF  // mask bits 0..7
	#define SET_V_SH1H(R,V)  (R = (__u8)((R & (__u8)(M_SH1H ^ 0xFF)) | (__u8)V))
	#define GET_V_SH1H(R)    (__u8)(R & M_SH1H)


#define R_RAM_USE 0x15 // register address, read only
	#define M_SRAM_USE  0xFF  // mask bits 0..7
	#define GET_V_SRAM_USE(R)    (__u8)(R & M_SRAM_USE)


#define R_SU_SEL 0x16 // register address, write only
	#define M_SU_SEL  0x03  // mask bits 0..1
	#define SET_V_SU_SEL(R,V)  (R = (__u8)((R & (__u8)(M_SU_SEL ^ 0xFF)) | (__u8)(V & 0x03)))
	#define GET_V_SU_SEL(R)    (__u8)(R & M_SU_SEL)

	#define M_MULT_SU  0x08  // mask bit 3
	#define SET_V_MULT_SU(R,V)  (R = (__u8)((R & (__u8)(M_MULT_SU ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_MULT_SU(R)    (__u8)((R & M_MULT_SU) >> 3)


#define R_CHIP_ID 0x16 // register address, read only
	#define M_CHIP_ID  0xFF  // mask bits 0..7
	#define GET_V_CHIP_ID(R)    (__u8)(R & M_CHIP_ID)


#define R_BERT_STA 0x17 // register address, read only
	#define M_RD_SYNC_SRC  0x07  // mask bits 0..2
	#define GET_V_RD_SYNC_SRC(R)    (__u8)(R & M_RD_SYNC_SRC)

	#define M_BERT_SYNC  0x10  // mask bit 4
	#define GET_V_BERT_SYNC(R)    (__u8)((R & M_BERT_SYNC) >> 4)

	#define M_BERT_INV_DATA  0x20  // mask bit 5
	#define GET_V_BERT_INV_DATA(R)    (__u8)((R & M_BERT_INV_DATA) >> 5)


#define R_SU_SYNC 0x17 // register address, write only
	#define M_SYNC_SEL  0x07  // mask bits 0..2
	#define SET_V_SYNC_SEL(R,V)  (R = (__u8)((R & (__u8)(M_SYNC_SEL ^ 0xFF)) | (__u8)(V & 0x07)))
	#define GET_V_SYNC_SEL(R)    (__u8)(R & M_SYNC_SEL)

	#define M_MAN_SYNC  0x08  // mask bit 3
	#define SET_V_MAN_SYNC(R,V)  (R = (__u8)((R & (__u8)(M_MAN_SYNC ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_MAN_SYNC(R)    (__u8)((R & M_MAN_SYNC) >> 3)

	#define M_AUTO_SYNCI  0x10  // mask bit 4
	#define SET_V_AUTO_SYNCI(R,V)  (R = (__u8)((R & (__u8)(M_AUTO_SYNCI ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_AUTO_SYNCI(R)    (__u8)((R & M_AUTO_SYNCI) >> 4)

	#define M_D_MERGE_TX  0x20  // mask bit 5
	#define SET_V_D_MERGE_TX(R,V)  (R = (__u8)((R & (__u8)(M_D_MERGE_TX ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_D_MERGE_TX(R)    (__u8)((R & M_D_MERGE_TX) >> 5)

	#define M_E_MERGE_RX  0x40  // mask bit 6
	#define SET_V_E_MERGE_RX(R,V)  (R = (__u8)((R & (__u8)(M_E_MERGE_RX ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_E_MERGE_RX(R)    (__u8)((R & M_E_MERGE_RX) >> 6)

	#define M_D_MERGE_RX  0x80  // mask bit 7
	#define SET_V_D_MERGE_RX(R,V)  (R = (__u8)((R & (__u8)(M_D_MERGE_RX ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_D_MERGE_RX(R)    (__u8)((R & M_D_MERGE_RX) >> 7)


#define R_F0_CNTL 0x18 // register address, read only
	#define M_F0_CNTL  0xFF  // mask bits 0..7
	#define GET_V_F0_CNTL(R)    (__u8)(R & M_F0_CNTL)


#define R_F0_CNTH 0x19 // register address, read only
	#define M_F0_CNTH  0xFF  // mask bits 0..7
	#define GET_V_F0_CNTH(R)    (__u8)(R & M_F0_CNTH)


#define R_BERT_ECL 0x1A // register address, read only
	#define M_BERT_ECL  0xFF  // mask bits 0..7
	#define GET_V_BERT_ECL(R)    (__u8)(R & M_BERT_ECL)


#define R_TI_WD 0x1A // register address, write only
	#define M_EV_TS  0x0F  // mask bits 0..3
	#define SET_V_EV_TS(R,V)  (R = (__u8)((R & (__u8)(M_EV_TS ^ 0xFF)) | (__u8)(V & 0x0F)))
	#define GET_V_EV_TS(R)    (__u8)(R & M_EV_TS)

	#define M_WD_TS  0xF0  // mask bits 4..7
	#define SET_V_WD_TS(R,V)  (R = (__u8)((R & (__u8)(M_WD_TS ^ 0xFF)) | (__u8)((V & 0x0F) << 4)))
	#define GET_V_WD_TS(R)    (__u8)((R & M_WD_TS) >> 4)


#define R_BERT_ECH 0x1B // register address, read only
	#define M_BERT_ECH  0xFF  // mask bits 0..7
	#define GET_V_BERT_ECH(R)    (__u8)(R & M_BERT_ECH)


#define R_BERT_WD_MD 0x1B // register address, write only
	#define M_PAT_SEQ  0x07  // mask bits 0..2
	#define SET_V_PAT_SEQ(R,V)  (R = (__u8)((R & (__u8)(M_PAT_SEQ ^ 0xFF)) | (__u8)(V & 0x07)))
	#define GET_V_PAT_SEQ(R)    (__u8)(R & M_PAT_SEQ)

	#define M_BERT_ERR  0x08  // mask bit 3
	#define SET_V_BERT_ERR(R,V)  (R = (__u8)((R & (__u8)(M_BERT_ERR ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_BERT_ERR(R)    (__u8)((R & M_BERT_ERR) >> 3)

	#define M_AUTO_WD_RES  0x20  // mask bit 5
	#define SET_V_AUTO_WD_RES(R,V)  (R = (__u8)((R & (__u8)(M_AUTO_WD_RES ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_AUTO_WD_RES(R)    (__u8)((R & M_AUTO_WD_RES) >> 5)

	#define M_WD_RES  0x80  // mask bit 7
	#define SET_V_WD_RES(R,V)  (R = (__u8)((R & (__u8)(M_WD_RES ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_WD_RES(R)    (__u8)((R & M_WD_RES) >> 7)


#define R_STATUS 0x1C // register address, read only
	#define M_BUSY  0x01  // mask bit 0
	#define GET_V_BUSY(R)    (__u8)(R & M_BUSY)

	#define M_PROC  0x02  // mask bit 1
	#define GET_V_PROC(R)    (__u8)((R & M_PROC) >> 1)

	#define M_LOST_STA  0x08  // mask bit 3
	#define GET_V_LOST_STA(R)    (__u8)((R & M_LOST_STA) >> 3)

	#define M_PCM_INIT  0x10  // mask bit 4
	#define GET_V_PCM_INIT(R)    (__u8)((R & M_PCM_INIT) >> 4)

	#define M_WAK_STA  0x20  // mask bit 5
	#define GET_V_WAK_STA(R)    (__u8)((R & M_WAK_STA) >> 5)

	#define M_MISC_IRQSTA  0x40  // mask bit 6
	#define GET_V_MISC_IRQSTA(R)    (__u8)((R & M_MISC_IRQSTA) >> 6)

	#define M_FIFO_IRQSTA  0x80  // mask bit 7
	#define GET_V_FIFO_IRQSTA(R)    (__u8)((R & M_FIFO_IRQSTA) >> 7)


#define R_SL_MAX 0x1D // register address, read only
	#define M_SL_MAX  0xFF  // mask bits 0..7
	#define GET_V_SL_MAX(R)    (__u8)(R & M_SL_MAX)


#define R_PWM_CFG 0x1E // register address, write only
	#define M_PWM0_16KHZ  0x10  // mask bit 4
	#define SET_V_PWM0_16KHZ(R,V)  (R = (__u8)((R & (__u8)(M_PWM0_16KHZ ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_PWM0_16KHZ(R)    (__u8)((R & M_PWM0_16KHZ) >> 4)

	#define M_PWM1_16KHZ  0x20  // mask bit 5
	#define SET_V_PWM1_16KHZ(R,V)  (R = (__u8)((R & (__u8)(M_PWM1_16KHZ ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_PWM1_16KHZ(R)    (__u8)((R & M_PWM1_16KHZ) >> 5)

	#define M_PWM_FRQ  0xC0  // mask bits 6..7
	#define SET_V_PWM_FRQ(R,V)  (R = (__u8)((R & (__u8)(M_PWM_FRQ ^ 0xFF)) | (__u8)((V & 0x03) << 6)))
	#define GET_V_PWM_FRQ(R)    (__u8)((R & M_PWM_FRQ) >> 6)


#define R_CHIP_RV 0x1F // register address, read only
	#define M_CHIP_RV  0x0F  // mask bits 0..3
	#define GET_V_CHIP_RV(R)    (__u8)(R & M_CHIP_RV)


#define R_FIFO_BL0_IRQ 0x20 // register address, read only
	#define M_FIFO0_TX_IRQ  0x01  // mask bit 0
	#define GET_V_FIFO0_TX_IRQ(R)    (__u8)(R & M_FIFO0_TX_IRQ)

	#define M_FIFO0_RX_IRQ  0x02  // mask bit 1
	#define GET_V_FIFO0_RX_IRQ(R)    (__u8)((R & M_FIFO0_RX_IRQ) >> 1)

	#define M_FIFO1_TX_IRQ  0x04  // mask bit 2
	#define GET_V_FIFO1_TX_IRQ(R)    (__u8)((R & M_FIFO1_TX_IRQ) >> 2)

	#define M_FIFO1_RX_IRQ  0x08  // mask bit 3
	#define GET_V_FIFO1_RX_IRQ(R)    (__u8)((R & M_FIFO1_RX_IRQ) >> 3)

	#define M_FIFO2_TX_IRQ  0x10  // mask bit 4
	#define GET_V_FIFO2_TX_IRQ(R)    (__u8)((R & M_FIFO2_TX_IRQ) >> 4)

	#define M_FIFO2_RX_IRQ  0x20  // mask bit 5
	#define GET_V_FIFO2_RX_IRQ(R)    (__u8)((R & M_FIFO2_RX_IRQ) >> 5)

	#define M_FIFO3_TX_IRQ  0x40  // mask bit 6
	#define GET_V_FIFO3_TX_IRQ(R)    (__u8)((R & M_FIFO3_TX_IRQ) >> 6)

	#define M_FIFO3_RX_IRQ  0x80  // mask bit 7
	#define GET_V_FIFO3_RX_IRQ(R)    (__u8)((R & M_FIFO3_RX_IRQ) >> 7)


#define R_FIFO_BL1_IRQ 0x21 // register address, read only
	#define M_FIFO4_TX_IRQ  0x01  // mask bit 0
	#define GET_V_FIFO4_TX_IRQ(R)    (__u8)(R & M_FIFO4_TX_IRQ)

	#define M_FIFO4_RX_IRQ  0x02  // mask bit 1
	#define GET_V_FIFO4_RX_IRQ(R)    (__u8)((R & M_FIFO4_RX_IRQ) >> 1)

	#define M_FIFO5_TX_IRQ  0x04  // mask bit 2
	#define GET_V_FIFO5_TX_IRQ(R)    (__u8)((R & M_FIFO5_TX_IRQ) >> 2)

	#define M_FIFO5_RX_IRQ  0x08  // mask bit 3
	#define GET_V_FIFO5_RX_IRQ(R)    (__u8)((R & M_FIFO5_RX_IRQ) >> 3)

	#define M_FIFO6_TX_IRQ  0x10  // mask bit 4
	#define GET_V_FIFO6_TX_IRQ(R)    (__u8)((R & M_FIFO6_TX_IRQ) >> 4)

	#define M_FIFO6_RX_IRQ  0x20  // mask bit 5
	#define GET_V_FIFO6_RX_IRQ(R)    (__u8)((R & M_FIFO6_RX_IRQ) >> 5)

	#define M_FIFO7_TX_IRQ  0x40  // mask bit 6
	#define GET_V_FIFO7_TX_IRQ(R)    (__u8)((R & M_FIFO7_TX_IRQ) >> 6)

	#define M_FIFO7_RX_IRQ  0x80  // mask bit 7
	#define GET_V_FIFO7_RX_IRQ(R)    (__u8)((R & M_FIFO7_RX_IRQ) >> 7)


#define R_FIFO_BL2_IRQ 0x22 // register address, read only
	#define M_FIFO8_TX_IRQ  0x01  // mask bit 0
	#define GET_V_FIFO8_TX_IRQ(R)    (__u8)(R & M_FIFO8_TX_IRQ)

	#define M_FIFO8_RX_IRQ  0x02  // mask bit 1
	#define GET_V_FIFO8_RX_IRQ(R)    (__u8)((R & M_FIFO8_RX_IRQ) >> 1)

	#define M_FIFO9_TX_IRQ  0x04  // mask bit 2
	#define GET_V_FIFO9_TX_IRQ(R)    (__u8)((R & M_FIFO9_TX_IRQ) >> 2)

	#define M_FIFO9_RX_IRQ  0x08  // mask bit 3
	#define GET_V_FIFO9_RX_IRQ(R)    (__u8)((R & M_FIFO9_RX_IRQ) >> 3)

	#define M_FIFO10_TX_IRQ  0x10  // mask bit 4
	#define GET_V_FIFO10_TX_IRQ(R)    (__u8)((R & M_FIFO10_TX_IRQ) >> 4)

	#define M_FIFO10_RX_IRQ  0x20  // mask bit 5
	#define GET_V_FIFO10_RX_IRQ(R)    (__u8)((R & M_FIFO10_RX_IRQ) >> 5)

	#define M_FIFO11_TX_IRQ  0x40  // mask bit 6
	#define GET_V_FIFO11_TX_IRQ(R)    (__u8)((R & M_FIFO11_TX_IRQ) >> 6)

	#define M_FIFO11_RX_IRQ  0x80  // mask bit 7
	#define GET_V_FIFO11_RX_IRQ(R)    (__u8)((R & M_FIFO11_RX_IRQ) >> 7)


#define R_FIFO_BL3_IRQ 0x23 // register address, read only
	#define M_FIFO12_TX_IRQ  0x01  // mask bit 0
	#define GET_V_FIFO12_TX_IRQ(R)    (__u8)(R & M_FIFO12_TX_IRQ)

	#define M_FIFO12_RX_IRQ  0x02  // mask bit 1
	#define GET_V_FIFO12_RX_IRQ(R)    (__u8)((R & M_FIFO12_RX_IRQ) >> 1)

	#define M_FIFO13_TX_IRQ  0x04  // mask bit 2
	#define GET_V_FIFO13_TX_IRQ(R)    (__u8)((R & M_FIFO13_TX_IRQ) >> 2)

	#define M_FIFO13_RX_IRQ  0x08  // mask bit 3
	#define GET_V_FIFO13_RX_IRQ(R)    (__u8)((R & M_FIFO13_RX_IRQ) >> 3)

	#define M_FIFO14_TX_IRQ  0x10  // mask bit 4
	#define GET_V_FIFO14_TX_IRQ(R)    (__u8)((R & M_FIFO14_TX_IRQ) >> 4)

	#define M_FIFO14_RX_IRQ  0x20  // mask bit 5
	#define GET_V_FIFO14_RX_IRQ(R)    (__u8)((R & M_FIFO14_RX_IRQ) >> 5)

	#define M_FIFO15_TX_IRQ  0x40  // mask bit 6
	#define GET_V_FIFO15_TX_IRQ(R)    (__u8)((R & M_FIFO15_TX_IRQ) >> 6)

	#define M_FIFO15_RX_IRQ  0x80  // mask bit 7
	#define GET_V_FIFO15_RX_IRQ(R)    (__u8)((R & M_FIFO15_RX_IRQ) >> 7)


#define R_FILL_BL0 0x24 // register address, read only
	#define M_FILL_FIFO0_TX  0x01  // mask bit 0
	#define GET_V_FILL_FIFO0_TX(R)    (__u8)(R & M_FILL_FIFO0_TX)

	#define M_FILL_FIFO0_RX  0x02  // mask bit 1
	#define GET_V_FILL_FIFO0_RX(R)    (__u8)((R & M_FILL_FIFO0_RX) >> 1)

	#define M_FILL_FIFO1_TX  0x04  // mask bit 2
	#define GET_V_FILL_FIFO1_TX(R)    (__u8)((R & M_FILL_FIFO1_TX) >> 2)

	#define M_FILL_FIFO1_RX  0x08  // mask bit 3
	#define GET_V_FILL_FIFO1_RX(R)    (__u8)((R & M_FILL_FIFO1_RX) >> 3)

	#define M_FILL_FIFO2_TX  0x10  // mask bit 4
	#define GET_V_FILL_FIFO2_TX(R)    (__u8)((R & M_FILL_FIFO2_TX) >> 4)

	#define M_FILL_FIFO2_RX  0x20  // mask bit 5
	#define GET_V_FILL_FIFO2_RX(R)    (__u8)((R & M_FILL_FIFO2_RX) >> 5)

	#define M_FILL_FIFO3_TX  0x40  // mask bit 6
	#define GET_V_FILL_FIFO3_TX(R)    (__u8)((R & M_FILL_FIFO3_TX) >> 6)

	#define M_FILL_FIFO3_RX  0x80  // mask bit 7
	#define GET_V_FILL_FIFO3_RX(R)    (__u8)((R & M_FILL_FIFO3_RX) >> 7)


#define R_FILL_BL1 0x25 // register address, read only
	#define M_FILL_FIFO4_TX  0x01  // mask bit 0
	#define GET_V_FILL_FIFO4_TX(R)    (__u8)(R & M_FILL_FIFO4_TX)

	#define M_FILL_FIFO4_RX  0x02  // mask bit 1
	#define GET_V_FILL_FIFO4_RX(R)    (__u8)((R & M_FILL_FIFO4_RX) >> 1)

	#define M_FILL_FIFO5_TX  0x04  // mask bit 2
	#define GET_V_FILL_FIFO5_TX(R)    (__u8)((R & M_FILL_FIFO5_TX) >> 2)

	#define M_FILL_FIFO5_RX  0x08  // mask bit 3
	#define GET_V_FILL_FIFO5_RX(R)    (__u8)((R & M_FILL_FIFO5_RX) >> 3)

	#define M_FILL_FIFO6_TX  0x10  // mask bit 4
	#define GET_V_FILL_FIFO6_TX(R)    (__u8)((R & M_FILL_FIFO6_TX) >> 4)

	#define M_FILL_FIFO6_RX  0x20  // mask bit 5
	#define GET_V_FILL_FIFO6_RX(R)    (__u8)((R & M_FILL_FIFO6_RX) >> 5)

	#define M_FILL_FIFO7_TX  0x40  // mask bit 6
	#define GET_V_FILL_FIFO7_TX(R)    (__u8)((R & M_FILL_FIFO7_TX) >> 6)

	#define M_FILL_FIFO7_RX  0x80  // mask bit 7
	#define GET_V_FILL_FIFO7_RX(R)    (__u8)((R & M_FILL_FIFO7_RX) >> 7)


#define R_FILL_BL2 0x26 // register address, read only
	#define M_FILL_FIFO8_TX  0x01  // mask bit 0
	#define GET_V_FILL_FIFO8_TX(R)    (__u8)(R & M_FILL_FIFO8_TX)

	#define M_FILL_FIFO8_RX  0x02  // mask bit 1
	#define GET_V_FILL_FIFO8_RX(R)    (__u8)((R & M_FILL_FIFO8_RX) >> 1)

	#define M_FILL_FIFO9_TX  0x04  // mask bit 2
	#define GET_V_FILL_FIFO9_TX(R)    (__u8)((R & M_FILL_FIFO9_TX) >> 2)

	#define M_FILL_FIFO9_RX  0x08  // mask bit 3
	#define GET_V_FILL_FIFO9_RX(R)    (__u8)((R & M_FILL_FIFO9_RX) >> 3)

	#define M_FILL_FIFO10_TX  0x10  // mask bit 4
	#define GET_V_FILL_FIFO10_TX(R)    (__u8)((R & M_FILL_FIFO10_TX) >> 4)

	#define M_FILL_FIFO10_RX  0x20  // mask bit 5
	#define GET_V_FILL_FIFO10_RX(R)    (__u8)((R & M_FILL_FIFO10_RX) >> 5)

	#define M_FILL_FIFO11_TX  0x40  // mask bit 6
	#define GET_V_FILL_FIFO11_TX(R)    (__u8)((R & M_FILL_FIFO11_TX) >> 6)

	#define M_FILL_FIFO11_RX  0x80  // mask bit 7
	#define GET_V_FILL_FIFO11_RX(R)    (__u8)((R & M_FILL_FIFO11_RX) >> 7)


#define R_FILL_BL3 0x27 // register address, read only
	#define M_FILL_FIFO12_TX  0x01  // mask bit 0
	#define GET_V_FILL_FIFO12_TX(R)    (__u8)(R & M_FILL_FIFO12_TX)

	#define M_FILL_FIFO12_RX  0x02  // mask bit 1
	#define GET_V_FILL_FIFO12_RX(R)    (__u8)((R & M_FILL_FIFO12_RX) >> 1)

	#define M_FILL_FIFO13_TX  0x04  // mask bit 2
	#define GET_V_FILL_FIFO13_TX(R)    (__u8)((R & M_FILL_FIFO13_TX) >> 2)

	#define M_FILL_FIFO13_RX  0x08  // mask bit 3
	#define GET_V_FILL_FIFO13_RX(R)    (__u8)((R & M_FILL_FIFO13_RX) >> 3)

	#define M_FILL_FIFO14_TX  0x10  // mask bit 4
	#define GET_V_FILL_FIFO14_TX(R)    (__u8)((R & M_FILL_FIFO14_TX) >> 4)

	#define M_FILL_FIFO14_RX  0x20  // mask bit 5
	#define GET_V_FILL_FIFO14_RX(R)    (__u8)((R & M_FILL_FIFO14_RX) >> 5)

	#define M_FILL_FIFO15_TX  0x40  // mask bit 6
	#define GET_V_FILL_FIFO15_TX(R)    (__u8)((R & M_FILL_FIFO15_TX) >> 6)

	#define M_FILL_FIFO15_RX  0x80  // mask bit 7
	#define GET_V_FILL_FIFO15_RX(R)    (__u8)((R & M_FILL_FIFO15_RX) >> 7)


#define R_CI_TX 0x28 // register address, write only
	#define M_GCI_C  0x3F  // mask bits 0..5
	#define SET_V_GCI_C(R,V)  (R = (__u8)((R & (__u8)(M_GCI_C ^ 0xFF)) | (__u8)(V & 0x3F)))
	#define GET_V_GCI_C(R)    (__u8)(R & M_GCI_C)


#define R_CI_RX 0x28 // register address, read only
	#define M_GCI_I  0x3F  // mask bits 0..5
	#define GET_V_GCI_I(R)    (__u8)(R & M_GCI_I)


#define R_GCI_CFG0 0x29 // register address, write only
	#define M_MON_END  0x01  // mask bit 0
	#define SET_V_MON_END(R,V)  (R = (__u8)((R & (__u8)(M_MON_END ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_MON_END(R)    (__u8)(R & M_MON_END)

	#define M_MON_SLOW  0x02  // mask bit 1
	#define SET_V_MON_SLOW(R,V)  (R = (__u8)((R & (__u8)(M_MON_SLOW ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_MON_SLOW(R)    (__u8)((R & M_MON_SLOW) >> 1)

	#define M_MON_DLL  0x04  // mask bit 2
	#define SET_V_MON_DLL(R,V)  (R = (__u8)((R & (__u8)(M_MON_DLL ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_MON_DLL(R)    (__u8)((R & M_MON_DLL) >> 2)

	#define M_MON_CI6  0x08  // mask bit 3
	#define SET_V_MON_CI6(R,V)  (R = (__u8)((R & (__u8)(M_MON_CI6 ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_MON_CI6(R)    (__u8)((R & M_MON_CI6) >> 3)

	#define M_GCI_SWAP_TXHS  0x10  // mask bit 4
	#define SET_V_GCI_SWAP_TXHS(R,V)  (R = (__u8)((R & (__u8)(M_GCI_SWAP_TXHS ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_GCI_SWAP_TXHS(R)    (__u8)((R & M_GCI_SWAP_TXHS) >> 4)

	#define M_GCI_SWAP_RXHS  0x20  // mask bit 5
	#define SET_V_GCI_SWAP_RXHS(R,V)  (R = (__u8)((R & (__u8)(M_GCI_SWAP_RXHS ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_GCI_SWAP_RXHS(R)    (__u8)((R & M_GCI_SWAP_RXHS) >> 5)

	#define M_GCI_SWAP_STIO  0x40  // mask bit 6
	#define SET_V_GCI_SWAP_STIO(R,V)  (R = (__u8)((R & (__u8)(M_GCI_SWAP_STIO ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_GCI_SWAP_STIO(R)    (__u8)((R & M_GCI_SWAP_STIO) >> 6)

	#define M_GCI_EN  0x80  // mask bit 7
	#define SET_V_GCI_EN(R,V)  (R = (__u8)((R & (__u8)(M_GCI_EN ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_GCI_EN(R)    (__u8)((R & M_GCI_EN) >> 7)


#define R_GCI_STA 0x29 // register address, read only
	#define M_MON_RXR  0x01  // mask bit 0
	#define GET_V_MON_RXR(R)    (__u8)(R & M_MON_RXR)

	#define M_MON_TXR  0x02  // mask bit 1
	#define GET_V_MON_TXR(R)    (__u8)((R & M_MON_TXR) >> 1)

	#define M_GCI_MX  0x04  // mask bit 2
	#define GET_V_GCI_MX(R)    (__u8)((R & M_GCI_MX) >> 2)

	#define M_GCI_MR  0x08  // mask bit 3
	#define GET_V_GCI_MR(R)    (__u8)((R & M_GCI_MR) >> 3)

	#define M_GCI_RX  0x10  // mask bit 4
	#define GET_V_GCI_RX(R)    (__u8)((R & M_GCI_RX) >> 4)

	#define M_GCI_ABO  0x20  // mask bit 5
	#define GET_V_GCI_ABO(R)    (__u8)((R & M_GCI_ABO) >> 5)


#define R_GCI_CFG1 0x2A // register address, write only
	#define M_GCI_SL  0x1F  // mask bits 0..4
	#define SET_V_GCI_SL(R,V)  (R = (__u8)((R & (__u8)(M_GCI_SL ^ 0xFF)) | (__u8)(V & 0x1F)))
	#define GET_V_GCI_SL(R)    (__u8)(R & M_GCI_SL)


#define R_MON_RX 0x2A // register address, read only
	#define M_MON_RX  0xFF  // mask bits 0..7
	#define GET_V_MON_RX(R)    (__u8)(R & M_MON_RX)


#define R_MON_TX 0x2B // register address, write only
	#define M_MON_TX  0xFF  // mask bits 0..7
	#define SET_V_MON_TX(R,V)  (R = (__u8)((R & (__u8)(M_MON_TX ^ 0xFF)) | (__u8)V))
	#define GET_V_MON_TX(R)    (__u8)(R & M_MON_TX)


#define A_SU_WR_STA 0x30 // register address, write only
	#define M_SU_SET_STA  0x0F  // mask bits 0..3
	#define SET_V_SU_SET_STA(R,V)  (R = (__u8)((R & (__u8)(M_SU_SET_STA ^ 0xFF)) | (__u8)(V & 0x0F)))
	#define GET_V_SU_SET_STA(R)    (__u8)(R & M_SU_SET_STA)

	#define M_SU_LD_STA  0x10  // mask bit 4
	#define SET_V_SU_LD_STA(R,V)  (R = (__u8)((R & (__u8)(M_SU_LD_STA ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_SU_LD_STA(R)    (__u8)((R & M_SU_LD_STA) >> 4)

	#define M_SU_ACT  0x60  // mask bits 5..6
	#define SET_V_SU_ACT(R,V)  (R = (__u8)((R & (__u8)(M_SU_ACT ^ 0xFF)) | (__u8)((V & 0x03) << 5)))
	#define GET_V_SU_ACT(R)    (__u8)((R & M_SU_ACT) >> 5)

	#define M_SU_SET_G2_G3  0x80  // mask bit 7
	#define SET_V_SU_SET_G2_G3(R,V)  (R = (__u8)((R & (__u8)(M_SU_SET_G2_G3 ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_SU_SET_G2_G3(R)    (__u8)((R & M_SU_SET_G2_G3) >> 7)


#define A_SU_RD_STA 0x30 // register address, read only
	#define M_SU_STA  0x0F  // mask bits 0..3
	#define GET_V_SU_STA(R)    (__u8)(R & M_SU_STA)

	#define M_SU_FR_SYNC  0x10  // mask bit 4
	#define GET_V_SU_FR_SYNC(R)    (__u8)((R & M_SU_FR_SYNC) >> 4)

	#define M_SU_T2_EXP  0x20  // mask bit 5
	#define GET_V_SU_T2_EXP(R)    (__u8)((R & M_SU_T2_EXP) >> 5)

	#define M_SU_INFO0  0x40  // mask bit 6
	#define GET_V_SU_INFO0(R)    (__u8)((R & M_SU_INFO0) >> 6)

	#define M_G2_G3  0x80  // mask bit 7
	#define GET_V_G2_G3(R)    (__u8)((R & M_G2_G3) >> 7)


#define A_SU_CTRL0 0x31 // register address, write only
	#define M_B1_TX_EN  0x01  // mask bit 0
	#define SET_V_B1_TX_EN(R,V)  (R = (__u8)((R & (__u8)(M_B1_TX_EN ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_B1_TX_EN(R)    (__u8)(R & M_B1_TX_EN)

	#define M_B2_TX_EN  0x02  // mask bit 1
	#define SET_V_B2_TX_EN(R,V)  (R = (__u8)((R & (__u8)(M_B2_TX_EN ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_B2_TX_EN(R)    (__u8)((R & M_B2_TX_EN) >> 1)

	#define M_SU_MD  0x04  // mask bit 2
	#define SET_V_SU_MD(R,V)  (R = (__u8)((R & (__u8)(M_SU_MD ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_SU_MD(R)    (__u8)((R & M_SU_MD) >> 2)

	#define M_ST_D_LPRIO  0x08  // mask bit 3
	#define SET_V_ST_D_LPRIO(R,V)  (R = (__u8)((R & (__u8)(M_ST_D_LPRIO ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_ST_D_LPRIO(R)    (__u8)((R & M_ST_D_LPRIO) >> 3)

	#define M_ST_SQ_EN  0x10  // mask bit 4
	#define SET_V_ST_SQ_EN(R,V)  (R = (__u8)((R & (__u8)(M_ST_SQ_EN ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_ST_SQ_EN(R)    (__u8)((R & M_ST_SQ_EN) >> 4)

	#define M_SU_TST_SIG  0x20  // mask bit 5
	#define SET_V_SU_TST_SIG(R,V)  (R = (__u8)((R & (__u8)(M_SU_TST_SIG ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_SU_TST_SIG(R)    (__u8)((R & M_SU_TST_SIG) >> 5)

	#define M_ST_PU_CTRL  0x40  // mask bit 6
	#define SET_V_ST_PU_CTRL(R,V)  (R = (__u8)((R & (__u8)(M_ST_PU_CTRL ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_ST_PU_CTRL(R)    (__u8)((R & M_ST_PU_CTRL) >> 6)

	#define M_SU_STOP  0x80  // mask bit 7
	#define SET_V_SU_STOP(R,V)  (R = (__u8)((R & (__u8)(M_SU_STOP ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_SU_STOP(R)    (__u8)((R & M_SU_STOP) >> 7)


#define A_SU_DLYL 0x31 // register address, read only
	#define M_SU_DLYL  0x1F  // mask bits 0..4
	#define GET_V_SU_DLYL(R)    (__u8)(R & M_SU_DLYL)


#define A_SU_CTRL1 0x32 // register address, write only
	#define M_G2_G3_EN  0x01  // mask bit 0
	#define SET_V_G2_G3_EN(R,V)  (R = (__u8)((R & (__u8)(M_G2_G3_EN ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_G2_G3_EN(R)    (__u8)(R & M_G2_G3_EN)

	#define M_D_RES  0x04  // mask bit 2
	#define SET_V_D_RES(R,V)  (R = (__u8)((R & (__u8)(M_D_RES ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_D_RES(R)    (__u8)((R & M_D_RES) >> 2)

	#define M_ST_E_IGNO  0x08  // mask bit 3
	#define SET_V_ST_E_IGNO(R,V)  (R = (__u8)((R & (__u8)(M_ST_E_IGNO ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_ST_E_IGNO(R)    (__u8)((R & M_ST_E_IGNO) >> 3)

	#define M_ST_E_LO  0x10  // mask bit 4
	#define SET_V_ST_E_LO(R,V)  (R = (__u8)((R & (__u8)(M_ST_E_LO ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_ST_E_LO(R)    (__u8)((R & M_ST_E_LO) >> 4)

	#define M_BAC_D  0x40  // mask bit 6
	#define SET_V_BAC_D(R,V)  (R = (__u8)((R & (__u8)(M_BAC_D ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_BAC_D(R)    (__u8)((R & M_BAC_D) >> 6)

	#define M_B12_SWAP  0x80  // mask bit 7
	#define SET_V_B12_SWAP(R,V)  (R = (__u8)((R & (__u8)(M_B12_SWAP ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_B12_SWAP(R)    (__u8)((R & M_B12_SWAP) >> 7)


#define A_SU_DLYH 0x32 // register address, read only
	#define M_SU_DLYH  0x1F  // mask bits 0..4
	#define GET_V_SU_DLYH(R)    (__u8)(R & M_SU_DLYH)


#define A_SU_CTRL2 0x33 // register address, write only
	#define M_B1_RX_EN  0x01  // mask bit 0
	#define SET_V_B1_RX_EN(R,V)  (R = (__u8)((R & (__u8)(M_B1_RX_EN ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_B1_RX_EN(R)    (__u8)(R & M_B1_RX_EN)

	#define M_B2_RX_EN  0x02  // mask bit 1
	#define SET_V_B2_RX_EN(R,V)  (R = (__u8)((R & (__u8)(M_B2_RX_EN ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_B2_RX_EN(R)    (__u8)((R & M_B2_RX_EN) >> 1)

	#define M_MS_SSYNC2  0x04  // mask bit 2
	#define SET_V_MS_SSYNC2(R,V)  (R = (__u8)((R & (__u8)(M_MS_SSYNC2 ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_MS_SSYNC2(R)    (__u8)((R & M_MS_SSYNC2) >> 2)

	#define M_BAC_S_SEL  0x08  // mask bit 3
	#define SET_V_BAC_S_SEL(R,V)  (R = (__u8)((R & (__u8)(M_BAC_S_SEL ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_BAC_S_SEL(R)    (__u8)((R & M_BAC_S_SEL) >> 3)

	#define M_SU_SYNC_NT  0x10  // mask bit 4
	#define SET_V_SU_SYNC_NT(R,V)  (R = (__u8)((R & (__u8)(M_SU_SYNC_NT ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_SU_SYNC_NT(R)    (__u8)((R & M_SU_SYNC_NT) >> 4)

	#define M_SU_2KHZ  0x20  // mask bit 5
	#define SET_V_SU_2KHZ(R,V)  (R = (__u8)((R & (__u8)(M_SU_2KHZ ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_SU_2KHZ(R)    (__u8)((R & M_SU_2KHZ) >> 5)

	#define M_SU_TRI  0x40  // mask bit 6
	#define SET_V_SU_TRI(R,V)  (R = (__u8)((R & (__u8)(M_SU_TRI ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_SU_TRI(R)    (__u8)((R & M_SU_TRI) >> 6)

	#define M_SU_EXCHG  0x80  // mask bit 7
	#define SET_V_SU_EXCHG(R,V)  (R = (__u8)((R & (__u8)(M_SU_EXCHG ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_SU_EXCHG(R)    (__u8)((R & M_SU_EXCHG) >> 7)


#define A_MS_TX 0x34 // register address, write only
	#define M_MS_TX  0x0F  // mask bits 0..3
	#define SET_V_MS_TX(R,V)  (R = (__u8)((R & (__u8)(M_MS_TX ^ 0xFF)) | (__u8)(V & 0x0F)))
	#define GET_V_MS_TX(R)    (__u8)(R & M_MS_TX)

	#define M_UP_S_TX  0x40  // mask bit 6
	#define SET_V_UP_S_TX(R,V)  (R = (__u8)((R & (__u8)(M_UP_S_TX ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_UP_S_TX(R)    (__u8)((R & M_UP_S_TX) >> 6)


#define A_MS_RX 0x34 // register address, read only
	#define M_MS_RX  0x0F  // mask bits 0..3
	#define GET_V_MS_RX(R)    (__u8)(R & M_MS_RX)

	#define M_MS_RX_RDY  0x10  // mask bit 4
	#define GET_V_MS_RX_RDY(R)    (__u8)((R & M_MS_RX_RDY) >> 4)

	#define M_UP_S_RX  0x40  // mask bit 6
	#define GET_V_UP_S_RX(R)    (__u8)((R & M_UP_S_RX) >> 6)

	#define M_MS_TX_RDY  0x80  // mask bit 7
	#define GET_V_MS_TX_RDY(R)    (__u8)((R & M_MS_TX_RDY) >> 7)


#define A_ST_CTRL3 0x35 // register address, write only
	#define M_ST_SEL  0x01  // mask bit 0
	#define SET_V_ST_SEL(R,V)  (R = (__u8)((R & (__u8)(M_ST_SEL ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_ST_SEL(R)    (__u8)(R & M_ST_SEL)

	#define M_ST_PULSE  0xFE  // mask bits 1..7
	#define SET_V_ST_PULSE(R,V)  (R = (__u8)((R & (__u8)(M_ST_PULSE ^ 0xFF)) | (__u8)((V & 0x7F) << 1)))
	#define GET_V_ST_PULSE(R)    (__u8)((R & M_ST_PULSE) >> 1)


#define A_UP_CTRL3 0x35 // register address, write only
	#define M_UP_SEL  0x01  // mask bit 0
	#define SET_V_UP_SEL(R,V)  (R = (__u8)((R & (__u8)(M_UP_SEL ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_UP_SEL(R)    (__u8)(R & M_UP_SEL)

	#define M_UP_VIO  0x02  // mask bit 1
	#define SET_V_UP_VIO(R,V)  (R = (__u8)((R & (__u8)(M_UP_VIO ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_UP_VIO(R)    (__u8)((R & M_UP_VIO) >> 1)

	#define M_UP_DC_STR  0x04  // mask bit 2
	#define SET_V_UP_DC_STR(R,V)  (R = (__u8)((R & (__u8)(M_UP_DC_STR ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_UP_DC_STR(R)    (__u8)((R & M_UP_DC_STR) >> 2)

	#define M_UP_DC_OFF  0x08  // mask bit 3
	#define SET_V_UP_DC_OFF(R,V)  (R = (__u8)((R & (__u8)(M_UP_DC_OFF ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_UP_DC_OFF(R)    (__u8)((R & M_UP_DC_OFF) >> 3)

	#define M_UP_RPT_PAT  0x10  // mask bit 4
	#define SET_V_UP_RPT_PAT(R,V)  (R = (__u8)((R & (__u8)(M_UP_RPT_PAT ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_UP_RPT_PAT(R)    (__u8)((R & M_UP_RPT_PAT) >> 4)

	#define M_UP_SCRM_MD  0x20  // mask bit 5
	#define SET_V_UP_SCRM_MD(R,V)  (R = (__u8)((R & (__u8)(M_UP_SCRM_MD ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_UP_SCRM_MD(R)    (__u8)((R & M_UP_SCRM_MD) >> 5)

	#define M_UP_SCRM_TX_OFF  0x40  // mask bit 6
	#define SET_V_UP_SCRM_TX_OFF(R,V)  (R = (__u8)((R & (__u8)(M_UP_SCRM_TX_OFF ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_UP_SCRM_TX_OFF(R)    (__u8)((R & M_UP_SCRM_TX_OFF) >> 6)

	#define M_UP_SCRM_RX_OFF  0x80  // mask bit 7
	#define SET_V_UP_SCRM_RX_OFF(R,V)  (R = (__u8)((R & (__u8)(M_UP_SCRM_RX_OFF ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_UP_SCRM_RX_OFF(R)    (__u8)((R & M_UP_SCRM_RX_OFF) >> 7)


#define A_SU_STA 0x35 // register address, read only
	#define M_ST_D_HPRIO9  0x01  // mask bit 0
	#define GET_V_ST_D_HPRIO9(R)    (__u8)(R & M_ST_D_HPRIO9)

	#define M_ST_D_LPRIO11  0x02  // mask bit 1
	#define GET_V_ST_D_LPRIO11(R)    (__u8)((R & M_ST_D_LPRIO11) >> 1)

	#define M_ST_D_CONT  0x04  // mask bit 2
	#define GET_V_ST_D_CONT(R)    (__u8)((R & M_ST_D_CONT) >> 2)

	#define M_ST_D_ACT  0x08  // mask bit 3
	#define GET_V_ST_D_ACT(R)    (__u8)((R & M_ST_D_ACT) >> 3)

	#define M_SU_AF0  0x80  // mask bit 7
	#define GET_V_SU_AF0(R)    (__u8)((R & M_SU_AF0) >> 7)


#define A_MS_DF 0x36 // register address, write only
	#define M_BAC_NINV  0x01  // mask bit 0
	#define SET_V_BAC_NINV(R,V)  (R = (__u8)((R & (__u8)(M_BAC_NINV ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_BAC_NINV(R)    (__u8)(R & M_BAC_NINV)

	#define M_SG_AB_INV  0x02  // mask bit 1
	#define SET_V_SG_AB_INV(R,V)  (R = (__u8)((R & (__u8)(M_SG_AB_INV ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_SG_AB_INV(R)    (__u8)((R & M_SG_AB_INV) >> 1)

	#define M_SQ_T_SRC  0x04  // mask bit 2
	#define SET_V_SQ_T_SRC(R,V)  (R = (__u8)((R & (__u8)(M_SQ_T_SRC ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_SQ_T_SRC(R)    (__u8)((R & M_SQ_T_SRC) >> 2)

	#define M_M_S_SRC  0x08  // mask bit 3
	#define SET_V_M_S_SRC(R,V)  (R = (__u8)((R & (__u8)(M_M_S_SRC ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_M_S_SRC(R)    (__u8)((R & M_M_S_SRC) >> 3)

	#define M_SQ_T_DST  0x10  // mask bit 4
	#define SET_V_SQ_T_DST(R,V)  (R = (__u8)((R & (__u8)(M_SQ_T_DST ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_SQ_T_DST(R)    (__u8)((R & M_SQ_T_DST) >> 4)

	#define M_SU_RX_VAL  0x20  // mask bit 5
	#define SET_V_SU_RX_VAL(R,V)  (R = (__u8)((R & (__u8)(M_SU_RX_VAL ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_SU_RX_VAL(R)    (__u8)((R & M_SU_RX_VAL) >> 5)


#define A_SU_CLK_DLY 0x37 // register address, write only
	#define M_SU_CLK_DLY  0x0F  // mask bits 0..3
	#define SET_V_SU_CLK_DLY(R,V)  (R = (__u8)((R & (__u8)(M_SU_CLK_DLY ^ 0xFF)) | (__u8)(V & 0x0F)))
	#define GET_V_SU_CLK_DLY(R)    (__u8)(R & M_SU_CLK_DLY)

	#define M_ST_SMPL  0x70  // mask bits 4..6
	#define SET_V_ST_SMPL(R,V)  (R = (__u8)((R & (__u8)(M_ST_SMPL ^ 0xFF)) | (__u8)((V & 0x07) << 4)))
	#define GET_V_ST_SMPL(R)    (__u8)((R & M_ST_SMPL) >> 4)


#define R_PWM0 0x38 // register address, write only
	#define M_PWM0  0xFF  // mask bits 0..7
	#define SET_V_PWM0(R,V)  (R = (__u8)((R & (__u8)(M_PWM0 ^ 0xFF)) | (__u8)V))
	#define GET_V_PWM0(R)    (__u8)(R & M_PWM0)


#define R_PWM1 0x39 // register address, write only
	#define M_PWM1  0xFF  // mask bits 0..7
	#define SET_V_PWM1(R,V)  (R = (__u8)((R & (__u8)(M_PWM1 ^ 0xFF)) | (__u8)V))
	#define GET_V_PWM1(R)    (__u8)(R & M_PWM1)


#define A_B1_TX 0x3C // register address, write only
	#define M_B1_TX  0xFF  // mask bits 0..7
	#define SET_V_B1_TX(R,V)  (R = (__u8)((R & (__u8)(M_B1_TX ^ 0xFF)) | (__u8)V))
	#define GET_V_B1_TX(R)    (__u8)(R & M_B1_TX)


#define A_B1_RX 0x3C // register address, read only
	#define M_B1_RX  0xFF  // mask bits 0..7
	#define GET_V_B1_RX(R)    (__u8)(R & M_B1_RX)


#define A_B2_TX 0x3D // register address, write only
	#define M_B2_TX  0xFF  // mask bits 0..7
	#define SET_V_B2_TX(R,V)  (R = (__u8)((R & (__u8)(M_B2_TX ^ 0xFF)) | (__u8)V))
	#define GET_V_B2_TX(R)    (__u8)(R & M_B2_TX)


#define A_B2_RX 0x3D // register address, read only
	#define M_B2_RX  0xFF  // mask bits 0..7
	#define GET_V_B2_RX(R)    (__u8)(R & M_B2_RX)


#define A_D_TX 0x3E // register address, write only
	#define M_D_TX_S  0x01  // mask bit 0
	#define SET_V_D_TX_S(R,V)  (R = (__u8)((R & (__u8)(M_D_TX_S ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_D_TX_S(R)    (__u8)(R & M_D_TX_S)

	#define M_D_TX_BAC  0x20  // mask bit 5
	#define SET_V_D_TX_BAC(R,V)  (R = (__u8)((R & (__u8)(M_D_TX_BAC ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_D_TX_BAC(R)    (__u8)((R & M_D_TX_BAC) >> 5)

	#define M_D_TX  0xC0  // mask bits 6..7
	#define SET_V_D_TX(R,V)  (R = (__u8)((R & (__u8)(M_D_TX ^ 0xFF)) | (__u8)((V & 0x03) << 6)))
	#define GET_V_D_TX(R)    (__u8)((R & M_D_TX) >> 6)


#define A_D_RX 0x3E // register address, read only
	#define M_D_RX_S  0x01  // mask bit 0
	#define GET_V_D_RX_S(R)    (__u8)(R & M_D_RX_S)

	#define M_D_RX_AB  0x10  // mask bit 4
	#define GET_V_D_RX_AB(R)    (__u8)((R & M_D_RX_AB) >> 4)

	#define M_D_RX_SG  0x20  // mask bit 5
	#define GET_V_D_RX_SG(R)    (__u8)((R & M_D_RX_SG) >> 5)

	#define M_D_RX  0xC0  // mask bits 6..7
	#define GET_V_D_RX(R)    (__u8)((R & M_D_RX) >> 6)


#define A_E_RX 0x3F // register address, read only
	#define M_E_RX_S  0x01  // mask bit 0
	#define GET_V_E_RX_S(R)    (__u8)(R & M_E_RX_S)

	#define M_E_RX_AB  0x10  // mask bit 4
	#define GET_V_E_RX_AB(R)    (__u8)((R & M_E_RX_AB) >> 4)

	#define M_E_RX_SG  0x20  // mask bit 5
	#define GET_V_E_RX_SG(R)    (__u8)((R & M_E_RX_SG) >> 5)

	#define M_E_RX  0xC0  // mask bits 6..7
	#define GET_V_E_RX(R)    (__u8)((R & M_E_RX) >> 6)


#define A_BAC_S_TX 0x3F // register address, write only
	#define M_S_TX  0x01  // mask bit 0
	#define SET_V_S_TX(R,V)  (R = (__u8)((R & (__u8)(M_S_TX ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_S_TX(R)    (__u8)(R & M_S_TX)

	#define M_BAC_TX  0x20  // mask bit 5
	#define SET_V_BAC_TX(R,V)  (R = (__u8)((R & (__u8)(M_BAC_TX ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_BAC_TX(R)    (__u8)((R & M_BAC_TX) >> 5)


#define R_GPIO_OUT1 0x40 // register address, write only
	#define M_GPIO_OUT8  0x01  // mask bit 0
	#define SET_V_GPIO_OUT8(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT8 ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_GPIO_OUT8(R)    (__u8)(R & M_GPIO_OUT8)

	#define M_GPIO_OUT9  0x02  // mask bit 1
	#define SET_V_GPIO_OUT9(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT9 ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_GPIO_OUT9(R)    (__u8)((R & M_GPIO_OUT9) >> 1)

	#define M_GPIO_OUT10  0x04  // mask bit 2
	#define SET_V_GPIO_OUT10(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT10 ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_GPIO_OUT10(R)    (__u8)((R & M_GPIO_OUT10) >> 2)

	#define M_GPIO_OUT11  0x08  // mask bit 3
	#define SET_V_GPIO_OUT11(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT11 ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_GPIO_OUT11(R)    (__u8)((R & M_GPIO_OUT11) >> 3)

	#define M_GPIO_OUT12  0x10  // mask bit 4
	#define SET_V_GPIO_OUT12(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT12 ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_GPIO_OUT12(R)    (__u8)((R & M_GPIO_OUT12) >> 4)

	#define M_GPIO_OUT13  0x20  // mask bit 5
	#define SET_V_GPIO_OUT13(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT13 ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_GPIO_OUT13(R)    (__u8)((R & M_GPIO_OUT13) >> 5)

	#define M_GPIO_OUT14  0x40  // mask bit 6
	#define SET_V_GPIO_OUT14(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT14 ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_GPIO_OUT14(R)    (__u8)((R & M_GPIO_OUT14) >> 6)

	#define M_GPIO_OUT15  0x80  // mask bit 7
	#define SET_V_GPIO_OUT15(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT15 ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_GPIO_OUT15(R)    (__u8)((R & M_GPIO_OUT15) >> 7)


#define R_GPIO_IN1 0x40 // register address, read only
	#define M_GPIO_IN8  0x01  // mask bit 0
	#define GET_V_GPIO_IN8(R)    (__u8)(R & M_GPIO_IN8)

	#define M_GPIO_IN9  0x02  // mask bit 1
	#define GET_V_GPIO_IN9(R)    (__u8)((R & M_GPIO_IN9) >> 1)

	#define M_GPIO_IN10  0x04  // mask bit 2
	#define GET_V_GPIO_IN10(R)    (__u8)((R & M_GPIO_IN10) >> 2)

	#define M_GPIO_IN11  0x08  // mask bit 3
	#define GET_V_GPIO_IN11(R)    (__u8)((R & M_GPIO_IN11) >> 3)

	#define M_GPIO_IN12  0x10  // mask bit 4
	#define GET_V_GPIO_IN12(R)    (__u8)((R & M_GPIO_IN12) >> 4)

	#define M_GPIO_IN13  0x20  // mask bit 5
	#define GET_V_GPIO_IN13(R)    (__u8)((R & M_GPIO_IN13) >> 5)

	#define M_GPIO_IN14  0x40  // mask bit 6
	#define GET_V_GPIO_IN14(R)    (__u8)((R & M_GPIO_IN14) >> 6)

	#define M_GPIO_IN15  0x80  // mask bit 7
	#define GET_V_GPIO_IN15(R)    (__u8)((R & M_GPIO_IN15) >> 7)


#define R_GPIO_OUT3 0x41 // register address, write only
	#define M_GPIO_OUT24  0x01  // mask bit 0
	#define SET_V_GPIO_OUT24(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT24 ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_GPIO_OUT24(R)    (__u8)(R & M_GPIO_OUT24)

	#define M_GPIO_OUT25  0x02  // mask bit 1
	#define SET_V_GPIO_OUT25(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT25 ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_GPIO_OUT25(R)    (__u8)((R & M_GPIO_OUT25) >> 1)

	#define M_GPIO_OUT26  0x04  // mask bit 2
	#define SET_V_GPIO_OUT26(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT26 ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_GPIO_OUT26(R)    (__u8)((R & M_GPIO_OUT26) >> 2)

	#define M_GPIO_OUT27  0x08  // mask bit 3
	#define SET_V_GPIO_OUT27(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT27 ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_GPIO_OUT27(R)    (__u8)((R & M_GPIO_OUT27) >> 3)

	#define M_GPIO_OUT28  0x10  // mask bit 4
	#define SET_V_GPIO_OUT28(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT28 ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_GPIO_OUT28(R)    (__u8)((R & M_GPIO_OUT28) >> 4)

	#define M_GPIO_OUT29  0x20  // mask bit 5
	#define SET_V_GPIO_OUT29(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT29 ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_GPIO_OUT29(R)    (__u8)((R & M_GPIO_OUT29) >> 5)

	#define M_GPIO_OUT30  0x40  // mask bit 6
	#define SET_V_GPIO_OUT30(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT30 ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_GPIO_OUT30(R)    (__u8)((R & M_GPIO_OUT30) >> 6)

	#define M_GPIO_OUT31  0x80  // mask bit 7
	#define SET_V_GPIO_OUT31(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT31 ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_GPIO_OUT31(R)    (__u8)((R & M_GPIO_OUT31) >> 7)


#define R_GPIO_IN3 0x41 // register address, read only
	#define M_GPIO_IN24  0x01  // mask bit 0
	#define GET_V_GPIO_IN24(R)    (__u8)(R & M_GPIO_IN24)

	#define M_GPIO_IN25  0x02  // mask bit 1
	#define GET_V_GPIO_IN25(R)    (__u8)((R & M_GPIO_IN25) >> 1)

	#define M_GPIO_IN26  0x04  // mask bit 2
	#define GET_V_GPIO_IN26(R)    (__u8)((R & M_GPIO_IN26) >> 2)

	#define M_GPIO_IN27  0x08  // mask bit 3
	#define GET_V_GPIO_IN27(R)    (__u8)((R & M_GPIO_IN27) >> 3)

	#define M_GPIO_IN28  0x10  // mask bit 4
	#define GET_V_GPIO_IN28(R)    (__u8)((R & M_GPIO_IN28) >> 4)

	#define M_GPIO_IN29  0x20  // mask bit 5
	#define GET_V_GPIO_IN29(R)    (__u8)((R & M_GPIO_IN29) >> 5)

	#define M_GPIO_IN30  0x40  // mask bit 6
	#define GET_V_GPIO_IN30(R)    (__u8)((R & M_GPIO_IN30) >> 6)

	#define M_GPIO_IN31  0x80  // mask bit 7
	#define GET_V_GPIO_IN31(R)    (__u8)((R & M_GPIO_IN31) >> 7)


#define R_GPIO_EN1 0x42 // register address, write only
	#define M_GPIO_EN8  0x01  // mask bit 0
	#define SET_V_GPIO_EN8(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN8 ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_GPIO_EN8(R)    (__u8)(R & M_GPIO_EN8)

	#define M_GPIO_EN9  0x02  // mask bit 1
	#define SET_V_GPIO_EN9(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN9 ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_GPIO_EN9(R)    (__u8)((R & M_GPIO_EN9) >> 1)

	#define M_GPIO_EN10  0x04  // mask bit 2
	#define SET_V_GPIO_EN10(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN10 ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_GPIO_EN10(R)    (__u8)((R & M_GPIO_EN10) >> 2)

	#define M_GPIO_EN11  0x08  // mask bit 3
	#define SET_V_GPIO_EN11(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN11 ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_GPIO_EN11(R)    (__u8)((R & M_GPIO_EN11) >> 3)

	#define M_GPIO_EN12  0x10  // mask bit 4
	#define SET_V_GPIO_EN12(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN12 ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_GPIO_EN12(R)    (__u8)((R & M_GPIO_EN12) >> 4)

	#define M_GPIO_EN13  0x20  // mask bit 5
	#define SET_V_GPIO_EN13(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN13 ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_GPIO_EN13(R)    (__u8)((R & M_GPIO_EN13) >> 5)

	#define M_GPIO_EN14  0x40  // mask bit 6
	#define SET_V_GPIO_EN14(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN14 ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_GPIO_EN14(R)    (__u8)((R & M_GPIO_EN14) >> 6)

	#define M_GPIO_EN15  0x80  // mask bit 7
	#define SET_V_GPIO_EN15(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN15 ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_GPIO_EN15(R)    (__u8)((R & M_GPIO_EN15) >> 7)


#define R_GPIO_EN3 0x43 // register address, write only
	#define M_GPIO_EN24  0x01  // mask bit 0
	#define SET_V_GPIO_EN24(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN24 ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_GPIO_EN24(R)    (__u8)(R & M_GPIO_EN24)

	#define M_GPIO_EN25  0x02  // mask bit 1
	#define SET_V_GPIO_EN25(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN25 ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_GPIO_EN25(R)    (__u8)((R & M_GPIO_EN25) >> 1)

	#define M_GPIO_EN26  0x04  // mask bit 2
	#define SET_V_GPIO_EN26(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN26 ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_GPIO_EN26(R)    (__u8)((R & M_GPIO_EN26) >> 2)

	#define M_GPIO_EN27  0x08  // mask bit 3
	#define SET_V_GPIO_EN27(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN27 ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_GPIO_EN27(R)    (__u8)((R & M_GPIO_EN27) >> 3)

	#define M_GPIO_EN28  0x10  // mask bit 4
	#define SET_V_GPIO_EN28(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN28 ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_GPIO_EN28(R)    (__u8)((R & M_GPIO_EN28) >> 4)

	#define M_GPIO_EN29  0x20  // mask bit 5
	#define SET_V_GPIO_EN29(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN29 ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_GPIO_EN29(R)    (__u8)((R & M_GPIO_EN29) >> 5)

	#define M_GPIO_EN30  0x40  // mask bit 6
	#define SET_V_GPIO_EN30(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN30 ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_GPIO_EN30(R)    (__u8)((R & M_GPIO_EN30) >> 6)

	#define M_GPIO_EN31  0x80  // mask bit 7
	#define SET_V_GPIO_EN31(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN31 ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_GPIO_EN31(R)    (__u8)((R & M_GPIO_EN31) >> 7)


#define R_GPIO_SEL_BL 0x44 // register address, write only
	#define M_GPIO_BL0  0x01  // mask bit 0
	#define SET_V_GPIO_BL0(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_BL0 ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_GPIO_BL0(R)    (__u8)(R & M_GPIO_BL0)

	#define M_GPIO_BL1  0x02  // mask bit 1
	#define SET_V_GPIO_BL1(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_BL1 ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_GPIO_BL1(R)    (__u8)((R & M_GPIO_BL1) >> 1)

	#define M_GPIO_BL2  0x04  // mask bit 2
	#define SET_V_GPIO_BL2(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_BL2 ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_GPIO_BL2(R)    (__u8)((R & M_GPIO_BL2) >> 2)

	#define M_GPIO_BL3  0x08  // mask bit 3
	#define SET_V_GPIO_BL3(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_BL3 ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_GPIO_BL3(R)    (__u8)((R & M_GPIO_BL3) >> 3)


#define R_GPIO_OUT2 0x45 // register address, write only
	#define M_GPIO_OUT16  0x01  // mask bit 0
	#define SET_V_GPIO_OUT16(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT16 ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_GPIO_OUT16(R)    (__u8)(R & M_GPIO_OUT16)

	#define M_GPIO_OUT17  0x02  // mask bit 1
	#define SET_V_GPIO_OUT17(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT17 ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_GPIO_OUT17(R)    (__u8)((R & M_GPIO_OUT17) >> 1)

	#define M_GPIO_OUT18  0x04  // mask bit 2
	#define SET_V_GPIO_OUT18(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT18 ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_GPIO_OUT18(R)    (__u8)((R & M_GPIO_OUT18) >> 2)

	#define M_GPIO_OUT19  0x08  // mask bit 3
	#define SET_V_GPIO_OUT19(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT19 ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_GPIO_OUT19(R)    (__u8)((R & M_GPIO_OUT19) >> 3)

	#define M_GPIO_OUT20  0x10  // mask bit 4
	#define SET_V_GPIO_OUT20(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT20 ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_GPIO_OUT20(R)    (__u8)((R & M_GPIO_OUT20) >> 4)

	#define M_GPIO_OUT21  0x20  // mask bit 5
	#define SET_V_GPIO_OUT21(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT21 ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_GPIO_OUT21(R)    (__u8)((R & M_GPIO_OUT21) >> 5)

	#define M_GPIO_OUT22  0x40  // mask bit 6
	#define SET_V_GPIO_OUT22(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT22 ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_GPIO_OUT22(R)    (__u8)((R & M_GPIO_OUT22) >> 6)

	#define M_GPIO_OUT23  0x80  // mask bit 7
	#define SET_V_GPIO_OUT23(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT23 ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_GPIO_OUT23(R)    (__u8)((R & M_GPIO_OUT23) >> 7)


#define R_GPIO_IN2 0x45 // register address, read only
	#define M_GPIO_IN16  0x01  // mask bit 0
	#define GET_V_GPIO_IN16(R)    (__u8)(R & M_GPIO_IN16)

	#define M_GPIO_IN17  0x02  // mask bit 1
	#define GET_V_GPIO_IN17(R)    (__u8)((R & M_GPIO_IN17) >> 1)

	#define M_GPIO_IN18  0x04  // mask bit 2
	#define GET_V_GPIO_IN18(R)    (__u8)((R & M_GPIO_IN18) >> 2)

	#define M_GPIO_IN19  0x08  // mask bit 3
	#define GET_V_GPIO_IN19(R)    (__u8)((R & M_GPIO_IN19) >> 3)

	#define M_GPIO_IN20  0x10  // mask bit 4
	#define GET_V_GPIO_IN20(R)    (__u8)((R & M_GPIO_IN20) >> 4)

	#define M_GPIO_IN21  0x20  // mask bit 5
	#define GET_V_GPIO_IN21(R)    (__u8)((R & M_GPIO_IN21) >> 5)

	#define M_GPIO_IN22  0x40  // mask bit 6
	#define GET_V_GPIO_IN22(R)    (__u8)((R & M_GPIO_IN22) >> 6)

	#define M_GPIO_IN23  0x80  // mask bit 7
	#define GET_V_GPIO_IN23(R)    (__u8)((R & M_GPIO_IN23) >> 7)


#define R_PWM_MD 0x46 // register address, write only
	#define M_WAK_EN  0x02  // mask bit 1
	#define SET_V_WAK_EN(R,V)  (R = (__u8)((R & (__u8)(M_WAK_EN ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_WAK_EN(R)    (__u8)((R & M_WAK_EN) >> 1)

	#define M_PWM0_MD  0x30  // mask bits 4..5
	#define SET_V_PWM0_MD(R,V)  (R = (__u8)((R & (__u8)(M_PWM0_MD ^ 0xFF)) | (__u8)((V & 0x03) << 4)))
	#define GET_V_PWM0_MD(R)    (__u8)((R & M_PWM0_MD) >> 4)

	#define M_PWM1_MD  0xC0  // mask bits 6..7
	#define SET_V_PWM1_MD(R,V)  (R = (__u8)((R & (__u8)(M_PWM1_MD ^ 0xFF)) | (__u8)((V & 0x03) << 6)))
	#define GET_V_PWM1_MD(R)    (__u8)((R & M_PWM1_MD) >> 6)


#define R_GPIO_EN2 0x47 // register address, write only
	#define M_GPIO_EN16  0x01  // mask bit 0
	#define SET_V_GPIO_EN16(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN16 ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_GPIO_EN16(R)    (__u8)(R & M_GPIO_EN16)

	#define M_GPIO_EN17  0x02  // mask bit 1
	#define SET_V_GPIO_EN17(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN17 ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_GPIO_EN17(R)    (__u8)((R & M_GPIO_EN17) >> 1)

	#define M_GPIO_EN18  0x04  // mask bit 2
	#define SET_V_GPIO_EN18(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN18 ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_GPIO_EN18(R)    (__u8)((R & M_GPIO_EN18) >> 2)

	#define M_GPIO_EN19  0x08  // mask bit 3
	#define SET_V_GPIO_EN19(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN19 ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_GPIO_EN19(R)    (__u8)((R & M_GPIO_EN19) >> 3)

	#define M_GPIO_EN20  0x10  // mask bit 4
	#define SET_V_GPIO_EN20(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN20 ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_GPIO_EN20(R)    (__u8)((R & M_GPIO_EN20) >> 4)

	#define M_GPIO_EN21  0x20  // mask bit 5
	#define SET_V_GPIO_EN21(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN21 ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_GPIO_EN21(R)    (__u8)((R & M_GPIO_EN21) >> 5)

	#define M_GPIO_EN22  0x40  // mask bit 6
	#define SET_V_GPIO_EN22(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN22 ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_GPIO_EN22(R)    (__u8)((R & M_GPIO_EN22) >> 6)

	#define M_GPIO_EN23  0x80  // mask bit 7
	#define SET_V_GPIO_EN23(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN23 ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_GPIO_EN23(R)    (__u8)((R & M_GPIO_EN23) >> 7)


#define R_GPIO_IN0 0x48 // register address, read only
	#define M_GPIO_IN0  0x01  // mask bit 0
	#define GET_V_GPIO_IN0(R)    (__u8)(R & M_GPIO_IN0)

	#define M_GPIO_IN1  0x02  // mask bit 1
	#define GET_V_GPIO_IN1(R)    (__u8)((R & M_GPIO_IN1) >> 1)

	#define M_GPIO_IN2  0x04  // mask bit 2
	#define GET_V_GPIO_IN2(R)    (__u8)((R & M_GPIO_IN2) >> 2)

	#define M_GPIO_IN3  0x08  // mask bit 3
	#define GET_V_GPIO_IN3(R)    (__u8)((R & M_GPIO_IN3) >> 3)

	#define M_GPIO_IN4  0x10  // mask bit 4
	#define GET_V_GPIO_IN4(R)    (__u8)((R & M_GPIO_IN4) >> 4)

	#define M_GPIO_IN5  0x20  // mask bit 5
	#define GET_V_GPIO_IN5(R)    (__u8)((R & M_GPIO_IN5) >> 5)

	#define M_GPIO_IN6  0x40  // mask bit 6
	#define GET_V_GPIO_IN6(R)    (__u8)((R & M_GPIO_IN6) >> 6)

	#define M_GPIO_IN7  0x80  // mask bit 7
	#define GET_V_GPIO_IN7(R)    (__u8)((R & M_GPIO_IN7) >> 7)


#define R_GPIO_OUT0 0x48 // register address, write only
	#define M_GPIO_OUT0  0x01  // mask bit 0
	#define SET_V_GPIO_OUT0(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT0 ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_GPIO_OUT0(R)    (__u8)(R & M_GPIO_OUT0)

	#define M_GPIO_OUT1  0x02  // mask bit 1
	#define SET_V_GPIO_OUT1(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT1 ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_GPIO_OUT1(R)    (__u8)((R & M_GPIO_OUT1) >> 1)

	#define M_GPIO_OUT2  0x04  // mask bit 2
	#define SET_V_GPIO_OUT2(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT2 ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_GPIO_OUT2(R)    (__u8)((R & M_GPIO_OUT2) >> 2)

	#define M_GPIO_OUT3  0x08  // mask bit 3
	#define SET_V_GPIO_OUT3(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT3 ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_GPIO_OUT3(R)    (__u8)((R & M_GPIO_OUT3) >> 3)

	#define M_GPIO_OUT4  0x10  // mask bit 4
	#define SET_V_GPIO_OUT4(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT4 ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_GPIO_OUT4(R)    (__u8)((R & M_GPIO_OUT4) >> 4)

	#define M_GPIO_OUT5  0x20  // mask bit 5
	#define SET_V_GPIO_OUT5(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT5 ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_GPIO_OUT5(R)    (__u8)((R & M_GPIO_OUT5) >> 5)

	#define M_GPIO_OUT6  0x40  // mask bit 6
	#define SET_V_GPIO_OUT6(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT6 ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_GPIO_OUT6(R)    (__u8)((R & M_GPIO_OUT6) >> 6)

	#define M_GPIO_OUT7  0x80  // mask bit 7
	#define SET_V_GPIO_OUT7(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_OUT7 ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_GPIO_OUT7(R)    (__u8)((R & M_GPIO_OUT7) >> 7)


#define R_GPIO_EN0 0x4A // register address, write only
	#define M_GPIO_EN0  0x01  // mask bit 0
	#define SET_V_GPIO_EN0(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN0 ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_GPIO_EN0(R)    (__u8)(R & M_GPIO_EN0)

	#define M_GPIO_EN1  0x02  // mask bit 1
	#define SET_V_GPIO_EN1(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN1 ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_GPIO_EN1(R)    (__u8)((R & M_GPIO_EN1) >> 1)

	#define M_GPIO_EN2  0x04  // mask bit 2
	#define SET_V_GPIO_EN2(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN2 ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_GPIO_EN2(R)    (__u8)((R & M_GPIO_EN2) >> 2)

	#define M_GPIO_EN3  0x08  // mask bit 3
	#define SET_V_GPIO_EN3(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN3 ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_GPIO_EN3(R)    (__u8)((R & M_GPIO_EN3) >> 3)

	#define M_GPIO_EN4  0x10  // mask bit 4
	#define SET_V_GPIO_EN4(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN4 ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_GPIO_EN4(R)    (__u8)((R & M_GPIO_EN4) >> 4)

	#define M_GPIO_EN5  0x20  // mask bit 5
	#define SET_V_GPIO_EN5(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN5 ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_GPIO_EN5(R)    (__u8)((R & M_GPIO_EN5) >> 5)

	#define M_GPIO_EN6  0x40  // mask bit 6
	#define SET_V_GPIO_EN6(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN6 ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_GPIO_EN6(R)    (__u8)((R & M_GPIO_EN6) >> 6)

	#define M_GPIO_EN7  0x80  // mask bit 7
	#define SET_V_GPIO_EN7(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_EN7 ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_GPIO_EN7(R)    (__u8)((R & M_GPIO_EN7) >> 7)


#define R_GPIO_SEL 0x4C // register address, write only
	#define M_GPIO_SEL0  0x01  // mask bit 0
	#define SET_V_GPIO_SEL0(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_SEL0 ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_GPIO_SEL0(R)    (__u8)(R & M_GPIO_SEL0)

	#define M_GPIO_SEL1  0x02  // mask bit 1
	#define SET_V_GPIO_SEL1(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_SEL1 ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_GPIO_SEL1(R)    (__u8)((R & M_GPIO_SEL1) >> 1)

	#define M_GPIO_SEL2  0x04  // mask bit 2
	#define SET_V_GPIO_SEL2(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_SEL2 ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_GPIO_SEL2(R)    (__u8)((R & M_GPIO_SEL2) >> 2)

	#define M_GPIO_SEL3  0x08  // mask bit 3
	#define SET_V_GPIO_SEL3(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_SEL3 ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_GPIO_SEL3(R)    (__u8)((R & M_GPIO_SEL3) >> 3)

	#define M_GPIO_SEL4  0x10  // mask bit 4
	#define SET_V_GPIO_SEL4(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_SEL4 ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_GPIO_SEL4(R)    (__u8)((R & M_GPIO_SEL4) >> 4)

	#define M_GPIO_SEL5  0x20  // mask bit 5
	#define SET_V_GPIO_SEL5(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_SEL5 ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_GPIO_SEL5(R)    (__u8)((R & M_GPIO_SEL5) >> 5)

	#define M_GPIO_SEL6  0x40  // mask bit 6
	#define SET_V_GPIO_SEL6(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_SEL6 ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_GPIO_SEL6(R)    (__u8)((R & M_GPIO_SEL6) >> 6)

	#define M_GPIO_SEL7  0x80  // mask bit 7
	#define SET_V_GPIO_SEL7(R,V)  (R = (__u8)((R & (__u8)(M_GPIO_SEL7 ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_GPIO_SEL7(R)    (__u8)((R & M_GPIO_SEL7) >> 7)


#define R_PLL_STA 0x50 // register address, read only
	#define M_PLL_LOCK  0x80  // mask bit 7
	#define GET_V_PLL_LOCK(R)    (__u8)((R & M_PLL_LOCK) >> 7)


#define R_PLL_CTRL 0x50 // register address, write only
	#define M_PLL_NRES  0x01  // mask bit 0
	#define SET_V_PLL_NRES(R,V)  (R = (__u8)((R & (__u8)(M_PLL_NRES ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_PLL_NRES(R)    (__u8)(R & M_PLL_NRES)

	#define M_PLL_TST  0x02  // mask bit 1
	#define SET_V_PLL_TST(R,V)  (R = (__u8)((R & (__u8)(M_PLL_TST ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_PLL_TST(R)    (__u8)((R & M_PLL_TST) >> 1)

	#define M_PLL_FREEZE  0x20  // mask bit 5
	#define SET_V_PLL_FREEZE(R,V)  (R = (__u8)((R & (__u8)(M_PLL_FREEZE ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_PLL_FREEZE(R)    (__u8)((R & M_PLL_FREEZE) >> 5)

	#define M_PLL_M  0xC0  // mask bits 6..7
	#define SET_V_PLL_M(R,V)  (R = (__u8)((R & (__u8)(M_PLL_M ^ 0xFF)) | (__u8)((V & 0x03) << 6)))
	#define GET_V_PLL_M(R)    (__u8)((R & M_PLL_M) >> 6)


#define R_PLL_P 0x51 // register address, read/write
	#define M_PLL_P  0xFF  // mask bits 0..7
	#define SET_V_PLL_P(R,V)  (R = (__u8)((R & (__u8)(M_PLL_P ^ 0xFF)) | (__u8)V))
	#define GET_V_PLL_P(R)    (__u8)(R & M_PLL_P)


#define R_PLL_N 0x52 // register address, read/write
	#define M_PLL_N  0xFF  // mask bits 0..7
	#define SET_V_PLL_N(R,V)  (R = (__u8)((R & (__u8)(M_PLL_N ^ 0xFF)) | (__u8)V))
	#define GET_V_PLL_N(R)    (__u8)(R & M_PLL_N)


#define R_PLL_S 0x53 // register address, read/write
	#define M_PLL_S  0xFF  // mask bits 0..7
	#define SET_V_PLL_S(R,V)  (R = (__u8)((R & (__u8)(M_PLL_S ^ 0xFF)) | (__u8)V))
	#define GET_V_PLL_S(R)    (__u8)(R & M_PLL_S)


#define A_FIFO_DATA 0x80 // register address, read/write
	#define M_FIFO_DATA  0xFF  // mask bits 0..7
	#define SET_V_FIFO_DATA(R,V)  (R = (__u8)((R & (__u8)(M_FIFO_DATA ^ 0xFF)) | (__u8)V))
	#define GET_V_FIFO_DATA(R)    (__u8)(R & M_FIFO_DATA)


#define A_FIFO_DATA_NOINC 0x84 // register address, read/write
	#define M_FIFO_DATA_NOINC  0xFF  // mask bits 0..7
	#define SET_V_FIFO_DATA_NOINC(R,V)  (R = (__u8)((R & (__u8)(M_FIFO_DATA_NOINC ^ 0xFF)) | (__u8)V))
	#define GET_V_FIFO_DATA_NOINC(R)    (__u8)(R & M_FIFO_DATA_NOINC)


#define R_INT_DATA 0x88 // register address, read only
	#define M_INT_DATA  0xFF  // mask bits 0..7
	#define GET_V_INT_DATA(R)    (__u8)(R & M_INT_DATA)


#define R_RAM_DATA 0xC0 // register address, r*/w
	#define M_RAM_DATA  0xFF  // mask bits 0..7
	#define SET_V_RAM_DATA(R,V)  (R = (__u8)((R & (__u8)(M_RAM_DATA ^ 0xFF)) | (__u8)V))
	#define GET_V_RAM_DATA(R)    (__u8)(R & M_RAM_DATA)


#define A_SL_CFG 0xD0 // register address, r*/w
	#define M_CH_SDIR  0x01  // mask bit 0
	#define SET_V_CH_SDIR(R,V)  (R = (__u8)((R & (__u8)(M_CH_SDIR ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_CH_SDIR(R)    (__u8)(R & M_CH_SDIR)

	#define M_CH_SNUM  0x3E  // mask bits 1..5
	#define SET_V_CH_SNUM(R,V)  (R = (__u8)((R & (__u8)(M_CH_SNUM ^ 0xFF)) | (__u8)((V & 0x1F) << 1)))
	#define GET_V_CH_SNUM(R)    (__u8)((R & M_CH_SNUM) >> 1)

	#define M_ROUT  0xC0  // mask bits 6..7
	#define SET_V_ROUT(R,V)  (R = (__u8)((R & (__u8)(M_ROUT ^ 0xFF)) | (__u8)((V & 0x03) << 6)))
	#define GET_V_ROUT(R)    (__u8)((R & M_ROUT) >> 6)


#define A_CH_MSK 0xF4 // register address, r*/w
	#define M_CH_MSK  0xFF  // mask bits 0..7
	#define SET_V_CH_MSK(R,V)  (R = (__u8)((R & (__u8)(M_CH_MSK ^ 0xFF)) | (__u8)V))
	#define GET_V_CH_MSK(R)    (__u8)(R & M_CH_MSK)


#define A_CON_HDLC 0xFA // register address, r*/w
	#define M_IFF  0x01  // mask bit 0
	#define SET_V_IFF(R,V)  (R = (__u8)((R & (__u8)(M_IFF ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_IFF(R)    (__u8)(R & M_IFF)

	#define M_HDLC_TRP  0x02  // mask bit 1
	#define SET_V_HDLC_TRP(R,V)  (R = (__u8)((R & (__u8)(M_HDLC_TRP ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_HDLC_TRP(R)    (__u8)((R & M_HDLC_TRP) >> 1)

	#define M_FIFO_IRQ  0x1C  // mask bits 2..4
	#define SET_V_FIFO_IRQ(R,V)  (R = (__u8)((R & (__u8)(M_FIFO_IRQ ^ 0xFF)) | (__u8)((V & 0x07) << 2)))
	#define GET_V_FIFO_IRQ(R)    (__u8)((R & M_FIFO_IRQ) >> 2)

	#define M_DATA_FLOW  0xE0  // mask bits 5..7
	#define SET_V_DATA_FLOW(R,V)  (R = (__u8)((R & (__u8)(M_DATA_FLOW ^ 0xFF)) | (__u8)((V & 0x07) << 5)))
	#define GET_V_DATA_FLOW(R)    (__u8)((R & M_DATA_FLOW) >> 5)


#define A_SUBCH_CFG 0xFB // register address, r*/w
	#define M_BIT_CNT  0x07  // mask bits 0..2
	#define SET_V_BIT_CNT(R,V)  (R = (__u8)((R & (__u8)(M_BIT_CNT ^ 0xFF)) | (__u8)(V & 0x07)))
	#define GET_V_BIT_CNT(R)    (__u8)(R & M_BIT_CNT)

	#define M_START_BIT  0x38  // mask bits 3..5
	#define SET_V_START_BIT(R,V)  (R = (__u8)((R & (__u8)(M_START_BIT ^ 0xFF)) | (__u8)((V & 0x07) << 3)))
	#define GET_V_START_BIT(R)    (__u8)((R & M_START_BIT) >> 3)

	#define M_LOOP_FIFO  0x40  // mask bit 6
	#define SET_V_LOOP_FIFO(R,V)  (R = (__u8)((R & (__u8)(M_LOOP_FIFO ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_LOOP_FIFO(R)    (__u8)((R & M_LOOP_FIFO) >> 6)

	#define M_INV_DATA  0x80  // mask bit 7
	#define SET_V_INV_DATA(R,V)  (R = (__u8)((R & (__u8)(M_INV_DATA ^ 0xFF)) | (__u8)((V & 0x01) << 7)))
	#define GET_V_INV_DATA(R)    (__u8)((R & M_INV_DATA) >> 7)


#define A_CHANNEL 0xFC // register address, r*/w
	#define M_CH_FDIR  0x01  // mask bit 0
	#define SET_V_CH_FDIR(R,V)  (R = (__u8)((R & (__u8)(M_CH_FDIR ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_CH_FDIR(R)    (__u8)(R & M_CH_FDIR)

	#define M_CH_FNUM  0x1E  // mask bits 1..4
	#define SET_V_CH_FNUM(R,V)  (R = (__u8)((R & (__u8)(M_CH_FNUM ^ 0xFF)) | (__u8)((V & 0x0F) << 1)))
	#define GET_V_CH_FNUM(R)    (__u8)((R & M_CH_FNUM) >> 1)


#define A_FIFO_SEQ 0xFD // register address, r*/w
	#define M_NEXT_FIFO_DIR  0x01  // mask bit 0
	#define SET_V_NEXT_FIFO_DIR(R,V)  (R = (__u8)((R & (__u8)(M_NEXT_FIFO_DIR ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_NEXT_FIFO_DIR(R)    (__u8)(R & M_NEXT_FIFO_DIR)

	#define M_NEXT_FIFO_NUM  0x1E  // mask bits 1..4
	#define SET_V_NEXT_FIFO_NUM(R,V)  (R = (__u8)((R & (__u8)(M_NEXT_FIFO_NUM ^ 0xFF)) | (__u8)((V & 0x0F) << 1)))
	#define GET_V_NEXT_FIFO_NUM(R)    (__u8)((R & M_NEXT_FIFO_NUM) >> 1)

	#define M_SEQ_END  0x40  // mask bit 6
	#define SET_V_SEQ_END(R,V)  (R = (__u8)((R & (__u8)(M_SEQ_END ^ 0xFF)) | (__u8)((V & 0x01) << 6)))
	#define GET_V_SEQ_END(R)    (__u8)((R & M_SEQ_END) >> 6)


#define A_FIFO_CTRL 0xFF // register address, r*/w
	#define M_FIFO_IRQMSK  0x01  // mask bit 0
	#define SET_V_FIFO_IRQMSK(R,V)  (R = (__u8)((R & (__u8)(M_FIFO_IRQMSK ^ 0xFF)) | (__u8)(V & 0x01)))
	#define GET_V_FIFO_IRQMSK(R)    (__u8)(R & M_FIFO_IRQMSK)

	#define M_BERT_EN  0x02  // mask bit 1
	#define SET_V_BERT_EN(R,V)  (R = (__u8)((R & (__u8)(M_BERT_EN ^ 0xFF)) | (__u8)((V & 0x01) << 1)))
	#define GET_V_BERT_EN(R)    (__u8)((R & M_BERT_EN) >> 1)

	#define M_MIX_IRQ  0x04  // mask bit 2
	#define SET_V_MIX_IRQ(R,V)  (R = (__u8)((R & (__u8)(M_MIX_IRQ ^ 0xFF)) | (__u8)((V & 0x01) << 2)))
	#define GET_V_MIX_IRQ(R)    (__u8)((R & M_MIX_IRQ) >> 2)

	#define M_FR_ABO  0x08  // mask bit 3
	#define SET_V_FR_ABO(R,V)  (R = (__u8)((R & (__u8)(M_FR_ABO ^ 0xFF)) | (__u8)((V & 0x01) << 3)))
	#define GET_V_FR_ABO(R)    (__u8)((R & M_FR_ABO) >> 3)

	#define M_NO_CRC  0x10  // mask bit 4
	#define SET_V_NO_CRC(R,V)  (R = (__u8)((R & (__u8)(M_NO_CRC ^ 0xFF)) | (__u8)((V & 0x01) << 4)))
	#define GET_V_NO_CRC(R)    (__u8)((R & M_NO_CRC) >> 4)

	#define M_NO_REP  0x20  // mask bit 5
	#define SET_V_NO_REP(R,V)  (R = (__u8)((R & (__u8)(M_NO_REP ^ 0xFF)) | (__u8)((V & 0x01) << 5)))
	#define GET_V_NO_REP(R)    (__u8)((R & M_NO_REP) >> 5)


#endif /* _XHFC24SUCD_H_ */

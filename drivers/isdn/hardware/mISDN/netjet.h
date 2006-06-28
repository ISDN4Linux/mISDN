/* 
 *
 * NETjet common header file
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 *              by Matt Henderson and Daniel Potts,
 *                 Traverse Technologies P/L www.traverse.com.au
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Ported to mISDN from HiSax by Daniel Potts
 *  
 */

#define NETJET_CTRL		0x00
#define NETJET_DMACTRL		0x01
#define NETJET_AUXCTRL		0x02
#define NETJET_AUXDATA		0x03
#define NETJET_IRQMASK0 	0x04
#define NETJET_IRQMASK1 	0x05
#define NETJET_IRQSTAT0 	0x06
#define NETJET_IRQSTAT1 	0x07
#define NETJET_DMA_READ_START	0x08
#define NETJET_DMA_READ_IRQ	0x0c
#define NETJET_DMA_READ_END	0x10
#define NETJET_DMA_READ_ADR	0x14
#define NETJET_DMA_WRITE_START	0x18
#define NETJET_DMA_WRITE_IRQ	0x1c
#define NETJET_DMA_WRITE_END	0x20
#define NETJET_DMA_WRITE_ADR	0x24
#define NETJET_PULSE_CNT	0x28

#define NETJET_ISAC_OFF		0xc0
#define NETJET_ISACIRQ		0x10
#define NETJET_IRQM0_READ_MASK	0x0c
#define NETJET_IRQM0_READ_1	0x04
#define NETJET_IRQM0_READ_2	0x08
#define NETJET_IRQM0_WRITE_MASK	0x03
#define NETJET_IRQM0_WRITE_1	0x01
#define NETJET_IRQM0_WRITE_2	0x02

#define NETJET_HA_OFFSET	2
#define NETJET_HA_BITS		4
#define NETJET_HA_MASK		0xf // mask from offset

#define NETJET_DMA_TXSIZE 	512 // dma buf size in 32-bit words
#define NETJET_DMA_RXSIZE 	128 // dma buf size in 32-bit words

#define HDLC_ZERO_SEARCH 	0
#define HDLC_FLAG_SEARCH 	1
#define HDLC_FLAG_FOUND  	2
#define HDLC_FRAME_FOUND 	3
#define HDLC_NULL 		4
#define HDLC_PART 		5
#define HDLC_FULL 		6

#define HDLC_FLAG_VALUE		0x7e

#define FLG_NOFRAME		26
#define FLG_HALF		27
#define FLG_EMPTY		28


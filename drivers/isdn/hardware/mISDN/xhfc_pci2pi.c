/* $Id: xhfc_pci2pi.c,v 1.3 2006/03/06 16:20:33 mbachem Exp $
 *
 * PCI2PI Pci Bridge support for xhfc_su.c
 *
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
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/delay.h>
#include "xhfc_su.h"
#include "xhfc_pci2pi.h"



static PCI2PI_cfg PCI2PI_config = {
	/* default PI_INTELMX config */
	.del_cs = 0,
	.del_rd = 0,
	.del_wr = 0,
	.del_ale = 0,
	.del_adr = 0,
	.del_dout = 0,
	.default_adr = 0x00,
	.default_dout = 0x00,
	.pi_mode = PI_MODE,
	.setup = 1,
	.hold = 1,
	.cycle = 1,
	.ale_adr_first = 0,
	.ale_adr_setup = 0,
	.ale_adr_hold = 1,
	.ale_adr_wait = 0,
	.pause_seq = 1,
	.pause_end = 0,
	.gpio_out = 0,
	.status_int_enable = 1,
	.pi_int_pol = 0,
	.pi_wait_enable = 0,
	.spi_cfg0 = 0,
	.spi_cfg1 = 0,
	.spi_cfg2 = 0,
	.spi_cfg3 = 0,
	.eep_recover = 4,
};

/* base addr to address several XHFCs on one PCI2PI bridge */
__u32 PCI2PI_XHFC_OFFSETS[PCI2PI_MAX_XHFC] = {0, 0x400};


/***********************************/
/* initialise the XHFC PCI Bridge  */
/* return 0 on success.            */
/***********************************/
int
init_pci_bridge(xhfc_pi * pi)
{
	int err = -ENODEV;

	printk(KERN_INFO "%s %s: using PCI2PI Bridge at 0x%p\n",
	       pi->card_name, __FUNCTION__, pi->hw_membase);

	/* test if Bridge regsiter accessable */
	WritePCI2PI_u32(pi, PCI2PI_DEL_CS, 0x0);
	if (ReadPCI2PI_u32(pi, PCI2PI_DEL_CS) == 0x00) {
		WritePCI2PI_u32(pi, PCI2PI_DEL_CS, 0xFFFFFFFF);
		if (ReadPCI2PI_u32(pi, PCI2PI_DEL_CS) == 0xF) {
			err = 0;
		}
	}
	if (err)
		return (err);

	/* enable hardware reset XHFC */
	WritePCI2PI_u32(pi, PCI2PI_GPIO_OUT, GPIO_OUT_VAL);

	WritePCI2PI_u32(pi, PCI2PI_PI_MODE, PCI2PI_config.pi_mode);
	WritePCI2PI_u32(pi, PCI2PI_DEL_CS, PCI2PI_config.del_cs);
	WritePCI2PI_u32(pi, PCI2PI_DEL_RD, PCI2PI_config.del_rd);
	WritePCI2PI_u32(pi, PCI2PI_DEL_WR, PCI2PI_config.del_wr);
	WritePCI2PI_u32(pi, PCI2PI_DEL_ALE, PCI2PI_config.del_ale);
	WritePCI2PI_u32(pi, PCI2PI_DEL_ADR, PCI2PI_config.del_adr);
	WritePCI2PI_u32(pi, PCI2PI_DEL_DOUT, PCI2PI_config.del_dout);
	WritePCI2PI_u32(pi, PCI2PI_DEFAULT_ADR, PCI2PI_config.default_adr);
	WritePCI2PI_u32(pi, PCI2PI_DEFAULT_DOUT,
			PCI2PI_config.default_dout);

	WritePCI2PI_u32(pi, PCI2PI_CYCLE_SHD, 0x80 * PCI2PI_config.setup
			+ 0x40 * PCI2PI_config.hold + PCI2PI_config.cycle);

	WritePCI2PI_u32(pi, PCI2PI_ALE_ADR_WHSF,
			PCI2PI_config.ale_adr_first +
			PCI2PI_config.ale_adr_setup * 2 +
			PCI2PI_config.ale_adr_hold * 4 +
			PCI2PI_config.ale_adr_wait * 8);

	WritePCI2PI_u32(pi, PCI2PI_CYCLE_PAUSE,
			0x10 * PCI2PI_config.pause_seq +
			PCI2PI_config.pause_end);
	WritePCI2PI_u32(pi, PCI2PI_STATUS_INT_ENABLE,
			PCI2PI_config.status_int_enable);

	WritePCI2PI_u32(pi, PCI2PI_PI_INT_POL,
			2 * PCI2PI_config.pi_wait_enable +
			PCI2PI_config.pi_int_pol);

	WritePCI2PI_u32(pi, PCI2PI_SPI_CFG0, PCI2PI_config.spi_cfg0);
	WritePCI2PI_u32(pi, PCI2PI_SPI_CFG1, PCI2PI_config.spi_cfg1);
	WritePCI2PI_u32(pi, PCI2PI_SPI_CFG2, PCI2PI_config.spi_cfg2);
	WritePCI2PI_u32(pi, PCI2PI_SPI_CFG3, PCI2PI_config.spi_cfg3);
	WritePCI2PI_u32(pi, PCI2PI_EEP_RECOVER, PCI2PI_config.eep_recover);
	ReadPCI2PI_u32(pi, PCI2PI_STATUS);


	/* release hardware reset XHFC */
	WritePCI2PI_u32(pi, PCI2PI_GPIO_OUT, GPIO_OUT_VAL | PCI2PI_GPIO7_NRST);
	udelay(10);

	return (err);
}

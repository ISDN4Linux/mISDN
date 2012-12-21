/* xhfc_pci2pi.c
 * PCI2PI Pci Bridge support for xhfc_su.c
 *
 * (C) 2007 Copyright Cologne Chip AG
 * Authors : Joerg Ciesielski, Martin Bachem
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

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include "xhfc_su.h"
#include "xhfc_pci2pi.h"

static int card_cnt = 0;
static LIST_HEAD(card_list);
static DEFINE_RWLOCK(card_lock);


static struct pci_device_id xhfc_ids[] = {
	{
	 .vendor = PCI_VENDOR_ID_CCD,
	 .device = 0xA003,
	 .subvendor = 0x1397,
	 .subdevice = 0xA003,
	 /* CCAG XHFC Eval Board with using 1 XHFC */
	 .driver_data = (unsigned long)
	 &((struct pi_params) {
			       "XHFC Evaluation Board",
			       1})
	 },
	{}
};

static struct pci_driver xhfc_driver = {
      name:DRIVER_NAME,
      probe:xhfc_pci_probe,
      remove:xhfc_pci_remove,
      id_table:xhfc_ids,
};

/*
 * PCI hotplug interface: probe new card
 *  every PCI card will alloc mem for one 'struct xhfc_pi'
 *  and alloc 'struct xhfc' at pi->xhfc for every XHFC expected
 *  on that PCI card
 */
int
xhfc_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct pi_params *driver_data =
	    (struct pi_params *) ent->driver_data;
	struct xhfc_pi *pi = NULL;
	__u8 i;
	u_long flags;

	int err = -ENOMEM;

	/* alloc mem for ProcessorInterface xhfc_pi */
	if (!(pi = kzalloc(sizeof(struct xhfc_pi), GFP_KERNEL))) {
		printk(KERN_ERR "%s: No kmem for XHFC card\n",
		       __FUNCTION__);
		goto out;
	}

	pi->cardnum = card_cnt;

	sprintf(pi->name, "%s_PI%d", DRIVER_NAME, pi->cardnum);
	printk(KERN_INFO
	       "%s %s: adapter '%s' found on PCI bus %02x dev %02x"
	       ", using %i XHFC controllers\n", pi->name, __FUNCTION__,
	       driver_data->device_name, pdev->bus->number, pdev->devfn,
	       driver_data->num_xhfcs);

	/* alloc mem for all XHFCs (xhfc_t) */
	if (!
	    (pi->xhfc =
	     kzalloc(sizeof(struct xhfc) * driver_data->num_xhfcs,
		     GFP_KERNEL))) {
		printk(KERN_ERR "%s %s: No kmem for sizeof(xhfc_t)*%i \n",
		       pi->name, __FUNCTION__, driver_data->num_xhfcs);
		goto out;
	}

	pi->pdev = pdev;
	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR "%s %s: error with pci_enable_device\n",
		       pi->name, __FUNCTION__);
		goto out;
	}

	if (driver_data->num_xhfcs > PCI2PI_MAX_XHFC) {
		printk(KERN_ERR
		       "%s %s: max no of addressable XHFCs reached\n",
		       pi->name, __FUNCTION__);
		goto out;
	}

	pi->driver_data = *driver_data;
	pi->irq = pdev->irq;
	pi->hw_membase = (u_char *) pci_resource_start(pdev, 1);
	pi->membase = ioremap((ulong) pi->hw_membase, 4096);
	pci_set_drvdata(pdev, pi);

	err = init_pci_bridge(pi);
	if (err) {
		printk(KERN_ERR "%s %s: init_pci_bridge failed!\n",
		       pi->name, __FUNCTION__);
		goto out;
	}

	/* init interrupt engine */
	if (request_irq(pi->irq, xhfc_interrupt, IRQF_SHARED, "XHFC", pi)) {
		printk(KERN_WARNING "%s %s: couldn't get interrupt %d\n",
		       pi->name, __FUNCTION__, pi->irq);
		pi->irq = 0;
		err = -EIO;
		goto out;
	}

	err = 0;
	pi->num_xhfcs = pi->driver_data.num_xhfcs;
	for (i = 0; i < pi->num_xhfcs; i++) {
		pi->xhfc[i].pi = pi;
		pi->xhfc[i].chipidx = i;
		err |= setup_instance(&pi->xhfc[i], pdev->dev.parent);
	}

	if (!err) {
		card_cnt++;
		write_lock_irqsave(&card_lock, flags);
		list_add_tail(&pi->list, &card_list);
		write_unlock_irqrestore(&card_lock, flags);
		return (0);
	} else {
		goto out;
	}

      out:
	if (pi->xhfc)
		kfree(pi->xhfc);
	if (pi)
		kfree(pi);
	return (err);
}

/*
 * PCI hotplug interface: remove card
 */
void
xhfc_pci_remove(struct pci_dev *pdev)
{
	int i;

	struct xhfc_pi *pi = pci_get_drvdata(pdev);
	printk(KERN_INFO "%s %s: removing card\n", pi->name, __FUNCTION__);

	for (i = 0; i < pi->num_xhfcs; i++)
		disable_interrupts(&pi->xhfc[i]);
	free_irq(pi->irq, pi);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(HZ / 10);
	for (i = 0; i < pi->driver_data.num_xhfcs; i++)
		release_instance(&pi->xhfc[i]);
	kfree(pi);
	card_cnt--;
	pci_disable_device(pdev);
	return;
}

int
xhfc_register_pi(void)
{
	return pci_register_driver(&xhfc_driver);
}

int
xhfc_unregister_pi(void)
{
	pci_unregister_driver(&xhfc_driver);
	return 0;
}


/******************************************************************************/


static struct PCI2PI_cfg PCI2PI_config = {
	0,			// .del_cs
	0,			// .del_rd
	0,			// .del_wr
	0,			// .del_ale
	0,			// .del_adr
	0,			// .del_dout
	0x00,			// .default_adr
	0x00,			// .default_dout
	PI_MODE,		// .pi_mode
	1,			// .setup
	1,			// .hold
	1,			// .cycle
	0,			// .ale_adr_first
	0,			// .ale_adr_setup
	1,			// .ale_adr_hold
	0,			// .ale_adr_wait
	1,			// .pause_seq
	0,			// .pause_end
	0,			// .gpio_out
	1,			// .status_int_enable
	0,			// .pi_int_pol
	0,			// .pi_wait_enable
	0,			// .spi_cfg0
	2,			// .spi_cfg1
	0,			// .spi_cfg2
	0,			// .spi_cfg3
	4,			// .eep_recover
};

/* base addr to address several XHFCs on one PCI2PI bridge */
__u32 PCI2PI_XHFC_OFFSETS[PCI2PI_MAX_XHFC] = { 0, 0x400 };


/* read and write functions to access registers of the PCI bridge */

static inline __u8
ReadPCI2PI_u8(struct xhfc_pi *pi, __u16 reg_addr)
{
	return (*(volatile __u8 *) (pi->membase + reg_addr));
}

static inline __u16
ReadPCI2PI_u16(struct xhfc_pi *pi, __u16 reg_addr)
{
	return (*(volatile __u16 *) (pi->membase + reg_addr));
}

static inline __u32
ReadPCI2PI_u32(struct xhfc_pi *pi, __u16 reg_addr)
{
	return (*(volatile __u32 *) (pi->membase + reg_addr));
}

static inline void
WritePCI2PI_u8(struct xhfc_pi *pi, __u16 reg_addr, __u8 value)
{
	*((volatile __u8 *) (pi->membase + reg_addr)) = value;
}

static inline void
WritePCI2PI_u16(struct xhfc_pi *pi, __u16 reg_addr, __u16 value)
{
	*((volatile __u16 *) (pi->membase + reg_addr)) = value;
}

static inline void
WritePCI2PI_u32(struct xhfc_pi *pi, __u16 reg_addr, __u32 value)
{
	*((volatile __u32 *) (pi->membase + reg_addr)) = value;
}

/*
 * initialise the XHFC PCI Bridge
 * return 0 on success.
 */
int
init_pci_bridge(struct xhfc_pi *pi)
{
	int err = -ENODEV;

	printk(KERN_INFO
	       "%s %s: using PCI2PI Bridge at 0x%p, PI-Mode(0x%x)\n",
	       pi->name, __FUNCTION__, pi->hw_membase,
	       PCI2PI_config.pi_mode);

	spin_lock_init(&pi->lock);

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
	WritePCI2PI_u32(pi, PCI2PI_GPIO_OUT,
			GPIO_OUT_VAL | PCI2PI_GPIO7_NRST);
	udelay(10);

	return (err);
}


/*
 * read and write functions to access a XHFC at the local bus interface
 * of the PCI bridge there are two sets of functions to access the XHFC
 * in the following different interface modes:
 *
 * multiplexed bus interface modes PI_INTELMX and PI_MOTMX
 *  - these modes use a single (atomic) PCI cycle to read or write
 *    a XHFC register
 *  - non multiplexed bus interface modes PI_INTELNOMX, PI_MOTMX
 *    and PI_SPI
 *
 * these modes use a separate PCI cycles to select the XHFC register and to read
 * or write data. That means these register accesses are non atomic and could be
 * interrupted by an interrupt. The driver must take care that a register access
 * in these modes is not interrupted by its own interrupt handler.
 */


/*****************************************************************************/

#if ((PI_MODE==PI_INTELMX) || (PI_MODE==PI_MOTMX))

/* functions for multiplexed access */
inline __u8
read_xhfc(struct xhfc * xhfc, __u8 reg_addr)
{
	return (*(volatile __u8 *) (xhfc->pi->membase +
				    PCI2PI_XHFC_OFFSETS[xhfc->chipidx] +
				    (reg_addr << 2)));
}

/*
 * read four bytes from the same register address
 * e.g. A_FIFO_DATA
 * this function is only defined for software compatibility here
 */
inline __u32
read32_xhfc(struct xhfc *xhfc, __u8 reg_addr)
{
	__u32 value;

	value = (*(volatile __u8 *) (xhfc->pi->membase +
				     PCI2PI_XHFC_OFFSETS[xhfc->chipidx] +
				     (reg_addr << 2)));
	value |= (*(volatile __u8 *) (xhfc->pi->membase +
				      PCI2PI_XHFC_OFFSETS[xhfc->chipidx] +
				      (reg_addr << 2))) << 8;
	value |= (*(volatile __u8 *) (xhfc->pi->membase +
				      PCI2PI_XHFC_OFFSETS[xhfc->chipidx] +
				      (reg_addr << 2))) << 16;
	value |= (*(volatile __u8 *) (xhfc->pi->membase +
				      PCI2PI_XHFC_OFFSETS[xhfc->chipidx] +
				      (reg_addr << 2))) << 24;

	return (value);
}

inline void
write_xhfc(struct xhfc *xhfc, __u8 reg_addr, __u8 value)
{
	*((volatile __u8 *) (xhfc->pi->membase +
			     PCI2PI_XHFC_OFFSETS[xhfc->chipidx] +
			     (reg_addr << 2))) = value;
}

/*
 * writes four bytes to the same register address
 * e.g. A_FIFO_DATA
 * this function is only defined for software compatibility here
 */
inline void
write32_xhfc(struct xhfc *xhfc, __u8 reg_addr, __u32 value)
{
	*((volatile __u8 *) (xhfc->pi->membase +
			     PCI2PI_XHFC_OFFSETS[xhfc->chipidx] +
			     (reg_addr << 2))) = value & 0xff;
	*((volatile __u8 *) (xhfc->pi->membase +
			     PCI2PI_XHFC_OFFSETS[xhfc->chipidx] +
			     (reg_addr << 2))) = (value >> 8) & 0xff;
	*((volatile __u8 *) (xhfc->pi->membase +
			     PCI2PI_XHFC_OFFSETS[xhfc->chipidx] +
			     (reg_addr << 2))) = (value >> 16) & 0xff;
	*((volatile __u8 *) (xhfc->pi->membase +
			     PCI2PI_XHFC_OFFSETS[xhfc->chipidx] +
			     (reg_addr << 2))) = (value >> 24) & 0xff;
}

/*
 * always reads a single byte with short read method
 * this allows to read ram based registers
 * that normally requires long read access times
 */
inline __u8
sread_xhfc(struct xhfc *xhfc, __u8 reg_addr)
{
	(*(volatile __u8 *) (xhfc->pi->membase +
			     PCI2PI_XHFC_OFFSETS[xhfc->chipidx] +
			     (reg_addr << 2)));
	return (*(volatile __u8 *)
		(xhfc->pi->membase + PCI2PI_XHFC_OFFSETS[xhfc->chipidx] +
		 (R_INT_DATA << 2)));
}

/*
 * this function reads the currently selected regsiter from XHFC and is only
 * required for non multiplexed access modes. For multiplexed access modes this
 * function is only defined for for software compatibility.
 */
inline __u8
read_xhfcregptr(struct xhfc *xhfc)
{
	return 0;
}

/*
 * this function writes the XHFC register address pointer and is only
 * required for non multiplexed access modes. For multiplexed access modes this
 * function is only defined for for software compatibility. */
inline void
write_xhfcregptr(struct xhfc *xhfc, __u8 reg_addr)
{
}

#endif /* PI_MODE==PI_INTELMX || PI_MODE==PI_MOTMX */

/*****************************************************************************/

#if PI_MODE==PI_INTELNOMX || PI_MODE==PI_MOT
/*
 * functions for non multiplexed access:
 * XHFC register address pointer is accessed with PCI address A2=1 and XHFC data
 * port is accessed with PCI address A2=0
 */

inline __u8
read_xhfc(struct xhfc *xhfc, __u8 reg_addr)
{
	u_long flags;
	__u8 data;

	spin_lock_irqsave(&xhfc->pi->lock, flags);
	*((volatile __u8 *) (xhfc->pi->membase +
			     PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + 4)) =
	    reg_addr;
	data =
	    *(volatile __u8 *) (xhfc->pi->membase +
				PCI2PI_XHFC_OFFSETS[xhfc->chipidx]);
	spin_unlock_irqrestore(&xhfc->pi->lock, flags);
	return (data);
}

/*
 * read four bytes from the same register address by using a 32bit PCI access.
 * The PCI bridge generates for 8 bit data read cycles at the local bus
 * interface.
 */
inline __u32
read32_xhfc(struct xhfc * xhfc, __u8 reg_addr)
{
	u_long flags;
	__u32 data;
	spin_lock_irqsave(&xhfc->pi->lock, flags);
	*((volatile __u8 *) (xhfc->pi->membase +
			     PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + 4)) =
	    reg_addr;
	data =
	    *(volatile __u32 *) xhfc->pi->membase +
	        PCI2PI_XHFC_OFFSETS[xhfc->chipidx];
	    spin_unlock_irqrestore(&xhfc->pi->lock, flags);
	return (data);
}

inline void
write_xhfc(struct xhfc *xhfc, __u8 reg_addr, __u8 value)
{
	u_long flags;
	spin_lock_irqsave(&xhfc->pi->lock, flags);
	*((volatile __u8 *) (xhfc->pi->membase +
			     PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + 4)) =
	    reg_addr;
	*((volatile __u8 *) (xhfc->pi->membase +
			     PCI2PI_XHFC_OFFSETS[xhfc->chipidx])) = value;
	spin_unlock_irqrestore(&xhfc->pi->lock, flags);
}

/*
 * writes four bytes to the same register address (e.g. A_FIFO_DATA) by using a
 * 32bit PCI access. The PCI bridge generates for 8 bit data write cycles at the
 * local bus interface.
 */
inline void
write32_xhfc(struct xhfc *xhfc, __u8 reg_addr, __u32 value)
{
	u_long flags;
	spin_lock_irqsave(&xhfc->pi->lock, flags);
	*((volatile __u8 *) (xhfc->pi->membase +
			     PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + 4)) =
	    reg_addr;
	*((volatile __u32 *) (xhfc->pi->membase +
			      PCI2PI_XHFC_OFFSETS[xhfc->chipidx])) = value;
	spin_unlock_irqrestore(&xhfc->pi->lock, flags);
}

/*
 * reads a single byte with short read method (r*). This allows to read ram
 * based registers that normally requires long read access times
 */
inline __u8
sread_xhfc(struct xhfc *xhfc, __u8 reg_addr)
{
	u_long flags;
	__u8 data;

	spin_lock_irqsave(&xhfc->pi->lock, flags);
	*((volatile __u8 *) (xhfc->pi->membase +
			     PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + 4)) =
	    reg_addr;

	/* dummy read to get R_INT_DATA */
	(*(volatile __u8 *) (xhfc->pi->membase +
			     PCI2PI_XHFC_OFFSETS[xhfc->chipidx]));

	*((volatile __u8 *) (xhfc->pi->membase +
			     PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + 4)) =
	    R_INT_DATA;
	data =
	    *(volatile __u8 *) (xhfc->pi->membase +
				PCI2PI_XHFC_OFFSETS[xhfc->chipidx]);
	    spin_unlock_irqrestore(&xhfc->pi->lock, flags);
	return (data);
}

/*
 * this function reads the currently selected regsiter from XHFC
 */
inline __u8
read_xhfcregptr(struct xhfc * xhfc)
{
	return (*(volatile __u8 *) (xhfc->pi->membase +
				    PCI2PI_XHFC_OFFSETS[xhfc->chipidx] +
				    4));
}

/*
 * this function writes the XHFC register address pointer
 */
inline void
write_xhfcregptr(struct xhfc *xhfc, __u8 reg_addr)
{
	*((volatile __u8 *) (xhfc->pi->membase +
			     PCI2PI_XHFC_OFFSETS[xhfc->chipidx] + 4)) =
	    reg_addr;
}

#endif /* PI_MODE==PI_INTELNOMX || PI_MODE==PI_MOT */


/*****************************************************************************/

#if PI_MODE == PI_SPI

/* SPI mode transaction bit definitions */
#define SPI_ADDR	0x40
#define SPI_DATA	0x00
#define SPI_RD		0x80
#define SPI_WR		0x00
#define SPI_BROAD	0x20
#define SPI_MULTI	0x20

inline __u8
read_xhfc(struct xhfc *xhfc, __u8 reg_addr)
{
	u_long flags;
	__u8 data;

	spin_lock_irqsave(&xhfc->pi->lock, flags);

	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// initiate a 32 clock SPI master transfer
	WritePCI2PI_u32(xhfc->pi, PCI2PI_SPI_MO_DATA,
			((SPI_ADDR | SPI_WR | xhfc->chipidx) << 24) |
			(reg_addr << 16) | ((SPI_DATA | SPI_RD) << 8));
	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));

	// read data from the SPI data receive register and return one byte
	data = ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_MI_DATA) & 0xFF;

	spin_unlock_irqrestore(&xhfc->pi->lock, flags);
	return data;
}

/*
 * read four bytes from the same register address by using a
 * SPI multiple read access
 */
inline __u32
read32_xhfc(struct xhfc * xhfc, __u8 reg_addr)
{
	u_long flags;
	__u32 data;

	spin_lock_irqsave(&xhfc->pi->lock, flags);

	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// initiate a 16 clock SPI master transfer
	WritePCI2PI_u16(xhfc->pi, PCI2PI_SPI_MO_DATA,
			((SPI_ADDR | SPI_WR | xhfc->
			  chipidx) << 8) | reg_addr);
	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// initiate a 8 clock SPI master transfer
	WritePCI2PI_u8(xhfc->pi, PCI2PI_SPI_MO_DATA,
		       (SPI_DATA | SPI_RD | SPI_MULTI));
	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// initiate a 32 clock SPI master transfer
	// output data is arbitrary
	WritePCI2PI_u32(xhfc->pi, PCI2PI_SPI_MO_DATA, 0);
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));

	// read data from the SPI data receive register and return four bytes
	data = be32_to_cpu(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_MI_DATA));

	spin_unlock_irqrestore(&xhfc->pi->lock, flags);
	return data;
}

inline void
write_xhfc(struct xhfc *xhfc, __u8 reg_addr, __u8 value)
{
	u_long flags;
	spin_lock_irqsave(&xhfc->pi->lock, flags);

	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// initiate a 32 clock SPI master transfer
	WritePCI2PI_u32(xhfc->pi, PCI2PI_SPI_MO_DATA,
			((SPI_ADDR | SPI_WR | xhfc->chipidx) << 24) |
			(reg_addr << 16) |
			((SPI_DATA | SPI_WR) << 8) | value);

	spin_unlock_irqrestore(&xhfc->pi->lock, flags);
}

/*
 * writes four bytes to the same register address (e.g. A_FIFO_DATA) by using a
 * SPI multiple write access
 */
inline void
write32_xhfc(struct xhfc *xhfc, __u8 reg_addr, __u32 value)
{
	u_long flags;
	spin_lock_irqsave(&xhfc->pi->lock, flags);

	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// initiate a 16 clock SPI master transfer
	WritePCI2PI_u16(xhfc->pi, PCI2PI_SPI_MO_DATA,
			((SPI_ADDR | SPI_WR | xhfc->
			  chipidx) << 8) | reg_addr);
	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// initiate a 8 clock SPI master transfer
	WritePCI2PI_u8(xhfc->pi, PCI2PI_SPI_MO_DATA,
		       (SPI_DATA | SPI_WR | SPI_MULTI));
	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// initiate a 32 clock SPI master transfer
	WritePCI2PI_u32(xhfc->pi, PCI2PI_SPI_MO_DATA, cpu_to_be32(value));

	spin_unlock_irqrestore(&xhfc->pi->lock, flags);
}

/*
 * reads a single byte with short read method (r*). This allows to read ram
 * based registers that normally requires long read access times
 */
inline __u8
sread_xhfc(struct xhfc *xhfc, __u8 reg_addr)
{
	u_long flags;
	__u8 data;

	spin_lock_irqsave(&xhfc->pi->lock, flags);

	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// initiate a 32 clock SPI master transfer
	WritePCI2PI_u32(xhfc->pi, PCI2PI_SPI_MO_DATA,
			((SPI_ADDR | SPI_WR | xhfc->chipidx) << 24) |
			(reg_addr << 16) | ((SPI_DATA | SPI_RD) << 8));

	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// initiate a 32 clock SPI master transfer to read R_INT_DATA register
	WritePCI2PI_u32(xhfc->pi, PCI2PI_SPI_MO_DATA,
			((SPI_ADDR | SPI_WR | xhfc->chipidx) << 24) |
			(R_INT_DATA << 16) | ((SPI_DATA | SPI_RD) << 8));

	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));

	// read data from the SPI data receive register and return one byte
	data = ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_MI_DATA) & 0xFF;

	spin_unlock_irqrestore(&xhfc->pi->lock, flags);
	return data;
}

/*
 * this function reads the currently selected regsiter from XHFC
 */
inline __u8
read_xhfcregptr(struct xhfc * xhfc)
{
	u_long flags;
	__u8 data;

	spin_lock_irqsave(&xhfc->pi->lock, flags);

	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// initiate a 16 clock SPI master transfer
	WritePCI2PI_u16(xhfc->pi, PCI2PI_SPI_MO_DATA,
			((SPI_ADDR | SPI_RD | xhfc->chipidx) << 8));
	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));

	// read data from the SPI data receive register and return one byte
	data = ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_MI_DATA) & 0xFF;

	spin_unlock_irqrestore(&xhfc->pi->lock, flags);
	return data;
}

/*
 * this function writes the XHFC register address pointer
 */
inline void
write_xhfcregptr(struct xhfc *xhfc, __u8 reg_addr)
{
	u_long flags;

	spin_lock_irqsave(&xhfc->pi->lock, flags);

	// wait until SPI master is idle
	while (!(ReadPCI2PI_u32(xhfc->pi, PCI2PI_SPI_STATUS) & 1));
	// initiate a 16 clock SPI master transfer
	WritePCI2PI_u16(xhfc->pi, PCI2PI_SPI_MO_DATA,
			((SPI_ADDR | SPI_WR | xhfc->
			  chipidx) << 8) | reg_addr);

	spin_unlock_irqrestore(&xhfc->pi->lock, flags);
}

#endif /* PI_MODE == PI_SPI */

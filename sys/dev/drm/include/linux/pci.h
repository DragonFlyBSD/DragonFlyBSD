/*
 * Copyright (c) 2014-2020 François Tigeot <ftigeot@wolfpond.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef LINUX_PCI_H
#define LINUX_PCI_H

#include <linux/mod_devicetable.h>

#include <linux/types.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/list.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/kobject.h>
#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/io.h>
#include <uapi/linux/pci.h>

#include <linux/pci_ids.h>

#include <sys/pciio.h>
#include <sys/rman.h>
#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>

#define PCI_ANY_ID	(~0u)

struct pci_bus;
struct device_node;

struct pci_device_id {
	uint32_t vendor;
	uint32_t device;
	uint32_t subvendor;
	uint32_t subdevice;
	uint32_t class;
	uint32_t class_mask;
	unsigned long driver_data;
};

typedef unsigned short pci_dev_flags_t;

#define PCI_DEV_FLAGS_NEEDS_RESUME	(1 << 11)

struct pci_dev {
	struct pci_bus *bus;		/* bus device is nailed to */
	struct device dev;

	uint32_t devfn;
	uint16_t vendor;		/* vendor ID */
	uint16_t device;		/* device ID */
	uint16_t subsystem_vendor;
	uint16_t subsystem_device;

	uint8_t revision;		/* revision ID */

	unsigned int irq;		/* handle with care */
	void *pci_dev_data;

	unsigned int	no_64bit_msi:1;
	pci_dev_flags_t dev_flags;

	/* DragonFly-specific data */
	int		_irq_type;
	struct resource	*_irqr;
	int		_irqrid;
};

struct pci_bus {
	struct pci_dev *self;		/* handle to pdev self */
	struct device *dev;		/* handle to dev */

	unsigned char number;		/* bus addr number */
};

struct pci_driver {
	const char *name;
	const struct pci_device_id *id_table;
	int (*probe)(struct pci_dev *dev, const struct pci_device_id *id);
	void (*remove)(struct pci_dev *dev);
};

#define PCI_DMA_BIDIRECTIONAL	0

/* extracted from radeon/si.c radeon/cik.c */
#define PCI_EXP_LNKCTL PCIER_LINKCTRL /* 16 */
#define PCI_EXP_LNKCTL2 48
#define PCI_EXP_LNKCTL_HAWD PCIEM_LNKCTL_HAWD /* 0x0200 */
#define PCI_EXP_DEVSTA PCIER_DEVSTS /* 10 */
#define PCI_EXP_DEVSTA_TRPND 0x0020
#define PCI_EXP_LNKCAP_CLKPM 0x00040000

static inline int
pci_read_config_byte(struct pci_dev *pdev, int where, u8 *val)
{
	*val = (u8)pci_read_config(pdev->dev.bsddev, where, 1);
	return 0;
}

static inline int
pci_read_config_word(struct pci_dev *pdev, int where, u16 *val)
{
	*val = (u16)pci_read_config(pdev->dev.bsddev, where, 2);
	return 0;
}

static inline int
pci_read_config_dword(struct pci_dev *pdev, int where, u32 *val)
{
	*val = (u32)pci_read_config(pdev->dev.bsddev, where, 4);
	return 0;
}

static inline int
pci_write_config_byte(struct pci_dev *pdev, int where, u8 val)
{
	pci_write_config(pdev->dev.bsddev, where, val, 1);
	return 0;
}

static inline int
pci_write_config_word(struct pci_dev *pdev, int where, u16 val)
{
	pci_write_config(pdev->dev.bsddev, where, val, 2);
	return 0;
}

static inline int
pci_write_config_dword(struct pci_dev *pdev, int where, u32 val)
{
	pci_write_config(pdev->dev.bsddev, where, val, 4);
	return 0;
}

/* extracted from drm/radeon/evergreen.c */
static inline int
pcie_get_readrq(struct pci_dev *pdev)
{
	u16 ctl;
	int err, cap;

	err = pci_find_extcap(pdev->dev.bsddev, PCIY_EXPRESS, &cap);

	cap += PCIER_DEVCTRL;

	ctl = pci_read_config(pdev->dev.bsddev, cap, 2);

	return 128 << ((ctl & PCIEM_DEVCTL_MAX_READRQ_MASK) >> 12);
}

/* valid rq sizes: 128, 256, 512, 1024, 2048, 4096 (^2N) */
static inline int
pcie_set_readrq(struct pci_dev *pdev, int rq)
{
	u16 ctl;
	int err, cap;

	if (rq < 128 || rq > 4096 || !is_power_of_2(rq))
		return -EINVAL;

	err = pci_find_extcap(pdev->dev.bsddev, PCIY_EXPRESS, &cap);
	if (err)
		return (-1);

	cap += PCIER_DEVCTRL;

	ctl = pci_read_config(pdev->dev.bsddev, cap, 2);
	ctl &= ~PCIEM_DEVCTL_MAX_READRQ_MASK;
	ctl |= ((ffs(rq) - 8) << 12);
	pci_write_config(pdev->dev.bsddev, cap, ctl, 2);
	return 0;
}

static inline struct pci_dev *
pci_dev_get(struct pci_dev *dev)
{
	/* Linux increments a reference count here */
	return dev;
}

static inline struct pci_dev *
pci_dev_put(struct pci_dev *dev)
{
	/* Linux decrements a reference count here */
	return dev;
}


static inline int
pci_set_dma_mask(struct pci_dev *dev, u64 mask)
{
	return -EIO;
}

static inline int
pci_set_consistent_dma_mask(struct pci_dev *dev, u64 mask)
{
	return -EIO;
}

typedef int pci_power_t;

#define PCI_D0		0
#define PCI_D1		1
#define PCI_D2		2
#define PCI_D3hot	3
#define PCI_D3cold	4

#include <asm/pci.h>

static inline struct resource_list_entry*
_pci_get_rle(struct pci_dev *pdev, int bar)
{
	struct pci_devinfo *dinfo;
	device_t dev = pdev->dev.bsddev;
	struct resource_list_entry *rle;

	dinfo = device_get_ivars(dev);

	/* Some child devices don't have registered resources, they
	 * are only present in the parent */
	if (dinfo == NULL)
		dev = device_get_parent(dev);
	dinfo = device_get_ivars(dev);
	if (dinfo == NULL)
		return NULL;

	rle = resource_list_find(&dinfo->resources, SYS_RES_MEMORY, PCIR_BAR(bar));
	if (rle == NULL) {
		rle = resource_list_find(&dinfo->resources,
					 SYS_RES_IOPORT, PCIR_BAR(bar));
	}

	return rle;
}

/*
 * Returns the first address (memory address or I/O port number)
 * associated with one of the PCI I/O regions.The region is selected by
 * the integer bar (the base address register), ranging from 0–5 (inclusive).
 * The return value can be used by ioremap()
 */
static inline phys_addr_t
pci_resource_start(struct pci_dev *pdev, int bar)
{
	struct resource *res;
	int rid;

	rid = PCIR_BAR(bar);
	res = bus_alloc_resource_any(pdev->dev.bsddev, SYS_RES_MEMORY, &rid, RF_SHAREABLE);
	if (res == NULL) {
		kprintf("pci_resource_start(0x%p, 0x%x) failed\n", pdev, PCIR_BAR(bar));
		return -1;
	}

	return rman_get_start(res);
}

static inline phys_addr_t
pci_resource_len(struct pci_dev *pdev, int bar)
{
	struct resource_list_entry *rle;

	rle = _pci_get_rle(pdev, bar);
	if (rle == NULL)
		return -1;

	return  rman_get_size(rle->res);
}

static inline void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen)
{
	resource_size_t base, size;

	base = pci_resource_start(dev, bar);
	size = pci_resource_len(dev, bar);

	if (base == 0)
		return NULL;

	if (maxlen && size > maxlen)
		size = maxlen;

	return ioremap(base, size);
}

static inline int
pci_bus_read_config_byte(struct pci_bus *bus, unsigned int devfn, int where, u8 *val)
{
	const struct pci_dev *pdev = container_of(&bus, struct pci_dev, bus);

	*val = (u8)pci_read_config(pdev->dev.bsddev, where, 1);
	return 0;
}

static inline int
pci_bus_read_config_word(struct pci_bus *bus, unsigned int devfn, int where, u16 *val)
{
	const struct pci_dev *pdev = container_of(&bus, struct pci_dev, bus);

	*val = (u16)pci_read_config(pdev->dev.bsddev, where, 2);
	return 0;
}

static inline const char *
pci_name(struct pci_dev *pdev)
{
	return device_get_desc(pdev->dev.bsddev);
}

static inline void *
pci_get_drvdata(struct pci_dev *pdev)
{
	return pdev->pci_dev_data;
}

static inline void
pci_set_drvdata(struct pci_dev *pdev, void *data)
{
	pdev->pci_dev_data = data;
}

static inline int
pci_register_driver(struct pci_driver *drv)
{
	/* pci_register_driver not implemented */
	return 0;
}

static inline void
pci_unregister_driver(struct pci_driver *dev)
{
	/* pci_unregister_driver not implemented */
}

static inline void
pci_clear_master(struct pci_dev *pdev)
{
	pci_disable_busmaster(pdev->dev.bsddev);
}

static inline void
pci_set_master(struct pci_dev *pdev)
{
	pci_enable_busmaster(pdev->dev.bsddev);
}

static inline int
pci_pcie_cap(struct pci_dev *pdev)
{
	return pci_get_pciecap_ptr(pdev->dev.bsddev);
}

/* DRM_MAX_PCI_RESOURCE */
#define DEVICE_COUNT_RESOURCE	6

#include <uapi/linux/pci_regs.h>

/* From FreeBSD */
static inline bool pcie_cap_has_devctl(const struct pci_dev *dev)
{
		return true;
}

static inline int
pci_find_capability(struct pci_dev *pdev, int capid)
{
	int reg;

	if (pci_find_extcap(pdev->dev.bsddev, capid, &reg))
		return (0);
	return (reg);
}

static inline u16 pcie_flags_reg(struct pci_dev *dev)
{
	int pos;
	u16 reg16;

	pos = pci_find_capability(dev, PCI_CAP_ID_EXP);
	if (!pos)
		return 0;

	pci_read_config_word(dev, pos + PCI_EXP_FLAGS, &reg16);

	return reg16;
}

static inline int pci_pcie_type(struct pci_dev *dev)
{
	return (pcie_flags_reg(dev) & PCI_EXP_FLAGS_TYPE) >> 4;
}


static inline int pcie_cap_version(struct pci_dev *dev)
{
	return pcie_flags_reg(dev) & PCI_EXP_FLAGS_VERS;
}

static inline bool pcie_cap_has_lnkctl(struct pci_dev *dev)
{
	int type = pci_pcie_type(dev);

	return pcie_cap_version(dev) > 1 ||
	       type == PCI_EXP_TYPE_ROOT_PORT ||
	       type == PCI_EXP_TYPE_ENDPOINT ||
	       type == PCI_EXP_TYPE_LEG_END;
}

static inline bool pcie_cap_has_sltctl(struct pci_dev *dev)
{
	int type = pci_pcie_type(dev);

	return pcie_cap_version(dev) > 1 || type == PCI_EXP_TYPE_ROOT_PORT ||
	    (type == PCI_EXP_TYPE_DOWNSTREAM &&
	    pcie_flags_reg(dev) & PCI_EXP_FLAGS_SLOT);
}

static inline bool pcie_cap_has_rtctl(struct pci_dev *dev)
{
	int type = pci_pcie_type(dev);

	return pcie_cap_version(dev) > 1 || type == PCI_EXP_TYPE_ROOT_PORT ||
	    type == PCI_EXP_TYPE_RC_EC;
}

static inline bool
pcie_capability_reg_implemented(struct pci_dev *dev, int pos)
{
	if (!pci_is_pcie(dev->dev.bsddev))
		return false;

	switch (pos) {
	case PCI_EXP_FLAGS_TYPE:
		return true;
	case PCI_EXP_DEVCAP:
	case PCI_EXP_DEVCTL:
	case PCI_EXP_DEVSTA:
		return pcie_cap_has_devctl(dev);
	case PCI_EXP_LNKCAP:
	case PCI_EXP_LNKCTL:
	case PCI_EXP_LNKSTA:
		return pcie_cap_has_lnkctl(dev);
	case PCI_EXP_SLTCAP:
	case PCI_EXP_SLTCTL:
	case PCI_EXP_SLTSTA:
		return pcie_cap_has_sltctl(dev);
	case PCI_EXP_RTCTL:
	case PCI_EXP_RTCAP:
	case PCI_EXP_RTSTA:
		return pcie_cap_has_rtctl(dev);
	case PCI_EXP_DEVCAP2:
	case PCI_EXP_DEVCTL2:
	case PCI_EXP_LNKCAP2:
	case PCI_EXP_LNKCTL2:
	case PCI_EXP_LNKSTA2:
		return pcie_cap_version(dev) > 1;
	default:
		return false;
	}
}

static inline void __iomem __must_check *
pci_map_rom(struct pci_dev *pdev, size_t *size)
{
	return vga_pci_map_bios(device_get_parent(pdev->dev.bsddev), size);
}

static inline void
pci_unmap_rom(struct pci_dev *pdev, void __iomem *rom)
{
	vga_pci_unmap_bios(device_get_parent(pdev->dev.bsddev), rom);
}

static inline int
pci_resource_flags(struct pci_dev *pdev, int bar)
{
	/* Hardcoded to return only the type */
	if ((bar & PCIM_BAR_SPACE) == PCIM_BAR_IO_SPACE) {
		kprintf("pci_resource_flags: pdev=%p bar=%d type=IO\n", pdev, bar);
		return IORESOURCE_IO;
	} else {
		kprintf("pci_resource_flags: pdev=%p bar=%d type=MEM\n", pdev, bar);
		return IORESOURCE_MEM;
	}
}

#include <linux/pci-dma-compat.h>

#endif /* LINUX_PCI_H */

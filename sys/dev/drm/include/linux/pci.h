/*
 * Copyright (c) 2014-2015 François Tigeot
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

#define PCI_ANY_ID	(~0u)

#include <sys/bus.h>
#include <bus/pci/pcivar.h>

#include <linux/types.h>
#include <linux/device.h>
#include <linux/io.h>

#include <linux/pci_ids.h>

struct pci_device_id {
	uint32_t class;
	uint32_t class_mask;
	uint32_t vendor;
	uint32_t device;
	uint32_t subvendor;
	uint32_t subdevice;
	unsigned long driver_data;
};

struct pci_dev {
	struct device	*dev;
	unsigned short device;
};

#define PCI_DEVFN(slot, func)   ((((slot) & 0x1f) << 3) | ((func) & 0x07))

#define PCI_DMA_BIDIRECTIONAL	0

static inline int
pci_read_config_byte(struct pci_dev *pdev, int where, u8 *val)
{
	*val = (u16)pci_read_config(pdev->dev, where, 1);
	return 0;
}

static inline int
pci_read_config_word(struct pci_dev *pdev, int where, u16 *val)
{
	*val = (u16)pci_read_config(pdev->dev, where, 2);
	return 0;
}

static inline int
pci_read_config_dword(struct pci_dev *pdev, int where, u32 *val)
{
	*val = (u32)pci_read_config(pdev->dev, where, 4);
	return 0;
}

static inline int
pci_write_config_byte(struct pci_dev *pdev, int where, u8 val)
{
	pci_write_config(pdev->dev, where, val, 1);
	return 0;
}

static inline int
pci_write_config_word(struct pci_dev *pdev, int where, u16 val)
{
	pci_write_config(pdev->dev, where, val, 2);
	return 0;
}

static inline int
pci_write_config_dword(struct pci_dev *pdev, int where, u32 val)
{
	pci_write_config(pdev->dev, where, val, 4);
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

#endif /* LINUX_PCI_H */

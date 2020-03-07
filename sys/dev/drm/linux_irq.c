/*
 * Copyright (c) 2018-2019 François Tigeot <ftigeot@wolfpond.org>
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

#include <linux/interrupt.h>
#include <linux/device.h>
#include <drm/drmP.h>

#include <sys/bus.h>
#include <bus/pci/pcivar.h>

struct irq_data {
	unsigned int		irq;
	void			*dev_id;
	irq_handler_t		handler;
	const char		*name;
	int			rid;
	struct resource		*resource;
	void			*cookiep;
	struct			lwkt_serialize irq_lock;
	SLIST_ENTRY(irq_data)	id_irq_entries;
};

struct lock irqdata_lock = LOCK_INITIALIZER("dlidl", 0, LK_CANRECURSE);

SLIST_HEAD(irq_data_list_head, irq_data) irq_list = SLIST_HEAD_INITIALIZER(irq_list);

/* DragonFly irq handler, used to invoke Linux ones */
static void
linux_irq_handler(void *arg)
{
	struct irq_data *irq_entry = arg;

	irq_entry->handler(irq_entry->irq, irq_entry->dev_id);
}

/*
 * dev is a struct drm_device*
 * returns: zero on success, non-zero on failure
 */
int
request_irq(unsigned int irq, irq_handler_t handler,
	    unsigned long flags, const char *name, void *dev)
{
	int error;
	struct irq_data *irq_entry;
	struct drm_device *ddev = dev;
	device_t bdev = ddev->dev->bsddev;

	irq_entry = kmalloc(sizeof(*irq_entry), M_DRM, M_WAITOK);

	/* From drm_init_pdev() */
	irq_entry->rid = ddev->pdev->_irqrid;
	irq_entry->resource = ddev->pdev->_irqr;

	irq_entry->irq = irq;
	irq_entry->dev_id = dev;
	irq_entry->handler = handler;
	irq_entry->name = name;
	lwkt_serialize_init(&irq_entry->irq_lock);

	error = bus_setup_intr(bdev, irq_entry->resource, INTR_MPSAFE,
	    linux_irq_handler, irq_entry, &irq_entry->cookiep,
	    &irq_entry->irq_lock);
	if (error) {
		kprintf("request_irq: failed in bus_setup_intr()\n");
		bus_release_resource(bdev, SYS_RES_IRQ,
		    irq_entry->rid, irq_entry->resource);
		kfree(irq_entry);
		return -error;
	}
	lockmgr(&irqdata_lock, LK_EXCLUSIVE);
	SLIST_INSERT_HEAD(&irq_list, irq_entry, id_irq_entries);
	lockmgr(&irqdata_lock, LK_RELEASE);

	return 0;
}

/* dev_id is a struct drm_device* */
void
free_irq(unsigned int irq, void *dev_id)
{
	struct irq_data *irq_entry, *tmp_ie;
	struct drm_device *ddev = dev_id;
	device_t bsddev = ddev->dev->bsddev;
	struct resource *res = ddev->pdev->_irqr;
	int found = 0;

	SLIST_FOREACH_MUTABLE(irq_entry, &irq_list, id_irq_entries, tmp_ie) {
		if ((irq_entry->irq == irq) && (irq_entry->dev_id == dev_id)) {
			found = 1;
			break;
		}
	}

	if (!found) {
		kprintf("free_irq: irq %d for dev_id %p was not registered\n",
		    irq, dev_id);
		return;
	}

	bus_teardown_intr(bsddev, res, irq_entry->cookiep);
	bus_release_resource(bsddev, SYS_RES_IRQ, irq_entry->rid, res);
	if (ddev->pdev->_irq_type == PCI_INTR_TYPE_MSI)
		pci_release_msi(bsddev);

	lockmgr(&irqdata_lock, LK_EXCLUSIVE);
	SLIST_REMOVE(&irq_list, irq_entry, irq_data, id_irq_entries);
	lockmgr(&irqdata_lock, LK_RELEASE);
	kfree(irq_entry);
}

void
disable_irq(unsigned int irq)
{
	struct irq_data *irq_entry;
	struct drm_device *ddev;
	device_t bsddev;

	SLIST_FOREACH(irq_entry, &irq_list, id_irq_entries) {
		if (irq_entry->irq == irq)
			break;
	}

	kprintf("disabling irq %d\n", irq);

	ddev = irq_entry->dev_id;
	bsddev = ddev->dev->bsddev;
	bus_teardown_intr(bsddev, irq_entry->resource, irq_entry->cookiep);
}

void
enable_irq(unsigned int irq)
{
	struct irq_data *irq_entry;
	struct drm_device *ddev;
	device_t bsddev;

	SLIST_FOREACH(irq_entry, &irq_list, id_irq_entries) {
		if (irq_entry->irq == irq)
			break;
	}

	kprintf("enabling irq %d\n", irq);

	ddev = irq_entry->dev_id;
	bsddev = ddev->dev->bsddev;
	bus_setup_intr(bsddev, irq_entry->resource, INTR_MPSAFE,
	    linux_irq_handler, irq_entry, &irq_entry->cookiep,
	    &irq_entry->irq_lock);
}

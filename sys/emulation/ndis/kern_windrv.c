/*-
 * Copyright (c) 2005
 *      Bill Paul <wpaul@windriver.com>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/compat/ndis/kern_windrv.c,v 1.21 2010/11/22 20:46:38 bschmidt Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/unistd.h>
#include <sys/types.h>

#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex2.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/mbuf.h>
#include <sys/bus.h>
#include <sys/proc.h>
#include <sys/sched.h>

#include <sys/queue.h>

#include <bus/u4b/usb.h>
#include <bus/u4b/usbdi.h>

#include <emulation/ndis/pe_var.h>
#include <emulation/ndis/cfg_var.h>
#include <emulation/ndis/resource_var.h>
#include <emulation/ndis/ntoskrnl_var.h>
#include <emulation/ndis/ndis_var.h>
#include <emulation/ndis/hal_var.h>
#include <emulation/ndis/u4bd_var.h>

static struct lock drvdb_lock;
static STAILQ_HEAD(drvdb, drvdb_ent) drvdb_head;

static driver_object	fake_pci_driver; /* serves both PCI and cardbus */
static driver_object	fake_pccard_driver;

#define DUMMY_REGISTRY_PATH "\\\\some\\bogus\\path"

int
windrv_libinit(void)
{
	STAILQ_INIT(&drvdb_head);
	lockinit(&drvdb_lock, "Windows driver DB lock", 0, LK_CANRECURSE);

	/*
	 * PCI and pccard devices don't need to use IRPs to
	 * interact with their bus drivers (usually), so our
	 * emulated PCI and pccard drivers are just stubs.
	 * USB devices, on the other hand, do all their I/O
	 * by exchanging IRPs with the USB bus driver, so
	 * for that we need to provide emulator dispatcher
	 * routines, which are in a separate module.
	 */

	windrv_bus_attach(&fake_pci_driver, "PCI Bus");
	windrv_bus_attach(&fake_pccard_driver, "PCCARD Bus");

	return (0);
}

int
windrv_libfini(void)
{
	struct drvdb_ent	*d;

	lockmgr(&drvdb_lock, LK_EXCLUSIVE);
	while(STAILQ_FIRST(&drvdb_head) != NULL) {
		d = STAILQ_FIRST(&drvdb_head);
		STAILQ_REMOVE_HEAD(&drvdb_head, link);
		kfree(d, M_DEVBUF);
	}
	lockmgr(&drvdb_lock, LK_RELEASE);

	RtlFreeUnicodeString(&fake_pci_driver.dro_drivername);
	RtlFreeUnicodeString(&fake_pccard_driver.dro_drivername);

	lockuninit(&drvdb_lock);

	return (0);
}

/*
 * Given the address of a driver image, find its corresponding
 * driver_object.
 */

driver_object *
windrv_lookup(vm_offset_t img, char *name)
{
	struct drvdb_ent	*d;
	unicode_string		us;
	ansi_string		as;

	bzero((char *)&us, sizeof(us));

	/* Damn unicode. */

	if (name != NULL) {
		RtlInitAnsiString(&as, name);
		if (RtlAnsiStringToUnicodeString(&us, &as, TRUE))
			return (NULL);
	}

	lockmgr(&drvdb_lock, LK_EXCLUSIVE);
	STAILQ_FOREACH(d, &drvdb_head, link) {
		if (d->windrv_object->dro_driverstart == (void *)img ||
		    (bcmp((char *)d->windrv_object->dro_drivername.us_buf,
		    (char *)us.us_buf, us.us_len) == 0 && us.us_len)) {
			lockmgr(&drvdb_lock, LK_RELEASE);
			if (name != NULL)
				ExFreePool(us.us_buf);
			return (d->windrv_object);
		}
	}
	lockmgr(&drvdb_lock, LK_RELEASE);

	if (name != NULL)
		RtlFreeUnicodeString(&us);

	return (NULL);
}

struct drvdb_ent *
windrv_match(matchfuncptr matchfunc, void *ctx)
{
	struct drvdb_ent	*d;
	int			match;

	lockmgr(&drvdb_lock, LK_EXCLUSIVE);
	STAILQ_FOREACH(d, &drvdb_head, link) {
		if (d->windrv_devlist == NULL)
			continue;
		match = matchfunc(d->windrv_bustype, d->windrv_devlist, ctx);
		if (match == TRUE) {
			lockmgr(&drvdb_lock, LK_RELEASE);
			return (d);
		}
	}
	lockmgr(&drvdb_lock, LK_RELEASE);

	return (NULL);
}

/*
 * Remove a driver_object from our datatabase and destroy it. Throw
 * away any custom driver extension info that may have been added.
 */

int
windrv_unload(module_t mod, vm_offset_t img, int len)
{
	struct drvdb_ent	*db, *r = NULL;
	driver_object		*drv;
	device_object		*d, *pdo;
	device_t		dev;
	list_entry		*e;

	drv = windrv_lookup(img, NULL);

	/*
	 * When we unload a driver image, we need to force a
	 * detach of any devices that might be using it. We
	 * need the PDOs of all attached devices for this.
	 * Getting at them is a little hard. We basically
	 * have to walk the device lists of all our bus
	 * drivers.
	 */

	lockmgr(&drvdb_lock, LK_EXCLUSIVE);
	STAILQ_FOREACH(db, &drvdb_head, link) {
		/*
		 * Fake bus drivers have no devlist info.
		 * If this driver has devlist info, it's
		 * a loaded Windows driver and has no PDOs,
		 * so skip it.
		 */
		if (db->windrv_devlist != NULL)
			continue;
		pdo = db->windrv_object->dro_devobj;
		while (pdo != NULL) {
			d = pdo->do_attacheddev;
			if (d->do_drvobj != drv) {
				pdo = pdo->do_nextdev;
				continue;
			}
			dev = pdo->do_devext;
			pdo = pdo->do_nextdev;
			lockmgr(&drvdb_lock, LK_RELEASE);
			device_detach(dev);
			lockmgr(&drvdb_lock, LK_EXCLUSIVE);
		}
	}

	STAILQ_FOREACH(db, &drvdb_head, link) {
		if (db->windrv_object->dro_driverstart == (void *)img) {
			r = db;
			STAILQ_REMOVE(&drvdb_head, db, drvdb_ent, link);
			break;
		}
	}
	lockmgr(&drvdb_lock, LK_RELEASE);

	if (r == NULL)
		return (ENOENT);

	if (drv == NULL)
		return (ENOENT);

	/*
	 * Destroy any custom extensions that may have been added.
	 */
	drv = r->windrv_object;
	while (!IsListEmpty(&drv->dro_driverext->dre_usrext)) {
		e = RemoveHeadList(&drv->dro_driverext->dre_usrext);
		ExFreePool(e);
	}

	/* Free the driver extension */
	kfree(drv->dro_driverext, M_DEVBUF);

	/* Free the driver name */
	RtlFreeUnicodeString(&drv->dro_drivername);

	/* Free driver object */
	kfree(drv, M_DEVBUF);

	/* Free our DB handle */
	kfree(r, M_DEVBUF);

	return (0);
}

#define WINDRV_LOADED		htonl(0x42534F44)

#ifdef __x86_64__
static void
patch_user_shared_data_address(vm_offset_t img, size_t len)
{
	unsigned long i, n, max_addr, *addr;

	n = len - sizeof(unsigned long);
	max_addr = KI_USER_SHARED_DATA + sizeof(kuser_shared_data);
	for (i = 0; i < n; i++) {
		addr = (unsigned long *)(img + i);
		if (*addr >= KI_USER_SHARED_DATA && *addr < max_addr) {
			*addr -= KI_USER_SHARED_DATA;
			*addr += (unsigned long)&kuser_shared_data;
		}
	}
}
#endif

/*
 * Loader routine for actual Windows driver modules, ultimately
 * calls the driver's DriverEntry() routine.
 */

int
windrv_load(module_t mod, vm_offset_t img, int len, interface_type bustype,
    void *devlist, ndis_cfg *regvals)
{
	image_import_descriptor	imp_desc;
	image_optional_header	opt_hdr;
	driver_entry		entry;
	struct drvdb_ent	*new;
	struct driver_object	*drv;
	int			status;
	uint32_t		*ptr;
	ansi_string		as;

	/*
	 * First step: try to relocate and dynalink the executable
	 * driver image.
	 */

	ptr = (uint32_t *)(img + 8);
	if (*ptr == WINDRV_LOADED)
		goto skipreloc;

	/* Perform text relocation */
	if (pe_relocate(img))
		return (ENOEXEC);

	/* Dynamically link the NDIS.SYS routines -- required. */
	if (pe_patch_imports(img, "NDIS", ndis_functbl))
		return (ENOEXEC);

	/* Dynamically link the HAL.dll routines -- optional. */
	if (pe_get_import_descriptor(img, &imp_desc, "HAL") == 0) {
		if (pe_patch_imports(img, "HAL", hal_functbl))
			return (ENOEXEC);
	}

	/* Dynamically link ntoskrnl.exe -- optional. */
	if (pe_get_import_descriptor(img, &imp_desc, "ntoskrnl") == 0) {
		if (pe_patch_imports(img, "ntoskrnl", ntoskrnl_functbl))
			return (ENOEXEC);
	}

#ifdef __x86_64__
	patch_user_shared_data_address(img, len);
#endif

	/* Dynamically link USBD.SYS -- optional */
	if (pe_get_import_descriptor(img, &imp_desc, "USBD") == 0) {
		if (pe_patch_imports(img, "USBD", usbd_functbl))
			return (ENOEXEC);
	}

	*ptr = WINDRV_LOADED;

skipreloc:

	/* Next step: find the driver entry point. */

	pe_get_optional_header(img, &opt_hdr);
	entry = (driver_entry)pe_translate_addr(img, opt_hdr.ioh_entryaddr);

	/* Next step: allocate and store a driver object. */

	new = kmalloc(sizeof(struct drvdb_ent), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (new == NULL)
		return (ENOMEM);

	drv = kmalloc(sizeof(driver_object), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (drv == NULL) {
		kfree (new, M_DEVBUF);
		return (ENOMEM);
	}

	/* Allocate a driver extension structure too. */

	drv->dro_driverext = kmalloc(sizeof(driver_extension),
	    M_DEVBUF, M_NOWAIT|M_ZERO);

	if (drv->dro_driverext == NULL) {
		kfree(new, M_DEVBUF);
		kfree(drv, M_DEVBUF);
		return (ENOMEM);
	}

	InitializeListHead((&drv->dro_driverext->dre_usrext));

	drv->dro_driverstart = (void *)img;
	drv->dro_driversize = len;

	RtlInitAnsiString(&as, DUMMY_REGISTRY_PATH);
	if (RtlAnsiStringToUnicodeString(&drv->dro_drivername, &as, TRUE)) {
		kfree(new, M_DEVBUF);
		kfree(drv, M_DEVBUF);
		return (ENOMEM);
	}

	new->windrv_object = drv;
	new->windrv_regvals = regvals;
	new->windrv_devlist = devlist;
	new->windrv_bustype = bustype;

	/* Now call the DriverEntry() function. */

	status = MSCALL2(entry, drv, &drv->dro_drivername);

	if (status != STATUS_SUCCESS) {
		RtlFreeUnicodeString(&drv->dro_drivername);
		kfree(drv, M_DEVBUF);
		kfree(new, M_DEVBUF);
		return (ENODEV);
	}

	lockmgr(&drvdb_lock, LK_EXCLUSIVE);
	STAILQ_INSERT_HEAD(&drvdb_head, new, link);
	lockmgr(&drvdb_lock, LK_RELEASE);

	return (0);
}

/*
 * Make a new Physical Device Object for a device that was
 * detected/plugged in. For us, the PDO is just a way to
 * get at the device_t.
 */

int
windrv_create_pdo(driver_object *drv, device_t bsddev)
{
	device_object		*dev;

	/*
	 * This is a new physical device object, which technically
	 * is the "top of the stack." Consequently, we don't do
	 * an IoAttachDeviceToDeviceStack() here.
	 */

	lockmgr(&drvdb_lock, LK_EXCLUSIVE);
	IoCreateDevice(drv, 0, NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &dev);
	lockmgr(&drvdb_lock, LK_RELEASE);

	/* Stash pointer to our BSD device handle. */

	dev->do_devext = bsddev;

	return (STATUS_SUCCESS);
}

void
windrv_destroy_pdo(driver_object *drv, device_t bsddev)
{
	device_object		*pdo;

	pdo = windrv_find_pdo(drv, bsddev);

	/* Remove reference to device_t */

	pdo->do_devext = NULL;

	lockmgr(&drvdb_lock, LK_EXCLUSIVE);
	IoDeleteDevice(pdo);
	lockmgr(&drvdb_lock, LK_RELEASE);
}

/*
 * Given a device_t, find the corresponding PDO in a driver's
 * device list.
 */

device_object *
windrv_find_pdo(driver_object *drv, device_t bsddev)
{
	device_object		*pdo;

	lockmgr(&drvdb_lock, LK_EXCLUSIVE);
	pdo = drv->dro_devobj;
	while (pdo != NULL) {
		if (pdo->do_devext == bsddev) {
			lockmgr(&drvdb_lock, LK_RELEASE);
			return (pdo);
		}
		pdo = pdo->do_nextdev;
	}
	lockmgr(&drvdb_lock, LK_RELEASE);

	return (NULL);
}

/*
 * Add an internally emulated driver to the database. We need this
 * to set up an emulated bus driver so that it can receive IRPs.
 */

int
windrv_bus_attach(driver_object *drv, char *name)
{
	struct drvdb_ent	*new;
	ansi_string		as;

	new = kmalloc(sizeof(struct drvdb_ent), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (new == NULL)
		return (ENOMEM);

	RtlInitAnsiString(&as, name);
	if (RtlAnsiStringToUnicodeString(&drv->dro_drivername, &as, TRUE))
	{
		kfree(new, M_DEVBUF);
		return (ENOMEM);
	}

	/*
	 * Set up a fake image pointer to avoid false matches
	 * in windrv_lookup().
	 */
	drv->dro_driverstart = (void *)0xFFFFFFFF;

	new->windrv_object = drv;
	new->windrv_devlist = NULL;
	new->windrv_regvals = NULL;

	lockmgr(&drvdb_lock, LK_EXCLUSIVE);
	STAILQ_INSERT_HEAD(&drvdb_head, new, link);
	lockmgr(&drvdb_lock, LK_RELEASE);

	return (0);
}

#ifdef __x86_64__

extern void	x86_64_wrap(void);
extern void	x86_64_wrap_call(void);
extern void	x86_64_wrap_end(void);

int
windrv_wrap(funcptr func, funcptr *wrap, int argcnt, int ftype)
{
	funcptr			p;
	vm_offset_t		*calladdr;
	vm_offset_t		wrapstart, wrapend, wrapcall;

	wrapstart = (vm_offset_t)&x86_64_wrap;
	wrapend = (vm_offset_t)&x86_64_wrap_end;
	wrapcall = (vm_offset_t)&x86_64_wrap_call;

	/* Allocate a new wrapper instance. */

	p = kmalloc((wrapend - wrapstart), M_DEVBUF, M_NOWAIT);
	if (p == NULL)
		return (ENOMEM);

	/* Copy over the code. */

	bcopy((char *)wrapstart, p, (wrapend - wrapstart));

	/* Insert the function address into the new wrapper instance. */

	calladdr = (uint64_t *)((char *)p + (wrapcall - wrapstart) + 2);
	*calladdr = (vm_offset_t)func;

	*wrap = p;

	return (0);
}
#endif /* __x86_64__ */


int
windrv_unwrap(funcptr func)
{
	kfree(func, M_DEVBUF);

	return (0);
}

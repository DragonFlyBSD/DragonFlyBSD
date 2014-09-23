/*
 * $OpenBSD: usb_port.h,v 1.18 2000/09/06 22:42:10 rahnds Exp $
 * $NetBSD: usb_port.h,v 1.68 2005/07/30 06:14:50 skrll Exp $
 * $FreeBSD: src/sys/dev/usb/usb_port.h,v 1.65 2003/11/09 23:54:21 joe Exp $
 */

/* Also already merged from NetBSD:
 *	$NetBSD: usb_port.h,v 1.57 2002/09/27 20:42:01 thorpej Exp $
 *	$NetBSD: usb_port.h,v 1.58 2002/10/01 01:25:26 thorpej Exp $
 */

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
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
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _USB_PORT_H
#define _USB_PORT_H

/*
 * Macros to ease the import of USB drivers from other BSD systems. Drivers
 * in the DragonFly tree don't use any of these macros.
 *
 * Driver that wants to use these can simply include <bus/usb/usb_port.h>.
 */

#include "opt_usb.h"

#define Static static

#define device_ptr_t device_t
#define USBBASEDEVICE device_t
#define USBDEV(bdev) (bdev)
#define USBDEVNAME(bdev) device_get_nameunit(bdev)
#define USBDEVUNIT(bdev) device_get_unit(bdev)
#define USBDEVPTRNAME(bdev) device_get_nameunit(bdev)
#define USBGETSOFTC(bdev) (device_get_softc(bdev))

#define DECLARE_USB_DMA_T \
	struct usb_dma_block; \
	typedef struct { \
		struct usb_dma_block *block; \
		u_int offs; \
		u_int len; \
	} usb_dma_t

#define	PROC_LOCK(p)
#define	PROC_UNLOCK(p)
#define uio_procp uio_td

typedef struct thread *usb_proc_ptr;

#define	config_pending_incr()
#define	config_pending_decr()

#define usb_kthread_create(f, s)			\
		kthread_create(f, s, NULL, "dummy")
#define usb_kthread_create1(f, s, p, name, arg)		\
		kthread_create(f, s, p, name, arg)
#define usb_kthread_create2(f, s, p, name) 		\
		kthread_create(f, s, p, name)

typedef struct callout usb_callout_t;
#define usb_callout_init(h)     callout_init(&(h))
#define usb_callout(h, t, f, d) callout_reset(&(h), (t), (f), (d))
#define usb_uncallout(h, f, d)  callout_stop(&(h))

#define clalloc(p, s, x) (clist_alloc_cblocks((p), (s), (s)), 0)
#define clfree(p) clist_free_cblocks((p))

#define config_detach(dev, flag) device_delete_child(device_get_parent(dev), dev)

typedef struct malloc_type *usb_malloc_type;

#define USB_DECLARE_DRIVER_INIT(dname, init...) \
Static device_probe_t __CONCAT(dname,_match); \
Static device_attach_t __CONCAT(dname,_attach); \
Static device_detach_t __CONCAT(dname,_detach); \
\
Static devclass_t __CONCAT(dname,_devclass); \
\
Static device_method_t __CONCAT(dname,_methods)[] = { \
        DEVMETHOD(device_probe, __CONCAT(dname,_match)), \
        DEVMETHOD(device_attach, __CONCAT(dname,_attach)), \
        DEVMETHOD(device_detach, __CONCAT(dname,_detach)), \
	init, \
        DEVMETHOD_END \
}; \
\
Static driver_t __CONCAT(dname,_driver) = { \
        #dname, \
        __CONCAT(dname,_methods), \
        sizeof(struct __CONCAT(dname,_softc)) \
}; \
MODULE_DEPEND(dname, usb, 1, 1, 1)


#define METHODS_NONE			{0,0}
#define USB_DECLARE_DRIVER(dname)	USB_DECLARE_DRIVER_INIT(dname, METHODS_NONE)

#define USB_MATCH(dname) \
Static int \
__CONCAT(dname,_match)(device_t self)

#define USB_MATCH_START(dname, uaa) \
        struct usb_attach_arg *uaa = device_get_ivars(self)

#define USB_MATCH_SETUP()

#define USB_ATTACH(dname) \
Static int \
__CONCAT(dname,_attach)(device_t self)

#define USB_ATTACH_START(dname, sc, uaa) \
        struct __CONCAT(dname,_softc) *sc = device_get_softc(self); \
        struct usb_attach_arg *uaa = device_get_ivars(self)

/* Returns from attach */
#define USB_ATTACH_ERROR_RETURN		return ENXIO
#define USB_ATTACH_SUCCESS_RETURN	return 0

#define USB_ATTACH_SETUP \
	device_set_desc_copy(self, devinfo)

#define USB_DETACH(dname) \
Static int \
__CONCAT(dname,_detach)(device_t self)

#define USB_DETACH_START(dname, sc) \
	struct __CONCAT(dname,_softc) *sc = device_get_softc(self)

#define USB_GET_SC_OPEN(dname, unit, sc) \
	sc = devclass_get_softc(__CONCAT(dname,_devclass), unit); \
	if (sc == NULL) \
		return (ENXIO)

#define USB_GET_SC(dname, unit, sc) \
	sc = devclass_get_softc(__CONCAT(dname,_devclass), unit)

#define USB_DO_ATTACH(dev, bdev, parent, args, print, sub) \
	(device_probe_and_attach((bdev)) == 0 ? (bdev) : 0)

/* conversion from one type of queue to the other */
#define SIMPLEQ_REMOVE_HEAD	STAILQ_REMOVE_HEAD
#define SIMPLEQ_INSERT_HEAD	STAILQ_INSERT_HEAD
#define SIMPLEQ_INSERT_TAIL	STAILQ_INSERT_TAIL
#define SIMPLEQ_NEXT		STAILQ_NEXT
#define SIMPLEQ_FIRST		STAILQ_FIRST
#define SIMPLEQ_HEAD		STAILQ_HEAD
#define SIMPLEQ_EMPTY		STAILQ_EMPTY
#define SIMPLEQ_FOREACH		STAILQ_FOREACH
#define SIMPLEQ_INIT		STAILQ_INIT
#define SIMPLEQ_HEAD_INITIALIZER	STAILQ_HEAD_INITIALIZER
#define SIMPLEQ_ENTRY		STAILQ_ENTRY

#define logprintf		kprintf

#endif /* _USB_PORT_H */

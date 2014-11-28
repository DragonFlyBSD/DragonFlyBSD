/*-
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2003 Paul Saab
 * Copyright (c) 2003 Vinod Kashyap
 * Copyright (c) 2000 BSDi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/twe/twe_compat.h,v 1.17 2012/11/17 01:52:19 svnexp Exp $
 */
/*
 * Portability and compatibility interfaces.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/sysctl.h>
#include <sys/buf2.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/stat.h>
#include <sys/rman.h>
#include <sys/devicestat.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#define TWE_DRIVER_NAME		twe
#define TWED_DRIVER_NAME	twed
#define TWE_MALLOC_CLASS	M_TWE

/* 
 * Wrappers for bus-space actions
 */
#define TWE_CONTROL(sc, val)		bus_write_4((sc)->twe_io, 0x0, (u_int32_t)val)
#define TWE_STATUS(sc)			(u_int32_t)bus_read_4((sc)->twe_io, 0x4)
#define TWE_COMMAND_QUEUE(sc, val)	bus_write_4((sc)->twe_io, 0x8, (u_int32_t)val)
#define TWE_RESPONSE_QUEUE(sc)		bus_read_4((sc)->twe_io, 0xc)

/*
 * FreeBSD-specific softc elements
 */
#define TWE_PLATFORM_SOFTC								\
    bus_dmamap_t		twe_cmdmap;	/* DMA map for command */				\
    u_int32_t			twe_cmdphys;	/* address of command in controller space */		\
    device_t			twe_dev;		/* bus device */		\
    cdev_t			twe_dev_t;		/* control device */		\
    struct resource		*twe_io;		/* register interface window */	\
    bus_dma_tag_t		twe_parent_dmat;	/* parent DMA tag */		\
    bus_dma_tag_t		twe_buffer_dmat;	/* data buffer DMA tag */	\
    bus_dma_tag_t		twe_cmd_dmat;		/* command buffer DMA tag */	\
    bus_dma_tag_t		twe_immediate_dmat;	/* command buffer DMA tag */	\
    struct resource		*twe_irq;		/* interrupt */			\
    void			*twe_intr;		/* interrupt handle */		\
    struct intr_config_hook	twe_ich;		/* delayed-startup hook */	\
    void			*twe_cmd;		/* command structures */	\
    void			*twe_immediate;		/* immediate commands */	\
    bus_dmamap_t		twe_immediate_map;					\
    struct lock			twe_io_lock;						\
    struct lock			twe_config_lock;

/*
 * FreeBSD-specific request elements
 */
#define TWE_PLATFORM_REQUEST										\
    bus_dmamap_t		tr_dmamap;	/* DMA map for data */					\
    u_int32_t			tr_dataphys;	/* data buffer base address in controller space */

/*
 * Output identifying the controller/disk
 */
#define twe_printf(sc, fmt, args...)	device_printf(sc->twe_dev, fmt , ##args)
#define twed_printf(twed, fmt, args...)	device_printf(twed->twed_dev, fmt , ##args)

#define	TWE_IO_LOCK(sc)			lockmgr(&(sc)->twe_io_lock, LK_EXCLUSIVE)
#define	TWE_IO_UNLOCK(sc)		lockmgr(&(sc)->twe_io_lock, LK_RELEASE)
#define	TWE_CONFIG_LOCK(sc)		lockmgr(&(sc)->twe_config_lock, LK_EXCLUSIVE)
#define	TWE_CONFIG_UNLOCK(sc)		lockmgr(&(sc)->twe_config_lock, LK_RELEASE)
#define	TWE_CONFIG_ASSERT_LOCKED(sc)	KKASSERT(lockowned(&(sc)->twe_config_lock))

/*
 * XXX
 *
 * Mimics FreeBSD's mtx_assert() behavior.
 * We might want a global lockassert() function in the future.
 */
static __inline void
twe_lockassert(struct lock *lockp)
{
	if (panicstr == NULL && !dumping)
		KKASSERT(lockstatus(lockp, curthread) != 0);
}

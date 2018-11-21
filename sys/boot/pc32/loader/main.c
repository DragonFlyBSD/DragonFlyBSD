/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
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
 * $FreeBSD: src/sys/boot/i386/loader/main.c,v 1.28 2003/08/25 23:28:32 obrien Exp $
 */

/*
 * MD bootstrap main() and assorted miscellaneous
 * commands.
 */

#include <stand.h>
#include <string.h>
#include <machine/bootinfo.h>
#include <machine/psl.h>
#include <sys/reboot.h>
#include <sys/boot.h>

#include "bootstrap.h"
#include "libi386/libi386.h"
#include "libi386/smbios.h"
#include "btxv86.h"

#define	KARGS_FLAGS_CD		0x1
#define	KARGS_FLAGS_PXE		0x2

#define COMCONSOLE_DEBUG	1
/* Arguments passed in from the boot1/boot2 loader */
static struct
{
    u_int32_t	howto;
    u_int32_t	bootdev;
    u_int32_t	bootflags;
    u_int32_t	pxeinfo;
    u_int32_t	res2;
    u_int32_t	bootinfo;
} *kargs;

static u_int32_t	initial_howto;
static u_int32_t	initial_bootdev;
static struct bootinfo	*initial_bootinfo;

struct arch_switch	archsw;		/* MI/MD interface boundary */

static void		extract_currdev(void);
extern int              isa_inb(int port);
extern void             isa_outb(int port, int value);
void			exit(int code);

/* from vers.c */
extern	char bootprog_name[], bootprog_rev[], bootprog_date[], bootprog_maker[];

/* XXX debugging */
extern char _end[];

/* convert flags to environment "boot_xxx" strings */
static void howtoflags2env(uint32_t howto)
{
    uint32_t i;
    for (i = 0; howto_names[i].mask != 0; i++) {
        if (howto & howto_names[i].mask) {
            setenv(howto_names[i].ev, "YES", 1);
        }
    }
}

static void setconsole(void)
{
    if (initial_howto & RB_MUTE)
        setenv("console", "nullconsole", 1);
    else if (!strcmp(getenv("boot_multicons"), "YES")) {
        if (!strcmp(getenv("boot_serial"), "YES"))
            setenv("console", "comconsole vidconsole", 1);
        else
            setenv("console", "vidconsole comconsole", 1);
    } 
    else if (!strcmp(getenv("boot_serial"), "YES"))
        setenv("console", "comconsole", 1);
}

int
main(void)
{
    char *memend;
    int i;

    /* Pick up arguments */
    kargs = (void *)__args;
    initial_howto = kargs->howto;
    initial_bootdev = kargs->bootdev;
    initial_bootinfo = kargs->bootinfo ? (struct bootinfo *)PTOV(kargs->bootinfo) : NULL;

    /* Initialize the v86 register set to a known-good state. */
    bzero(&v86, sizeof(v86));
    v86.efl = PSL_RESERVED_DEFAULT | PSL_I;

    /*
     * Initialize the heap as early as possible.  Once this is done, 
     * malloc() is usable.
     *
     * Don't include our stack in the heap.  If the stack is in low
     * user memory use {end,bios_basemem}.  If the stack is in high
     * user memory but not extended memory then don't let the heap
     * overlap the stack.  If the stack is in extended memory limit
     * the heap to bios_basemem.
     *
     * Be sure to use the virtual bios_basemem address rather then
     * the physical bios_basemem address or we may overwrite BIOS
     * data.
     */
    bios_getmem();
    memend = (char *)&memend - 0x8000;	/* space for stack (16K) */
    memend = (char *)((uintptr_t)memend & ~(uintptr_t)(0x1000 - 1));

    /*
     * Steal the heap from high memory.  bios_basemem no longer has
     * enough space, last failure was the gunzip code for initrd.img
     * needing > ~32KB of temporary buffer space and we ran out.  again.
     *
     * High memory reserves at least ~1MB for the loader.
     */
#if 0
    if (memend < (char *)_end) {
	setheap((void *)_end, PTOV(bios_basemem));
    } else {
	if (memend > (char *)PTOV(bios_basemem))
	    memend = (char *)PTOV(bios_basemem);
	setheap((void *)_end, memend);
    }
#endif
    setheap((void *)heapbase, (void *)memtop);

    /*
     * Initialise the block cache
     */
    bcache_init(32, 512);	/* 16k cache XXX tune this */

    /*
     * convert flags to environment boot_xxxx entries
     */
    howtoflags2env(initial_howto);

    /*
     * Special handling for PXE and CD booting.
     */
    if (kargs->bootinfo == 0) {
	/*
	 * We only want the PXE disk to try to init itself in the below
	 * walk through devsw if we actually booted off of PXE.
	 */
	if (kargs->bootflags & KARGS_FLAGS_PXE)
	    pxe_enable(kargs->pxeinfo ? PTOV(kargs->pxeinfo) : NULL);
	else if (kargs->bootflags & KARGS_FLAGS_CD)
	    bc_add(initial_bootdev);
    }

    /*
     * XXX Chicken-and-egg problem; we want to have console output early,
     * but some console attributes may depend on reading from eg. the boot
     * device, which we can't do yet.
     *
     * We can use printf() etc. once this is done.   The previous boot stage
     * might have requested a video or serial preference, in which case we
     * set it.  If neither is specified or both are specified we leave the
     * console environment variable alone, defaulting to dual boot.
     */
    setconsole();
    cons_probe();

    /*
     * March through the device switch probing for things.
     */
    for (i = 0; devsw[i] != NULL; i++) {
	/*WDEBUG('M' + i);*/
	if (devsw[i]->dv_init != NULL)
	    (devsw[i]->dv_init)();
	/*WDEBUG('M' + i);*/
    }
    printf("BIOS %dkB/%dkB available memory\n",
	    bios_basemem / 1024, bios_extmem / 1024);
    if (initial_bootinfo != NULL) {
	initial_bootinfo->bi_basemem = bios_basemem / 1024;
	initial_bootinfo->bi_extmem = bios_extmem / 1024;
    }

    /* detect ACPI for future reference */
    biosacpi_detect();

    /* detect SMBIOS for future reference */
    smbios_detect(NULL);

    /* enable EHCI */
    setenv("ehci_load", "YES", 1);

    /* enable XHCI */
    setenv("xhci_load", "YES", 1);

    printf("\n");
    printf("%s, Revision %s\n", bootprog_name, bootprog_rev);
    printf("(%s, %s)\n", bootprog_maker, bootprog_date);

#if COMCONSOLE_DEBUG
    printf("args at %p initial_howto = %08x bootdev = %08x bootinfo = %p\n",
	kargs, initial_howto, initial_bootdev, initial_bootinfo);
    if (initial_howto & RB_SERIAL || initial_howto & RB_MULTIPLE || initial_howto & RB_DUAL) {
        printf("Serial at Speed:%s on Port:%s\n", getenv("comconsole_speed"), getenv("comconsole_port"));
    }
#endif

    extract_currdev();				/* set $currdev and $loaddev */
    setenv("LINES", "24", 1);			/* optional */

    bios_getsmap();

    archsw.arch_autoload = i386_autoload;
    archsw.arch_getdev = i386_getdev;
    archsw.arch_copyin = i386_copyin;
    archsw.arch_copyout = i386_copyout;
    archsw.arch_readin = i386_readin;
    archsw.arch_isainb = isa_inb;
    archsw.arch_isaoutb = isa_outb;

    interact();			/* doesn't return */

    /* if we ever get here, it is an error */
    return (1);
}

/*
 * Set the 'current device' by (if possible) recovering the boot device as
 * supplied by the initial bootstrap.
 *
 * XXX should be extended for netbooting.
 */
static void
extract_currdev(void)
{
    struct i386_devdesc	new_currdev;
    int			biosdev = -1;

    /* Assume we are booting from a BIOS disk by default */
    new_currdev.d_dev = &biosdisk;

    /* new-style boot loaders such as pxeldr and cdldr */
    if (kargs->bootinfo == 0) {
        if ((kargs->bootflags & KARGS_FLAGS_CD) != 0) {
	    /* we are booting from a CD with cdboot */
	    new_currdev.d_dev = &bioscd;
	    new_currdev.d_kind.bioscd.unit = bc_bios2unit(initial_bootdev);
	} else if ((kargs->bootflags & KARGS_FLAGS_PXE) != 0) {
	    /* we are booting from pxeldr */
	    new_currdev.d_dev = &pxedisk;
	    new_currdev.d_kind.netif.unit = 0;
	} else {
	    /* we don't know what our boot device is */
	    new_currdev.d_kind.biosdisk.slice = -1;
	    new_currdev.d_kind.biosdisk.partition = 0;
	    biosdev = -1;
	}
    } else if ((initial_bootdev & B_MAGICMASK) != B_DEVMAGIC) {
	/* The passed-in boot device is bad */
	new_currdev.d_kind.biosdisk.slice = -1;
	new_currdev.d_kind.biosdisk.partition = 0;
	biosdev = -1;
    } else {
	new_currdev.d_kind.biosdisk.slice = B_SLICE(initial_bootdev) - 1;
	new_currdev.d_kind.biosdisk.partition = B_PARTITION(initial_bootdev);
	biosdev = initial_bootinfo->bi_bios_dev;

	/*
	 * If we are booted by an old bootstrap, we have to guess at the BIOS
	 * unit number.  We will loose if there is more than one disk type
	 * and we are not booting from the lowest-numbered disk type
	 * (ie. SCSI when IDE also exists).
	 */
	if ((biosdev == 0) && (B_TYPE(initial_bootdev) != 2))	/* biosdev doesn't match major */
	    biosdev = 0x80 + B_UNIT(initial_bootdev);		/* assume harddisk */
    }
    new_currdev.d_type = new_currdev.d_dev->dv_type;

    /*
     * If we are booting off of a BIOS disk and we didn't succeed in determining
     * which one we booted off of, just use disk0: as a reasonable default.
     */
    if ((new_currdev.d_type == biosdisk.dv_type) &&
	((new_currdev.d_kind.biosdisk.unit = bd_bios2unit(biosdev)) == -1)) {
	printf("Can't work out which disk we are booting from.\n"
	       "Guessed BIOS device 0x%x not found by probes, defaulting to disk0:\n", biosdev);
	new_currdev.d_kind.biosdisk.unit = 0;
    }
    env_setenv("currdev", EV_VOLATILE, i386_fmtdev(&new_currdev),
	       i386_setcurrdev, env_nounset);
    env_setenv("loaddev", EV_VOLATILE, i386_fmtdev(&new_currdev), env_noset,
	       env_nounset);
}

COMMAND_SET(reboot, "reboot", "reboot the system", command_reboot);

static int
command_reboot(int argc, char *argv[])
{
    int i;

    for (i = 0; devsw[i] != NULL; ++i)
	if (devsw[i]->dv_cleanup != NULL)
	    (devsw[i]->dv_cleanup)();

    printf("Rebooting...\n");
    delay(1000000);
    __exit(0);
}

/* provide this for panic, as it's not in the startup code */
void
exit(int code)
{
    __exit(code);
}

COMMAND_SET(heap, "heap", "show heap usage", command_heap);

static int
command_heap(int argc, char *argv[])
{
    char *base;
    size_t bytes;

    mallocstats();
    base = getheap(&bytes);
    printf("heap %p-%p (%d)\n", base, base + bytes, (int)bytes);
    printf("stack at %p\n", &argc);
    return(CMD_OK);
}


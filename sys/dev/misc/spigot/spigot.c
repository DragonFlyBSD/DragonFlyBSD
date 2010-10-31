/*
 * Video spigot capture driver.
 *
 * Copyright (c) 1995, Jim Lowe.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer. 2.
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This is the minimum driver code required to make a spigot work.
 * Unfortunatly, I can't include a real driver since the information
 * on the spigot is under non-disclosure.  You can pick up a library
 * that will work with this driver from
 * ftp://ftp.cs.uwm.edu/pub/FreeBSD-UWM.  The library contains the
 * source that I can release as well as several object modules and
 * functions that allows one to read spigot data.  See the code for
 * spigot_grab.c that is included with the library data.
 *
 * The vendor will not allow me to release the spigot library code.
 * Please don't ask me for it.
 *
 * To use this driver you will need the spigot library.  The library is
 * available from:
 *
 *	ftp.cs.uwm.edu://pub/FreeBSD-UWM/spigot/spigot.tar.gz
 *
 * Version 1.7, December 1995.
 *
 * $FreeBSD: src/sys/i386/isa/spigot.c,v 1.44 2000/01/29 16:17:36 peter Exp $
 *
 */

#include	"use_spigot.h"

#if NSPIGOT > 1
error "Can only have 1 spigot configured."
#endif

#include	"opt_spigot.h"

#include	<sys/param.h>
#include	<sys/systm.h>
#include	<sys/conf.h>
#include	<sys/device.h>
#include	<sys/proc.h>
#include	<sys/priv.h>
#include	<sys/signalvar.h>
#include	<sys/mman.h>

#include	<machine/frame.h>
#include	<machine/md_var.h>
#include	<machine/spigot.h>
#include	<machine/psl.h>

#include	<bus/isa/isa_device.h>

static struct spigot_softc {
	u_long		flags;
	u_long	 	maddr;
	struct proc	*p;
	u_long		signal_num;
	u_short		irq;
} spigot_softc[NSPIGOT];

/* flags in softc */
#define	OPEN		0x01
#define	ALIVE		0x02

#define	UNIT(dev) minor(dev)

static int	spigot_probe(struct isa_device *id);
static int	spigot_attach(struct isa_device *id);

struct isa_driver	spigotdriver = {spigot_probe, spigot_attach, "spigot"};

static	d_open_t	spigot_open;
static	d_close_t	spigot_close;
static	d_read_t	spigot_read;
static	d_write_t	spigot_write;
static	d_ioctl_t	spigot_ioctl;
static	d_mmap_t	spigot_mmap;

static struct dev_ops spigot_ops = {
	{ "spigot", 0, 0 },
	.d_open =	spigot_open,
	.d_close =	spigot_close,
	.d_read =	spigot_read,
	.d_write =	spigot_write,
	.d_ioctl =	spigot_ioctl,
	.d_mmap =	spigot_mmap,
};

static void	spigintr(void *);

static int
spigot_probe(struct isa_device *devp)
{
	struct spigot_softc *ss;
	int status;

	ss = (struct spigot_softc *)&spigot_softc[devp->id_unit];
	ss->flags = 0;
	ss->maddr = 0;
	ss->irq = 0;

	if(devp->id_iobase != 0xad6 || inb(0xad9) == 0xff) 
		status = 0;	/* not found */
	else {
		status = 1;	/* found */
		ss->flags |= ALIVE;
	}

	return(status);
}

static int
spigot_attach(struct isa_device *devp)
{
	int	unit;
	struct	spigot_softc	*ss= &spigot_softc[unit = devp->id_unit];

	devp->id_intr = (inthand2_t *)spigintr;
	ss->maddr = kvtop(devp->id_maddr);
	ss->irq = devp->id_irq;
	make_dev(&spigot_ops, unit, 0, 0, 0644, "spigot%d", unit);
	return 1;
}

static	int
spigot_open(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	int error;
	struct spigot_softc *ss;

	ss = (struct spigot_softc *)&spigot_softc[UNIT(dev)];

	if((ss->flags & ALIVE) == 0)
		return ENXIO;

	if(ss->flags & OPEN)
		return EBUSY;

#if !defined(SPIGOT_UNSECURE)
	/*
	 * Don't allow open() unless the process has sufficient privileges,
	 * since mapping the i/o page and granting i/o privilege would
	 * require sufficient privilege soon and nothing much can be done
	 * without them.
	 */
	error = priv_check_cred(ap->a_cred, PRIV_ROOT, 0);
	if (error != 0)
		return error;
	if (securelevel > 0)
		return EPERM;
#endif

	ss->flags |= OPEN;
	ss->p = 0;
	ss->signal_num = 0;

	return 0;
}

static	int
spigot_close(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct spigot_softc *ss;

	ss = (struct spigot_softc *)&spigot_softc[UNIT(dev)];
	ss->flags &= ~OPEN;
	ss->p = 0;
	ss->signal_num = 0;

	outb(0xad6, 0);

	return 0;
}

static	int
spigot_write(struct dev_write_args *ap)
{
	return ENXIO;
}

static	int
spigot_read(struct dev_read_args *ap)
{
	return ENXIO;
}


static	int
spigot_ioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	caddr_t data = ap->a_data;
	int error;
	struct spigot_softc *ss;
	struct spigot_info *info;

	ss = (struct spigot_softc *)&spigot_softc[UNIT(dev)];
	if (data == NULL)
		return(EINVAL);

	switch(ap->a_cmd){
	case	SPIGOT_SETINT:
		if (*(int *)data < 0 || *(int *)data > _SIG_MAXSIG)
			return (EINVAL);
		ss->p = curproc;
		ss->signal_num = *((int *)data);
		break;
	case	SPIGOT_IOPL_ON:	/* allow access to the IO PAGE */
#if !defined(SPIGOT_UNSECURE)
		error = priv_check_cred(ap->a_cred, PRIV_ROOT, 0);
		if (error != 0)
			return error;
		if (securelevel > 0)
			return EPERM;
#endif
		curthread->td_lwp->lwp_md.md_regs->tf_eflags |= PSL_IOPL;
		break;
	case	SPIGOT_IOPL_OFF: /* deny access to the IO PAGE */
		curthread->td_lwp->lwp_md.md_regs->tf_eflags &= ~PSL_IOPL;
		break;
	case	SPIGOT_GET_INFO:
		info = (struct spigot_info *)data;
		info->maddr  = ss->maddr;
		info->irq = ss->irq;
		break;
	default:
		return ENOTTY;
	}

	return 0;
}

/*
 * Interrupt procedure.
 * Just call a user level interrupt routine.
 */
static void
spigintr(void *arg)
{
	int unit = (int)arg;
	struct spigot_softc *ss;

	ss = (struct spigot_softc *)&spigot_softc[unit];
	if(ss->p && ss->signal_num)
		ksignal(ss->p, ss->signal_num);
}

static	int
spigot_mmap(struct dev_mmap_args *ap)
{
	struct spigot_softc *ss;

	ss = (struct spigot_softc *)&spigot_softc[0];
	if (ap->a_offset != 0) 
		return EINVAL;
	if (ap->a_nprot & PROT_EXEC)
		return EINVAL;
	ap->a_result = i386_btop(ss->maddr);
	return(0);
}

/*
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)cons.c	7.2 (Berkeley) 5/9/91
 * $FreeBSD: src/sys/kern/tty_cons.c,v 1.81.2.4 2001/12/17 18:44:41 guido Exp $
 */

#include "opt_ddb.h"
#include "opt_comconsole.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/reboot.h>
#include <sys/sysctl.h>
#include <sys/tty.h>
#include <sys/uio.h>
#include <sys/msgport.h>
#include <sys/msgport2.h>
#include <sys/device.h>

#include <ddb/ddb.h>

#include <machine/cpu.h>

static d_open_t cnopen;
static d_close_t cnclose;
static d_read_t cnread;
static d_write_t cnwrite;
static d_ioctl_t cnioctl;
static d_kqfilter_t cnkqfilter;

static int cnintercept(struct dev_generic_args *ap);

#define	CDEV_MAJOR	0
static struct dev_ops cn_ops = {
	{ "console", 0, D_TTY },
	.d_open =	cnopen,
	.d_close =	cnclose,
	.d_read =	cnread,
	.d_write =	cnwrite,
	.d_ioctl =	cnioctl,
	.d_kqfilter =	cnkqfilter,
};

static struct dev_ops cn_iops = {
	{ "intercept", 0, D_TTY },
	.d_default =    cnintercept
};

static struct dev_ops *cn_fwd_ops;
static cdev_t	cn_dev;

//XXX: get this shit out! (alexh)
#if 0
SYSCTL_OPAQUE(_machdep, CPU_CONSDEV, consdev, CTLFLAG_RD,
	&cn_udev, sizeof cn_udev, "T,udev_t", "");
#endif

static int cn_mute;

#ifdef BREAK_TO_DEBUGGER
int	break_to_debugger = 1;
#else
int	break_to_debugger = 0;
#endif
TUNABLE_INT("kern.break_to_debugger", &break_to_debugger);
SYSCTL_INT(_kern, OID_AUTO, break_to_debugger, CTLFLAG_RW,
	&break_to_debugger, 0, "");

#ifdef ALT_BREAK_TO_DEBUGGER
int	alt_break_to_debugger = 1;
#else
int	alt_break_to_debugger = 0;
#endif
TUNABLE_INT("kern.alt_break_to_debugger", &alt_break_to_debugger);
SYSCTL_INT(_kern, OID_AUTO, alt_break_to_debugger, CTLFLAG_RW,
	&alt_break_to_debugger, 0, "");

int	cons_unavail = 0;	/* XXX:
				 * physical console not available for
				 * input (i.e., it is in graphics mode)
				 */
int	sysbeep_enable = 1;

static u_char cn_is_open;		/* nonzero if logical console is open */
static int openmode, openflag;		/* how /dev/console was openned */
static cdev_t cn_devfsdev;		/* represents the device private info */
static u_char cn_phys_is_open;		/* nonzero if physical device is open */
static u_char console_pausing;		/* pause after each line during probe */
static char *console_pausestr=
"<pause; press any key to proceed to next line or '.' to end pause mode>";

struct consdev *cn_tab;		/* physical console device info */
struct consdev *gdb_tab;	/* physical gdb debugger device info */

SYSCTL_INT(_kern, OID_AUTO, sysbeep_enable, CTLFLAG_RW, &sysbeep_enable, 0, "");

CONS_DRIVER(cons, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
SET_DECLARE(cons_set, struct consdev);

void
cninit(void)
{
	struct consdev *best_cp, *cp, **list;

	/*
	 * Workaround for token acquisition and releases during the
	 * console init.  For some reason if lwkt_gettoken()'s mpcount
	 * optimization is turned off the console init blows up.  It
	 * might be trying to kprintf() something in the middle of
	 * its init.
	 */
	lwkt_gettoken(&tty_token);

	/*
	 * Check if we should mute the console (for security reasons perhaps)
	 * It can be changes dynamically using sysctl kern.consmute
	 * once we are up and going.
	 *
	 */
	cn_mute = ((boothowto & (RB_MUTE
			|RB_SINGLE
			|RB_VERBOSE
			|RB_ASKNAME
			|RB_CONFIG)) == RB_MUTE);

	/*
	 * Find the first console with the highest priority.
	 */
	best_cp = NULL;
	SET_FOREACH(list, cons_set) {
		cp = *list;
		if (cp->cn_probe == NULL)
			continue;
		(*cp->cn_probe)(cp);
		if (cp->cn_pri > CN_DEAD && cp->cn_probegood &&
		    (best_cp == NULL || cp->cn_pri > best_cp->cn_pri))
			best_cp = cp;
	}

	
	/*
	 * If no console, give up.
	 */
	if (best_cp == NULL) {
		if (cn_tab != NULL && cn_tab->cn_term != NULL)
			(*cn_tab->cn_term)(cn_tab);
		goto done;
	}

	/*
	 * Initialize console, then attach to it.  This ordering allows
	 * debugging using the previous console, if any.
	 */
	(*best_cp->cn_init)(best_cp);
	if (cn_tab != NULL && cn_tab != best_cp) {
		/* Turn off the previous console.  */
		if (cn_tab->cn_term != NULL)
			(*cn_tab->cn_term)(cn_tab);
	}
	if (boothowto & RB_PAUSE)
		console_pausing = 1;
done:
	cn_tab = best_cp;

	/*
	 * We can safely release the token after the init is done.
	 * Also assert that the mpcount is still correct or otherwise
	 * the SMP/AP boot will blow up on us.
	 */
	lwkt_reltoken(&tty_token);
}


/*
 * Hook the open and close functions on the selected device.
 */
void
cninit_finish(void)
{
	if ((cn_tab == NULL) || cn_mute)
		return;
	if (cn_tab->cn_dev == NULL) {
		cn_tab->cn_init_fini(cn_tab);
		if (cn_tab->cn_dev == NULL) {
			kprintf("Unable to hook console! cn_tab %p\n", cn_tab);
			return;
		}
	}

	cn_fwd_ops = dev_ops_intercept(cn_tab->cn_dev, &cn_iops);
	cn_dev = cn_tab->cn_dev;
	console_pausing = 0;
}

static void
cnuninit(void)
{
	if (cn_tab == NULL)
		return;
	if (cn_fwd_ops)
		dev_ops_restore(cn_tab->cn_dev, cn_fwd_ops);
	cn_fwd_ops = NULL;
	cn_dev = NULL;
}


/*
 * User has changed the state of the console muting.
 * This may require us to open or close the device in question.
 */
static int
sysctl_kern_consmute(SYSCTL_HANDLER_ARGS)
{
	int error;
	int ocn_mute;

	ocn_mute = cn_mute;
	error = sysctl_handle_int(oidp, &cn_mute, 0, req);
	if((error == 0) && (cn_tab != NULL) && (req->newptr != NULL)) {
		if(ocn_mute && !cn_mute) {
			/*
			 * going from muted to unmuted.. open the physical dev 
			 * if the console has been openned
			 */
			cninit_finish();
			if (cn_is_open) {
				/* XXX curproc is not what we want really */
				error = dev_dopen(cn_dev, openflag,
						openmode, curproc->p_ucred, NULL);
			}
			/* if it failed, back it out */
			if ( error != 0) cnuninit();
		} else if (!ocn_mute && cn_mute) {
			/*
			 * going from unmuted to muted.. close the physical dev 
			 * if it's only open via /dev/console
			 */
			if (cn_is_open) {
				error = dev_dclose(cn_dev, openflag,
						   openmode, NULL);
			}
			if (error == 0)
				cnuninit();
		}
		if (error != 0) {
			/* 
	 		 * back out the change if there was an error
			 */
			cn_mute = ocn_mute;
		}
	}
	return (error);
}

SYSCTL_PROC(_kern, OID_AUTO, consmute, CTLTYPE_INT|CTLFLAG_RW,
	0, sizeof cn_mute, sysctl_kern_consmute, "I", "");


/*
 * We intercept the OPEN and CLOSE calls on the original device, and
 * forward the rest through.
 */
static int
cnintercept(struct dev_generic_args *ap)
{
	int error;

	if (ap->a_desc == &dev_open_desc) {
		error = cnopen((struct dev_open_args *)ap);
	} else if (ap->a_desc == &dev_close_desc) {
		error = cnclose((struct dev_close_args *)ap);
	} else if (cn_fwd_ops) {
		error = dev_doperate_ops(cn_fwd_ops, ap);
	} else {
		error = ENXIO;
	}
	return (error);
}

/*
 * cnopen() is called as an intercept function (dev will be that of the
 * actual physical device representing our console), and also called from
 * the muting code and from the /dev/console switch (dev will have the
 * console's cdevsw).
 */
static int
cnopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	int flag = ap->a_oflags;
	int mode = ap->a_devtype;
	cdev_t cndev;
	cdev_t physdev;
	int retval = 0;

	if (cn_tab == NULL || cn_fwd_ops == NULL)
		return (0);

	cndev = cn_tab->cn_dev;		/* actual physical device */
	physdev = (dev == cn_devfsdev) ? cndev : dev;

	/*
	 * If mute is active, then non console opens don't get here
	 * so we don't need to check for that. They bypass this and go
	 * straight to the device.
	 *
	 * It is important to note that due to our intercept and the fact
	 * that we might be called via the original (intercepted) device,
	 * the original device's ops may point to us, so to avoid an
	 * infinite recursion we have to forward through cn_fwd_ops.
	 * This is a severe hack that really needs to be fixed XXX.
	 *
	 * XXX at the moment we assume that the port forwarding function
	 * is synchronous for open.
	 */
	if (!cn_mute) {
		ap->a_head.a_dev = physdev;
		retval = dev_doperate_ops(cn_fwd_ops, &ap->a_head);
	}
	if (retval == 0) {
		/*
		 * check if we openned it via /dev/console or
		 * via the physical entry (e.g. /dev/sio0).
		 */
		if (dev == cndev) {
			cn_phys_is_open = 1;
		} else if (physdev == cndev) {
			openmode = mode;
			openflag = flag;
			cn_is_open = 1;
		}
		dev->si_tty = cndev->si_tty;
	}
	return (retval);
}

/*
 * cnclose() is called as a port intercept function (dev will be that of the
 * actual physical device representing our console), and also called from
 * the muting code and from the /dev/console switch (dev will have the
 * console's cdevsw).
 */
static int
cnclose(struct dev_close_args *ap)
{
	struct tty *cn_tp;
	cdev_t cndev;
	cdev_t physdev;
	cdev_t dev = ap->a_head.a_dev;

	if (cn_tab == NULL || cn_fwd_ops == NULL)
		return(0);
	cndev = cn_tab->cn_dev;
	cn_tp = cndev->si_tty;
	physdev = (dev == cn_devfsdev) ? cndev : dev;

	/*
	 * act appropriatly depending on whether it's /dev/console
	 * or the pysical device (e.g. /dev/sio) that's being closed.
	 * in either case, don't actually close the device unless
	 * both are closed.
	 */
	if (dev == cndev) {
		/* the physical device is about to be closed */
		cn_phys_is_open = 0;
		if (cn_is_open) {
			if (cn_tp) {
				/* perform a ttyhalfclose() */
				/* reset session and proc group */
				ttyclearsession(cn_tp);
			}
			return(0);
		}
	} else if (physdev == cndev) {
		/* the logical console is about to be closed */
		cn_is_open = 0;
		if (cn_phys_is_open)
			return(0);
		dev = cndev;
	}
	if (cn_fwd_ops) {
		ap->a_head.a_dev = dev;
		return (dev_doperate_ops(cn_fwd_ops, &ap->a_head));
	}
	return (0);
}

/*
 * The following functions are dispatched solely from the /dev/console
 * device.  Their job is primarily to forward the request through.
 * If the console is not attached to anything then write()'s are sunk
 * to null and reads return 0 (mostly).
 */
static int
cnread(struct dev_read_args *ap)
{
	if (cn_tab == NULL || cn_fwd_ops == NULL)
		return (0);
	ap->a_head.a_dev = cn_tab->cn_dev;
	return (dev_doperate(&ap->a_head));
}

static int
cnwrite(struct dev_write_args *ap)
{
	struct uio *uio = ap->a_uio;
	cdev_t dev;

	if (cn_tab == NULL || cn_fwd_ops == NULL) {
		uio->uio_resid = 0; /* dump the data */
		return (0);
	}
	if (constty)
		dev = constty->t_dev;
	else
		dev = cn_tab->cn_dev;
	log_console(uio);
	ap->a_head.a_dev = dev;
	return (dev_doperate(&ap->a_head));
}

static int
cnioctl(struct dev_ioctl_args *ap)
{
	int error;

	if (cn_tab == NULL || cn_fwd_ops == NULL)
		return (0);
	KKASSERT(curproc != NULL);
	/*
	 * Superuser can always use this to wrest control of console
	 * output from the "virtual" console.
	 */
	if (ap->a_cmd == TIOCCONS && constty) {
		if (ap->a_cred) {
			error = priv_check_cred(ap->a_cred, PRIV_ROOT, 0);
			if (error)
				return (error);
		}
		constty = NULL;
		return (0);
	}
	ap->a_head.a_dev = cn_tab->cn_dev;
	return (dev_doperate(&ap->a_head));
}

static int
cnkqfilter(struct dev_kqfilter_args *ap)
{
	if ((cn_tab == NULL) || cn_mute || cn_fwd_ops == NULL)
		return (1);
	ap->a_head.a_dev = cn_tab->cn_dev;
	return (dev_doperate(&ap->a_head));
}

/*
 * These synchronous functions are primarily used the kernel needs to 
 * access the keyboard (e.g. when running the debugger), or output data
 * directly to the console.
 */
int
cngetc(void)
{
	int c;
	if ((cn_tab == NULL) || cn_mute)
		return (-1);
	c = (*cn_tab->cn_getc)(cn_tab->cn_private);
	if (c == '\r') c = '\n'; /* console input is always ICRNL */
	return (c);
}

int
cncheckc(void)
{
	if ((cn_tab == NULL) || cn_mute)
		return (-1);
	return ((*cn_tab->cn_checkc)(cn_tab->cn_private));
}

void
cnputc(int c)
{
	char *cp;

	if ((cn_tab == NULL) || cn_mute)
		return;
	if (c) {
		if (c == '\n')
			(*cn_tab->cn_putc)(cn_tab->cn_private, '\r');
		(*cn_tab->cn_putc)(cn_tab->cn_private, c);
#ifdef DDB
		if (console_pausing && !db_active && (c == '\n')) {
#else
		if (console_pausing && (c == '\n')) {
#endif
			for(cp=console_pausestr; *cp != '\0'; cp++)
			    (*cn_tab->cn_putc)(cn_tab->cn_private, *cp);
			if (cngetc() == '.')
				console_pausing = 0;
			(*cn_tab->cn_putc)(cn_tab->cn_private, '\r');
			for(cp=console_pausestr; *cp != '\0'; cp++)
			    (*cn_tab->cn_putc)(cn_tab->cn_private, ' ');
			(*cn_tab->cn_putc)(cn_tab->cn_private, '\r');
		}
	}
}

void
cndbctl(int on)
{
	static int refcount;

	if (cn_tab == NULL)
		return;
	if (!on)
		refcount--;
	if (refcount == 0 && cn_tab->cn_dbctl != NULL)
		(*cn_tab->cn_dbctl)(cn_tab->cn_private, on);
	if (on)
		refcount++;
}

static void
cn_drvinit(void *unused)
{
	cn_devfsdev = make_only_devfs_dev(&cn_ops, 0, UID_ROOT, GID_WHEEL,
					  0600, "console");
}

SYSINIT(cndev, SI_SUB_DRIVERS, SI_ORDER_MIDDLE + CDEV_MAJOR, cn_drvinit, NULL);

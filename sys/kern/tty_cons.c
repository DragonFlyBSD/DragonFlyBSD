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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * $DragonFly: src/sys/kern/tty_cons.c,v 1.14 2004/09/13 16:22:36 dillon Exp $
 */

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/reboot.h>
#include <sys/sysctl.h>
#include <sys/tty.h>
#include <sys/uio.h>
#include <sys/msgport.h>
#include <sys/msgport2.h>
#include <sys/device.h>

#include <ddb/ddb.h>

#include <machine/cpu.h>

static int cnopen(struct cdevmsg_open *msg);
static int cnclose(struct cdevmsg_close *msg);
static int cnread(struct cdevmsg_read *msg);
static int cnwrite(struct cdevmsg_write *msg);
static int cnioctl(struct cdevmsg_ioctl *msg);
static int cnpoll(struct cdevmsg_poll *msg);
static int cnkqfilter(struct cdevmsg_kqfilter *msg);

static int console_putport(lwkt_port_t port, lwkt_msg_t lmsg);
static int console_interceptport(lwkt_port_t port, lwkt_msg_t lmsg);

static struct lwkt_port	cn_port;	/* console device port */
static struct lwkt_port	cn_iport;	/* intercept port */

#define	CDEV_MAJOR	0
static struct cdevsw cn_cdevsw = {
	/* name */	"console",
	/* maj */	CDEV_MAJOR,
	/* flags */	D_TTY | D_KQFILTER,
	/* port */	&cn_port,
	/* clone */	NULL
};

static dev_t	cn_dev_t;
static udev_t	cn_udev_t;
SYSCTL_OPAQUE(_machdep, CPU_CONSDEV, consdev, CTLFLAG_RD,
	&cn_udev_t, sizeof cn_udev_t, "T,dev_t", "");

static int cn_mute;

int	cons_unavail = 0;	/* XXX:
				 * physical console not available for
				 * input (i.e., it is in graphics mode)
				 */

static u_char cn_is_open;		/* nonzero if logical console is open */
static int openmode, openflag;		/* how /dev/console was openned */
static dev_t cn_devfsdev;		/* represents the device private info */
static u_char cn_phys_is_open;		/* nonzero if physical device is open */
       struct consdev *cn_tab;		/* physical console device info */
static u_char console_pausing;		/* pause after each line during probe */
static char *console_pausestr=
"<pause; press any key to proceed to next line or '.' to end pause mode>";

static lwkt_port_t	cn_fwd_port;

CONS_DRIVER(cons, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
SET_DECLARE(cons_set, struct consdev);

void
cninit()
{
	struct consdev *best_cp, *cp, **list;

	/*
	 * Our port intercept
	 */
	lwkt_initport(&cn_port, NULL);
	cn_port.mp_putport = console_putport;
	lwkt_initport(&cn_iport, NULL);
	cn_iport.mp_putport = console_interceptport;

	/*
	 * Find the first console with the highest priority.
	 */
	best_cp = NULL;
	SET_FOREACH(list, cons_set) {
		cp = *list;
		if (cp->cn_probe == NULL)
			continue;
		(*cp->cn_probe)(cp);
		if (cp->cn_pri > CN_DEAD &&
		    (best_cp == NULL || cp->cn_pri > best_cp->cn_pri))
			best_cp = cp;
	}

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
	 * If no console, give up.
	 */
	if (best_cp == NULL) {
		if (cn_tab != NULL && cn_tab->cn_term != NULL)
			(*cn_tab->cn_term)(cn_tab);
		cn_tab = best_cp;
		return;
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
	cn_tab = best_cp;
}

/*
 * Hook the open and close functions on the selected device.
 */
void
cninit_finish()
{
	if ((cn_tab == NULL) || cn_mute)
		return;

	/*
	 * Hook the open and close functions.  XXX bad hack.
	 */
	if (dev_is_good(cn_tab->cn_dev))
		cn_fwd_port = cdevsw_dev_override(cn_tab->cn_dev, &cn_iport);
	cn_dev_t = cn_tab->cn_dev;
	cn_udev_t = dev2udev(cn_dev_t);
	console_pausing = 0;
}

static void
cnuninit(void)
{
	if (cn_tab == NULL)
		return;

	/*
	 * Unhook the open and close functions.  XXX bad hack
	 */
	if (cn_fwd_port)
		cdevsw_dev_override(cn_tab->cn_dev, cn_fwd_port);
	cn_fwd_port = NULL;
	cn_dev_t = NODEV;
	cn_udev_t = NOUDEV;
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
				error = dev_dopen(cn_dev_t, openflag,
						openmode, curthread);
			}
			/* if it failed, back it out */
			if ( error != 0) cnuninit();
		} else if (!ocn_mute && cn_mute) {
			/*
			 * going from unmuted to muted.. close the physical dev 
			 * if it's only open via /dev/console
			 */
			if (cn_is_open) {
				error = dev_dclose(cn_dev_t, openflag,
						openmode, curthread);
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
console_interceptport(lwkt_port_t port, lwkt_msg_t lmsg)
{
	cdevallmsg_t msg = (cdevallmsg_t)lmsg;
	int error;

	switch(msg->am_lmsg.ms_cmd.cm_op) {
	case CDEV_CMD_OPEN:
		error = cnopen(&msg->am_open);
		break;
	case CDEV_CMD_CLOSE:
		error = cnclose(&msg->am_close);
		break;
	default:
		error = lwkt_forwardmsg(cn_fwd_port, &msg->am_lmsg);
		break;
	}
	return(error);
}

/*
 * This is the port handler for /dev/console.  These functions will basically
 * past the request through to the actual physical device representing the
 * console. 
 *
 * Note, however, that cnopen() and cnclose() are also called from the mute
 * code and the intercept code.
 */
static int
console_putport(lwkt_port_t port, lwkt_msg_t lmsg)
{
	cdevallmsg_t msg = (cdevallmsg_t)lmsg;
	int error;

	switch(msg->am_lmsg.ms_cmd.cm_op) {
	case CDEV_CMD_OPEN:
		error = cnopen(&msg->am_open);
		break;
	case CDEV_CMD_CLOSE:
		error = cnclose(&msg->am_close);
		break;
	case CDEV_CMD_STRATEGY:
		nostrategy(msg->am_strategy.bp);
		error = 0;
		break;
	case CDEV_CMD_IOCTL:
		error = cnioctl(&msg->am_ioctl);
		break;
	case CDEV_CMD_DUMP:
		error = nodump(msg->am_dump.msg.dev, 0, 0, 0);
		break;
	case CDEV_CMD_PSIZE:
		error = nopsize(msg->am_psize.msg.dev);
		break;
	case CDEV_CMD_READ:
		error = cnread(&msg->am_read);
		break;
	case CDEV_CMD_WRITE:
		error = cnwrite(&msg->am_write);
		break;
	case CDEV_CMD_POLL:
		error = cnpoll(&msg->am_poll);
		break;
	case CDEV_CMD_KQFILTER:
		error = cnkqfilter(&msg->am_kqfilter);
		break;
	case CDEV_CMD_MMAP:
		error = nommap(msg->am_mmap.msg.dev,
				msg->am_mmap.offset,
				msg->am_mmap.nprot);
		break;
	default:
		error = ENODEV;
		break;
	}
	return(error);
}

/*
 * cnopen() is called as a port intercept function (dev will be that of the
 * actual physical device representing our console), and also called from
 * the muting code and from the /dev/console switch (dev will have the
 * console's cdevsw).
 */
static int
cnopen(struct cdevmsg_open *msg)
{
	dev_t dev = msg->msg.dev;
	int flag = msg->oflags;
	int mode = msg->devtype;
	dev_t cndev, physdev;
	int retval = 0;

	if (cn_tab == NULL || cn_fwd_port == NULL)
		return (0);
	cndev = cn_tab->cn_dev;
	physdev = (major(dev) == major(cndev) ? dev : cndev);

	/*
	 * If mute is active, then non console opens don't get here
	 * so we don't need to check for that. They bypass this and go
	 * straight to the device.
	 *
	 * XXX at the moment we assume that the port forwarding function
	 * is synchronous for open.
	 */
	if (!cn_mute) {
		msg->msg.dev = physdev;
		retval = lwkt_forwardmsg(cn_fwd_port, &msg->msg.msg);
	}
	if (retval == 0) {
		/* 
		 * check if we openned it via /dev/console or 
		 * via the physical entry (e.g. /dev/sio0).
		 */
		if (dev == cndev)
			cn_phys_is_open = 1;
		else if (physdev == cndev) {
			openmode = mode;
			openflag = flag;
			cn_is_open = 1;
		}
		dev->si_tty = physdev->si_tty;
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
cnclose(struct cdevmsg_close *msg)
{
	dev_t dev = msg->msg.dev;
	dev_t cndev;
	struct tty *cn_tp;

	if (cn_tab == NULL || cn_fwd_port == NULL)
		return (0);
	cndev = cn_tab->cn_dev;
	cn_tp = cndev->si_tty;
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
			return (0);
		}
	} else if (major(dev) != major(cndev)) {
		/* the logical console is about to be closed */
		cn_is_open = 0;
		if (cn_phys_is_open)
			return (0);
		dev = cndev;
	}
	if (cn_fwd_port) {
		msg->msg.dev = dev;
		return(lwkt_forwardmsg(cn_fwd_port, &msg->msg.msg));
	}
	return (0);
}

/*
 * The following functions are dispatched solely from the /dev/console
 * port switch.  Their job is primarily to forward the request through.
 * If the console is not attached to anything then write()'s are sunk
 * to null and reads return 0 (mostly).
 */
static int
cnread(struct cdevmsg_read *msg)
{
	if (cn_tab == NULL || cn_fwd_port == NULL)
		return (0);
	msg->msg.dev = cn_tab->cn_dev;
	return(lwkt_forwardmsg(cn_fwd_port, &msg->msg.msg));
}

static int
cnwrite(struct cdevmsg_write *msg)
{
	struct uio *uio = msg->uio;
	dev_t dev;

	if (cn_tab == NULL || cn_fwd_port == NULL) {
		uio->uio_resid = 0; /* dump the data */
		return (0);
	}
	if (constty)
		dev = constty->t_dev;
	else
		dev = cn_tab->cn_dev;
	log_console(uio);
	msg->msg.dev = dev;
	return(lwkt_forwardmsg(cn_fwd_port, &msg->msg.msg));
}

static int
cnioctl(struct cdevmsg_ioctl *msg)
{
	u_long cmd = msg->cmd;
	int error;

	if (cn_tab == NULL || cn_fwd_port == NULL)
		return (0);
	KKASSERT(msg->td->td_proc != NULL);
	/*
	 * Superuser can always use this to wrest control of console
	 * output from the "virtual" console.
	 */
	if (cmd == TIOCCONS && constty) {
		error = suser(msg->td);
		if (error)
			return (error);
		constty = NULL;
		return (0);
	}
	msg->msg.dev = cn_tab->cn_dev;
	return(lwkt_forwardmsg(cn_fwd_port, &msg->msg.msg));
}

static int
cnpoll(struct cdevmsg_poll *msg)
{
	if ((cn_tab == NULL) || cn_mute || cn_fwd_port == NULL)
		return (1);
	msg->msg.dev = cn_tab->cn_dev;
	return(lwkt_forwardmsg(cn_fwd_port, &msg->msg.msg));
}

static int
cnkqfilter(struct cdevmsg_kqfilter *msg)
{
	if ((cn_tab == NULL) || cn_mute || cn_fwd_port == NULL)
		return (1);
	msg->msg.dev = cn_tab->cn_dev;
	return(lwkt_forwardmsg(cn_fwd_port, &msg->msg.msg));
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
	c = (*cn_tab->cn_getc)(cn_tab->cn_dev);
	if (c == '\r') c = '\n'; /* console input is always ICRNL */
	return (c);
}

int
cncheckc(void)
{
	if ((cn_tab == NULL) || cn_mute)
		return (-1);
	return ((*cn_tab->cn_checkc)(cn_tab->cn_dev));
}

void
cnputc(int c)
{
	char *cp;

	if ((cn_tab == NULL) || cn_mute)
		return;
	if (c) {
		if (c == '\n')
			(*cn_tab->cn_putc)(cn_tab->cn_dev, '\r');
		(*cn_tab->cn_putc)(cn_tab->cn_dev, c);
#ifdef DDB
		if (console_pausing && !db_active && (c == '\n')) {
#else
		if (console_pausing && (c == '\n')) {
#endif
			for(cp=console_pausestr; *cp != '\0'; cp++)
			    (*cn_tab->cn_putc)(cn_tab->cn_dev, *cp);
			if (cngetc() == '.')
				console_pausing = 0;
			(*cn_tab->cn_putc)(cn_tab->cn_dev, '\r');
			for(cp=console_pausestr; *cp != '\0'; cp++)
			    (*cn_tab->cn_putc)(cn_tab->cn_dev, ' ');
			(*cn_tab->cn_putc)(cn_tab->cn_dev, '\r');
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
		(*cn_tab->cn_dbctl)(cn_tab->cn_dev, on);
	if (on)
		refcount++;
}

static void
cn_drvinit(void *unused)
{
	cdevsw_add(&cn_cdevsw, 0, 0);
	cn_devfsdev = make_dev(&cn_cdevsw, 0, UID_ROOT, GID_WHEEL,
				0600, "console");
}

SYSINIT(cndev,SI_SUB_DRIVERS,SI_ORDER_MIDDLE+CDEV_MAJOR,cn_drvinit,NULL)

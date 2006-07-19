/*
 * 
 *             Coda: an Experimental Distributed File System
 *                              Release 3.1
 * 
 *           Copyright (c) 1987-1998 Carnegie Mellon University
 *                          All Rights Reserved
 * 
 * Permission  to  use, copy, modify and distribute this software and its
 * documentation is hereby granted,  provided  that  both  the  copyright
 * notice  and  this  permission  notice  appear  in  all  copies  of the
 * software, derivative works or  modified  versions,  and  any  portions
 * thereof, and that both notices appear in supporting documentation, and
 * that credit is given to Carnegie Mellon University  in  all  documents
 * and publicity pertaining to direct or indirect use of this code or its
 * derivatives.
 * 
 * CODA IS AN EXPERIMENTAL SOFTWARE SYSTEM AND IS  KNOWN  TO  HAVE  BUGS,
 * SOME  OF  WHICH MAY HAVE SERIOUS CONSEQUENCES.  CARNEGIE MELLON ALLOWS
 * FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION.   CARNEGIE  MELLON
 * DISCLAIMS  ANY  LIABILITY  OF  ANY  KIND  FOR  ANY  DAMAGES WHATSOEVER
 * RESULTING DIRECTLY OR INDIRECTLY FROM THE USE OF THIS SOFTWARE  OR  OF
 * ANY DERIVATIVE WORK.
 * 
 * Carnegie  Mellon  encourages  users  of  this  software  to return any
 * improvements or extensions that  they  make,  and  to  grant  Carnegie
 * Mellon the rights to redistribute these changes without encumbrance.
 * 
 * 	@(#) src/sys/coda/coda_fbsd.cr,v 1.1.1.1 1998/08/29 21:14:52 rvb Exp $
 * $FreeBSD: src/sys/coda/coda_fbsd.c,v 1.18 1999/09/25 18:23:43 phk Exp $
 * $DragonFly: src/sys/vfs/coda/Attic/coda_fbsd.c,v 1.12 2006/07/19 05:59:54 dillon Exp $
 * 
 */

#include "use_vcoda.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/fcntl.h>
#include <sys/ucred.h>
#include <sys/vnode.h>
#include <sys/conf.h>

#include <vm/vm.h>
#include <vm/vnode_pager.h>

#include "coda.h"
#include "cnode.h"
#include "coda_vnops.h"
#include "coda_psdev.h"

/* 
   From: "Jordan K. Hubbard" <jkh@time.cdrom.com>
   Subject: Re: New 3.0 SNAPshot CDROM about ready for production.. 
   To: "Robert.V.Baron" <rvb@GLUCK.CODA.CS.CMU.EDU>
   Date: Fri, 20 Feb 1998 15:57:01 -0800

   > Also I need a character device major number. (and might want to reserve
   > a block of 10 syscalls.)

   Just one char device number?  No block devices?  Very well, cdev 93 is yours!
*/

#define VC_DEV_NO      93

static struct cdevsw codadevsw = {
	/* name */	"Coda",
	/* maj */	VC_DEV_NO,
	/* flags */	0,
	/* port */      NULL,
	/* clone */	NULL,

	/* open */	vc_nb_open,
	/* close */	vc_nb_close,
	/* read */	vc_nb_read,
	/* write */	vc_nb_write,
	/* ioctl */	vc_nb_ioctl,
	/* poll */	vc_nb_poll,
	/* mmap */	nommap,
	/* strategy */	nostrategy,
	/* dump */	nodump,
	/* psize */	nopsize
};

int     vcdebug = 1;
#define VCDEBUG if (vcdebug) printf

static int
codadev_modevent(module_t mod, int type, void *data)
{
	switch (type) {
	case MOD_LOAD:
		cdevsw_add(&codadevsw, 0, 0);
		break;
	case MOD_UNLOAD:
		cdevsw_remove(&codadevsw, 0, 0);
		break;
	default:
		break;
	}
	return 0;
}
static moduledata_t codadev_mod = {
	"codadev",
	codadev_modevent,
	NULL
};
DECLARE_MODULE(codadev, codadev_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE+VC_DEV_NO);

int
coda_fbsd_getpages(struct vop_getpages_args *ap)
{
    int ret = 0;

    /* ??? a_offset */
    ret = vnode_pager_generic_getpages(ap->a_vp, ap->a_m, ap->a_count,
				       ap->a_reqpage);
    return ret;
}

int
coda_fbsd_putpages(struct vop_putpages_args *ap)
{
	/*??? a_offset */
	return vnode_pager_generic_putpages(ap->a_vp, ap->a_m, ap->a_count,
		ap->a_sync, ap->a_rtvals);
}


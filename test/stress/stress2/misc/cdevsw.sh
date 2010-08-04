#!/bin/sh

#
# Copyright (c) 2008 Peter Holm <pho@FreeBSD.org>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#

# Test scenario by kib@freebsd.org

# Test of patch for Giant trick in cdevsw

exit 	# Test moved to fpclone*.sh

[ `id -u ` -ne 0 ] && echo "Must be root!" && exit 1

. ../default.cfg

odir=`pwd`
dir=$RUNDIR/tclone
[ ! -d $dir ] && mkdir -p $dir

cd $dir
cat > Makefile <<EOF
KMOD= tclone
SRCS= tclone.c

.include <bsd.kmod.mk>
EOF

sed '1,/^EOF2/d' < $odir/$0 > tclone.c
make
kldload $dir/tclone.ko

cd $odir
dd if=/dev/tclone bs=1m count=5k > /dev/null 2>&1 &

export runRUNTIME=2m
cd /home/pho/stress2; ./run.sh pty.cfg

kldstat
kldunload $dir/tclone.ko
rm -rf $dir
exit

EOF2
/* $FreeBSD$ */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/malloc.h>

static d_open_t		tclone_open;
static d_close_t	tclone_close;
static d_read_t		tclone_read;

static struct cdevsw tclone_cdevsw = {
	.d_open =	tclone_open,
	.d_close =	tclone_close,
	.d_read =	tclone_read,
	.d_name =	"tclone",
	.d_version =	D_VERSION,
	.d_flags =	D_TRACKCLOSE|D_NEEDGIANT
};

static eventhandler_tag tclone_ehtag;
static struct clonedevs *tclone_clones;

MALLOC_DEFINE(M_TCLONESC, "tclone memory", "tclone memory");

struct tclone_sc
{
	int pos;
};

static void
tclone_clone(void *arg, struct ucred *cred,
	     char *name, int namelen, struct cdev **dev)
{
	int i, clone;

	if (*dev != NULL)
		return;
	if (strcmp(name, "tclone") != 0)
		return;

	clone = 0;
	do {
		i = clone_create(&tclone_clones, &tclone_cdevsw,
				 &clone, dev, 0);
		if (i == 0)
			clone++;
	} while ((clone <= CLONE_UNITMASK) && (i == 0));

	if ((i != 0) && (clone <= CLONE_UNITMASK)) {
		*dev = make_dev_credf(MAKEDEV_REF,
		    &tclone_cdevsw, unit2minor(clone),
		    cred, UID_ROOT, GID_WHEEL, 0666,
		    "tclone.%u", clone);
		if (*dev != NULL) {
			(*dev)->si_flags |= SI_CHEAPCLONE;
			(*dev)->si_drv1 = (void *)1;
		}
	}
}

static int
tclone_open(struct cdev *dev, int oflags, int devtype, d_thread_t *td)
{
	int status;

	if (!dev->si_drv2) {
		/* only allow one open() of this file */
		dev->si_drv2 = malloc(sizeof(struct tclone_sc), M_TCLONESC,
		    M_WAITOK | M_ZERO);
		status = 0;
	} else
		status = EBUSY;

	if (status == 0) {
		/* XXX Fix me? (clear of SI_CHEAPCLONE) */
		dev->si_flags &= ~SI_CHEAPCLONE;
	}

	return (status);
}

static int
tclone_close(struct cdev *dev, int fflag, int devtype, d_thread_t *td)
{
	void *x;

	x = dev->si_drv2;
	dev->si_drv2 = &tclone_cdevsw;
	if (x != &tclone_cdevsw)
		free(x, M_TCLONESC);
	destroy_dev_sched(dev);
	return (0);
}

static char rdata[] = "tclone sample data string\n";

static int
tclone_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct tclone_sc *sc;
	int rv, amnt;

	sc = dev->si_drv2;
	rv = 0;
	while (uio->uio_resid > 0) {
		amnt = MIN(uio->uio_resid, sizeof(rdata) - sc->pos);
		rv = uiomove(rdata + sc->pos, amnt, uio);
		if (rv != 0)
			break;
		sc->pos += amnt;
		sc->pos %= sizeof(rdata);
	}
	return (rv);
}

static int
tclone_modevent(module_t mod, int what, void *arg)
{
	switch (what) {
        case MOD_LOAD:
		clone_setup(&tclone_clones);
		tclone_ehtag = EVENTHANDLER_REGISTER(dev_clone,
						     tclone_clone, 0, 0);
		if (tclone_ehtag == NULL)
			return ENOMEM;
		return(0);

        case MOD_UNLOAD:
		EVENTHANDLER_DEREGISTER(dev_clone, tclone_ehtag);
		drain_dev_clone_events();
		clone_cleanup(&tclone_clones);
		destroy_dev_drain(&tclone_cdevsw);
		return (0);
        default:
		break;
	}

	return (0);
}

moduledata_t tclone_mdata = {
	"tclone",
	tclone_modevent,
	NULL
};

DECLARE_MODULE(tclone, tclone_mdata, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(tclone, 1);

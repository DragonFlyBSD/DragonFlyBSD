/*-
 * Copyright (c) 2001 Cameron Grant <cg@freebsd.org>
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
 * $FreeBSD: src/sys/dev/sound/pcm/sndstat.c,v 1.20.2.2 2005/12/30 19:55:54 netchild Exp $
 */

#include <dev/sound/pcm/sound.h>
#include <dev/sound/pcm/vchan.h>
#ifdef	USING_MUTEX
#include <sys/lock.h>
#endif


#define	SS_TYPE_MODULE		0
#define	SS_TYPE_FIRST		1
#define	SS_TYPE_PCM		1
#define	SS_TYPE_MIDI		2
#define	SS_TYPE_SEQUENCER	3
#define	SS_TYPE_LAST		3

static d_open_t sndstat_open;
static d_close_t sndstat_close;
static d_read_t sndstat_read;

static struct dev_ops sndstat_cdevsw = {
	{ "sndstat", 0, 0 },
	/* .d_flags =	D_NEEDGIANT, */
	.d_open =	sndstat_open,
	.d_close =	sndstat_close,
	.d_read =	sndstat_read,
};

struct sndstat_entry {
	SLIST_ENTRY(sndstat_entry) link;
	device_t dev;
	char *str;
	sndstat_handler handler;
	int type, unit;
};

#ifdef	USING_MUTEX
static struct lock sndstat_lock;
#endif
static struct sbuf sndstat_sbuf;
static int sndstat_isopen = 0;
static int sndstat_bufptr;
static int sndstat_maxunit = -1;
static int sndstat_files = 0;

static SLIST_HEAD(, sndstat_entry) sndstat_devlist = SLIST_HEAD_INITIALIZER(none);

static int sndstat_verbose = 1;
#ifdef	USING_MUTEX
TUNABLE_INT("hw.snd.verbose", &sndstat_verbose);
#else
TUNABLE_INT_DECL("hw.snd.verbose", 1, sndstat_verbose);
#endif

static int sndstat_prepare(struct sbuf *s);

static int
sysctl_hw_sndverbose(SYSCTL_HANDLER_ARGS)
{
	int error, verbose;

	verbose = sndstat_verbose;
	error = sysctl_handle_int(oidp, &verbose, 0, req);
	if (error == 0 && req->newptr != NULL) {
		lockmgr(&sndstat_lock, LK_EXCLUSIVE);
		if (verbose < 0 || verbose > 3)
			error = EINVAL;
		else
			sndstat_verbose = verbose;
		lockmgr(&sndstat_lock, LK_RELEASE);
	}
	return error;
}
SYSCTL_PROC(_hw_snd, OID_AUTO, verbose, CTLTYPE_INT | CTLFLAG_RW,
            0, sizeof(int), sysctl_hw_sndverbose, "I", "");

static int
sndstat_open(struct dev_open_args *ap)
{
	int error;

	lockmgr(&sndstat_lock, LK_EXCLUSIVE);
	if (sndstat_isopen) {
		lockmgr(&sndstat_lock, LK_RELEASE);
		return EBUSY;
	}
	sndstat_isopen = 1;
	lockmgr(&sndstat_lock, LK_RELEASE);
	if (sbuf_new(&sndstat_sbuf, NULL, 4096, 0) == NULL) {
		error = ENXIO;
		goto out;
	}
	sndstat_bufptr = 0;
	error = (sndstat_prepare(&sndstat_sbuf) > 0) ? 0 : ENOMEM;
out:
	if (error) {
		lockmgr(&sndstat_lock, LK_EXCLUSIVE);
		sndstat_isopen = 0;
		lockmgr(&sndstat_lock, LK_RELEASE);
	}
	return (error);
}

static int
sndstat_close(struct dev_close_args *ap)
{
	lockmgr(&sndstat_lock, LK_EXCLUSIVE);
	if (!sndstat_isopen) {
		lockmgr(&sndstat_lock, LK_RELEASE);
		return EBADF;
	}
	sbuf_delete(&sndstat_sbuf);
	sndstat_isopen = 0;

	lockmgr(&sndstat_lock, LK_RELEASE);
	return 0;
}

static int
sndstat_read(struct dev_read_args *ap)
{
	struct uio *buf = ap->a_uio;
	int l, err;

	lockmgr(&sndstat_lock, LK_EXCLUSIVE);
	if (!sndstat_isopen) {
		lockmgr(&sndstat_lock, LK_RELEASE);
		return EBADF;
	}
	l = (int)szmin(buf->uio_resid,
		       sbuf_len(&sndstat_sbuf) - sndstat_bufptr);
	if (l > 0) {
		err = uiomove(sbuf_data(&sndstat_sbuf) + sndstat_bufptr,
			      l, buf);
	} else {
		err = 0;
	}
	sndstat_bufptr += l;

	lockmgr(&sndstat_lock, LK_RELEASE);
	return err;
}

/************************************************************************/

static struct sndstat_entry *
sndstat_find(int type, int unit)
{
	struct sndstat_entry *ent;

	SLIST_FOREACH(ent, &sndstat_devlist, link) {
		if (ent->type == type && ent->unit == unit)
			return ent;
	}

	return NULL;
}

int
sndstat_acquire(void)
{
	lockmgr(&sndstat_lock, LK_EXCLUSIVE);
	if (sndstat_isopen) {
		lockmgr(&sndstat_lock, LK_RELEASE);
		return EBUSY;
	}
	sndstat_isopen = 1;
	lockmgr(&sndstat_lock, LK_RELEASE);
	return 0;
}

int
sndstat_release(void)
{
	lockmgr(&sndstat_lock, LK_EXCLUSIVE);
	if (!sndstat_isopen) {
		lockmgr(&sndstat_lock, LK_RELEASE);
		return EBADF;
	}
	sndstat_isopen = 0;
	lockmgr(&sndstat_lock, LK_RELEASE);
	return 0;
}

int
sndstat_register(device_t dev, char *str, sndstat_handler handler)
{
	struct sndstat_entry *ent;
	const char *devtype;
	int type, unit;

	if (dev) {
		unit = device_get_unit(dev);
		devtype = device_get_name(dev);
		if (!strcmp(devtype, "pcm"))
			type = SS_TYPE_PCM;
		else if (!strcmp(devtype, "midi"))
			type = SS_TYPE_MIDI;
		else if (!strcmp(devtype, "sequencer"))
			type = SS_TYPE_SEQUENCER;
		else
			return EINVAL;
	} else {
		type = SS_TYPE_MODULE;
		unit = -1;
	}

	ent = kmalloc(sizeof *ent, M_DEVBUF, M_ZERO | M_WAITOK);

	ent->dev = dev;
	ent->str = str;
	ent->type = type;
	ent->unit = unit;
	ent->handler = handler;

	lockmgr(&sndstat_lock, LK_EXCLUSIVE);
	SLIST_INSERT_HEAD(&sndstat_devlist, ent, link);
	if (type == SS_TYPE_MODULE)
		sndstat_files++;
	sndstat_maxunit = (unit > sndstat_maxunit)? unit : sndstat_maxunit;
	lockmgr(&sndstat_lock, LK_RELEASE);

	return 0;
}

int
sndstat_registerfile(char *str)
{
	return sndstat_register(NULL, str, NULL);
}

int
sndstat_unregister(device_t dev)
{
	struct sndstat_entry *ent;

	lockmgr(&sndstat_lock, LK_EXCLUSIVE);
	SLIST_FOREACH(ent, &sndstat_devlist, link) {
		if (ent->dev == dev) {
			SLIST_REMOVE(&sndstat_devlist, ent, sndstat_entry, link);
			lockmgr(&sndstat_lock, LK_RELEASE);
			kfree(ent, M_DEVBUF);

			return 0;
		}
	}
	lockmgr(&sndstat_lock, LK_RELEASE);

	return ENXIO;
}

int
sndstat_unregisterfile(char *str)
{
	struct sndstat_entry *ent;

	lockmgr(&sndstat_lock, LK_EXCLUSIVE);
	SLIST_FOREACH(ent, &sndstat_devlist, link) {
		if (ent->dev == NULL && ent->str == str) {
			SLIST_REMOVE(&sndstat_devlist, ent, sndstat_entry, link);
			sndstat_files--;
			lockmgr(&sndstat_lock, LK_RELEASE);
			kfree(ent, M_DEVBUF);

			return 0;
		}
	}
	lockmgr(&sndstat_lock, LK_RELEASE);

	return ENXIO;
}

/************************************************************************/

static int
sndstat_prepare(struct sbuf *s)
{
	struct sndstat_entry *ent;
    	int i, j;

	sbuf_printf(s, "FreeBSD Audio Driver (newpcm)\n");
	if (SLIST_EMPTY(&sndstat_devlist)) {
		sbuf_printf(s, "No devices installed.\n");
		sbuf_finish(s);
    		return sbuf_len(s);
	}

	sbuf_printf(s, "Installed devices:\n");

    	for (i = 0; i <= sndstat_maxunit; i++) {
		for (j = SS_TYPE_FIRST; j <= SS_TYPE_LAST; j++) {
			ent = sndstat_find(j, i);
			if (!ent)
				continue;
			sbuf_printf(s, "%s:", device_get_nameunit(ent->dev));
			sbuf_printf(s, " <%s>", device_get_desc(ent->dev));
			sbuf_printf(s, " %s", ent->str);
			if (ent->handler)
				ent->handler(s, ent->dev, sndstat_verbose);
			else
				sbuf_printf(s, " [no handler]");
			sbuf_printf(s, "\n");
		}
    	}

	if (sndstat_verbose >= 3 && sndstat_files > 0) {
		sbuf_printf(s, "\nFile Versions:\n");

		SLIST_FOREACH(ent, &sndstat_devlist, link) {
			if (ent->dev == NULL && ent->str != NULL)
				sbuf_printf(s, "%s\n", ent->str);
		}
	}

	sbuf_finish(s);
    	return sbuf_len(s);
}

static int
sndstat_init(void)
{
	lockinit(&sndstat_lock, "sndstat", 0, 0);
	if (make_dev(&sndstat_cdevsw, SND_DEV_STATUS,
		     UID_ROOT, GID_WHEEL, 0444, "sndstat") == NULL) {
		return ENXIO;
	}
	return 0;
}

static int
sndstat_uninit(void)
{
	lockmgr(&sndstat_lock, LK_EXCLUSIVE);
	if (sndstat_isopen) {
		lockmgr(&sndstat_lock, LK_RELEASE);
		return EBUSY;
	}

	//dev_ops_remove(&sndstat_cdevsw, -1, SND_DEV_STATUS);
	dev_ops_remove_all(&sndstat_cdevsw);
	lockmgr(&sndstat_lock, LK_RELEASE);
	return 0;
}

static void
sndstat_sysinit(void *p)
{
	sndstat_init();
}

static void
sndstat_sysuninit(void *p)
{
	int error;

	error = sndstat_uninit();
	KASSERT(error == 0, ("%s: error = %d", __func__, error));
}

SYSINIT(sndstat_sysinit, SI_SUB_DRIVERS, SI_ORDER_FIRST, sndstat_sysinit, NULL);
SYSUNINIT(sndstat_sysuninit, SI_SUB_DRIVERS, SI_ORDER_FIRST, sndstat_sysuninit, NULL);

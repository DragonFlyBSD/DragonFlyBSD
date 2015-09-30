/*-
 * Copyright (c) 2003 Mathew Kanner
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (augustss@netbsd.org).
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

 /*
  * Parts of this file started out as NetBSD: midi.c 1.31
  * They are mostly gone.  Still the most obvious will be the state
  * machine midi_in
  */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: head/sys/dev/sound/midi/midi.c 227309 2011-11-07 15:43:11Z ed $");

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/conf.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/poll.h>
#include <sys/sbuf.h>
#include <sys/kobj.h>
#include <sys/module.h>
#include <sys/device.h>

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/midi/midi.h>
#include "mpu_if.h"

#include <dev/sound/midi/midiq.h>
#include "synth_if.h"
MALLOC_DEFINE(M_MIDI, "midi buffers", "Midi data allocation area");

#ifndef KOBJMETHOD_END
#define KOBJMETHOD_END	{ NULL, NULL }
#endif

#define PCMMKMINOR(u, d, c) ((((c) & 0xff) << 16) | (((u) & 0x0f) << 4) | ((d) & 0x0f))
#define MIDIMKMINOR(u, d, c) PCMMKMINOR(u, d, c)

#define MIDI_DEV_RAW	2
#define MIDI_DEV_MIDICTL 12

enum midi_states {
	MIDI_IN_START, MIDI_IN_SYSEX, MIDI_IN_DATA
};

/*
 * The MPU interface current has init() uninit() inqsize(( outqsize()
 * callback() : fiddle with the tx|rx status.
 */

#include "mpu_if.h"

/*
 * /dev/rmidi	Structure definitions
 */

#define MIDI_NAMELEN   16
struct snd_midi {
	KOBJ_FIELDS;
	struct lock lock;		/* Protects all but queues */
	void   *cookie;

	int	unit;			/* Should only be used in midistat */
	int	channel;		/* Should only be used in midistat */

	int	busy;
	int	flags;			/* File flags */
	char	name[MIDI_NAMELEN];
	struct lock qlock;		/* Protects inq, outq and flags */
	MIDIQ_HEAD(, char) inq, outq;
	int	rchan, wchan;
	struct kqinfo rkq, wkq;
	int	hiwat;			/* QLEN(outq)>High-water -> disable
					 * writes from userland */
	enum midi_states inq_state;
	int	inq_status, inq_left;	/* Variables for the state machine in
					 * Midi_in, this is to provide that
					 * signals only get issued only
					 * complete command packets. */
	struct proc *async;
	struct cdev *dev;
	struct synth_midi *synth;
	int	synth_flags;
	TAILQ_ENTRY(snd_midi) link;
};

struct synth_midi {
	KOBJ_FIELDS;
	struct snd_midi *m;
};

static synth_open_t midisynth_open;
static synth_close_t midisynth_close;
static synth_writeraw_t midisynth_writeraw;
static synth_killnote_t midisynth_killnote;
static synth_startnote_t midisynth_startnote;
static synth_setinstr_t midisynth_setinstr;
static synth_alloc_t midisynth_alloc;
static synth_controller_t midisynth_controller;
static synth_bender_t midisynth_bender;


static kobj_method_t midisynth_methods[] = {
	KOBJMETHOD(synth_open, midisynth_open),
	KOBJMETHOD(synth_close, midisynth_close),
	KOBJMETHOD(synth_writeraw, midisynth_writeraw),
	KOBJMETHOD(synth_setinstr, midisynth_setinstr),
	KOBJMETHOD(synth_startnote, midisynth_startnote),
	KOBJMETHOD(synth_killnote, midisynth_killnote),
	KOBJMETHOD(synth_alloc, midisynth_alloc),
	KOBJMETHOD(synth_controller, midisynth_controller),
	KOBJMETHOD(synth_bender, midisynth_bender),
	KOBJMETHOD_END
};

DEFINE_CLASS(midisynth, midisynth_methods, 0);

/*
 * Module Exports & Interface
 *
 * struct midi_chan *midi_init(MPU_CLASS cls, int unit, int chan) int
 * midi_uninit(struct snd_midi *) 0 == no error EBUSY or other error int
 * Midi_in(struct midi_chan *, char *buf, int count) int Midi_out(struct
 * midi_chan *, char *buf, int count)
 *
 * midi_{in,out} return actual size transfered
 *
 */


/*
 * midi_devs tailq, holder of all rmidi instances protected by midistat_lock
 */

TAILQ_HEAD(, snd_midi) midi_devs;

/*
 * /dev/midistat variables and declarations, protected by midistat_lock
 */

static struct lock midistat_lock;
static int      midistat_isopen = 0;
static struct sbuf midistat_sbuf;
static int      midistat_bufptr;
static struct cdev *midistat_dev;

/*
 * /dev/midistat	dev_t declarations
 */

static d_open_t midistat_open;
static d_close_t midistat_close;
static d_read_t midistat_read;

static void	midi_filter_detach(struct knote *);
static int	midi_filter_read(struct knote *, long);
static int	midi_filter_write(struct knote *, long);

static struct dev_ops midistat_ops = {
	{ "midistat", 0, D_MPSAFE },
	.d_open = midistat_open,
	.d_close = midistat_close,
	.d_read = midistat_read,
};

static struct filterops midi_read_filterops =
	{ FILTEROP_ISFD, NULL, midi_filter_detach, midi_filter_read };
static struct filterops midi_write_filterops =
	{ FILTEROP_ISFD, NULL, midi_filter_detach, midi_filter_write };

/*
 * /dev/rmidi dev_t declarations, struct variable access is protected by
 * locks contained within the structure.
 */

static d_open_t midi_open;
static d_close_t midi_close;
static d_ioctl_t midi_ioctl;
static d_read_t midi_read;
static d_write_t midi_write;
static d_kqfilter_t midi_kqfilter;

static struct dev_ops midi_ops = {
	{ "rmidi", 0, D_MPSAFE },
	.d_open = midi_open,
	.d_close = midi_close,
	.d_read = midi_read,
	.d_write = midi_write,
	.d_ioctl = midi_ioctl,
	.d_kqfilter = midi_kqfilter,
};

/*
 * Prototypes of library functions
 */

static int      midi_destroy(struct snd_midi *, int);
static int      midistat_prepare(struct sbuf * s);
static int      midi_load(void);
static int      midi_unload(void);

/*
 * Misc declr.
 */
SYSCTL_NODE(_hw, OID_AUTO, midi, CTLFLAG_RD, 0, "Midi driver");
static SYSCTL_NODE(_hw_midi, OID_AUTO, stat, CTLFLAG_RD, 0, "Status device");

int             midi_debug;
/* XXX: should this be moved into debug.midi? */
SYSCTL_INT(_hw_midi, OID_AUTO, debug, CTLFLAG_RW, &midi_debug, 0, "");

int             midi_dumpraw;
SYSCTL_INT(_hw_midi, OID_AUTO, dumpraw, CTLFLAG_RW, &midi_dumpraw, 0, "");

int             midi_instroff;
SYSCTL_INT(_hw_midi, OID_AUTO, instroff, CTLFLAG_RW, &midi_instroff, 0, "");

int             midistat_verbose;
SYSCTL_INT(_hw_midi_stat, OID_AUTO, verbose, CTLFLAG_RW, 
	&midistat_verbose, 0, "");

#define MIDI_DEBUG(l,a)	if(midi_debug>=l) a
/*
 * CODE START
 */

/*
 * Register a new rmidi device. cls midi_if interface unit == 0 means
 * auto-assign new unit number unit != 0 already assigned a unit number, eg.
 * not the first channel provided by this device. channel,	sub-unit
 * cookie is passed back on MPU calls Typical device drivers will call with
 * unit=0, channel=1..(number of channels) and cookie=soft_c and won't care
 * what unit number is used.
 *
 * It is an error to call midi_init with an already used unit/channel combo.
 *
 * Returns NULL on error
 *
 */
struct snd_midi *
midi_init(kobj_class_t cls, int unit, int channel, void *cookie)
{
	struct snd_midi *m;
	int i;
	int inqsize, outqsize;
	MIDI_TYPE *buf;

	MIDI_DEBUG(1, kprintf("midiinit: unit %d/%d.\n", unit, channel));
	lockmgr(&midistat_lock, LK_EXCLUSIVE);
	/*
	 * Protect against call with existing unit/channel or auto-allocate a
	 * new unit number.
	 */
	i = -1;
	TAILQ_FOREACH(m, &midi_devs, link) {
		lockmgr(&m->lock, LK_EXCLUSIVE);
		if (unit != 0) {
			if (m->unit == unit && m->channel == channel) {
				lockmgr(&m->lock, LK_RELEASE);
				goto err0;
			}
		} else {
			/*
			 * Find a better unit number
			 */
			if (m->unit > i)
				i = m->unit;
		}
		lockmgr(&m->lock, LK_RELEASE);
	}

	if (unit == 0)
		unit = i + 1;

	MIDI_DEBUG(1, kprintf("midiinit #2: unit %d/%d.\n", unit, channel));
	m = kmalloc(sizeof(*m), M_MIDI, M_WAITOK | M_ZERO);

	m->synth = kmalloc(sizeof(*m->synth), M_MIDI, M_WAITOK | M_ZERO);
	kobj_init((kobj_t)m->synth, &midisynth_class);
	m->synth->m = m;
	kobj_init((kobj_t)m, cls);
	inqsize = MPU_INQSIZE(m, cookie);
	outqsize = MPU_OUTQSIZE(m, cookie);

	MIDI_DEBUG(1, kprintf("midiinit queues %d/%d.\n", inqsize, outqsize));
	if (!inqsize && !outqsize)
		goto err1;

	lockinit(&m->lock, "raw midi", 0, LK_CANRECURSE);
	lockinit(&m->qlock, "q raw midi", 0, LK_CANRECURSE);

	lockmgr(&m->lock, LK_EXCLUSIVE);
	lockmgr(&m->qlock, LK_EXCLUSIVE);

	if (inqsize)
		buf = kmalloc(sizeof(MIDI_TYPE) * inqsize, M_MIDI, M_WAITOK);
	else
		buf = NULL;

	MIDIQ_INIT(m->inq, buf, inqsize);

	if (outqsize)
		buf = kmalloc(sizeof(MIDI_TYPE) * outqsize, M_MIDI, M_WAITOK);
	else
		buf = NULL;
	m->hiwat = outqsize / 2;

	MIDIQ_INIT(m->outq, buf, outqsize);

	if ((inqsize && !MIDIQ_BUF(m->inq)) ||
	    (outqsize && !MIDIQ_BUF(m->outq)))
		goto err2;


	m->busy = 0;
	m->flags = 0;
	m->unit = unit;
	m->channel = channel;
	m->cookie = cookie;

	if (MPU_INIT(m, cookie))
		goto err2;

	lockmgr(&m->lock, LK_RELEASE);
	lockmgr(&m->qlock, LK_RELEASE);

	TAILQ_INSERT_TAIL(&midi_devs, m, link);

	lockmgr(&midistat_lock, LK_RELEASE);

	m->dev = make_dev(&midi_ops,
	    MIDIMKMINOR(unit, MIDI_DEV_RAW, channel),
	    UID_ROOT, GID_WHEEL, 0666, "midi%d.%d", unit, channel);
	m->dev->si_drv1 = m;

	return m;

err2:	lockuninit(&m->qlock);
	lockuninit(&m->lock);

	if (MIDIQ_BUF(m->inq))
		kfree(MIDIQ_BUF(m->inq), M_MIDI);
	if (MIDIQ_BUF(m->outq))
		kfree(MIDIQ_BUF(m->outq), M_MIDI);
err1:	kfree(m, M_MIDI);
err0:	lockmgr(&midistat_lock, LK_RELEASE);
	MIDI_DEBUG(1, kprintf("midi_init ended in error\n"));
	return NULL;
}

/*
 * midi_uninit does not call MIDI_UNINIT, as since this is the implementors
 * entry point. midi_unint if fact, does not send any methods. A call to
 * midi_uninit is a defacto promise that you won't manipulate ch anymore
 *
 */

int
midi_uninit(struct snd_midi *m)
{
	int err;

	err = ENXIO;
	lockmgr(&midistat_lock, LK_EXCLUSIVE);
	lockmgr(&m->lock, LK_EXCLUSIVE);
	if (m->busy) {
		if (!(m->rchan || m->wchan))
			goto err;

		if (m->rchan) {
			wakeup(&m->rchan);
			m->rchan = 0;
		}
		if (m->wchan) {
			wakeup(&m->wchan);
			m->wchan = 0;
		}
	}
	err = midi_destroy(m, 0);
	if (!err)
		goto exit;

err:	lockmgr(&m->lock, LK_RELEASE);
exit:	lockmgr(&midistat_lock, LK_RELEASE);
	return err;
}

/*
 * midi_in: process all data until the queue is full, then discards the rest.
 * Since midi_in is a state machine, data discards can cause it to get out of
 * whack.  Process as much as possible.  It calls, wakeup, selnotify and
 * psignal at most once.
 */

#ifdef notdef
static int midi_lengths[] = {2, 2, 2, 2, 1, 1, 2, 0};

#endif					/* notdef */
/* Number of bytes in a MIDI command */
#define MIDI_LENGTH(d) (midi_lengths[((d) >> 4) & 7])
#define MIDI_ACK	0xfe
#define MIDI_IS_STATUS(d) ((d) >= 0x80)
#define MIDI_IS_COMMON(d) ((d) >= 0xf0)

#define MIDI_SYSEX_START	0xF0
#define MIDI_SYSEX_END	    0xF7


int
midi_in(struct snd_midi *m, MIDI_TYPE *buf, int size)
{
	/* int             i, sig, enq; */
	int used;

	/* MIDI_TYPE       data; */
	MIDI_DEBUG(5, kprintf("midi_in: m=%p size=%d\n", m, size));

/*
 * XXX: locking flub
 */
	if (!(m->flags & M_RX))
		return size;

	used = 0;

	lockmgr(&m->qlock, LK_EXCLUSIVE);
#if 0
	/*
	 * Don't bother queuing if not in read mode.  Discard everything and
	 * return size so the caller doesn't freak out.
	 */

	if (!(m->flags & M_RX))
		return size;

	for (i = sig = 0; i < size; i++) {

		data = buf[i];
		enq = 0;
		if (data == MIDI_ACK)
			continue;

		switch (m->inq_state) {
		case MIDI_IN_START:
			if (MIDI_IS_STATUS(data)) {
				switch (data) {
				case 0xf0:	/* Sysex */
					m->inq_state = MIDI_IN_SYSEX;
					break;
				case 0xf1:	/* MTC quarter frame */
				case 0xf3:	/* Song select */
					m->inq_state = MIDI_IN_DATA;
					enq = 1;
					m->inq_left = 1;
					break;
				case 0xf2:	/* Song position pointer */
					m->inq_state = MIDI_IN_DATA;
					enq = 1;
					m->inq_left = 2;
					break;
				default:
					if (MIDI_IS_COMMON(data)) {
						enq = 1;
						sig = 1;
					} else {
						m->inq_state = MIDI_IN_DATA;
						enq = 1;
						m->inq_status = data;
						m->inq_left = MIDI_LENGTH(data);
					}
					break;
				}
			} else if (MIDI_IS_STATUS(m->inq_status)) {
				m->inq_state = MIDI_IN_DATA;
				if (!MIDIQ_FULL(m->inq)) {
					used++;
					MIDIQ_ENQ(m->inq, &m->inq_status, 1);
				}
				enq = 1;
				m->inq_left = MIDI_LENGTH(m->inq_status) - 1;
			}
			break;
			/*
			 * End of case MIDI_IN_START:
			 */

		case MIDI_IN_DATA:
			enq = 1;
			if (--m->inq_left <= 0)
				sig = 1;/* deliver data */
			break;
		case MIDI_IN_SYSEX:
			if (data == MIDI_SYSEX_END)
				m->inq_state = MIDI_IN_START;
			break;
		}

		if (enq)
			if (!MIDIQ_FULL(m->inq)) {
				MIDIQ_ENQ(m->inq, &data, 1);
				used++;
			}
		/*
	         * End of the state machines main "for loop"
	         */
	}
	if (sig) {
#endif
		MIDI_DEBUG(6, kprintf("midi_in: len %jd avail %jd\n",
		    (intmax_t)MIDIQ_LEN(m->inq),
		    (intmax_t)MIDIQ_AVAIL(m->inq)));
		if (MIDIQ_AVAIL(m->inq) > size) {
			used = size;
			MIDIQ_ENQ(m->inq, buf, size);
		} else {
			MIDI_DEBUG(4, kprintf("midi_in: Discarding data qu\n"));
			lockmgr(&m->qlock, LK_RELEASE);
			return 0;
		}
		if (m->rchan) {
			wakeup(&m->rchan);
			m->rchan = 0;
		}
		KNOTE(&m->rkq.ki_note, 0);
		if (m->async) {
			PHOLD(m->async);
			ksignal(m->async, SIGIO);
			PRELE(m->async);
		}
#if 0
	}
#endif
	lockmgr(&m->qlock, LK_RELEASE);
	return used;
}

/*
 * midi_out: The only clearer of the M_TXEN flag.
 */
int
midi_out(struct snd_midi *m, MIDI_TYPE *buf, int size)
{
	int used;

/*
 * XXX: locking flub
 */
	if (!(m->flags & M_TXEN))
		return 0;

	MIDI_DEBUG(2, kprintf("midi_out: %p\n", m));
	lockmgr(&m->qlock, LK_EXCLUSIVE);
	used = MIN(size, MIDIQ_LEN(m->outq));
	MIDI_DEBUG(3, kprintf("midi_out: used %d\n", used));
	if (used)
		MIDIQ_DEQ(m->outq, buf, used);
	if (MIDIQ_EMPTY(m->outq)) {
		m->flags &= ~M_TXEN;
		MPU_CALLBACKP(m, m->cookie, m->flags);
	}
	if (used && MIDIQ_AVAIL(m->outq) > m->hiwat) {
		if (m->wchan) {
			wakeup(&m->wchan);
			m->wchan = 0;
		}
		KNOTE(&m->wkq.ki_note, 0);
		if (m->async) {
			PHOLD(m->async);
			ksignal(m->async, SIGIO);
			PRELE(m->async);
		}
	}
	lockmgr(&m->qlock, LK_RELEASE);
	return used;
}


/*
 * /dev/rmidi#.#	device access functions
 */
int
midi_open(struct dev_open_args *ap)
{
	cdev_t i_dev = ap->a_head.a_dev;
	int flags = ap->a_oflags;
	struct snd_midi *m = i_dev->si_drv1;
	int retval;

#if 0 /* XXX */
	MIDI_DEBUG(1, kprintf("midiopen %p %s %s\n", td,
	    flags & FREAD ? "M_RX" : "", flags & FWRITE ? "M_TX" : ""));
#endif
	if (m == NULL)
		return ENXIO;

	lockmgr(&m->lock, LK_EXCLUSIVE);
	lockmgr(&m->qlock, LK_EXCLUSIVE);

	retval = 0;

	if (flags & FREAD) {
		if (MIDIQ_SIZE(m->inq) == 0)
			retval = ENXIO;
		else if (m->flags & M_RX)
			retval = EBUSY;
		if (retval)
			goto err;
	}
	if (flags & FWRITE) {
		if (MIDIQ_SIZE(m->outq) == 0)
			retval = ENXIO;
		else if (m->flags & M_TX)
			retval = EBUSY;
		if (retval)
			goto err;
	}
	m->busy++;

	m->rchan = 0;
	m->wchan = 0;
	m->async = 0;

	if (flags & FREAD) {
		m->flags |= M_RX | M_RXEN;
		/*
	         * Only clear the inq, the outq might still have data to drain
	         * from a previous session
	         */
		MIDIQ_CLEAR(m->inq);
	};

	if (flags & FWRITE)
		m->flags |= M_TX;

	MPU_CALLBACK(m, m->cookie, m->flags);

	MIDI_DEBUG(2, kprintf("midi_open: opened.\n"));

err:	lockmgr(&m->qlock, LK_RELEASE);
	lockmgr(&m->lock, LK_RELEASE);
	return retval;
}

int
midi_close(struct dev_close_args *ap)
{
	cdev_t i_dev = ap->a_head.a_dev;
	int flags = ap->a_fflag;
	struct snd_midi *m = i_dev->si_drv1;
	int retval;
	int oldflags;

#if 0 /* XXX */
	MIDI_DEBUG(1, kprintf("midi_close %p %s %s\n", td,
	    flags & FREAD ? "M_RX" : "", flags & FWRITE ? "M_TX" : ""));
#endif

	if (m == NULL)
		return ENXIO;

	lockmgr(&m->lock, LK_EXCLUSIVE);
	lockmgr(&m->qlock, LK_EXCLUSIVE);

	if ((flags & FREAD && !(m->flags & M_RX)) ||
	    (flags & FWRITE && !(m->flags & M_TX))) {
		retval = ENXIO;
		goto err;
	}
	m->busy--;

	oldflags = m->flags;

	if (flags & FREAD)
		m->flags &= ~(M_RX | M_RXEN);
	if (flags & FWRITE)
		m->flags &= ~M_TX;

	if ((m->flags & (M_TXEN | M_RXEN)) != (oldflags & (M_RXEN | M_TXEN)))
		MPU_CALLBACK(m, m->cookie, m->flags);

	MIDI_DEBUG(1, kprintf("midi_close: closed, busy = %d.\n", m->busy));

	lockmgr(&m->qlock, LK_RELEASE);
	lockmgr(&m->lock, LK_RELEASE);
	retval = 0;
err:	return retval;
}

/*
 * TODO: midi_read, per oss programmer's guide pg. 42 should return as soon
 * as data is available.
 */
int
midi_read(struct dev_read_args *ap)
{
	cdev_t i_dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	int ioflag = ap->a_ioflag;
#define MIDI_RSIZE 32
	struct snd_midi *m = i_dev->si_drv1;
	int retval;
	int used;
	char buf[MIDI_RSIZE];

	MIDI_DEBUG(5, kprintf("midiread: count=%lu\n",
	    (unsigned long)uio->uio_resid));

	retval = EIO;

	if (m == NULL)
		goto err0;

	lockmgr(&m->lock, LK_EXCLUSIVE);
	lockmgr(&m->qlock, LK_EXCLUSIVE);

	if (!(m->flags & M_RX))
		goto err1;

	while (uio->uio_resid > 0) {
		while (MIDIQ_EMPTY(m->inq)) {
			retval = EWOULDBLOCK;
			if (ioflag & O_NONBLOCK)
				goto err1;
			lockmgr(&m->lock, LK_RELEASE);
			m->rchan = 1;
			retval = lksleep(&m->rchan, &m->qlock,
			    PCATCH, "midi RX", 0);
			/*
			 * We slept, maybe things have changed since last
			 * dying check
			 */
			if (retval == EINTR)
				goto err0;
			if (m != i_dev->si_drv1)
				retval = ENXIO;
			/* if (retval && retval != ERESTART) */
			if (retval)
				goto err0;
			lockmgr(&m->lock, LK_EXCLUSIVE);
			lockmgr(&m->qlock, LK_EXCLUSIVE);
			m->rchan = 0;
			if (!m->busy)
				goto err1;
		}
		MIDI_DEBUG(6, kprintf("midi_read start\n"));
		/*
	         * At this point, it is certain that m->inq has data
	         */

		used = MIN(MIDIQ_LEN(m->inq), uio->uio_resid);
		used = MIN(used, MIDI_RSIZE);

		MIDI_DEBUG(6, kprintf("midiread: uiomove cc=%d\n", used));
		MIDIQ_DEQ(m->inq, buf, used);
		retval = uiomove(buf, used, uio);
		if (retval)
			goto err1;
	}

	/*
	 * If we Made it here then transfer is good
	 */
	retval = 0;
err1:	lockmgr(&m->qlock, LK_RELEASE);
	lockmgr(&m->lock, LK_RELEASE);
err0:	MIDI_DEBUG(4, kprintf("midi_read: ret %d\n", retval));
	return retval;
}

/*
 * midi_write: The only setter of M_TXEN
 */

int
midi_write(struct dev_write_args *ap)
{
	cdev_t i_dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	int ioflag = ap->a_ioflag;
#define MIDI_WSIZE 32
	struct snd_midi *m = i_dev->si_drv1;
	int retval;
	int used;
	char buf[MIDI_WSIZE];


	MIDI_DEBUG(4, kprintf("midi_write\n"));
	retval = 0;
	if (m == NULL)
		goto err0;

	lockmgr(&m->lock, LK_EXCLUSIVE);
	lockmgr(&m->qlock, LK_EXCLUSIVE);

	if (!(m->flags & M_TX))
		goto err1;

	while (uio->uio_resid > 0) {
		while (MIDIQ_AVAIL(m->outq) == 0) {
			retval = EWOULDBLOCK;
			if (ioflag & O_NONBLOCK)
				goto err1;
			lockmgr(&m->lock, LK_RELEASE);
			m->wchan = 1;
			MIDI_DEBUG(3, kprintf("midi_write lksleep\n"));
			retval = lksleep(&m->wchan, &m->qlock,
			    PCATCH, "midi TX", 0);
			/*
			 * We slept, maybe things have changed since last
			 * dying check
			 */
			if (retval == EINTR)
				goto err0;
			if (m != i_dev->si_drv1)
				retval = ENXIO;
			if (retval)
				goto err0;
			lockmgr(&m->lock, LK_EXCLUSIVE);
			lockmgr(&m->qlock, LK_EXCLUSIVE);
			m->wchan = 0;
			if (!m->busy)
				goto err1;
		}

		/*
	         * We are certain than data can be placed on the queue
	         */

		used = MIN(MIDIQ_AVAIL(m->outq), uio->uio_resid);
		used = MIN(used, MIDI_WSIZE);
		MIDI_DEBUG(5, kprintf("midiout: resid %zd len %jd avail %jd\n",
		    uio->uio_resid, (intmax_t)MIDIQ_LEN(m->outq),
		    (intmax_t)MIDIQ_AVAIL(m->outq)));


		MIDI_DEBUG(5, kprintf("midi_write: uiomove cc=%d\n", used));
		retval = uiomove(buf, used, uio);
		if (retval)
			goto err1;
		MIDIQ_ENQ(m->outq, buf, used);
		/*
	         * Inform the bottom half that data can be written
	         */
		if (!(m->flags & M_TXEN)) {
			m->flags |= M_TXEN;
			MPU_CALLBACK(m, m->cookie, m->flags);
		}
	}
	/*
	 * If we Made it here then transfer is good
	 */
	retval = 0;
err1:	lockmgr(&m->qlock, LK_RELEASE);
	lockmgr(&m->lock, LK_RELEASE);
err0:	return retval;
}

int
midi_ioctl(struct dev_ioctl_args *ap)
{
	return ENXIO;
}

int
midi_kqfilter(struct dev_kqfilter_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct knote *kn = ap->a_kn;
	struct snd_midi *m;
	struct klist *klist;

	ap->a_result = 0;
	m = dev->si_drv1;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &midi_read_filterops;
		kn->kn_hook = (caddr_t)m;
		klist = &m->rkq.ki_note;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &midi_write_filterops;
		kn->kn_hook = (caddr_t)m;
		klist = &m->wkq.ki_note;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		return (0);
	}

	knote_insert(klist, kn);

	return(0);
}

static void
midi_filter_detach(struct knote *kn)
{
	struct snd_midi *m = (struct snd_midi *)kn->kn_hook;
	struct klist *rklist = &m->rkq.ki_note;
	struct klist *wklist = &m->wkq.ki_note;

	knote_remove(rklist, kn);
	knote_remove(wklist, kn);
}

static int
midi_filter_read(struct knote *kn, long hint)
{
	struct snd_midi *m = (struct snd_midi *)kn->kn_hook;
	int ready = 0;

	lockmgr(&m->lock, LK_EXCLUSIVE);
	lockmgr(&m->qlock, LK_EXCLUSIVE);

	if (!MIDIQ_EMPTY(m->inq))
		ready = 1;

	lockmgr(&m->lock, LK_RELEASE);
	lockmgr(&m->qlock, LK_RELEASE);

	return (ready);
}

static int
midi_filter_write(struct knote *kn, long hint)
{
	struct snd_midi *m = (struct snd_midi *)kn->kn_hook;
	int ready = 0;

	lockmgr(&m->lock, LK_EXCLUSIVE);
	lockmgr(&m->qlock, LK_EXCLUSIVE);

	if (MIDIQ_AVAIL(m->outq) < m->hiwat)
		ready = 1;

	lockmgr(&m->lock, LK_RELEASE);
	lockmgr(&m->qlock, LK_RELEASE);

	return (ready);
}

/*
 * /dev/midistat device functions
 *
 */
static int
midistat_open(struct dev_open_args *ap)
{
	int error;

	MIDI_DEBUG(1, kprintf("midistat_open\n"));
	lockmgr(&midistat_lock, LK_EXCLUSIVE);

	if (midistat_isopen) {
		lockmgr(&midistat_lock, LK_RELEASE);
		return EBUSY;
	}
	midistat_isopen = 1;
	lockmgr(&midistat_lock, LK_RELEASE);

	if (sbuf_new(&midistat_sbuf, NULL, 4096, SBUF_AUTOEXTEND) == NULL) {
		error = ENXIO;
		lockmgr(&midistat_lock, LK_EXCLUSIVE);
		goto out;
	}
	lockmgr(&midistat_lock, LK_EXCLUSIVE);
	midistat_bufptr = 0;
	error = (midistat_prepare(&midistat_sbuf) > 0) ? 0 : ENOMEM;

out:	if (error)
		midistat_isopen = 0;
	lockmgr(&midistat_lock, LK_RELEASE);
	return error;
}

static int
midistat_close(struct dev_close_args *ap)
{
	MIDI_DEBUG(1, kprintf("midistat_close\n"));
	lockmgr(&midistat_lock, LK_EXCLUSIVE);
	if (!midistat_isopen) {
		lockmgr(&midistat_lock, LK_RELEASE);
		return EBADF;
	}
	sbuf_delete(&midistat_sbuf);
	midistat_isopen = 0;

	lockmgr(&midistat_lock, LK_RELEASE);
	return 0;
}

static int
midistat_read(struct dev_read_args *ap)
{
	struct uio *buf = ap->a_uio;
	int l, err;

	MIDI_DEBUG(4, kprintf("midistat_read\n"));
	lockmgr(&midistat_lock, LK_EXCLUSIVE);
	if (!midistat_isopen) {
		lockmgr(&midistat_lock, LK_RELEASE);
		return EBADF;
	}
	l = min(buf->uio_resid, sbuf_len(&midistat_sbuf) - midistat_bufptr);
	err = 0;
	if (l > 0) {
		lockmgr(&midistat_lock, LK_RELEASE);
		err = uiomove(sbuf_data(&midistat_sbuf) + midistat_bufptr, l,
		    buf);
		lockmgr(&midistat_lock, LK_EXCLUSIVE);
	} else
		l = 0;
	midistat_bufptr += l;
	lockmgr(&midistat_lock, LK_RELEASE);
	return err;
}

/*
 * Module library functions
 */

static int
midistat_prepare(struct sbuf *s)
{
	struct snd_midi *m;

	KKASSERT(lockowned(&midistat_lock));

	sbuf_printf(s, "FreeBSD Midi Driver (midi2)\n");
	if (TAILQ_EMPTY(&midi_devs)) {
		sbuf_printf(s, "No devices installed.\n");
		sbuf_finish(s);
		return sbuf_len(s);
	}
	sbuf_printf(s, "Installed devices:\n");

	TAILQ_FOREACH(m, &midi_devs, link) {
		lockmgr(&m->lock, LK_EXCLUSIVE);
		sbuf_printf(s, "%s [%d/%d:%s]", m->name, m->unit, m->channel,
		    MPU_PROVIDER(m, m->cookie));
		sbuf_printf(s, "%s", MPU_DESCR(m, m->cookie, midistat_verbose));
		sbuf_printf(s, "\n");
		lockmgr(&m->lock, LK_RELEASE);
	}

	sbuf_finish(s);
	return sbuf_len(s);
}

#ifdef notdef
/*
 * Convert IOCTL command to string for debugging
 */

static char *
midi_cmdname(int cmd)
{
	static struct {
		int	cmd;
		char   *name;
	}     *tab, cmdtab_midiioctl[] = {
#define A(x)	{x, ## x}
		/*
	         * Once we have some real IOCTLs define, the following will
	         * be relavant.
	         *
	         * A(SNDCTL_MIDI_PRETIME), A(SNDCTL_MIDI_MPUMODE),
	         * A(SNDCTL_MIDI_MPUCMD), A(SNDCTL_SYNTH_INFO),
	         * A(SNDCTL_MIDI_INFO), A(SNDCTL_SYNTH_MEMAVL),
	         * A(SNDCTL_FM_LOAD_INSTR), A(SNDCTL_FM_4OP_ENABLE),
	         * A(MIOSPASSTHRU), A(MIOGPASSTHRU), A(AIONWRITE),
	         * A(AIOGSIZE), A(AIOSSIZE), A(AIOGFMT), A(AIOSFMT),
	         * A(AIOGMIX), A(AIOSMIX), A(AIOSTOP), A(AIOSYNC),
	         * A(AIOGCAP),
	         */
#undef A
		{
			-1, "unknown"
		},
	};

	for (tab = cmdtab_midiioctl; tab->cmd != cmd && tab->cmd != -1; tab++);
	return tab->name;
}

#endif					/* notdef */

/*
 * midisynth
 */


int
midisynth_open(void *n, void *arg, int flags)
{
	struct snd_midi *m = ((struct synth_midi *)n)->m;
	int retval;

	MIDI_DEBUG(1, kprintf("midisynth_open %s %s\n",
	    flags & FREAD ? "M_RX" : "", flags & FWRITE ? "M_TX" : ""));

	if (m == NULL)
		return ENXIO;

	lockmgr(&m->lock, LK_EXCLUSIVE);
	lockmgr(&m->qlock, LK_EXCLUSIVE);

	retval = 0;

	if (flags & FREAD) {
		if (MIDIQ_SIZE(m->inq) == 0)
			retval = ENXIO;
		else if (m->flags & M_RX)
			retval = EBUSY;
		if (retval)
			goto err;
	}
	if (flags & FWRITE) {
		if (MIDIQ_SIZE(m->outq) == 0)
			retval = ENXIO;
		else if (m->flags & M_TX)
			retval = EBUSY;
		if (retval)
			goto err;
	}
	m->busy++;

	/*
	 * TODO: Consider m->async = 0;
	 */

	if (flags & FREAD) {
		m->flags |= M_RX | M_RXEN;
		/*
	         * Only clear the inq, the outq might still have data to drain
	         * from a previous session
	         */
		MIDIQ_CLEAR(m->inq);
		m->rchan = 0;
	};

	if (flags & FWRITE) {
		m->flags |= M_TX;
		m->wchan = 0;
	}
	m->synth_flags = flags & (FREAD | FWRITE);

	MPU_CALLBACK(m, m->cookie, m->flags);


err:	lockmgr(&m->qlock, LK_RELEASE);
	lockmgr(&m->lock, LK_RELEASE);
	MIDI_DEBUG(2, kprintf("midisynth_open: return %d.\n", retval));
	return retval;
}

int
midisynth_close(void *n)
{
	struct snd_midi *m = ((struct synth_midi *)n)->m;
	int retval;
	int oldflags;

	MIDI_DEBUG(1, kprintf("midisynth_close %s %s\n",
	    m->synth_flags & FREAD ? "M_RX" : "",
	    m->synth_flags & FWRITE ? "M_TX" : ""));

	if (m == NULL)
		return ENXIO;

	lockmgr(&m->lock, LK_EXCLUSIVE);
	lockmgr(&m->qlock, LK_EXCLUSIVE);

	if ((m->synth_flags & FREAD && !(m->flags & M_RX)) ||
	    (m->synth_flags & FWRITE && !(m->flags & M_TX))) {
		retval = ENXIO;
		goto err;
	}
	m->busy--;

	oldflags = m->flags;

	if (m->synth_flags & FREAD)
		m->flags &= ~(M_RX | M_RXEN);
	if (m->synth_flags & FWRITE)
		m->flags &= ~M_TX;

	if ((m->flags & (M_TXEN | M_RXEN)) != (oldflags & (M_RXEN | M_TXEN)))
		MPU_CALLBACK(m, m->cookie, m->flags);

	MIDI_DEBUG(1, kprintf("midi_close: closed, busy = %d.\n", m->busy));

	lockmgr(&m->qlock, LK_RELEASE);
	lockmgr(&m->lock, LK_RELEASE);
	retval = 0;
err:	return retval;
}

/*
 * Always blocking.
 */

int
midisynth_writeraw(void *n, uint8_t *buf, size_t len)
{
	struct snd_midi *m = ((struct synth_midi *)n)->m;
	int retval;
	int used;
	int i;

	MIDI_DEBUG(4, kprintf("midisynth_writeraw\n"));

	retval = 0;

	if (m == NULL)
		return ENXIO;

	lockmgr(&m->lock, LK_EXCLUSIVE);
	lockmgr(&m->qlock, LK_EXCLUSIVE);

	if (!(m->flags & M_TX))
		goto err1;

	if (midi_dumpraw)
		kprintf("midi dump: ");

	while (len > 0) {
		while (MIDIQ_AVAIL(m->outq) == 0) {
			if (!(m->flags & M_TXEN)) {
				m->flags |= M_TXEN;
				MPU_CALLBACK(m, m->cookie, m->flags);
			}
			lockmgr(&m->lock, LK_RELEASE);
			m->wchan = 1;
			MIDI_DEBUG(3, kprintf("midisynth_writeraw lksleep\n"));
			retval = lksleep(&m->wchan, &m->qlock,
			    PCATCH, "midi TX", 0);
			/*
			 * We slept, maybe things have changed since last
			 * dying check
			 */
			if (retval == EINTR)
				goto err0;

			if (retval)
				goto err0;
			lockmgr(&m->lock, LK_EXCLUSIVE);
			lockmgr(&m->qlock, LK_EXCLUSIVE);
			m->wchan = 0;
			if (!m->busy)
				goto err1;
		}

		/*
	         * We are certain than data can be placed on the queue
	         */

		used = MIN(MIDIQ_AVAIL(m->outq), len);
		used = MIN(used, MIDI_WSIZE);
		MIDI_DEBUG(5,
		    kprintf("midi_synth: resid %zu len %jd avail %jd\n",
		    len, (intmax_t)MIDIQ_LEN(m->outq),
		    (intmax_t)MIDIQ_AVAIL(m->outq)));

		if (midi_dumpraw)
			for (i = 0; i < used; i++)
				kprintf("%x ", buf[i]);

		MIDIQ_ENQ(m->outq, buf, used);
		len -= used;

		/*
	         * Inform the bottom half that data can be written
	         */
		if (!(m->flags & M_TXEN)) {
			m->flags |= M_TXEN;
			MPU_CALLBACK(m, m->cookie, m->flags);
		}
	}
	/*
	 * If we Made it here then transfer is good
	 */
	if (midi_dumpraw)
		kprintf("\n");

	retval = 0;
err1:	lockmgr(&m->qlock, LK_RELEASE);
	lockmgr(&m->lock, LK_RELEASE);
err0:	return retval;
}

static int
midisynth_killnote(void *n, uint8_t chn, uint8_t note, uint8_t vel)
{
	u_char c[3];


	if (note > 127 || chn > 15)
		return (EINVAL);

	if (vel > 127)
		vel = 127;

	if (vel == 64) {
		c[0] = 0x90 | (chn & 0x0f);	/* Note on. */
		c[1] = (u_char)note;
		c[2] = 0;
	} else {
		c[0] = 0x80 | (chn & 0x0f);	/* Note off. */
		c[1] = (u_char)note;
		c[2] = (u_char)vel;
	}

	return midisynth_writeraw(n, c, 3);
}

static int
midisynth_setinstr(void *n, uint8_t chn, uint16_t instr)
{
	u_char c[2];

	if (instr > 127 || chn > 15)
		return EINVAL;

	c[0] = 0xc0 | (chn & 0x0f);	/* Progamme change. */
	c[1] = instr + midi_instroff;

	return midisynth_writeraw(n, c, 2);
}

static int
midisynth_startnote(void *n, uint8_t chn, uint8_t note, uint8_t vel)
{
	u_char c[3];

	if (note > 127 || chn > 15)
		return EINVAL;

	if (vel > 127)
		vel = 127;

	c[0] = 0x90 | (chn & 0x0f);	/* Note on. */
	c[1] = (u_char)note;
	c[2] = (u_char)vel;

	return midisynth_writeraw(n, c, 3);
}
static int
midisynth_alloc(void *n, uint8_t chan, uint8_t note)
{
	return chan;
}

static int
midisynth_controller(void *n, uint8_t chn, uint8_t ctrlnum, uint16_t val)
{
	u_char c[3];

	if (ctrlnum > 127 || chn > 15)
		return EINVAL;

	c[0] = 0xb0 | (chn & 0x0f);	/* Control Message. */
	c[1] = ctrlnum;
	c[2] = val;
	return midisynth_writeraw(n, c, 3);
}

static int
midisynth_bender(void *n, uint8_t chn, uint16_t val)
{
	u_char c[3];


	if (val > 16383 || chn > 15)
		return EINVAL;

	c[0] = 0xe0 | (chn & 0x0f);	/* Pitch bend. */
	c[1] = (u_char)val & 0x7f;
	c[2] = (u_char)(val >> 7) & 0x7f;

	return midisynth_writeraw(n, c, 3);
}

/*
 * Single point of midi destructions.
 */
static int
midi_destroy(struct snd_midi *m, int midiuninit)
{

	KKASSERT(lockowned(&midistat_lock));
	KKASSERT(lockowned(&m->lock));

	MIDI_DEBUG(3, kprintf("midi_destroy\n"));
	m->dev->si_drv1 = NULL;
	lockmgr(&m->lock, LK_RELEASE);	/* XXX */
	destroy_dev(m->dev);
	TAILQ_REMOVE(&midi_devs, m, link);
	if (midiuninit)
		MPU_UNINIT(m, m->cookie);
	kfree(MIDIQ_BUF(m->inq), M_MIDI);
	kfree(MIDIQ_BUF(m->outq), M_MIDI);
	lockuninit(&m->qlock);
	lockuninit(&m->lock);
	kfree(m, M_MIDI);
	return 0;
}

/*
 * Load and unload functions, creates the /dev/midistat device
 */

static int
midi_load(void)
{
	lockinit(&midistat_lock, "midistat lock", 0, LK_CANRECURSE);
	TAILQ_INIT(&midi_devs);		/* Initialize the queue. */

	midistat_dev = make_dev(&midistat_ops,
	    MIDIMKMINOR(0, MIDI_DEV_MIDICTL, 0),
	    UID_ROOT, GID_WHEEL, 0666, "midistat");

	return 0;
}

static int
midi_unload(void)
{
	struct snd_midi *m;
	int retval;

	MIDI_DEBUG(1, kprintf("midi_unload()\n"));
	retval = EBUSY;
	lockmgr(&midistat_lock, LK_EXCLUSIVE);
	if (midistat_isopen)
		goto exit0;

	TAILQ_FOREACH(m, &midi_devs, link) {
		lockmgr(&m->lock, LK_EXCLUSIVE);
		if (m->busy)
			retval = EBUSY;
		else
			retval = midi_destroy(m, 1);
		if (retval)
			goto exit1;
	}

	lockmgr(&midistat_lock, LK_RELEASE);	/* XXX */

	destroy_dev(midistat_dev);
	/*
	 * Made it here then unload is complete
	 */
	lockuninit(&midistat_lock);
	return 0;

exit1:
	lockmgr(&m->lock, LK_RELEASE);
exit0:
	lockmgr(&midistat_lock, LK_RELEASE);
	if (retval)
		MIDI_DEBUG(2, kprintf("midi_unload: failed\n"));
	return retval;
}

extern int seq_modevent(module_t mod, int type, void *data);

static int
midi_modevent(module_t mod, int type, void *data)
{
	int retval;

	retval = 0;

	switch (type) {
	case MOD_LOAD:
		retval = midi_load();
#if 0
		if (retval == 0)
			retval = seq_modevent(mod, type, data);
#endif
		break;

	case MOD_UNLOAD:
		retval = midi_unload();
#if 0
		if (retval == 0)
			retval = seq_modevent(mod, type, data);
#endif
		break;

	default:
		break;
	}

	return retval;
}

kobj_t
midimapper_addseq(void *arg1, int *unit, void **cookie)
{
	unit = 0;

	return (kobj_t)arg1;
}

int
midimapper_open(void *arg1, void **cookie)
{
	int retval = 0;
	struct snd_midi *m;

	lockmgr(&midistat_lock, LK_EXCLUSIVE);

	TAILQ_FOREACH(m, &midi_devs, link) {
		retval++;
	}

	lockmgr(&midistat_lock, LK_RELEASE);
	return retval;
}

int
midimapper_close(void *arg1, void *cookie)
{
	return 0;
}

kobj_t
midimapper_fetch_synth(void *arg, void *cookie, int unit)
{
	struct snd_midi *m;
	int retval = 0;

	lockmgr(&midistat_lock, LK_EXCLUSIVE);

	TAILQ_FOREACH(m, &midi_devs, link) {
		if (unit == retval) {
			lockmgr(&midistat_lock, LK_RELEASE);
			return (kobj_t)m->synth;
		}
		retval++;
	}

	lockmgr(&midistat_lock, LK_RELEASE);
	return NULL;
}

DEV_MODULE(midi, midi_modevent, NULL);
MODULE_VERSION(midi, 1);

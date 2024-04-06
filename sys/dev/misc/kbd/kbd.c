/*
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/kbd/kbd.c,v 1.17.2.2 2001/07/30 16:46:43 yokota Exp $
 */
/*
 * Generic keyboard driver.
 */

#include "opt_kbd.h"
#include "opt_evdev.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/caps.h>
#include <sys/conf.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <sys/event.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <sys/thread.h>

#include <machine/console.h>

#include "kbdreg.h"

#ifdef EVDEV_SUPPORT
#include <dev/misc/evdev/evdev.h>
#include <dev/misc/evdev/input.h>
#endif

#if 0
#define lwkt_gettoken(x)
#define lwkt_reltoken(x)
#endif

#define KBD_INDEX(dev)	minor(dev)

#define KB_QSIZE	512
#define KB_BUFSIZE	64

struct genkbd_softc {
	int		gkb_flags;	/* flag/status bits */
#define KB_ASLEEP	(1 << 0)
	struct kqinfo	gkb_rkq;
	char		gkb_q[KB_QSIZE];		/* input queue */
	unsigned int	gkb_q_start;
	unsigned int	gkb_q_length;
};

typedef struct genkbd_softc *genkbd_softc_t;

static	SLIST_HEAD(, keyboard_driver) keyboard_drivers =
	SLIST_HEAD_INITIALIZER(keyboard_drivers);

SET_DECLARE(kbddriver_set, const keyboard_driver_t);

/* local arrays */

/*
 * We need at least one entry each in order to initialize a keyboard
 * for the kernel console.  The arrays will be increased dynamically
 * when necessary.
 */

static keyboard_t *keyboard[KBD_MAXKEYBOARDS];

keyboard_switch_t *kbdsw[KBD_MAXKEYBOARDS];

/*
 * Low-level keyboard driver functions.
 *
 * Keyboard subdrivers, such as the AT keyboard driver and the USB keyboard
 * driver, call these functions to initialize the keyboard_t structure
 * and register it to the virtual keyboard driver `kbd'.
 *
 * The reinit call is made when a driver has partially detached a keyboard
 * but does not unregistered it, then wishes to reinitialize it later on.
 * This is how the USB keyboard driver handles the 'default' keyboard,
 * because unregistering the keyboard associated with the console will
 * destroy its console association forever.
 */
void
kbd_reinit_struct(keyboard_t *kbd, int config, int pref)
{
	lwkt_gettoken(&kbd_token);
	kbd->kb_flags |= KB_NO_DEVICE;	/* device has not been found */
	kbd->kb_config = config & ~KB_CONF_PROBE_ONLY;
	kbd->kb_led = 0;		/* unknown */
	kbd->kb_data = NULL;
	kbd->kb_keymap = NULL;
	kbd->kb_accentmap = NULL;
	kbd->kb_fkeytab = NULL;
	kbd->kb_fkeytab_size = 0;
	kbd->kb_delay1 = KB_DELAY1;	/* these values are advisory only */
	kbd->kb_delay2 = KB_DELAY2;
	kbd->kb_count = 0;
	kbd->kb_pref = pref;
	bzero(kbd->kb_lastact, sizeof(kbd->kb_lastact));
	lwkt_reltoken(&kbd_token);
}

/* initialize the keyboard_t structure */
void
kbd_init_struct(keyboard_t *kbd, char *name, int type, int unit, int config,
		int pref, int port, int port_size)
{
	lwkt_gettoken(&kbd_token);
	kbd->kb_flags = 0;
	kbd->kb_name = name;
	kbd->kb_type = type;
	kbd->kb_unit = unit;
	kbd->kb_io_base = port;
	kbd->kb_io_size = port_size;
	kbd_reinit_struct(kbd, config, pref);
	lockinit(&kbd->kb_lock, name, 0, LK_CANRECURSE);
	lwkt_reltoken(&kbd_token);
}

void
kbd_set_maps(keyboard_t *kbd, keymap_t *keymap, accentmap_t *accmap,
	     fkeytab_t *fkeymap, int fkeymap_size)
{
	lwkt_gettoken(&kbd_token);
	kbd->kb_keymap = keymap;
	kbd->kb_accentmap = accmap;
	kbd->kb_fkeytab = fkeymap;
	kbd->kb_fkeytab_size = fkeymap_size;
	lwkt_reltoken(&kbd_token);
}

/* declare a new keyboard driver */
int
kbd_add_driver(keyboard_driver_t *driver)
{
	lwkt_gettoken(&kbd_token);
	if (SLIST_NEXT(driver, link)) {
		lwkt_reltoken(&kbd_token);
		return EINVAL;
	}
	SLIST_INSERT_HEAD(&keyboard_drivers, driver, link);
	lwkt_reltoken(&kbd_token);
	return 0;
}

int
kbd_delete_driver(keyboard_driver_t *driver)
{
	lwkt_gettoken(&kbd_token);
	SLIST_REMOVE(&keyboard_drivers, driver, keyboard_driver, link);
	SLIST_NEXT(driver, link) = NULL;
	lwkt_reltoken(&kbd_token);
	return 0;
}

/* register a keyboard and associate it with a function table */
int
kbd_register(keyboard_t *kbd)
{
	const keyboard_driver_t **list;
	const keyboard_driver_t *p;
	keyboard_t *mux;
	keyboard_info_t ki;
	int index;

	lwkt_gettoken(&kbd_token);
	mux = kbd_get_keyboard(kbd_find_keyboard("kbdmux", -1));

	for (index = 0; index < KBD_MAXKEYBOARDS; ++index) {
		if (keyboard[index] == NULL)
			break;
	}
	if (index >= KBD_MAXKEYBOARDS) {
		lwkt_reltoken(&kbd_token);
		return -1;
	}

	kbd->kb_index = index;
	KBD_UNBUSY(kbd);
	KBD_VALID(kbd);
	kbd->kb_active = 0;	/* disabled until someone calls kbd_enable() */
	kbd->kb_token = NULL;
	kbd->kb_callback.kc_func = NULL;
	kbd->kb_callback.kc_arg = NULL;
	callout_init_mp(&kbd->kb_atkbd_timeout_ch);

	SLIST_FOREACH(p, &keyboard_drivers, link) {
		if (strcmp(p->name, kbd->kb_name) == 0) {
			keyboard[index] = kbd;
			kbdsw[index] = p->kbdsw;

			if (mux != NULL) {
				bzero(&ki, sizeof(ki));
				strcpy(ki.kb_name, kbd->kb_name);
				ki.kb_unit = kbd->kb_unit;
				kbd_ioctl(mux, KBADDKBD, (caddr_t) &ki);
			}

			lwkt_reltoken(&kbd_token);
			return index;
		}
	}
	SET_FOREACH(list, kbddriver_set) {
		p = *list;
		if (strcmp(p->name, kbd->kb_name) == 0) {
			keyboard[index] = kbd;
			kbdsw[index] = p->kbdsw;

			if (mux != NULL) {
				bzero(&ki, sizeof(ki));
				strcpy(ki.kb_name, kbd->kb_name);
				ki.kb_unit = kbd->kb_unit;
				kbd_ioctl(mux, KBADDKBD, (caddr_t) &ki);
			}

			lwkt_reltoken(&kbd_token);
			return index;
		}
	}

	lwkt_reltoken(&kbd_token);
	return -1;
}

int
kbd_unregister(keyboard_t *kbd)
{
	int error;

	KBD_LOCK_ASSERT(kbd);
	lwkt_gettoken(&kbd_token);
	if ((kbd->kb_index < 0) || (kbd->kb_index >= KBD_MAXKEYBOARDS)) {
		lwkt_reltoken(&kbd_token);
		return ENOENT;
	}
	if (keyboard[kbd->kb_index] != kbd) {
		lwkt_reltoken(&kbd_token);
		return ENOENT;
	}

	callout_stop(&kbd->kb_atkbd_timeout_ch);
	if (KBD_IS_BUSY(kbd)) {
		error = (*kbd->kb_callback.kc_func)(kbd, KBDIO_UNLOADING,
						    kbd->kb_callback.kc_arg);
		if (error) {
			lwkt_reltoken(&kbd_token);
			return error;
		}
		if (KBD_IS_BUSY(kbd)) {
			lwkt_reltoken(&kbd_token);
			return EBUSY;
		}
	}
	KBD_CONFIG_LOST(kbd);
	KBD_INVALID(kbd);
	keyboard[kbd->kb_index] = NULL;
	kbdsw[kbd->kb_index] = NULL;

	KBD_ALWAYS_UNLOCK(kbd);
	lockuninit(&kbd->kb_lock);

	lwkt_reltoken(&kbd_token);
	return 0;
}

/* find a funciton table by the driver name */
keyboard_switch_t *
kbd_get_switch(char *driver)
{
	const keyboard_driver_t **list;
	const keyboard_driver_t *p;

	lwkt_gettoken(&kbd_token);

	SLIST_FOREACH(p, &keyboard_drivers, link) {
		if (strcmp(p->name, driver) == 0) {
			lwkt_reltoken(&kbd_token);
			return p->kbdsw;
		}
	}
	SET_FOREACH(list, kbddriver_set) {
		p = *list;
		if (strcmp(p->name, driver) == 0) {
			lwkt_reltoken(&kbd_token);
			return p->kbdsw;
		}
	}

	lwkt_reltoken(&kbd_token);
	return NULL;
}

/*
 * Keyboard client functions
 * Keyboard clients, such as the console driver `syscons' and the keyboard
 * cdev driver, use these functions to claim and release a keyboard for
 * exclusive use.
 */
/*
 * find the keyboard specified by a driver name and a unit number
 * starting at given index
 */
int
kbd_find_keyboard2(char *driver, int unit, int index, int legacy)
{
	int i;
	int pref;
	int pref_index;

	pref = 0;
	pref_index = -1;

	lwkt_gettoken(&kbd_token);
	if ((index < 0) || (index >= KBD_MAXKEYBOARDS)) {
		lwkt_reltoken(&kbd_token);
		return (-1);
	}

	for (i = index; i < KBD_MAXKEYBOARDS; ++i) {
		if (keyboard[i] == NULL)
			continue;
		if (!KBD_IS_VALID(keyboard[i]))
			continue;
		if (strcmp("*", driver) && strcmp(keyboard[i]->kb_name, driver))
			continue;
		if ((unit != -1) && (keyboard[i]->kb_unit != unit))
			continue;
		/*
		 * If we are in legacy mode, we do the old preference magic and
		 * don't return on the first found unit.
		 */
		if (legacy) {
			if (pref <= keyboard[i]->kb_pref) {
				pref = keyboard[i]->kb_pref;
				pref_index = i;
			}
		} else {
			lwkt_reltoken(&kbd_token);
			return i;
		}
	}

	if (!legacy)
		KKASSERT(pref_index == -1);

	lwkt_reltoken(&kbd_token);
	return (pref_index);
}

/* find the keyboard specified by a driver name and a unit number */
int
kbd_find_keyboard(char *driver, int unit)
{
	return (kbd_find_keyboard2(driver, unit, 0, 1));
}

/* allocate a keyboard */
int
kbd_allocate(char *driver, int unit, void *id, kbd_callback_func_t *func,
	     void *arg)
{
	int index;

	if (func == NULL)
		return -1;

	lwkt_gettoken(&kbd_token);

	index = kbd_find_keyboard(driver, unit);
	if (index >= 0) {
		if (KBD_IS_BUSY(keyboard[index])) {
			lwkt_reltoken(&kbd_token);
			return -1;
		}
		keyboard[index]->kb_token = id;
		KBD_BUSY(keyboard[index]);
		keyboard[index]->kb_callback.kc_func = func;
		keyboard[index]->kb_callback.kc_arg = arg;
		kbd_clear_state(keyboard[index]);
	}

	lwkt_reltoken(&kbd_token);
	return index;
}

int
kbd_release(keyboard_t *kbd, void *id)
{
	int error;

	lwkt_gettoken(&kbd_token);

	if (!KBD_IS_VALID(kbd) || !KBD_IS_BUSY(kbd)) {
		error = EINVAL;
	} else if (kbd->kb_token != id) {
		error = EPERM;
	} else {
		kbd->kb_token = NULL;
		KBD_UNBUSY(kbd);
		kbd->kb_callback.kc_func = NULL;
		kbd->kb_callback.kc_arg = NULL;
		kbd_clear_state(kbd);
		error = 0;
	}

	lwkt_reltoken(&kbd_token);
	return error;
}

int
kbd_change_callback(keyboard_t *kbd, void *id, kbd_callback_func_t *func,
		    void *arg)
{
	int error;

	lwkt_gettoken(&kbd_token);

	if (!KBD_IS_VALID(kbd) || !KBD_IS_BUSY(kbd)) {
		error = EINVAL;
	} else if (kbd->kb_token != id) {
		error = EPERM;
	} else if (func == NULL) {
		error = EINVAL;
	} else {
		kbd->kb_callback.kc_func = func;
		kbd->kb_callback.kc_arg = arg;
		error = 0;
	}

	lwkt_reltoken(&kbd_token);
	return error;
}

/* get a keyboard structure */
keyboard_t *
kbd_get_keyboard(int index)
{
	keyboard_t *kbd;

	lwkt_gettoken(&kbd_token);
	if ((index < 0) || (index >= KBD_MAXKEYBOARDS)) {
		lwkt_reltoken(&kbd_token);
		return NULL;
	}
	if (keyboard[index] == NULL) {
		lwkt_reltoken(&kbd_token);
		return NULL;
	}
	if (!KBD_IS_VALID(keyboard[index])) {
		lwkt_reltoken(&kbd_token);
		return NULL;
	}
	kbd = keyboard[index];
	lwkt_reltoken(&kbd_token);

	return kbd;
}

/*
 * The back door for the console driver; configure keyboards
 * This function is for the kernel console to initialize keyboards
 * at very early stage.
 */

int
kbd_configure(int flags)
{
	const keyboard_driver_t **list;
	const keyboard_driver_t *p;

	lwkt_gettoken(&kbd_token);

	SLIST_FOREACH(p, &keyboard_drivers, link) {
		if (p->configure != NULL)
			(*p->configure)(flags);
	}
	SET_FOREACH(list, kbddriver_set) {
		p = *list;
		if (p->configure != NULL)
			(*p->configure)(flags);
	}

	lwkt_reltoken(&kbd_token);
	return 0;
}

#ifdef KBD_INSTALL_CDEV

/*
 * Virtual keyboard cdev driver functions
 * The virtual keyboard driver dispatches driver functions to
 * appropriate subdrivers.
 */

#define KBD_UNIT(dev)	minor(dev)

static d_open_t		genkbdopen;
static d_close_t	genkbdclose;
static d_read_t		genkbdread;
static d_write_t	genkbdwrite;
static d_ioctl_t	genkbdioctl;
static d_kqfilter_t	genkbdkqfilter;

static void genkbdfiltdetach(struct knote *);
static int genkbdfilter(struct knote *, long);

static struct dev_ops kbd_ops = {
	{ "kbd", 0, D_MPSAFE },
	.d_open =	genkbdopen,
	.d_close =	genkbdclose,
	.d_read =	genkbdread,
	.d_write =	genkbdwrite,
	.d_ioctl =	genkbdioctl,
	.d_kqfilter =	genkbdkqfilter
};

/*
 * Attach a keyboard.
 *
 * NOTE: The usb driver does not detach the default keyboard if it is
 *	 unplugged, but calls kbd_attach() when it is plugged back in.
 */
int
kbd_attach(keyboard_t *kbd)
{
	cdev_t dev;
	char tbuf[MAKEDEV_MINNBUF];

	lwkt_gettoken(&kbd_token);
	if (kbd->kb_index >= KBD_MAXKEYBOARDS) {
		lwkt_reltoken(&kbd_token);
		return EINVAL;
	}
	if (keyboard[kbd->kb_index] != kbd) {
		lwkt_reltoken(&kbd_token);
		return EINVAL;
	}

	if (kbd->kb_dev == NULL) {
		kbd->kb_dev = make_dev(&kbd_ops, kbd->kb_index,
				       UID_ROOT, GID_WHEEL, 0600, "kbd%s",
				       makedev_unit_b32(tbuf, kbd->kb_index));
	}
	dev = kbd->kb_dev;
	if (dev->si_drv1 == NULL) {
		dev->si_drv1 = kmalloc(sizeof(struct genkbd_softc), M_DEVBUF,
				       M_WAITOK);
	}
	bzero(dev->si_drv1, sizeof(struct genkbd_softc));

	kprintf("kbd%d at %s%d\n", kbd->kb_index, kbd->kb_name, kbd->kb_unit);
	lwkt_reltoken(&kbd_token);
	return 0;
}

int
kbd_detach(keyboard_t *kbd)
{
	cdev_t dev;

	lwkt_gettoken(&kbd_token);

	if (kbd->kb_index >= KBD_MAXKEYBOARDS) {
		lwkt_reltoken(&kbd_token);
		return EINVAL;
	}
	if (keyboard[kbd->kb_index] != kbd) {
		lwkt_reltoken(&kbd_token);
		return EINVAL;
	}

	if ((dev = kbd->kb_dev) != NULL) {
		if (dev->si_drv1) {
			kfree(dev->si_drv1, M_DEVBUF);
			dev->si_drv1 = NULL;
		}
		kbd->kb_dev = NULL;
	}
	dev_ops_remove_minor(&kbd_ops, kbd->kb_index);
	lwkt_reltoken(&kbd_token);
	return 0;
}

/*
 * Generic keyboard cdev driver functions
 * Keyboard subdrivers may call these functions to implement common
 * driver functions.
 */

static void
genkbd_putc(genkbd_softc_t sc, char c)
{
	unsigned int p;

	lwkt_gettoken(&kbd_token);

	if (sc->gkb_q_length == KB_QSIZE) {
		lwkt_reltoken(&kbd_token);
		return;
	}

	p = (sc->gkb_q_start + sc->gkb_q_length) % KB_QSIZE;
	sc->gkb_q[p] = c;
	sc->gkb_q_length++;

	lwkt_reltoken(&kbd_token);
}

static size_t
genkbd_getc(genkbd_softc_t sc, char *buf, size_t len)
{

	lwkt_gettoken(&kbd_token);

	/* Determine copy size. */
	if (sc->gkb_q_length == 0) {
		lwkt_reltoken(&kbd_token);
		return (0);
	}
	if (len >= sc->gkb_q_length)
		len = sc->gkb_q_length;
	if (len >= KB_QSIZE - sc->gkb_q_start)
		len = KB_QSIZE - sc->gkb_q_start;

	/* Copy out data and progress offset. */
	memcpy(buf, sc->gkb_q + sc->gkb_q_start, len);
	sc->gkb_q_start = (sc->gkb_q_start + len) % KB_QSIZE;
	sc->gkb_q_length -= len;

	lwkt_reltoken(&kbd_token);
	return (len);
}

static kbd_callback_func_t genkbd_event;

static int
genkbdopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	keyboard_t *kbd;
	genkbd_softc_t sc;
	int i;

	/*
	 * Disallow access to disk volumes if RESTRICTEDROOT
	 */
	if (caps_priv_check_self(SYSCAP_RESTRICTEDROOT))
		return (EPERM);

	lwkt_gettoken(&kbd_token);
	sc = dev->si_drv1;
	kbd = kbd_get_keyboard(KBD_INDEX(dev));
	if ((sc == NULL) || (kbd == NULL) || !KBD_IS_VALID(kbd)) {
		lwkt_reltoken(&kbd_token);
		return ENXIO;
	}
	i = kbd_allocate(kbd->kb_name, kbd->kb_unit, sc,
			 genkbd_event, sc);
	if (i < 0) {
		lwkt_reltoken(&kbd_token);
		return EBUSY;
	}
	/* assert(i == kbd->kb_index) */
	/* assert(kbd == kbd_get_keyboard(i)) */

	/*
	 * NOTE: even when we have successfully claimed a keyboard,
	 * the device may still be missing (!KBD_HAS_DEVICE(kbd)).
	 */

	sc->gkb_q_length = 0;
	lwkt_reltoken(&kbd_token);

	return 0;
}

static int
genkbdclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	keyboard_t *kbd;
	genkbd_softc_t sc;

	/*
	 * NOTE: the device may have already become invalid.
	 * kbd == NULL || !KBD_IS_VALID(kbd)
	 */
	lwkt_gettoken(&kbd_token);
	sc = dev->si_drv1;
	kbd = kbd_get_keyboard(KBD_INDEX(dev));
	if ((sc == NULL) || (kbd == NULL) || !KBD_IS_VALID(kbd)) {
		/* XXX: we shall be forgiving and don't report error... */
	} else {
		kbd_release(kbd, sc);
	}
	lwkt_reltoken(&kbd_token);
	return 0;
}

static int
genkbdread(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	keyboard_t *kbd;
	genkbd_softc_t sc;
	u_char buffer[KB_BUFSIZE];
	int len;
	int error;

	/* wait for input */
	lwkt_gettoken(&kbd_token);
	sc = dev->si_drv1;
	kbd = kbd_get_keyboard(KBD_INDEX(dev));
	if ((sc == NULL) || (kbd == NULL) || !KBD_IS_VALID(kbd)) {
		lwkt_reltoken(&kbd_token);
		return ENXIO;
	}
	while (sc->gkb_q_length == 0) {
		if (ap->a_ioflag & IO_NDELAY) { /* O_NONBLOCK? */
			lwkt_reltoken(&kbd_token);
			return EWOULDBLOCK;
		}
		sc->gkb_flags |= KB_ASLEEP;
		error = tsleep((caddr_t)sc, PCATCH, "kbdrea", 0);
		kbd = kbd_get_keyboard(KBD_INDEX(dev));
		if ((kbd == NULL) || !KBD_IS_VALID(kbd)) {
			lwkt_reltoken(&kbd_token);
			return ENXIO;	/* our keyboard has gone... */
		}
		if (error) {
			sc->gkb_flags &= ~KB_ASLEEP;
			lwkt_reltoken(&kbd_token);
			return error;
		}
	}
	lwkt_reltoken(&kbd_token);

	/* copy as much input as possible */
	error = 0;
	while (uio->uio_resid > 0) {
		len = (int)szmin(uio->uio_resid, sizeof(buffer));
		len = genkbd_getc(sc, buffer, len);
		if (len <= 0)
			break;
		error = uiomove(buffer, (size_t)len, uio);
		if (error)
			break;
	}

	return error;
}

static int
genkbdwrite(struct dev_write_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	keyboard_t *kbd;

	lwkt_gettoken(&kbd_token);
	kbd = kbd_get_keyboard(KBD_INDEX(dev));
	if ((kbd == NULL) || !KBD_IS_VALID(kbd)) {
		lwkt_reltoken(&kbd_token);
		return ENXIO;
	}
	lwkt_reltoken(&kbd_token);
	return ENODEV;
}

static int
genkbdioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	keyboard_t *kbd;
	int error;

	lwkt_gettoken(&kbd_token);
	kbd = kbd_get_keyboard(KBD_INDEX(dev));
	if ((kbd == NULL) || !KBD_IS_VALID(kbd)) {
		lwkt_reltoken(&kbd_token);
		return ENXIO;
	}
	error = kbd_ioctl(kbd, ap->a_cmd, ap->a_data);
	if (error == ENOIOCTL)
		error = ENODEV;

	lwkt_reltoken(&kbd_token);
	return error;
}

static struct filterops genkbdfiltops =
	{ FILTEROP_ISFD, NULL, genkbdfiltdetach, genkbdfilter };

static int
genkbdkqfilter(struct dev_kqfilter_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct knote *kn = ap->a_kn;
	genkbd_softc_t sc;
	struct klist *klist;

	ap->a_result = 0;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &genkbdfiltops;
		kn->kn_hook = (caddr_t)dev;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		return (0);
	}

	sc = dev->si_drv1;
	klist = &sc->gkb_rkq.ki_note;
	knote_insert(klist, kn);

	return (0);
}

static void
genkbdfiltdetach(struct knote *kn)
{
	cdev_t dev = (cdev_t)kn->kn_hook;
	genkbd_softc_t sc;
	struct klist *klist;

	sc = dev->si_drv1;
	klist = &sc->gkb_rkq.ki_note;
	knote_remove(klist, kn);
}

static int
genkbdfilter(struct knote *kn, long hint)
{
	cdev_t dev = (cdev_t)kn->kn_hook;
	keyboard_t *kbd;
	genkbd_softc_t sc;
	int ready = 0;

	lwkt_gettoken(&kbd_token);
	sc = dev->si_drv1;
        kbd = kbd_get_keyboard(KBD_INDEX(dev));
	if ((sc == NULL) || (kbd == NULL) || !KBD_IS_VALID(kbd)) {
		/* The keyboard has gone */
		kn->kn_flags |= (EV_EOF | EV_NODATA);
		ready = 1;
	} else {
		if (sc->gkb_q_length > 0)
                        ready = 1;
        }
	lwkt_reltoken(&kbd_token);

	return (ready);
}

static int
genkbd_event(keyboard_t *kbd, int event, void *arg)
{
	genkbd_softc_t sc;
	size_t len;
	u_char *cp;
	int mode;
	int c;

	lwkt_gettoken(&kbd_token);
	/* assert(KBD_IS_VALID(kbd)) */
	sc = (genkbd_softc_t)arg;

	switch (event) {
	case KBDIO_KEYINPUT:
		break;
	case KBDIO_UNLOADING:
		/* the keyboard is going... */
		kbd_release(kbd, sc);
		if (sc->gkb_flags & KB_ASLEEP) {
			sc->gkb_flags &= ~KB_ASLEEP;
			wakeup((caddr_t)sc);
		}
		KNOTE(&sc->gkb_rkq.ki_note, 0);
		lwkt_reltoken(&kbd_token);
		return 0;
	default:
		lwkt_reltoken(&kbd_token);
		return EINVAL;
	}

	/* obtain the current key input mode */
	if (kbd_ioctl(kbd, KDGKBMODE, (caddr_t)&mode))
		mode = K_XLATE;

	/* read all pending input */
	while (kbd_check_char(kbd)) {
		c = kbd_read_char(kbd, FALSE);
		if (c == NOKEY)
			continue;
		if (c == ERRKEY)	/* XXX: ring bell? */
			continue;
		if (!KBD_IS_BUSY(kbd))
			/* the device is not open, discard the input */
			continue;

		/* store the byte as is for K_RAW and K_CODE modes */
		if (mode != K_XLATE) {
			genkbd_putc(sc, KEYCHAR(c));
			continue;
		}

		/* K_XLATE */
		if (c & RELKEY)	/* key release is ignored */
			continue;

		/* process special keys; most of them are just ignored... */
		if (c & SPCLKEY) {
			switch (KEYCHAR(c)) {
			default:
				/* ignore them... */
				continue;
			case BTAB:	/* a backtab: ESC [ Z */
				genkbd_putc(sc, 0x1b);
				genkbd_putc(sc, '[');
				genkbd_putc(sc, 'Z');
				continue;
			}
		}

		/* normal chars, normal chars with the META, function keys */
		switch (KEYFLAGS(c)) {
		case 0:			/* a normal char */
			genkbd_putc(sc, KEYCHAR(c));
			break;
		case MKEY:		/* the META flag: prepend ESC */
			genkbd_putc(sc, 0x1b);
			genkbd_putc(sc, KEYCHAR(c));
			break;
		case FKEY | SPCLKEY:	/* a function key, return string */
			cp = kbd_get_fkeystr(kbd, KEYCHAR(c), &len);
			if (cp != NULL) {
				while (len-- >  0)
					genkbd_putc(sc, *cp++);
			}
			break;
		}
	}

	/* wake up sleeping/polling processes */
	if (sc->gkb_q_length > 0) {
		if (sc->gkb_flags & KB_ASLEEP) {
			sc->gkb_flags &= ~KB_ASLEEP;
			wakeup((caddr_t)sc);
		}
		KNOTE(&sc->gkb_rkq.ki_note, 0);
	}

	lwkt_reltoken(&kbd_token);
	return 0;
}

#endif /* KBD_INSTALL_CDEV */

/*
 * Generic low-level keyboard functions
 * The low-level functions in the keyboard subdriver may use these
 * functions.
 */

int
genkbd_commonioctl(keyboard_t *kbd, u_long cmd, caddr_t arg)
{
	keyarg_t *keyp;
	fkeyarg_t *fkeyp;
	int i;

	lwkt_gettoken(&kbd_token);
	switch (cmd) {

	case KDGKBINFO:		/* get keyboard information */
		((keyboard_info_t *)arg)->kb_index = kbd->kb_index;
		i = imin(strlen(kbd->kb_name) + 1,
			 sizeof(((keyboard_info_t *)arg)->kb_name));
		bcopy(kbd->kb_name, ((keyboard_info_t *)arg)->kb_name, i);
		((keyboard_info_t *)arg)->kb_unit = kbd->kb_unit;
		((keyboard_info_t *)arg)->kb_type = kbd->kb_type;
		((keyboard_info_t *)arg)->kb_config = kbd->kb_config;
		((keyboard_info_t *)arg)->kb_flags = kbd->kb_flags;
		break;

	case KDGKBTYPE:		/* get keyboard type */
		*(int *)arg = kbd->kb_type;
		break;

	case KDGETREPEAT:	/* get keyboard repeat rate */
		((int *)arg)[0] = kbd->kb_delay1;
		((int *)arg)[1] = kbd->kb_delay2; 
		break;

	case GIO_KEYMAP:	/* get keyboard translation table */
		bcopy(kbd->kb_keymap, arg, sizeof(*kbd->kb_keymap));
		break;
	case PIO_KEYMAP:	/* set keyboard translation table */
#ifndef KBD_DISABLE_KEYMAP_LOAD
		bzero(kbd->kb_accentmap, sizeof(*kbd->kb_accentmap));
		bcopy(arg, kbd->kb_keymap, sizeof(*kbd->kb_keymap));
		break;
#else
		lwkt_reltoken(&kbd_token);
		return ENODEV;
#endif

	case GIO_KEYMAPENT:	/* get keyboard translation table entry */
		keyp = (keyarg_t *)arg;
		if (keyp->keynum >= sizeof(kbd->kb_keymap->key)
					/sizeof(kbd->kb_keymap->key[0])) {
			lwkt_reltoken(&kbd_token);
			return EINVAL;
		}
		bcopy(&kbd->kb_keymap->key[keyp->keynum], &keyp->key,
		      sizeof(keyp->key));
		break;
	case PIO_KEYMAPENT:	/* set keyboard translation table entry */
#ifndef KBD_DISABLE_KEYMAP_LOAD
		keyp = (keyarg_t *)arg;
		if (keyp->keynum >= sizeof(kbd->kb_keymap->key)
					/sizeof(kbd->kb_keymap->key[0])) {
			lwkt_reltoken(&kbd_token);
			return EINVAL;
		}
		bcopy(&keyp->key, &kbd->kb_keymap->key[keyp->keynum],
		      sizeof(keyp->key));
		break;
#else
		lwkt_reltoken(&kbd_token);
		return ENODEV;
#endif

	case GIO_DEADKEYMAP:	/* get accent key translation table */
		bcopy(kbd->kb_accentmap, arg, sizeof(*kbd->kb_accentmap));
		break;
	case PIO_DEADKEYMAP:	/* set accent key translation table */
#ifndef KBD_DISABLE_KEYMAP_LOAD
		bcopy(arg, kbd->kb_accentmap, sizeof(*kbd->kb_accentmap));
		break;
#else
		lwkt_reltoken(&kbd_token);
		return ENODEV;
#endif

	case GETFKEY:		/* get functionkey string */
		fkeyp = (fkeyarg_t *)arg;
		if (fkeyp->keynum >= kbd->kb_fkeytab_size) {
			lwkt_reltoken(&kbd_token);
			return EINVAL;
		}
		bcopy(kbd->kb_fkeytab[fkeyp->keynum].str, fkeyp->keydef,
		      kbd->kb_fkeytab[fkeyp->keynum].len);
		fkeyp->flen = kbd->kb_fkeytab[fkeyp->keynum].len;
		break;
	case SETFKEY:		/* set functionkey string */
#ifndef KBD_DISABLE_KEYMAP_LOAD
		fkeyp = (fkeyarg_t *)arg;
		if (fkeyp->keynum >= kbd->kb_fkeytab_size) {
			lwkt_reltoken(&kbd_token);
			return EINVAL;
		}
		kbd->kb_fkeytab[fkeyp->keynum].len = imin(fkeyp->flen, MAXFK);
		bcopy(fkeyp->keydef, kbd->kb_fkeytab[fkeyp->keynum].str,
		      kbd->kb_fkeytab[fkeyp->keynum].len);
		break;
#else
		lwkt_reltoken(&kbd_token);
		return ENODEV;
#endif

	default:
		lwkt_reltoken(&kbd_token);
		return ENOIOCTL;
	}

	lwkt_reltoken(&kbd_token);
	return 0;
}

/* get a pointer to the string associated with the given function key */
u_char *
genkbd_get_fkeystr(keyboard_t *kbd, int fkey, size_t *len)
{
	u_char *ch;

	if (kbd == NULL)
		return NULL;

	lwkt_gettoken(&kbd_token);
	fkey -= F_FN;
	if (fkey > kbd->kb_fkeytab_size) {
		lwkt_reltoken(&kbd_token);
		return NULL;
	}
	*len = kbd->kb_fkeytab[fkey].len;
	ch = kbd->kb_fkeytab[fkey].str;

	lwkt_reltoken(&kbd_token);
	return ch;
}

/* diagnostic dump */
static char *
get_kbd_type_name(int type)
{
	static struct {
		int type;
		char *name;
	} name_table[] = {
		{ KB_84,	"AT 84" },
		{ KB_101,	"AT 101/102" },
		{ KB_OTHER,	"generic" },
	};
	int i;

	for (i = 0; i < NELEM(name_table); ++i) {
		if (type == name_table[i].type)
			return name_table[i].name;
	}
	return "unknown";
}

void
genkbd_diag(keyboard_t *kbd, int level)
{
	if (level > 0) {
		kprintf("kbd%d: %s%d, %s (%d), config:0x%x, flags:0x%x", 
		       kbd->kb_index, kbd->kb_name, kbd->kb_unit,
		       get_kbd_type_name(kbd->kb_type), kbd->kb_type,
		       kbd->kb_config, kbd->kb_flags);
		if (kbd->kb_io_base > 0)
			kprintf(", port:0x%x-0x%x", kbd->kb_io_base, 
			       kbd->kb_io_base + kbd->kb_io_size - 1);
		kprintf("\n");
	}
}

#define set_lockkey_state(k, s, l)				\
	if (!((s) & l ## DOWN)) {				\
		int i;						\
		(s) |= l ## DOWN;				\
		(s) ^= l ## ED;					\
		i = (s) & LOCK_MASK;				\
		kbd_ioctl((k), KDSETLED, (caddr_t)&i); \
	}

static u_int
save_accent_key(keyboard_t *kbd, u_int key, int *accents)
{
	int i;

	lwkt_gettoken(&kbd_token);
	/* make an index into the accent map */
	i = key - F_ACC + 1;
	if ((i > kbd->kb_accentmap->n_accs)
	    || (kbd->kb_accentmap->acc[i - 1].accchar == 0)) {
		/* the index is out of range or pointing to an empty entry */
		*accents = 0;
		lwkt_reltoken(&kbd_token);
		return ERRKEY;
	}

	/* 
	 * If the same accent key has been hit twice, produce the accent char
	 * itself.
	 */
	if (i == *accents) {
		key = kbd->kb_accentmap->acc[i - 1].accchar;
		*accents = 0;
		lwkt_reltoken(&kbd_token);
		return key;
	}

	/* remember the index and wait for the next key  */
	*accents = i; 
	lwkt_reltoken(&kbd_token);
	return NOKEY;
}

static u_int
make_accent_char(keyboard_t *kbd, u_int ch, int *accents)
{
	struct acc_t *acc;
	int i;

	lwkt_gettoken(&kbd_token);
	acc = &kbd->kb_accentmap->acc[*accents - 1];
	*accents = 0;

	/* 
	 * If the accent key is followed by the space key,
	 * produce the accent char itself.
	 */
	if (ch == ' ') {
		lwkt_reltoken(&kbd_token);
		return acc->accchar;
	}

	/* scan the accent map */
	for (i = 0; i < NUM_ACCENTCHARS; ++i) {
		if (acc->map[i][0] == 0)	/* end of table */
			break;
		if (acc->map[i][0] == ch) {
			lwkt_reltoken(&kbd_token);
			return acc->map[i][1];
		}
	}
	lwkt_reltoken(&kbd_token);
	/* this char cannot be accented... */
	return ERRKEY;
}

int
genkbd_keyaction(keyboard_t *kbd, int keycode, int up, int *shiftstate,
		 int *accents)
{
	struct keyent_t *key;
	int state = *shiftstate;
	int action;
	int f;
	int i;

	lwkt_gettoken(&kbd_token);
	i = keycode;
	f = state & (AGRS | ALKED);
	if ((f == AGRS1) || (f == AGRS2) || (f == ALKED))
		i += ALTGR_OFFSET;
	key = &kbd->kb_keymap->key[i];
	i = ((state & SHIFTS) ? 1 : 0)
	    | ((state & CTLS) ? 2 : 0)
	    | ((state & ALTS) ? 4 : 0);
	if (((key->flgs & FLAG_LOCK_C) && (state & CLKED))
		|| ((key->flgs & FLAG_LOCK_N) && (state & NLKED)) )
		i ^= 1;

	if (up) {	/* break: key released */
		action = kbd->kb_lastact[keycode];
		kbd->kb_lastact[keycode] = NOP;
		switch (action) {
		case LSHA:
			if (state & SHIFTAON) {
				set_lockkey_state(kbd, state, ALK);
				state &= ~ALKDOWN;
			}
			action = LSH;
			/* FALL THROUGH */
		case LSH:
			state &= ~SHIFTS1;
			break;
		case RSHA:
			if (state & SHIFTAON) {
				set_lockkey_state(kbd, state, ALK);
				state &= ~ALKDOWN;
			}
			action = RSH;
			/* FALL THROUGH */
		case RSH:
			state &= ~SHIFTS2;
			break;
		case LCTRA:
			if (state & SHIFTAON) {
				set_lockkey_state(kbd, state, ALK);
				state &= ~ALKDOWN;
			}
			action = LCTR;
			/* FALL THROUGH */
		case LCTR:
			state &= ~CTLS1;
			break;
		case RCTRA:
			if (state & SHIFTAON) {
				set_lockkey_state(kbd, state, ALK);
				state &= ~ALKDOWN;
			}
			action = RCTR;
			/* FALL THROUGH */
		case RCTR:
			state &= ~CTLS2;
			break;
		case LALTA:
			if (state & SHIFTAON) {
				set_lockkey_state(kbd, state, ALK);
				state &= ~ALKDOWN;
			}
			action = LALT;
			/* FALL THROUGH */
		case LALT:
			state &= ~ALTS1;
			break;
		case RALTA:
			if (state & SHIFTAON) {
				set_lockkey_state(kbd, state, ALK);
				state &= ~ALKDOWN;
			}
			action = RALT;
			/* FALL THROUGH */
		case RALT:
			state &= ~ALTS2;
			break;
		case ASH:
			state &= ~AGRS1;
			break;
		case META:
			state &= ~METAS1;
			break;
		case NLK:
			state &= ~NLKDOWN;
			break;
		case CLK:
			state &= ~CLKDOWN;
			break;
		case SLK:
			state &= ~SLKDOWN;
			break;
		case ALK:
			state &= ~ALKDOWN;
			break;
		case NOP:
			/* release events of regular keys are not reported */
			*shiftstate &= ~SHIFTAON;
			lwkt_reltoken(&kbd_token);
			return NOKEY;
		}
		*shiftstate = state & ~SHIFTAON;
		lwkt_reltoken(&kbd_token);
		return (SPCLKEY | RELKEY | action);
	} else {	/* make: key pressed */
		action = key->map[i];
		state &= ~SHIFTAON;
		if (key->spcl & (0x80 >> i)) {
			/* special keys */
			if (kbd->kb_lastact[keycode] == NOP)
				kbd->kb_lastact[keycode] = action;
			if (kbd->kb_lastact[keycode] != action)
				action = NOP;
			switch (action) {
			/* LOCKING KEYS */
			case NLK:
				set_lockkey_state(kbd, state, NLK);
				break;
			case CLK:
				set_lockkey_state(kbd, state, CLK);
				break;
			case SLK:
				set_lockkey_state(kbd, state, SLK);
				break;
			case ALK:
				set_lockkey_state(kbd, state, ALK);
				break;
			/* NON-LOCKING KEYS */
			case SPSC: case RBT:  case SUSP: case STBY:
			case DBG:  case NEXT: case PREV: case PNC:
			case HALT: case PDWN:
				*accents = 0;
				break;
			case BTAB:
				*accents = 0;
				action |= BKEY;
				break;
			case LSHA:
				state |= SHIFTAON;
				action = LSH;
				/* FALL THROUGH */
			case LSH:
				state |= SHIFTS1;
				break;
			case RSHA:
				state |= SHIFTAON;
				action = RSH;
				/* FALL THROUGH */
			case RSH:
				state |= SHIFTS2;
				break;
			case LCTRA:
				state |= SHIFTAON;
				action = LCTR;
				/* FALL THROUGH */
			case LCTR:
				state |= CTLS1;
				break;
			case RCTRA:
				state |= SHIFTAON;
				action = RCTR;
				/* FALL THROUGH */
			case RCTR:
				state |= CTLS2;
				break;
			case LALTA:
				state |= SHIFTAON;
				action = LALT;
				/* FALL THROUGH */
			case LALT:
				state |= ALTS1;
				break;
			case RALTA:
				state |= SHIFTAON;
				action = RALT;
				/* FALL THROUGH */
			case RALT:
				state |= ALTS2;
				break;
			case ASH:
				state |= AGRS1;
				break;
			case META:
				state |= METAS1;
				break;
			case NOP:
				*shiftstate = state;
				lwkt_reltoken(&kbd_token);
				return NOKEY;
			default:
				/* is this an accent (dead) key? */
				*shiftstate = state;
				if (action >= F_ACC && action <= L_ACC) {
					action = save_accent_key(kbd, action,
								 accents);
					switch (action) {
					case NOKEY:
					case ERRKEY:
						lwkt_reltoken(&kbd_token);
						return action;
					default:
						if (state & METAS) {
							lwkt_reltoken(&kbd_token);
							return (action | MKEY);
						} else { 
							lwkt_reltoken(&kbd_token);
							return action;
						}
					}
					/* NOT REACHED */
				}
				/* other special keys */
				if (*accents > 0) {
					*accents = 0;
					lwkt_reltoken(&kbd_token);
					return ERRKEY;
				}
				if (action >= F_FN && action <= L_FN)
					action |= FKEY;
				/* XXX: return fkey string for the FKEY? */
				lwkt_reltoken(&kbd_token);
				return (SPCLKEY | action);
			}
			*shiftstate = state;
			lwkt_reltoken(&kbd_token);
			return (SPCLKEY | action);
		} else {
			/* regular keys */
			kbd->kb_lastact[keycode] = NOP;
			*shiftstate = state;
			if (*accents > 0) {
				/* make an accented char */
				action = make_accent_char(kbd, action, accents);
				if (action == ERRKEY) {
					lwkt_reltoken(&kbd_token);
					return action;
				}
			}
			if (state & METAS)
				action |= MKEY;
			lwkt_reltoken(&kbd_token);
			return action;
		}
	}
	/* NOT REACHED */
	lwkt_reltoken(&kbd_token);
}

#ifdef EVDEV_SUPPORT
void
kbd_ev_event(keyboard_t *kbd, uint16_t type, uint16_t code, int32_t value)
{
	int delay[2], led = 0, leds, oleds;

	if (type == EV_LED) {
		leds = oleds = KBD_LED_VAL(kbd);
		switch (code) {
		case LED_CAPSL:
			led = CLKED;
			break;
		case LED_NUML:
			led = NLKED;
			break;
		case LED_SCROLLL:
			led = SLKED;
			break;
		}

		if (value)
			leds |= led;
		else
			leds &= ~led;

		if (leds != oleds)
			kbd_ioctl(kbd, KDSETLED, (caddr_t)&leds);

	} else if (type == EV_REP && code == REP_DELAY) {
		delay[0] = value;
		delay[1] = kbd->kb_delay2;
		kbd_ioctl(kbd, KDSETREPEAT, (caddr_t)delay);
	} else if (type == EV_REP && code == REP_PERIOD) {
		delay[0] = kbd->kb_delay1;
		delay[1] = value;
		kbd_ioctl(kbd, KDSETREPEAT, (caddr_t)delay);
	}
}
#endif

/*
 * (MPSAFE)
 *
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
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
 */

#include "opt_kbd.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <sys/event.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <sys/thread.h>
#include <sys/thread2.h>

#include <machine/console.h>

#include "kbdreg.h"

int
sw_probe(keyboard_switch_t *sw, int unit, void *arg, int flags)
{
	int error;

	error = (*sw->probe)(unit, arg, flags);
	return (error);
}

int
sw_init(keyboard_switch_t *sw, int unit, keyboard_t **kbdpp,
	void *arg, int flags)
{
	int error;

	error = (*sw->init)(unit, kbdpp, arg, flags);
	return (error);
}

int
kbd_term(keyboard_t *kbd)
{
	int error;

	KBD_ALWAYS_LOCK(kbd);
	error = (*kbdsw[kbd->kb_index]->term)(kbd);
	if (error)
		KBD_ALWAYS_UNLOCK(kbd);
	/* kbd structure is stale if error is 0 */
	return (error);
}

int
kbd_intr(keyboard_t *kbd, void *arg)
{
	int error;
	KBD_LOCK_DECLARE;

	KBD_LOCK(kbd);
	error = (*kbdsw[kbd->kb_index]->intr)(kbd, arg);
	KBD_UNLOCK(kbd);

	return (error);
}

int
kbd_test_if(keyboard_t *kbd)
{
	int error;
	KBD_LOCK_DECLARE;

	KBD_LOCK(kbd);
	error = (*kbdsw[kbd->kb_index]->test_if)(kbd);
	KBD_UNLOCK(kbd);

	return (error);
}

int
kbd_enable(keyboard_t *kbd)
{
	int error;
	KBD_LOCK_DECLARE;

	KBD_LOCK(kbd);
	error = (*kbdsw[kbd->kb_index]->enable)(kbd);
	KBD_UNLOCK(kbd);

	return (error);
}

int
kbd_disable(keyboard_t *kbd)
{
	int error;
	KBD_LOCK_DECLARE;

	KBD_LOCK(kbd);
	error = (*kbdsw[kbd->kb_index]->disable)(kbd);
	KBD_UNLOCK(kbd);

	return (error);
}

int
kbd_read(keyboard_t *kbd, int wait)
{
	int error;
	KBD_LOCK_DECLARE;

	KBD_LOCK(kbd);
	error = (*kbdsw[kbd->kb_index]->read)(kbd, wait);
	KBD_UNLOCK(kbd);

	return (error);
}

int
kbd_check(keyboard_t *kbd)
{
	int error;
	KBD_LOCK_DECLARE;

	KBD_LOCK(kbd);
	error = (*kbdsw[kbd->kb_index]->check)(kbd);
	KBD_UNLOCK(kbd);

	return (error);
}

u_int
kbd_read_char(keyboard_t *kbd, int wait)
{
	int error;
	KBD_LOCK_DECLARE;

	KBD_LOCK(kbd);
	error = (*kbdsw[kbd->kb_index]->read_char)(kbd, wait);
	KBD_UNLOCK(kbd);

	return (error);
}

int
kbd_check_char(keyboard_t *kbd)
{
	int error;
	KBD_LOCK_DECLARE;

	KBD_LOCK(kbd);
	error = (*kbdsw[kbd->kb_index]->check_char)(kbd);
	KBD_UNLOCK(kbd);

	return (error);
}

int
kbd_ioctl(keyboard_t *kbd, u_long cmd, caddr_t data)
{
	int error;
	KBD_LOCK_DECLARE;

	if (kbd) {
		KBD_LOCK(kbd);
		error = (*kbdsw[kbd->kb_index]->ioctl)(kbd, cmd, data);
		KBD_UNLOCK(kbd);
	} else {
		error = ENODEV;
	}
	return (error);
}

int
kbd_lock(keyboard_t *kbd, int xlock)
{
	int error;
	KBD_LOCK_DECLARE;

	KBD_LOCK(kbd);
	error = (*kbdsw[kbd->kb_index]->lock)(kbd, xlock);
	KBD_UNLOCK(kbd);

	return (error);
}

void
kbd_clear_state(keyboard_t *kbd)
{
	KBD_LOCK_DECLARE;

	KBD_LOCK(kbd);
	(*kbdsw[kbd->kb_index]->clear_state)(kbd);
	KBD_UNLOCK(kbd);
}

int
kbd_get_state(keyboard_t *kbd, void *buf, size_t len)
{
	int error;
	KBD_LOCK_DECLARE;

	KBD_LOCK(kbd);
	error = (*kbdsw[kbd->kb_index]->get_state)(kbd, buf, len);
	KBD_UNLOCK(kbd);

	return (error);
}

int
kbd_set_state(keyboard_t *kbd, void *buf, size_t len)
{
	int error;
	KBD_LOCK_DECLARE;

	KBD_LOCK(kbd);
	error = (*kbdsw[kbd->kb_index]->set_state)(kbd, buf, len);
	KBD_UNLOCK(kbd);

	return (error);
}

u_char *
kbd_get_fkeystr(keyboard_t *kbd, int fkey, size_t *len)
{
	KBD_LOCK_DECLARE;
	u_char *retstr;

	KBD_LOCK(kbd);
	retstr = (*kbdsw[kbd->kb_index]->get_fkeystr)(kbd, fkey, len);
	KBD_UNLOCK(kbd);

	return (retstr);
}

/*
 * Polling mode set by debugger, we cannot lock!
 */
int
kbd_poll(keyboard_t *kbd, int on)
{
	int error;

	if (!on)
		KBD_UNPOLL(kbd);
	error = (*kbdsw[kbd->kb_index]->poll)(kbd, on);
	if (on)
		KBD_POLL(kbd);

	return (error);
}

void
kbd_diag(keyboard_t *kbd, int level)
{
	KBD_LOCK_DECLARE;

	KBD_LOCK(kbd);
	(*kbdsw[kbd->kb_index]->diag)(kbd, level);
	KBD_UNLOCK(kbd);
}

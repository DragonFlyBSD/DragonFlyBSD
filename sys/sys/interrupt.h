/*
 * Copyright (c) 1997, Stefan Esser <se@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/sys/interrupt.h,v 1.9.2.1 2001/10/14 20:05:50 luigi Exp $
 * $DragonFly: src/sys/sys/interrupt.h,v 1.6 2003/07/08 06:27:28 dillon Exp $
 */

#ifndef _SYS_INTERRUPT_H_
#define _SYS_INTERRUPT_H_

#define MAX_INTS	32

typedef void inthand2_t __P((void *_cookie));
typedef void ointhand2_t __P((int _device_id));

struct thread;

struct thread *register_swi(int intr, inthand2_t *handler, void *arg,
			    const char *name);
struct thread *register_int(int intr, inthand2_t *handler, void *arg,
			    const char *name);
void register_randintr(int intr);

void swi_setpriority(int intr, int pri);
void unregister_swi(int intr, inthand2_t *handler);
void unregister_int(int intr, inthand2_t *handler);
void unregister_randintr(int intr);
void ithread_done(int intr);	/* procedure defined in MD */
void sched_ithd(int intr);	/* procedure called from MD */

/* Counts and names for statistics (defined in MD code). */
extern u_long	eintrcnt[];	/* end of intrcnt[] */
extern char	eintrnames[];	/* end of intrnames[] */
extern u_long	intrcnt[];	/* counts for for each device and stray */
extern char	intrnames[];	/* string table containing device names */
#endif

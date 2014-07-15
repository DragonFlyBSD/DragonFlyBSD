/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
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

#include <sys/globaldata.h>	/* curthread in mtx_assert() */
#include <sys/lock.h>

#ifndef _VA_LIST_DECLARED
#define _VA_LIST_DECLARED
typedef __va_list	va_list;
#endif
#define va_start(ap,last)	__va_start(ap,last)
#define va_end(ap)	__va_end(ap)

#define IFNET_RLOCK()	crit_enter()
#define IFNET_RUNLOCK()	crit_exit()

#define IFQ_LOCK(ifq)	ALTQ_LOCK((ifq))
#define IFQ_UNLOCK(ifq)	ALTQ_UNLOCK((ifq))

#define CTR1(ktr_line, ...)
#define CTR2(ktr_line, ...)
#define CTR3(ktr_line, ...)
#define CTR4(ktr_line, ...)
#define CTR5(ktr_line, ...)
#define CTR6(ktr_line, ...)
#define cpu_spinwait()	cpu_pause()

#define SI_SUB_NETGRAPH	SI_SUB_DRIVERS

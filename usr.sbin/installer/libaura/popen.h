/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Chris Pressey <cpressey@catseye.mine.nu>.
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

/*
 * popen.h
 * $Id: popen.h,v 1.5 2005/02/06 06:57:30 cpressey Exp $
 * A modified version of the standard popen()/pclose() functions
 * which adds a third function, pgetpid(), which allows the program
 * which used popen() to obtain the pid of the process on the other
 * end of the pipe.
 */

#ifndef __AURA_POPEN_H_
#define __AURA_POPEN_H_

#include <sys/types.h>

#include <stdio.h>

#define	AURA_PGETS_TIMEOUT	1
#define	AURA_PGETS_SELECT_ERR	2
#define	AURA_PGETS_EOF		3
#define	AURA_PGETS_FGETS_ERR	4

FILE			*aura_popen(const char *, const char *, ...)
			     __printflike(1, 3);
int			 aura_pclose(FILE *);
pid_t			 aura_pgetpid(FILE *);
int			 aura_pgets(FILE *, char *, size_t, long, int *);

#endif /* !__AURA_POPEN_H_ */

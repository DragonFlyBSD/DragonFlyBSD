/*
 * Copyright (c) 2004 Joerg Sonnenberger <joerg@bec.de>
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
 * $DragonFly: src/include/libiberty.h,v 1.3 2004/10/23 12:15:21 joerg Exp $
 */

#ifndef _LIBIBERTY_H_
#define _LIBIBERTY_H_

#include <sys/cdefs.h>
#include <sys/stdint.h>

#ifndef _SIZE_T_DECLARED
#define _SIZE_T_DECLARED
typedef __size_t	size_t;
#endif

__BEGIN_DECLS
char **		buildargv(const char *);
void		freeargv(char **);
char **		dupargv(char * const *);

void		hex_init(void);
int		hex_p(int);
unsigned int	hex_value(int);

char *		concat(const char *, ...);
char *		reconcat(char *, ...);
const char *	lbasename(const char *);
char *		lrealpath(const char *);
int		xatexit(void (*)(void));
void *		xcalloc(size_t, size_t);
void		xexit(int);
void *		xmalloc(size_t);
void *		xmemdup(const void *, size_t, size_t);
void *		xrealloc(void *, size_t);
char *		xstrdup(const char *);
char *		xstrerror(int error);
__END_DECLS

#endif

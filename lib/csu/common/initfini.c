/*-
 * Copyright 2012 Konstantin Belousov <kib@FreeBSD.org>
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
 */

extern int main(int, char **, char **);

extern void (*__preinit_array_start []) (int, char **, char **) __dso_hidden;
extern void (*__preinit_array_end   []) (int, char **, char **) __dso_hidden;
extern void (*__init_array_start    []) (int, char **, char **) __dso_hidden;
extern void (*__init_array_end      []) (int, char **, char **) __dso_hidden;
extern void (*__fini_array_start    []) (void) __dso_hidden;
extern void (*__fini_array_end      []) (void) __dso_hidden;
extern void _fini(void);
extern void _init(void);

extern int _DYNAMIC;
#pragma weak _DYNAMIC

char **environ;
const char *__progname = "";

static inline void
handle_progname(const char *v)
{
	const char *s;

	__progname = v;
	for (s = __progname; *s != '\0'; s++) {
		if (*s == '/')
			__progname = s + 1;
	}
}


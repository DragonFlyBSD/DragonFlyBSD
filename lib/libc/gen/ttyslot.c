/*
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 * @(#)ttyslot.c	8.1 (Berkeley) 6/4/93
 * $DragonFly: src/lib/libc/gen/ttyslot.c,v 1.4 2005/11/13 00:07:42 swildner Exp $
 */

#include <paths.h>
#include <ttyent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int
ttyslot(void)
{
	struct ttyent *ttyp;
	int slot;
	int cnt;
	char *name;
	size_t len = sizeof(_PATH_DEV) - 1;

	setttyent();
	for (cnt = 0; cnt < 3; ++cnt)
		if ( (name = ttyname(cnt)) ) {
			if (strncmp(name, _PATH_DEV, len) != 0)
				break;

			name += len;

			for (slot = 1; (ttyp = getttyent()); ++slot)
				if (!strcmp(ttyp->ty_name, name)) {
					endttyent();
					return(slot);
				}
			break;
		}
	endttyent();
	return(0);
}

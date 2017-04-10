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
 *
 * $DragonFly: src/sbin/hammer/cycle.c,v 1.5 2008/07/07 00:27:22 dillon Exp $
 */

#include "hammer.h"

void
hammer_get_cycle(hammer_base_elm_t base, hammer_tid_t *extra)
{
	struct stat st;
	int fd;

	if (CyclePath && (fd = open(CyclePath, O_RDONLY)) >= 0) {
		if (fstat(fd, &st) < 0) {
			fprintf(stderr, "cycle-file %s: cannot stat\n",
				CyclePath);
			close(fd);
			return;
		}
		if (st.st_size < (off_t)sizeof(*base)) {
			fprintf(stderr, "cycle-file %s: clearing old version\n",
				CyclePath);
			close(fd);
			remove(CyclePath);
			return;
		}
		if (read(fd, base, sizeof(*base)) != sizeof(*base)) {
			fprintf(stderr, "cycle-file %s: read failed %s\n",
				CyclePath, strerror(errno));
			return;
		}
		if (extra) {
			if (read(fd, extra, sizeof(*extra)) != sizeof(*extra)) {
				fprintf(stderr, "cycle-file %s: Warning, malformed\n",
					CyclePath);
			}
		}
		close(fd);
	}
	/* ok if the file does not exist */
}

void
hammer_set_cycle(hammer_base_elm_t base, hammer_tid_t extra)
{
	int fd;

	if ((fd = open(CyclePath, O_RDWR|O_CREAT|O_TRUNC, 0666)) >= 0) {
		write(fd, base, sizeof(*base));
		write(fd, &extra, sizeof(extra));
		close(fd);
	} else {
		fprintf(stderr, "Warning: Unable to write to %s: %s\n",
			CyclePath, strerror(errno));
	}
}

void
hammer_reset_cycle(void)
{
	remove(CyclePath);
}


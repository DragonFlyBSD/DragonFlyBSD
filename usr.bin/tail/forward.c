/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Edward Sze-Tyan Wang.
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
 * @(#)forward.c	8.1 (Berkeley) 6/6/93
 * $FreeBSD: src/usr.bin/tail/forward.c,v 1.11.6.7 2003/01/07 05:26:22 tjr Exp $
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/event.h>

#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include "extern.h"

static void rlines(FILE *, off_t, struct stat *);

/* defines for inner loop actions */
#define USE_SLEEP	0
#define USE_KQUEUE	1
#define ADD_EVENTS	2

struct kevent *ev;
int action = USE_SLEEP;
int kq;

/*
 * forward -- display the file, from an offset, forward.
 *
 * There are eight separate cases for this -- regular and non-regular
 * files, by bytes or lines and from the beginning or end of the file.
 *
 * FBYTES	byte offset from the beginning of the file
 *	REG	seek
 *	NOREG	read, counting bytes
 *
 * FLINES	line offset from the beginning of the file
 *	REG	read, counting lines
 *	NOREG	read, counting lines
 *
 * RBYTES	byte offset from the end of the file
 *	REG	seek
 *	NOREG	cyclically read characters into a wrap-around buffer
 *
 * RLINES
 *	REG	mmap the file and step back until reach the correct offset.
 *	NOREG	cyclically read lines into a wrap-around array of buffers
 */
void
forward(FILE *fp, enum STYLE style, off_t off, struct stat *sbp)
{
	int ch;

	switch(style) {
	case FBYTES:
		if (off == 0)
			break;
		if (S_ISREG(sbp->st_mode)) {
			if (sbp->st_size < off)
				off = sbp->st_size;
			if (fseeko(fp, off, SEEK_SET) == -1) {
				ierr();
				return;
			}
		} else while (off--)
			if ((ch = getc(fp)) == EOF) {
				if (ferror(fp)) {
					ierr();
					return;
				}
				break;
			}
		break;
	case FLINES:
		if (off == 0)
			break;
		for (;;) {
			if ((ch = getc(fp)) == EOF) {
				if (ferror(fp)) {
					ierr();
					return;
				}
				break;
			}
			if (ch == '\n' && !--off)
				break;
		}
		break;
	case RBYTES:
		if (S_ISREG(sbp->st_mode)) {
			if (sbp->st_size >= off &&
			    fseeko(fp, -off, SEEK_END) == -1) {
				ierr();
				return;
			}
		} else if (off == 0) {
			while (getc(fp) != EOF);
			if (ferror(fp)) {
				ierr();
				return;
			}
		} else
			if (display_bytes(fp, off))
				return;
		break;
	case RLINES:
		if (S_ISREG(sbp->st_mode))
			if (!off) {
				if (fseeko(fp, (off_t)0, SEEK_END) == -1) {
					ierr();
					return;
				}
			} else
				rlines(fp, off, sbp);
		else if (off == 0) {
			while (getc(fp) != EOF);
			if (ferror(fp)) {
				ierr();
				return;
			}
		} else
			if (display_lines(fp, off))
				return;
		break;
	case REVERSE:
		errx(1, "internal error: forward style cannot be REVERSE");
		/* NOTREACHED */
	}

	while ((ch = getc(fp)) != EOF) {
		if (putchar(ch) == EOF)
			oerr();
	}
	if (ferror(fp)) {
		ierr();
		return;
	}
	fflush(stdout);
}

/*
 * rlines -- display the last offset lines of the file.
 */
static void
rlines(FILE *fp, off_t off, struct stat *sbp)
{
	struct mapinfo map;
	off_t curoff, size;
	int i;

	if (!(size = sbp->st_size))
		return;
	map.start = NULL;
	map.fd = fileno(fp);
	map.mapoff = map.maxoff = size;

	/*
	 * Last char is special, ignore whether newline or not. Note that
	 * size == 0 is dealt with above, and size == 1 sets curoff to -1.
	 */
	curoff = size - 2;
	while (curoff >= 0) {
		if (curoff < map.mapoff && maparound(&map, curoff) != 0) {
			ierr();
			return;
		}
		for (i = curoff - map.mapoff; i >= 0; i--)
			if (map.start[i] == '\n' && --off == 0)
				break;
		/* `i' is either the map offset of a '\n', or -1. */
		curoff = map.mapoff + i;
		if (i >= 0)
			break;
	}
	curoff++;
	if (mapprint(&map, curoff, size - curoff) != 0) {
		ierr();
		exit(1);
	}

	/* Set the file pointer to reflect the length displayed. */
	if (fseeko(fp, sbp->st_size, SEEK_SET) == -1) {
		ierr();
		return;
	}
	if (map.start != NULL && munmap(map.start, map.maplen)) {
		ierr();
		return;
	}
}

/*
 * follow -- display the file, from an offset, forward.
 */

static void
show(file_info_t *file, int at_index)
{
	int ch, first;

	first = 1;
	while ((ch = getc(file->fp)) != EOF) {
		if (first && no_files > 1) {
			showfilename(at_index, file->file_name);
			first = 0;
		}
		if (putchar(ch) == EOF)
			oerr();
	}
	fflush(stdout);
	if (ferror(file->fp)) {
		file->fp = NULL;
		ierr();
	} else {
		clearerr(file->fp);
	}
}

void
showfilename(int at_index, const char *filename)
{
	static int last_index = -1;
	static int continuing = 0;

	if (last_index == at_index)
		return;
	if (!qflag) {
		if (continuing)
			printf("\n");
		printf("==> %s <==\n", filename);
	}
	continuing = 1;
	last_index = at_index;
}

static void
set_events(file_info_t *files)
{
	int i, n;
	file_info_t *file;
	struct timespec ts;

	ts.tv_sec = 0;
	ts.tv_nsec = 0;

	n = 0;
	action = USE_KQUEUE;
	for (i = 0, file = files; i < no_files; i++, file++) {
		if (file->fp == NULL)
			continue;
		if (Fflag && fileno(file->fp) != STDIN_FILENO) {
			EV_SET(&ev[n], fileno(file->fp), EVFILT_VNODE,
			       EV_ADD | EV_ENABLE | EV_CLEAR,
			       NOTE_DELETE | NOTE_RENAME, 0, 0);
			n++;
		}
		EV_SET(&ev[n], fileno(file->fp), EVFILT_READ,
		       EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, 0);
		n++;
	}

	if (kevent(kq, ev, n, NULL, 0, &ts) < 0)
		action = USE_SLEEP;
}

void
follow(file_info_t *files, enum STYLE style, off_t off)
{
	int active, i, n;
	file_info_t *file;
	struct stat sb2;
	struct timespec ts;

	/* Position each of the files */
	file = files;
	active = 0;
	n = 0;
	for (i = 0; i < no_files; i++, file++) {
		if (file->fp) {
			active = 1;
			n++;
			if (no_files > 1)
				showfilename(i, file->file_name);
			forward(file->fp, style, off, &file->st);
			if (Fflag && fileno(file->fp) != STDIN_FILENO)
				n++;
		}
	}

	if (!active)
		return;

	kq = kqueue();
	if (kq == -1)
		err(1, "kqueue");
	ev = malloc(n * sizeof(struct kevent));
	if (ev == NULL)
		err(1, "Couldn't allocate memory for kevents.");
	set_events(files);

	for (;;) {
		for (i = 0, file = files; i < no_files; i++, file++) {
			if (file->fp == NULL)
				continue;
			if (Fflag && fileno(file->fp) != STDIN_FILENO) {
				if (stat(file->file_name, &sb2) == -1) {
					/*
					 * file was rotated, skip it until it
					 * reappears.
					 */
					continue;
				}
				if (sb2.st_ino != file->st.st_ino ||
				    sb2.st_dev != file->st.st_dev ||
				    sb2.st_nlink == 0) {
					file->fp = freopen(file->file_name, "r",
							   file->fp);
					if (file->fp == NULL) {
						ierr();
						continue;
					} else {
						memcpy(&file->st, &sb2,
						       sizeof(struct stat));
						set_events(files);
					}
				}
			}
			show(file, i);
		}

		switch (action) {
		case USE_KQUEUE:
			ts.tv_sec = 1;
			ts.tv_nsec = 0;
			/*
			 * In the -F case, we set a timeout to ensure that
			 * we re-stat the file at least once every second.
			 */
			n = kevent(kq, NULL, 0, ev, 1, Fflag ? &ts : NULL);
			if (n == -1)
				err(1, "kevent");
			if (n == 0) {
				/* timeout */
				break;
			} else if (ev->filter == EVFILT_READ && ev->data < 0) {
				/* file shrank, reposition to end */
				if (lseek(ev->ident, 0, SEEK_END) == -1) {
					ierr();
					continue;
				}
			}
			break;

		case USE_SLEEP:
			usleep(250000);
			break;
		}
	}
}

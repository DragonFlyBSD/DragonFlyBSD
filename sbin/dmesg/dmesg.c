/*-
 * Copyright (c) 1991, 1993
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
 * @(#) Copyright (c) 1991, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)dmesg.c	8.1 (Berkeley) 6/5/93
 * $FreeBSD: src/sbin/dmesg/dmesg.c,v 1.11.2.3 2001/08/08 22:32:15 obrien Exp $
 */

#include <sys/types.h>
#include <sys/msgbuf.h>
#include <sys/sysctl.h>

#include <err.h>
#include <fcntl.h>
#include <kvm.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <vis.h>
#include <sys/syslog.h>

struct nlist nl[] = {
#define	X_MSGBUF	0
	{ "_msgbufp",	0, 0, 0, 0 },
	{ NULL,		0, 0, 0, 0 },
};

static void dumpbuf(char *bp, size_t bufpos, size_t buflen,
		    int *newl, int *skip, int *pri);
void usage(void);

#define	KREAD(addr, var) \
	kvm_read(kd, addr, &var, sizeof(var)) != sizeof(var)

#define INCRBUFSIZE	65536

int all_opt;

int
main(int argc, char **argv)
{
	int newl, skip;
	struct msgbuf *bufp, cur;
	char *bp, *memf, *nlistf;
	kvm_t *kd;
	int ch;
	int clear = 0;
	int pri = 0;
	int tailmode = 0;
	int kno;
	unsigned int rindex;
	size_t buflen, bufpos;

	setlocale(LC_CTYPE, "");
	memf = nlistf = NULL;
	while ((ch = getopt(argc, argv, "acfM:N:n:")) != -1) {
		switch(ch) {
		case 'a':
			all_opt++;
			break;
		case 'c':
			clear = 1;
			break;
		case 'f':
			++tailmode;
			break;
		case 'M':
			memf = optarg;
			break;
		case 'N':
			nlistf = optarg;
			break;
		case 'n':
			kno = strtol(optarg, NULL, 0);
			asprintf(&memf, "/var/crash/vmcore.%d", kno);
			asprintf(&nlistf, "/var/crash/kern.%d", kno);
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	newl = 0;
	skip = 0;
	pri = 0;

	if (memf == NULL && nlistf == NULL && tailmode == 0) {
		/* Running kernel. Use sysctl. */
		if (sysctlbyname("kern.msgbuf", NULL, &buflen, NULL, 0) == -1)
			err(1, "sysctl kern.msgbuf");
		buflen += 4096;		/* add some slop */
		if ((bp = malloc(buflen)) == NULL)
			errx(1, "malloc failed");
		if (sysctlbyname("kern.msgbuf", bp, &buflen, NULL, 0) == -1)
			err(1, "sysctl kern.msgbuf");
		/* We get a dewrapped buffer using sysctl. */
		bufpos = 0;
		dumpbuf(bp, bufpos, buflen, &newl, &skip, &pri);
	} else {
		/* Read in kernel message buffer, do sanity checks. */
		kd = kvm_open(nlistf, memf, NULL, O_RDONLY, "dmesg");
		if (kd == NULL)
			exit (1);
		if (kvm_nlist(kd, nl) == -1)
			errx(1, "kvm_nlist: %s", kvm_geterr(kd));
		if (nl[X_MSGBUF].n_type == 0)
			errx(1, "%s: msgbufp not found",
			    nlistf ? nlistf : "namelist");
		bp = malloc(INCRBUFSIZE);
		if (!bp)
			errx(1, "malloc failed");
		if (KREAD(nl[X_MSGBUF].n_value, bufp))
			errx(1, "kvm_read: %s", kvm_geterr(kd));
		if (KREAD((long)bufp, cur))
			errx(1, "kvm_read: %s", kvm_geterr(kd));
		if (cur.msg_magic != MSG_MAGIC)
			errx(1, "kernel message buffer has different magic "
			    "number");

		/*
		 * Start point.  Use rindex == bufx as our end-of-buffer
		 * indication but for the initial rindex from the kernel
		 * this means the buffer has cycled and is 100% full.
		 */
		rindex = cur.msg_bufr;
		if (rindex == cur.msg_bufx) {
			if (++rindex >= cur.msg_size)
				rindex = 0;
		}

		for (;;) {
			if (cur.msg_bufx >= rindex)
				buflen = cur.msg_bufx - rindex;
			else
				buflen = cur.msg_size - rindex;
			if (buflen > INCRBUFSIZE)
				buflen = INCRBUFSIZE;
			if (kvm_read(kd, (long)cur.msg_ptr + rindex,
				     bp, buflen) != (ssize_t)buflen) {
				errx(1, "kvm_read: %s", kvm_geterr(kd));
			}
			if (buflen)
				dumpbuf(bp, 0, buflen, &newl, &skip, &pri);
			rindex += buflen;
			if (rindex >= cur.msg_size)
				rindex = 0;
			if (rindex == cur.msg_bufx) {
				if (tailmode == 0)
					break;
				fflush(stdout);
				if (tailmode == 1)
					sleep(1);
			}
			if (KREAD((long)bufp, cur))
				errx(1, "kvm_read: %s", kvm_geterr(kd));
		}
		kvm_close(kd);
	}
	if (!newl)
		putchar('\n');
	if (clear) {
		if (sysctlbyname("kern.msgbuf_clear", NULL, NULL,
				 &clear, sizeof(int)) != 0) {
			err(1, "sysctl kern.msgbuf_clear");
		}
	}
	return(0);
}

static
void
dumpbuf(char *bp, size_t bufpos, size_t buflen,
	int *newl, int *skip, int *pri)
{
	int ch;
	char *p, *ep;
	char buf[5];

	/*
	 * The message buffer is circular.  If the buffer has wrapped, the
	 * write pointer points to the oldest data.  Otherwise, the write
	 * pointer points to \0's following the data.  Read the entire
	 * buffer starting at the write pointer and ignore nulls so that
	 * we effectively start at the oldest data.
	 */
	p = bp + bufpos;
	ep = (bufpos == 0 ? bp + buflen : p);
	do {
		if (p == bp + buflen)
			p = bp;
		ch = *p;
		/* Skip "\n<.*>" syslog sequences. */
		if (*skip) {
			if (ch == '\n') {
				*skip = 0;
				*newl = 1;
			} if (ch == '>') {
				if (LOG_FAC(*pri) == LOG_KERN || all_opt)
					*newl = *skip = 0;
			} else if (ch >= '0' && ch <= '9') {
				*pri *= 10;
				*pri += ch - '0';
			}
			continue;
		}
		if (*newl && ch == '<') {
			*pri = 0;
			*skip = 1;
			continue;
		}
		if (ch == '\0')
			continue;
		*newl = ch == '\n';
		vis(buf, ch, 0, 0);
		if (buf[1] == 0)
			putchar(buf[0]);
		else
			printf("%s", buf);
	} while (++p != ep);
}

void
usage(void)
{
	fprintf(stderr, "usage: dmesg [-ac] [-M core] [-N system]\n");
	exit(1);
}

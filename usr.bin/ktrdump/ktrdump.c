/*-
 * Copyright (c) 2002 Jake Burkholder
 * Copyright (c) 2004 Robert Watson
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/usr.bin/ktrdump/ktrdump.c,v 1.10 2005/05/21 09:55:06 ru Exp $
 * $DragonFly: src/usr.bin/ktrdump/ktrdump.c,v 1.2 2005/06/21 00:47:07 dillon Exp $
 */

#include <sys/cdefs.h>

#include <sys/types.h>
#include <sys/ktr.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <nlist.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	SBUFLEN	128

extern char *optarg;
extern int optind;

static void usage(void);
static void print_header(FILE *fo, int row);
static void print_entry(FILE *fo, kvm_t *kd, int n, int i, struct ktr_entry *entry);
static struct ktr_info *kvm_ktrinfo(kvm_t *kd, void *kptr);
static const char *kvm_string(kvm_t *kd, const char *kptr);
static const char *trunc_path(const char *str, int maxlen);

static struct nlist nl[] = {
	{ "_ktr_version" },
	{ "_ktr_entries" },
	{ "_ktr_idx" },
	{ "_ktr_buf" },
	{ "_ncpus" },
	{ NULL }
};

static int cflag;
static int fflag;
static int iflag;
static int mflag;
static int nflag;
static int qflag;
static int rflag;
static int tflag;
static int xflag;
static int pflag;
static int64_t last_timestamp;

static char corefile[PATH_MAX];
static char execfile[PATH_MAX];

static char desc[SBUFLEN];
static char errbuf[_POSIX2_LINE_MAX];
static char fbuf[PATH_MAX];
static char obuf[PATH_MAX];

/*
 * Reads the ktr trace buffer from kernel memory and prints the trace entries.
 */
int
main(int ac, char **av)
{
	struct ktr_entry **ktr_buf;
	uintmax_t tlast, tnow;
	struct stat sb;
	kvm_t *kd;
	FILE *fo;
	char *p;
	int version;
	int entries;
	int *ktr_idx;
	int ncpus;
	int in;
	int c;
	int i;
	int n;

	/*
	 * Parse commandline arguments.
	 */
	fo = stdout;
	while ((c = getopt(ac, av, "acfiqrtxpN:M:o:")) != -1)
		switch (c) {
		case 'a':
			cflag = 1;
			iflag = 1;
			tflag = 1;
			xflag = 1;
			fflag = 1;
			pflag = 1;
			break;
		case 'c':
			cflag = 1;
			break;
		case 'N':
			if (strlcpy(execfile, optarg, sizeof(execfile))
			    >= sizeof(execfile))
				errx(1, "%s: File name too long", optarg);
			nflag = 1;
			break;
		case 'f':
			fflag = 1;
			break;
		case 'i':
			iflag = 1;
			break;
		case 'M':
			if (strlcpy(corefile, optarg, sizeof(corefile))
			    >= sizeof(corefile))
				errx(1, "%s: File name too long", optarg);
			mflag = 1;
			break;
		case 'o':
			if ((fo = fopen(optarg, "w")) == NULL)
				err(1, "%s", optarg);
			break;
		case 'p':
			pflag++;
			break;
		case 'q':
			qflag++;
			break;
		case 'r':
			rflag = 1;
			break;
		case 't':
			tflag = 1;
			break;
		case 'x':
			xflag = 1;
			break;
		case '?':
		default:
			usage();
		}
	ac -= optind;
	av += optind;
	if (ac != 0)
		usage();

	/*
	 * Open our execfile and corefile, resolve needed symbols and read in
	 * the trace buffer.
	 */
	if ((kd = kvm_openfiles(nflag ? execfile : NULL,
	    mflag ? corefile : NULL, NULL, O_RDONLY, errbuf)) == NULL)
		errx(1, "%s", errbuf);
	if (kvm_nlist(kd, nl) != 0)
		errx(1, "%s", kvm_geterr(kd));
	if (kvm_read(kd, nl[0].n_value, &version, sizeof(version)) == -1)
		errx(1, "%s", kvm_geterr(kd));
	if (kvm_read(kd, nl[4].n_value, &ncpus, sizeof(ncpus)) == -1)
		errx(1, "%s", kvm_geterr(kd));

	if (version != KTR_VERSION)
		errx(1, "ktr version mismatch");
	if (kvm_read(kd, nl[1].n_value, &entries, sizeof(entries)) == -1)
		errx(1, "%s", kvm_geterr(kd));
	ktr_buf = malloc(sizeof(*ktr_buf) * ncpus);
	ktr_idx = malloc(sizeof(*ktr_idx) * ncpus);

	if (kvm_read(kd, nl[2].n_value, ktr_idx, sizeof(*ktr_idx) * ncpus) == -1)
		errx(1, "%s", kvm_geterr(kd));
	if (kvm_read(kd, nl[3].n_value, ktr_buf, sizeof(*ktr_buf) * ncpus) == -1)
		errx(1, "%s", kvm_geterr(kd));
	for (n = 0; n < ncpus; ++n) {
		void *kptr = ktr_buf[n];
		ktr_buf[n] = malloc(sizeof(**ktr_buf) * entries);
		if (kvm_read(kd, (uintptr_t)kptr, ktr_buf[n], sizeof(**ktr_buf) * entries) == -1)
		errx(1, "%s", kvm_geterr(kd));
	}

	/*
	 * Now tear through the trace buffer.
	 */
	for (n = 0; n < ncpus; ++n) {
		last_timestamp = 0;
		for (i = 0; i < entries; ++i) {
			print_header(fo, i);
			print_entry(fo, kd, n, i, &ktr_buf[n][i]);
		}
	}
	return (0);
}

static void
print_header(FILE *fo, int row)
{
	if (qflag == 0 && row % 20 == 0) {
		fprintf(fo, "%-6s ", "index");
		if (cflag)
			fprintf(fo, "%-3s ", "cpu");
		if (tflag || rflag)
			fprintf(fo, "%-16s ", "timestamp");
		if (xflag)
			fprintf(fo, "%-10s %-10s", "caller1", "caller2");
		if (iflag)
			fprintf(fo, "%-20s ", "ID");
		if (fflag)
			fprintf(fo, "%10s%-30s ", "", "file and line");
		if (pflag)
			fprintf(fo, "%s", "trace");
		fprintf(fo, "\n");
	}
}

static void
print_entry(FILE *fo, kvm_t *kd, int n, int i, struct ktr_entry *entry)
{
	struct ktr_info *info = NULL;

	fprintf(fo, " %5d ", i);
	if (cflag)
		fprintf(fo, "%-3d ", n);
	if (tflag || rflag) {
		if (rflag)
			fprintf(fo, "%-16lld ", entry->ktr_timestamp -
						last_timestamp);
		else
			fprintf(fo, "%-16lld ", entry->ktr_timestamp);
	}
	if (xflag)	
		fprintf(fo, "%p %p ", entry->ktr_caller1, entry->ktr_caller2);
	if (iflag) {
		info = kvm_ktrinfo(kd, entry->ktr_info);
		if (info)
			fprintf(fo, "%-20s ", kvm_string(kd, info->kf_name));
		else
			fprintf(fo, "%-20s ", "<empty>");
	}
	if (fflag)
		fprintf(fo, "%34s:%-4d ", trunc_path(kvm_string(kd, entry->ktr_file), 34), entry->ktr_line);
	if (pflag) {
		if (info == NULL)
			info = kvm_ktrinfo(kd, entry->ktr_info);
		if (info) {
			fprintf(fo, kvm_string(kd, info->kf_format),
				entry->ktr_data[0], entry->ktr_data[1],
				entry->ktr_data[2], entry->ktr_data[3],
				entry->ktr_data[4], entry->ktr_data[5],
				entry->ktr_data[6], entry->ktr_data[7],
				entry->ktr_data[8], entry->ktr_data[9]);
		} else {
			fprintf(fo, "");
		}
	}
	fprintf(fo, "\n");
	last_timestamp = entry->ktr_timestamp;
}

static
struct ktr_info *
kvm_ktrinfo(kvm_t *kd, void *kptr)
{
	static struct ktr_info save_info;
	static void *save_kptr;

	if (kptr == NULL)
		return(NULL);
	if (save_kptr != kptr) {
		if (kvm_read(kd, (uintptr_t)kptr, &save_info, sizeof(save_info)) == -1) {
			bzero(&save_info, sizeof(save_info));
		} else {
			save_kptr = kptr;
		}
	}
	return(&save_info);
}

static
const char *
kvm_string(kvm_t *kd, const char *kptr)
{
	static char save_str[128];
	static const char *save_kptr;
	int l;
	int n;

	if (kptr == NULL)
		return("?");
	if (save_kptr != kptr) {
		save_kptr = kptr;
		l = 0;
		while (l < sizeof(save_str) - 1) {
			n = 256 - ((intptr_t)(kptr + l) & 255);
			if (n > sizeof(save_str) - l - 1)
				n = sizeof(save_str) - l - 1;
			if (kvm_read(kd, (uintptr_t)(kptr + l), save_str + l, n) < 0)
				break;
			while (l < sizeof(save_str) && n) {
			    if (save_str[l] == 0)
				    break;
			    --n;
			    ++l;
			}
			if (n)
			    break;
		}
		save_str[l] = 0;
	}
	return(save_str);
}

static
const char *
trunc_path(const char *str, int maxlen)
{
	int len = strlen(str);

	if (len > maxlen)
		return(str + len - maxlen);
	else
		return(str);
}

static void
usage(void)
{
	fprintf(stderr, "usage: ktrdump [-acfipqrtx] [-N execfile] "
			"[-M corefile] [-o outfile]\n");
	exit(1);
}

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
 * $DragonFly: src/usr.bin/pctrack/pctrack.c,v 1.2 2008/09/02 11:50:46 matthias Exp $
 */

#include <sys/kinfo.h>
#include <sys/types.h>
#include <sys/ktr.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/queue.h>

#include <err.h>
#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <nlist.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>

#define	SBUFLEN	128

static void usage(void);
static void do_output(int, int, struct kinfo_pcheader *, struct kinfo_pctrack *, int);
static void read_symbols(const char *);
static const char *address_to_symbol(void *);

static struct nlist nl[] = {
	{ .n_name = "_ncpus" },
	{ .n_name = "_cputime_pcheader" },
	{ .n_name = "_cputime_pctrack" },
	{ .n_name = NULL }
};

static char corefile[PATH_MAX];
static char execfile[PATH_MAX];
static char errbuf[_POSIX2_LINE_MAX];

static int sflag;
static int iflag;
static int nflag;
static int fflag;
static int cflag = -1;
static int Nflag;
static int Mflag;

/*
 * Reads the cputime_pctrack[] structure from the kernel and displays
 * the results in a human readable format.
 */
int
main(int ac, char **av)
{
	struct kinfo_pcheader pchead;
	struct kinfo_pctrack pctrack;
	kvm_t *kd;
	int ntrack;
	int ncpus;
	int cpu;
	int repeat;
	int c;

	/*
	 * Parse commandline arguments.
	 */
	while ((c = getopt(ac, av, "nsifc:N:M:")) != -1) {
		switch (c) {
		case 'N':
			if (strlcpy(execfile, optarg, sizeof(execfile))
			    >= sizeof(execfile))
				errx(1, "%s: File name too long", optarg);
			Nflag = 1;
			break;
		case 'M':
			if (strlcpy(corefile, optarg, sizeof(corefile))
			    >= sizeof(corefile))
				errx(1, "%s: File name too long", optarg);
			Mflag = 1;
			break;
		case 'c':
			cflag = strtol(optarg, NULL, 0);
			break;
		case 's':
			sflag = 1;
			break;
		case 'i':
			iflag = 1;
			break;
		case 'n':
			nflag = 1;
			break;
		case 'f':
			fflag = 1;
			break;
		default:
			usage();
		}
	}

	if (sflag == 0 && iflag == 0) {
		sflag = 1;
		iflag = 1;
	}
	if (nflag == 0)
		read_symbols(Nflag ? execfile : NULL);

	if (fflag && (cflag < 0 || sflag + iflag > 1)) {
		fprintf(stderr, "-f can only be specified with a particular cpu and just one of -i or -s\n");
		exit(1);
	}

	ac -= optind;
	av += optind;
	if (ac != 0 && strtod(av[0], NULL) > 0.0) {
		repeat = (int)(strtod(av[0], NULL) * 1000000.0);
		++av;
		--ac;
	} else if (fflag) {
		repeat = 1000000 / 10;
	} else {
		repeat = 0;
	}
	if (ac != 0)
		usage();

	/*
	 * Open our execfile and corefile, resolve needed symbols and read in
	 * the trace buffer.
	 */
	if ((kd = kvm_openfiles(Nflag ? execfile : NULL,
	    Mflag ? corefile : NULL, NULL, O_RDONLY, errbuf)) == NULL)
		errx(1, "%s", errbuf);
	if (kvm_nlist(kd, nl) != 0)
		errx(1, "%s", kvm_geterr(kd));

	if (kvm_read(kd, nl[0].n_value, &ncpus, sizeof(ncpus)) == -1)
		errx(1, "%s", kvm_geterr(kd));
	if (kvm_read(kd, nl[1].n_value, &pchead, sizeof(pchead)) == -1)
		errx(1, "%s", kvm_geterr(kd));

again:
	for (cpu = 0; cpu < ncpus; ++cpu) {
		for (ntrack = 0; ntrack < pchead.pc_ntrack; ++ntrack) {
			int offset;

			if (ntrack == PCTRACK_SYS && sflag == 0)
				continue;
			if (ntrack == PCTRACK_INT && iflag == 0)
				continue;
			if (cflag >= 0 && cflag != cpu)
				continue;

			offset = offsetof(struct kinfo_pctrack, 
					  pc_array[pchead.pc_arysize]);
			offset = (offset * pchead.pc_ntrack * cpu) +
				 (offset * ntrack);
			if (kvm_read(kd, nl[2].n_value + offset, &pctrack, sizeof(pctrack)) < 0)
				errx(1, "%s", kvm_geterr(kd));

			printf("CPU %d %s:\n", cpu,
				(ntrack == PCTRACK_SYS) ? "SYSTEM" :
				(ntrack == PCTRACK_INT) ? "INTERRUPT" : "?"
			);

			do_output(cpu, ntrack, &pchead, &pctrack, pctrack.pc_index - pchead.pc_arysize);
			while (fflag) {
				usleep(repeat);
				int last_index = pctrack.pc_index;
				kvm_read(kd, nl[2].n_value + offset, &pctrack,
					 sizeof(pctrack));
				do_output(cpu, ntrack, &pchead, &pctrack, last_index);
			}
		}
	}
	if (repeat) {
		usleep(repeat);
		goto again;
	}
	return(0);
}

static void
do_output(int cpu __unused, int track __unused, struct kinfo_pcheader *pchead, struct kinfo_pctrack *pctrack, int base_index)
{
	int i;

	i = base_index;
	if (pctrack->pc_index - base_index > pchead->pc_arysize) {
		i = pctrack->pc_index - pchead->pc_arysize;
	}
	while (i < pctrack->pc_index) {
		void *data = pctrack->pc_array[i & (pchead->pc_arysize - 1)];
		if (nflag)
			printf("\t%p\n", data);
		else
			printf("\t%s\n", address_to_symbol(data));
		++i;
	}
}

struct symdata {
	TAILQ_ENTRY(symdata) link;
	const char *symname;
	char *symaddr;
	char symtype;
};

static TAILQ_HEAD(symlist, symdata) symlist;
static struct symdata *symcache;
static char *symbegin;
static char *symend;

static void
read_symbols(const char *file)
{
	char buf[256];
	char cmd[256];
	size_t buflen = sizeof(buf);
	FILE *fp;
	struct symdata *sym;
	char *s1;
	char *s2;
	char *s3;

	TAILQ_INIT(&symlist);

	if (file == NULL) {
		if (sysctlbyname("kern.bootfile", buf, &buflen, NULL, 0) < 0)
			file = "/boot/kernel";
		else
			file = buf;
	}
	snprintf(cmd, sizeof(cmd), "nm -n %s", file);
	if ((fp = popen(cmd, "r")) != NULL) {
		while (fgets(buf, sizeof(buf), fp) != NULL) {
		    s1 = strtok(buf, " \t\n");
		    s2 = strtok(NULL, " \t\n");
		    s3 = strtok(NULL, " \t\n");
		    if (s1 && s2 && s3) {
			sym = malloc(sizeof(struct symdata));
			sym->symaddr = (char *)strtoul(s1, NULL, 16);
			sym->symtype = s2[0];
			sym->symname = strdup(s3);
			if (strcmp(s3, "kernbase") == 0)
				symbegin = sym->symaddr;
			if (strcmp(s3, "end") == 0)
				symend = sym->symaddr;
			TAILQ_INSERT_TAIL(&symlist, sym, link);
		    }
		}
		pclose(fp);
	}
	symcache = TAILQ_FIRST(&symlist);
}

static const char *
address_to_symbol(void *kptr)
{
	static char buf[64];

	if (symcache == NULL ||
	   (char *)kptr < symbegin || (char *)kptr >= symend
	) {
		snprintf(buf, sizeof(buf), "%p", kptr);
		return(buf);
	}
	while ((char *)symcache->symaddr < (char *)kptr) {
		if (TAILQ_NEXT(symcache, link) == NULL)
			break;
		symcache = TAILQ_NEXT(symcache, link);
	}
	while ((char *)symcache->symaddr > (char *)kptr) {
		if (symcache != TAILQ_FIRST(&symlist))
			symcache = TAILQ_PREV(symcache, symlist, link);
	}
	snprintf(buf, sizeof(buf), "%s+%d", symcache->symname,
		(int)((char *)kptr - symcache->symaddr));
	return(buf);
}

static void
usage(void)
{
	fprintf(stderr, "usage: pctrack [-nsi] [-c cpu] [-N execfile] "
			"[-M corefile]\n");
	exit(1);
}

/*
 * cc randread.c -o ~/bin/randread -O2 -lm
 *
 * randread device [bufsize:512 [range%:90 [nprocs:32]]]
 *
 * requires TSC
 */
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/errno.h>
#include <sys/wait.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <assert.h>
#include <machine/atomic.h>
#include <machine/cpufunc.h>

typedef struct pdata {
	int64_t	counter;
	int64_t lotime;
	int64_t hitime;
	int64_t tsc_total1;
	int64_t tsc_total2;
	int64_t unused00;
	int64_t unused01;
	int	unused02;
	int	reset;
} pdata_t;

int
main(int ac, char **av)
{
    char *buf;
    size_t bytes = 512;
    off_t limit;
    int fd;
    int i;
    int loops;
    int nprocs = 32;
    double range = 90.0;
    volatile pdata_t *pdata;
    int64_t tsc1;
    int64_t tsc2;
    int64_t delta;
    int64_t tscfreq = 0;
    int64_t lotime;
    int64_t hitime;
    size_t tscfreq_size = sizeof(tscfreq);

    sysctlbyname("hw.tsc_frequency", &tscfreq, &tscfreq_size, NULL, 0);
    assert(tscfreq != 0);

    if (ac < 2 || ac > 5) {
	fprintf(stderr, "%s <device> [bufsize:512 [range%:90 [nprocs:32]]]\n",
		av[0]);
	exit (1);
    }

    if (ac >= 3) {
	bytes = (size_t)strtoul(av[2], NULL, 0);
	if (bytes < 512 || (bytes ^ (bytes - 1)) != ((bytes << 1) - 1)) {
	    fprintf(stderr, "bytes must be a power of 2 >= 512\n");
	    exit (1);
	}
    }
    buf = malloc(bytes);

    if (ac >= 4) {
	range = strtod(av[3], NULL);
    }

    if (ac >= 5) {
	nprocs = strtol(av[4], NULL, 0);
	if (nprocs < 0 || nprocs > 512) {
	    fprintf(stderr, "absurd nprocs (%d)\n", nprocs);
	    exit(1);
	}
    }

    fd = open(av[1], O_RDONLY);
    if (fd < 0) {
	fprintf(stderr, "open %s: %s\n", av[1], strerror(errno));
	exit (1);
    }

    lseek(fd, 0L, 2);
    limit = lseek(fd, 0L, 1);
    limit = (off_t)((double)limit * range / 100.0);
    limit &= ~(off_t)(bytes - 1);
    printf("device %s bufsize %zd limit %4.3fGB nprocs %d\n",
	av[1], bytes, (double)limit / (1024.0*1024.0*1024.0), nprocs);

    pdata = mmap(NULL, nprocs * sizeof(*pdata), PROT_READ|PROT_WRITE,
		    MAP_SHARED|MAP_ANON, -1, 0);

    for (i = 0; i < nprocs; ++i) {
	if (fork() == 0) {
	    close(fd);
	    fd = open(av[1], O_RDONLY);
	    srandomdev();
	    pdata += i;

	    tsc2 = rdtsc();
	    pdata->lotime = 0x7FFFFFFFFFFFFFFFLL;

	    for (;;) {
		long pos;

		if (pdata->reset) {
			pdata->counter = 0;
			pdata->tsc_total1 = 0;
			pdata->tsc_total2 = 0;
			pdata->lotime = 0x7FFFFFFFFFFFFFFFLL;
			pdata->hitime = 0;
			pdata->reset = 0;
		}

		pos = random() ^ ((long)random() << 31);
		pos &= 0x7FFFFFFFFFFFFFFFLLU;
		pos = (pos % limit) & ~(off_t)(bytes - 1);
		lseek(fd, pos, 0);
		read(fd, buf, bytes);
		tsc1 = tsc2;
		tsc2 = rdtsc();
		delta = tsc2 - tsc1;
		++pdata->counter;
		pdata->tsc_total1 += delta;
		pdata->tsc_total2 += delta * delta;
		if (pdata->lotime > delta)
			pdata->lotime = delta;
		if (pdata->hitime < delta)
			pdata->hitime = delta;
	    }
	}
    }

    tsc2 = rdtsc();
    loops = 0;

    for (;;) {
	int64_t count;
	int64_t total1;
	int64_t total2;
	double v;
	double lo;
	double hi;
	double s1;
	double s2;
	double stddev;

	sleep(1);
	lotime = pdata[0].lotime;
	hitime = pdata[0].hitime;
	total1 = 0;
	total2 = 0;
	count = 0;

	for (i = 0; i < nprocs; ++i) {
		count += pdata[i].counter;
		total1 += pdata[i].tsc_total1;
		total2 += pdata[i].tsc_total2;
		if (lotime > pdata[i].lotime)
			lotime = pdata[i].lotime;
		if (hitime < pdata[i].hitime)
			hitime = pdata[i].hitime;
		pdata[i].reset = 1;
	}
	tsc1 = tsc2;
	tsc2 = rdtsc();
	delta = tsc2 - tsc1;
	v = count * ((double)delta / (double)tscfreq);
	lo = (double)lotime / (double)tscfreq;
	hi = (double)hitime / (double)tscfreq;

	s1 = ((double)total2 - (double)total1 * (double)total1 / (double)count) / ((double)count - 1);
	if (s1 < 0.0)
		stddev = -sqrt(-s1);
	else
		stddev = sqrt(s1);
	stddev = stddev / (double)tscfreq;	/* normalize to 1 second units */

	if (loops) {
		printf("%6.0f/s avg=%6.2fuS bw=%-6.2fMB/s "
		       "lo=%-3.2fuS, hi=%-3.2fuS stddev=%3.2fuS\n",
		       v,
		       1e6 * nprocs / v,
		       (double)count * bytes / 1e6 / ((double)delta / (double)tscfreq),
		       lo * 1e6,
		       hi * 1e6,
		       stddev * 1e6);
	}
	++loops;
    }
    return 0;
}

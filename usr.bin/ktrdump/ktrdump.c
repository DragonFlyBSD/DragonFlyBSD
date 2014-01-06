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
 */

#include <sys/types.h>
#include <sys/ktr.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/queue.h>

#include <ctype.h>
#include <devinfo.h>
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
#include <evtr.h>
#include <stdarg.h>

struct ktr_buffer {
	struct ktr_entry *ents;
	int modified;
	int reset;
	int beg_idx;		/* Beginning index */
	int end_idx;		/* Ending index */
};

static struct nlist nl1[] = {
	{ .n_name = "_ktr_version" },
	{ .n_name = "_ktr_entries" },
	{ .n_name = "_ncpus" },
	{ .n_name = NULL }
};

static struct nlist nl2[] = {
	{ .n_name = "_tsc_frequency" },
	{ .n_name = NULL }
};

static struct nlist nl_version_ktr_idx[] = {
	{ .n_name = "_ktr_idx" },
	{ .n_name = "_ktr_buf" },
	{ .n_name = NULL }
};

static struct nlist nl_version_ktr_cpu[] = {
	{ .n_name = "_ktr_cpu" },
	{ .n_name = NULL }
};

struct save_ctx {
	char save_buf[512];
	const void *save_kptr;
};

typedef void (*ktr_iter_cb_t)(void *, int, int, struct ktr_entry *, uint64_t *);

#ifdef __x86_64__
/* defined according to the x86_64 ABI spec */
struct my_va_list {
	uint32_t gp_offset;	/* offset to next available gpr in reg_save_area */
	uint32_t fp_offset;	/* offset to next available fpr in reg_save_area */
	void *overflow_arg_area;	/* args that are passed on the stack */
	struct reg_save_area *reg_save_area;		/* register args */
	/*
	 * NOT part of the ABI. ->overflow_arg_area gets advanced when code
	 * iterates over the arguments with va_arg(). That means we need to
	 * keep a copy in order to free the allocated memory (if any)
	 */
	void *overflow_arg_area_save;
} __attribute__((packed));

typedef struct my_va_list *machine_va_list;

struct reg_save_area {
	uint64_t rdi, rsi, rdx, rcx, r8, r9;
	/* XMM registers follow, but we don't use them */
};
#elif __i386__
typedef void *machine_va_list;
#endif

static int cflag;
static int dflag;
static int fflag;
static int iflag;
static int lflag;
static int nflag;
static int qflag;
static int rflag;
static int sflag;
static int tflag;
static int xflag;
static int pflag;
static int Mflag;
static int Nflag;
static double tsc_frequency;
static double correction_factor = 0.0;

static char corefile[PATH_MAX];
static char execfile[PATH_MAX];

static char errbuf[_POSIX2_LINE_MAX];
static int ncpus;
static kvm_t *kd;
static int entries_per_buf;
static int fifo_mask;
static int ktr_version;

static void usage(void);
static int earliest_ts(struct ktr_buffer *);
static void dump_machine_info(evtr_t);
static void dump_device_info(evtr_t);
static void print_header(FILE *, int);
static void print_entry(FILE *, int, int, struct ktr_entry *, u_int64_t *);
static void print_callback(void *, int, int, struct ktr_entry *, uint64_t *);
static void dump_callback(void *, int, int, struct ktr_entry *, uint64_t *);
static struct ktr_info *kvm_ktrinfo(void *, struct save_ctx *);
static const char *kvm_string(const char *, struct save_ctx *);
static const char *trunc_path(const char *, int);
static void read_symbols(const char *);
static const char *address_to_symbol(void *, struct save_ctx *);
static struct ktr_buffer *ktr_bufs_init(void);
static void get_indices(struct ktr_entry **, int *);
static void load_bufs(struct ktr_buffer *, struct ktr_entry **, int *);
static void iterate_buf(FILE *, struct ktr_buffer *, int, u_int64_t *, ktr_iter_cb_t);
static void iterate_bufs_timesorted(FILE *, struct ktr_buffer *, u_int64_t *, ktr_iter_cb_t);
static void kvmfprintf(FILE *fp, const char *ctl, va_list va);
static int va_list_from_blob(machine_va_list *valist, const char *fmt, char *blob, size_t blobsize);
static void va_list_cleanup(machine_va_list *valist);
/*
 * Reads the ktr trace buffer from kernel memory and prints the trace entries.
 */
int
main(int ac, char **av)
{
	struct ktr_buffer *ktr_bufs;
	struct ktr_entry **ktr_kbuf;
	ktr_iter_cb_t callback = &print_callback;
	int *ktr_idx;
	FILE *fo;
	void *ctx;
	int64_t tts;
	int *ktr_start_index;
	int c;
	int n;

	/*
	 * Parse commandline arguments.
	 */
	fo = stdout;
	while ((c = getopt(ac, av, "acfinqrtxpslA:N:M:o:d")) != -1) {
		switch (c) {
		case 'a':
			cflag = 1;
			iflag = 1;
			rflag = 1;
			xflag = 1;
			pflag = 1;
			sflag = 1;
			break;
		case 'c':
			cflag = 1;
			break;
		case 'd':
			dflag = 1;
			sflag = 1;
			callback = &dump_callback;
			break;
		case 'N':
			if (strlcpy(execfile, optarg, sizeof(execfile))
			    >= sizeof(execfile))
				errx(1, "%s: File name too long", optarg);
			Nflag = 1;
			break;
		case 'f':
			fflag = 1;
			break;
		case 'l':
			lflag = 1;
			break;
		case 'i':
			iflag = 1;
			break;
		case 'A':
			correction_factor = strtod(optarg, NULL);
			break;
		case 'M':
			if (strlcpy(corefile, optarg, sizeof(corefile))
			    >= sizeof(corefile))
				errx(1, "%s: File name too long", optarg);
			Mflag = 1;
			break;
		case 'n':
			nflag = 1;
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
		case 's':
			sflag = 1;	/* sort across the cpus */
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
	}
	ctx = fo;
	if (dflag) {
		ctx = evtr_open_write(fo);
		if (!ctx) {
			err(1, "Can't create event stream");
		}
	}
	if (cflag + iflag + tflag + xflag + fflag + pflag == 0) {
		cflag = 1;
		iflag = 1;
		tflag = 1;
		pflag = 1;
	}
	if (correction_factor != 0.0 && (rflag == 0 || nflag)) {
		fprintf(stderr, "Correction factor can only be applied with -r and without -n\n");
		exit(1);
	}
	ac -= optind;
	av += optind;
	if (ac != 0)
		usage();

	/*
	 * Open our execfile and corefile, resolve needed symbols and read in
	 * the trace buffer.
	 */
	if ((kd = kvm_openfiles(Nflag ? execfile : NULL,
	    Mflag ? corefile : NULL, NULL, O_RDONLY, errbuf)) == NULL)
		errx(1, "%s", errbuf);
	if (kvm_nlist(kd, nl1) != 0)
		errx(1, "%s", kvm_geterr(kd));
	if (kvm_read(kd, nl1[0].n_value, &ktr_version, sizeof(ktr_version)) == -1)
		errx(1, "%s", kvm_geterr(kd));
	if (kvm_read(kd, nl1[2].n_value, &ncpus, sizeof(ncpus)) == -1)
		errx(1, "%s", kvm_geterr(kd));
	ktr_start_index = malloc(sizeof(*ktr_start_index) * ncpus);
	if (ktr_version >= KTR_VERSION_WITH_FREQ && kvm_nlist(kd, nl2) == 0) {
		if (kvm_read(kd, nl2[0].n_value, &tts, sizeof(tts)) == -1)
			errx(1, "%s", kvm_geterr(kd));
		tsc_frequency = (double)tts;
	}
	if (ktr_version > KTR_VERSION)
		errx(1, "ktr version too high for us to handle");
	if (kvm_read(kd, nl1[1].n_value, &entries_per_buf,
				sizeof(entries_per_buf)) == -1)
		errx(1, "%s", kvm_geterr(kd));
	fifo_mask = entries_per_buf - 1;

	printf("TSC frequency is %6.3f MHz\n", tsc_frequency / 1000000.0);

	if (dflag) {
		dump_machine_info((evtr_t)ctx);
		dump_device_info((evtr_t)ctx);
	}
	ktr_kbuf = calloc(ncpus, sizeof(*ktr_kbuf));
	ktr_idx = calloc(ncpus, sizeof(*ktr_idx));

	if (nflag == 0)
		read_symbols(Nflag ? execfile : NULL);

	if (ktr_version < KTR_VERSION_KTR_CPU) {
		if (kvm_nlist(kd, nl_version_ktr_idx))
			errx(1, "%s", kvm_geterr(kd));
	} else {
		if (kvm_nlist(kd, nl_version_ktr_cpu))
			errx(1, "%s", kvm_geterr(kd));
	}

	get_indices(ktr_kbuf, ktr_idx);

	ktr_bufs = ktr_bufs_init();

	if (sflag) {
		u_int64_t last_timestamp = 0;
		do {
			load_bufs(ktr_bufs, ktr_kbuf, ktr_idx);
			iterate_bufs_timesorted(ctx, ktr_bufs, &last_timestamp,
						callback);
			if (lflag)
				usleep(1000000 / 10);
		} while (lflag);
	} else {
		u_int64_t *last_timestamp = calloc(sizeof(u_int64_t), ncpus);
		do {
			load_bufs(ktr_bufs, ktr_kbuf, ktr_idx);
			for (n = 0; n < ncpus; ++n)
				iterate_buf(ctx, ktr_bufs, n, &last_timestamp[n],
					callback);
			if (lflag)
				usleep(1000000 / 10);
		} while (lflag);
	}
	if (dflag)
		evtr_close(ctx);
	return (0);
}

static
int
dump_devinfo(struct devinfo_dev *dev, void *arg)
{
	struct evtr_event ev;
	evtr_t evtr = (evtr_t)arg;
	const char *fmt = "#devicenames[\"%s\"] = %#lx";
	char fmtdatabuf[sizeof(char *) + sizeof(devinfo_handle_t)];
	char *fmtdata = fmtdatabuf;

	if (!dev->dd_name[0])
		return 0;
	ev.type = EVTR_TYPE_PROBE;
	ev.ts = 0;
	ev.line = 0;
	ev.file = NULL;
	ev.cpu = -1;
	ev.func = NULL;
	ev.fmt = fmt;
	((char **)fmtdata)[0] = &dev->dd_name[0];
	fmtdata += sizeof(char *);
	((devinfo_handle_t *)fmtdata)[0] = dev->dd_handle;
	ev.fmtdata = fmtdatabuf;
	ev.fmtdatalen = sizeof(fmtdatabuf);

	if (evtr_dump_event(evtr, &ev)) {
		err(1, "%s", evtr_errmsg(evtr));
	}

	return devinfo_foreach_device_child(dev, dump_devinfo, evtr);
}

static
void
dump_device_info(evtr_t evtr)
{
	struct devinfo_dev *root;
	if (devinfo_init())
		return;
	if (!(root = devinfo_handle_to_device(DEVINFO_ROOT_DEVICE))) {
		warn("can't find root device");
		return;
	}
	devinfo_foreach_device_child(root, dump_devinfo, evtr);
}

static
void
dump_machine_info(evtr_t evtr)
{
	struct evtr_event ev;
	int i;

	bzero(&ev, sizeof(ev));
	ev.type = EVTR_TYPE_SYSINFO;
	ev.ncpus = ncpus;
	evtr_dump_event(evtr, &ev);
	if (evtr_error(evtr)) {
		err(1, "%s", evtr_errmsg(evtr));
	}

	for (i = 0; i < ncpus; ++i) {
		bzero(&ev, sizeof(ev));
		ev.type = EVTR_TYPE_CPUINFO;
		ev.cpu = i;
		ev.cpuinfo.freq = tsc_frequency;
		evtr_dump_event(evtr, &ev);
		if (evtr_error(evtr)) {
			err(1, "%s", evtr_errmsg(evtr));
		}
	}
}

static void
print_header(FILE *fo, int row)
{
	if (qflag == 0 && (u_int32_t)row % 20 == 0) {
		fprintf(fo, "%-6s ", "index");
		if (cflag)
			fprintf(fo, "%-3s ", "cpu");
		if (tflag || rflag)
			fprintf(fo, "%-16s ", "timestamp");
		if (xflag) {
			if (nflag)
			    fprintf(fo, "%-10s %-10s", "caller2", "caller1");
			else
			    fprintf(fo, "%-20s %-20s", "caller2", "caller1");
		}
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
print_entry(FILE *fo, int n, int row, struct ktr_entry *entry,
	    u_int64_t *last_timestamp)
{
	struct ktr_info *info = NULL;
	static struct save_ctx nctx, pctx, fmtctx, symctx, infoctx;

	fprintf(fo, " %06x ", row & 0x00FFFFFF);
	if (cflag)
		fprintf(fo, "%-3d ", n);
	if (tflag || rflag) {
		if (rflag && !nflag && tsc_frequency != 0.0) {
			fprintf(fo, "%13.3f uS ",
				(double)(entry->ktr_timestamp - *last_timestamp) * 1000000.0 / tsc_frequency - correction_factor);
		} else if (rflag) {
			fprintf(fo, "%-16ju ",
			    (uintmax_t)(entry->ktr_timestamp - *last_timestamp));
		} else {
			fprintf(fo, "%-16ju ",
			    (uintmax_t)entry->ktr_timestamp);
		}
	}
	if (xflag) {
		if (nflag) {
		    fprintf(fo, "%p %p ", 
			    entry->ktr_caller2, entry->ktr_caller1);
		} else {
		    fprintf(fo, "%-25s ", 
			    address_to_symbol(entry->ktr_caller2, &symctx));
		    fprintf(fo, "%-25s ", 
			    address_to_symbol(entry->ktr_caller1, &symctx));
		}
	}
	if (iflag) {
		info = kvm_ktrinfo(entry->ktr_info, &infoctx);
		if (info)
			fprintf(fo, "%-20s ", kvm_string(info->kf_name, &nctx));
		else
			fprintf(fo, "%-20s ", "<empty>");
	}
	if (fflag)
		fprintf(fo, "%34s:%-4d ",
			trunc_path(kvm_string(entry->ktr_file, &pctx), 34),
			entry->ktr_line);
	if (pflag) {
		if (info == NULL)
			info = kvm_ktrinfo(entry->ktr_info, &infoctx);
		if (info) {
			machine_va_list ap;
			const char *fmt;
			fmt = kvm_string(info->kf_format, &fmtctx);
			if (va_list_from_blob(&ap, fmt,
					      (char *)&entry->ktr_data,
					      info->kf_data_size))
				err(2, "Can't generate va_list from %s\n", fmt);
			kvmfprintf(fo, kvm_string(info->kf_format, &fmtctx),
				   (void *)ap);
			va_list_cleanup(&ap);
		}
	}
	fprintf(fo, "\n");
	*last_timestamp = entry->ktr_timestamp;
}

static
void
print_callback(void *ctx, int n, int row, struct ktr_entry *entry, uint64_t *last_ts)
{
	FILE *fo = (FILE *)ctx;
	print_header(fo, row);
	print_entry(fo, n, row, entry, last_ts);
}

/*
 * If free == 0, replace all (kvm) string pointers in fmtdata with pointers
 * to user-allocated copies of the strings.
 * If free != 0, free those pointers.
 */
static
int
mangle_string_ptrs(const char *fmt, uint8_t *fmtdata, int dofree)
{
	const char *f, *p;
	size_t skipsize, intsz;
	static struct save_ctx strctx;
	int ret = 0;

	for (f = fmt; f[0] != '\0'; ++f) {
		if (f[0] != '%')
			continue;
		++f;
		skipsize = 0;
		for (p = f; p[0]; ++p) {
			int again = 0;
			/*
			 * Eat flags. Notice this will accept duplicate
			 * flags.
			 */
			switch (p[0]) {
			case '#':
			case '0':
			case '-':
			case ' ':
			case '+':
			case '\'':
				again = !0;
				break;
			}
			if (!again)
				break;
		}
		/* Eat minimum field width, if any */
		for (; isdigit(p[0]); ++p)
			;
		if (p[0] == '.')
			++p;
		/* Eat precision, if any */
		for (; isdigit(p[0]); ++p)
			;
		intsz = 0;
		switch (p[0]) {
		case 'l':
			if (p[1] == 'l') {
				++p;
				intsz = sizeof(long long);
			} else {
				intsz = sizeof(long);
			}
			break;
		case 'j':
			intsz = sizeof(intmax_t);
			break;
		case 't':
			intsz = sizeof(ptrdiff_t);
			break;
		case 'z':
			intsz = sizeof(size_t);
			break;
		default:
			break;
		}
		if (intsz != 0)
			++p;
		else
			intsz = sizeof(int);

		switch (p[0]) {
		case 'd':
		case 'i':
		case 'o':
		case 'u':
		case 'x':
		case 'X':
		case 'c':
			skipsize = intsz;
			break;
		case 'p':
			skipsize = sizeof(void *);
			break;
		case 'f':
			if (p[-1] == 'l')
				skipsize = sizeof(double);
			else
				skipsize = sizeof(float);
			break;
		case 's':
			if (dofree) {
			  char *t = ((char **)fmtdata)[0];
			  free(t);
			  skipsize = sizeof(char *);
			} else {
			  char *t = strdup(kvm_string(((char **)fmtdata)[0],
							  &strctx));
			  ((const char **)fmtdata)[0] = t;
					
				skipsize = sizeof(char *);
			}
			++ret;
			break;
		default:
			fprintf(stderr, "Unknown conversion specifier %c "
				"in fmt starting with %s", p[0], f - 1);
			return -1;
		}
		fmtdata += skipsize;
	}
	return ret;
}

static
void
dump_callback(void *ctx, int n, int row __unused, struct ktr_entry *entry,
	      uint64_t *last_ts __unused)
{
	evtr_t evtr = (evtr_t)ctx;
	struct evtr_event ev;
	static struct save_ctx pctx, fmtctx, infoctx;
	struct ktr_info *ki;
	int conv = 0;	/* pointless */

	ev.ts = entry->ktr_timestamp;
	ev.type = EVTR_TYPE_PROBE;
	ev.line = entry->ktr_line;
	ev.file = kvm_string(entry->ktr_file, &pctx);
	ev.func = NULL;
	ev.cpu = n;
	if ((ki = kvm_ktrinfo(entry->ktr_info, &infoctx))) {
		ev.fmt = kvm_string(ki->kf_format, &fmtctx);
		ev.fmtdata = entry->ktr_data;
		if ((conv = mangle_string_ptrs(ev.fmt,
					       __DECONST(uint8_t *, ev.fmtdata),
					       0)) < 0)
			errx(1, "Can't parse format string\n");
		ev.fmtdatalen = ki->kf_data_size;
	} else {
		ev.fmt = ev.fmtdata = NULL;
		ev.fmtdatalen = 0;
	}
	if (evtr_dump_event(evtr, &ev)) {
		err(1, "%s", evtr_errmsg(evtr));
	}
	if (ev.fmtdata && conv) {
		mangle_string_ptrs(ev.fmt, __DECONST(uint8_t *, ev.fmtdata),
				   !0);
	}
}

static
struct ktr_info *
kvm_ktrinfo(void *kptr, struct save_ctx *ctx)
{
	struct ktr_info *ki = (void *)ctx->save_buf;

	if (kptr == NULL)
		return(NULL);
	if (ctx->save_kptr != kptr) {
		if (kvm_read(kd, (uintptr_t)kptr, ki, sizeof(*ki)) == -1) {
			bzero(&ki, sizeof(*ki));
		} else {
			ctx->save_kptr = kptr;
		}
	}
	return(ki);
}

static
const char *
kvm_string(const char *kptr, struct save_ctx *ctx)
{
	u_int l;
	u_int n;

	if (kptr == NULL)
		return("?");
	if (ctx->save_kptr != (const void *)kptr) {
		ctx->save_kptr = (const void *)kptr;
		l = 0;
		while (l < sizeof(ctx->save_buf) - 1) {
			n = 256 - ((intptr_t)(kptr + l) & 255);
			if (n > sizeof(ctx->save_buf) - l - 1)
				n = sizeof(ctx->save_buf) - l - 1;
			if (kvm_read(kd, (uintptr_t)(kptr + l), ctx->save_buf + l, n) < 0)
				break;
			while (l < sizeof(ctx->save_buf) && n) {
			    if (ctx->save_buf[l] == 0)
				    break;
			    --n;
			    ++l;
			}
			if (n)
			    break;
		}
		ctx->save_buf[l] = 0;
	}
	return(ctx->save_buf);
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

static
void
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

static
const char *
address_to_symbol(void *kptr, struct save_ctx *ctx)
{
	char *buf = ctx->save_buf;
	int size = sizeof(ctx->save_buf);

	if (symcache == NULL ||
	   (char *)kptr < symbegin || (char *)kptr >= symend
	) {
		snprintf(buf, size, "%p", kptr);
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
	snprintf(buf, size, "%s+%d", symcache->symname,
		(int)((char *)kptr - symcache->symaddr));
	return(buf);
}

static
struct ktr_buffer *
ktr_bufs_init(void)
{
	struct ktr_buffer *ktr_bufs, *it;
	int i;

	ktr_bufs = malloc(sizeof(*ktr_bufs) * ncpus);
	if (!ktr_bufs)
		err(1, "can't allocate data structures\n");
	for (i = 0; i < ncpus; ++i) {
		it = ktr_bufs + i;
		it->ents = malloc(sizeof(struct ktr_entry) * entries_per_buf);
		if (it->ents == NULL)
			err(1, "can't allocate data structures\n");
		it->reset = 1;
		it->beg_idx = -1;
		it->end_idx = -1;
	}
	return ktr_bufs;
}

static
void
get_indices(struct ktr_entry **ktr_kbuf, int *ktr_idx)
{
	static struct ktr_cpu *ktr_cpus;
	int i;

	if (ktr_cpus == NULL)
		ktr_cpus = malloc(sizeof(*ktr_cpus) * ncpus);

	if (ktr_version < KTR_VERSION_KTR_CPU) {
		if (kvm_read(kd, nl_version_ktr_idx[0].n_value, ktr_idx,
		    sizeof(*ktr_idx) * ncpus) == -1) {
			errx(1, "%s", kvm_geterr(kd));
		}
		if (ktr_kbuf[0] == NULL) {
			if (kvm_read(kd, nl_version_ktr_idx[1].n_value,
			    ktr_kbuf, sizeof(*ktr_kbuf) * ncpus) == -1) {
				errx(1, "%s", kvm_geterr(kd));
			}
		}
	} else {
		if (kvm_read(kd, nl_version_ktr_cpu[0].n_value,
			     ktr_cpus, sizeof(*ktr_cpus) * ncpus) == -1) {
				errx(1, "%s", kvm_geterr(kd));
		}
		for (i = 0; i < ncpus; ++i) {
			ktr_idx[i] = ktr_cpus[i].core.ktr_idx;
			ktr_kbuf[i] = ktr_cpus[i].core.ktr_buf;
		}
	}
}

/*
 * Get the trace buffer data from the kernel
 */
static
void
load_bufs(struct ktr_buffer *ktr_bufs, struct ktr_entry **kbufs, int *ktr_idx)
{
	struct ktr_buffer *kbuf;
	int i;

	get_indices(kbufs, ktr_idx);
	for (i = 0; i < ncpus; ++i) {
		kbuf = &ktr_bufs[i];
		if (ktr_idx[i] == kbuf->end_idx)
			continue;
		kbuf->end_idx = ktr_idx[i];

		/*
		 * If we do not have a notion of the beginning index, assume
		 * it is entries_per_buf before the ending index.  Don't
		 * worry about underflows/negative numbers, the indices will
		 * be masked.
		 */
		if (kbuf->reset) {
			kbuf->beg_idx = kbuf->end_idx - entries_per_buf + 1;
			kbuf->reset = 0;
		}
		if (kvm_read(kd, (uintptr_t)kbufs[i], ktr_bufs[i].ents,
				sizeof(struct ktr_entry) * entries_per_buf)
									== -1)
			errx(1, "%s", kvm_geterr(kd));
		kbuf->modified = 1;
		kbuf->beg_idx = earliest_ts(kbuf);
	}

}

/*
 * Locate the earliest timestamp iterating backwards from end_idx, but
 * not going further back then beg_idx.  We have to do this because
 * the kernel uses a circulating buffer.
 */
static
int
earliest_ts(struct ktr_buffer *buf)
{
	struct ktr_entry *save;
	int count, scan, i, earliest;

	count = 0;
	earliest = buf->end_idx - 1;
	save = &buf->ents[earliest & fifo_mask];
	for (scan = buf->end_idx - 1; scan != buf->beg_idx -1; --scan) {
		i = scan & fifo_mask;
		if (buf->ents[i].ktr_timestamp <= save->ktr_timestamp &&
		    buf->ents[i].ktr_timestamp > 0)
			earliest = scan;
		/*
		 * We may have gotten so far behind that beg_idx wrapped
		 * more then once around the buffer.  Just stop
		 */
		if (++count == entries_per_buf)
			break;
	}
	return earliest;
}

static
void
iterate_buf(FILE *fo, struct ktr_buffer *ktr_bufs, int cpu,
	    u_int64_t *last_timestamp, ktr_iter_cb_t cb)
{
	struct ktr_buffer *buf = ktr_bufs + cpu;

	if (buf->modified == 0)
		return;
	if (*last_timestamp == 0) {
		*last_timestamp =
			buf->ents[buf->beg_idx & fifo_mask].ktr_timestamp;
	}
	while (buf->beg_idx != buf->end_idx) {
		cb(fo, cpu, buf->beg_idx,
		   &buf->ents[buf->beg_idx & fifo_mask],
		   last_timestamp);
		++buf->beg_idx;
	}
	buf->modified = 0;
}

static
void
iterate_bufs_timesorted(FILE *fo, struct ktr_buffer *ktr_bufs,
			u_int64_t *last_timestamp, ktr_iter_cb_t cb)
{
	struct ktr_entry *ent;
	struct ktr_buffer *buf;
	int n, bestn;
	u_int64_t ts;
	static int row = 0;

	for (;;) {
		ts = 0;
		bestn = -1;
		for (n = 0; n < ncpus; ++n) {
			buf = ktr_bufs + n;
			if (buf->beg_idx == buf->end_idx)
				continue;
			ent = &buf->ents[buf->beg_idx & fifo_mask];
			if (ts == 0 || (ts >= ent->ktr_timestamp)) {
				ts = ent->ktr_timestamp;
				bestn = n;
			}
		}
		if ((bestn < 0) || (ts < *last_timestamp))
			break;
		buf = ktr_bufs + bestn;
		cb(fo, bestn, row,
		   &buf->ents[buf->beg_idx & fifo_mask],
		   last_timestamp);
		++buf->beg_idx;
		*last_timestamp = ts;
		++row;
	}
}

static
void
kvmfprintf(FILE *fp, const char *ctl, va_list va)
{
	int n;
	int is_long;
	int is_done;
	char fmt[256];
	static struct save_ctx strctx;
	const char *s;

	while (*ctl) {
		for (n = 0; ctl[n]; ++n) {
			fmt[n] = ctl[n];
			if (ctl[n] == '%')
				break;
		}
		if (n == 0) {
			is_long = 0;
			is_done = 0;
			n = 1;
			while (n < (int)sizeof(fmt)) {
				fmt[n] = ctl[n];
				fmt[n+1] = 0;

				switch(ctl[n]) {
				case 'p':
					is_long = 1;
					/* fall through */
				case 'd':
				case 'u':
				case 'x':
				case 'o':
				case 'X':
					/*
					 * Integral
					 */
					switch(is_long) {
					case 0:
						fprintf(fp, fmt,
							va_arg(va, int));
						break;
					case 1:
						fprintf(fp, fmt,
							va_arg(va, long));
						break;
					case 2:
						fprintf(fp, fmt,
						    va_arg(va, long long));
						break;
					case 3:
						fprintf(fp, fmt,
						    va_arg(va, size_t));
						break;
					}
					++n;
					is_done = 1;
					break;
				case 'c':
				        fprintf(fp, "%c", va_arg(va, int));
					++n;
					is_done = 1;
					break;
				case 's':
					/*
					 * String
					 */
					s = kvm_string(va_arg(va, char *), &strctx);
					fwrite(s, 1, strlen(s), fp);
					++n;
					is_done = 1;
					break;
				case 'f':
					/*
					 * Floating
					 */
					fprintf(fp, fmt,
						va_arg(va, double));
					++n;
					break;
				case 'j':
					is_long = 2;
					break;
				case 'z':
					is_long = 3;
					break;
				case 'l':
					if (is_long)
						is_long = 2;
					else
						is_long = 1;
					break;
				case '.':
				case '-':
				case '+':
				case '0':
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
				case '8':
				case '9':
					break;
				default:
					is_done = 1;
					break;
				}
				if (is_done)
					break;
				++n;
			}
		} else {
			fmt[n] = 0;
			fprintf(fp, fmt, NULL);
		}
		ctl += n;
	}
}

static void
usage(void)
{
	fprintf(stderr, "usage: ktrdump [-acfilnpqrstx] [-A factor] "
			"[-N execfile] [-M corefile] [-o outfile]\n");
	exit(1);
}

enum argument_class {
	ARGCLASS_NONE,
	ARGCLASS_INTEGER,
	ARGCLASS_FP,
	ARGCLASS_MEMORY,
	ARGCLASS_ERR,
};
static size_t
conversion_size(const char *fmt, enum argument_class *argclass)
{
	const char *p;
	size_t convsize, intsz;

	*argclass = ARGCLASS_ERR;
	if (fmt[0] != '%')
		return -1;

	convsize = -1;
	for (p = fmt + 1; p[0]; ++p) {
		int again = 0;
		/*
		 * Eat flags. Notice this will accept duplicate
		 * flags.
		 */
		switch (p[0]) {
		case '#':
		case '0':
		case '-':
		case ' ':
		case '+':
		case '\'':
			again = !0;
			break;
		}
		if (!again)
			break;
	}
	/* Eat minimum field width, if any */
	for (; isdigit(p[0]); ++p)
			;
	if (p[0] == '.')
		++p;
	/* Eat precision, if any */
	for (; isdigit(p[0]); ++p)
		;
	intsz = 0;
	switch (p[0]) {
	case 'h':
		if (p[1] == 'h') {
			++p;
			intsz = sizeof(char);
		} else {
			intsz = sizeof(short);
		}
		break;
	case 'l':
		if (p[1] == 'l') {
			++p;
			intsz = sizeof(long long);
		} else {
			intsz = sizeof(long);
		}
		break;
	case 'j':
		intsz = sizeof(intmax_t);
		break;
	case 't':
		intsz = sizeof(ptrdiff_t);
		break;
	case 'z':
		intsz = sizeof(size_t);
		break;
	default:
		p--;	/* Anticipate the ++p that follows. Yes, I know. Eeek. */
		break;
	}
	if (intsz == 0)
		intsz = sizeof(int);
	++p;

	switch (p[0]) {
	case 'c':
		/* for %c, we only store 1 byte in the ktr entry */
		convsize = sizeof(char);
		*argclass = ARGCLASS_INTEGER;
		break;
	case 'd':
	case 'i':
	case 'o':
	case 'u':
	case 'x':
	case 'X':
		convsize = intsz;
		*argclass = ARGCLASS_INTEGER;
		break;
	case 'p':
		convsize = sizeof(void *);
		*argclass = ARGCLASS_INTEGER;
		break;
	case 'f':
		if (p[-1] == 'l')
			convsize = sizeof(double);
		else
			convsize = sizeof(float);
		break;
		*argclass = ARGCLASS_FP;
	case 's':
		convsize = sizeof(char *);
		*argclass = ARGCLASS_INTEGER;
		break;
	case '%':
		convsize = 0;
		*argclass = ARGCLASS_NONE;
		break;
	default:
		fprintf(stderr, "Unknown conversion specifier %c "
			"in fmt starting with %s", p[0], fmt - 1);
		return -2;
	}
	return convsize;
}

#ifdef __x86_64__
static int
va_list_push_integral(struct my_va_list *valist, void *val, size_t valsize,
		     size_t *stacksize)
{
	uint64_t r;

	switch (valsize) {
	case 1:
		r = *(uint8_t *)val; break;
	case 2:
		r = *(uint32_t *)val; break;
	case 4:
		r = (*(uint32_t *)val); break;
	case 8:
		r = *(uint64_t *)val; break;
	default:
		err(1, "WTF\n");
	}
	/* we always need to push the full 8 bytes */
	if ((valist->gp_offset + valsize) <= 48) {	/* got a free reg */

		memcpy(((char *)valist->reg_save_area + valist->gp_offset),
		       &r, sizeof(r));
		valist->gp_offset += sizeof(r);
		return 0;
	}
	/* push to "stack" */
	if (!(valist->overflow_arg_area = realloc(valist->overflow_arg_area,
						  *stacksize + sizeof(r))))
		return -1;
	/*
	 * Keep a pointer to the start of the allocated memory block so
	 * we can free it later. We need to update it after every realloc().
	 */
	valist->overflow_arg_area_save = valist->overflow_arg_area;
	memcpy((char *)valist->overflow_arg_area + *stacksize, &r, sizeof(r));
	*stacksize += sizeof(r);
	return 0;
}

static void
va_list_rewind(struct my_va_list *valist)
{
	valist->gp_offset = 0;
}

static void
va_list_cleanup(machine_va_list *_valist)
{
	machine_va_list valist;
	if (!_valist || !*_valist)
		return;
	valist = *_valist;
	if (valist->reg_save_area)
		free(valist->reg_save_area);
	if (valist->overflow_arg_area_save)
		free(valist->overflow_arg_area_save);
	free(valist);
}

static int
va_list_from_blob(machine_va_list *_valist, const char *fmt, char *blob, size_t blobsize)
{
	machine_va_list valist;
	struct reg_save_area *regs;
	const char *f;
	size_t sz;

	if (!(valist = malloc(sizeof(*valist))))
		return -1;
	if (!(regs = malloc(sizeof(*regs))))
		goto free_valist;
	*valist = (struct my_va_list) {
		.gp_offset = 0,
		.fp_offset = 0,
		.overflow_arg_area = NULL,
		.reg_save_area = regs,
		.overflow_arg_area_save = NULL,
	};
	enum argument_class argclass;
	size_t stacksize = 0;

	for (f = fmt; *f != '\0'; ++f) {
		if (*f != '%')
			continue;
		sz = conversion_size(f, &argclass);
		if (argclass == ARGCLASS_INTEGER) {
			if (blobsize < sz) {
				fprintf(stderr, "not enough data available "
					"for format: %s", fmt);
				goto free_areas;
			}
			if (va_list_push_integral(valist, blob, sz, &stacksize))
				goto free_areas;
			blob += sz;
			blobsize -= sz;
		} else if (argclass != ARGCLASS_NONE)
			goto free_areas;
		/* walk past the '%' */
		++f;
	}
	if (blobsize) {
		fprintf(stderr, "Couldn't consume all data for format %s "
			"(%zd bytes left over)\n", fmt, blobsize);
		goto free_areas;
	}
	va_list_rewind(valist);
	*_valist = valist;
	return 0;
free_areas:
	if (valist->reg_save_area)
		free(valist->reg_save_area);
	if (valist->overflow_arg_area_save)
		free(valist->overflow_arg_area_save);
free_valist:
	free(valist);
	*_valist = NULL;
	return -1;
}
#elif __i386__

static void
va_list_cleanup(machine_va_list *valist)
{
	if (*valist)
		free(*valist);
}

static int
va_list_from_blob(machine_va_list *valist, const char *fmt, char *blob, size_t blobsize)
{
	const char *f;
	char *n;
	size_t bytes, sz;
	enum argument_class argclass;

	n = NULL;
	bytes = 0;
	for (f = fmt; *f != '\0'; ++f) {
		if (*f != '%')
			continue;
		sz = conversion_size(f, &argclass);
		if (blobsize < sz) {
			fprintf(stderr, "not enough data available "
				"for format: %s", fmt);
			goto free_va;
		}
		if ((argclass == ARGCLASS_INTEGER) && (sz < 4)) {
			int i = -1;	/* do C integer promotion */
			if (sz == 1)
				i = *(char *)blob;
			else
				i = *(short *)blob;
			if (!(n = realloc(n, bytes + 4)))
				goto free_va;
			memcpy(n + bytes, &i, sizeof(i));
			bytes += 4;
		} else {
			if (!(n = realloc(n, bytes + sz)))
				goto free_va;
			memcpy(n + bytes, blob, sz);
			bytes += sz;
		}
		blob += sz;
		blobsize -= sz;

	}
	if (blobsize) {
		fprintf(stderr, "Couldn't consume all data for format %s "
			"(%zd bytes left over)\n", fmt, blobsize);
		goto free_va;
	}
	*valist = n;
	return 0;
free_va:
	if (n)
		free(n);
	*valist = NULL;
	return -1;
}

#else
#error "Don't know how to get a va_list on this platform"
#endif

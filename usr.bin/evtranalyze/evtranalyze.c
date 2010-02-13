/*
 * Copyright (c) 2009, 2010 Aggelos Economopoulos.  All rights reserved.
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
 */

#include <assert.h>
#include <err.h>
#include <libgen.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <evtr.h>
#include "xml.h"
#include "svg.h"

enum {
	NR_TOP_THREADS = 5,
};

struct rows {
	double row_increment;
	double row_off;
};

#define CMD_PROTO(name)	\
	static int cmd_ ## name(int, char **)

CMD_PROTO(show);
CMD_PROTO(svg);

struct command {
	const char *name;
	int (*func)(int argc, char **argv);
} commands[] = {
	{
		.name = "show",
		.func = &cmd_show,
	},
	{
		.name = "svg",
		.func = &cmd_svg,
	},
	{
		.name = NULL,
	},
};

evtr_t evtr;
char *opt_infile;
static int evtranalyze_debug;

#define printd(...)					\
	do {						\
		if (evtranalyze_debug) {		\
			fprintf(stderr, __VA_ARGS__);	\
		}					\
	} while (0)

static
void
usage(void)
{
	fprintf(stderr, "bad usage :P\n");
	exit(2);
}

static
void
rows_init(struct rows *rows, int n, double height, double perc)
{
	double row_h;
	rows->row_increment = height / n;
	/* actual row height */
        row_h = perc * rows->row_increment;
	rows->row_off = (rows->row_increment - row_h) / 2.0;
	assert(!isnan(rows->row_increment));
	assert(!isnan(rows->row_off));
}

static
void
rows_n(struct rows *rows, int n, double *y, double *height)
{
	*y = n * rows->row_increment + rows->row_off;
	*height = rows->row_increment - 2 * rows->row_off;
}

/*
 * Which fontsize to use so that the string fits in the
 * given rect.
 */
static
double
fontsize_for_rect(double width, double height, int textlen)
{
	double wpc, maxh;
	/*
	 * We start with a font size equal to the height
	 * of the rectangle and round down so that we only
	 * use a limited number of sizes.
	 *
	 * For a rectangle width comparable to the height,
	 * the text might extend outside of the rectangle.
	 * In that case we need to limit it.
	 */
	/* available width per character */
	wpc = width / textlen;
	/*
	 * Assuming a rough hight/width ratio for characters,
	 * calculate the available height and round it down
	 * just to be on the safe side.
	 */
#define GLYPH_HIGHT_TO_WIDTH 1.5
	maxh = GLYPH_HIGHT_TO_WIDTH * wpc * 0.9;
	if (height > maxh) {
		height = maxh;
	} else if (height < 0.01) {
		height = 0.01;
	} else {
		/* rounding (XXX: make cheaper)*/
		height = log(height);
		height = round(height);
		height = exp(height);
	}
	return height;
}

struct pass_hook {
	void (*pre)(void *);
	void (*event)(void *, evtr_event_t);
	void (*post)(void *);
	void *data;
	struct evtr_filter *filts;
	int nfilts;
};

struct thread_info {
	uint64_t runtime;
};

struct td_switch_ctx {
	svg_document_t svg;
	struct rows *cpu_rows;
	struct rows *thread_rows;
	uint64_t interval_start, interval_end;
	uint64_t first_ts, last_ts;
	double width;
	double xscale;	/* scale factor applied to x */
	svg_rect_t cpu_sw_rect;
	svg_rect_t thread_rect;
	svg_rect_t inactive_rect;
	svg_text_t thread_label;
	struct cpu_table {
		struct cpu *cpus;
		int ncpus;
	} cputab;
	struct evtr_thread **top_threads;
	int nr_top_threads;
	double thread_rows_yoff;
};

struct cpu {
	struct evtr_thread *td;
	int i;		/* cpu index */
	uint64_t ts;	/* time cpu switched to td */
	uint64_t first_ts, last_ts;
	double freq;
};

static
void
do_pass(struct pass_hook *hooks, int nhooks)
{
	struct evtr_filter *filts = NULL;
	int nfilts = 0, i;
	struct evtr_query *q;
	struct evtr_event ev;

	for (i = 0; i < nhooks; ++i) {
		struct pass_hook *h = &hooks[i];
		if (h->pre)
			h->pre(h->data);
		if (h->nfilts > 0) {
			filts = realloc(filts, (nfilts + h->nfilts) *
					sizeof(struct evtr_filter));
			if (!filts)
				err(1, "Out of memory");
			memcpy(filts + nfilts, h->filts,
			       h->nfilts * sizeof(struct evtr_filter));
			nfilts += h->nfilts;
		}
	}
	q = evtr_query_init(evtr, filts, nfilts);
	if (!q)
		err(1, "Can't initialize query\n");
	while(!evtr_query_next(q, &ev)) {
		for (i = 0; i < nhooks; ++i) {
			if (hooks[i].event)
				hooks[i].event(hooks[i].data, &ev);
		}
	}
	if (evtr_error(evtr)) {
		err(1, evtr_errmsg(evtr));
	}
	evtr_query_destroy(q);

	for (i = 0; i < nhooks; ++i) {
		if (hooks[i].post)
			hooks[i].post(hooks[i].data);
	}
	if (evtr_rewind(evtr))
		err(1, "Can't rewind event stream\n");
}

static
void
draw_thread_run(struct td_switch_ctx *ctx, struct cpu *c, evtr_event_t ev, int row)
{
	double x, w, y, height;
	w = (ev->ts - c->ts) * ctx->xscale;
	x = (ev->ts - ctx->first_ts) * ctx->xscale;
	rows_n(ctx->thread_rows, row, &y, &height);
	svg_rect_draw(ctx->svg, ctx->thread_rect, x - w,
		      y + ctx->thread_rows_yoff, w, height);
}

static
void
draw_ctx_switch(struct td_switch_ctx *ctx, struct cpu *c, evtr_event_t ev)
{
	struct svg_transform textrot;
	char comm[100];
	double x, w, fs, y, height;
	int textlen;

	assert(ctx->xscale > 0.0);
	if (!c->ts)
		return;
	/* distance to previous context switch */
	w = (ev->ts - c->ts) * ctx->xscale;
	x = (ev->ts - ctx->first_ts) * ctx->xscale;
	if ((x - w) < 0) {
		fprintf(stderr, "(%llu - %llu) * %.20lf\n", ev->ts,
			ctx->first_ts, ctx->xscale);
		abort();
	}

	rows_n(ctx->cpu_rows, c->i, &y, &height);
	assert(!isnan(y));
	assert(!isnan(height));

	svg_rect_draw(ctx->svg, ctx->cpu_sw_rect, x - w, y, w, height);

	/*
	 * Draw the text label describing the thread we
	 * switched out of.
	 */
	textrot.tx = x - w;
	textrot.ty = y;
	textrot.sx = 1.0;
	textrot.sy = 1.0;
	textrot.rot = 90.0;
	textlen = snprintf(comm, sizeof(comm) - 1, "%s (%p)",
			   c->td ? c->td->comm : "unknown",
				 c->td ? c->td->id: NULL);
	if (textlen > (int)sizeof(comm))
		textlen = sizeof(comm) - 1;
	comm[sizeof(comm) - 1] = '\0';
	/*
	 * Note the width and hight are exchanged because
	 * the bounding rectangle is rotated by 90 degrees.
	 */
	fs = fontsize_for_rect(height, w, textlen);
	svg_text_draw(ctx->svg, ctx->thread_label, &textrot, comm,
		      fs);
}


/*
 * The stats for ntd have changed, update ->top_threads
 */
static
void
top_threads_update(struct td_switch_ctx *ctx, struct evtr_thread *ntd)
{
	struct thread_info *tdi = ntd->userdata;
	int i, j;
	for (i = 0; i < ctx->nr_top_threads; ++i) {
		struct evtr_thread *td = ctx->top_threads[i];
		if (td == ntd) {
			/*
			 * ntd is already in top_threads and it is at
			 * the correct ranking
			 */
			break;
		}
		if (!td) {
			/* empty slot -- just insert our thread */
			ctx->top_threads[i] = ntd;
			break;
		}
		if (((struct thread_info *)td->userdata)->runtime >=
		    tdi->runtime) {
			/* this thread ranks higher than we do. Move on */
			continue;
		}
		/*
		 * OK, we've found the first thread that we outrank, so we
		 * need to replace it w/ our thread.
		 */
		td = ntd;	/* td holds the thread we will insert next */
		for (j = i + 1; j < ctx->nr_top_threads; ++j, ++i) {
			struct evtr_thread *tmp;

			/* tmp holds the thread we replace */
			tmp = ctx->top_threads[j];
			ctx->top_threads[j] = td;
			if (tmp == ntd) {
				/*
				 * Our thread was already in the top list,
				 * and we just removed the second instance.
				 * Nothing more to do.
				 */
				break;
			}
			td = tmp;
		}
		break;
	}
}

static
void
ctxsw_prepare_event(void *_ctx, evtr_event_t ev)
{
	struct td_switch_ctx *ctx = _ctx;
	struct cpu *c, *cpus = ctx->cputab.cpus;
	struct thread_info *tdi;

	(void)evtr;
	printd("test1 (%llu:%llu) : %llu\n", ctx->interval_start,
	       ctx->interval_end, ev->ts);
	if ((ev->ts > ctx->interval_end) ||
	    (ev->ts < ctx->interval_start))
		return;
	printd("PREPEV on %d\n", ev->cpu);

	/* update first/last timestamps */
	c = &cpus[ev->cpu];
	if (!c->first_ts) {
		c->first_ts = ev->ts;
		printd("setting first_ts (%d) = %llu\n", ev->cpu,
		       c->first_ts);
	}
	c->last_ts = ev->ts;
	/*
	 * c->td can be null when this is the first ctxsw event we
	 * observe for a cpu
	 */
	if (c->td) {
		/* update thread stats */
		if (!c->td->userdata) {
			if (!(tdi = malloc(sizeof(struct thread_info))))
				err(1, "Out of memory");
			c->td->userdata = tdi;
			tdi->runtime = 0;
		}
		tdi = c->td->userdata;
		tdi->runtime += ev->ts - c->ts;
		top_threads_update(ctx, c->td);
	}

	/* Notice that ev->td is the new thread for ctxsw events */
	c->td = ev->td;
	c->ts = ev->ts;
}

static
void
ctxsw_prepare_post(void *_ctx)
{
	struct td_switch_ctx *ctx = _ctx;
	struct cpu *cpus = ctx->cputab.cpus;
	int i;

	(void)evtr;
	ctx->first_ts = -1;
	ctx->last_ts = 0;
	printd("first_ts[0] = %llu\n",cpus[0].first_ts);
	for (i = 0; i < ctx->cputab.ncpus; ++i) {
		printd("first_ts[%d] = %llu\n", i, cpus[i].first_ts);
		if (cpus[i].first_ts && (cpus[i].first_ts < ctx->first_ts))
			ctx->first_ts = cpus[i].first_ts;
		if (cpus[i].last_ts && (cpus[i].last_ts > ctx->last_ts))
			ctx->last_ts = cpus[i].last_ts;
		cpus[i].td = NULL;
		cpus[i].ts = 0;
	}
}

static
void
ctxsw_draw_pre(void *_ctx)
{
	struct td_switch_ctx *ctx = _ctx;
	struct svg_transform textrot;
	char comm[100];
	double y, height, fs;
	int i, textlen;
	struct evtr_thread *td;

	textrot.tx = 0.0 - 0.2;	/* XXX */
	textrot.sx = 1.0;
	textrot.sy = 1.0;
	textrot.rot = 270.0;

	for (i = 0; i < ctx->nr_top_threads; ++i) {
		td = ctx->top_threads[i];
		rows_n(ctx->thread_rows, i, &y, &height);
		svg_rect_draw(ctx->svg, ctx->inactive_rect, 0.0,
			      y + ctx->thread_rows_yoff, ctx->width, height);
		textlen = snprintf(comm, sizeof(comm) - 1, "%s (%p)",
				   td->comm, td->id);
		if (textlen > (int)sizeof(comm))
			textlen = sizeof(comm) - 1;
		comm[sizeof(comm) - 1] = '\0';
		fs = fontsize_for_rect(height, 100.0, textlen);

		textrot.ty = y + ctx->thread_rows_yoff + height;
		svg_text_draw(ctx->svg, ctx->thread_label, &textrot,
			      comm, fs);
	}
}

static
void
ctxsw_draw_event(void *_ctx, evtr_event_t ev)
{
	struct td_switch_ctx *ctx = _ctx;
	struct cpu *c = &ctx->cputab.cpus[ev->cpu];
	int i;

	/*
	 * ctx->last_ts can be 0 if there were no events
	 * in the specified interval, in which case
	 * ctx->first_ts is invalid too.
	 */
	assert(!ctx->last_ts || (ev->ts >= ctx->first_ts));
	printd("test2 (%llu:%llu) : %llu\n", ctx->interval_start,
	       ctx->interval_end, ev->ts);
	if ((ev->ts > ctx->interval_end) ||
	    (ev->ts < ctx->interval_start))
		return;
	printd("DRAWEV %d\n", ev->cpu);
	if (c->td != ev->td) {	/* thread switch (or preemption) */
		draw_ctx_switch(ctx, c, ev);
		/* XXX: this is silly */
		for (i = 0; i < ctx->nr_top_threads; ++i) {
			if (ctx->top_threads[i] == c->td) {
				draw_thread_run(ctx, c, ev, i);
				break;
			}
		}
		c->td = ev->td;
		c->ts = ev->ts;
	}
}

static
void
cputab_init(struct cpu_table *ct)
{
	struct cpu *cpus;
	double *freqs;
	int i;

	if ((ct->ncpus = evtr_ncpus(evtr)) <= 0)
		err(1, "No cpu information!\n");
	printd("evtranalyze: ncpus %d\n", ct->ncpus);
	if (!(ct->cpus = malloc(sizeof(struct cpu) * ct->ncpus))) {
		err(1, "Can't allocate memory\n");
	}
	cpus = ct->cpus;
	if (!(freqs = malloc(sizeof(double) * ct->ncpus))) {
		err(1, "Can't allocate memory\n");
	}
	if ((i = evtr_cpufreqs(evtr, freqs))) {
		warnc(i, "Can't get cpu frequencies\n");
		for (i = 0; i < ct->ncpus; ++i) {
			freqs[i] = -1.0;
		}
	}

	/* initialize cpu array */
	for (i = 0; i < ct->ncpus; ++i) {
		cpus[i].td = NULL;
		cpus[i].ts = 0;
		cpus[i].i = i;
		cpus[i].first_ts = 0;
		cpus[i].last_ts = 0;
		cpus[i].freq = freqs[i];
	}
	free(freqs);
}


static
int
cmd_svg(int argc, char **argv)
{
	svg_document_t svg;
	int ch;
	double height, width;
	struct rows cpu_rows, thread_rows;
	struct td_switch_ctx td_ctx;
	struct evtr_filter ctxsw_filts[2] = {
		{
			.flags = 0,
			.cpu = -1,
		},
		{
			.flags = 0,
			.cpu = -1,
		},
	};
	struct pass_hook ctxsw_prepare = {
		.pre = NULL,
		.event = ctxsw_prepare_event,
		.post = ctxsw_prepare_post,
		.data = &td_ctx,
		.filts = ctxsw_filts,
		.nfilts = sizeof(ctxsw_filts)/sizeof(ctxsw_filts[0]),
	}, ctxsw_draw = {
		.pre = ctxsw_draw_pre,
		.event = ctxsw_draw_event,
		.post = NULL,
		.data = &td_ctx,
		.filts = ctxsw_filts,
		.nfilts = sizeof(ctxsw_filts)/sizeof(ctxsw_filts[0]),
	};

	/*
	 * We are interested in thread switch and preemption
	 * events, but we don't use the data directly. Instead
	 * we rely on ev->td.
	 */
	ctxsw_filts[0].fmt = "sw  %p > %p";
	ctxsw_filts[1].fmt = "pre %p > %p";
	td_ctx.interval_start = 0;
	td_ctx.interval_end = -1;	/* i.e. no interval given */
	td_ctx.nr_top_threads = NR_TOP_THREADS;

	printd("argc: %d, argv[0] = %s\n", argc, argv[0] ? argv[0] : "NULL");
	optind = 0;
	optreset = 1;
	while ((ch = getopt(argc, argv, "i:")) != -1) {
		switch (ch) {
		case 'i':
			if (sscanf(optarg, "%llu:%llu", &td_ctx.interval_start,
				   &td_ctx.interval_end) != 2) {
				usage();
			}
			break;
		default:
			usage();
		}

	}
	argc -= optind;
	argv += optind;

	height = 200.0;
	width = 700.0;
	td_ctx.width = width;

	cputab_init(&td_ctx.cputab);
	if (!(td_ctx.top_threads = calloc(td_ctx.nr_top_threads, sizeof(struct evtr_thread *))))
		err(1, "Can't allocate memory\n");
	if (!(svg = svg_document_create("output.svg")))
		err(1, "Can't open svg document\n");

	/*
	 * Create rectangles to use for output.
	 */
	if (!(td_ctx.cpu_sw_rect = svg_rect_new("generic")))
		err(1, "Can't create rectangle\n");
	if (!(td_ctx.thread_rect = svg_rect_new("thread")))
		err(1, "Can't create rectangle\n");
	if (!(td_ctx.inactive_rect = svg_rect_new("inactive")))
		err(1, "Can't create rectangle\n");
	/* text for thread names */
	if (!(td_ctx.thread_label = svg_text_new("generic")))
		err(1, "Can't create text\n");
	rows_init(&cpu_rows, td_ctx.cputab.ncpus, height, 0.9);
	td_ctx.svg = svg;
	td_ctx.xscale = -1.0;
	td_ctx.cpu_rows = &cpu_rows;

	do_pass(&ctxsw_prepare, 1);
	td_ctx.thread_rows_yoff = height;
	td_ctx.thread_rows = &thread_rows;
	rows_init(td_ctx.thread_rows, td_ctx.nr_top_threads, 300, 0.9);
	td_ctx.xscale = width / (td_ctx.last_ts - td_ctx.first_ts);
	printd("first %llu, last %llu, xscale %lf\n", td_ctx.first_ts,
	       td_ctx.last_ts, td_ctx.xscale);

	do_pass(&ctxsw_draw, 1);

	svg_document_close(svg);
	return 0;
}

static
int
cmd_show(int argc, char **argv)
{
	struct evtr_event ev;
	struct evtr_query *q;
	struct evtr_filter filt;
	struct cpu_table cputab;
	double freq;
	int ch;
	uint64_t last_ts = 0;

	cputab_init(&cputab);
	/*
	 * Assume all cores run on the same frequency
	 * for now. There's no reason to complicate
	 * things unless we can detect frequency change
	 * events as well.
	 *
	 * Note that the code is very simplistic and will
	 * produce garbage if the kernel doesn't fixup
	 * the timestamps for cores running with different
	 * frequencies.
	 */
	freq = cputab.cpus[0].freq;
	freq /= 1000000;	/* we want to print out usecs */
	printd("using freq = %lf\n", freq);
	filt.fmt = NULL;
	optind = 0;
	optreset = 1;
	while ((ch = getopt(argc, argv, "f:")) != -1) {
		switch (ch) {
		case 'f':
			filt.fmt = optarg;
			break;
		}
	}
	filt.flags = 0;
	filt.cpu = -1;
	printd("fmt = %s\n", filt.fmt ? filt.fmt : "NULL");
	q = evtr_query_init(evtr, &filt, 1);
	if (!q)
		err(1, "Can't initialize query\n");
	while(!evtr_query_next(q, &ev)) {
		char buf[1024];

		if (!last_ts)
			last_ts = ev.ts;
		if (freq < 0.0) {
			printf("%s\t%llu cycles\t[%.3d]\t%s:%d",
			       ev.td ? ev.td->comm : "unknown",
			       ev.ts - last_ts, ev.cpu,
			       basename(ev.file), ev.line);
		} else {
			printf("%s\t%.3lf usecs\t[%.3d]\t%s:%d",
			       ev.td ? ev.td->comm : "unknown",
			       (ev.ts - last_ts) / freq, ev.cpu,
			       basename(ev.file), ev.line);
		}
		if (ev.fmt) {
			evtr_event_data(&ev, buf, sizeof(buf));
			printf(" !\t%s\n", buf);
		} else {
			printf("\n");
		}
		last_ts = ev.ts;
	}
	if (evtr_error(evtr)) {
		err(1, evtr_errmsg(evtr));
	}
	evtr_query_destroy(q);
	return 0;
}

int
main(int argc, char **argv)
{
	int ch;
	FILE *inf;
	struct command *cmd;

	while ((ch = getopt(argc, argv, "f:D:")) != -1) {
		switch (ch) {
		case 'f':
			opt_infile = optarg;
			break;
		case 'D':
			evtranalyze_debug = atoi(optarg);
			evtr_set_debug(evtranalyze_debug);
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0) {
		err(2, "need to specify a command\n");
	}
	if (!opt_infile || !strcmp(opt_infile, "-")) {
		inf = stdin;
	} else {
		inf = fopen(opt_infile, "r");
		if (!inf) {
			err(2, "Can't open input file\n");
		}
	}

	if (!(evtr = evtr_open_read(inf))) {
		err(1, "Can't open evtr stream\n");
	}


	for (cmd = commands; cmd->name != NULL; ++cmd) {
		if (strcmp(argv[0], cmd->name))
			continue;
		cmd->func(argc, argv);
		break;
	}
	if (!cmd->name) {
		err(2, "no such command: %s\n", argv[0]);
	}
		
	evtr_close(evtr);
	return 0;
}

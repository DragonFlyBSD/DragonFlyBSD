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
#include <ctype.h>
#include <err.h>
#include <inttypes.h>
#include <libgen.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <evtr.h>
#include "xml.h"
#include "svg.h"
#include "plotter.h"
#include "trivial.h"

enum {
	NR_TOP_THREADS = 5,
	NR_BUCKETS = 1021,
};

struct rows {
	double row_increment;
	double row_off;
};

#define CMD_PROTO(name)	\
	static int cmd_ ## name(int, char **)

CMD_PROTO(show);
CMD_PROTO(svg);
CMD_PROTO(stats);
CMD_PROTO(summary);

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
		.name = "stats",
		.func = &cmd_stats,
	},
	{
		.name = "summary",
		.func = &cmd_summary,
	},
	{
		.name = NULL,
	},
};

evtr_t evtr;
static char *opt_infile;
unsigned evtranalyze_debug;

static
void
printd_set_flags(const char *str, unsigned int *flags)
{
	/*
	 * This is suboptimal as we don't detect
	 * invalid flags.
	 */
	for (; *str; ++str) {
		if ('A' == *str) {
			*flags = -1;
			return;
		}
		if (!islower(*str))
			err(2, "invalid debug flag %c\n", *str);
		*flags |= 1 << (*str - 'a');
	}
}

struct hashentry {
	uintptr_t key;
	uintptr_t val;
	struct hashentry *next;
};

struct hashtab {
	struct hashentry *buckets[NR_BUCKETS];
	uintptr_t (*hashfunc)(uintptr_t);
	uintptr_t (*cmpfunc)(uintptr_t, uintptr_t);
};

static int
ehash_find(const struct hashtab *tab, uintptr_t key, uintptr_t *val)
{
	struct hashentry *ent;

	for(ent = tab->buckets[tab->hashfunc(key)];
	    ent && tab->cmpfunc(ent->key, key);
	    ent = ent->next);

	if (!ent)
		return !0;
	*val = ent->val;
	return 0;
}

static struct hashentry *
ehash_insert(struct hashtab *tab, uintptr_t key, uintptr_t val)
{
	struct hashentry *ent;
	int hsh;

	if (!(ent = malloc(sizeof(*ent)))) {
		fprintf(stderr, "out of memory\n");
		return NULL;
	}
	hsh = tab->hashfunc(key);
	ent->next = tab->buckets[hsh];
	ent->key = key;
	ent->val = val;
	tab->buckets[hsh] = ent;
	return ent;
}
static
int
ehash_delete(struct hashtab *tab, uintptr_t key)
{
	struct hashentry *ent, *prev;

	prev = NULL;
	for(ent = tab->buckets[tab->hashfunc(key)];
	    ent && tab->cmpfunc(ent->key, key);
	    prev = ent, ent = ent->next);
	if (!ent)
		return !0;
	if (prev)
		prev->next = ent->next;
	else
		tab->buckets[tab->hashfunc(key)] = ent->next;
	free(ent);
	return 0;
}

static
uintptr_t
cmpfunc_pointer(uintptr_t a, uintptr_t b)
{
	return b - a;
}

static
uintptr_t
hashfunc_pointer(uintptr_t p)
{
	return p % NR_BUCKETS;
}

static
uintptr_t
hashfunc_string(uintptr_t p)
{
	const char *str = (char *)p;
        unsigned long hash = 5381;
        int c;

        while ((c = *str++))
            hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
	return hash  % NR_BUCKETS;
}

static struct hashtab *
ehash_new(void)
{
	struct hashtab *tab;
	if (!(tab = calloc(sizeof(struct hashtab), 1)))
		return tab;
	tab->hashfunc = &hashfunc_pointer;
	tab->cmpfunc = &cmpfunc_pointer;
	return tab;
}

/* returns 0 if equal */
static
int
cmp_vals(evtr_variable_value_t a, evtr_variable_value_t b)
{
	if (a->type != b->type)
		return !0;
	switch (a->type) {
	case EVTR_VAL_NIL:
		return 0;
	case EVTR_VAL_INT:
		return !(a->num == b->num);
	case EVTR_VAL_STR:
		return strcmp(a->str, b->str);
	case EVTR_VAL_HASH:
		return !0;	/* come on! */
	case EVTR_VAL_CTOR:
		err(3, "not implemented");
	}
	err(3, "can't get here");
}

static
uintptr_t
cmpfunc_ctor(uintptr_t _a, uintptr_t _b)
{
	evtr_variable_value_t vala, valb;
	vala = (evtr_variable_value_t)_a;
	valb = (evtr_variable_value_t)_b;
	if (strcmp(vala->ctor.name, valb->ctor.name))
		return !0;
	vala = TAILQ_FIRST(&vala->ctor.args);
	valb = TAILQ_FIRST(&valb->ctor.args);
	for (;;) {
		if (!vala && !valb)
			return 0;
		if ((vala && !valb) || (valb && !vala))
			return !0;
		if (cmp_vals(vala, valb))
			return !0;
		vala = TAILQ_NEXT(vala, link);
		valb = TAILQ_NEXT(valb, link);
	}
}

static
uintptr_t
hashfunc_ctor(uintptr_t _p)
{
	evtr_variable_value_t val, ctor_val = (evtr_variable_value_t)_p;
	char buf[1024], *p = &buf[0];
	size_t len;

	p = buf;
	assert(ctor_val->type == EVTR_VAL_CTOR);
	len = strlcpy(buf, ctor_val->ctor.name, sizeof(buf));
	if (len >= sizeof(buf))
		goto done;

	TAILQ_FOREACH(val, &ctor_val->ctor.args, link) {
		switch (val->type) {
		case EVTR_VAL_NIL:
			assert(!"can't happen");
			break;
		case EVTR_VAL_INT:
			len += snprintf(p + len, sizeof(buf) - len - 1,
					"%jd", val->num);
			break;
		case EVTR_VAL_STR:
			len = strlcat(p, val->str, sizeof(buf));
			break;
		case EVTR_VAL_HASH:
			break;	/* come on! */
		case EVTR_VAL_CTOR:
			err(3, "not implemented");
		}
		if (len >= (sizeof(buf) - 1))
			break;
	}
done:
	buf[sizeof(buf) - 1] = '\0';
	return hashfunc_string((uintptr_t)buf);
}

typedef struct vector {
	uintmax_t *vals;
	int used;
	int allocated;
} *vector_t;

static vector_t
vector_new(void)
{
	vector_t v;
	if (!(v = malloc(sizeof(*v))))
		return v;
	v->allocated = 2;
	if (!(v->vals = malloc(v->allocated * sizeof(v->vals[0])))) {
		free(v);
		return NULL;
	}
	v->allocated = 
	v->used = 0;
	return v;
}

static
void
vector_push(vector_t v, uintmax_t val)
{
	uintmax_t *tmp;
	if (v->used == v->allocated) {
		tmp = realloc(v->vals, 2 * v->allocated * sizeof(v->vals[0]));
		if (!tmp)
			err(1, "out of memory");
		v->vals = tmp;
		v->allocated *= 2;
	}
	v->vals[v->used++] = val;
}

static
void
vector_destroy(vector_t v)
{
	free(v->vals);
	free(v);
}

static
int
vector_nelems(vector_t v)
{
	return v->used;
}

#define vector_foreach(v, val, i)					\
	for (i = 0, val = v->vals[0]; i < v->used; val = v->vals[++i])

static
double
stddev(vector_t v, double avg)
{
	uintmax_t val;
	int i;
	double diff, sqr_sum = 0.0;

	if (vector_nelems(v) < 2)
		return 1 / 0.0;
	vector_foreach(v, val, i) {
		diff = val - avg;
		sqr_sum += diff * diff;
	}
	return sqrt(sqr_sum / (vector_nelems(v) - 1));
}

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

struct ts_interval {
	uint64_t start;
	uint64_t end;
};

struct td_switch_ctx {
	svg_document_t svg;
	struct rows *cpu_rows;
	struct rows *thread_rows;
	/* which events the user cares about */
	struct ts_interval interval;
	/* first/last event timestamps on any cpu */
	struct ts_interval firstlast;
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
	/* timestamp for first/last event on this cpu */
	struct ts_interval firstlast;
	double freq;
	uintmax_t evcnt;
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
	if (evtr_query_error(q)) {
		err(1, "%s", evtr_query_errmsg(q));
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
	x = (ev->ts - ctx->firstlast.start) * ctx->xscale;
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
	x = (ev->ts - ctx->firstlast.start) * ctx->xscale;
	if ((x - w) < 0) {
		fprintf(stderr, "(%ju - %ju) * %.20lf\n",
			(uintmax_t)ev->ts,
			(uintmax_t)ctx->firstlast.start, ctx->xscale);
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
	printd(INTV, "test1 (%ju:%ju) : %ju\n",
	       (uintmax_t)ctx->interval.start,
	       (uintmax_t)ctx->interval.end,
	       (uintmax_t)ev->ts);
	if ((ev->ts > ctx->interval.end) ||
	    (ev->ts < ctx->interval.start))
		return;
	printd(INTV, "PREPEV on %d\n", ev->cpu);

	/* update first/last timestamps */
	c = &cpus[ev->cpu];
	if (!c->firstlast.start) {
		c->firstlast.start = ev->ts;
	}
	c->firstlast.end = ev->ts;
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
find_first_last_ts(struct cpu_table *cputab, struct ts_interval *fl)
{
	struct cpu *cpus = &cputab->cpus[0];
	int i;

	fl->start = -1;
	fl->end = 0;
	for (i = 0; i < cputab->ncpus; ++i) {
		printd(INTV, "cpu%d: (%ju, %ju)\n", i,
		       (uintmax_t)cpus[i].firstlast.start,
		       (uintmax_t)cpus[i].firstlast.end);
		if (cpus[i].firstlast.start &&
		    (cpus[i].firstlast.start < fl->start))
			fl->start = cpus[i].firstlast.start;
		if (cpus[i].firstlast.end &&
		    (cpus[i].firstlast.end > fl->end))
			fl->end = cpus[i].firstlast.end;
		cpus[i].td = NULL;
		cpus[i].ts = 0;
	}
	printd(INTV, "global (%jd, %jd)\n", (uintmax_t)fl->start, (uintmax_t)fl->end);
}

static
void
ctxsw_prepare_post(void *_ctx)
{
	struct td_switch_ctx *ctx = _ctx;

	find_first_last_ts(&ctx->cputab, &ctx->firstlast);
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
		if (!td)
			break;
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
	 * ctx->firstlast.end can be 0 if there were no events
	 * in the specified interval, in which case
	 * ctx->firstlast.start is invalid too.
	 */
	assert(!ctx->firstlast.end || (ev->ts >= ctx->firstlast.start));
	printd(INTV, "test2 (%ju:%ju) : %ju\n", (uintmax_t)ctx->interval.start,
	       (uintmax_t)ctx->interval.end, (uintmax_t)ev->ts);
	if ((ev->ts > ctx->interval.end) ||
	    (ev->ts < ctx->interval.start))
		return;
	printd(INTV, "DRAWEV %d\n", ev->cpu);
	if (c->td != ev->td) {	/* thread switch (or preemption) */
		draw_ctx_switch(ctx, c, ev);
		/* XXX: this is silly */
		for (i = 0; i < ctx->nr_top_threads; ++i) {
			if (!ctx->top_threads[i])
				break;
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
	printd(MISC, "evtranalyze: ncpus %d\n", ct->ncpus);
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
		cpus[i].firstlast.start = 0;
		cpus[i].firstlast.end = 0;
		cpus[i].evcnt = 0;
		cpus[i].freq = freqs[i];
	}
	free(freqs);
}

static
void
parse_interval(const char *_str, struct ts_interval *ts,
	       struct cpu_table *cputab)
{
	double s, e, freq;
	const char *str = _str + 1;

	if ('c' == *_str) {	/* cycles */
		if (sscanf(str, "%" SCNu64 ":%" SCNu64,
			   &ts->start,
			   &ts->end) == 2)
			return;
	} else if ('m' == *_str) {	/* miliseconds */
		if (sscanf(str, "%lf:%lf", &s, &e) == 2) {
			freq = cputab->cpus[0].freq;
			freq *= 1000.0;	/* msecs */
			if (freq < 0.0) {
				fprintf(stderr, "No frequency information"
					" available\n");
				err(2, "Can't convert time to cycles\n");
			}
			ts->start = s * freq;
			ts->end = e * freq;
			return;
		}
	}
	fprintf(stderr, "invalid interval format: %s\n", _str);
	usage();
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
	const char *outpath = "output.svg";
	struct evtr_filter ctxsw_filts[2] = {
		{
			.flags = 0,
			.cpu = -1,
			.ev_type = EVTR_TYPE_PROBE,
		},
		{
			.flags = 0,
			.cpu = -1,
			.ev_type = EVTR_TYPE_PROBE,
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
	td_ctx.interval.start = 0;
	td_ctx.interval.end = -1;	/* i.e. no interval given */
	td_ctx.nr_top_threads = NR_TOP_THREADS;
	cputab_init(&td_ctx.cputab);	/* needed for parse_interval() */

	optind = 0;
	optreset = 1;
	while ((ch = getopt(argc, argv, "i:o:")) != -1) {
		switch (ch) {
		case 'i':
			parse_interval(optarg, &td_ctx.interval,
				       &td_ctx.cputab);
			break;
		case 'o':
			outpath = optarg;
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

	if (!(td_ctx.top_threads = calloc(td_ctx.nr_top_threads,
					  sizeof(struct evtr_thread *))))
		err(1, "Can't allocate memory\n");
	if (!(svg = svg_document_create(outpath)))
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
	td_ctx.xscale = width / (td_ctx.firstlast.end - td_ctx.firstlast.start);
	printd(SVG, "first %ju, last %ju, xscale %lf\n",
	       (uintmax_t)td_ctx.firstlast.start, (uintmax_t)td_ctx.firstlast.end,
	       td_ctx.xscale);

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
	printd(MISC, "using freq = %lf\n", freq);
	filt.flags = 0;
	filt.cpu = -1;
	filt.ev_type = EVTR_TYPE_PROBE;
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
	q = evtr_query_init(evtr, &filt, 1);
	if (!q)
		err(1, "Can't initialize query\n");
	while(!evtr_query_next(q, &ev)) {
		char buf[1024];

		if (!last_ts)
			last_ts = ev.ts;
		if (freq < 0.0) {
			printf("%s\t%ju cycles\t[%.3d]\t%s:%d",
			       ev.td ? ev.td->comm : "unknown",
			       (uintmax_t)(ev.ts - last_ts), ev.cpu,
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
	if (evtr_query_error(q)) {
		err(1, "%s", evtr_query_errmsg(q));
	}
	evtr_query_destroy(q);
	return 0;
}

struct stats_ops {
	const char *statscmd;
	void *(*prepare)(int, char **, struct evtr_filter *);
	void (*each_event)(void *, evtr_event_t);
	void (*report)(void *);
};

struct stats_integer_ctx {
	const char *varname;
	struct {
		int plot;
		const char *path;
	} opts;
	void *plotter_ctx;
	struct plotter *plotter;
	plotid_t time_plot;
	uintmax_t sum;
	uintmax_t occurences;
};

static
void *
stats_integer_prepare(int argc, char **argv, struct evtr_filter *filt)
{
	struct stats_integer_ctx *ctx;
	int ch;

	if (!(ctx = calloc(1, sizeof(*ctx))))
		return ctx;
 
	optind = 0;
	optreset = 1;
	while ((ch = getopt(argc, argv, "p:")) != -1) {
		switch (ch) {
		case 'p':
			ctx->opts.plot = !0;
			ctx->opts.path = optarg;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		err(2, "Need exactly one variable");
	ctx->varname = argv[0];
	ctx->sum = ctx->occurences = 0;
	filt->flags = 0;
	filt->cpu = -1;
	filt->ev_type = EVTR_TYPE_STMT;
	filt->var = ctx->varname;
	if (!ctx->opts.plot)
		return ctx;

	if (!(ctx->plotter = plotter_factory()))
		err(1, "can't allocate plotter");
	if (!(ctx->plotter_ctx = ctx->plotter->plot_init(ctx->opts.path)))
		err(1, "can't allocate plotter context");

	if ((ctx->time_plot = ctx->plotter->plot_new(ctx->plotter_ctx,
							  PLOT_TYPE_LINE,
							  ctx->varname)) < 0)
		err(1, "can't create histogram");
	return ctx;
}

static
void
stats_integer_each(void *_ctx, evtr_event_t ev)
{
	struct stats_integer_ctx *ctx = _ctx;
	if (EVTR_VAL_INT != ev->stmt.val->type) {
		fprintf(stderr, "event at %jd (cpu %d) does not treat %s as an"
			"integer variable; ignored\n", ev->ts, ev->cpu,
			ctx->varname);
		return;
	}
	if (ctx->plotter)
		ctx->plotter->plot_line(ctx->plotter_ctx, ctx->time_plot,
					(double)ev->ts, (double)ev->stmt.val->num);
	ctx->sum += ev->stmt.val->num;
	++ctx->occurences;
}

static
void
stats_integer_report(void *_ctx)
{
	struct stats_integer_ctx *ctx = _ctx;
	printf("median for variable %s is %lf\n", ctx->varname,
	       (double)ctx->sum / ctx->occurences);
	if (ctx->plotter)
		ctx->plotter->plot_finish(ctx->plotter_ctx);

	free(ctx);
}

struct stats_completion_ctx {
	struct stats_completion_options {
		int plot;
		const char *path;
	} opts;
	struct plotter *plotter;
	void *plotter_ctx;
	plotid_t durations_plot;
	const char *varname;
	const char *ctor;
	const char *dtor;
	struct hashtab *ctors;
	uintmax_t historical_dtors;
	uintmax_t uncompleted_events;
	uintmax_t begun_events;
	uintmax_t completed_events;
	uintmax_t completed_duration_sum;
	vector_t durations;
};

struct ctor_data {
	evtr_variable_value_t val;
	uintmax_t ts;
};

static
struct ctor_data *
ctor_data_new(evtr_event_t ev)
{
	struct ctor_data *cd;

	if (!(cd = malloc(sizeof(*cd))))
		return cd;
	cd->val = ev->stmt.val;
	cd->ts = ev->ts;
	return cd;
}

static
void *
stats_completion_prepare(int argc, char **argv, struct evtr_filter *filt)
{
	struct stats_completion_ctx *ctx;
	int ch;

	if (!(ctx = calloc(1, sizeof(*ctx))))
		return ctx;

	optind = 0;
	optreset = 1;
	while ((ch = getopt(argc, argv, "p:")) != -1) {
		switch (ch) {
		case 'p':
			ctx->opts.plot = !0;
			ctx->opts.path = optarg;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 3)
		err(2, "need a variable, a constructor and a destructor");
	if (!(ctx->ctors = ehash_new()))
		goto free_ctx;
	ctx->ctors->hashfunc = &hashfunc_ctor;
	ctx->ctors->cmpfunc = &cmpfunc_ctor;
	if (!(ctx->durations = vector_new()))
		goto free_ctors;
	ctx->varname = argv[0];
	ctx->ctor = argv[1];
	ctx->dtor = argv[2];

	filt->flags = 0;
	filt->cpu = -1;
	filt->ev_type = EVTR_TYPE_STMT;
	filt->var = ctx->varname;

	if (!ctx->opts.plot)
		return ctx;

	if (!(ctx->plotter = plotter_factory()))
		err(1, "can't allocate plotter");
	if (!(ctx->plotter_ctx = ctx->plotter->plot_init(ctx->opts.path)))
		err(1, "can't allocate plotter context");

	if ((ctx->durations_plot = ctx->plotter->plot_new(ctx->plotter_ctx,
							  PLOT_TYPE_HIST,
							  ctx->varname)) < 0)
		err(1, "can't create histogram");
	return ctx;
free_ctors:
	;	/* XXX */
free_ctx:
	free(ctx);
	return NULL;
}

static
void
stats_completion_each(void *_ctx, evtr_event_t ev)
{
	struct stats_completion_ctx *ctx = _ctx;
	struct ctor_data *cd;

	if (ev->stmt.val->type != EVTR_VAL_CTOR) {
		fprintf(stderr, "event at %jd (cpu %d) does not assign to %s "
			"with a data constructor; ignored\n", ev->ts, ev->cpu,
			ctx->varname);
		return;
	}
	if (!strcmp(ev->stmt.val->ctor.name, ctx->ctor)) {
		uintptr_t v;
		if (!ehash_find(ctx->ctors, (uintptr_t)ev->stmt.val, &v)) {
			/* XXX:better diagnostic */
			fprintf(stderr, "duplicate ctor\n");
			err(3, "giving up");
		}
		if (!(cd = ctor_data_new(ev)))
			err(1, "out of memory");
		v = (uintptr_t)cd;
		if (!ehash_insert(ctx->ctors, (uintptr_t)ev->stmt.val, v))
			err(1, "out of memory");
		++ctx->begun_events;
	} else if (!strcmp(ev->stmt.val->ctor.name, ctx->dtor)) {
		uintptr_t v;
		const char *tmp = ev->stmt.val->ctor.name;
		ev->stmt.val->ctor.name = ctx->ctor;
		if (ehash_find(ctx->ctors, (uintptr_t)ev->stmt.val, &v)) {
			++ctx->historical_dtors;
			ev->stmt.val->ctor.name = tmp;
			return;
		}
		cd = (struct ctor_data *)v;
		if (cd->ts >= ev->ts) {
			/* XXX:better diagnostic */
			fprintf(stderr, "destructor preceds constructor;"
				" ignored\n");
			ev->stmt.val->ctor.name = tmp;
			return;
		}
		if (ctx->plotter)
			ctx->plotter->plot_histogram(ctx->plotter_ctx,
						     ctx->durations_plot,
						     (double)(ev->ts - cd->ts));
		vector_push(ctx->durations, ev->ts - cd->ts);
		++ctx->completed_events;
		ctx->completed_duration_sum += ev->ts - cd->ts;
		if (ehash_delete(ctx->ctors, (uintptr_t)ev->stmt.val))
			err(3, "ctor disappeared from hash!");
		ev->stmt.val->ctor.name = tmp;
	} else {
		fprintf(stderr, "event at %jd (cpu %d) assigns to %s "
			"with an unexpected data constructor; ignored\n",
			ev->ts, ev->cpu, ctx->varname);
		return;
	}
}

static
void
stats_completion_report(void *_ctx)
{
	struct stats_completion_ctx *ctx = _ctx;
	double avg;

	printf("Events completed without having started:\t%jd\n",
	       ctx->historical_dtors);
	printf("Events started but didn't complete:\t%jd\n",
	       ctx->begun_events - ctx->completed_events);
	avg = (double)ctx->completed_duration_sum / ctx->completed_events;
	printf("Average event duration:\t%lf (stddev %lf)\n", avg,
	       stddev(ctx->durations, avg));

	if (ctx->plotter)
		ctx->plotter->plot_finish(ctx->plotter_ctx);
	vector_destroy(ctx->durations);
	/* XXX: hash */
	free(ctx);
}

static struct stats_ops cmd_stat_ops[] = {
	{
		.statscmd = "integer",
		.prepare = &stats_integer_prepare,
		.each_event = &stats_integer_each,
		.report = &stats_integer_report,
	},
	{
		.statscmd = "completion",
		.prepare = &stats_completion_prepare,
		.each_event = &stats_completion_each,
		.report = &stats_completion_report,
	},
	{
		.statscmd = NULL,
	},
};

static
int
cmd_stats(int argc, char **argv)
{
	struct evtr_event ev;
	struct evtr_query *q;
	struct evtr_filter filt;
	struct cpu_table cputab;
	double freq;
	uint64_t last_ts = 0;
	struct stats_ops *statsops = &cmd_stat_ops[0];
	void *statctx;

	for (; statsops->statscmd; ++statsops) {
		if (!strcmp(statsops->statscmd, argv[1]))
			break;
	}
	if (!statsops->statscmd)
		err(2, "No such stats type: %s", argv[1]);

	--argc;
	++argv;
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
	printd(MISC, "using freq = %lf\n", freq);

	if (!(statctx = statsops->prepare(argc, argv, &filt)))
		err(1, "Can't allocate stats context");
	q = evtr_query_init(evtr, &filt, 1);
	if (!q)
		err(1, "Can't initialize query");
	while(!evtr_query_next(q, &ev)) {

		if (!last_ts)
			last_ts = ev.ts;

		assert(ev.type == EVTR_TYPE_STMT);
		statsops->each_event(statctx, &ev);
		last_ts = ev.ts;
	}
	if (evtr_query_error(q)) {
		err(1, "%s", evtr_query_errmsg(q));
	}
	evtr_query_destroy(q);
	statsops->report(statctx);
	return 0;
}


static
int
cmd_summary(int argc, char **argv)
{
	struct evtr_filter filt;
	struct evtr_event ev;
	struct evtr_query *q;
	double freq;
	struct cpu_table cputab;
	struct ts_interval global;
	uintmax_t global_evcnt;
	int i;

	(void)argc;
	(void)argv;

	cputab_init(&cputab);
	filt.ev_type = EVTR_TYPE_PROBE;
	filt.fmt = NULL;
	filt.flags = 0;
	filt.cpu = -1;

	q = evtr_query_init(evtr, &filt, 1);
	if (!q)
		err(1, "Can't initialize query\n");
	while(!evtr_query_next(q, &ev)) {
		struct cpu *c = &cputab.cpus[ev.cpu];
		if (!c->firstlast.start)
			c->firstlast.start = ev.ts;
		++c->evcnt;
		c->firstlast.end = ev.ts;
	}
	if (evtr_query_error(q)) {
		err(1, "%s", evtr_query_errmsg(q));
	}
	evtr_query_destroy(q);

	find_first_last_ts(&cputab, &global);

	freq = cputab.cpus[0].freq;
	global_evcnt = 0;
	for (i = 0; i < cputab.ncpus; ++i) {
		struct cpu *c = &cputab.cpus[i];
		printf("CPU %d: %jd events in %.3lf secs\n", i,
		       c->evcnt, (c->firstlast.end - c->firstlast.start)
		       / freq);
		global_evcnt += c->evcnt;
	}
	printf("Total: %jd events on %d cpus in %.3lf secs\n", global_evcnt,
	       cputab.ncpus, (global.end - global.start) / freq);
	return 0;
}


int
main(int argc, char **argv)
{
	int ch;
	FILE *inf;
	struct command *cmd;
	char *tmp;

	while ((ch = getopt(argc, argv, "f:D:")) != -1) {
		switch (ch) {
		case 'f':
			opt_infile = optarg;
			break;
		case 'D':
			if ((tmp = strchr(optarg, ':'))) {
				*tmp++ = '\0';
				evtr_set_debug(tmp);
			}
			printd_set_flags(optarg, &evtranalyze_debug);
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
	if (!opt_infile) {
		err(2, "you need to specify an input file\n");
	} else if (!strcmp(opt_infile, "-")) {
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

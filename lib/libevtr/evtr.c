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
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/tree.h>


#include "evtr.h"
#include "internal.h"

unsigned evtr_debug;

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


enum {
	MAX_EVHDR_SIZE = PATH_MAX + 200,
	/* string namespaces */
	EVTR_NS_PATH = 0x1,
	EVTR_NS_FUNC,
	EVTR_NS_DSTR,
	EVTR_NS_MAX,
	NR_BUCKETS = 1021,	/* prime */
	PARSE_ERR_BUFSIZE = 256,
	REC_ALIGN = 8,
	REC_BOUNDARY = 1 << 14,
	FILTF_ID = 0x10,
	EVTRF_WR = 0x1,		/* open for writing */
	EVTRQF_PENDING = 0x1,
};

typedef uint16_t fileid_t;
typedef uint16_t funcid_t;
typedef uint16_t fmtid_t;

struct trace_event_header {
	uint8_t type;
	uint64_t ts;	/* XXX: this should only be part of probe */
} __attribute__((packed));

struct probe_event_header {
	struct trace_event_header eh;
	/*
	 * For these fields, 0 implies "not available"
	 */
	fileid_t file;
	funcid_t caller1;
	funcid_t caller2;
	funcid_t func;
	uint16_t line;
	fmtid_t fmt;
	uint16_t datalen;
	uint8_t cpu;	/* -1 if n/a */
} __attribute__((packed));

struct string_event_header {
	struct trace_event_header eh;
	uint16_t ns;
	uint32_t id;
	uint16_t len;
} __attribute__((packed));

struct fmt_event_header {
	struct trace_event_header eh;
	uint16_t id;
	uint8_t subsys_len;
	uint8_t fmt_len;
} __attribute__((packed));

struct cpuinfo_event_header {
	double freq;
	uint8_t cpu;
} __attribute__((packed));

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

struct symtab {
	struct hashtab tab;
};

struct event_fmt {
	const char *subsys;
	const char *fmt;
};

struct event_filter_unresolved {
	TAILQ_ENTRY(event_filter_unresolved) link;
	evtr_filter_t filt;
};

struct id_map {
	RB_ENTRY(id_map) rb_node;
	int id;
	const void *data;
};

RB_HEAD(id_tree, id_map);
struct string_map {
	struct id_tree root;
};

struct fmt_map {
	struct id_tree root;
};

RB_HEAD(thread_tree, evtr_thread);

struct thread_map {
	struct thread_tree root;
};

struct event_callback {
	void (*cb)(evtr_event_t, void *data);
	void *data;	/* this field must be malloc()ed */
};

struct cpu {
	struct evtr_thread *td;	/* currently executing thread */
	double freq;
};

struct evtr {
	FILE *f;
	int flags;
	int err;
	const char *errmsg;
	off_t bytes;
	union {
		/*
		 * When writing, we keep track of the strings we've
		 * already dumped so we only dump them once.
		 * Paths, function names etc belong to different
		 * namespaces.
		 */
		struct hashtab_str *strings[EVTR_NS_MAX - 1];
		/*
		 * When reading, we build a map from id to string.
		 * Every id must be defined at the point of use.
		 */
		struct string_map maps[EVTR_NS_MAX - 1];
	};
	union {
		/* same as above, but for subsys+fmt pairs */
		struct fmt_map fmtmap;
		struct hashtab_str *fmts;
	};
	struct thread_map threads;
	struct cpu *cpus;
	int ncpus;
};

struct evtr_query {
	evtr_t evtr;
	off_t off;
	evtr_filter_t filt;
	int nfilt;
	int nmatched;
	int ntried;
	void *buf;
	int bufsize;
	struct symtab *symtab;
	int ncbs;
	struct event_callback **cbs;
	/*
	 * Filters that have a format specified and we
	 * need to resolve that to an fmtid
	 */
	TAILQ_HEAD(, event_filter_unresolved) unresolved_filtq;
	int err;
	const char *errmsg;
	char parse_err_buf[PARSE_ERR_BUFSIZE];
	int flags;
	struct evtr_event pending_event;
};

void
evtr_set_debug(const char *str)
{
	printd_set_flags(str, &evtr_debug);
}

static int id_map_cmp(struct id_map *, struct id_map *);
RB_PROTOTYPE2(id_tree, id_map, rb_node, id_map_cmp, int);
RB_GENERATE2(id_tree, id_map, rb_node, id_map_cmp, int, id);

static int thread_cmp(struct evtr_thread *, struct evtr_thread *);
RB_PROTOTYPE2(thread_tree, evtr_thread, rb_node, thread_cmp, void *);
RB_GENERATE2(thread_tree, evtr_thread, rb_node, thread_cmp, void *, id);

static inline
void
validate_string(const char *str)
{
	if (!(evtr_debug & MISC))
		return;
	for (; *str; ++str)
		assert(isprint(*str));
}

static
void
id_tree_free(struct id_tree *root)
{
	struct id_map *v, *n;

	for (v = RB_MIN(id_tree, root); v; v = n) {
		n = RB_NEXT(id_tree, root, v);
		RB_REMOVE(id_tree, root, v);
	}
}

static
int
evtr_register_callback(evtr_query_t q, void (*fn)(evtr_event_t, void *), void *d)
{
	struct event_callback *cb;
	void *cbs;

	if (!(cb = malloc(sizeof(*cb)))) {
		q->err = ENOMEM;
		return !0;
	}
	cb->cb = fn;
	cb->data = d;
	if (!(cbs = realloc(q->cbs, (++q->ncbs) * sizeof(cb)))) {
		--q->ncbs;
		free(cb);
		q->err = ENOMEM;
		return !0;
	}
	q->cbs = cbs;
	q->cbs[q->ncbs - 1] = cb;
	return 0;
}

static
void
evtr_deregister_callbacks(evtr_query_t q)
{
	int i;

	for (i = 0; i < q->ncbs; ++i) {
		free(q->cbs[i]);
	}
	free(q->cbs);
	q->cbs = NULL;
}

static
void
evtr_run_callbacks(evtr_event_t ev, evtr_query_t q)
{
	struct event_callback *cb;
	int i;

	for (i = 0; i < q->ncbs; ++i) {
		cb = q->cbs[i];
		cb->cb(ev, cb->data);
	}
}

static
struct cpu *
evtr_cpu(evtr_t evtr, int c)
{
	if ((c < 0) || (c >= evtr->ncpus))
		return NULL;
	return &evtr->cpus[c];
}

static int parse_format_data(evtr_event_t ev, const char *fmt, ...)
	       __printflike(2, 3) __scanflike(2, 3);

static
int
parse_format_data(evtr_event_t ev, const char *fmt, ...)
{
	va_list ap;
	char buf[2048];

	if (strcmp(fmt, ev->fmt))
		return 0;
	vsnprintf(buf, sizeof(buf), fmt, __DECONST(void *, ev->fmtdata));
	printd(MISC, "string is: %s\n", buf);
	va_start(ap, fmt);
	return vsscanf(buf, fmt, ap);
}

static
void
evtr_deregister_filters(evtr_query_t q, evtr_filter_t filt, int nfilt)
{
	struct event_filter_unresolved *u, *tmp;
	int i;
	TAILQ_FOREACH_MUTABLE(u, &q->unresolved_filtq, link, tmp) {
		for (i = 0; i < nfilt; ++i) {
			if (u->filt == &filt[i]) {
				TAILQ_REMOVE(&q->unresolved_filtq, u, link);
			}
		}
	}
}

static
int
evtr_filter_register(evtr_query_t q, evtr_filter_t filt)
{
	struct event_filter_unresolved *res;

	if (!(res = malloc(sizeof(*res)))) {
		q->err = ENOMEM;
		return !0;
	}
	res->filt = filt;
	TAILQ_INSERT_TAIL(&q->unresolved_filtq, res, link);
	return 0;
}

static
int
evtr_query_needs_parsing(evtr_query_t q)
{
	int i;

	for (i = 0; i < q->nfilt; ++i)
		if (q->filt[i].ev_type == EVTR_TYPE_STMT)
			return !0;
	return 0;
}

void
evtr_event_data(evtr_event_t ev, char *buf, size_t len)
{
	/*
	 * XXX: we implicitly trust the format string.
	 * We shouldn't.
	 */
	if (ev->fmtdatalen) {
		vsnprintf(buf, len, ev->fmt, __DECONST(void *, ev->fmtdata));
	} else {
		strlcpy(buf, ev->fmt, len);
	}
}

int
evtr_error(evtr_t evtr)
{
	return evtr->err || (evtr->errmsg != NULL);
}

const char *
evtr_errmsg(evtr_t evtr)
{
	return evtr->errmsg ? evtr->errmsg : strerror(evtr->err);
}

int
evtr_query_error(evtr_query_t q)
{
	return q->err || (q->errmsg != NULL) || evtr_error(q->evtr);
}

const char *
evtr_query_errmsg(evtr_query_t q)
{
	return q->errmsg ? q->errmsg :
		(q->err ? strerror(q->err) :
		 (evtr_errmsg(q->evtr)));
}

static
int
id_map_cmp(struct id_map *a, struct id_map *b)
{
	return a->id - b->id;
}

static
int
thread_cmp(struct evtr_thread *a, struct evtr_thread *b)
{
	ptrdiff_t d;
	d =  a->id - b->id;
	if (d < 0)
		return -1;
	if (!d)
		return 0;
	return 1;
}

#define DEFINE_MAP_FIND(prefix, type)		\
	static					\
	type				\
	prefix ## _map_find(struct id_tree *tree, int id)\
	{						 \
		struct id_map *sid;			 \
							\
		sid = id_tree_RB_LOOKUP(tree, id);	\
		return sid ? sid->data : NULL;		\
	}

DEFINE_MAP_FIND(string, const char *)
DEFINE_MAP_FIND(fmt, const struct event_fmt *)

static
struct evtr_thread *
thread_map_find(struct thread_map *map, void *id)
{
	return thread_tree_RB_LOOKUP(&map->root, id);
}

#define DEFINE_MAP_INSERT(prefix, type, _cmp, _dup)	\
	static					\
	int								\
	prefix ## _map_insert(struct id_tree *tree, type data, int id) \
	{								\
	struct id_map *sid, *osid;					\
									\
	sid = malloc(sizeof(*sid));					\
	if (!sid) {							\
		return ENOMEM;						\
	}								\
	sid->id = id;							\
	sid->data = data;						\
	if ((osid = id_tree_RB_INSERT(tree, sid))) {			\
		free(sid);						\
		if (_cmp((type)osid->data, data)) {			\
			return EEXIST;					\
		}							\
		printd(DS, "mapping already exists, skipping\n");		\
		/* we're OK with redefinitions of an id to the same string */ \
		return 0;						\
	}								\
	/* only do the strdup if we're inserting a new string */	\
	sid->data = _dup(data);		/* XXX: oom */			\
	return 0;							\
}

static
void
thread_map_insert(struct thread_map *map, struct evtr_thread *td)
{
	struct evtr_thread *otd;

	if ((otd = thread_tree_RB_INSERT(&map->root, td))) {
		/*
		 * Thread addresses might be reused, we're
		 * ok with that.
		 * DANGER, Will Robinson: this means the user
		 * of the API needs to copy event->td if they
		 * want it to remain stable.
		 */
		free((void *)otd->comm);
		otd->comm = td->comm;
		free(td);
	}
}

static
int
event_fmt_cmp(const struct event_fmt *a, const struct event_fmt *b)
{
	int ret = 0;

	if (a->subsys) {
		if (b->subsys) {
			ret = strcmp(a->subsys, b->subsys);
		} else {
			ret = strcmp(a->subsys, "");
		}
	} else if (b->subsys) {
			ret = strcmp("", b->subsys);
	}
	if (ret)
		return ret;
	return strcmp(a->fmt, b->fmt);
}

static
struct event_fmt *
event_fmt_dup(const struct event_fmt *o)
{
	struct event_fmt *n;

	if (!(n = malloc(sizeof(*n)))) {
		return n;
	}
	memcpy(n, o, sizeof(*n));
	return n;
}

DEFINE_MAP_INSERT(string, const char *, strcmp, strdup)
DEFINE_MAP_INSERT(fmt, const struct event_fmt *, event_fmt_cmp, event_fmt_dup)

int
hash_find(const struct hashtab *tab, uintptr_t key, uintptr_t *val)
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

struct hashentry *
hash_insert(struct hashtab *tab, uintptr_t key, uintptr_t val)
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

struct hashtab *
hash_new(void)
{
	struct hashtab *tab;
	if (!(tab = calloc(sizeof(struct hashtab), 1)))
		return tab;
	tab->hashfunc = &hashfunc_pointer;
	tab->cmpfunc = &cmpfunc_pointer;
	return tab;
}

struct hashtab_str {	/* string -> id map */
	struct hashtab tab;
	uint16_t id;
};

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

static
uintptr_t
cmpfunc_string(uintptr_t a, uintptr_t b)
{
	return strcmp((char *)a, (char *)b);
}


static
struct hashtab_str *
strhash_new(void)
{
	struct hashtab_str *strtab;
	if (!(strtab = calloc(sizeof(struct hashtab_str), 1)))
		return strtab;
	strtab->tab.hashfunc = &hashfunc_string;
	strtab->tab.cmpfunc = &cmpfunc_string;
	return strtab;
}

static
void
strhash_destroy(struct hashtab_str *strtab)
{
	free(strtab);
}

static
int
strhash_find(struct hashtab_str *strtab, const char *str, uint16_t *id)
{
	uintptr_t val;

	if (hash_find(&strtab->tab, (uintptr_t)str, &val))
		return !0;
	*id = (uint16_t)val;
	return 0;
}

static
int
strhash_insert(struct hashtab_str *strtab, const char *str, uint16_t *id)
{
	uintptr_t val;

	val = ++strtab->id;
	if (strtab->id == 0) {
		fprintf(stderr, "too many strings\n");
		return ERANGE;
	}
	str = strdup(str);
	if (!str) {
		fprintf(stderr, "out of memory\n");
		--strtab->id;
		return ENOMEM;
	}
	hash_insert(&strtab->tab, (uintptr_t)str, (uintptr_t)val);
	*id = strtab->id;
	return 0;
}

struct symtab *
symtab_new(void)
{
	struct symtab *symtab;
	if (!(symtab = calloc(sizeof(struct symtab), 1)))
		return symtab;
	symtab->tab.hashfunc = &hashfunc_string;
	symtab->tab.cmpfunc = &cmpfunc_string;
	return symtab;
}

void
symtab_destroy(struct symtab *symtab)
{
	free(symtab);
}

struct evtr_variable *
symtab_find(const struct symtab *symtab, const char *str)
{
	uintptr_t val;

	if (hash_find(&symtab->tab, (uintptr_t)str, &val))
		return NULL;
	return (struct evtr_variable *)val;
}

int
symtab_insert(struct symtab *symtab, const char *name,
	       struct evtr_variable *var)
{
	name = strdup(name);
	if (!name) {
		fprintf(stderr, "out of memory\n");
		return ENOMEM;
	}
	hash_insert(&symtab->tab, (uintptr_t)name, (uintptr_t)var);
	return 0;
}

static
int
evtr_filter_match(evtr_query_t q, evtr_filter_t f, evtr_event_t ev)
{
	if ((f->cpu != -1) && (f->cpu != ev->cpu))
		return 0;

	assert(!(f->flags & FILTF_ID));
	if (ev->type != f->ev_type)
		return 0;
	if (ev->type == EVTR_TYPE_PROBE) {
		if (f->fmt && strcmp(ev->fmt, f->fmt))
			return 0;
	} else if (ev->type == EVTR_TYPE_STMT) {
		struct evtr_variable *var;
		/* resolve var */
		/* XXX: no need to do that *every* time */
		parse_var(f->var, q->symtab, &var, &q->parse_err_buf[0],
			  PARSE_ERR_BUFSIZE);
		/*
		 * Ignore errors, they're expected since the
		 * variable might not be instantiated yet
		 */
		if (var != ev->stmt.var)
			return 0;
	}
	return !0;
}

static
int
evtr_match_filters(struct evtr_query *q, evtr_event_t ev)
{
	int i;

	/* no filters means we're interested in all events */
	if (!q->nfilt)
		return !0;
	++q->ntried;
	for (i = 0; i < q->nfilt; ++i) {
		if (evtr_filter_match(q, &q->filt[i], ev)) {
			++q->nmatched;
			return !0;
		}
	}
	return 0;
}

static
void
parse_callback(evtr_event_t ev, void *d)
{
	evtr_query_t q = (evtr_query_t)d;
	if (ev->type != EVTR_TYPE_PROBE)
		return;
	if (!ev->fmt || (ev->fmt[0] != '#'))
		return;
	/*
	 * Copy the event to ->pending_event, then call
	 * the parser to convert it into a synthesized
	 * EVTR_TYPE_STMT event.
	 */
	memcpy(&q->pending_event, ev, sizeof(*ev));
	parse_string(&q->pending_event, q->symtab, &ev->fmt[1],
		     &q->parse_err_buf[0], PARSE_ERR_BUFSIZE);
	if (q->parse_err_buf[0]) {	/* parse error */
		q->errmsg = &q->parse_err_buf[0];
		return;
	}
	if (!evtr_match_filters(q, &q->pending_event))
		return;
	/*
	 * This will cause us to return ->pending_event next time
	 * we're called.
	 */
	q->flags |= EVTRQF_PENDING;
}

static
void
thread_creation_callback(evtr_event_t ev, void *d)
{
	evtr_query_t q = (evtr_query_t)d;
	evtr_t evtr = q->evtr;
	struct evtr_thread *td;
	void *ktd;
	char buf[20];

	if (parse_format_data(ev, "new_td %p %s", &ktd, buf) != 2) {
		return;
	}
	buf[19] = '\0';

	if (!(td = malloc(sizeof(*td)))) {
		q->err = ENOMEM;
		return;
	}
	td->id = ktd;
	td->userdata = NULL;
	if (!(td->comm = strdup(buf))) {
		free(td);
		q->err = ENOMEM;
		return;
	}
	printd(DS, "inserting new thread %p: %s\n", td->id, td->comm);
	thread_map_insert(&evtr->threads, td);
}

static
void
thread_switch_callback(evtr_event_t ev, void *d)
{
	evtr_t evtr = ((evtr_query_t)d)->evtr;
	struct evtr_thread *tdp, *tdn;
	void *ktdp, *ktdn;
	struct cpu *cpu;
	static struct evtr_event tdcr;
	static char *fmt = "new_td %p %s";
	char tidstr[40];
	char fmtdata[sizeof(void *) + sizeof(char *)];

	cpu = evtr_cpu(evtr, ev->cpu);
	if (!cpu) {
		printw("invalid cpu %d\n", ev->cpu);
		return;
	}
	if (parse_format_data(ev, "sw  %p > %p", &ktdp, &ktdn) != 2) {
		return;
	}
	tdp = thread_map_find(&evtr->threads, ktdp);
	if (!tdp) {
		printd(DS, "switching from unknown thread %p\n", ktdp);
	}
	tdn = thread_map_find(&evtr->threads, ktdn);
	if (!tdn) {
		/*
		 * Fake a thread creation event for threads we
		 * haven't seen before.
		 */
		tdcr.type = EVTR_TYPE_PROBE;
		tdcr.ts = ev->ts;
		tdcr.file = NULL;
		tdcr.func = NULL;
		tdcr.line = 0;
		tdcr.fmt = fmt;
		tdcr.fmtdata = &fmtdata;
		tdcr.fmtdatalen = sizeof(fmtdata);
		tdcr.cpu = ev->cpu;
		tdcr.td = NULL;
		snprintf(tidstr, sizeof(tidstr), "%p", ktdn);
		((void **)fmtdata)[0] = ktdn;
		((char **)fmtdata)[1] = &tidstr[0];
		thread_creation_callback(&tdcr, d);

		tdn = thread_map_find(&evtr->threads, ktdn);
		assert(tdn != NULL);
		printd(DS, "switching to unknown thread %p\n", ktdn);
		cpu->td = tdn;
		return;
	}
	printd(DS, "cpu %d: switching to thread %p\n", ev->cpu, ktdn);
	cpu->td = tdn;
}

static
void
assert_foff_in_sync(evtr_t evtr)
{
	off_t off;

	/*
	 * We keep our own offset because we
	 * might want to support mmap()
	 */
	off = ftello(evtr->f);
	if (evtr->bytes != off) {
		fprintf(stderr, "bytes %jd, off %jd\n", evtr->bytes, off);
		abort();
	}
}

static
int
evtr_write(evtr_t evtr, const void *buf, size_t bytes)
{
	assert_foff_in_sync(evtr);
	if (fwrite(buf, bytes, 1, evtr->f) != 1) {
		evtr->err = errno;
		evtr->errmsg = strerror(errno);
		return !0;
	}
	evtr->bytes += bytes;
	assert_foff_in_sync(evtr);
	return 0;
}

/*
 * Called after dumping a record to make sure the next
 * record is REC_ALIGN aligned. This does not make much sense,
 * as we shouldn't be using packed structs anyway.
 */
static
int
evtr_dump_pad(evtr_t evtr)
{
	size_t pad;
	static char buf[REC_ALIGN];

	pad = REC_ALIGN - (evtr->bytes % REC_ALIGN);
	if (pad > 0) {
		return evtr_write(evtr, buf, pad);
	}
	return 0;
}

/*
 * We make sure that there is a new record every REC_BOUNDARY
 * bytes, this costs next to nothing in space and allows for
 * fast seeking.
 */
static
int
evtr_dump_avoid_boundary(evtr_t evtr, size_t bytes)
{
	unsigned pad, i;
	static char buf[256];

	pad = REC_BOUNDARY - (evtr->bytes % REC_BOUNDARY);
	/* if adding @bytes would cause us to cross a boundary... */
	if (bytes > pad) {
		/* then pad to the boundary */
		for (i = 0; i < (pad / sizeof(buf)); ++i) {
			if (evtr_write(evtr, buf, sizeof(buf))) {
				return !0;
			}
		}
		i = pad % sizeof(buf);
		if (i) {
			if (evtr_write(evtr, buf, i)) {
				return !0;
			}
		}
	}
	return 0;
}

static
int
evtr_dump_fmt(evtr_t evtr, uint64_t ts, const evtr_event_t ev)
{
	struct fmt_event_header fmt;
	uint16_t id;
	int err;
	char *subsys = "", buf[1024];

	if (strlcpy(buf, subsys, sizeof(buf)) >= sizeof(buf)) {
		evtr->errmsg = "name of subsystem is too large";
		evtr->err = ERANGE;
		return 0;
	}
	if (strlcat(buf, ev->fmt, sizeof(buf)) >= sizeof(buf)) {
		evtr->errmsg = "fmt + name of subsystem is too large";
		evtr->err = ERANGE;
		return 0;
	}

	if (!strhash_find(evtr->fmts, buf, &id)) {
		return id;
	}
	if ((err = strhash_insert(evtr->fmts, buf, &id))) {
		evtr->err = err;
		return 0;
	}

	fmt.eh.type = EVTR_TYPE_FMT;
	fmt.eh.ts = ts;
	fmt.subsys_len = strlen(subsys);
	fmt.fmt_len = strlen(ev->fmt);
	fmt.id = id;
	if (evtr_dump_avoid_boundary(evtr, sizeof(fmt) + fmt.subsys_len +
				     fmt.fmt_len))
		return 0;
	if (evtr_write(evtr, &fmt, sizeof(fmt)))
		return 0;
	if (evtr_write(evtr, subsys, fmt.subsys_len))
		return 0;
	if (evtr_write(evtr, ev->fmt, fmt.fmt_len))
		return 0;
	if (evtr_dump_pad(evtr))
		return 0;
	return fmt.id;
}

/*
 * Replace string pointers or string ids in fmtdata
 */ 
static
int
mangle_string_ptrs(const char *fmt, uint8_t *fmtdata,
		   const char *(*replace)(void *, const char *), void *ctx)
{
	const char *f, *p;
	size_t skipsize, intsz;
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
			((const char **)fmtdata)[0] =
				replace(ctx, ((char **)fmtdata)[0]);
			skipsize = sizeof(char *);
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

/* XXX: do we really want the timestamp? */
static
int
evtr_dump_string(evtr_t evtr, uint64_t ts, const char *str, int ns)
{
	struct string_event_header s;
	int err;
	uint16_t id;

	assert((0 <= ns) && (ns < EVTR_NS_MAX));
	if (!strhash_find(evtr->strings[ns], str, &id)) {
		return id;
	}
	if ((err = strhash_insert(evtr->strings[ns], str, &id))) {
		evtr->err = err;
		return 0;
	}

	printd(DS, "hash_insert %s ns %d id %d\n", str, ns, id);
	s.eh.type = EVTR_TYPE_STR;
	s.eh.ts = ts;
	s.ns = ns;
	s.id = id;
	s.len = strnlen(str, PATH_MAX);

	if (evtr_dump_avoid_boundary(evtr, sizeof(s) + s.len))
		return 0;
	if (evtr_write(evtr, &s, sizeof(s)))
		return 0;
	if (evtr_write(evtr, str, s.len))
		return 0;
	if (evtr_dump_pad(evtr))
		return 0;
	return s.id;
}

struct replace_ctx {
	evtr_t evtr;
	uint64_t ts;
};

static
const char *
replace_strptr(void *_ctx, const char *s)
{
	struct replace_ctx *ctx = _ctx;
	return (const char *)(uintptr_t)evtr_dump_string(ctx->evtr, ctx->ts, s,
							 EVTR_NS_DSTR);
}

static
const char *
replace_strid(void *_ctx, const char *s)
{
	struct replace_ctx *ctx = _ctx;
	const char *ret;

	ret = string_map_find(&ctx->evtr->maps[EVTR_NS_DSTR - 1].root,
			      (int)(uintptr_t)s);
	if (!ret) {
		fprintf(stderr, "Unknown id for data string\n");
		ctx->evtr->errmsg = "unknown id for data string";
		ctx->evtr->err = !0;
	}
	validate_string(ret);
	printd(DS, "replacing strid %d (ns %d) with string '%s' (or int %#x)\n",
	       (int)(uintptr_t)s, EVTR_NS_DSTR, ret ? ret : "NULL", (int)(uintptr_t)ret);
	return ret;
}

static
int
evtr_dump_probe(evtr_t evtr, evtr_event_t ev)
{
	struct probe_event_header kev;
	char buf[1024];

	memset(&kev, '\0', sizeof(kev));
	kev.eh.type = ev->type;
	kev.eh.ts = ev->ts;
	kev.line = ev->line;
	kev.cpu = ev->cpu;
	if (ev->file) {
		kev.file = evtr_dump_string(evtr, kev.eh.ts, ev->file,
					    EVTR_NS_PATH);
	}
	if (ev->func) {
		kev.func = evtr_dump_string(evtr, kev.eh.ts, ev->func,
					    EVTR_NS_FUNC);
	}
	if (ev->fmt) {
		kev.fmt = evtr_dump_fmt(evtr, kev.eh.ts, ev);
	}
	if (ev->fmtdata) {
		struct replace_ctx replctx = {
			.evtr = evtr,
			.ts = ev->ts,
		};
		assert(ev->fmtdatalen <= (int)sizeof(buf));
		kev.datalen = ev->fmtdatalen;
		/*
		 * Replace all string pointers with string ids before dumping
		 * the data.
		 */
		memcpy(buf, ev->fmtdata, ev->fmtdatalen);
		if (mangle_string_ptrs(ev->fmt, buf,
				       replace_strptr, &replctx) < 0)
			return !0;
		if (evtr->err)
			return evtr->err;
	}
	if (evtr_dump_avoid_boundary(evtr, sizeof(kev) + ev->fmtdatalen))
		return !0;
	if (evtr_write(evtr, &kev, sizeof(kev)))
		return !0;
	if (evtr_write(evtr, buf, ev->fmtdatalen))
		return !0;
	if (evtr_dump_pad(evtr))
		return !0;
	return 0;
}

static
int
evtr_dump_sysinfo(evtr_t evtr, evtr_event_t ev)
{
	uint8_t type = EVTR_TYPE_SYSINFO;
	uint16_t ncpus = ev->ncpus;

	if (ncpus <= 0) {
		evtr->errmsg = "invalid number of cpus";
		return !0;
	}
	if (evtr_dump_avoid_boundary(evtr, sizeof(type) + sizeof(ncpus)))
		return !0;
	if (evtr_write(evtr, &type, sizeof(type))) {
		return !0;
	}
	if (evtr_write(evtr, &ncpus, sizeof(ncpus))) {
		return !0;
	}
	if (evtr_dump_pad(evtr))
		return !0;
	return 0;
}
static
int
evtr_dump_cpuinfo(evtr_t evtr, evtr_event_t ev)
{
	struct cpuinfo_event_header ci;
	uint8_t type;

	if (evtr_dump_avoid_boundary(evtr, sizeof(type) + sizeof(ci)))
		return !0;
	type = EVTR_TYPE_CPUINFO;
	if (evtr_write(evtr, &type, sizeof(type))) {
		return !0;
	}
	ci.cpu = ev->cpu;
	ci.freq = ev->cpuinfo.freq;
	if (evtr_dump_avoid_boundary(evtr, sizeof(ci)))
		return !0;
	if (evtr_write(evtr, &ci, sizeof(ci))) {
		return !0;
	}
	if (evtr_dump_pad(evtr))
		return !0;
	return 0;
}

int
evtr_rewind(evtr_t evtr)
{
	assert((evtr->flags & EVTRF_WR) == 0);
	evtr->bytes = 0;
	if (fseek(evtr->f, 0, SEEK_SET)) {
		evtr->err = errno;
		return !0;
	}
	return 0;
}

int
evtr_dump_event(evtr_t evtr, evtr_event_t ev)
{
	switch (ev->type) {
	case EVTR_TYPE_PROBE:
		return evtr_dump_probe(evtr, ev);
	case EVTR_TYPE_SYSINFO:
		return evtr_dump_sysinfo(evtr, ev);
	case EVTR_TYPE_CPUINFO:
		return evtr_dump_cpuinfo(evtr, ev);
	}
	evtr->errmsg = "unknown event type";
	return !0;
}

static
evtr_t
evtr_alloc(FILE *f)
{
	evtr_t evtr;
	if (!(evtr = malloc(sizeof(*evtr)))) {
		return NULL;
	}

	evtr->f = f;
	evtr->err = 0;
	evtr->errmsg = NULL;
	evtr->bytes = 0;
	return evtr;
}

static int evtr_next_event(evtr_t, evtr_event_t);

evtr_t
evtr_open_read(FILE *f)
{
	evtr_t evtr;
	struct evtr_event ev;
	int i;

	if (!(evtr = evtr_alloc(f))) {
		return NULL;
	}
	evtr->flags = 0;
	for (i = 0; i < (EVTR_NS_MAX - 1); ++i) {
		RB_INIT(&evtr->maps[i].root);
	}
	RB_INIT(&evtr->fmtmap.root);
	RB_INIT(&evtr->threads.root);
	evtr->cpus = NULL;
	evtr->ncpus = 0;
	/*
	 * Load the first event so we can pick up any
	 * sysinfo entries.
	 */
	if (evtr_next_event(evtr, &ev)) {
		goto free_evtr;
	}
	if (evtr_rewind(evtr))
		goto free_evtr;
	return evtr;
free_evtr:
	free(evtr);
	return NULL;
}

evtr_t
evtr_open_write(FILE *f)
{
	evtr_t evtr;
	int i, j;

	if (!(evtr = evtr_alloc(f))) {
		return NULL;
	}

	evtr->flags = EVTRF_WR;
	if (!(evtr->fmts = strhash_new()))
		goto free_evtr;
	for (i = 0; i < EVTR_NS_MAX; ++i) {
		evtr->strings[i] = strhash_new();
		if (!evtr->strings[i]) {
			for (j = 0; j < i; ++j) {
				strhash_destroy(evtr->strings[j]);
			}
			goto free_fmts;
		}
	}

	return evtr;
free_fmts:
	strhash_destroy(evtr->fmts);
free_evtr:
	free(evtr);
	return NULL;
}

static
void
hashtab_destroy(struct hashtab *h)
{
	struct hashentry *ent, *next;
	int i;
	for (i = 0; i < NR_BUCKETS; ++i) {
		for (ent = h->buckets[i]; ent; ent = next) {
			next = ent->next;
			free(ent);
		}
	}
	free(h);
}

void
evtr_close(evtr_t evtr)
{
	int i;

	if (evtr->flags & EVTRF_WR) {
		hashtab_destroy(&evtr->fmts->tab);
		for (i = 0; i < EVTR_NS_MAX - 1; ++i)
			hashtab_destroy(&evtr->strings[i]->tab);
	} else {
		id_tree_free(&evtr->fmtmap.root);
		for (i = 0; i < EVTR_NS_MAX - 1; ++i) {
			id_tree_free(&evtr->maps[i].root);
		}
	}
	free(evtr);
}

static
int
evtr_read(evtr_t evtr, void *buf, size_t size)
{
	assert(size > 0);
	assert_foff_in_sync(evtr);
	printd(IO, "evtr_read at %#jx, %zd bytes\n", evtr->bytes, size);
	if (fread(buf, size, 1, evtr->f) != 1) {
		if (feof(evtr->f)) {
			evtr->errmsg = "incomplete record";
		} else {
			evtr->errmsg = strerror(errno);
		}
		return !0;
	}
	evtr->bytes += size;
	assert_foff_in_sync(evtr);
	return 0;
}

static
int
evtr_load_fmt(evtr_query_t q, char *buf)
{
	evtr_t evtr = q->evtr;
	struct fmt_event_header *evh = (struct fmt_event_header *)buf;
	struct event_fmt *fmt;
	char *subsys = NULL, *fmtstr;

	if (!(fmt = malloc(sizeof(*fmt)))) {
		evtr->err = errno;
		return !0;
	}
	if (evtr_read(evtr, buf + sizeof(struct trace_event_header),
		      sizeof(*evh) - sizeof(evh->eh))) {
		goto free_fmt;
	}
	assert(!evh->subsys_len);
	if (evh->subsys_len) {
		if (!(subsys = malloc(evh->subsys_len))) {
			evtr->err = errno;
			goto free_fmt;
		}
		if (evtr_read(evtr, subsys, evh->subsys_len)) {
			goto free_subsys;
		}
		fmt->subsys = subsys;
	} else {
		fmt->subsys = "";
	}
	if (!(fmtstr = malloc(evh->fmt_len + 1))) {
		evtr->err = errno;
		goto free_subsys;
	}
	if (evtr_read(evtr, fmtstr, evh->fmt_len)) {
		goto free_fmtstr;
	}
	fmtstr[evh->fmt_len] = '\0';
	fmt->fmt = fmtstr;

	printd(DS, "fmt_map_insert (%d, %s)\n", evh->id, fmt->fmt);
	evtr->err = fmt_map_insert(&evtr->fmtmap.root, fmt, evh->id);
	switch (evtr->err) {
	case ENOMEM:
		evtr->errmsg = "out of memory";
		break;
	case EEXIST:
		evtr->errmsg = "redefinition of an id to a "
			"different format (corrupt input)";
		break;
	default:
		;
	}
	return evtr->err;

free_fmtstr:
	free(fmtstr);
free_subsys:
	if (subsys)
		free(subsys);
free_fmt:
	free(fmt);
	return !0;
}

static
int
evtr_load_string(evtr_t evtr, char *buf)
{
	char sbuf[PATH_MAX + 1];
	struct string_event_header *evh = (struct string_event_header *)buf;

	if (evtr_read(evtr, buf + sizeof(struct trace_event_header),
		      sizeof(*evh) - sizeof(evh->eh))) {
		return !0;
	}
	if (evh->len > PATH_MAX) {
		evtr->errmsg = "string too large (corrupt input)";
		return !0;
	}
	if (evh->len && evtr_read(evtr, sbuf, evh->len)) {
		return !0;
	}
	sbuf[evh->len] = 0;
	if (evh->ns >= EVTR_NS_MAX) {
		evtr->errmsg = "invalid namespace (corrupt input)";
		return !0;
	}
	validate_string(sbuf);
	printd(DS, "evtr_load_string:ns %d id %d : \"%s\"\n", evh->ns, evh->id,
	       sbuf);
	evtr->err = string_map_insert(&evtr->maps[evh->ns - 1].root, sbuf, evh->id);
	switch (evtr->err) {
	case ENOMEM:
		evtr->errmsg = "out of memory";
		break;
	case EEXIST:
		evtr->errmsg = "redefinition of an id to a "
			"different string (corrupt input)";
		break;
	default:
		;
	}
	return 0;
}

static
int
evtr_skip(evtr_t evtr, off_t bytes)
{
	if (fseek(evtr->f, bytes, SEEK_CUR)) {
		evtr->err = errno;
		evtr->errmsg = strerror(errno);
		return !0;
	}
	evtr->bytes += bytes;
	return 0;
}

/*
 * Make sure q->buf is at least len bytes
 */
static
int
evtr_query_reserve_buf(struct evtr_query *q, int len)
{
	void *tmp;

	if (q->bufsize >= len)
		return 0;
	if (!(tmp = realloc(q->buf, len)))
		return !0;
	q->buf = tmp;
	q->bufsize = len;
	return 0;
}

static
int
evtr_load_probe(evtr_t evtr, evtr_event_t ev, char *buf, struct evtr_query *q)
{
	struct probe_event_header *evh = (struct probe_event_header *)buf;
	struct cpu *cpu;

	if (evtr_read(evtr, buf + sizeof(struct trace_event_header),
		      sizeof(*evh) - sizeof(evh->eh)))
		return !0;
	memset(ev, '\0', sizeof(*ev));
	ev->ts = evh->eh.ts;
	ev->type = EVTR_TYPE_PROBE;
	ev->line = evh->line;
	ev->cpu = evh->cpu;
	if ((cpu = evtr_cpu(evtr, evh->cpu))) {
		ev->td = cpu->td;
	} else {
		ev->td = NULL;
	}
	if (evh->file) {
		ev->file = string_map_find(
			&evtr->maps[EVTR_NS_PATH - 1].root,
			evh->file);
		if (!ev->file) {
			evtr->errmsg = "unknown id for file path";
			evtr->err = !0;
			ev->file = "<unknown>";
		} else {
			validate_string(ev->file);
		}
	} else {
		ev->file = "<unknown>";
	}
	if (evh->fmt) {
		const struct event_fmt *fmt;
		if (!(fmt = fmt_map_find(&evtr->fmtmap.root, evh->fmt))) {
			evtr->errmsg = "unknown id for event fmt";
			evtr->err = !0;
			ev->fmt = NULL;
		} else {
			ev->fmt = fmt->fmt;
			validate_string(fmt->fmt);
		}
	}
	if (evh->datalen) {
		if (evtr_query_reserve_buf(q, evh->datalen + 1)) {
			evtr->err = ENOMEM;
		} else if (!evtr_read(evtr, q->buf, evh->datalen)) {
			struct replace_ctx replctx = {
				.evtr = evtr,
				.ts = ev->ts,
			};
			assert(ev->fmt);

			ev->fmtdata = q->buf;
			/*
			 * If the format specifies any string pointers, there
			 * is a string id stored in the fmtdata. Look it up
			 * and replace it with a string pointer before
			 * returning it to the user.
			 */
			if (mangle_string_ptrs(ev->fmt, __DECONST(uint8_t *,
								  ev->fmtdata),
					       replace_strid, &replctx) < 0)
				return evtr->err;
			if (evtr->err)
				return evtr->err;
			((char *)ev->fmtdata)[evh->datalen] = '\0';
			ev->fmtdatalen = evh->datalen;
		}
	}
	evtr_run_callbacks(ev, q);
	return evtr->err;
}

static
int
evtr_skip_to_record(evtr_t evtr)
{
	int skip;
	
	skip = REC_ALIGN - (evtr->bytes % REC_ALIGN);
	if (skip > 0) {
		if (fseek(evtr->f, skip, SEEK_CUR)) {
			evtr->err = errno;
			evtr->errmsg = strerror(errno);
			return !0;
		}
		evtr->bytes += skip;
	}
	return 0;
}

static
int
evtr_load_sysinfo(evtr_t evtr)
{
	uint16_t ncpus;
	int i;

	if (evtr_read(evtr, &ncpus, sizeof(ncpus))) {
		return !0;
	}
	if (evtr->cpus)
		return 0;
	evtr->cpus = malloc(ncpus * sizeof(struct cpu));
	if (!evtr->cpus) {
		evtr->err = ENOMEM;
		return !0;
	}
	evtr->ncpus = ncpus;
	for (i = 0; i < ncpus; ++i) {
		evtr->cpus[i].td = NULL;
		evtr->cpus[i].freq = -1.0;
	}
	return 0;
}

static
int
evtr_load_cpuinfo(evtr_t evtr)
{
	struct cpuinfo_event_header cih;
	struct cpu *cpu;

	if (evtr_read(evtr, &cih, sizeof(cih))) {
		return !0;
	}
	if (cih.freq < 0.0) {
		evtr->errmsg = "cpu freq is negative";
		evtr->err = EINVAL;
		return !0;
	}
	/*
	 * Notice that freq is merely a multiplier with
	 * which we convert a timestamp to seconds; if
	 * ts is not in cycles, freq is not the frequency.
	 */
	if (!(cpu = evtr_cpu(evtr, cih.cpu))) {
		evtr->errmsg = "freq for invalid cpu";
		evtr->err = EINVAL;
		return !0;
	}
	cpu->freq = cih.freq;
	return 0;
}

static
int
_evtr_next_event(evtr_t evtr, evtr_event_t ev, struct evtr_query *q)
{
	char buf[MAX_EVHDR_SIZE];
	int ret, err;
	struct trace_event_header *evhdr = (struct trace_event_header *)buf;

	for (ret = 0; !ret;) {
		if (q->flags & EVTRQF_PENDING) {
			q->off = evtr->bytes;
			memcpy(ev, &q->pending_event, sizeof(*ev));
			q->flags &= ~EVTRQF_PENDING;
			return 0;
		}
		if (evtr_read(evtr, &evhdr->type, 1)) {
			if (feof(evtr->f)) {
				evtr->errmsg = NULL;
				evtr->err = 0;
				return -1;
			}
			return !0;
		}
		/*
		 * skip pad records -- this will only happen if there's a
		 * variable sized record close to the boundary
		 */
		if (evhdr->type == EVTR_TYPE_PAD) {
			evtr_skip_to_record(evtr);
			continue;
		}
		if (evhdr->type == EVTR_TYPE_SYSINFO) {
			evtr_load_sysinfo(evtr);
			continue;
		} else if (evhdr->type == EVTR_TYPE_CPUINFO) {
			evtr_load_cpuinfo(evtr);
			continue;
		}
		if (evtr_read(evtr, buf + 1, sizeof(*evhdr) - 1))
			return feof(evtr->f) ? -1 : !0;
		switch (evhdr->type) {
		case EVTR_TYPE_PROBE:
			if ((err = evtr_load_probe(evtr, ev, buf, q))) {
				if (err == -1) {
					/* no match */
					ret = 0;
				} else {
					return !0;
				}
			} else {
				ret = !0;
			}
			break;
		case EVTR_TYPE_STR:
			if (evtr_load_string(evtr, buf)) {
				return !0;
			}
			break;
		case EVTR_TYPE_FMT:
			if (evtr_load_fmt(q, buf)) {
				return !0;
			}
			break;
		default:
			evtr->err = !0;
			evtr->errmsg = "unknown event type (corrupt input?)";
			return !0;
		}
		evtr_skip_to_record(evtr);
		if (ret) {
			if (!evtr_match_filters(q, ev)) {
				ret = 0;
				continue;
			}
			q->off = evtr->bytes;
			return 0;
		}
	}
	/* can't get here */
	return !0;
}

static
int
evtr_next_event(evtr_t evtr, evtr_event_t ev)
{
	struct evtr_query *q;
	int ret;

	if (!(q = evtr_query_init(evtr, NULL, 0))) {
		evtr->err = ENOMEM;
		return !0;
	}
	ret = _evtr_next_event(evtr, ev, q);
	evtr_query_destroy(q);
	return ret;
}

int
evtr_last_event(evtr_t evtr, evtr_event_t ev)
{
	struct stat st;
	int fd;
	off_t last_boundary;

	if (evtr_error(evtr))
		return !0;

	fd = fileno(evtr->f);
	if (fstat(fd, &st))
		return !0;
	/*
	 * This skips pseudo records, so we can't provide
	 * an event with all fields filled in this way.
	 * It's doable, just needs some care. TBD.
	 */
	if (0 && (st.st_mode & S_IFREG)) {
		/*
		 * Skip to last boundary, that's the closest to the EOF
		 * location that we are sure contains a header so we can
		 * pick up the stream.
		 */
		last_boundary = (st.st_size / REC_BOUNDARY) * REC_BOUNDARY;
		/* XXX: ->bytes should be in query */
		assert(evtr->bytes == 0);
		evtr_skip(evtr, last_boundary);
	}


	/*
	 * If we can't seek, we need to go through the whole file.
	 * Since you can't seek back, this is pretty useless unless
	 * you really are interested only in the last event.
	 */
	while (!evtr_next_event(evtr, ev))
		;
	if (evtr_error(evtr))
		return !0;
	evtr_rewind(evtr);
	return 0;
}

struct evtr_query *
evtr_query_init(evtr_t evtr, evtr_filter_t filt, int nfilt)
{
	struct evtr_query *q;
	int i;

	if (!(q = malloc(sizeof(*q)))) {
		return q;
	}
	q->bufsize = 2;
	if (!(q->buf = malloc(q->bufsize))) {
		goto free_q;
	}
	if (!(q->symtab = symtab_new()))
		goto free_buf;
	q->evtr = evtr;
	q->off = 0;
	q->filt = filt;
	q->nfilt = nfilt;
	TAILQ_INIT(&q->unresolved_filtq);
	q->nmatched = 0;
	q->cbs = NULL;
	q->ncbs = 0;
	q->flags = 0;
	memset(&q->pending_event, '\0', sizeof(q->pending_event));
	if (evtr_register_callback(q, &thread_creation_callback, q)) {
		goto free_symtab;
	}
	if (evtr_register_callback(q, &thread_switch_callback, q)) {
		goto free_cbs;
	}
	if (evtr_query_needs_parsing(q) &&
	    evtr_register_callback(q, &parse_callback, q)) {
		goto free_cbs;
	}

	for (i = 0; i < nfilt; ++i) {
		filt[i].flags = 0;
		if (filt[i].fmt == NULL)
			continue;
		if (evtr_filter_register(q, &filt[i])) {
			evtr_deregister_filters(q, filt, i);
			goto free_symtab;
		}
	}

	return q;
free_cbs:
	evtr_deregister_callbacks(q);
free_symtab:
	symtab_destroy(q->symtab);
free_buf:
	free(q->buf);
free_q:
	free(q);
	return NULL;
}

void
evtr_query_destroy(struct evtr_query *q)
{
	evtr_deregister_filters(q, q->filt, q->nfilt);
		
	free(q->buf);
	free(q);
}

int
evtr_query_next(struct evtr_query *q, evtr_event_t ev)
{
	if (evtr_query_error(q))
		return !0;
	/* we may support that in the future */
	if (q->off != q->evtr->bytes) {
		q->errmsg = "evtr/query offset mismatch";
		return !0;
	}
	return _evtr_next_event(q->evtr, ev, q);
}

int
evtr_ncpus(evtr_t evtr)
{
	return evtr->ncpus;
}

int
evtr_cpufreqs(evtr_t evtr, double *freqs)
{
	int i;

	if (!freqs)
		return EINVAL;
	for (i = 0; i < evtr->ncpus; ++i) {
		freqs[i] = evtr->cpus[i].freq;
	}
	return 0;
}

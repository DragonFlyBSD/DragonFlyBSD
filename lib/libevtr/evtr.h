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

#ifndef EVTR_H
#define EVTR_H

#include <stdint.h>
#include <stdio.h>
#include <sys/queue.h>
/* XXX: remove */
#include <sys/tree.h>

enum {
	EVTR_TYPE_PAD = 0x0,
	EVTR_TYPE_PROBE = 0x1,
	EVTR_TYPE_STR = 0x2,
	EVTR_TYPE_FMT = 0x3,
	EVTR_TYPE_SYSINFO = 0x4,
	EVTR_TYPE_CPUINFO = 0x5,
	EVTR_TYPE_STMT = 0x6,
};

struct hashtab;
typedef struct evtr_variable_value {
	enum evtr_value_type {
		EVTR_VAL_NIL,
		EVTR_VAL_INT,
		EVTR_VAL_STR,
		EVTR_VAL_HASH,
		EVTR_VAL_CTOR,
	} type;
	union {
		uintmax_t num;
		const char *str;
		struct hashtab *hashtab;
		const struct symtab *symtab;
		struct {
			const char *name;
			TAILQ_HEAD(, evtr_variable_value) args;
		} ctor;
	};
	TAILQ_ENTRY(evtr_variable_value) link;
} *evtr_variable_value_t;

typedef struct evtr_variable {
	const char *name;
	struct evtr_variable_value val;
} *evtr_var_t;

struct evtr_thread {
	RB_ENTRY(evtr_thread) rb_node;
	void *id;
	const char *comm;
	/* available for the user of the library, NULL if not set */
	void *userdata;
};

/*
 * This structure is used for interchange of data with
 * the user of the library
 */
typedef struct evtr_event {
	uint8_t type;	/* EVTR_TYPE_* */
	uint64_t ts;	/* timestamp. Must be nondecreasing */
	union {
		uint16_t ncpus;	/* EVTR_TYPE_SYSINFO */
		struct evtr_cpuinfo { /* EVTR_TYPE_CPUINFO */
			double freq;
		} cpuinfo;
		struct evtr_stmt {
			const struct evtr_variable *var;
			enum evtr_op {
				EVTR_OP_SET,
			} op;
			struct evtr_variable_value *val;
		} stmt;
	};
	/*
	 * Pointer to filename. NULL if n/a.
	 * For an event returned by the library,
	 * it is a pointer to storage allocated
	 * by the library that will be around
	 * until the call to evtr_close.
	 */
	const char *file;
	/* Same as above */ 
	const char *func;
	/* line number. 0 if n/a */
	uint16_t line;
	/*
	 * Format string, also used to identify
	 * the event. Ownership rules are the same
	 * as for file.
	 */
	const char *fmt;
	/*
	 * Data corresponding to the format string.
	 * For an event returned by the library,
	 * it is a pointer to an internal buffer
	 * that becomes invalid when the next record
	 * is returned. If the user wants to keep this
	 * data around, they must copy it.
	 */
	const void *fmtdata;
	/* Length of fmtdata */
	int fmtdatalen;
	/* Cpu on which the event occured */
	uint8_t cpu;
	/*
	 * Thread, which generated the event (if applicable). The
	 * storage pointed to belongs to the library and may (even
	 * though it's highly unlikely) point to a different thread
	 * in the future. The user needs to copy it if they want
	 * this data.
	 */
	struct evtr_thread *td;
} *evtr_event_t;

/*
 * Specifies which conditions to filter query results
 * with. It is modified by the library and should
 * not be touched after initialization.
 */
typedef struct evtr_filter {
	int flags;	/* must be initialized to 0 */
	/*
	 * Which cpu we are interested in. -1 means
	 * any cpu. XXX: use mask? (note we could just
	 * do that internally)
	 */
	int cpu;
	/* what event type we're interested in */
	int ev_type;
	/*
	 * If the user sets fmt, only events with a format
	 * string identical to the one specified will be
	 * returned. This field is modified by the library.
	 */
	union {
		const char *fmt;
		int fmtid;
		const char *var;
	};
} *evtr_filter_t;

struct evtr_query;
struct evtr;
typedef struct evtr *evtr_t;
typedef struct evtr_query *evtr_query_t;

evtr_t evtr_open_read(FILE *);
evtr_t evtr_open_write(FILE *);
void evtr_close(evtr_t);
int evtr_dump_event(evtr_t, evtr_event_t);
int evtr_error(evtr_t);
const char * evtr_errmsg(evtr_t);
int evtr_query_error(evtr_query_t);
const char * evtr_query_errmsg(evtr_query_t);
void evtr_event_data(evtr_event_t, char *, size_t);
struct evtr_query * evtr_query_init(evtr_t, evtr_filter_t, int);
void evtr_query_destroy(struct evtr_query *);
int evtr_query_next(struct evtr_query *, evtr_event_t);
int evtr_last_event(evtr_t, evtr_event_t);
int evtr_rewind(evtr_t);

int evtr_ncpus(evtr_t);
int evtr_cpufreqs(evtr_t, double *);
void evtr_set_debug(const char *);


#endif	/* EVTR_H */

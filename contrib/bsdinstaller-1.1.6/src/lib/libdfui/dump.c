/*
 * Copyright (c)2004 Cat's Eye Technologies.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * 
 *   Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * 
 *   Neither the name of Cat's Eye Technologies nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission. 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

/*
 * dump.c
 * $Id: dump.c,v 1.5 2005/02/06 19:53:19 cpressey Exp $
 * Debugging functions for libdfui.
 * These functions are just stubs when libdfui is built without DEBUG.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define NEEDS_DFUI_STRUCTURE_DEFINITIONS
#include "dfui.h"
#undef NEEDS_DFUI_STRUCTURE_DEFINITIONS
#include "dump.h"

FILE *dfui_debug_file;

#ifdef DEBUG
#define __debug_only
#else
#define __debug_only __unused
#endif

void
dfui_info_dump(const struct dfui_info *info __debug_only)
{
#ifdef DEBUG
	fprintf(dfui_debug_file, "+ INFO:\n");
	if (info == NULL) {
		fprintf(dfui_debug_file, "  *NULL*");
		return;
	}
	fprintf(dfui_debug_file, "  name:       %s\n", info->name);
	fprintf(dfui_debug_file, "  short_desc: %s\n", info->short_desc);
	fprintf(dfui_debug_file, "  long_desc:  %s\n", info->long_desc);
#endif
}

void
dfui_option_dump(const struct dfui_option *o __debug_only)
{
#ifdef DEBUG
	fprintf(dfui_debug_file, "+ OPTION:\n");
	if (o == NULL) {
		fprintf(dfui_debug_file, "  *NULL*");
		return;
	}
	fprintf(dfui_debug_file, "value: %s\n", o->value);
#endif
}

void
dfui_field_dump(const struct dfui_field *fi __debug_only)
{
#ifdef DEBUG
	struct dfui_option *o;

	fprintf(dfui_debug_file, "+ FIELD:\n");
	if (fi == NULL) {
		fprintf(dfui_debug_file, "  *NULL*");
		return;
	}
	fprintf(dfui_debug_file, "id: %s\n", fi->id);
	dfui_info_dump(fi->info);
	for (o = fi->option_head; o != NULL; o = o->next) {
		dfui_option_dump(o);
	}
#endif
}

void
dfui_action_dump(const struct dfui_action *a __debug_only)
{
#ifdef DEBUG
	fprintf(dfui_debug_file, "+ ACTION:\n");
	if (a == NULL) {
		fprintf(dfui_debug_file, "  *NULL*");
		return;
	}
	fprintf(dfui_debug_file, "id: %s\n", a->id);
	dfui_info_dump(a->info);
	/* parameters */
#endif
}

void
dfui_celldata_dump(const struct dfui_celldata *c __debug_only)
{
#ifdef DEBUG
	if (c == NULL) {
		fprintf(dfui_debug_file, "*NULL* ");
		return;
	}
	fprintf(dfui_debug_file, "{%s = %s}", c->field_id, c->value);
#endif
}

void
dfui_dataset_dump(const struct dfui_dataset *ds __debug_only)
{
#ifdef DEBUG
	struct dfui_celldata *c;

	fprintf(dfui_debug_file, "+ DATASET:\n");
	if (ds == NULL) {
		fprintf(dfui_debug_file, "  *NULL*");
		return;
	}

	for (c = ds->celldata_head; c != NULL; c = c->next) {
		dfui_celldata_dump(c);
	}
	fprintf(dfui_debug_file, "\n");
#endif
}


void
dfui_form_dump(const struct dfui_form *f __debug_only)
{
#ifdef DEBUG
	struct dfui_field *fi;
	struct dfui_action *a;
	struct dfui_dataset *ds;

	fprintf(dfui_debug_file, "FORM ------\n");
	if (f == NULL) {
		fprintf(dfui_debug_file, "*NULL*");
		return;
	}
	
	fprintf(dfui_debug_file, "id: %s\n", f->id);
	dfui_info_dump(f->info);

	fprintf(dfui_debug_file, "multiple: %d\n", f->multiple);
	fprintf(dfui_debug_file, "extensible: %d\n", f->extensible);
	
	for (fi = f->field_head; fi != NULL; fi = fi->next) {
		dfui_field_dump(fi);
	}
	for (a = f->action_head; a != NULL; a = a->next) {
		dfui_action_dump(a);
	}
	for (ds = f->dataset_head; ds != NULL; ds = ds->next) {
		dfui_dataset_dump(ds);
	}
#endif
}

void
dfui_response_dump(const struct dfui_response *r __debug_only)
{
#ifdef DEBUG
	struct dfui_dataset *ds;

	fprintf(dfui_debug_file, "RESPONSE ------\n");
	if (r == NULL) {
		fprintf(dfui_debug_file, "*NULL*");
		return;
	}
	fprintf(dfui_debug_file, "form id: %s\n", r->form_id);
	fprintf(dfui_debug_file, "action id: %s\n", r->action_id);

	for (ds = r->dataset_head; ds != NULL; ds = ds->next) {
		dfui_dataset_dump(ds);
	}
#endif
}

void
dfui_progress_dump(const struct dfui_progress *pr __debug_only)
{
#ifdef DEBUG
	fprintf(dfui_debug_file, "PROGRESS ------\n");
	if (pr == NULL) {
		fprintf(dfui_debug_file, "*NULL*");
		return;
	}

	/* fprintf(dfui_debug_file, "id: %s\n", pr->id); */
	dfui_info_dump(pr->info);

	fprintf(dfui_debug_file, "amount: %d\n", pr->amount);
#endif
}

void
dfui_debug(const char *fmt __debug_only, ...)
{
#ifdef DEBUG
	va_list args;

	va_start(args, fmt);
	vfprintf(dfui_debug_file, fmt, args);
	va_end(args);
#endif
}

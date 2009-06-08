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
 * encode.c
 * $Id: encode.c,v 1.12 2005/02/07 06:40:00 cpressey Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libaura/buffer.h>

#define	NEEDS_DFUI_STRUCTURE_DEFINITIONS
#include "dfui.h"
#undef	NEEDS_DFUI_STRUCTURE_DEFINITIONS
#include "encoding.h"

/*** BASIC TYPES ***/

void
dfui_encode_string(struct aura_buffer *e, const char *str)
{
	char fmt[16];

	if (str == NULL) {
		aura_buffer_cat(e, "0:");
	} else {
		snprintf(fmt, 16, "%zu", strlen(str));
		aura_buffer_cat(e, fmt);
		aura_buffer_cat(e, ":");
		aura_buffer_cat(e, str);
	}
}

void
dfui_encode_int(struct aura_buffer *e, int i)
{
	char fmt[16];

	snprintf(fmt, 16, "%d", i);
	aura_buffer_cat(e, fmt);
	aura_buffer_cat(e, " ");
}

void
dfui_encode_bool(struct aura_buffer *e, int b)
{
	if (b)
		aura_buffer_cat(e, "Y");
	else
		aura_buffer_cat(e, "N");
}

/*** FORM TYPES ***/

void
dfui_encode_info(struct aura_buffer *e, struct dfui_info *i)
{
	dfui_encode_string(e, i->name);
	dfui_encode_string(e, i->short_desc);
	dfui_encode_string(e, i->long_desc);
}

void
dfui_encode_form(struct aura_buffer *e, struct dfui_form *f)
{
	aura_buffer_cat(e, "F{");
	dfui_encode_string(e, f->id);
	dfui_encode_info(e, f->info);

	dfui_encode_bool(e, f->multiple);
	dfui_encode_bool(e, f->extensible);

	dfui_encode_fields(e, f->field_head);
	dfui_encode_actions(e, f->action_head);
	dfui_encode_datasets(e, f->dataset_head);
	dfui_encode_properties(e, f->property_head);

	aura_buffer_cat(e, "}");
}

void
dfui_encode_fields(struct aura_buffer *e, struct dfui_field *head)
{
	struct dfui_field *fi;

	aura_buffer_cat(e, "f{");
	for (fi = head; fi != NULL; fi = fi->next) {
		dfui_encode_field(e, fi);
	}
	aura_buffer_cat(e, "}");
}

void
dfui_encode_field(struct aura_buffer *e, struct dfui_field *fi)
{
	dfui_encode_string(e, fi->id);
	dfui_encode_info(e, fi->info);
	dfui_encode_options(e, fi->option_head);
	dfui_encode_properties(e, fi->property_head);
}

void
dfui_encode_options(struct aura_buffer *e, struct dfui_option *head)
{
	struct dfui_option *o;

	aura_buffer_cat(e, "O{");
	for (o = head; o != NULL; o = o->next) {
		dfui_encode_option(e, o);
	}
	aura_buffer_cat(e, "}");
}

void
dfui_encode_option(struct aura_buffer *e, struct dfui_option *o)
{
	dfui_encode_string(e, o->value);
}

void
dfui_encode_actions(struct aura_buffer *e, struct dfui_action *head)
{
	struct dfui_action *a;

	aura_buffer_cat(e, "a{");
	for (a = head; a != NULL; a = a->next) {
		dfui_encode_action(e, a);
	}
	aura_buffer_cat(e, "}");
}

void
dfui_encode_action(struct aura_buffer *e, struct dfui_action *a)
{
	dfui_encode_string(e, a->id);
	dfui_encode_info(e, a->info);
	dfui_encode_properties(e, a->property_head);
}

void
dfui_encode_datasets(struct aura_buffer *e, struct dfui_dataset *head)
{
	struct dfui_dataset *ds;

	aura_buffer_cat(e, "D{");
	for (ds = head; ds != NULL; ds = ds->next) {
		dfui_encode_dataset(e, ds);
	}
	aura_buffer_cat(e, "}");
}

void
dfui_encode_dataset(struct aura_buffer *e, struct dfui_dataset *ds)
{
	dfui_encode_celldatas(e, ds->celldata_head);
}

void
dfui_encode_celldatas(struct aura_buffer *e, struct dfui_celldata *c)
{
	aura_buffer_cat(e, "d{");
	while (c != NULL) {
		dfui_encode_celldata(e, c);
		c = c->next;
	}
	aura_buffer_cat(e, "}");
}

void
dfui_encode_celldata(struct aura_buffer *e, struct dfui_celldata *c)
{
	dfui_encode_string(e, c->field_id);
	dfui_encode_string(e, c->value);
}

void
dfui_encode_properties(struct aura_buffer *e, struct dfui_property *h)
{
	aura_buffer_cat(e, "p{");
	while (h != NULL) {
		dfui_encode_property(e, h);
		h = h->next;
	}
	aura_buffer_cat(e, "}");
}

void
dfui_encode_property(struct aura_buffer *e, struct dfui_property *h)
{
	dfui_encode_string(e, h->name);
	dfui_encode_string(e, h->value);
}

void
dfui_encode_response(struct aura_buffer *e, struct dfui_response *r)
{
	if (r) {
		aura_buffer_cat(e, "R{");
		dfui_encode_string(e, r->form_id);
		dfui_encode_string(e, r->action_id);
		dfui_encode_datasets(e, r->dataset_head);
		aura_buffer_cat(e, "}");
	}
}

void
dfui_encode_progress(struct aura_buffer *e, struct dfui_progress *pr)
{
	dfui_encode_info(e, pr->info);
	dfui_encode_int(e, pr->amount);
	dfui_encode_int(e, pr->streaming);
	dfui_encode_string(e, pr->msg_line);
}

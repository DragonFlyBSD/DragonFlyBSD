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
 * decode.c
 * $Id: decode.c,v 1.11 2005/02/07 06:39:59 cpressey Exp $
 */

#include <ctype.h>
#include <stdlib.h>

#include <libaura/mem.h>
#include <libaura/buffer.h>

#define	NEEDS_DFUI_STRUCTURE_DEFINITIONS
#include "dfui.h"
#undef	NEEDS_DFUI_STRUCTURE_DEFINITIONS
#include "encoding.h"
#include "dump.h"

/*** BASIC TYPES ***/

/*
 * This function returns a newly-allocated string.  It is the
 * caller's responsibility that it be free()'ed.
 */
char *
dfui_decode_string(struct aura_buffer *e)
{
	char *str;
	int i = 0;
	int len = 0;

	while (isdigit(aura_buffer_peek_char(e)) && !aura_buffer_eof(e)) {
		len = len * 10 + aura_buffer_scan_char(e) - '0';
	}

	str = aura_malloc(len + 1, "decoded string");

	if (!aura_buffer_expect(e, ":")) return(NULL);
	while (len > 0 && !aura_buffer_eof(e)) {
		str[i++] = aura_buffer_scan_char(e);
		len--;
	}

	str[i] = '\0';

	return(str);
}

int
dfui_decode_int(struct aura_buffer *e)
{
	int x = 0;

	while (isdigit(aura_buffer_peek_char(e)) && !aura_buffer_eof(e)) {
		x = x * 10 + aura_buffer_scan_char(e) - '0';
	}
	if (aura_buffer_expect(e, " ")) {
		return(x);
	} else {
		return(0);
	}
}

int
dfui_decode_bool(struct aura_buffer *e)
{
	char c;

	c = aura_buffer_scan_char(e);

	if (c == 'Y')
		return(1);
	else if (c == 'N')
		return(0);
	else /* XXX ??? error */
		return(0);
}

/*** FORM TYPES ***/

struct dfui_info *
dfui_decode_info(struct aura_buffer *e)
{
	char *name, *short_desc, *long_desc;
	struct dfui_info *i;

	name = dfui_decode_string(e);
	short_desc = dfui_decode_string(e);
	long_desc = dfui_decode_string(e);

	i = dfui_info_new(name, short_desc, long_desc);

	free(name);
	free(short_desc);
	free(long_desc);

	return(i);
}

struct dfui_field *
dfui_decode_field(struct aura_buffer *e)
{
	char *id;
	struct dfui_info *i;
	struct dfui_field *fi;

	id = dfui_decode_string(e);
	i = dfui_decode_info(e);

	fi = dfui_field_new(id, i);
	fi->option_head = dfui_decode_options(e);
	fi->property_head = dfui_decode_properties(e);
	free(id);

	return(fi);
}

struct dfui_field *
dfui_decode_fields(struct aura_buffer *e)
{
	struct dfui_field *head = NULL, *fi;

	if (!aura_buffer_expect(e, "f{")) return(NULL);
	while (aura_buffer_peek_char(e) != '}') {
		fi = dfui_decode_field(e);
		fi->next = head;
		head = fi;
	}
	aura_buffer_expect(e, "}");

	return(head);
}

struct dfui_option *
dfui_decode_option(struct aura_buffer *e)
{
	char *value;

	value = dfui_decode_string(e);

	return(dfui_option_new(value));
}

struct dfui_option *
dfui_decode_options(struct aura_buffer *e)
{
	struct dfui_option *head = NULL, *o;

	if (!aura_buffer_expect(e, "O{")) return(NULL);
	while (aura_buffer_peek_char(e) != '}') {
		o = dfui_decode_option(e);
		o->next = head;
		head = o;
	}
	aura_buffer_expect(e, "}");

	return(head);
}

struct dfui_action *
dfui_decode_action(struct aura_buffer *e)
{
	char *id;
	struct dfui_info *i;
	struct dfui_action *a;

	id = dfui_decode_string(e);
	i = dfui_decode_info(e);
	a = dfui_action_new(id, i);
	a->property_head = dfui_decode_properties(e);
	free(id);

	return(a);
}

struct dfui_action *
dfui_decode_actions(struct aura_buffer *e)
{
	struct dfui_action *head = NULL, *a;

	if (!aura_buffer_expect(e, "a{")) return(NULL);
	while (aura_buffer_peek_char(e) != '}') {
		a = dfui_decode_action(e);
		a->next = head;
		head = a;
	}
	aura_buffer_expect(e, "}");

	return(head);
}

struct dfui_celldata *
dfui_decode_celldata(struct aura_buffer *e)
{
	char *field_id;
	char *value;
	struct dfui_celldata *c;

	field_id = dfui_decode_string(e);
	value = dfui_decode_string(e);

	c = dfui_celldata_new(field_id, value);

	free(field_id);
	free(value);

	return(c);
}

struct dfui_celldata *
dfui_decode_celldatas(struct aura_buffer *e)
{
	struct dfui_celldata *c, *head = NULL;

	if (!aura_buffer_expect(e, "d{")) return(NULL);
	while (aura_buffer_peek_char(e) != '}') {
		c = dfui_decode_celldata(e);
		c->next = head;
		head = c;
	}
	aura_buffer_expect(e, "}");

	return(head);
}

struct dfui_property *
dfui_decode_property(struct aura_buffer *e)
{
	char *name, *value;
	struct dfui_property *h;

	name = dfui_decode_string(e);
	value = dfui_decode_string(e);

	h = dfui_property_new(name, value);

	free(value);
	free(name);

	return(h);
}

struct dfui_property *
dfui_decode_properties(struct aura_buffer *e)
{
	struct dfui_property *h, *head = NULL;

	if (!aura_buffer_expect(e, "p{")) return(NULL);
	while (aura_buffer_peek_char(e) != '}') {
		h = dfui_decode_property(e);
		h->next = head;
		head = h;
	}
	aura_buffer_expect(e, "}");

	return(head);
}

struct dfui_dataset *
dfui_decode_dataset(struct aura_buffer *e)
{
	struct dfui_dataset *ds;

	ds = dfui_dataset_new();
	ds->celldata_head = dfui_decode_celldatas(e);

	return(ds);
}

struct dfui_dataset *
dfui_decode_datasets(struct aura_buffer *e)
{
	struct dfui_dataset *head = NULL, *ds;

	if (!aura_buffer_expect(e, "D{")) return(NULL);
	while (aura_buffer_peek_char(e) != '}') {
		ds = dfui_decode_dataset(e);
		ds->next = head;
		head = ds;
	}
	aura_buffer_expect(e, "}");

	return(head);
}

struct dfui_form *
dfui_decode_form(struct aura_buffer *e)
{
	char *id;
	struct dfui_info *i;
	struct dfui_form *f;

	if (!aura_buffer_expect(e, "F{")) return(NULL);

	id = dfui_decode_string(e);
	i = dfui_decode_info(e);

	f = dfui_form_new(id, i);

	dfui_form_set_multiple(f, dfui_decode_bool(e));
	dfui_form_set_extensible(f, dfui_decode_bool(e));

	f->field_head = dfui_decode_fields(e);
	f->action_head = dfui_decode_actions(e);
	f->dataset_head = dfui_decode_datasets(e);
	f->property_head = dfui_decode_properties(e);

	aura_buffer_expect(e, "}");
	free(id);

	return(f);
}

struct dfui_response *
dfui_decode_response(struct aura_buffer *e)
{
	char *form_id;
	char *action_id;
	struct dfui_response *r;

	if (!aura_buffer_expect(e, "R{")) return(NULL);

	form_id = dfui_decode_string(e);
	action_id = dfui_decode_string(e);
	r = dfui_response_new(form_id, action_id);
	r->dataset_head = dfui_decode_datasets(e);
	free(form_id);
	free(action_id);

	aura_buffer_expect(e, "}");

	return(r);
}

struct dfui_progress *
dfui_decode_progress(struct aura_buffer *e)
{
	struct dfui_info *i;
	int amount, streaming;
	char *msg_line;
	struct dfui_progress *pr;

	i = dfui_decode_info(e);
	amount = dfui_decode_int(e);
	streaming = dfui_decode_int(e);
	msg_line = dfui_decode_string(e);

	pr = dfui_progress_new(i, amount);
	dfui_progress_set_streaming(pr, streaming);
	dfui_progress_set_msg_line(pr, msg_line);

	free(msg_line);

	return(pr);
}

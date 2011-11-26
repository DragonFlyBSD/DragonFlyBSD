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
 * form.c
 * $Id: form.c,v 1.18 2005/03/04 21:26:20 cpressey Exp $
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <libaura/mem.h>

#define	NEEDS_DFUI_STRUCTURE_DEFINITIONS
#include "dfui.h"
#undef	NEEDS_DFUI_STRUCTURE_DEFINITIONS
#include "dump.h"

/*** INFOS ***/

struct dfui_info *
dfui_info_new(const char *name, const char *short_desc, const char *long_desc)
{
	struct dfui_info *i;

	AURA_MALLOC(i, dfui_info);
	i->name = aura_strdup(name);
	i->short_desc = aura_strdup(short_desc);
	i->long_desc = aura_strdup(long_desc);

	return(i);
}

void
dfui_info_free(struct dfui_info *i)
{
	free(i->name);
	free(i->short_desc);
	free(i->long_desc);
	AURA_FREE(i, dfui_info);
}

const char *
dfui_info_get_name(const struct dfui_info *i)
{
	if (i == NULL)
		return("");
	return(i->name);
}

const char *
dfui_info_get_short_desc(const struct dfui_info *i)
{
	if (i == NULL)
		return("");
	return(i->short_desc);
}

const char *
dfui_info_get_long_desc(const struct dfui_info *i)
{
	if (i == NULL)
		return("");
	return(i->long_desc);
}

void
dfui_info_set_name(struct dfui_info *i, const char *name)
{
	if (i == NULL)
		return;
	if (i->name != NULL)
		free(i->name);
	i->name = aura_strdup(name);
}

void
dfui_info_set_short_desc(struct dfui_info *i, const char *short_desc)
{
	if (i == NULL)
		return;
	if (i->short_desc != NULL)
		free(i->short_desc);
	i->short_desc = aura_strdup(short_desc);
}

void
dfui_info_set_long_desc(struct dfui_info *i, const char *long_desc)
{
	if (i == NULL)
		return;
	if (i->long_desc != NULL)
		free(i->long_desc);
	i->long_desc = aura_strdup(long_desc);
}

/*** PROPERTIES ***/

struct dfui_property *
dfui_property_new(const char *name, const char *value)
{
	struct dfui_property *p;

	AURA_MALLOC(p, dfui_property);
	p->name = aura_strdup(name);
	p->value = aura_strdup(value);

	return(p);
}

void
dfui_property_free(struct dfui_property *p)
{
	if (p == NULL)
		return;
	free(p->name);
	free(p->value);
	AURA_FREE(p, dfui_property);
}

void
dfui_properties_free(struct dfui_property *head)
{
	struct dfui_property *p;

	for (p = head; p != NULL; ) {
		head = p->next;
		dfui_property_free(p);
		p = head;
	}
}

struct dfui_property *
dfui_property_find(struct dfui_property *head, const char *name)
{
	struct dfui_property *p;

	for (p = head; p != NULL; p = p->next) {
		if (strcmp(name, p->name) == 0)
			return(p);
	}

	return(NULL);
}

const char *
dfui_property_get(struct dfui_property *head, const char *name)
{
	struct dfui_property *p;

	if ((p = dfui_property_find(head, name)) != NULL)
		return(p->value);
	return("");
}

struct dfui_property *
dfui_property_set(struct dfui_property **head, const char *name, const char *value)
{
	struct dfui_property *p;

	if (head == NULL)
		return(NULL);

	if ((p = dfui_property_find(*head, name)) != NULL) {
		free(p->value);
		p->value = aura_strdup(value);
		return(p);
	}

	p = dfui_property_new(name, value);
	p->next = *head;
	*head = p;

	return(p);
}

const char *
dfui_property_get_name(const struct dfui_property *p)
{
	return(p->name);
}

const char *
dfui_property_get_value(const struct dfui_property *p)
{
	return(p->value);
}

/*** FIELDS ***/

struct dfui_field *
dfui_field_new(const char *id, struct dfui_info *info)
{
	struct dfui_field *fi;

	AURA_MALLOC(fi, dfui_field);
	fi->id = aura_strdup(id);
	fi->info = info;
	fi->option_head = NULL;
	fi->property_head = NULL;
	fi->next = NULL;

	dfui_field_property_set(fi, "editable", "true");

	return(fi);
}

void
dfui_field_free(struct dfui_field *fi)
{
	free(fi->id);
	dfui_info_free(fi->info);
	dfui_options_free(fi->option_head);
	dfui_properties_free(fi->property_head);
	AURA_FREE(fi, dfui_field);
}

void
dfui_fields_free(struct dfui_field *head)
{
	struct dfui_field *fi;

	fi = head;
	while (fi != NULL) {
		head = fi->next;
		dfui_field_free(fi);
		fi = head;
	}
}

struct dfui_field *
dfui_field_get_next(const struct dfui_field *fi)
{
	if (fi == NULL)
		return(NULL);
	return(fi->next);
}

const char *
dfui_field_get_id(const struct dfui_field *fi)
{
	if (fi == NULL)
		return(NULL);
	return(fi->id);
}

struct dfui_info *
dfui_field_get_info(const struct dfui_field *fi)
{
	if (fi == NULL)
		return(NULL);
	return(fi->info);
}

struct dfui_option *
dfui_field_option_add(struct dfui_field *fi, const char *value)
{
	struct dfui_option *o;

	if (fi == NULL)
		return(NULL);
	o = dfui_option_new(value);
	o->next = fi->option_head;
	fi->option_head = o;

	return(o);
}

struct dfui_option *
dfui_field_option_get_first(const struct dfui_field *fi)
{
	if (fi == NULL)
		return(NULL);
	return(fi->option_head);
}

struct dfui_property *
dfui_field_property_set(struct dfui_field *fi, const char *name, const char *value)
{
	return(dfui_property_set(&fi->property_head, name, value));
}

const char *
dfui_field_property_get(const struct dfui_field *fi, const char *name)
{
	return(dfui_property_get(fi->property_head, name));
}

int
dfui_field_property_is(const struct dfui_field *fi, const char *name, const char *value)
{
	struct dfui_property *h;

	if (fi == NULL)
		return(0);
	if ((h = dfui_property_find(fi->property_head, name)) == NULL)
		return(0);
	return(!strcmp(h->value, value));
}

/*** OPTIONS ***/

struct dfui_option *
dfui_option_new(const char *value)
{
	struct dfui_option *o;

	AURA_MALLOC(o, dfui_option);
	o->value = aura_strdup(value);
	o->next = NULL;

	return(o);
}

void
dfui_option_free(struct dfui_option *o)
{
	if (o == NULL)
		return;
	free(o->value);
	AURA_FREE(o, dfui_option);
}

void
dfui_options_free(struct dfui_option *head)
{
	struct dfui_option *o;

	o = head;
	while (o != NULL) {
		head = o->next;
		dfui_option_free(o);
		o = head;
	}
}

struct dfui_option *
dfui_option_get_next(const struct dfui_option *o)
{
	if (o == NULL)
		return(NULL);
	return(o->next);
}

const char *
dfui_option_get_value(const struct dfui_option *o)
{
	if (o == NULL)
		return("");
	return(o->value);
}

/*** ACTIONS ***/

struct dfui_action *
dfui_action_new(const char *id, struct dfui_info *info)
{
	struct dfui_action *a;

	AURA_MALLOC(a, dfui_action);
	a->id = aura_strdup(id);
	a->info = info;
	a->next = NULL;
	a->property_head = NULL;

	return(a);
}

void
dfui_action_free(struct dfui_action *a)
{
	free(a->id);
	dfui_info_free(a->info);
	dfui_properties_free(a->property_head);
	AURA_FREE(a, dfui_action);
}

void
dfui_actions_free(struct dfui_action *head)
{
	struct dfui_action *a;

	a = head;
	while (a != NULL) {
		head = a->next;
		dfui_action_free(a);
		a = head;
	}
}

struct dfui_action *
dfui_action_get_next(const struct dfui_action *a)
{
	if (a == NULL)
		return(NULL);
	return(a->next);
}

const char *
dfui_action_get_id(const struct dfui_action *a)
{
	if (a == NULL)
		return(NULL);
	return(a->id);
}

struct dfui_info *
dfui_action_get_info(const struct dfui_action *a)
{
	if (a == NULL)
		return(NULL);
	return(a->info);
}

struct dfui_property *
dfui_action_property_set(struct dfui_action *a, const char *name, const char *value)
{
	return(dfui_property_set(&a->property_head, name, value));
}

const char *
dfui_action_property_get(const struct dfui_action *a, const char *name)
{
	return(dfui_property_get(a->property_head, name));
}

int
dfui_action_property_is(const struct dfui_action *a, const char *name, const char *value)
{
	struct dfui_property *h;

	if (a == NULL)
		return(0);
	if ((h = dfui_property_find(a->property_head, name)) == NULL)
		return(0);
	return(!strcmp(h->value, value));
}

/*** FORMS ***/

struct dfui_form *
dfui_form_new(const char *id, struct dfui_info *info)
{
	struct dfui_form *f;

	AURA_MALLOC(f, dfui_form);
	f->id = aura_strdup(id);
	f->info = info;
	f->multiple = 0;
	f->extensible = 0;
	f->field_head = NULL;
	f->action_head = NULL;
	f->dataset_head = NULL;
	f->property_head = NULL;

	return(f);
};

/*
 * Convenience function for creating a form.
 * This function takes a list of any number of strings.
 * This list MUST be terminated by a NULL pointer.
 * The first four strings are the id, name, short description, and long
 * description of the form.
 * Each subsequent string determines what the strings following it represent:
 *    "f": create a field (id, name, short desc, long desc).
 *    "o": add an option to the last field (value).
 *    "a": create an action (id, name, short desc, long desc).
 *    "p": add a property to the last object (name, value).
 */
struct dfui_form *
dfui_form_create(const char *id, const char *name,
		 const char *short_desc, const char *long_desc, ...)
{
#define DFUI_FORM_CREATE_FORM	0
#define DFUI_FORM_CREATE_FIELD	1
#define DFUI_FORM_CREATE_ACTION	2

	struct dfui_form *f;
	struct dfui_info *i;
	va_list args;
	int state = DFUI_FORM_CREATE_FORM;
	char *arg;
	void *object = NULL;
	const char *a_id;
	char *a_name, *a_short_desc, *a_long_desc;

	i = dfui_info_new(name, short_desc, long_desc);
	f = dfui_form_new(id, i);

	va_start(args, long_desc);
	while ((arg = va_arg(args, char *)) != NULL) {
		switch (arg[0]) {
		case 'f':
			a_id = va_arg(args, const char *);
			a_name = va_arg(args, char *);
			a_short_desc = va_arg(args, char *);
			a_long_desc = va_arg(args, char *);
			i = dfui_info_new(a_name, a_short_desc, a_long_desc);
			object = (void *)dfui_form_field_add(f, a_id, i);
			state = DFUI_FORM_CREATE_FIELD;
			break;
		case 'a':
			a_id = va_arg(args, const char *);
			a_name = va_arg(args, char *);
			a_short_desc = va_arg(args, char *);
			a_long_desc = va_arg(args, char *);
			i = dfui_info_new(a_name, a_short_desc, a_long_desc);
			object = (void *)dfui_form_action_add(f, a_id, i);
			state = DFUI_FORM_CREATE_ACTION;
			break;
		case 'o':
			a_name = va_arg(args, char *);
			if (state == DFUI_FORM_CREATE_FIELD) {
				dfui_field_option_add(object, a_name);
			} else {
				dfui_debug("form_create: can't add option to non-field\n");
			}
			break;
		case 'h':
		case 'p':
			a_id = va_arg(args, char *);
			a_short_desc = va_arg(args, char *);

			if (state == DFUI_FORM_CREATE_FIELD) {
				dfui_field_property_set(object, a_id, a_short_desc);
			} else if (state == DFUI_FORM_CREATE_ACTION) {
				dfui_action_property_set(object, a_id, a_short_desc);
			} else if (state == DFUI_FORM_CREATE_FORM) {
				dfui_form_property_set(f, a_id, a_short_desc);
			} else {
				dfui_debug("form_create: can't add property in this state\n");
			}
			break;

		default:
			dfui_debug("form_create: unknown option `%c'\n", arg[0]);
			break;
		}
	}

	va_end(args);
	return(f);
}

void
dfui_form_free(struct dfui_form *f)
{
	free(f->id);
	dfui_info_free(f->info);
	dfui_fields_free(f->field_head);
	dfui_actions_free(f->action_head);
	dfui_datasets_free(f->dataset_head);
	dfui_properties_free(f->property_head);
	AURA_FREE(f, dfui_form);
}

struct dfui_field *
dfui_form_field_add(struct dfui_form *f, const char *id, struct dfui_info *info)
{
	struct dfui_field *fi;

	if (f == NULL)
		return(NULL);
	fi = dfui_field_new(id, info);
	fi->next = f->field_head;
	f->field_head = fi;

	return(fi);
}

struct dfui_field *
dfui_form_field_attach(struct dfui_form *f, struct dfui_field *fi)
{
	if (f == NULL)
		return(NULL);
	fi->next = f->field_head;
	f->field_head = fi;

	return(fi);
}

struct dfui_action *
dfui_form_action_add(struct dfui_form *f, const char *id, struct dfui_info *info)
{
	struct dfui_action *a;

	if (f == NULL)
		return(NULL);
	a = dfui_action_new(id, info);
	a->next = f->action_head;
	f->action_head = a;

	return(a);
}

struct dfui_action *
dfui_form_action_attach(struct dfui_form *f, struct dfui_action *a)
{
	if (f == NULL)
		return(NULL);
	a->next = f->action_head;
	f->action_head = a;

	return(a);
}

void
dfui_form_dataset_add(struct dfui_form *f, struct dfui_dataset *ds)
{
	if (f == NULL || ds == NULL)
		return;
	ds->next = f->dataset_head;
	f->dataset_head = ds;
}

struct dfui_dataset *
dfui_form_dataset_get_first(const struct dfui_form *f)
{
	if (f == NULL)
		return(NULL);
	return(f->dataset_head);
}

int
dfui_form_dataset_count(const struct dfui_form *f)
{
	int n = 0;
	struct dfui_dataset *ds;

	if (f == NULL)
		return(0);

	ds = f->dataset_head;
	while (ds != NULL) {
		n++;
		ds = ds->next;
	}

	return(n);
}

void
dfui_form_datasets_free(struct dfui_form *f)
{
	if (f == NULL)
		return;
	dfui_datasets_free(f->dataset_head);
	f->dataset_head = NULL;
}

struct dfui_field *
dfui_form_field_find(const struct dfui_form *f, const char *id)
{
	struct dfui_field *fi;

	if (f == NULL)
		return(NULL);

	fi = f->field_head;
	while (fi != NULL) {
		if (!strcmp(id, fi->id))
			return(fi);
		fi = fi->next;
	}

	return(NULL);
}

struct dfui_field *
dfui_form_field_get_first(const struct dfui_form *f)
{
	if (f == NULL)
		return(NULL);
	return(f->field_head);
}

int
dfui_form_field_count(const struct dfui_form *f)
{
	int n = 0;
	struct dfui_field *fi;

	if (f == NULL)
		return(0);

	fi = f->field_head;
	while (fi != NULL) {
		n++;
		fi = fi->next;
	}

	return(n);
}

struct dfui_action *
dfui_form_action_find(const struct dfui_form *f, const char *id)
{
	struct dfui_action *a = f->action_head;

	while (a != NULL) {
		if (!strcmp(id, a->id))
			return(a);
		a = a->next;
	}

	return(NULL);
}

struct dfui_action *
dfui_form_action_get_first(const struct dfui_form *f)
{
	if (f == NULL)
		return(NULL);
	return(f->action_head);
}

int
dfui_form_action_count(const struct dfui_form *f)
{
	int n = 0;
	struct dfui_action *a;

	if (f == NULL)
		return(0);

	a = f->action_head;
	while (a != NULL) {
		n++;
		a = a->next;
	}

	return(n);
}

struct dfui_property *
dfui_form_property_set(struct dfui_form *f, const char *name, const char *value)
{
	return(dfui_property_set(&f->property_head, name, value));
}

const char *
dfui_form_property_get(const struct dfui_form *f, const char *name)
{
	return(dfui_property_get(f->property_head, name));
}

int
dfui_form_property_is(const struct dfui_form *f, const char *name, const char *value)
{
	struct dfui_property *h;

	if (f == NULL)
		return(0);
	if ((h = dfui_property_find(f->property_head, name)) == NULL)
		return(0);
	return(!strcmp(h->value, value));
}

const char *
dfui_form_get_id(const struct dfui_form *f)
{
	if (f == NULL)
		return(NULL);	/* XXX ? */
	return(f->id);
}

struct dfui_info *
dfui_form_get_info(const struct dfui_form *f)
{
	if (f == NULL)
		return(NULL);
	return(f->info);
}

void
dfui_form_set_multiple(struct dfui_form *f, int multiple)
{
	if (f == NULL)
		return;
	f->multiple = multiple;
}

int
dfui_form_is_multiple(const struct dfui_form *f)
{
	if (f == NULL)
		return(0);
	return(f->multiple);
}

void
dfui_form_set_extensible(struct dfui_form *f, int extensible)
{
	if (f == NULL)
		return;
	f->extensible = extensible;
}

int
dfui_form_is_extensible(const struct dfui_form *f)
{
	if (f == NULL)
		return(0);
	return(f->extensible);
}

/*** CELLDATAS ***/

struct dfui_celldata *
dfui_celldata_new(const char *field_id, const char *value)
{
	struct dfui_celldata *c;

	AURA_MALLOC(c, dfui_celldata);
	c->field_id = aura_strdup(field_id);
	c->value = aura_strdup(value);

	return(c);
}

void
dfui_celldata_free(struct dfui_celldata *c)
{
	if (c == NULL)
		return;
	free(c->field_id);
	free(c->value);
	AURA_FREE(c, dfui_celldata);
}

void
dfui_celldatas_free(struct dfui_celldata *head)
{
	struct dfui_celldata *c;

	c = head;
	while (c != NULL) {
		head = c->next;
		dfui_celldata_free(c);
		c = head;
	}
}

struct dfui_celldata *
dfui_celldata_find(struct dfui_celldata *head, const char *id)
{
	struct dfui_celldata *c;

	c = head;
	while (c != NULL) {
		if (!strcmp(id, c->field_id))
			return(c);
		c = c->next;
	}

	return(NULL);
}

struct dfui_celldata *
dfui_celldata_get_next(const struct dfui_celldata *cd)
{
	if (cd != NULL) {
		return(cd->next);
	} else {
		return(NULL);
	}
}

const char *
dfui_celldata_get_field_id(const struct dfui_celldata *cd)
{
	if (cd != NULL) {
		return(cd->field_id);
	} else {
		return(NULL);
	}
}

const char *
dfui_celldata_get_value(const struct dfui_celldata *cd)
{
	if (cd != NULL) {
		return(cd->value);
	} else {
		return("");
	}
}

/*** DATASETS ***/

struct dfui_dataset *
dfui_dataset_new(void)
{
	struct dfui_dataset *ds;

	AURA_MALLOC(ds, dfui_dataset);
	ds->celldata_head = NULL;
	ds->next = NULL;

	return(ds);
}

struct dfui_dataset *
dfui_dataset_dup(const struct dfui_dataset *ds)
{
	struct dfui_dataset *nds;
	struct dfui_celldata *cd;

	nds = dfui_dataset_new();

	for (cd = ds->celldata_head; cd != NULL; cd = cd->next) {
		dfui_dataset_celldata_add(nds,
		    cd->field_id, cd->value);
	}

	return(nds);
}

void
dfui_dataset_free(struct dfui_dataset *ds)
{
	dfui_celldatas_free(ds->celldata_head);
	AURA_FREE(ds, dfui_dataset);
}

void
dfui_datasets_free(struct dfui_dataset *head)
{
	struct dfui_dataset *ds;

	ds = head;
	while (ds != NULL) {
		head = ds->next;
		dfui_dataset_free(ds);
		ds = head;
	}
}

struct dfui_celldata *
dfui_dataset_celldata_add(struct dfui_dataset *ds, const char *field_id, const char *value)
{
	struct dfui_celldata *c;

	if (ds == NULL)
		return(NULL);

	c = dfui_celldata_new(field_id, value);
	c->next = ds->celldata_head;
	ds->celldata_head = c;

	return(c);
}

struct dfui_celldata *
dfui_dataset_celldata_get_first(const struct dfui_dataset *ds)
{
	if (ds == NULL)
		return(NULL);
	return(ds->celldata_head);
}

struct dfui_celldata *
dfui_dataset_celldata_find(const struct dfui_dataset *ds, const char *field_id)
{
	if (ds == NULL)
		return(NULL);
	return(dfui_celldata_find(ds->celldata_head, field_id));
}

struct dfui_dataset *
dfui_dataset_get_next(const struct dfui_dataset *ds)
{
	if (ds == NULL)
		return(NULL);
	return(ds->next);
}

/*
 * Returns the value of the celldata with the given id found in the
 * given dataset.  If no such celldata is found, a constant zero-length
 * string is returned.  As such, the return value of this function is
 * guaranteed to not be NULL and must NEVER be freed after use.
 */
const char *
dfui_dataset_get_value(const struct dfui_dataset *ds, const char *id)
{
	struct dfui_celldata *cd;

	if ((cd = dfui_dataset_celldata_find(ds, id)) != NULL) {
		return(dfui_celldata_get_value(cd));
	} else {
		return("");
	}
}

/*
 * Allocates a string to hold the value of the celldata with the
 * given id found in the given dataset.  Even if no such celldata
 * is found, an allocated, zero-length string is returned.  As such,
 * the return value of this function is guaranteed to not be NULL,
 * and must ALWAYS be freed after use.
 */
char *
dfui_dataset_dup_value(const struct dfui_dataset *ds, const char *id)
{
	return(aura_strdup(dfui_dataset_get_value(ds, id)));
}

/*** RESPONSES ***/

struct dfui_response *
dfui_response_new(const char *form_id, const char *action_id)
{
	struct dfui_response *r;

	AURA_MALLOC(r, dfui_response);
	r->form_id = aura_strdup(form_id);
	r->action_id = aura_strdup(action_id);
	r->dataset_head = NULL;

	return(r);
}

void
dfui_response_free(struct dfui_response *r)
{
	free(r->form_id);
	free(r->action_id);
	dfui_datasets_free(r->dataset_head);
	AURA_FREE(r, dfui_response);
}

void
dfui_response_dataset_add(struct dfui_response *r, struct dfui_dataset *ds)
{
	if (ds == NULL || r == NULL)
		return;
	ds->next = r->dataset_head;
	r->dataset_head = ds;
}

struct dfui_dataset *
dfui_response_dataset_get_first(const struct dfui_response *r)
{
	if (r == NULL)
		return(NULL);
	return(r->dataset_head);
}

int
dfui_response_dataset_count(const struct dfui_response *r)
{
	int n = 0;
	struct dfui_dataset *ds;

	if (r == NULL)
		return(0);

	ds = r->dataset_head;
	while (ds != NULL) {
		n++;
		ds = ds->next;
	}

	return(n);
}

const char *
dfui_response_get_form_id(const struct dfui_response *r)
{
	if (r == NULL)
		return(NULL);	/* XXX ? */
	return(r->form_id);
}

const char *
dfui_response_get_action_id(const struct dfui_response *r)
{
	if (r == NULL)
		return(NULL);	/* XXX ? */
	return(r->action_id);
}

/* PROGRESS BARS */

struct dfui_progress *
dfui_progress_new(struct dfui_info *info, int amount)
{
	struct dfui_progress *pr;

	AURA_MALLOC(pr, dfui_progress);
	pr->info = info;
	pr->amount = amount;
	pr->streaming = 0;
	pr->msg_line = NULL;

	return(pr);
}

void
dfui_progress_free(struct dfui_progress *pr)
{
	if (pr == NULL)
		return;
	dfui_info_free(pr->info);
	if (pr->msg_line != NULL)
		free(pr->msg_line);
	AURA_FREE(pr, dfui_progress);
}

struct dfui_info *
dfui_progress_get_info(const struct dfui_progress *pr)
{
	if (pr == NULL)
		return(NULL);
	return(pr->info);
}

void
dfui_progress_set_amount(struct dfui_progress *pr, int amount)
{
	if (pr == NULL)
		return;
	pr->amount = amount;
}

int
dfui_progress_get_amount(const struct dfui_progress *pr)
{
	if (pr == NULL)
		return(0);
	return(pr->amount);
}

void
dfui_progress_set_streaming(struct dfui_progress *pr, int streaming)
{
	if (pr == NULL)
		return;
	pr->streaming = streaming;
}

int
dfui_progress_get_streaming(const struct dfui_progress *pr)
{
	if (pr == NULL)
		return(0);
	return(pr->streaming);
}

void
dfui_progress_set_msg_line(struct dfui_progress *pr, const char *msg_line)
{
	if (pr == NULL)
		return;
	if (pr->msg_line != NULL)
		free(pr->msg_line);
	pr->msg_line = aura_strdup(msg_line);
}

const char *
dfui_progress_get_msg_line(const struct dfui_progress *pr)
{
	if (pr == NULL)
		return("");
	if (pr->msg_line == NULL)
		return("");
	return(pr->msg_line);
}

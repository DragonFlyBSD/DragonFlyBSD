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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "xml.h"
#include "trivial.h"

xml_document_t
xml_document_create(const char *file)
{
	xml_document_t doc;

	if ((doc = malloc(sizeof(struct xml_document))) == NULL)
		return (NULL);

	if ((doc->file = fopen(file, "w")) == NULL) {
		free(doc);
		return (NULL);
	}
	STAILQ_INIT(&doc->open_elems);
	doc->nr_open = 0;
	doc->errmsg = NULL;

	fprintf(doc->file, "<?xml version=\"1.0\" encoding=\"UTF-8\" "
		"standalone=\"no\"?>\n");
	fprintf(doc->file, "<!-- Created by evtranalyze -->\n");

	return doc;
}

int
xml_document_close(xml_document_t doc)
{
	fclose(doc->file);
	return 0;
}


static
void
indent(xml_document_t doc)
{
	int i;

	for (i = 0; i < doc->nr_open; ++i) {
		fprintf(doc->file, "  ");
	}
}

#if 0
int
xml_elem_compile(xml_element_t el)
{
	char *buf, *p;
	int bufsize, c, ret;

	bufsize = 2;
	if (!(buf = malloc(bufsize))) {
		return !0;
	}
again_name:
	p = buf;
	ret = snprintf(p, sizeof(buf), "<%s ", el->name);
	if (ret > sizeof(buf)) {
		bufsize *= 2;
		buf = realloc(bufsize);
		if (!buf) {
			free(p);
			return !0;
		}
		goto again_name;
	}
	c += ret;
}
#endif

static
int
xml_elem_print(xml_document_t doc, xml_element_t el, int closed, int nl)
{
	xml_attribute_t at;
	fprintf(doc->file, "<%s", el->name);
	STAILQ_FOREACH(at, &el->attributes, next) {
		fprintf(doc->file, " %s=\"%s\"", at->name, at->value);
	}
	fprintf(doc->file, "%s%s", closed ? "/>" : ">", nl ? "\n" : "");
	return 0;
}

static
int
_xml_elem_begin(xml_document_t doc, xml_element_t el, int closed, int nl)
{
	STAILQ_INSERT_HEAD(&doc->open_elems, el, link);
	indent(doc);
	++doc->nr_open;
	xml_elem_print(doc, el, closed, nl);
	return 0;
}

static
int
_xml_elem_close(xml_document_t doc, xml_element_t el, int do_indent)
{
	if (el != STAILQ_FIRST(&doc->open_elems)) {
		return !0;
	}

	STAILQ_REMOVE_HEAD(&doc->open_elems, link);
	--doc->nr_open;
	if (do_indent)
		indent(doc);
	fprintf(doc->file, "</%s>\n", el->name);
	return 0;
}

int
xml_elem_close(xml_document_t doc, xml_element_t el)
{
	return _xml_elem_close(doc, el, !0);
}

int
xml_elem_begin(xml_document_t doc, xml_element_t el)
{
	if (el->value) {
		return !0;
	}
	return _xml_elem_begin(doc, el, 0, !0);
}

int
xml_elem_closed(xml_document_t doc, xml_element_t el)
{
	if (el->value) {
		_xml_elem_begin(doc, el, 0, 0);
		fprintf(doc->file, "%s", el->value);
		_xml_elem_close(doc, el, 0);
		return 0;
	}
	indent(doc);
	return xml_elem_print(doc, el, !0, !0);
}


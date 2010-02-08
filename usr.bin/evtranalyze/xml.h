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

#ifndef _EVTRANALYZE_XML_H_
#define _EVTRANALYZE_XML_H_

#include <stdio.h>
#include <sys/queue.h>


typedef struct xml_attribute {
	const char    *name;
	const char    *value;
	STAILQ_ENTRY(xml_attribute) next;
} *xml_attribute_t;

typedef struct xml_element {
	const char    *name;
	const char    *value;
	STAILQ_HEAD(, xml_attribute) attributes;
	STAILQ_ENTRY(xml_element) link;
} *xml_element_t;

typedef struct xml_document {
	FILE    *file;
	STAILQ_HEAD(, xml_element) open_elems;
	int nr_open;
	const char *errmsg;
} *xml_document_t;

static inline
void
xml_elem_init(xml_element_t el, const char *name)
{
	el->name = name;
	el->value = NULL;
	STAILQ_INIT(&el->attributes);
}

static inline
void
xml_elem_set_value(xml_element_t el, const char *value)
{
	el->value = value;
}

static inline
void
xml_attribute_init(xml_attribute_t at, const char *name, const char *value)
{
	at->name = name;
	at->value = value;
}

static inline
void
xml_attribute_set_value(xml_attribute_t at, const char *value)
{
	at->value = value;
}

static inline
void
xml_elem_set_attribute(xml_element_t el, xml_attribute_t at)
{
	STAILQ_INSERT_TAIL(&el->attributes, at, next);
}


xml_document_t xml_document_create(const char *);
int xml_document_close(xml_document_t);
int xml_elem_closed(xml_document_t, xml_element_t);
int xml_elem_begin(xml_document_t, xml_element_t);
int xml_elem_close(xml_document_t, xml_element_t);

#endif /* !_EVTRANALYZE_XML_H_ */

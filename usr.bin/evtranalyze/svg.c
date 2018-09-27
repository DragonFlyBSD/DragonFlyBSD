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
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "xml.h"
#include "svg.h"
#include "trivial.h"

enum {
	MAX_VALSTR_LEN = 30,
};

struct svg_rect {
	struct xml_element el;
	struct xml_attribute x, y, w, h, cl;
	char x_val[MAX_VALSTR_LEN];
	char y_val[MAX_VALSTR_LEN];
	char w_val[MAX_VALSTR_LEN];
	char h_val[MAX_VALSTR_LEN];
};

struct svg_text {
	struct xml_element el;
	struct xml_attribute x, y, cl;
	struct xml_attribute fontsize, transform;
	char x_val[MAX_VALSTR_LEN];
	char y_val[MAX_VALSTR_LEN];
	char fontsize_val[MAX_VALSTR_LEN];
	char transform_val[MAX_VALSTR_LEN * 4];
};

struct svg_line {
	struct xml_element el;
	struct xml_attribute x1, y1, x2, y2, cl;
	struct xml_attribute transform;
	char x1_val[MAX_VALSTR_LEN], y1_val[MAX_VALSTR_LEN];
	char x2_val[MAX_VALSTR_LEN], y2_val[MAX_VALSTR_LEN];
	char transform_val[MAX_VALSTR_LEN * 6];
};

struct svg_document {
	xml_document_t xml;
	const char *css;
	struct xml_element svg;
	struct xml_attribute svg_attrs[2];
	struct svg_text text;
};

static char default_css[] =
	"<![CDATA["
	"rect.generic { fill: green; stroke: black; stroke-width: 0.01;}"
	"rect.thread { fill: yellow; stroke: black; stroke-width: 0.01;}"
	"rect.inactive { fill: grey; stroke: black; stroke-width: 0.01;}"
	"text.generic { fill: black; stroke: none;}]]>";

static
int
svg_transform_print(svg_transform_t tf, char *buf, size_t len)
{
	static double eps = 0.0001;
	char *p;
	int c;

	if (!tf) {
		assert(len >= 1);
		buf[0] = '\0';
		return 0;
	}
	p = buf;
	if ((fabs(tf->tx) > eps) && (fabs(tf->ty) > eps)) {
		c = snprintf(buf, len, "translate(%.20lf,%.20lf)", tf->tx,
			     tf->ty);
		len -= c;
		if (len <= 0)
			return !0;
		p += c;
	}
	if ((fabs(tf->sx - 1) > eps) && (fabs(tf->sy - 1) > eps)) {
		c = snprintf(p, len, "%sscale(%.20lf,%.20lf)",
			     (p == buf) ? "" : " ", tf->sx, tf->sy);
		len -= c;
		if (len <= 0)
			return !0;
		p += c;
	}
	if (fabs(tf->rot) > eps) {
		c = snprintf(p, len, "%srotate(%.2lf)",
			     (p == buf) ? "" : " ", tf->rot);
		len -= c;
		if (len <= 0)
			return !0;
		p += c;
	}
	return 0;
}

static
void
svg_rect_init(struct svg_rect *rect, const char *cl)
{
	xml_elem_init(&rect->el, "rect");
	xml_attribute_init(&rect->x, "x", NULL);
	xml_elem_set_attribute(&rect->el, &rect->x);
	xml_attribute_init(&rect->y, "y", NULL);
	xml_elem_set_attribute(&rect->el, &rect->y);
	xml_attribute_init(&rect->w, "width", NULL);
	xml_elem_set_attribute(&rect->el, &rect->w);
	xml_attribute_init(&rect->h, "height", NULL);
	xml_elem_set_attribute(&rect->el, &rect->h);
	if (cl) {
		xml_attribute_init(&rect->cl, "class", cl);
		xml_elem_set_attribute(&rect->el, &rect->cl);
	}
}

/*
 * In the future, we might want to stick the rectangle in the
 * <defs> element at this point and then <use> it in the rest
 * of the document.
 */
struct svg_rect *
svg_rect_new(const char *cl)
{
	struct svg_rect *r;

	if (!(r = malloc(sizeof(*r))))
		return r;
	svg_rect_init(r, cl);
	return r;
}


int
svg_rect_draw(svg_document_t doc, struct svg_rect *rect, double x,
	      double y, double w, double h)
{
	snprintf(&rect->x_val[0], sizeof(rect->x_val), "%.20lf", x);
	xml_attribute_set_value(&rect->x, &rect->x_val[0]);
	snprintf(&rect->y_val[0], sizeof(rect->y_val), "%lf", y);
	xml_attribute_set_value(&rect->y, &rect->y_val[0]);
	snprintf(&rect->w_val[0], sizeof(rect->w_val), "%.20lf", w);
	xml_attribute_set_value(&rect->w, &rect->w_val[0]);
	snprintf(&rect->h_val[0], sizeof(rect->h_val), "%lf", h);
	xml_attribute_set_value(&rect->h, &rect->h_val[0]);

	xml_elem_closed(doc->xml, &rect->el);
	return 0;
}

static
void
svg_text_init(struct svg_text *text, const char *cl)
{
	xml_elem_init(&text->el, "text");
#if 0 /* remove */
	xml_attribute_init(&text->x, "x", NULL);
	xml_elem_set_attribute(&text->el, &text->x);
	xml_attribute_init(&text->y, "y", NULL);
	xml_elem_set_attribute(&text->el, &text->y);
#endif
	xml_attribute_init(&text->fontsize, "font-size", NULL);
	xml_elem_set_attribute(&text->el, &text->fontsize);
	xml_attribute_init(&text->transform, "transform", NULL);
	xml_elem_set_attribute(&text->el, &text->transform);

	if (cl) {
		xml_attribute_init(&text->cl, "class", cl);
		xml_elem_set_attribute(&text->el, &text->cl);
	}

}

struct svg_text *
svg_text_new(const char *cl)
{
	svg_text_t text;

	if (!(text = malloc(sizeof(*text))))
		return text;
	svg_text_init(text, cl);
	return text;
}

int
svg_text_draw(svg_document_t doc, svg_text_t text, svg_transform_t tf,
	      const char *str, double fontsize)
{
#if 0 /* remove */
	snprintf(&text->x_val[0], sizeof(text->x_val), "%.20lf", x);
	xml_attribute_set_value(&text->x, &text->x_val[0]);
	snprintf(&text->y_val[0], sizeof(text->y_val), "%.20lf", y);
	xml_attribute_set_value(&text->y, &text->y_val[0]);
#endif
	snprintf(&text->fontsize_val[0], sizeof(text->fontsize_val), "%.20lf",
		 fontsize);
	xml_attribute_set_value(&text->fontsize, &text->fontsize_val[0]);
	if (svg_transform_print(tf, &text->transform_val[0],
				sizeof(text->transform_val)))
		return !0;
	xml_attribute_set_value(&text->transform, &text->transform_val[0]);
	xml_elem_set_value(&text->el, str);

	xml_elem_closed(doc->xml, &text->el);
	return 0;
}

static
void
svg_line_init(struct svg_line *line, const char *cl)
{
	xml_elem_init(&line->el, "line");
	xml_attribute_init(&line->x1, "x1", NULL);
	xml_elem_set_attribute(&line->el, &line->x1);
	xml_attribute_init(&line->x2, "x2", NULL);
	xml_elem_set_attribute(&line->el, &line->x2);
	xml_attribute_init(&line->y1, "y1", NULL);
	xml_elem_set_attribute(&line->el, &line->y1);
	xml_attribute_init(&line->y2, "y2", NULL);
	xml_elem_set_attribute(&line->el, &line->y2);

	xml_attribute_init(&line->transform, "transform", NULL);
	xml_elem_set_attribute(&line->el, &line->transform);

	if (cl) {
		xml_attribute_init(&line->cl, "class", cl);
		xml_elem_set_attribute(&line->el, &line->cl);
	}

}

struct svg_line *
svg_line_new(const char *cl)
{
	svg_line_t line;

	if (!(line = malloc(sizeof(*line))))
		return line;
	svg_line_init(line, cl);
	return line;
}

int
svg_line_draw(svg_document_t doc, svg_line_t line, double x1, double _y1,
	      double x2, double y2, svg_transform_t tf)
{
	snprintf(&line->x1_val[0], sizeof(line->x1_val), "%.20lf", x1);
	xml_attribute_set_value(&line->x1, &line->x1_val[0]);

	snprintf(&line->x2_val[0], sizeof(line->x2_val), "%.20lf", x2);
	xml_attribute_set_value(&line->x2, &line->x2_val[0]);

	snprintf(&line->y1_val[0], sizeof(line->y1_val), "%.10lf", _y1);
	xml_attribute_set_value(&line->y1, &line->y1_val[0]);

	snprintf(&line->y2_val[0], sizeof(line->y2_val), "%.20lf", y2);
	xml_attribute_set_value(&line->y2, &line->y2_val[0]);

	xml_attribute_set_value(&line->transform, &line->transform_val[0]);
	if (svg_transform_print(tf,
				&line->transform_val[0],
				sizeof(line->transform_val)))
		return !0;
	xml_elem_closed(doc->xml, &line->el);
	return 0;
}

svg_document_t
svg_document_create(const char *path)
{
	svg_document_t svg;
	struct xml_element style, defs;
	struct xml_attribute type;

	if (!(svg = malloc(sizeof(*svg))))
		return NULL;
	if (!(svg->xml = xml_document_create(path))) {
		free(svg);
		return NULL;
	}
	svg->css = &default_css[0];
	xml_attribute_init(&type, "type", "text/css");
	xml_elem_init(&defs, "defs");
	xml_elem_init(&style, "style");
	xml_elem_set_attribute(&style, &type);
	xml_elem_init(&svg->svg, "svg");
	xml_attribute_init(&svg->svg_attrs[0], "version", "1.1");
	xml_elem_set_attribute(&svg->svg, &svg->svg_attrs[0]);
	xml_attribute_init(&svg->svg_attrs[1], "xmlns",
			   "http://www.w3.org/2000/svg");
	xml_elem_set_attribute(&svg->svg, &svg->svg_attrs[1]);
	xml_elem_begin(svg->xml, &svg->svg);
	xml_elem_begin(svg->xml, &defs);
	xml_elem_set_value(&style, svg->css);
	xml_elem_closed(svg->xml, &style);
	xml_elem_close(svg->xml, &defs);
	
	return svg;
}

int
svg_document_close(svg_document_t svg)
{
	xml_elem_close(svg->xml, &svg->svg);
	xml_document_close(svg->xml);
	return 0;
}

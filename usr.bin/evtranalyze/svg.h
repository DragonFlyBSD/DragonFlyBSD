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

#ifndef SVG_H
#define SVG_H

#include <stdint.h>

struct svg_document;
struct svg_rect;
struct svg_text;
struct svg_line;
typedef struct svg_document *svg_document_t;
typedef struct svg_rect *svg_rect_t;
typedef struct svg_text *svg_text_t;
typedef struct svg_line *svg_line_t;

typedef struct svg_transform {
	double tx, ty;
	double sx, sy;
	double rot;
} *svg_transform_t;

svg_document_t svg_document_create(const char *);
int svg_document_close(svg_document_t);
struct svg_rect *svg_rect_new(const char *);
int svg_rect_draw(svg_document_t, svg_rect_t, double, double, double,
		  double);
struct svg_text *svg_text_new(const char *);
int svg_text_draw(svg_document_t, svg_text_t, svg_transform_t,
	      const char *, double);
struct svg_line *svg_line_new(const char *);
int svg_line_draw(svg_document_t, svg_line_t, double, double, double, double,
		  svg_transform_t);

#endif	/* SVG_H */

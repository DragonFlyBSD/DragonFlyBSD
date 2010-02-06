// -*- C++ -*-
/* Copyright (C) 1989, 1990, 1991, 1992, 2001, 2007, 2009
   Free Software Foundation, Inc.
     Written by James Clark (jjc@jclark.com)

This file is part of groff.

groff is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 3 of the License, or
(at your option) any later version.

groff is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>. */

#include "lib.h"

#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include "cset.h"
#include "errarg.h"
#include "error.h"

#include "box.h"

typedef enum {troff, mathml} eqnmode_t;

extern char start_delim;
extern char end_delim;
extern int non_empty_flag;
extern int inline_flag;
extern int draw_flag;
extern int one_size_reduction_flag;
extern int compatible_flag;
extern int nroff;
extern eqnmode_t output_format;
extern int xhtml;

void init_lex(const char *str, const char *filename, int lineno);
void lex_error(const char *message,
	       const errarg &arg1 = empty_errarg,
	       const errarg &arg2 = empty_errarg,
	       const errarg &arg3 = empty_errarg);

void init_table(const char *device);

// prefix for all registers, strings, macros
#define PREFIX "0"

/* grep.h - interface to grep driver for searching subroutines.
   Copyright (C) 1992, 1998, 2001, 2007, 2009-2015 Free Software Foundation,
   Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifndef GREP_GREP_H
#define GREP_GREP_H 1

#include <stdbool.h>

/* The following flags are exported from grep for the matchers
   to look at. */
extern bool match_icase;	/* -i */
extern bool match_words;	/* -w */
extern bool match_lines;	/* -x */
extern char eolbyte;		/* -z */

/* An enum textbin describes the file's type, inferred from data read
   before the first line is selected for output.  */
enum textbin
  {
    /* Binary, as it contains null bytes and the -z option is not in effect,
       or it contains encoding errors.  */
    TEXTBIN_BINARY = -1,

    /* Not known yet.  Only text has been seen so far.  */
    TEXTBIN_UNKNOWN = 0,

    /* Text.  */
    TEXTBIN_TEXT = 1
  };

/* Input file type.  */
extern enum textbin input_textbin;

#endif

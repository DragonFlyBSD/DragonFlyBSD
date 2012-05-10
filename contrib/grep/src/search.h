/* search.c - searching subroutines using dfa, kwset and regex for grep.
   Copyright 1992, 1998, 2000, 2007, 2009-2012 Free Software Foundation, Inc.

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

#ifndef GREP_SEARCH_H
#define GREP_SEARCH_H 1

#include <config.h>

#include <sys/types.h>

#include "mbsupport.h"

#include <wchar.h>
#include <wctype.h>
#include <regex.h>

#include "system.h"
#include "error.h"
#include "grep.h"
#include "kwset.h"
#include "xalloc.h"

/* searchutils.c */
extern void kwsinit (kwset_t *);

extern char *mbtolower (const char *, size_t *);
extern bool is_mb_middle (const char **, const char *, const char *, size_t);

/* dfasearch.c */
extern void GEAcompile (char const *, size_t, reg_syntax_t);
extern size_t EGexecute (char const *, size_t, size_t *, char const *);

/* kwsearch.c */
extern void Fcompile (char const *, size_t);
extern size_t Fexecute (char const *, size_t, size_t *, char const *);

/* pcresearch.c */
extern void Pcompile (char const *, size_t);
extern size_t Pexecute (char const *, size_t, size_t *, char const *);

#endif /* GREP_SEARCH_H */

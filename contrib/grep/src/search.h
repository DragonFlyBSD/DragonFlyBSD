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
#include <stdint.h>

#include "mbsupport.h"

#include <wchar.h>
#include <wctype.h>
#include <regex.h>

#include "system.h"
#include "error.h"
#include "grep.h"
#include "kwset.h"
#include "xalloc.h"

/* This must be a signed type.  Each value is the difference in the size
   of a character (in bytes) induced by converting to lower case.
   The vast majority of values are 0, but a few are 1 or -1, so
   technically, two bits may be sufficient.  */
typedef signed char mb_len_map_t;

/* searchutils.c */
extern void kwsinit (kwset_t *);

extern char *mbtolower (const char *, size_t *, mb_len_map_t **);
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

/* Apply the MAP (created by mbtolower) to the lowercase-buffer-relative
   *OFF and *LEN, converting them to be relative to the original buffer.  */
static inline void
mb_case_map_apply (mb_len_map_t const *map, size_t *off, size_t *len)
{
  if (map)
    {
      intmax_t off_incr = 0;
      intmax_t len_incr = 0;
      size_t k;
      for (k = 0; k < *off; k++)
        off_incr += map[k];
      for (k = *off; k < *off + *len; k++)
        len_incr += map[k];
      *off += off_incr;
      *len += len_incr;
    }
}

#endif /* GREP_SEARCH_H */

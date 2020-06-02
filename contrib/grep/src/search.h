/* search.c - searching subroutines using dfa, kwset and regex for grep.
   Copyright 1992, 1998, 2000, 2007, 2009-2020 Free Software Foundation, Inc.

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
#include <wchar.h>
#include <wctype.h>
#include <regex.h>

#include "system.h"
#include "grep.h"
#include "dfa.h"
#include "kwset.h"
#include "xalloc.h"
#include "localeinfo.h"

_GL_INLINE_HEADER_BEGIN
#ifndef SEARCH_INLINE
# define SEARCH_INLINE _GL_INLINE
#endif

/* This must be a signed type.  Each value is the difference in the size
   of a character (in bytes) induced by converting to lower case.
   The vast majority of values are 0, but a few are 1 or -1, so
   technically, two bits may be sufficient.  */
typedef signed char mb_len_map_t;

/* searchutils.c */
extern void wordinit (void);
extern kwset_t kwsinit (bool);
extern size_t wordchars_size (char const *, char const *) _GL_ATTRIBUTE_PURE;
extern size_t wordchar_next (char const *, char const *) _GL_ATTRIBUTE_PURE;
extern size_t wordchar_prev (char const *, char const *, char const *)
  _GL_ATTRIBUTE_PURE;
extern ptrdiff_t mb_goback (char const **, size_t *, char const *,
                            char const *);

/* dfasearch.c */
extern void *GEAcompile (char *, size_t, reg_syntax_t);
extern size_t EGexecute (void *, char const *, size_t, size_t *, char const *);

/* kwsearch.c */
extern void *Fcompile (char *, size_t, reg_syntax_t);
extern size_t Fexecute (void *, char const *, size_t, size_t *, char const *);

/* pcresearch.c */
extern void *Pcompile (char *, size_t, reg_syntax_t);
extern size_t Pexecute (void *, char const *, size_t, size_t *, char const *);

/* grep.c */
extern struct localeinfo localeinfo;
extern void fgrep_to_grep_pattern (char **, size_t *);

/* Return the number of bytes in the character at the start of S, which
   is of size N.  N must be positive.  MBS is the conversion state.
   This acts like mbrlen, except it returns 1 when mbrlen would return 0,
   and it is typically faster because of the cache.  */
SEARCH_INLINE size_t
mb_clen (char const *s, size_t n, mbstate_t *mbs)
{
  size_t len = localeinfo.sbclen[to_uchar (*s)];
  return len == (size_t) -2 ? mbrlen (s, n, mbs) : len;
}

_GL_INLINE_HEADER_END

#endif /* GREP_SEARCH_H */

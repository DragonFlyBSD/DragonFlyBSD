/* searchutils.c - helper subroutines for grep's matchers.
   Copyright 1992, 1998, 2000, 2007, 2009-2010 Free Software Foundation, Inc.

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

#include <config.h>
#include <assert.h>
#include "search.h"

#define NCHAR (UCHAR_MAX + 1)

void
kwsinit (kwset_t *kwset)
{
  static char trans[NCHAR];
  int i;

  if (match_icase
#if MBS_SUPPORT
      && MB_CUR_MAX == 1
#endif
     )
    {
      for (i = 0; i < NCHAR; ++i)
        trans[i] = tolower (i);

      *kwset = kwsalloc (trans);
    }
  else
    *kwset = kwsalloc (NULL);

  if (!*kwset)
    xalloc_die ();
}

#if MBS_SUPPORT
/* Convert the *N-byte string, BEG, to lowercase, and write the
   NUL-terminated result into malloc'd storage.  Upon success, set *N
   to the length (in bytes) of the resulting string (not including the
   trailing NUL byte), and return a pointer to the lowercase string.
   Upon memory allocation failure, this function exits.
   Note that on input, *N must be larger than zero.

   Note that while this function returns a pointer to malloc'd storage,
   the caller must not free it, since this function retains a pointer
   to the buffer and reuses it on any subsequent call.  As a consequence,
   this function is not thread-safe.  */
char *
mbtolower (const char *beg, size_t *n)
{
  static char *out;
  static size_t outalloc;
  size_t outlen, mb_cur_max;
  mbstate_t is, os;
  const char *end;
  char *p;

  if (*n > outalloc || outalloc == 0)
    {
      outalloc = MAX(1, *n);
      out = xrealloc (out, outalloc);
    }

  /* appease clang-2.6 */
  assert (out);
  if (*n == 0)
    return out;

  memset (&is, 0, sizeof (is));
  memset (&os, 0, sizeof (os));
  end = beg + *n;

  mb_cur_max = MB_CUR_MAX;
  p = out;
  outlen = 0;
  while (beg < end)
    {
      wchar_t wc;
      size_t mbclen = mbrtowc(&wc, beg, end - beg, &is);
      if (outlen + mb_cur_max >= outalloc)
        {
          out = x2nrealloc (out, &outalloc, 1);
          p = out + outlen;
        }

      if (mbclen == (size_t) -1 || mbclen == (size_t) -2 || mbclen == 0)
        {
          /* An invalid sequence, or a truncated multi-octet character.
             We treat it as a single-octet character.  */
          *p++ = *beg++;
          outlen++;
          memset (&is, 0, sizeof (is));
          memset (&os, 0, sizeof (os));
        }
      else
        {
          beg += mbclen;
          mbclen = wcrtomb (p, towlower ((wint_t) wc), &os);
          p += mbclen;
          outlen += mbclen;
        }
    }

  *n = p - out;
  *p = 0;
  return out;
}


bool
is_mb_middle (const char **good, const char *buf, const char *end,
              size_t match_len)
{
  const char *p = *good;
  const char *prev = p;
  mbstate_t cur_state;

  /* TODO: can be optimized for UTF-8.  */
  memset(&cur_state, 0, sizeof(mbstate_t));
  while (p < buf)
    {
      size_t mbclen = mbrlen(p, end - p, &cur_state);

      /* Store the beginning of the previous complete multibyte character.  */
      if (mbclen != (size_t) -2)
        prev = p;

      if (mbclen == (size_t) -1 || mbclen == (size_t) -2 || mbclen == 0)
        {
          /* An invalid sequence, or a truncated multibyte character.
             We treat it as a single byte character.  */
          mbclen = 1;
          memset(&cur_state, 0, sizeof cur_state);
        }
      p += mbclen;
    }

  *good = prev;

  if (p > buf)
    return true;

  /* P == BUF here.  */
  return 0 < match_len && match_len < mbrlen (p, end - p, &cur_state);
}
#endif /* MBS_SUPPORT */

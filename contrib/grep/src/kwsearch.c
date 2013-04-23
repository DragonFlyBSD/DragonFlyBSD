/* kwsearch.c - searching subroutines using kwset for grep.
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

/* Written August 1992 by Mike Haertel. */

#include <config.h>
#include "search.h"

/* For -w, we also consider _ to be word constituent.  */
#define WCHAR(C) (isalnum (C) || (C) == '_')

/* KWset compiled pattern.  For Ecompile and Gcompile, we compile
   a list of strings, at least one of which is known to occur in
   any string matching the regexp. */
static kwset_t kwset;

void
Fcompile (char const *pattern, size_t size)
{
  char const *err;
  size_t psize = size;
  mb_len_map_t *map = NULL;
  char const *pat = (match_icase && MB_CUR_MAX > 1
                     ? mbtolower (pattern, &psize, &map)
                     : pattern);

  kwsinit (&kwset);

  char const *beg = pat;
  do
    {
      char const *lim;
      char const *end;
      for (lim = beg;; ++lim)
        {
          end = lim;
          if (lim >= pat + psize)
            break;
         if (*lim == '\n')
           {
             lim++;
             break;
           }
#if HAVE_DOS_FILE_CONTENTS
         if (*lim == '\r' && lim + 1 < pat + psize && lim[1] == '\n')
           {
             lim += 2;
             break;
           }
#endif
        }

      if ((err = kwsincr (kwset, beg, end - beg)) != NULL)
        error (EXIT_TROUBLE, 0, "%s", err);
      beg = lim;
    }
  while (beg < pat + psize);

  if ((err = kwsprep (kwset)) != NULL)
    error (EXIT_TROUBLE, 0, "%s", err);
}

size_t
Fexecute (char const *buf, size_t size, size_t *match_size,
          char const *start_ptr)
{
  char const *beg, *try, *end, *mb_start;
  size_t len;
  char eol = eolbyte;
  struct kwsmatch kwsmatch;
  size_t ret_val;
  mb_len_map_t *map = NULL;

  if (MB_CUR_MAX > 1)
    {
      if (match_icase)
        {
          char *case_buf = mbtolower (buf, &size, &map);
          if (start_ptr)
            start_ptr = case_buf + (start_ptr - buf);
          buf = case_buf;
        }
    }

  for (mb_start = beg = start_ptr ? start_ptr : buf; beg <= buf + size; beg++)
    {
      size_t offset = kwsexec (kwset, beg, buf + size - beg, &kwsmatch);
      if (offset == (size_t) -1)
        goto failure;
      len = kwsmatch.size[0];
      if (MB_CUR_MAX > 1
          && is_mb_middle (&mb_start, beg + offset, buf + size, len))
        {
          /* The match was a part of multibyte character, advance at least
             one byte to ensure no infinite loop happens.  */
          mbstate_t s;
          memset (&s, 0, sizeof s);
          size_t mb_len = mbrlen (mb_start, (buf + size) - (beg + offset), &s);
          if (mb_len == (size_t) -2)
            goto failure;
          beg = mb_start;
          if (mb_len != (size_t) -1)
            beg += mb_len - 1;
          continue;
        }
      beg += offset;
      if (start_ptr && !match_words)
        goto success_in_beg_and_len;
      if (match_lines)
        {
          if (beg > buf && beg[-1] != eol)
            continue;
          if (beg + len < buf + size && beg[len] != eol)
            continue;
          goto success;
        }
      else if (match_words)
        for (try = beg; ; )
          {
            if (try > buf && WCHAR((unsigned char) try[-1]))
              break;
            if (try + len < buf + size && WCHAR((unsigned char) try[len]))
              {
                if (!len)
                  break;
                offset = kwsexec (kwset, beg, --len, &kwsmatch);
                if (offset == (size_t) -1)
                  break;
                try = beg + offset;
                len = kwsmatch.size[0];
              }
            else if (!start_ptr)
              goto success;
            else
              goto success_in_beg_and_len;
          } /* for (try) */
      else
        goto success;
    } /* for (beg in buf) */

 failure:
  ret_val = -1;
  goto out;

 success:
  if ((end = memchr (beg + len, eol, (buf + size) - (beg + len))) != NULL)
    end++;
  else
    end = buf + size;
  while (buf < beg && beg[-1] != eol)
    --beg;
  len = end - beg;
 success_in_beg_and_len:;
  size_t off = beg - buf;
  mb_case_map_apply (map, &off, &len);

  *match_size = len;
  ret_val = off;
 out:
  return ret_val;
}

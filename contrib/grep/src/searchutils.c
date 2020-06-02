/* searchutils.c - helper subroutines for grep's matchers.
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

#include <config.h>

#define SEARCH_INLINE _GL_EXTERN_INLINE
#define SYSTEM_INLINE _GL_EXTERN_INLINE
#include "search.h"

/* For each byte B, sbwordchar[B] is true if B is a single-byte
   character that is a word constituent, and is false otherwise.  */
static bool sbwordchar[NCHAR];

/* Whether -w considers WC to be a word constituent.  */
static bool
wordchar (wint_t wc)
{
  return wc == L'_' || iswalnum (wc);
}

void
wordinit (void)
{
  for (int i = 0; i < NCHAR; i++)
    sbwordchar[i] = wordchar (localeinfo.sbctowc[i]);
}

kwset_t
kwsinit (bool mb_trans)
{
  char *trans = NULL;

  if (match_icase && (MB_CUR_MAX == 1 || mb_trans))
    {
      trans = xmalloc (NCHAR);
      if (MB_CUR_MAX == 1)
        for (int i = 0; i < NCHAR; i++)
          trans[i] = toupper (i);
      else
        for (int i = 0; i < NCHAR; i++)
          {
            wint_t wc = localeinfo.sbctowc[i];
            wint_t uwc = towupper (wc);
            if (uwc != wc)
              {
                mbstate_t mbs = { 0 };
                size_t len = wcrtomb (&trans[i], uwc, &mbs);
                if (len != 1)
                  abort ();
              }
            else
              trans[i] = i;
          }
    }

  return kwsalloc (trans);
}

/* In the buffer *MB_START, return the number of bytes needed to go
   back from CUR to the previous boundary, where a "boundary" is the
   start of a multibyte character or is an error-encoding byte.  The
   buffer ends at END (i.e., one past the address of the buffer's last
   byte).  If CUR is already at a boundary, return 0.  If CUR is no
   larger than *MB_START, return CUR - *MB_START without modifying
   *MB_START or *MBCLEN.

   When returning zero, set *MB_START to CUR.  When returning a
   positive value, set *MB_START to the next boundary after CUR,
   or to END if there is no such boundary, and set *MBCLEN to the
   length of the preceding character.  */
ptrdiff_t
mb_goback (char const **mb_start, size_t *mbclen, char const *cur,
           char const *end)
{
  const char *p = *mb_start;
  const char *p0 = p;
  size_t clen;

  if (cur <= p)
    return cur - p;

  if (localeinfo.using_utf8)
    {
      p = cur;
      clen = 1;

      if (cur < end && (*cur & 0xc0) == 0x80)
        for (int i = 1; i <= 3; i++)
          if ((cur[-i] & 0xc0) != 0x80)
            {
              mbstate_t mbs = { 0 };
              clen = mb_clen (cur - i, end - (cur - i), &mbs);
              if (i < clen && clen < (size_t) -2)
                {
                  p0 = cur - i;
                  p = p0 + clen;
                }
              break;
            }
    }
  else
    {
      mbstate_t mbs = { 0 };
      do
        {
          clen = mb_clen (p, end - p, &mbs);

          if ((size_t) -2 <= clen)
            {
              /* An invalid sequence, or a truncated multibyte character.
                 Treat it as a single byte character.  */
              clen = 1;
              memset (&mbs, 0, sizeof mbs);
            }
          p0 = p;
          p += clen;
        }
      while (p < cur);
    }

  *mb_start = p;
  if (mbclen)
    *mbclen = clen;
  return p == cur ? 0 : cur - p0;
}

/* Examine the start of BUF (which goes to END) for word constituents.
   If COUNTALL, examine as many as possible; otherwise, examine at most one.
   Return the total number of bytes in the examined characters.  */
static size_t
wordchars_count (char const *buf, char const *end, bool countall)
{
  size_t n = 0;
  mbstate_t mbs = { 0 };
  while (n < end - buf)
    {
      unsigned char b = buf[n];
      if (sbwordchar[b])
        n++;
      else if (localeinfo.sbclen[b] != -2)
        break;
      else
        {
          wchar_t wc = 0;
          size_t wcbytes = mbrtowc (&wc, buf + n, end - buf - n, &mbs);
          if (!wordchar (wc))
            break;
          n += wcbytes + !wcbytes;
        }
      if (!countall)
        break;
    }
  return n;
}

/* Examine the start of BUF for the longest prefix containing just
   word constituents.  Return the total number of bytes in the prefix.
   The buffer ends at END.  */
size_t
wordchars_size (char const *buf, char const *end)
{
  return wordchars_count (buf, end, true);
}

/* If BUF starts with a word constituent, return the number of bytes
   used to represent it; otherwise, return zero.  The buffer ends at END.  */
size_t
wordchar_next (char const *buf, char const *end)
{
  return wordchars_count (buf, end, false);
}

/* In the buffer BUF, return nonzero if the character whose encoding
   contains the byte before CUR is a word constituent.  The buffer
   ends at END.  */
size_t
wordchar_prev (char const *buf, char const *cur, char const *end)
{
  if (buf == cur)
    return 0;
  unsigned char b = *--cur;
  if (! localeinfo.multibyte
      || (localeinfo.using_utf8 && localeinfo.sbclen[b] != -2))
    return sbwordchar[b];
  char const *p = buf;
  cur -= mb_goback (&p, NULL, cur, end);
  return wordchar_next (cur, end);
}

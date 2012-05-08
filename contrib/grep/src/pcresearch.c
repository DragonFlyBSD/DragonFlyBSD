/* pcresearch.c - searching subroutines using PCRE for grep.
   Copyright 2000, 2007, 2009-2012 Free Software Foundation, Inc.

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
#if HAVE_PCRE_H
# include <pcre.h>
#elif HAVE_PCRE_PCRE_H
# include <pcre/pcre.h>
#endif

#if HAVE_LIBPCRE
/* Compiled internal form of a Perl regular expression.  */
static pcre *cre;

/* Additional information about the pattern.  */
static pcre_extra *extra;
#endif

void
Pcompile (char const *pattern, size_t size)
{
#if !HAVE_LIBPCRE
  error (EXIT_TROUBLE, 0, "%s",
         _("support for the -P option is not compiled into "
           "this --disable-perl-regexp binary"));
#else
  int e;
  char const *ep;
  char *re = xnmalloc (4, size + 7);
  int flags = PCRE_MULTILINE | (match_icase ? PCRE_CASELESS : 0);
  char const *patlim = pattern + size;
  char *n = re;
  char const *p;
  char const *pnul;

  /* FIXME: Remove these restrictions.  */
  if (memchr(pattern, '\n', size))
    error (EXIT_TROUBLE, 0, _("the -P option only supports a single pattern"));

  *n = '\0';
  if (match_lines)
    strcpy (n, "^(");
  if (match_words)
    strcpy (n, "\\b(");
  n += strlen (n);

  /* The PCRE interface doesn't allow NUL bytes in the pattern, so
     replace each NUL byte in the pattern with the four characters
     "\000", removing a preceding backslash if there are an odd
     number of backslashes before the NUL.

     FIXME: This method does not work with some multibyte character
     encodings, notably Shift-JIS, where a multibyte character can end
     in a backslash byte.  */
  for (p = pattern; (pnul = memchr (p, '\0', patlim - p)); p = pnul + 1)
    {
      memcpy (n, p, pnul - p);
      n += pnul - p;
      for (p = pnul; pattern < p && p[-1] == '\\'; p--)
        continue;
      n -= (pnul - p) & 1;
      strcpy (n, "\\000");
      n += 4;
    }

  memcpy (n, p, patlim - p);
  n += patlim - p;
  *n = '\0';
  if (match_words)
    strcpy (n, ")\\b");
  if (match_lines)
    strcpy (n, ")$");

  cre = pcre_compile (re, flags, &ep, &e, pcre_maketables ());
  if (!cre)
    error (EXIT_TROUBLE, 0, "%s", ep);

  extra = pcre_study (cre, 0, &ep);
  if (ep)
    error (EXIT_TROUBLE, 0, "%s", ep);

  free (re);
#endif
}

size_t
Pexecute (char const *buf, size_t size, size_t *match_size,
          char const *start_ptr)
{
#if !HAVE_LIBPCRE
  /* We can't get here, because Pcompile would have been called earlier.  */
  error (EXIT_TROUBLE, 0, _("internal error"));
  return -1;
#else
  /* This array must have at least two elements; everything after that
     is just for performance improvement in pcre_exec.  */
  int sub[300];

  const char *line_buf, *line_end, *line_next;
  int e = PCRE_ERROR_NOMATCH;
  ptrdiff_t start_ofs = start_ptr ? start_ptr - buf : 0;

  /* PCRE can't limit the matching to single lines, therefore we have to
     match each line in the buffer separately.  */
  for (line_next = buf;
       e == PCRE_ERROR_NOMATCH && line_next < buf + size;
       start_ofs -= line_next - line_buf)
    {
      line_buf = line_next;
      line_end = memchr (line_buf, eolbyte, (buf + size) - line_buf);
      if (line_end == NULL)
        line_next = line_end = buf + size;
      else
        line_next = line_end + 1;

      if (start_ptr && start_ptr >= line_end)
        continue;

      if (INT_MAX < line_end - line_buf)
        error (EXIT_TROUBLE, 0, _("exceeded PCRE's line length limit"));

      e = pcre_exec (cre, extra, line_buf, line_end - line_buf,
                     start_ofs < 0 ? 0 : start_ofs, 0,
                     sub, sizeof sub / sizeof *sub);
    }

  if (e <= 0)
    {
      switch (e)
        {
        case PCRE_ERROR_NOMATCH:
          return -1;

        case PCRE_ERROR_NOMEMORY:
          error (EXIT_TROUBLE, 0, _("memory exhausted"));

        case PCRE_ERROR_MATCHLIMIT:
          error (EXIT_TROUBLE, 0,
                 _("exceeded PCRE's backtracking limit"));

        default:
          abort ();
        }
    }
  else
    {
      /* Narrow down to the line we've found.  */
      char const *beg = line_buf + sub[0];
      char const *end = line_buf + sub[1];
      char const *buflim = buf + size;
      char eol = eolbyte;
      if (!start_ptr)
        {
          /* FIXME: The case when '\n' is not found indicates a bug:
             Since grep is line oriented, the match should never contain
             a newline, so there _must_ be a newline following.
           */
          if (!(end = memchr (end, eol, buflim - end)))
            end = buflim;
          else
            end++;
          while (buf < beg && beg[-1] != eol)
            --beg;
        }

      *match_size = end - beg;
      return beg - buf;
    }
#endif
}

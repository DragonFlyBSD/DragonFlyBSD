/* kwsearch.c - searching subroutines using kwset for grep.
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

/* Written August 1992 by Mike Haertel. */

#include <config.h>
#include "search.h"

/* A compiled -F pattern list.  */

struct kwsearch
{
  /* The kwset for this pattern list.  */
  kwset_t kwset;

  /* The number of user-specified patterns.  This is less than
     'kwswords (kwset)' when some extra one-character words have been
     appended, one for each troublesome character that will require a
     DFA search.  */
  ptrdiff_t words;

  /* The user's pattern and its size in bytes.  */
  char *pattern;
  size_t size;

  /* The user's pattern compiled as a regular expression,
     or null if it has not been compiled.  */
  void *re;
};

/* Compile the -F style PATTERN, containing SIZE bytes.  Return a
   description of the compiled pattern.  */

void *
Fcompile (char *pattern, size_t size, reg_syntax_t ignored)
{
  kwset_t kwset;
  ptrdiff_t total = size;
  char *buf = NULL;
  size_t bufalloc = 0;

  kwset = kwsinit (true);

  char const *p = pattern;
  do
    {
      ptrdiff_t len;
      char const *sep = memchr (p, '\n', total);
      if (sep)
        {
          len = sep - p;
          sep++;
          total -= (len + 1);
        }
      else
        {
          len = total;
          total = 0;
        }

      if (match_lines)
        {
          if (eolbyte == '\n' && pattern < p && sep)
            p--;
          else
            {
              if (bufalloc < len + 2)
                {
                  free (buf);
                  bufalloc = len + 2;
                  buf = x2realloc (NULL, &bufalloc);
                  buf[0] = eolbyte;
                }
              memcpy (buf + 1, p, len);
              buf[len + 1] = eolbyte;
              p = buf;
            }
          len += 2;
        }
      kwsincr (kwset, p, len);

      p = sep;
    }
  while (p);

  free (buf);
  ptrdiff_t words = kwswords (kwset);

  if (match_icase)
    {
      /* For each pattern character C that has a case folded
         counterpart F that is multibyte and so cannot easily be
         implemented via translating a single byte, append a pattern
         containing just F.  That way, if the data contains F, the
         matcher can fall back on DFA.  For example, if C is 'i' and
         the locale is en_US.utf8, append a pattern containing just
         the character U+0131 (LATIN SMALL LETTER DOTLESS I), so that
         Fexecute will use a DFA if the data contain U+0131.  */
      mbstate_t mbs = { 0 };
      char checked[NCHAR] = {0,};
      for (p = pattern; p < pattern + size; p++)
        {
          unsigned char c = *p;
          if (checked[c])
            continue;
          checked[c] = true;

          wint_t wc = localeinfo.sbctowc[c];
          wchar_t folded[CASE_FOLDED_BUFSIZE];

          for (int i = case_folded_counterparts (wc, folded); 0 <= --i; )
            {
              char s[MB_LEN_MAX];
              int nbytes = wcrtomb (s, folded[i], &mbs);
              if (1 < nbytes)
                kwsincr (kwset, s, nbytes);
            }
        }
    }

  kwsprep (kwset);

  struct kwsearch *kwsearch = xmalloc (sizeof *kwsearch);
  kwsearch->kwset = kwset;
  kwsearch->words = words;
  kwsearch->pattern = pattern;
  kwsearch->size = size;
  kwsearch->re = NULL;
  return kwsearch;
}

/* Use the compiled pattern VCP to search the buffer BUF of size SIZE.
   If found, return the offset of the first match and store its
   size into *MATCH_SIZE.  If not found, return SIZE_MAX.
   If START_PTR is nonnull, start searching there.  */
size_t
Fexecute (void *vcp, char const *buf, size_t size, size_t *match_size,
          char const *start_ptr)
{
  char const *beg, *end, *mb_start;
  ptrdiff_t len;
  char eol = eolbyte;
  struct kwsmatch kwsmatch;
  size_t ret_val;
  bool mb_check;
  bool longest;
  struct kwsearch *kwsearch = vcp;
  kwset_t kwset = kwsearch->kwset;
  size_t mbclen;

  if (match_lines)
    mb_check = longest = false;
  else
    {
      mb_check = localeinfo.multibyte & !localeinfo.using_utf8;
      longest = mb_check | !!start_ptr | match_words;
    }

  for (mb_start = beg = start_ptr ? start_ptr : buf; beg <= buf + size; beg++)
    {
      ptrdiff_t offset = kwsexec (kwset, beg - match_lines,
                                  buf + size - beg + match_lines, &kwsmatch,
                                  longest);
      if (offset < 0)
        break;
      len = kwsmatch.size[0] - 2 * match_lines;

      if (kwsearch->words <= kwsmatch.index)
        {
          /* The data contain a multibyte character that matches
             some pattern character that is a case folded counterpart.
             Since the kwset code cannot handle this case, fall back
             on the DFA code, which can.  */
          if (! kwsearch->re)
            {
              fgrep_to_grep_pattern (&kwsearch->pattern, &kwsearch->size);
              kwsearch->re = GEAcompile (kwsearch->pattern, kwsearch->size,
                                         RE_SYNTAX_GREP);
            }
          return EGexecute (kwsearch->re, buf, size, match_size, start_ptr);
        }

      mbclen = 0;
      if (mb_check
          && mb_goback (&mb_start, &mbclen, beg + offset, buf + size) != 0)
        {
          /* We have matched a single byte that is not at the beginning of a
             multibyte character.  mb_goback has advanced MB_START past that
             multibyte character.  Now, we want to position BEG so that the
             next kwsexec search starts there.  Thus, to compensate for the
             for-loop's BEG++, above, subtract one here.  This code is
             unusually hard to reach, and exceptionally, let's show how to
             trigger it here:

               printf '\203AA\n'|LC_ALL=ja_JP.SHIFT_JIS src/grep -F A

             That assumes the named locale is installed.
             Note that your system's shift-JIS locale may have a different
             name, possibly including "sjis".  */
          beg = mb_start - 1;
          continue;
        }
      beg += offset;
      if (!!start_ptr & !match_words)
        goto success_in_beg_and_len;
      if (match_lines)
        {
          len += start_ptr == NULL;
          goto success_in_beg_and_len;
        }
      if (! match_words)
        goto success;

      /* We need a preceding mb_start pointer.  Use the beginning of line
         if there is a preceding newline.  */
      if (mbclen == 0)
        {
          char const *nl = memrchr (mb_start, eol, beg - mb_start);
          if (nl)
            mb_start = nl + 1;
        }

      /* Succeed if neither the preceding nor the following character is a
         word constituent.  If the preceding is not, yet the following
         character IS a word constituent, keep trying with shorter matches.  */
      if (mbclen > 0
          ? ! wordchar_next (beg - mbclen, buf + size)
          : ! wordchar_prev (mb_start, beg, buf + size))
        for (;;)
          {
            if (! wordchar_next (beg + len, buf + size))
              {
                if (start_ptr)
                  goto success_in_beg_and_len;
                else
                  goto success;
              }
            if (!start_ptr && !localeinfo.multibyte)
              {
                if (! kwsearch->re)
                  {
                    fgrep_to_grep_pattern (&kwsearch->pattern, &kwsearch->size);
                    kwsearch->re = GEAcompile (kwsearch->pattern,
                                               kwsearch->size,
                                               RE_SYNTAX_GREP);
                  }
                end = memchr (beg + len, eol, (buf + size) - (beg + len));
                end = end ? end + 1 : buf + size;
                if (EGexecute (kwsearch->re, beg, end - beg, match_size, NULL)
                    != (size_t) -1)
                  goto success_match_words;
                beg = end - 1;
                break;
              }
            if (!len)
              break;
            offset = kwsexec (kwset, beg, --len, &kwsmatch, true);
            if (offset != 0)
              break;
            len = kwsmatch.size[0];
          }

      /* No word match was found at BEG.  Skip past word constituents,
         since they cannot precede the next match and not skipping
         them could make things much slower.  */
      beg += wordchars_size (beg, buf + size);
      mb_start = beg;
    } /* for (beg in buf) */

  return -1;

 success:
  end = memchr (beg + len, eol, (buf + size) - (beg + len));
  end = end ? end + 1 : buf + size;
 success_match_words:
  beg = memrchr (buf, eol, beg - buf);
  beg = beg ? beg + 1 : buf;
  len = end - beg;
 success_in_beg_and_len:;
  size_t off = beg - buf;

  *match_size = len;
  ret_val = off;
  return ret_val;
}

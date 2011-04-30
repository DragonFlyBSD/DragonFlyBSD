/* Searching a string for a character.
   Copyright (C) 2007 Free Software Foundation, Inc.
   Written by Bruno Haible <bruno@clisp.org>, 2007.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <config.h>

/* Specification.  */
#include <string.h>

#if HAVE_MBRTOWC
# include "mbuiter.h"
#endif

/* Locate the first single-byte character C in the character string STRING,
   and return a pointer to it.  Return NULL if C is not found in STRING.  */
char *
mbschr (const char *string, int c)
{
#if HAVE_MBRTOWC
  if (MB_CUR_MAX > 1
      /* Optimization: We know that ASCII characters < 0x30 don't occur as
	 part of multibyte characters longer than 1 byte.  Hence, if c < 0x30,
	 the faster unibyte loop can be used.  */
      && (unsigned char) c >= 0x30)
    {
      mbui_iterator_t iter;

      for (mbui_init (iter, string);; mbui_advance (iter))
	{
	  if (!mbui_avail (iter))
	    goto notfound;
	  if (mb_len (mbui_cur (iter)) == 1
	      && (unsigned char) * mbui_cur_ptr (iter) == (unsigned char) c)
	    break;
	}
      return (char *) mbui_cur_ptr (iter);
     notfound:
      return NULL;
    }
  else
#endif
    return strchr (string, c);
}

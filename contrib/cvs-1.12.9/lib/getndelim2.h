/* getndelim2 - Read n characters or less from a stream, stopping at one of up
   to two specified delimiters.

   Copyright (C) 2003 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#ifndef GETNDELIM2_H
#define GETNDELIM2_H 1

#include <stddef.h>
#include <stdio.h>

/* Get ssize_t.  */
#include <sys/types.h>

#define GETNDELIM_NO_LIMIT (ssize_t)-1

/* Read up to (and including) a delimiter DELIM1 from STREAM into *LINEPTR
   + OFFSET (and NUL-terminate it).  If DELIM2 is non-zero, then read up
   and including the first occurrence of DELIM1 or DELIM2.  *LINEPTR is
   a pointer returned from malloc (or NULL), pointing to *LINESIZE bytes of
   space.  It is realloc'd as necessary.  Read no more than LIMIT bytes.
   Return the number of bytes read and stored at *LINEPTR + OFFSET (not
   including the NUL terminator), or -1 on error or EOF.  */
extern ssize_t getndelim2 (char **_lineptr, size_t *_linesize, size_t _offset,
                           size_t _limit, int _delim1, int _delim2,
                           FILE *_stream);

#endif /* GETNDELIM2_H */

/* getnline - Read a line of n characters or less from a stream.

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

#ifndef GETNLINE_H
#define GETNLINE_H 1

#include <stddef.h>
#include <stdio.h>

/* Get ssize_t.  */
#include <sys/types.h>

/* Read a line, up to the next newline, from STREAM, and store it in *LINEPTR.
   *LINEPTR is a pointer returned from malloc (or NULL), pointing to *LINESIZE
   bytes of space.  It is realloc'd as necessary.  Read a maximum of LIMIT
   bytes.
   Return the number of bytes read and stored at *LINEPTR (not including the
   NUL terminator), or -1 on error or EOF.  */
extern ssize_t getnline (char **_lineptr, size_t *_linesize, size_t limit,
                         FILE *_stream);

/* Read a line, up to the next occurrence of DELIMITER, from STREAM, and store
   it in *LINEPTR.
   *LINEPTR is a pointer returned from malloc (or NULL), pointing to *LINESIZE
   bytes of space.  It is realloc'd as necessary.  Read a maximum of LIMIT
   bytes.
   Return the number of bytes read and stored at *LINEPTR (not including the
   NUL terminator), or -1 on error or EOF.  */
extern ssize_t getndelim (char **_lineptr, size_t *_linesize, size_t limit,
                          int _delimiter, FILE *_stream);

#endif /* GETNLINE_H */

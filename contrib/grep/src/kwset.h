/* kwset.h - header declaring the keyword set library.
   Copyright (C) 1989, 1998, 2005, 2007, 2009-2014 Free Software Foundation,
   Inc.

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

/* Written August 1989 by Mike Haertel.
   The author may be reached (Email) at the address mike@ai.mit.edu,
   or (US mail) as Mike Haertel c/o Free Software Foundation. */

#include <stddef.h>

struct kwsmatch
{
  size_t index;			/* Index number of matching keyword. */
  size_t offset[1];		/* Offset of each submatch. */
  size_t size[1];		/* Length of each submatch. */
};

#include "arg-nonnull.h"

struct kwset;
typedef struct kwset *kwset_t;

/* Return an opaque pointer to a newly allocated keyword set.  A nonnull arg
   specifies a table of character translations to be applied to all
   pattern and search text.  */
extern kwset_t kwsalloc (char const *);

/* Incrementally extend the keyword set to include the given string.
   Remember an index number for each keyword included in the set.  */
extern void kwsincr (kwset_t, char const *, size_t);

/* When the keyword set has been completely built, prepare it for use.  */
extern void kwsprep (kwset_t);

/* Search through the given buffer for a member of the keyword set.
   Return a pointer to the leftmost longest match found, or NULL if
   no match is found.  If foundlen is non-NULL, store the length of
   the matching substring in the integer it points to.  Similarly,
   if foundindex is non-NULL, store the index of the particular
   keyword found therein. */
extern size_t kwsexec (kwset_t, char const *, size_t, struct kwsmatch *)
  _GL_ARG_NONNULL ((4));

/* Deallocate the given keyword set and all its associated storage. */
extern void kwsfree (kwset_t);

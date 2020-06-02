/* kwset.h - header declaring the keyword set library.
   Copyright (C) 1989, 1998, 2005, 2007, 2009-2020 Free Software Foundation,
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

/* Written August 1989 by Mike Haertel.  */

#include <stddef.h>
#include <stdbool.h>

struct kwsmatch
{
  ptrdiff_t index;			/* Index number of matching keyword.  */
  ptrdiff_t offset[1];		/* Offset of match.  */
  ptrdiff_t size[1];		/* Length of match.  */
};

#include "arg-nonnull.h"

struct kwset;
typedef struct kwset *kwset_t;

extern kwset_t kwsalloc (char const *);
extern void kwsincr (kwset_t, char const *, ptrdiff_t);
extern ptrdiff_t kwswords (kwset_t) _GL_ATTRIBUTE_PURE;
extern void kwsprep (kwset_t);
extern ptrdiff_t kwsexec (kwset_t, char const *, ptrdiff_t,
                          struct kwsmatch *, bool)
  _GL_ARG_NONNULL ((4));
extern void kwsfree (kwset_t);

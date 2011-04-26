/* grep.h - interface to grep driver for searching subroutines.
   Copyright (C) 1992, 1998, 2001, 2007, 2009-2010 Free Software Foundation,
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

#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 6) || __STRICT_ANSI__
# define __attribute__(x)
#endif

/* Function pointer types.  */
typedef void (*compile_fp_t) (char const *, size_t);
typedef size_t (*execute_fp_t) (char const *, size_t, size_t *, char const *);

/* grep.c expects the matchers vector to be terminated by an entry
   with a NULL name, and to contain at least one entry. */
extern struct matcher
{
  const char *name;
  compile_fp_t compile;
  execute_fp_t execute;
} const matchers[];

extern const char before_options[];
extern const char after_options[];

/* The following flags are exported from grep for the matchers
   to look at. */
extern int match_icase;		/* -i */
extern int match_words;		/* -w */
extern int match_lines;		/* -x */
extern unsigned char eolbyte;	/* -z */

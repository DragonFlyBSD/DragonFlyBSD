/* quote.h - prototypes for quote.c

   Copyright (C) 1998-2001, 2003, 2009-2012 Free Software Foundation, Inc.

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

#ifndef QUOTE_H_
# define QUOTE_H_ 1

/* The quoting options used by quote_n and quote.  Its type is incomplete,
   so it's useful only in expressions like '&quote_quoting_options'.  */
extern struct quoting_options quote_quoting_options;

/* Return an unambiguous printable representation of NAME,
   allocated in slot N, suitable for diagnostics.  */
char const *quote_n (int n, char const *name);

/* Return an unambiguous printable representation of NAME,
   suitable for diagnostics.  */
char const *quote (char const *name);

#endif /* !QUOTE_H_ */

/* Copyright (C) 1989, 1990, 1991, 1992, 2009
     Free Software Foundation, Inc.
     Written by James Clark (jjc@jclark.com)

This file is part of groff.

groff is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 3 of the License, or
(at your option) any later version.

groff is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>. */

#include <string.h>

extern char *strsave(const char *);

extern const char *current_filename;
extern int current_lineno;

void change_filename(const char *f)
{
  if (current_filename != 0 && strcmp(current_filename, f) == 0)
    return;
  current_filename = strsave(f);
}

void change_lineno(int ln)
{
  current_lineno = ln;
}

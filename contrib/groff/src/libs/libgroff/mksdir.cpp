/* Copyright (C) 2001, 2009 Free Software Foundation, Inc.
     Written by Werner Lemberg (wl@gnu.org)

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


/* This file is heavily based on the file mkstemp.c which is part of the
   fileutils package. */


extern int gen_tempname(char *, int = 0);

/* Generate a unique temporary directory name from TEMPLATE.
   The last six characters of TEMPLATE must be "XXXXXX";
   they are replaced with a string that makes the filename unique.
   Then open the directory and return a fd. */
int mksdir(char *tmpl)
{
  return gen_tempname(tmpl, 1);
}

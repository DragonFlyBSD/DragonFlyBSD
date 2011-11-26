// -*- C++ -*-
/* Copyright (C) 2002, 2009 Free Software Foundation, Inc.
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

struct paper {
  char *name;
  double length;		// in PS points
  double width;			// in PS points
};

// global constructor
static class papersize_init {
  static int initialised;
public:
  papersize_init();
} _papersize_init;

// A0-A7, B0-B7, C0-C7, D0-D7, 8 American paper sizes, 1 special size */
#define NUM_PAPERSIZES 4*8 + 8 + 1

extern paper papersizes[];

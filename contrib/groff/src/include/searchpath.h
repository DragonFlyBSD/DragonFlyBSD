// -*- C++ -*-
/* Copyright (C) 1989, 1990, 1991, 1992, 2000, 2003, 2009
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

class search_path {
  char *dirs;
  unsigned init_len;
public:
  search_path(const char *envvar, const char *standard,
	      int add_home, int add_current);
  ~search_path();
  void command_line_dir(const char *);
  FILE *open_file(const char *, char **);
  FILE *open_file_cautious(const char *, char ** = 0, const char * = 0);
};

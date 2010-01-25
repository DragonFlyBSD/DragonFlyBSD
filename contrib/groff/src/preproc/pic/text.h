// -*- C++ -*-
/* Copyright (C) 1989, 2009 Free Software Foundation, Inc.
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


enum hadjustment {
  CENTER_ADJUST,
  LEFT_ADJUST,
  RIGHT_ADJUST
  };

enum vadjustment {
  NONE_ADJUST,
  ABOVE_ADJUST,
  BELOW_ADJUST
  };

struct adjustment {
  hadjustment h;
  vadjustment v;
};

struct text_piece {
  char *text;
  adjustment adj;
  const char *filename;
  int lineno;

  text_piece();
  ~text_piece();
};

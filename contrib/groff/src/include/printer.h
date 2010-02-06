// -*- C++ -*-

// <groff_src_dir>/src/include/printer.h

/* Copyright (C) 1989, 1990, 1991, 1992, 2001, 2002, 2003, 2004, 2006,
                 2009
   Free Software Foundation, Inc.

   Written by James Clark (jjc@jclark.com)

   Last update: 5 Jan 2009

   This file is part of groff.

   groff is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   groff is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

/* Description

   The class `printer' performs the postprocessing.  Each
   postprocessor only needs to implement a derived class of `printer' and
   a suitable function `make_printer' for the device-dependent tasks.
   Then the methods of class `printer' are called automatically by
   `do_file()' in `input.cpp'.
*/

#include "color.h"

struct environment {
  int fontno;
  int size;
  int hpos;
  int vpos;
  int height;
  int slant;
  color *col;
  color *fill;
};

class font;

struct font_pointer_list {
  font *p;
  font_pointer_list *next;

  font_pointer_list(font *, font_pointer_list *);
};

class printer {
public:
  printer();
  virtual ~printer();
  void load_font(int, const char *);
  void set_ascii_char(unsigned char, const environment *, int * = 0);
  void set_special_char(const char *, const environment *, int * = 0);
  virtual void set_numbered_char(int, const environment *, int * = 0);
  glyph *set_char_and_width(const char *, const environment *,
			    int *, font **);
  font *get_font_from_index(int);
  virtual void draw(int, int *, int, const environment *);
  // perform change of line color (text, outline) in the print-out 
  virtual void change_color(const environment * const);
  // perform change of fill color in the print-out 
  virtual void change_fill_color(const environment * const);
  virtual void begin_page(int) = 0;
  virtual void end_page(int) = 0;
  virtual font *make_font(const char *);
  virtual void end_of_line();
  virtual void special(char *, const environment *, char = 'p');
  virtual void devtag(char *, const environment *, char = 'p');

protected:
  font_pointer_list *font_list;
  font **font_table;
  int nfonts;

  // information about named characters
  int is_char_named;
  int is_named_set;
  char named_command;
  const char *named_char_s;
  int named_char_n;

private:
  font *find_font(const char *);
  virtual void set_char(glyph *, font *, const environment *, int,
			const char *) = 0;
};

printer *make_printer();

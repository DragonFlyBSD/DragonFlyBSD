// -*- C++ -*-
/* Copyright (C) 1989, 1990, 1991, 1992, 2006, 2009
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

void process_commands(const char *file);
void process_commands(string &s, const char *file, int lineno);

extern int have_bibliography;
extern int accumulate;
extern int move_punctuation;
extern int search_default;
extern search_list database_list;
extern int label_in_text;
extern int label_in_reference;
extern int sort_adjacent_labels;
extern string pre_label;
extern string post_label;
extern string sep_label;

extern void do_bib(const char *);
extern void output_references();

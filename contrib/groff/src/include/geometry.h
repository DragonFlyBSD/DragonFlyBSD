// -*- C++ -*-
/* Copyright (C) 2001, 2009 Free Software Foundation, Inc.
     Written by Gaius Mulley <gaius@glam.ac.uk>

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

int adjust_arc_center(const int *, double *);
void check_output_arc_limits(int x, int y,
			     int xv1, int yv1,
			     int xv2, int yv2,
			     double c0, double c1,
			     int *minx, int *maxx,
			     int *miny, int *maxy);

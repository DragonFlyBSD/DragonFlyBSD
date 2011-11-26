// -*- C++ -*-
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

struct place;
struct position {
  double x;
  double y;
  position(double, double );
  position();
  position(const place &);
  position &operator+=(const position &);
  position &operator-=(const position &);
  position &operator*=(double);
  position &operator/=(double);
};

position operator-(const position &);
position operator+(const position &, const position &);
position operator-(const position &, const position &);
position operator/(const position &, double);
position operator*(const position &, double);
// dot product
double operator*(const position &, const position &);
int operator==(const position &, const position &);
int operator!=(const position &, const position &);

double hypot(const position &a);

typedef position distance;


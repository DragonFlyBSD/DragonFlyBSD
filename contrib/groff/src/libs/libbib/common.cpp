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

unsigned hash(const char *s, int len)
{
#if 0
  unsigned h = 0, g;
  while (*s != '\0') {
    h <<= 4;
    h += *s++;
    if ((g = h & 0xf0000000) != 0) {
      h ^= g >> 24;
      h ^= g;
    }
  }
#endif
  unsigned h = 0;
  while (--len >= 0)
    h = *s++ + 65587*h;
  return h;
}


/* xref.h -- declarations for the cross references.
   $Id: xref.h,v 1.4 2007/07/01 21:20:33 karl Exp $

   Copyright (C) 2004, 2007 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef XREF_H
#define XREF_H

enum reftype
{
  menu_reference, followed_reference
};

extern char *get_xref_token (int expand);

#endif /* not XREF_H */

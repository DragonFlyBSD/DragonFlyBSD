/* defun.h -- declaration for defuns.
   $Id: defun.h,v 1.6 2007/07/01 21:20:32 karl Exp $

   Copyright (C) 1999, 2005, 2007 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

   Written by Karl Heinz Marbaise <kama@hippo.fido.de>.  */

#ifndef DEFUN_H
#define DEFUN_H

#include "insertion.h"

extern enum insertion_type get_base_type (enum insertion_type);
extern void cm_defun (void);

#endif /* !DEFUN_H */


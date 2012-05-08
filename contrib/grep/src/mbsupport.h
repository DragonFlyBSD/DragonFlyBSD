/* mbsupport.h --- Localize determination of whether we have multibyte stuff.

   Copyright (C) 2004-2005, 2007, 2009-2012 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include <stdlib.h>

#ifndef MBS_SUPPORT
# define MBS_SUPPORT 1
#endif

#if ! MBS_SUPPORT
# undef MB_CUR_MAX
# define MB_CUR_MAX 1
#endif

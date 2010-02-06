// -*- C++ -*-
/* Copyright (C) 1992, 2000, 2001, 2002, 2005, 2009
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

#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_CC_OSFCN_H
#include <osfcn.h>
#else
#include <fcntl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#endif

#ifndef S_IRUSR
#define S_IRUSR 0400
#endif

#ifndef S_IRGRP
#define S_IRGRP 0040
#endif

#ifndef S_IROTH
#define S_IROTH 0004
#endif

#ifndef S_IWUSR
#define S_IWUSR 0200
#endif

#ifndef S_IXUSR
#define S_IXUSR 0100
#endif

#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

#ifndef O_RDONLY
#define O_RDONLY 0
#endif

#ifndef F_OK
#define F_OK 0
#endif

#ifndef HAVE_ISATTY
#define isatty(n) (1)
#endif

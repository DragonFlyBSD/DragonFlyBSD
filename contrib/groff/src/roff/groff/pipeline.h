/* Copyright (C) 1989, 1990, 1991, 1992, 2000, 2002, 2005, 2009
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

#ifdef __cplusplus
extern "C" {
  int run_pipeline(int, char ***, int);
}
#endif

/* run_pipeline can handle at most this many commands */
#define MAX_COMMANDS 13

/* Children exit with this status if execvp fails. */
#define EXEC_FAILED_EXIT_STATUS 0xff

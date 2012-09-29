/* Implement __enable_execute_stack using mprotect(2).
   Copyright (C) 2011, 2012 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 3, or (at your option) any later
   version.

   GCC is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   Under Section 7 of GPL version 3, you are granted additional
   permissions described in the GCC Runtime Library Exception, version
   3.1, as published by the Free Software Foundation.

   You should have received a copy of the GNU General Public License and
   a copy of the GCC Runtime Library Exception along with this program;
   see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
   <http://www.gnu.org/licenses/>.  */

#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>

#define STACK_PROT_RWX (PROT_READ | PROT_WRITE | PROT_EXEC)

extern void __enable_execute_stack (void *);

void
__enable_execute_stack (void *addr)
{
  static int size;
  static long mask;
  char *page, *ends;
  long page_addr, ends_addr;

  if (size == 0)
  {
    size = getpagesize ();
    mask = ~((long) size - 1);
  }
  page_addr = (long) addr;
  ends_addr = (long) (addr + __LIBGCC_TRAMPOLINE_SIZE__);

  page = (char *) (page_addr & mask);
  ends = (char *) ((ends_addr & mask) + size);

  /*
   * Note that no errors should be emitted by mprotect; it is considered
   * dangerous for library calls to send messages to stdout/stderr.
   */
  if (mprotect (page, ends - page, STACK_PROT_RWX) < 0)
    abort ();
}

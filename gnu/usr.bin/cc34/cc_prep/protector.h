/* $DragonFly: src/gnu/usr.bin/cc34/cc_prep/protector.h,v 1.1 2004/06/19 10:34:17 joerg Exp $ */
/* RTL buffer overflow protection function for GNU C compiler
   Copyright (C) 2003 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.  */


/* Declare GUARD variable.  */
#define GUARD_m		Pmode
#define UNITS_PER_GUARD						\
  MAX(BIGGEST_ALIGNMENT / BITS_PER_UNIT, GET_MODE_SIZE (GUARD_m))

#ifndef L_stack_smash_handler

/* Insert a guard variable before a character buffer and change the order
 of pointer variables, character buffers and pointer arguments.  */

extern void prepare_stack_protection  (int);

#ifdef TREE_CODE
/* Search a character array from the specified type tree.  */

extern int search_string_def (tree);
#endif

/* Examine whether the input contains frame pointer addressing.  */

extern int contains_fp (rtx);

/* Return size that is not allocated for stack frame. It will be allocated
   to modify the home of pseudo registers called from global_alloc.  */

extern HOST_WIDE_INT get_frame_free_size (void);

/* Allocate a local variable in the stack area before character buffers
   to avoid the corruption of it.  */

extern rtx assign_stack_local_for_pseudo_reg (enum machine_mode,
					      HOST_WIDE_INT, int);

#endif

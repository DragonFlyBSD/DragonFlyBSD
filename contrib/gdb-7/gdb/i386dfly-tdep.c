/* Target-dependent code for DragonFly/i386.

   Copyright (C) 2003-2013 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "defs.h"
#include "arch-utils.h"
#include "gdbcore.h"
#include "osabi.h"
#include "regcache.h"

#include "gdb_assert.h"

#include "i386-tdep.h"
#include "i387-tdep.h"
#include "solib-svr4.h"

static int i386dfly_r_reg_offset[] =
{
  44, /* %eax */
  40, /* %ecx */
  36, /* %edx */
  32, /* %ebx */
  72, /* %esp */
  24, /* %ebp */
  20, /* %esi */
  16, /* %edi */
  60, /* %eip */
  68, /* %eflags */
  64, /* %cs */
  76, /* %ss */
  12, /* %ds */
  8, /* %es */
  4, /* %fs */
  0  /* %gs */
};

/* Sigtramp routine location.  */
CORE_ADDR i386dfly_sigtramp_start_addr = 0xbfbfdf20;
CORE_ADDR i386dfly_sigtramp_end_addr = 0xbfbfdff0;

int i386dfly_sc_reg_offset[] =
{
  64, /* %eax */
  60, /* %ecx */
  56, /* %edx */
  52, /* %ebx */
  92, /* %esp */
  44, /* %ebp */
  40, /* %esi */
  36, /* %edi */
  80, /* %eip */
  88, /* %eflags */
  84, /* %cs */
  96, /* %ss */
  32, /* %ds */
  28, /* %es */
  24, /* %fs */
  20  /* %gs */
};

static void
i386dfly_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  i386_elf_init_abi(info, gdbarch);

  tdep->gregset_reg_offset = i386dfly_r_reg_offset;
  tdep->gregset_num_regs = ARRAY_SIZE (i386dfly_r_reg_offset);
  tdep->sizeof_gregset = 80;

  tdep->sc_reg_offset = i386dfly_sc_reg_offset;
  tdep->sc_num_regs = ARRAY_SIZE (i386dfly_sc_reg_offset);

  set_solib_svr4_fetch_link_map_offsets
    (gdbarch, svr4_ilp32_fetch_link_map_offsets);
}


/* Provide a prototype to silence -Wmissing-prototypes.  */
void _initialize_i386dfly_tdep (void);

void
_initialize_i386dfly_tdep (void)
{
  gdbarch_register_osabi (bfd_arch_i386, 0, GDB_OSABI_DRAGONFLY,
			  i386dfly_init_abi);
}

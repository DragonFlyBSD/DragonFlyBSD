/* Target-dependent code for DragonFly/amd64.

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
#include "frame.h"
#include "gdbcore.h"
#include "regcache.h"
#include "osabi.h"

#include "gdb_assert.h"
#include "gdb_string.h"

#include "amd64-tdep.h"
#include "solib-svr4.h"

/* Support for signal handlers.  */

/* Assuming THIS_FRAME is for a BSD sigtramp routine, return the
   address of the associated sigcontext structure.  */

static CORE_ADDR
amd64dfly_sigcontext_addr (struct frame_info *this_frame)
{
  struct gdbarch *gdbarch = get_frame_arch (this_frame);
  enum bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  CORE_ADDR sp;
  gdb_byte buf[8];

  /* The `struct sigcontext' (which really is an `ucontext_t' on
     DragonFly/amd64) lives at a fixed offset in the signal frame.  See
     <machine/sigframe.h>.  */
  get_frame_register (this_frame, AMD64_RSP_REGNUM, buf);
  sp = extract_unsigned_integer (buf, 8, byte_order);
  return sp + 16;
}

/* Mapping between the general-purpose registers in `struct reg'
   format and GDB's register cache layout.

   Note that some registers are 32-bit, but since we're little-endian
   we get away with that.  */

/* From <machine/reg.h>.  */
static int amd64dfly_r_reg_offset[] =
{
  6 * 8,			/* %rax */
  7 * 8,			/* %rbx */
  3 * 8,			/* %rcx */
  2 * 8,			/* %rdx */
  1 * 8,			/* %rsi */
  0 * 8,			/* %rdi */
  8 * 8,			/* %rbp */
  23 * 8,			/* %rsp */
  4 * 8,			/* %r8 ... */
  5 * 8,
  9 * 8,
  10 * 8,
  11 * 8,
  12 * 8,
  13 * 8,
  14 * 8,			/* ... %r15 */
  20 * 8,			/* %rip */
  22 * 8,			/* %eflags */
  21 * 8,			/* %cs */
  24 * 8,			/* %ss */
  -1,				/* %ds */
  -1,				/* %es */
  -1,				/* %fs */
  -1				/* %gs */
};

/* Location of the signal trampoline.  */
CORE_ADDR amd64dfly_sigtramp_start_addr = 0x7fffffffffc0ULL;
CORE_ADDR amd64dfly_sigtramp_end_addr = 0x7fffffffffe0ULL;

/* From <machine/signal.h>.  */
int amd64dfly_sc_reg_offset[] =
{
  24 + 6 * 8,			/* %rax */
  24 + 7 * 8,			/* %rbx */
  24 + 3 * 8,			/* %rcx */
  24 + 2 * 8,			/* %rdx */
  24 + 1 * 8,			/* %rsi */
  24 + 0 * 8,			/* %rdi */
  24 + 8 * 8,			/* %rbp */
  24 + 23 * 8,			/* %rsp */
  24 + 4 * 8,			/* %r8 ... */
  24 + 5 * 8,
  24 + 9 * 8,
  24 + 10 * 8,
  24 + 11 * 8,
  24 + 12 * 8,
  24 + 13 * 8,
  24 + 14 * 8,			/* ... %r15 */
  24 + 20 * 8,			/* %rip */
  24 + 22 * 8,			/* %eflags */
  24 + 21 * 8,			/* %cs */
  24 + 24 * 8,			/* %ss */
  -1,				/* %ds */
  -1,				/* %es */
  -1,				/* %fs */
  -1				/* %gs */
};

/* From /usr/src/lib/libc/amd64/gen/_setjmp.S.  */
static int amd64dfly_jmp_buf_reg_offset[] =
{
  -1,				/* %rax */
  1 * 8,			/* %rbx */
  -1,				/* %rcx */
  -1,				/* %rdx */
  -1,				/* %rsi */
  -1,				/* %rdi */
  3 * 8,			/* %rbp */
  2 * 8,			/* %rsp */
  -1,				/* %r8 ... */
  -1,
  -1,
  -1,				/* ... %r11 */
  4 * 8,			/* %r12 ... */
  5 * 8,
  6 * 8,
  7 * 8,			/* ... %r15 */
  0 * 8				/* %rip */
};

static void
amd64dfly_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  i386bsd_init_abi (info, gdbarch);

  tdep->gregset_reg_offset = amd64dfly_r_reg_offset;
  tdep->gregset_num_regs = ARRAY_SIZE (amd64dfly_r_reg_offset);
  tdep->sizeof_gregset = 25 * 8;

  amd64_init_abi (info, gdbarch);

  tdep->sigtramp_start = amd64dfly_sigtramp_start_addr;
  tdep->sigtramp_end = amd64dfly_sigtramp_end_addr;
  tdep->sigcontext_addr = amd64dfly_sigcontext_addr;
  tdep->sc_reg_offset = amd64dfly_sc_reg_offset;
  tdep->sc_num_regs = ARRAY_SIZE (amd64dfly_sc_reg_offset);

  set_solib_svr4_fetch_link_map_offsets
    (gdbarch, svr4_lp64_fetch_link_map_offsets);
}


/* Provide a prototype to silence -Wmissing-prototypes.  */
void _initialize_amd64dfly_tdep (void);

void
_initialize_amd64dfly_tdep (void)
{
  gdbarch_register_osabi (bfd_arch_i386, bfd_mach_x86_64,
			  GDB_OSABI_DRAGONFLY, amd64dfly_init_abi);
}

/* Native-dependent code for DragonFly/i386.

   Copyright (C) 2001, 2002, 2003, 2004, 2007, 2008, 2009
   Free Software Foundation, Inc.

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
#include "inferior.h"
#include "regcache.h"
#include "target.h"

#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/sysctl.h>

#include "fbsd-nat.h"
#include "i386-tdep.h"
#include "i386-nat.h"
#include "i386bsd-nat.h"

/* Prevent warning from -Wmissing-prototypes.  */
void _initialize_i386dfly_nat (void);

void
_initialize_i386dfly_nat (void)
{
  struct target_ops *t;

  /* Add some extra features to the common *BSD/i386 target.  */
  t = i386bsd_target ();

#ifdef HAVE_PT_GETDBREGS

  i386_use_watchpoints (t);

  i386_dr_low.set_control = i386bsd_dr_set_control;
  i386_dr_low.set_addr = i386bsd_dr_set_addr;
  i386_dr_low.reset_addr = i386bsd_dr_reset_addr;
  i386_dr_low.get_status = i386bsd_dr_get_status;
  i386_set_debug_register_length (4);

#endif /* HAVE_PT_GETDBREGS */


  t->to_pid_to_exec_file = fbsd_pid_to_exec_file;
  t->to_find_memory_regions = fbsd_find_memory_regions;
  t->to_make_corefile_notes = fbsd_make_corefile_notes;
  add_target (t);

  /* DragonFly provides a kern.ps_strings sysctl that we can use to
     locate the sigtramp.  That way we can still recognize a sigtramp
     if its location is changed in a new kernel.  Of course this is
     still based on the assumption that the sigtramp is placed
     directly under the location where the program arguments and
     environment can be found.  */
#ifdef KERN_PS_STRINGS
  {
    int mib[2];
    u_long ps_strings;
    size_t len;

    mib[0] = CTL_KERN;
    mib[1] = KERN_PS_STRINGS;
    len = sizeof (ps_strings);
    if (sysctl (mib, 2, &ps_strings, &len, NULL, 0) == 0)
      {
	i386dfly_sigtramp_start_addr = ps_strings - 128;
	i386dfly_sigtramp_end_addr = ps_strings;
      }
  }
#endif
}

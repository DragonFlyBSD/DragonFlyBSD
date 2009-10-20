/* Reverse execution and reverse debugging.

   Copyright (C) 2006, 2007, 2008, 2009 Free Software Foundation, Inc.

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
#include "gdb_string.h"
#include "target.h"
#include "top.h"
#include "cli/cli-cmds.h"
#include "cli/cli-decode.h"
#include "inferior.h"

/* User interface:
   reverse-step, reverse-next etc.  */

static void
exec_direction_default (void *notused)
{
  /* Return execution direction to default state.  */
  execution_direction = EXEC_FORWARD;
}

/* exec_reverse_once -- accepts an arbitrary gdb command (string), 
   and executes it with exec-direction set to 'reverse'.

   Used to implement reverse-next etc. commands.  */

static void
exec_reverse_once (char *cmd, char *args, int from_tty)
{
  char *reverse_command;
  enum exec_direction_kind dir = execution_direction;
  struct cleanup *old_chain;

  if (dir == EXEC_ERROR)
    error (_("Target %s does not support this command."), target_shortname);

  if (dir == EXEC_REVERSE)
    error (_("Already in reverse mode.  Use '%s' or 'set exec-dir forward'."),
	   cmd);

  if (!target_can_execute_reverse)
    error (_("Target %s does not support this command."), target_shortname);

  reverse_command = xstrprintf ("%s %s", cmd, args ? args : "");
  old_chain = make_cleanup (exec_direction_default, NULL);
  make_cleanup (xfree, reverse_command);
  execution_direction = EXEC_REVERSE;
  execute_command (reverse_command, from_tty);
  do_cleanups (old_chain);
}

static void
reverse_step (char *args, int from_tty)
{
  exec_reverse_once ("step", args, from_tty);
}

static void
reverse_stepi (char *args, int from_tty)
{
  exec_reverse_once ("stepi", args, from_tty);
}

static void
reverse_next (char *args, int from_tty)
{
  exec_reverse_once ("next", args, from_tty);
}

static void
reverse_nexti (char *args, int from_tty)
{
  exec_reverse_once ("nexti", args, from_tty);
}

static void
reverse_continue (char *args, int from_tty)
{
  exec_reverse_once ("continue", args, from_tty);
}

static void
reverse_finish (char *args, int from_tty)
{
  exec_reverse_once ("finish", args, from_tty);
}

/* Provide a prototype to silence -Wmissing-prototypes.  */
extern initialize_file_ftype _initialize_reverse;

void
_initialize_reverse (void)
{
  add_com ("reverse-step", class_run, reverse_step, _("\
Step program backward until it reaches the beginning of another source line.\n\
Argument N means do this N times (or till program stops for another reason).")
	   );
  add_com_alias ("rs", "reverse-step", class_alias, 1);

  add_com ("reverse-next", class_run, reverse_next, _("\
Step program backward, proceeding through subroutine calls.\n\
Like the \"reverse-step\" command as long as subroutine calls do not happen;\n\
when they do, the call is treated as one instruction.\n\
Argument N means do this N times (or till program stops for another reason).")
	   );
  add_com_alias ("rn", "reverse-next", class_alias, 1);

  add_com ("reverse-stepi", class_run, reverse_stepi, _("\
Step backward exactly one instruction.\n\
Argument N means do this N times (or till program stops for another reason).")
	   );
  add_com_alias ("rsi", "reverse-stepi", class_alias, 0);

  add_com ("reverse-nexti", class_run, reverse_nexti, _("\
Step backward one instruction, but proceed through called subroutines.\n\
Argument N means do this N times (or till program stops for another reason).")
	   );
  add_com_alias ("rni", "reverse-nexti", class_alias, 0);

  add_com ("reverse-continue", class_run, reverse_continue, _("\
Continue program being debugged but run it in reverse.\n\
If proceeding from breakpoint, a number N may be used as an argument,\n\
which means to set the ignore count of that breakpoint to N - 1 (so that\n\
the breakpoint won't break until the Nth time it is reached)."));
  add_com_alias ("rc", "reverse-continue", class_alias, 0);

  add_com ("reverse-finish", class_run, reverse_finish, _("\
Execute backward until just before selected stack frame is called."));
}

/* MI Command Set.

   Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005, 2007, 2008, 2009
   Free Software Foundation, Inc.

   Contributed by Cygnus Solutions (a Red Hat company).

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

/* Work in progress.  */

#include "defs.h"
#include "arch-utils.h"
#include "target.h"
#include "inferior.h"
#include "gdb_string.h"
#include "exceptions.h"
#include "top.h"
#include "gdbthread.h"
#include "mi-cmds.h"
#include "mi-parse.h"
#include "mi-getopt.h"
#include "mi-console.h"
#include "ui-out.h"
#include "mi-out.h"
#include "interps.h"
#include "event-loop.h"
#include "event-top.h"
#include "gdbcore.h"		/* For write_memory().  */
#include "value.h"
#include "regcache.h"
#include "gdb.h"
#include "frame.h"
#include "mi-main.h"
#include "mi-common.h"
#include "language.h"
#include "valprint.h"
#include "inferior.h"
#include "osdata.h"

#include <ctype.h>
#include <sys/time.h>

#if defined HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

#ifdef HAVE_GETRUSAGE
struct rusage rusage;
#endif

enum
  {
    FROM_TTY = 0
  };

int mi_debug_p;
struct ui_file *raw_stdout;

/* This is used to pass the current command timestamp
   down to continuation routines.  */
static struct mi_timestamp *current_command_ts;

static int do_timings = 0;

char *current_token;
int running_result_record_printed = 1;

/* Flag indicating that the target has proceeded since the last
   command was issued.  */
int mi_proceeded;

extern void _initialize_mi_main (void);
static void mi_cmd_execute (struct mi_parse *parse);

static void mi_execute_cli_command (const char *cmd, int args_p,
				    const char *args);
static void mi_execute_async_cli_command (char *cli_command, 
							char **argv, int argc);
static int register_changed_p (int regnum, struct regcache *,
			       struct regcache *);
static void get_register (struct frame_info *, int regnum, int format);

/* Command implementations.  FIXME: Is this libgdb?  No.  This is the MI
   layer that calls libgdb.  Any operation used in the below should be
   formalized.  */

static void timestamp (struct mi_timestamp *tv);

static void print_diff_now (struct mi_timestamp *start);
static void print_diff (struct mi_timestamp *start, struct mi_timestamp *end);

void
mi_cmd_gdb_exit (char *command, char **argv, int argc)
{
  /* We have to print everything right here because we never return.  */
  if (current_token)
    fputs_unfiltered (current_token, raw_stdout);
  fputs_unfiltered ("^exit\n", raw_stdout);
  mi_out_put (uiout, raw_stdout);
  /* FIXME: The function called is not yet a formal libgdb function.  */
  quit_force (NULL, FROM_TTY);
}

void
mi_cmd_exec_next (char *command, char **argv, int argc)
{
  /* FIXME: Should call a libgdb function, not a cli wrapper.  */
  mi_execute_async_cli_command ("next", argv, argc);
}

void
mi_cmd_exec_next_instruction (char *command, char **argv, int argc)
{
  /* FIXME: Should call a libgdb function, not a cli wrapper.  */
  mi_execute_async_cli_command ("nexti", argv, argc);
}

void
mi_cmd_exec_step (char *command, char **argv, int argc)
{
  /* FIXME: Should call a libgdb function, not a cli wrapper.  */
  mi_execute_async_cli_command ("step", argv, argc);
}

void
mi_cmd_exec_step_instruction (char *command, char **argv, int argc)
{
  /* FIXME: Should call a libgdb function, not a cli wrapper.  */
  mi_execute_async_cli_command ("stepi", argv, argc);
}

void
mi_cmd_exec_finish (char *command, char **argv, int argc)
{
  /* FIXME: Should call a libgdb function, not a cli wrapper.  */
  mi_execute_async_cli_command ("finish", argv, argc);
}

void
mi_cmd_exec_return (char *command, char **argv, int argc)
{
  /* This command doesn't really execute the target, it just pops the
     specified number of frames. */
  if (argc)
    /* Call return_command with from_tty argument equal to 0 so as to
       avoid being queried.  */
    return_command (*argv, 0);
  else
    /* Call return_command with from_tty argument equal to 0 so as to
       avoid being queried.  */
    return_command (NULL, 0);

  /* Because we have called return_command with from_tty = 0, we need
     to print the frame here.  */
  print_stack_frame (get_selected_frame (NULL), 1, LOC_AND_ADDRESS);
}

void
mi_cmd_exec_jump (char *args, char **argv, int argc)
{
  /* FIXME: Should call a libgdb function, not a cli wrapper.  */
  return mi_execute_async_cli_command ("jump", argv, argc);
}
 
static int
proceed_thread_callback (struct thread_info *thread, void *arg)
{
  int pid = *(int *)arg;

  if (!is_stopped (thread->ptid))
    return 0;

  if (PIDGET (thread->ptid) != pid)
    return 0;

  switch_to_thread (thread->ptid);
  clear_proceed_status ();
  proceed ((CORE_ADDR) -1, TARGET_SIGNAL_DEFAULT, 0);
  return 0;
}

void
mi_cmd_exec_continue (char *command, char **argv, int argc)
{
  if (argc == 0)
    continue_1 (0);
  else if (argc == 1 && strcmp (argv[0], "--all") == 0)
    continue_1 (1);
  else if (argc == 2 && strcmp (argv[0], "--thread-group") == 0)
    {
      struct cleanup *old_chain;
      int pid;
      if (argv[1] == NULL || argv[1] == '\0')
	error ("Thread group id not specified");
      pid = atoi (argv[1]);
      if (!in_inferior_list (pid))
	error ("Invalid thread group id '%s'", argv[1]);

      old_chain = make_cleanup_restore_current_thread ();
      iterate_over_threads (proceed_thread_callback, &pid);
      do_cleanups (old_chain);            
    }
  else
    error ("Usage: -exec-continue [--all|--thread-group id]");
}

static int
interrupt_thread_callback (struct thread_info *thread, void *arg)
{
  int pid = *(int *)arg;

  if (!is_running (thread->ptid))
    return 0;

  if (PIDGET (thread->ptid) != pid)
    return 0;

  target_stop (thread->ptid);
  return 0;
}

/* Interrupt the execution of the target.  Note how we must play around
   with the token variables, in order to display the current token in
   the result of the interrupt command, and the previous execution
   token when the target finally stops.  See comments in
   mi_cmd_execute.  */
void
mi_cmd_exec_interrupt (char *command, char **argv, int argc)
{
  if (argc == 0)
    {
      if (!is_running (inferior_ptid))
	error ("Current thread is not running.");

      interrupt_target_1 (0);
    }
  else if (argc == 1 && strcmp (argv[0], "--all") == 0)
    {
      if (!any_running ())
	error ("Inferior not running.");
      
      interrupt_target_1 (1);
    }
  else if (argc == 2 && strcmp (argv[0], "--thread-group") == 0)
    {
      struct cleanup *old_chain;
      int pid;
      if (argv[1] == NULL || argv[1] == '\0')
	error ("Thread group id not specified");
      pid = atoi (argv[1]);
      if (!in_inferior_list (pid))
	error ("Invalid thread group id '%s'", argv[1]);

      old_chain = make_cleanup_restore_current_thread ();
      iterate_over_threads (interrupt_thread_callback, &pid);
      do_cleanups (old_chain);
    }
  else
    error ("Usage: -exec-interrupt [--all|--thread-group id]");
}

static int
find_thread_of_process (struct thread_info *ti, void *p)
{
  int pid = *(int *)p;
  if (PIDGET (ti->ptid) == pid && !is_exited (ti->ptid))
    return 1;

  return 0;
}

void
mi_cmd_target_detach (char *command, char **argv, int argc)
{
  if (argc != 0 && argc != 1)
    error ("Usage: -target-detach [thread-group]");

  if (argc == 1)
    {
      struct thread_info *tp;
      char *end = argv[0];
      int pid = strtol (argv[0], &end, 10);
      if (*end != '\0')
	error (_("Cannot parse thread group id '%s'"), argv[0]);

      /* Pick any thread in the desired process.  Current
	 target_detach deteches from the parent of inferior_ptid.  */
      tp = iterate_over_threads (find_thread_of_process, &pid);
      if (!tp)
	error (_("Thread group is empty"));

      switch_to_thread (tp->ptid);
    }

  detach_command (NULL, 0);
}

void
mi_cmd_thread_select (char *command, char **argv, int argc)
{
  enum gdb_rc rc;
  char *mi_error_message;

  if (argc != 1)
    error ("mi_cmd_thread_select: USAGE: threadnum.");

  rc = gdb_thread_select (uiout, argv[0], &mi_error_message);

  if (rc == GDB_RC_FAIL)
    {
      make_cleanup (xfree, mi_error_message);
      error ("%s", mi_error_message);
    }
}

void
mi_cmd_thread_list_ids (char *command, char **argv, int argc)
{
  enum gdb_rc rc;
  char *mi_error_message;

  if (argc != 0)
    error ("mi_cmd_thread_list_ids: No arguments required.");

  rc = gdb_list_thread_ids (uiout, &mi_error_message);

  if (rc == GDB_RC_FAIL)
    {
      make_cleanup (xfree, mi_error_message);
      error ("%s", mi_error_message);
    }
}

void
mi_cmd_thread_info (char *command, char **argv, int argc)
{
  int thread = -1;
  
  if (argc != 0 && argc != 1)
    error ("Invalid MI command");

  if (argc == 1)
    thread = atoi (argv[0]);

  print_thread_info (uiout, thread, -1);
}

static int
print_one_inferior (struct inferior *inferior, void *arg)
{
  struct cleanup *back_to = make_cleanup_ui_out_tuple_begin_end (uiout, NULL);

  ui_out_field_fmt (uiout, "id", "%d", inferior->pid);
  ui_out_field_string (uiout, "type", "process");
  ui_out_field_int (uiout, "pid", inferior->pid);
  
  do_cleanups (back_to);
  return 0;
}

void
mi_cmd_list_thread_groups (char *command, char **argv, int argc)
{
  struct cleanup *back_to;
  int available = 0;
  char *id = NULL;

  if (argc > 0 && strcmp (argv[0], "--available") == 0)
    {
      ++argv;
      --argc;
      available = 1;
    }

  if (argc > 0)
    id = argv[0];

  back_to = make_cleanup (null_cleanup, NULL);

  if (available && id)
    {
      error (_("Can only report top-level available thread groups"));
    }
  else if (available)
    {
      struct osdata *data;
      struct osdata_item *item;
      int ix_items;

      data = get_osdata ("processes");
      make_cleanup_osdata_free (data);

      make_cleanup_ui_out_list_begin_end (uiout, "groups");

      for (ix_items = 0;
	   VEC_iterate (osdata_item_s, data->items,
			ix_items, item);
	   ix_items++)
	{
	  struct cleanup *back_to =
	    make_cleanup_ui_out_tuple_begin_end (uiout, NULL);

	  const char *pid = get_osdata_column (item, "pid");
	  const char *cmd = get_osdata_column (item, "command");
	  const char *user = get_osdata_column (item, "user");

	  ui_out_field_fmt (uiout, "id", "%s", pid);
	  ui_out_field_string (uiout, "type", "process");
	  if (cmd)
	    ui_out_field_string (uiout, "description", cmd);
	  if (user)
	    ui_out_field_string (uiout, "user", user);

	  do_cleanups (back_to);
	}
    }
  else if (id)
    {
      int pid = atoi (id);
      if (!in_inferior_list (pid))
	error ("Invalid thread group id '%s'", id);
      print_thread_info (uiout, -1, pid);    
    }
  else
    {
      make_cleanup_ui_out_list_begin_end (uiout, "groups");
      iterate_over_inferiors (print_one_inferior, NULL);
    }
  
  do_cleanups (back_to);
}

void
mi_cmd_data_list_register_names (char *command, char **argv, int argc)
{
  struct frame_info *frame;
  struct gdbarch *gdbarch;
  int regnum, numregs;
  int i;
  struct cleanup *cleanup;

  /* Note that the test for a valid register must include checking the
     gdbarch_register_name because gdbarch_num_regs may be allocated for
     the union of the register sets within a family of related processors.
     In this case, some entries of gdbarch_register_name will change depending
     upon the particular processor being debugged.  */

  frame = get_selected_frame (NULL);
  gdbarch = get_frame_arch (frame);
  numregs = gdbarch_num_regs (gdbarch) + gdbarch_num_pseudo_regs (gdbarch);

  cleanup = make_cleanup_ui_out_list_begin_end (uiout, "register-names");

  if (argc == 0)		/* No args, just do all the regs.  */
    {
      for (regnum = 0;
	   regnum < numregs;
	   regnum++)
	{
	  if (gdbarch_register_name (gdbarch, regnum) == NULL
	      || *(gdbarch_register_name (gdbarch, regnum)) == '\0')
	    ui_out_field_string (uiout, NULL, "");
	  else
	    ui_out_field_string (uiout, NULL,
				 gdbarch_register_name (gdbarch, regnum));
	}
    }

  /* Else, list of register #s, just do listed regs.  */
  for (i = 0; i < argc; i++)
    {
      regnum = atoi (argv[i]);
      if (regnum < 0 || regnum >= numregs)
	error ("bad register number");

      if (gdbarch_register_name (gdbarch, regnum) == NULL
	  || *(gdbarch_register_name (gdbarch, regnum)) == '\0')
	ui_out_field_string (uiout, NULL, "");
      else
	ui_out_field_string (uiout, NULL,
			     gdbarch_register_name (gdbarch, regnum));
    }
  do_cleanups (cleanup);
}

void
mi_cmd_data_list_changed_registers (char *command, char **argv, int argc)
{
  static struct regcache *this_regs = NULL;
  struct regcache *prev_regs;
  struct gdbarch *gdbarch;
  int regnum, numregs, changed;
  int i;
  struct cleanup *cleanup;

  /* The last time we visited this function, the current frame's register
     contents were saved in THIS_REGS.  Move THIS_REGS over to PREV_REGS,
     and refresh THIS_REGS with the now-current register contents.  */

  prev_regs = this_regs;
  this_regs = frame_save_as_regcache (get_selected_frame (NULL));
  cleanup = make_cleanup_regcache_xfree (prev_regs);

  /* Note that the test for a valid register must include checking the
     gdbarch_register_name because gdbarch_num_regs may be allocated for
     the union of the register sets within a family of related processors.
     In this  case, some entries of gdbarch_register_name will change depending
     upon the particular processor being debugged.  */

  gdbarch = get_regcache_arch (this_regs);
  numregs = gdbarch_num_regs (gdbarch) + gdbarch_num_pseudo_regs (gdbarch);

  make_cleanup_ui_out_list_begin_end (uiout, "changed-registers");

  if (argc == 0)		/* No args, just do all the regs.  */
    {
      for (regnum = 0;
	   regnum < numregs;
	   regnum++)
	{
	  if (gdbarch_register_name (gdbarch, regnum) == NULL
	      || *(gdbarch_register_name (gdbarch, regnum)) == '\0')
	    continue;
	  changed = register_changed_p (regnum, prev_regs, this_regs);
	  if (changed < 0)
	    error ("mi_cmd_data_list_changed_registers: Unable to read register contents.");
	  else if (changed)
	    ui_out_field_int (uiout, NULL, regnum);
	}
    }

  /* Else, list of register #s, just do listed regs.  */
  for (i = 0; i < argc; i++)
    {
      regnum = atoi (argv[i]);

      if (regnum >= 0
	  && regnum < numregs
	  && gdbarch_register_name (gdbarch, regnum) != NULL
	  && *gdbarch_register_name (gdbarch, regnum) != '\000')
	{
	  changed = register_changed_p (regnum, prev_regs, this_regs);
	  if (changed < 0)
	    error ("mi_cmd_data_list_register_change: Unable to read register contents.");
	  else if (changed)
	    ui_out_field_int (uiout, NULL, regnum);
	}
      else
	error ("bad register number");
    }
  do_cleanups (cleanup);
}

static int
register_changed_p (int regnum, struct regcache *prev_regs,
		    struct regcache *this_regs)
{
  struct gdbarch *gdbarch = get_regcache_arch (this_regs);
  gdb_byte prev_buffer[MAX_REGISTER_SIZE];
  gdb_byte this_buffer[MAX_REGISTER_SIZE];

  /* Registers not valid in this frame return count as unchanged.  */
  if (!regcache_valid_p (this_regs, regnum))
    return 0;

  /* First time through or after gdbarch change consider all registers as
     changed.  Same for registers not valid in the previous frame.  */
  if (!prev_regs || get_regcache_arch (prev_regs) != gdbarch
      || !regcache_valid_p (prev_regs, regnum))
    return 1;

  /* Get register contents and compare.  */
  regcache_cooked_read (prev_regs, regnum, prev_buffer);
  regcache_cooked_read (this_regs, regnum, this_buffer);

  return memcmp (prev_buffer, this_buffer,
		 register_size (gdbarch, regnum)) != 0;
}

/* Return a list of register number and value pairs.  The valid
   arguments expected are: a letter indicating the format in which to
   display the registers contents.  This can be one of: x (hexadecimal), d
   (decimal), N (natural), t (binary), o (octal), r (raw).  After the
   format argumetn there can be a sequence of numbers, indicating which
   registers to fetch the content of.  If the format is the only argument,
   a list of all the registers with their values is returned.  */
void
mi_cmd_data_list_register_values (char *command, char **argv, int argc)
{
  struct frame_info *frame;
  struct gdbarch *gdbarch;
  int regnum, numregs, format;
  int i;
  struct cleanup *list_cleanup, *tuple_cleanup;

  /* Note that the test for a valid register must include checking the
     gdbarch_register_name because gdbarch_num_regs may be allocated for
     the union of the register sets within a family of related processors.
     In this case, some entries of gdbarch_register_name will change depending
     upon the particular processor being debugged.  */

  if (argc == 0)
    error ("mi_cmd_data_list_register_values: Usage: -data-list-register-values <format> [<regnum1>...<regnumN>]");

  format = (int) argv[0][0];

  frame = get_selected_frame (NULL);
  gdbarch = get_frame_arch (frame);
  numregs = gdbarch_num_regs (gdbarch) + gdbarch_num_pseudo_regs (gdbarch);

  list_cleanup = make_cleanup_ui_out_list_begin_end (uiout, "register-values");

  if (argc == 1)	    /* No args, beside the format: do all the regs.  */
    {
      for (regnum = 0;
	   regnum < numregs;
	   regnum++)
	{
	  if (gdbarch_register_name (gdbarch, regnum) == NULL
	      || *(gdbarch_register_name (gdbarch, regnum)) == '\0')
	    continue;
	  tuple_cleanup = make_cleanup_ui_out_tuple_begin_end (uiout, NULL);
	  ui_out_field_int (uiout, "number", regnum);
	  get_register (frame, regnum, format);
	  do_cleanups (tuple_cleanup);
	}
    }

  /* Else, list of register #s, just do listed regs.  */
  for (i = 1; i < argc; i++)
    {
      regnum = atoi (argv[i]);

      if (regnum >= 0
	  && regnum < numregs
	  && gdbarch_register_name (gdbarch, regnum) != NULL
	  && *gdbarch_register_name (gdbarch, regnum) != '\000')
	{
	  tuple_cleanup = make_cleanup_ui_out_tuple_begin_end (uiout, NULL);
	  ui_out_field_int (uiout, "number", regnum);
	  get_register (frame, regnum, format);
	  do_cleanups (tuple_cleanup);
	}
      else
	error ("bad register number");
    }
  do_cleanups (list_cleanup);
}

/* Output one register's contents in the desired format.  */
static void
get_register (struct frame_info *frame, int regnum, int format)
{
  struct gdbarch *gdbarch = get_frame_arch (frame);
  gdb_byte buffer[MAX_REGISTER_SIZE];
  int optim;
  int realnum;
  CORE_ADDR addr;
  enum lval_type lval;
  static struct ui_stream *stb = NULL;

  stb = ui_out_stream_new (uiout);

  if (format == 'N')
    format = 0;

  frame_register (frame, regnum, &optim, &lval, &addr, &realnum, buffer);

  if (optim)
    error ("Optimized out");

  if (format == 'r')
    {
      int j;
      char *ptr, buf[1024];

      strcpy (buf, "0x");
      ptr = buf + 2;
      for (j = 0; j < register_size (gdbarch, regnum); j++)
	{
	  int idx = gdbarch_byte_order (gdbarch) == BFD_ENDIAN_BIG ?
		    j : register_size (gdbarch, regnum) - 1 - j;
	  sprintf (ptr, "%02x", (unsigned char) buffer[idx]);
	  ptr += 2;
	}
      ui_out_field_string (uiout, "value", buf);
      /*fputs_filtered (buf, gdb_stdout); */
    }
  else
    {
      struct value_print_options opts;
      get_formatted_print_options (&opts, format);
      opts.deref_ref = 1;
      val_print (register_type (gdbarch, regnum), buffer, 0, 0,
		 stb->stream, 0, &opts, current_language);
      ui_out_field_stream (uiout, "value", stb);
      ui_out_stream_delete (stb);
    }
}

/* Write given values into registers. The registers and values are
   given as pairs.  The corresponding MI command is 
   -data-write-register-values <format> [<regnum1> <value1>...<regnumN> <valueN>]*/
void
mi_cmd_data_write_register_values (char *command, char **argv, int argc)
{
  struct regcache *regcache;
  struct gdbarch *gdbarch;
  int numregs, i;
  char format;

  /* Note that the test for a valid register must include checking the
     gdbarch_register_name because gdbarch_num_regs may be allocated for
     the union of the register sets within a family of related processors.
     In this case, some entries of gdbarch_register_name will change depending
     upon the particular processor being debugged.  */

  regcache = get_current_regcache ();
  gdbarch = get_regcache_arch (regcache);
  numregs = gdbarch_num_regs (gdbarch) + gdbarch_num_pseudo_regs (gdbarch);

  if (argc == 0)
    error ("mi_cmd_data_write_register_values: Usage: -data-write-register-values <format> [<regnum1> <value1>...<regnumN> <valueN>]");

  format = (int) argv[0][0];

  if (!target_has_registers)
    error ("mi_cmd_data_write_register_values: No registers.");

  if (!(argc - 1))
    error ("mi_cmd_data_write_register_values: No regs and values specified.");

  if ((argc - 1) % 2)
    error ("mi_cmd_data_write_register_values: Regs and vals are not in pairs.");

  for (i = 1; i < argc; i = i + 2)
    {
      int regnum = atoi (argv[i]);

      if (regnum >= 0 && regnum < numregs
	  && gdbarch_register_name (gdbarch, regnum)
	  && *gdbarch_register_name (gdbarch, regnum))
	{
	  LONGEST value;

	  /* Get the value as a number.  */
	  value = parse_and_eval_address (argv[i + 1]);

	  /* Write it down.  */
	  regcache_cooked_write_signed (regcache, regnum, value);
	}
      else
	error ("bad register number");
    }
}

/* Evaluate the value of the argument.  The argument is an
   expression. If the expression contains spaces it needs to be
   included in double quotes.  */
void
mi_cmd_data_evaluate_expression (char *command, char **argv, int argc)
{
  struct expression *expr;
  struct cleanup *old_chain = NULL;
  struct value *val;
  struct ui_stream *stb = NULL;
  struct value_print_options opts;

  stb = ui_out_stream_new (uiout);

  if (argc != 1)
    {
      ui_out_stream_delete (stb);
      error ("mi_cmd_data_evaluate_expression: Usage: -data-evaluate-expression expression");
    }

  expr = parse_expression (argv[0]);

  old_chain = make_cleanup (free_current_contents, &expr);

  val = evaluate_expression (expr);

  /* Print the result of the expression evaluation.  */
  get_user_print_options (&opts);
  opts.deref_ref = 0;
  val_print (value_type (val), value_contents (val),
	     value_embedded_offset (val), value_address (val),
	     stb->stream, 0, &opts, current_language);

  ui_out_field_stream (uiout, "value", stb);
  ui_out_stream_delete (stb);

  do_cleanups (old_chain);
}

/* DATA-MEMORY-READ:

   ADDR: start address of data to be dumped.
   WORD-FORMAT: a char indicating format for the ``word''.  See 
   the ``x'' command.
   WORD-SIZE: size of each ``word''; 1,2,4, or 8 bytes.
   NR_ROW: Number of rows.
   NR_COL: The number of colums (words per row).
   ASCHAR: (OPTIONAL) Append an ascii character dump to each row.  Use
   ASCHAR for unprintable characters.

   Reads SIZE*NR_ROW*NR_COL bytes starting at ADDR from memory and
   displayes them.  Returns:

   {addr="...",rowN={wordN="..." ,... [,ascii="..."]}, ...}

   Returns: 
   The number of bytes read is SIZE*ROW*COL. */

void
mi_cmd_data_read_memory (char *command, char **argv, int argc)
{
  struct gdbarch *gdbarch = get_current_arch ();
  struct cleanup *cleanups = make_cleanup (null_cleanup, NULL);
  CORE_ADDR addr;
  long total_bytes;
  long nr_cols;
  long nr_rows;
  char word_format;
  struct type *word_type;
  long word_size;
  char word_asize;
  char aschar;
  gdb_byte *mbuf;
  int nr_bytes;
  long offset = 0;
  int optind = 0;
  char *optarg;
  enum opt
    {
      OFFSET_OPT
    };
  static struct mi_opt opts[] =
  {
    {"o", OFFSET_OPT, 1},
    { 0, 0, 0 }
  };

  while (1)
    {
      int opt = mi_getopt ("mi_cmd_data_read_memory", argc, argv, opts,
			   &optind, &optarg);
      if (opt < 0)
	break;
      switch ((enum opt) opt)
	{
	case OFFSET_OPT:
	  offset = atol (optarg);
	  break;
	}
    }
  argv += optind;
  argc -= optind;

  if (argc < 5 || argc > 6)
    error ("mi_cmd_data_read_memory: Usage: ADDR WORD-FORMAT WORD-SIZE NR-ROWS NR-COLS [ASCHAR].");

  /* Extract all the arguments. */

  /* Start address of the memory dump.  */
  addr = parse_and_eval_address (argv[0]) + offset;
  /* The format character to use when displaying a memory word.  See
     the ``x'' command. */
  word_format = argv[1][0];
  /* The size of the memory word.  */
  word_size = atol (argv[2]);
  switch (word_size)
    {
    case 1:
      word_type = builtin_type (gdbarch)->builtin_int8;
      word_asize = 'b';
      break;
    case 2:
      word_type = builtin_type (gdbarch)->builtin_int16;
      word_asize = 'h';
      break;
    case 4:
      word_type = builtin_type (gdbarch)->builtin_int32;
      word_asize = 'w';
      break;
    case 8:
      word_type = builtin_type (gdbarch)->builtin_int64;
      word_asize = 'g';
      break;
    default:
      word_type = builtin_type (gdbarch)->builtin_int8;
      word_asize = 'b';
    }
  /* The number of rows.  */
  nr_rows = atol (argv[3]);
  if (nr_rows <= 0)
    error ("mi_cmd_data_read_memory: invalid number of rows.");

  /* Number of bytes per row.  */
  nr_cols = atol (argv[4]);
  if (nr_cols <= 0)
    error ("mi_cmd_data_read_memory: invalid number of columns.");

  /* The un-printable character when printing ascii.  */
  if (argc == 6)
    aschar = *argv[5];
  else
    aschar = 0;

  /* Create a buffer and read it in.  */
  total_bytes = word_size * nr_rows * nr_cols;
  mbuf = xcalloc (total_bytes, 1);
  make_cleanup (xfree, mbuf);

  /* Dispatch memory reads to the topmost target, not the flattened
     current_target.  */
  nr_bytes = target_read_until_error (current_target.beneath,
				      TARGET_OBJECT_MEMORY, NULL, mbuf,
				      addr, total_bytes);
  if (nr_bytes <= 0)
    error ("Unable to read memory.");

  /* Output the header information.  */
  ui_out_field_core_addr (uiout, "addr", gdbarch, addr);
  ui_out_field_int (uiout, "nr-bytes", nr_bytes);
  ui_out_field_int (uiout, "total-bytes", total_bytes);
  ui_out_field_core_addr (uiout, "next-row",
			  gdbarch, addr + word_size * nr_cols);
  ui_out_field_core_addr (uiout, "prev-row",
			  gdbarch, addr - word_size * nr_cols);
  ui_out_field_core_addr (uiout, "next-page", gdbarch, addr + total_bytes);
  ui_out_field_core_addr (uiout, "prev-page", gdbarch, addr - total_bytes);

  /* Build the result as a two dimentional table.  */
  {
    struct ui_stream *stream = ui_out_stream_new (uiout);
    struct cleanup *cleanup_list_memory;
    int row;
    int row_byte;
    cleanup_list_memory = make_cleanup_ui_out_list_begin_end (uiout, "memory");
    for (row = 0, row_byte = 0;
	 row < nr_rows;
	 row++, row_byte += nr_cols * word_size)
      {
	int col;
	int col_byte;
	struct cleanup *cleanup_tuple;
	struct cleanup *cleanup_list_data;
	struct value_print_options opts;

	cleanup_tuple = make_cleanup_ui_out_tuple_begin_end (uiout, NULL);
	ui_out_field_core_addr (uiout, "addr", gdbarch, addr + row_byte);
	/* ui_out_field_core_addr_symbolic (uiout, "saddr", addr + row_byte); */
	cleanup_list_data = make_cleanup_ui_out_list_begin_end (uiout, "data");
	get_formatted_print_options (&opts, word_format);
	for (col = 0, col_byte = row_byte;
	     col < nr_cols;
	     col++, col_byte += word_size)
	  {
	    if (col_byte + word_size > nr_bytes)
	      {
		ui_out_field_string (uiout, NULL, "N/A");
	      }
	    else
	      {
		ui_file_rewind (stream->stream);
		print_scalar_formatted (mbuf + col_byte, word_type, &opts,
					word_asize, stream->stream);
		ui_out_field_stream (uiout, NULL, stream);
	      }
	  }
	do_cleanups (cleanup_list_data);
	if (aschar)
	  {
	    int byte;
	    ui_file_rewind (stream->stream);
	    for (byte = row_byte; byte < row_byte + word_size * nr_cols; byte++)
	      {
		if (byte >= nr_bytes)
		  {
		    fputc_unfiltered ('X', stream->stream);
		  }
		else if (mbuf[byte] < 32 || mbuf[byte] > 126)
		  {
		    fputc_unfiltered (aschar, stream->stream);
		  }
		else
		  fputc_unfiltered (mbuf[byte], stream->stream);
	      }
	    ui_out_field_stream (uiout, "ascii", stream);
	  }
	do_cleanups (cleanup_tuple);
      }
    ui_out_stream_delete (stream);
    do_cleanups (cleanup_list_memory);
  }
  do_cleanups (cleanups);
}

/* DATA-MEMORY-WRITE:

   COLUMN_OFFSET: optional argument. Must be preceeded by '-o'. The
   offset from the beginning of the memory grid row where the cell to
   be written is.
   ADDR: start address of the row in the memory grid where the memory
   cell is, if OFFSET_COLUMN is specified.  Otherwise, the address of
   the location to write to.
   FORMAT: a char indicating format for the ``word''.  See 
   the ``x'' command.
   WORD_SIZE: size of each ``word''; 1,2,4, or 8 bytes
   VALUE: value to be written into the memory address.

   Writes VALUE into ADDR + (COLUMN_OFFSET * WORD_SIZE).

   Prints nothing.  */
void
mi_cmd_data_write_memory (char *command, char **argv, int argc)
{
  struct gdbarch *gdbarch = get_current_arch ();
  enum bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  CORE_ADDR addr;
  char word_format;
  long word_size;
  /* FIXME: ezannoni 2000-02-17 LONGEST could possibly not be big
     enough when using a compiler other than GCC.  */
  LONGEST value;
  void *buffer;
  struct cleanup *old_chain;
  long offset = 0;
  int optind = 0;
  char *optarg;
  enum opt
    {
      OFFSET_OPT
    };
  static struct mi_opt opts[] =
  {
    {"o", OFFSET_OPT, 1},
    { 0, 0, 0 }
  };

  while (1)
    {
      int opt = mi_getopt ("mi_cmd_data_write_memory", argc, argv, opts,
			   &optind, &optarg);
      if (opt < 0)
	break;
      switch ((enum opt) opt)
	{
	case OFFSET_OPT:
	  offset = atol (optarg);
	  break;
	}
    }
  argv += optind;
  argc -= optind;

  if (argc != 4)
    error ("mi_cmd_data_write_memory: Usage: [-o COLUMN_OFFSET] ADDR FORMAT WORD-SIZE VALUE.");

  /* Extract all the arguments.  */
  /* Start address of the memory dump.  */
  addr = parse_and_eval_address (argv[0]);
  /* The format character to use when displaying a memory word.  See
     the ``x'' command.  */
  word_format = argv[1][0];
  /* The size of the memory word. */
  word_size = atol (argv[2]);

  /* Calculate the real address of the write destination.  */
  addr += (offset * word_size);

  /* Get the value as a number.  */
  value = parse_and_eval_address (argv[3]);
  /* Get the value into an array.  */
  buffer = xmalloc (word_size);
  old_chain = make_cleanup (xfree, buffer);
  store_signed_integer (buffer, word_size, byte_order, value);
  /* Write it down to memory.  */
  write_memory (addr, buffer, word_size);
  /* Free the buffer.  */
  do_cleanups (old_chain);
}

void
mi_cmd_enable_timings (char *command, char **argv, int argc)
{
  if (argc == 0)
    do_timings = 1;
  else if (argc == 1)
    {
      if (strcmp (argv[0], "yes") == 0)
	do_timings = 1;
      else if (strcmp (argv[0], "no") == 0)
	do_timings = 0;
      else
	goto usage_error;
    }
  else
    goto usage_error;
    
  return;

 usage_error:
  error ("mi_cmd_enable_timings: Usage: %s {yes|no}", command);
}

void
mi_cmd_list_features (char *command, char **argv, int argc)
{
  if (argc == 0)
    {
      struct cleanup *cleanup = NULL;
      cleanup = make_cleanup_ui_out_list_begin_end (uiout, "features");      

      ui_out_field_string (uiout, NULL, "frozen-varobjs");
      ui_out_field_string (uiout, NULL, "pending-breakpoints");
      ui_out_field_string (uiout, NULL, "thread-info");
      
#if HAVE_PYTHON
      ui_out_field_string (uiout, NULL, "python");
#endif
      
      do_cleanups (cleanup);
      return;
    }

  error ("-list-features should be passed no arguments");
}

void
mi_cmd_list_target_features (char *command, char **argv, int argc)
{
  if (argc == 0)
    {
      struct cleanup *cleanup = NULL;
      cleanup = make_cleanup_ui_out_list_begin_end (uiout, "features");      

      if (target_can_async_p ())
	ui_out_field_string (uiout, NULL, "async");
      
      do_cleanups (cleanup);
      return;
    }

  error ("-list-target-features should be passed no arguments");
}

/* Execute a command within a safe environment.
   Return <0 for error; >=0 for ok.

   args->action will tell mi_execute_command what action
   to perfrom after the given command has executed (display/suppress
   prompt, display error). */

static void
captured_mi_execute_command (struct ui_out *uiout, void *data)
{
  struct cleanup *cleanup;
  struct mi_parse *context = (struct mi_parse *) data;

  if (do_timings)
    current_command_ts = context->cmd_start;

  current_token = xstrdup (context->token);
  cleanup = make_cleanup (free_current_contents, &current_token);

  running_result_record_printed = 0;
  mi_proceeded = 0;
  switch (context->op)
    {
    case MI_COMMAND:
      /* A MI command was read from the input stream.  */
      if (mi_debug_p)
	/* FIXME: gdb_???? */
	fprintf_unfiltered (raw_stdout, " token=`%s' command=`%s' args=`%s'\n",
			    context->token, context->command, context->args);


      mi_cmd_execute (context);

      /* Print the result if there were no errors.

	 Remember that on the way out of executing a command, you have
	 to directly use the mi_interp's uiout, since the command could 
	 have reset the interpreter, in which case the current uiout 
	 will most likely crash in the mi_out_* routines.  */
      if (!running_result_record_printed)
	{
	  fputs_unfiltered (context->token, raw_stdout);
	  /* There's no particularly good reason why target-connect results
	     in not ^done.  Should kill ^connected for MI3.  */
	  fputs_unfiltered (strcmp (context->command, "target-select") == 0
			    ? "^connected" : "^done", raw_stdout);
	  mi_out_put (uiout, raw_stdout);
	  mi_out_rewind (uiout);
	  mi_print_timing_maybe ();
	  fputs_unfiltered ("\n", raw_stdout);
	}
      else
	    /* The command does not want anything to be printed.  In that
	       case, the command probably should not have written anything
	       to uiout, but in case it has written something, discard it.  */
	mi_out_rewind (uiout);
      break;

    case CLI_COMMAND:
      {
	char *argv[2];
	/* A CLI command was read from the input stream.  */
	/* This "feature" will be removed as soon as we have a
	   complete set of mi commands.  */
	/* Echo the command on the console.  */
	fprintf_unfiltered (gdb_stdlog, "%s\n", context->command);
	/* Call the "console" interpreter.  */
	argv[0] = "console";
	argv[1] = context->command;
	mi_cmd_interpreter_exec ("-interpreter-exec", argv, 2);

	/* If we changed interpreters, DON'T print out anything.  */
	if (current_interp_named_p (INTERP_MI)
	    || current_interp_named_p (INTERP_MI1)
	    || current_interp_named_p (INTERP_MI2)
	    || current_interp_named_p (INTERP_MI3))
	  {
	    if (!running_result_record_printed)
	      {
		fputs_unfiltered (context->token, raw_stdout);
		fputs_unfiltered ("^done", raw_stdout);
		mi_out_put (uiout, raw_stdout);
		mi_out_rewind (uiout);
		mi_print_timing_maybe ();
		fputs_unfiltered ("\n", raw_stdout);		
	      }
	    else
	      mi_out_rewind (uiout);
	  }
	break;
      }

    }

  do_cleanups (cleanup);

  return;
}


void
mi_execute_command (char *cmd, int from_tty)
{
  struct mi_parse *command;
  struct ui_out *saved_uiout = uiout;

  /* This is to handle EOF (^D). We just quit gdb.  */
  /* FIXME: we should call some API function here.  */
  if (cmd == 0)
    quit_force (NULL, from_tty);

  command = mi_parse (cmd);

  if (command != NULL)
    {
      struct gdb_exception result;
      ptid_t previous_ptid = inferior_ptid;

      if (do_timings)
	{
	  command->cmd_start = (struct mi_timestamp *)
	    xmalloc (sizeof (struct mi_timestamp));
	  timestamp (command->cmd_start);
	}

      result = catch_exception (uiout, captured_mi_execute_command, command,
				RETURN_MASK_ALL);
      if (result.reason < 0)
	{
	  /* The command execution failed and error() was called
	     somewhere.  */
	  fputs_unfiltered (command->token, raw_stdout);
	  fputs_unfiltered ("^error,msg=\"", raw_stdout);
	  if (result.message == NULL)
	    fputs_unfiltered ("unknown error", raw_stdout);
	  else
	    fputstr_unfiltered (result.message, '"', raw_stdout);
	  fputs_unfiltered ("\"\n", raw_stdout);
	  mi_out_rewind (uiout);
	}

      if (/* The notifications are only output when the top-level
	     interpreter (specified on the command line) is MI.  */      
	  ui_out_is_mi_like_p (interp_ui_out (top_level_interpreter ()))
	  /* Don't try report anything if there are no threads -- 
	     the program is dead.  */
	  && thread_count () != 0
	  /* -thread-select explicitly changes thread. If frontend uses that
	     internally, we don't want to emit =thread-selected, since
	     =thread-selected is supposed to indicate user's intentions.  */
	  && strcmp (command->command, "thread-select") != 0)
	{
	  struct mi_interp *mi = top_level_interpreter_data ();
	  int report_change = 0;

	  if (command->thread == -1)
	    {
	      report_change = (!ptid_equal (previous_ptid, null_ptid)
			       && !ptid_equal (inferior_ptid, previous_ptid)
			       && !ptid_equal (inferior_ptid, null_ptid));
	    }
	  else if (!ptid_equal (inferior_ptid, null_ptid))
	    {
	      struct thread_info *ti = inferior_thread ();
	      report_change = (ti->num != command->thread);
	    }

	  if (report_change)
	    {     
	      struct thread_info *ti = inferior_thread ();
	      target_terminal_ours ();
	      fprintf_unfiltered (mi->event_channel, 
				  "thread-selected,id=\"%d\"",
				  ti->num);
	      gdb_flush (mi->event_channel);
	    }
	}

      mi_parse_free (command);
    }

  fputs_unfiltered ("(gdb) \n", raw_stdout);
  gdb_flush (raw_stdout);
  /* Print any buffered hook code.  */
  /* ..... */
}

static void
mi_cmd_execute (struct mi_parse *parse)
{
  struct cleanup *cleanup;
  int i;

  prepare_execute_command ();

  cleanup = make_cleanup (null_cleanup, NULL);

  if (parse->frame != -1 && parse->thread == -1)
    error (_("Cannot specify --frame without --thread"));

  if (parse->thread != -1)
    {
      struct thread_info *tp = find_thread_id (parse->thread);
      if (!tp)
	error (_("Invalid thread id: %d"), parse->thread);

      if (is_exited (tp->ptid))
	error (_("Thread id: %d has terminated"), parse->thread);

      switch_to_thread (tp->ptid);
    }

  if (parse->frame != -1)
    {
      struct frame_info *fid;
      int frame = parse->frame;
      fid = find_relative_frame (get_current_frame (), &frame);
      if (frame == 0)
	/* find_relative_frame was successful */
	select_frame (fid);
      else
	error (_("Invalid frame id: %d"), frame);
    }

  if (parse->cmd->argv_func != NULL)
    parse->cmd->argv_func (parse->command, parse->argv, parse->argc);
  else if (parse->cmd->cli.cmd != 0)
    {
      /* FIXME: DELETE THIS. */
      /* The operation is still implemented by a cli command.  */
      /* Must be a synchronous one.  */
      mi_execute_cli_command (parse->cmd->cli.cmd, parse->cmd->cli.args_p,
			      parse->args);
    }
  else
    {
      /* FIXME: DELETE THIS.  */
      struct ui_file *stb;

      stb = mem_fileopen ();

      fputs_unfiltered ("Undefined mi command: ", stb);
      fputstr_unfiltered (parse->command, '"', stb);
      fputs_unfiltered (" (missing implementation)", stb);

      make_cleanup_ui_file_delete (stb);
      error_stream (stb);
    }
  do_cleanups (cleanup);
}

/* FIXME: This is just a hack so we can get some extra commands going.
   We don't want to channel things through the CLI, but call libgdb directly.
   Use only for synchronous commands.  */

void
mi_execute_cli_command (const char *cmd, int args_p, const char *args)
{
  if (cmd != 0)
    {
      struct cleanup *old_cleanups;
      char *run;
      if (args_p)
	run = xstrprintf ("%s %s", cmd, args);
      else
	run = xstrdup (cmd);
      if (mi_debug_p)
	/* FIXME: gdb_???? */
	fprintf_unfiltered (gdb_stdout, "cli=%s run=%s\n",
			    cmd, run);
      old_cleanups = make_cleanup (xfree, run);
      execute_command ( /*ui */ run, 0 /*from_tty */ );
      do_cleanups (old_cleanups);
      return;
    }
}

void
mi_execute_async_cli_command (char *cli_command, char **argv, int argc)
{
  struct cleanup *old_cleanups;
  char *run;

  if (target_can_async_p ())
    run = xstrprintf ("%s %s&", cli_command, argc ? *argv : "");
  else
    run = xstrprintf ("%s %s", cli_command, argc ? *argv : "");
  old_cleanups = make_cleanup (xfree, run);  

  execute_command ( /*ui */ run, 0 /*from_tty */ );

  if (target_can_async_p ())
    {
      /* If we're not executing, an exception should have been throw.  */
      gdb_assert (is_running (inferior_ptid));
      do_cleanups (old_cleanups);
    }
  else
    {
      /* Do this before doing any printing.  It would appear that some
         print code leaves garbage around in the buffer.  */
      do_cleanups (old_cleanups);
    }
}

void
mi_load_progress (const char *section_name,
		  unsigned long sent_so_far,
		  unsigned long total_section,
		  unsigned long total_sent,
		  unsigned long grand_total)
{
  struct timeval time_now, delta, update_threshold;
  static struct timeval last_update;
  static char *previous_sect_name = NULL;
  int new_section;
  struct ui_out *saved_uiout;

  /* This function is called through deprecated_show_load_progress
     which means uiout may not be correct.  Fix it for the duration
     of this function.  */
  saved_uiout = uiout;

  if (current_interp_named_p (INTERP_MI)
      || current_interp_named_p (INTERP_MI2))
    uiout = mi_out_new (2);
  else if (current_interp_named_p (INTERP_MI1))
    uiout = mi_out_new (1);
  else if (current_interp_named_p (INTERP_MI3))
    uiout = mi_out_new (3);
  else
    return;

  update_threshold.tv_sec = 0;
  update_threshold.tv_usec = 500000;
  gettimeofday (&time_now, NULL);

  delta.tv_usec = time_now.tv_usec - last_update.tv_usec;
  delta.tv_sec = time_now.tv_sec - last_update.tv_sec;

  if (delta.tv_usec < 0)
    {
      delta.tv_sec -= 1;
      delta.tv_usec += 1000000L;
    }

  new_section = (previous_sect_name ?
		 strcmp (previous_sect_name, section_name) : 1);
  if (new_section)
    {
      struct cleanup *cleanup_tuple;
      xfree (previous_sect_name);
      previous_sect_name = xstrdup (section_name);

      if (current_token)
	fputs_unfiltered (current_token, raw_stdout);
      fputs_unfiltered ("+download", raw_stdout);
      cleanup_tuple = make_cleanup_ui_out_tuple_begin_end (uiout, NULL);
      ui_out_field_string (uiout, "section", section_name);
      ui_out_field_int (uiout, "section-size", total_section);
      ui_out_field_int (uiout, "total-size", grand_total);
      do_cleanups (cleanup_tuple);
      mi_out_put (uiout, raw_stdout);
      fputs_unfiltered ("\n", raw_stdout);
      gdb_flush (raw_stdout);
    }

  if (delta.tv_sec >= update_threshold.tv_sec &&
      delta.tv_usec >= update_threshold.tv_usec)
    {
      struct cleanup *cleanup_tuple;
      last_update.tv_sec = time_now.tv_sec;
      last_update.tv_usec = time_now.tv_usec;
      if (current_token)
	fputs_unfiltered (current_token, raw_stdout);
      fputs_unfiltered ("+download", raw_stdout);
      cleanup_tuple = make_cleanup_ui_out_tuple_begin_end (uiout, NULL);
      ui_out_field_string (uiout, "section", section_name);
      ui_out_field_int (uiout, "section-sent", sent_so_far);
      ui_out_field_int (uiout, "section-size", total_section);
      ui_out_field_int (uiout, "total-sent", total_sent);
      ui_out_field_int (uiout, "total-size", grand_total);
      do_cleanups (cleanup_tuple);
      mi_out_put (uiout, raw_stdout);
      fputs_unfiltered ("\n", raw_stdout);
      gdb_flush (raw_stdout);
    }

  xfree (uiout);
  uiout = saved_uiout;
}

static void 
timestamp (struct mi_timestamp *tv)
  {
    long usec;
    gettimeofday (&tv->wallclock, NULL);
#ifdef HAVE_GETRUSAGE
    getrusage (RUSAGE_SELF, &rusage);
    tv->utime.tv_sec = rusage.ru_utime.tv_sec;
    tv->utime.tv_usec = rusage.ru_utime.tv_usec;
    tv->stime.tv_sec = rusage.ru_stime.tv_sec;
    tv->stime.tv_usec = rusage.ru_stime.tv_usec;
#else
    usec = get_run_time ();
    tv->utime.tv_sec = usec/1000000L;
    tv->utime.tv_usec = usec - 1000000L*tv->utime.tv_sec;
    tv->stime.tv_sec = 0;
    tv->stime.tv_usec = 0;
#endif
  }

static void 
print_diff_now (struct mi_timestamp *start)
  {
    struct mi_timestamp now;
    timestamp (&now);
    print_diff (start, &now);
  }

void
mi_print_timing_maybe (void)
{
  /* If the command is -enable-timing then do_timings may be
     true whilst current_command_ts is not initialized.  */
  if (do_timings && current_command_ts)
    print_diff_now (current_command_ts);
}

static long 
timeval_diff (struct timeval start, struct timeval end)
  {
    return ((end.tv_sec - start.tv_sec) * 1000000L)
      + (end.tv_usec - start.tv_usec);
  }

static void 
print_diff (struct mi_timestamp *start, struct mi_timestamp *end)
  {
    fprintf_unfiltered
      (raw_stdout,
       ",time={wallclock=\"%0.5f\",user=\"%0.5f\",system=\"%0.5f\"}", 
       timeval_diff (start->wallclock, end->wallclock) / 1000000.0, 
       timeval_diff (start->utime, end->utime) / 1000000.0, 
       timeval_diff (start->stime, end->stime) / 1000000.0);
  }

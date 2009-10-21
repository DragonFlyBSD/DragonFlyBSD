/* Multi-process control for GDB, the GNU debugger.

   Copyright (C) 2008, 2009 Free Software Foundation, Inc.

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
#include "target.h"
#include "command.h"
#include "gdbcmd.h"
#include "gdbthread.h"
#include "ui-out.h"
#include "observer.h"
#include "gdbthread.h"

void _initialize_inferiors (void);

static struct inferior *inferior_list = NULL;
static int highest_inferior_num;

/* Print notices on inferior events (attach, detach, etc.), set with
   `set print inferior-events'.  */
static int print_inferior_events = 0;

struct inferior*
current_inferior (void)
{
  struct inferior *inf = find_inferior_pid (ptid_get_pid (inferior_ptid));
  gdb_assert (inf);
  return inf;
}

static void
free_inferior (struct inferior *inf)
{
  discard_all_inferior_continuations (inf);
  xfree (inf->private);
  xfree (inf);
}

void
init_inferior_list (void)
{
  struct inferior *inf, *infnext;

  highest_inferior_num = 0;
  if (!inferior_list)
    return;

  for (inf = inferior_list; inf; inf = infnext)
    {
      infnext = inf->next;
      free_inferior (inf);
    }

  inferior_list = NULL;
}

struct inferior *
add_inferior_silent (int pid)
{
  struct inferior *inf;

  inf = xmalloc (sizeof (*inf));
  memset (inf, 0, sizeof (*inf));
  inf->pid = pid;

  inf->stop_soon = NO_STOP_QUIETLY;

  inf->num = ++highest_inferior_num;
  inf->next = inferior_list;
  inferior_list = inf;

  observer_notify_new_inferior (pid);

  return inf;
}

struct inferior *
add_inferior (int pid)
{
  struct inferior *inf = add_inferior_silent (pid);

  if (print_inferior_events)
    printf_unfiltered (_("[New inferior %d]\n"), pid);

  return inf;
}

struct delete_thread_of_inferior_arg
{
  int pid;
  int silent;
};

static int
delete_thread_of_inferior (struct thread_info *tp, void *data)
{
  struct delete_thread_of_inferior_arg *arg = data;

  if (ptid_get_pid (tp->ptid) == arg->pid)
    {
      if (arg->silent)
	delete_thread_silent (tp->ptid);
      else
	delete_thread (tp->ptid);
    }

  return 0;
}

/* If SILENT then be quiet -- don't announce a inferior death, or the
   exit of its threads.  */
static void
delete_inferior_1 (int pid, int silent)
{
  struct inferior *inf, *infprev;
  struct delete_thread_of_inferior_arg arg = { pid, silent };

  infprev = NULL;

  for (inf = inferior_list; inf; infprev = inf, inf = inf->next)
    if (inf->pid == pid)
      break;

  if (!inf)
    return;

  arg.pid = pid;
  arg.silent = silent;

  iterate_over_threads (delete_thread_of_inferior, &arg);

  /* Notify the observers before removing the inferior from the list,
     so that the observers have a change to look it up.  */
  observer_notify_inferior_exit (pid);

  if (infprev)
    infprev->next = inf->next;
  else
    inferior_list = inf->next;

  free_inferior (inf);
}

void
delete_inferior (int pid)
{
  delete_inferior_1 (pid, 0);

  if (print_inferior_events)
    printf_unfiltered (_("[Inferior %d exited]\n"), pid);
}

void
delete_inferior_silent (int pid)
{
  delete_inferior_1 (pid, 1);
}

void
detach_inferior (int pid)
{
  delete_inferior_1 (pid, 1);

  if (print_inferior_events)
    printf_unfiltered (_("[Inferior %d detached]\n"), pid);
}

void
discard_all_inferiors (void)
{
  struct inferior *inf, *infnext;

  for (inf = inferior_list; inf; inf = infnext)
    {
      infnext = inf->next;
      delete_inferior_silent (inf->pid);
    }
}

static struct inferior *
find_inferior_id (int num)
{
  struct inferior *inf;

  for (inf = inferior_list; inf; inf = inf->next)
    if (inf->num == num)
      return inf;

  return NULL;
}

struct inferior *
find_inferior_pid (int pid)
{
  struct inferior *inf;

  for (inf = inferior_list; inf; inf = inf->next)
    if (inf->pid == pid)
      return inf;

  return NULL;
}

struct inferior *
iterate_over_inferiors (int (*callback) (struct inferior *, void *),
			void *data)
{
  struct inferior *inf, *infnext;

  for (inf = inferior_list; inf; inf = infnext)
    {
      infnext = inf->next;
      if ((*callback) (inf, data))
	return inf;
    }

  return NULL;
}

int
valid_gdb_inferior_id (int num)
{
  struct inferior *inf;

  for (inf = inferior_list; inf; inf = inf->next)
    if (inf->num == num)
      return 1;

  return 0;
}

int
pid_to_gdb_inferior_id (int pid)
{
  struct inferior *inf;

  for (inf = inferior_list; inf; inf = inf->next)
    if (inf->pid == pid)
      return inf->num;

  return 0;
}

int
gdb_inferior_id_to_pid (int num)
{
  struct inferior *inferior = find_inferior_id (num);
  if (inferior)
    return inferior->pid;
  else
    return -1;
}

int
in_inferior_list (int pid)
{
  struct inferior *inf;

  for (inf = inferior_list; inf; inf = inf->next)
    if (inf->pid == pid)
      return 1;

  return 0;
}

int
have_inferiors (void)
{
  return inferior_list != NULL;
}

int
have_live_inferiors (void)
{
  /* The check on stratum suffices, as GDB doesn't currently support
     multiple target interfaces.  */
  return (current_target.to_stratum >= process_stratum && have_inferiors ());
}

/* Prints the list of inferiors and their details on UIOUT.  This is a
   version of 'info_inferior_command' suitable for use from MI.

   If REQUESTED_INFERIOR is not -1, it's the GDB id of the inferior that
   should be printed.  Otherwise, all inferiors are printed.  */
void
print_inferior (struct ui_out *uiout, int requested_inferior)
{
  struct inferior *inf;
  struct cleanup *old_chain;
  int inf_count = 0;

  /* Compute number of inferiors we will print.  */
  for (inf = inferior_list; inf; inf = inf->next)
    {
      struct cleanup *chain2;

      if (requested_inferior != -1 && inf->num != requested_inferior)
	continue;

      ++inf_count;
    }

  if (inf_count == 0)
    {
      ui_out_message (uiout, 0, "No inferiors.\n");
      return;
    }

  old_chain = make_cleanup_ui_out_table_begin_end (uiout, 3, inf_count,
						   "inferiors");
  ui_out_table_header (uiout, 1, ui_left, "current", "");
  ui_out_table_header (uiout, 4, ui_left, "number", "Num");
  ui_out_table_header (uiout, 17, ui_left, "target-id", "Description");
  ui_out_table_body (uiout);

  for (inf = inferior_list; inf; inf = inf->next)
    {
      struct cleanup *chain2;

      if (requested_inferior != -1 && inf->num != requested_inferior)
	continue;

      chain2 = make_cleanup_ui_out_tuple_begin_end (uiout, NULL);

      if (inf->pid == ptid_get_pid (inferior_ptid))
	ui_out_field_string (uiout, "current", "*");
      else
	ui_out_field_skip (uiout, "current");

      ui_out_field_int (uiout, "number", inf->num);
      ui_out_field_string (uiout, "target-id",
			   target_pid_to_str (pid_to_ptid (inf->pid)));

      ui_out_text (uiout, "\n");
      do_cleanups (chain2);
    }

  if (inferior_list
      && ptid_equal (inferior_ptid, null_ptid))
    ui_out_message (uiout, 0, "\n\
No selected inferior/thread.  See `help thread' or `help inferior'.\n");

  do_cleanups (old_chain);
}

static void
detach_inferior_command (char *args, int from_tty)
{
  int num, pid;
  struct thread_info *tp;

  if (!args || !*args)
    error (_("Requires argument (inferior id to detach)"));

  num = parse_and_eval_long (args);

  if (!valid_gdb_inferior_id (num))
    error (_("Inferior ID %d not known."), num);

  pid = gdb_inferior_id_to_pid (num);

  tp = any_thread_of_process (pid);
  if (!tp)
    error (_("Inferior has no threads."));

  switch_to_thread (tp->ptid);

  detach_command (NULL, from_tty);
}

static void
kill_inferior_command (char *args, int from_tty)
{
  int num, pid;
  struct thread_info *tp;

  if (!args || !*args)
    error (_("Requires argument (inferior id to kill)"));

  num = parse_and_eval_long (args);

  if (!valid_gdb_inferior_id (num))
    error (_("Inferior ID %d not known."), num);

  pid = gdb_inferior_id_to_pid (num);

  tp = any_thread_of_process (pid);
  if (!tp)
    error (_("Inferior has no threads."));

  switch_to_thread (tp->ptid);

  target_kill ();

  bfd_cache_close_all ();
}

static void
inferior_command (char *args, int from_tty)
{
  int num, pid;

  if (!have_inferiors ())
    error (_("No inferiors"));

  num = parse_and_eval_long (args);

  if (!valid_gdb_inferior_id (num))
    error (_("Inferior ID %d not known."), num);

  pid = gdb_inferior_id_to_pid (num);

  if (pid != ptid_get_pid (inferior_ptid))
    {
      struct thread_info *tp;

      tp = any_thread_of_process (pid);
      if (!tp)
	error (_("Inferior has no threads."));

      switch_to_thread (tp->ptid);
    }

  printf_filtered (_("[Switching to thread %d (%s)] "),
		   pid_to_thread_id (inferior_ptid),
		   target_pid_to_str (inferior_ptid));

  if (is_running (inferior_ptid))
    ui_out_text (uiout, "(running)\n");
  else
    {
      ui_out_text (uiout, "\n");
      print_stack_frame (get_selected_frame (NULL), 1, SRC_AND_LOC);
    }
}

/* Print information about currently known inferiors.  */

static void
info_inferiors_command (char *args, int from_tty)
{
  int requested = -1;

  if (args && *args)
    {
      requested = parse_and_eval_long (args);
      if (!valid_gdb_inferior_id (requested))
	error (_("Inferior ID %d not known."), requested);
    }

  print_inferior (uiout, requested);
}

/* Print notices when new inferiors are created and die.  */
static void
show_print_inferior_events (struct ui_file *file, int from_tty,
			   struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("Printing of inferior events is %s.\n"), value);
}

void
_initialize_inferiors (void)
{
  add_info ("inferiors", info_inferiors_command,
	    _("IDs of currently known inferiors."));

  add_setshow_boolean_cmd ("inferior-events", no_class,
         &print_inferior_events, _("\
Set printing of inferior events (e.g., inferior start and exit)."), _("\
Show printing of inferior events (e.g., inferior start and exit)."), NULL,
         NULL,
         show_print_inferior_events,
         &setprintlist, &showprintlist);

  add_cmd ("inferior", class_run, detach_inferior_command, _("\
Detach from inferior ID."),
	   &detachlist);

  add_cmd ("inferior", class_run, kill_inferior_command, _("\
Kill inferior ID."),
	   &killlist);

  add_cmd ("inferior", class_run, inferior_command, _("\
Use this command to switch between inferiors.\n\
The new inferior ID must be currently known."),
	   &cmdlist);
}

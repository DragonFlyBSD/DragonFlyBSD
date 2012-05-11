/* Skipping uninteresting files and functions while stepping.

   Copyright (C) 2011-2012 Free Software Foundation, Inc.

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
#include "skip.h"
#include "value.h"
#include "valprint.h"
#include "ui-out.h"
#include "gdb_string.h"
#include "symtab.h"
#include "gdbcmd.h"
#include "command.h"
#include "completer.h"
#include "stack.h"
#include "cli/cli-utils.h"
#include "arch-utils.h"
#include "linespec.h"
#include "objfiles.h"
#include "exceptions.h"
#include "breakpoint.h" /* for get_sal_arch () */

struct skiplist_entry
{
  int number;

  /* NULL if this isn't a skiplist entry for an entire file.
     The skiplist entry owns this pointer.  */
  char *filename;

  /* The name of the marked-for-skip function, if this is a skiplist
     entry for a function.  Note that this might be non-null even if
     the pc is 0 if the entry is pending a shared library load.

     The skiplist entry owns this pointer.  */
  char *function_name;

  /* 0 if this is a skiplist entry for an entire file, or if this
     entry will be on a function, pending a shared library load.  */
  CORE_ADDR pc;

  /* Architecture we used to create the skiplist entry. May be null
     if the entry is pending a shared library load.  */
  struct gdbarch *gdbarch;

  int enabled;
  int pending;

  struct skiplist_entry *next;
};

static void skip_function_command (char *arg, int from_tty);
static void skip_file_command (char *arg, int from_tty);
static void skip_info (char *arg, int from_tty);

static void add_skiplist_entry (struct skiplist_entry *e);
static void skip_function_pc (CORE_ADDR pc, char *name,
			      struct gdbarch *arch,
			      int pending);

static struct skiplist_entry *skiplist_entry_chain;
static int skiplist_entry_count;

#define ALL_SKIPLIST_ENTRIES(E) \
  for (E = skiplist_entry_chain; E; E = E->next)

#define ALL_SKIPLIST_ENTRIES_SAFE(E,TMP) \
  for (E = skiplist_entry_chain;         \
       E ? (TMP = E->next, 1) : 0;       \
       E = TMP)

static void
skip_file_command (char *arg, int from_tty)
{
  struct skiplist_entry *e;
  struct symtab *symtab;
  int pending = 0;
  char *filename = 0;

  /* If no argument was given, try to default to the last
     displayed codepoint.  */
  if (arg == 0)
    {
      symtab = get_last_displayed_symtab ();
      if (symtab == 0)
	error (_("No default file now."));
      else
	filename = symtab->filename;
    }
  else
    {
      symtab = lookup_symtab (arg);
      if (symtab == 0)
	{
	  fprintf_filtered (gdb_stderr, _("No source file named %s.\n"), arg);
	  if (!nquery (_("\
Ignore file pending future shared library load? ")))
	    return;

	  pending = 1;
	  filename = arg;
	}
      else
	filename = symtab->filename;
    }

  e = XZALLOC (struct skiplist_entry);
  e->filename = xstrdup (filename);
  e->enabled = 1;
  e->pending = pending;
  if (symtab != 0)
    e->gdbarch = get_objfile_arch (symtab->objfile);

  add_skiplist_entry (e);

  printf_filtered (_("File %s will be skipped when stepping.\n"), filename);
}

static void
skip_function_command (char *arg, int from_tty)
{
  CORE_ADDR func_pc;
  char *name = NULL;

  /* Default to the current function if no argument is given.  */
  if (arg == 0)
    {
      CORE_ADDR pc;
      if (!last_displayed_sal_is_valid ())
	error (_("No default function now."));

      pc = get_last_displayed_addr ();
      if (!find_pc_partial_function (pc, &name, &func_pc, 0))
	{
	  error (_("No function found containing current program point %s."),
		  paddress (get_current_arch (), pc));
	}
      skip_function_pc (func_pc, name, get_current_arch (), 0);
    }
  else
    {
      /* Decode arg.  We set funfirstline=1 so decode_line_1 will give us the
	 first line of the function specified, if it can, and so that we'll
	 reject variable names and the like.  */

      int i;
      int pending = 0;
      char *orig_arg = arg; /* decode_line_1 modifies the arg pointer.  */
      volatile struct gdb_exception decode_exception;
      struct symtabs_and_lines sals = { 0 };

      TRY_CATCH (decode_exception, RETURN_MASK_ERROR)
	{
	  sals = decode_line_1 (&arg, DECODE_LINE_FUNFIRSTLINE, 0, 0);
	}

      if (decode_exception.reason < 0)
        {
          if (decode_exception.error != NOT_FOUND_ERROR)
            throw_exception (decode_exception);

	  fprintf_filtered (gdb_stderr,
			    _("No function found named %s.\n"), orig_arg);

	  if (nquery (_("\
Ignore function pending future shared library load? ")))
	    {
	      /* Add the pending skiplist entry.  */
	      skip_function_pc (0, orig_arg, 0, 1);
	    }

	  return;
	}

      if (sals.nelts > 1)
	error (_("Specify just one function at a time."));
      if (strlen (arg) != 0)
	error (_("Junk at end of arguments."));

      /* The pc decode_line_1 gives us is the first line of the function,
	 but we actually want the line before that.  The call to
	 find_pc_partial_function gets us the value we actually want.  */
      {
	struct symtab_and_line sal = sals.sals[0];
	CORE_ADDR pc = sal.pc;
	CORE_ADDR func_start = 0;
	struct gdbarch *arch = get_sal_arch (sal);

	if (!find_pc_partial_function (pc, &name, &func_start, 0))
	  {
	    error (_("No function found containing program point %s."),
		     paddress (arch, pc));
	  }

	skip_function_pc (func_start, name, arch, 0);
      }
    }
}

static void
skip_info (char *arg, int from_tty)
{
  struct skiplist_entry *e;
  int num_printable_entries = 0;
  int address_width = 10;
  struct value_print_options opts;
  struct cleanup *tbl_chain;

  get_user_print_options (&opts);

  /* Count the number of rows in the table and see if we need space for a
     64-bit address anywhere.  */
  ALL_SKIPLIST_ENTRIES (e)
    if (arg == 0 || number_is_in_list (arg, e->number))
      {
	num_printable_entries++;
	if (e->gdbarch && gdbarch_addr_bit (e->gdbarch) > 32)
	  address_width = 18;
      }

  if (num_printable_entries == 0)
    {
      if (arg == 0)
	ui_out_message (current_uiout, 0, _("\
Not skipping any files or functions.\n"));
      else
	ui_out_message (current_uiout, 0,
			_("No skiplist entries found with number %s.\n"), arg);

      return;
    }

  if (opts.addressprint)
    tbl_chain = make_cleanup_ui_out_table_begin_end (current_uiout, 5,
						     num_printable_entries,
						     "SkiplistTable");
  else
    tbl_chain
       = make_cleanup_ui_out_table_begin_end (current_uiout, 4,
					      num_printable_entries,
					      "SkiplistTable");

  ui_out_table_header (current_uiout, 7, ui_left, "number", "Num");      /* 1 */
  ui_out_table_header (current_uiout, 14, ui_left, "type", "Type");      /* 2 */
  ui_out_table_header (current_uiout, 3, ui_left, "enabled", "Enb");     /* 3 */
  if (opts.addressprint)
    {
      ui_out_table_header (current_uiout, address_width, ui_left,
			   "addr", "Address");                           /* 4 */
    }
  ui_out_table_header (current_uiout, 40, ui_noalign, "what", "What");   /* 5 */
  ui_out_table_body (current_uiout);

  ALL_SKIPLIST_ENTRIES (e)
    {
      struct cleanup *entry_chain;

      QUIT;
      if (arg != 0 && !number_is_in_list (arg, e->number))
	continue;

      entry_chain = make_cleanup_ui_out_tuple_begin_end (current_uiout,
							 "blklst-entry");
      ui_out_field_int (current_uiout, "number", e->number);             /* 1 */

      if (e->function_name != 0)
	ui_out_field_string (current_uiout, "type", "function");         /* 2 */
      else if (e->filename != 0)
	ui_out_field_string (current_uiout, "type", "file");             /* 2 */
      else
	internal_error (__FILE__, __LINE__, _("\
Skiplist entry should have either a filename or a function name."));

      if (e->enabled)
	ui_out_field_string (current_uiout, "enabled", "y");             /* 3 */
      else
	ui_out_field_string (current_uiout, "enabled", "n");             /* 3 */

      if (opts.addressprint)
	{
	  if (e->pc != 0)
	    ui_out_field_core_addr (current_uiout, "addr",
				    e->gdbarch, e->pc);                  /* 4 */
	  else
	    ui_out_field_string (current_uiout, "addr", "");             /* 4 */
	}

      if (!e->pending && e->function_name != 0)
	{
	   struct symbol *sym;

	   gdb_assert (e->pc != 0);
	   sym = find_pc_function (e->pc);
	   if (sym)
	     ui_out_field_fmt (current_uiout, "what", "%s at %s:%d",
			       sym->ginfo.name,
			       sym->symtab->filename,
			       sym->line);
	   else
	     ui_out_field_string (current_uiout, "what", "?");
	}
      else if (e->pending && e->function_name != 0)
	{
	  ui_out_field_fmt (current_uiout, "what", "%s (PENDING)",
			    e->function_name);
	}
      else if (!e->pending && e->filename != 0)
	ui_out_field_string (current_uiout, "what", e->filename);
      else if (e->pending && e->filename != 0)
	ui_out_field_fmt (current_uiout, "what", "%s (PENDING)",
			  e->filename);

      ui_out_text (current_uiout, "\n");
      do_cleanups (entry_chain);
    }

  do_cleanups (tbl_chain);
}

static void
skip_enable_command (char *arg, int from_tty)
{
  struct skiplist_entry *e;
  int found = 0;

  ALL_SKIPLIST_ENTRIES (e)
    if (arg == 0 || number_is_in_list (arg, e->number))
      {
        e->enabled = 1;
        found = 1;
      }

  if (!found)
    error (_("No skiplist entries found with number %s."), arg);
}

static void
skip_disable_command (char *arg, int from_tty)
{
  struct skiplist_entry *e;
  int found = 0;

  ALL_SKIPLIST_ENTRIES (e)
    if (arg == 0 || number_is_in_list (arg, e->number))
      {
	e->enabled = 0;
        found = 1;
      }

  if (!found)
    error (_("No skiplist entries found with number %s."), arg);
}

static void
skip_delete_command (char *arg, int from_tty)
{
  struct skiplist_entry *e, *temp, *b_prev;
  int found = 0;

  b_prev = 0;
  ALL_SKIPLIST_ENTRIES_SAFE (e, temp)
    if (arg == 0 || number_is_in_list (arg, e->number))
      {
	if (b_prev != 0)
	  b_prev->next = e->next;
	else
	  skiplist_entry_chain = e->next;

	xfree (e->function_name);
	xfree (e->filename);
	xfree (e);
        found = 1;
      }
    else
      {
	b_prev = e;
      }

  if (!found)
    error (_("No skiplist entries found with number %s."), arg);
}

/* Create a skiplist entry for the given pc corresponding to the given
   function name and add it to the list.  */

static void
skip_function_pc (CORE_ADDR pc, char *name, struct gdbarch *arch,
		  int pending)
{
  struct skiplist_entry *e = XZALLOC (struct skiplist_entry);

  e->pc = pc;
  e->gdbarch = arch;
  e->enabled = 1;
  e->pending = pending;
  e->function_name = xstrdup (name);

  add_skiplist_entry (e);

  if (!pending)
    printf_filtered (_("Function %s at %s will be skipped when stepping.\n"),
		     name, paddress (get_current_arch (), pc));
  else
    printf_filtered (_("Function %s will be skipped when stepping, "
		       "pending shared library load.\n"),
		     name);
}

/* Add the given skiplist entry to our list, and set the entry's number.  */

static void
add_skiplist_entry (struct skiplist_entry *e)
{
  struct skiplist_entry *e1;

  e->number = ++skiplist_entry_count;

  /* Add to the end of the chain so that the list of
     skiplist entries will be in numerical order.  */

  e1 = skiplist_entry_chain;
  if (e1 == 0)
    skiplist_entry_chain = e;
  else
    {
      while (e1->next)
	e1 = e1->next;
      e1->next = e;
    }
}

/* Does the given pc correspond to the beginning of a skipped function? */

int
function_pc_is_marked_for_skip (CORE_ADDR pc)
{
  int searched_for_sal = 0;
  struct symtab_and_line sal;
  char *filename = NULL;
  struct skiplist_entry *e;

  ALL_SKIPLIST_ENTRIES (e)
    {
      if (!e->enabled || e->pending)
	continue;

      /* Does the pc we're stepping into match e's stored pc? */
      if (e->pc != 0 && pc == e->pc)
	return 1;

      if (e->filename != 0)
	{
	  /* Get the filename corresponding to this pc, if we haven't
	   * yet.  */
	  if (!searched_for_sal)
	    {
	      sal = find_pc_line (pc, 0);
              if (sal.symtab != 0)
                filename = sal.symtab->filename;
	      searched_for_sal = 1;
	    }
	  if (filename != 0 && strcmp (filename, e->filename) == 0)
	    return 1;
	}
    }

  return 0;
}

/* Re-set the skip list after symbols have been re-loaded.  */
void
skip_re_set (void)
{
  struct skiplist_entry *e;

  ALL_SKIPLIST_ENTRIES (e)
    {
      if (e->filename != 0)
	{
	  /* If it's an entry telling us to skip a file, but the entry is
	     currently pending a solib load, let's see if we now know
	     about the file.  */
	  struct symtab *symtab = lookup_symtab (e->filename);
	  if (symtab != 0)
	    {
	      xfree (e->filename);
	      e->filename = xstrdup (symtab->filename);
	      e->gdbarch = get_objfile_arch (symtab->objfile);
	      e->pending = 0;
	    }
	  else
	    {
	      e->pending = 1;
	    }
	}
      else if (e->function_name != 0)
        {
	  char *func_name = e->function_name;
	  struct symtabs_and_lines sals = { 0 };
	  volatile struct gdb_exception decode_exception;

	  TRY_CATCH (decode_exception, RETURN_MASK_ERROR)
	    {
	      sals = decode_line_1 (&func_name, DECODE_LINE_FUNFIRSTLINE, 0, 0);
	    }

	  if (decode_exception.reason >= 0
              && sals.nelts == 1 && strlen (func_name) == 0)
	    {
	      struct symtab_and_line sal = sals.sals[0];
	      CORE_ADDR pc = sal.pc;
	      CORE_ADDR func_start = 0;
	      struct gdbarch *arch = get_sal_arch (sal);
              char *func_name;

	      if (find_pc_partial_function (pc, &func_name, &func_start, 0))
		{
		  e->pending = 0;
                  e->function_name = xstrdup (func_name);
		  e->pc = func_start;
		  e->gdbarch = arch;
		}
	    }
	  else
	    {
	      e->pending = 1;
	    }
        }
    }
}

void
_initialize_step_skip (void)
{
  struct cmd_list_element *c;

  skiplist_entry_chain = 0;
  skiplist_entry_count = 0;

  add_prefix_cmd ("skip", class_breakpoint, skip_function_command, _("\
Ignore a function while stepping.\n\
Usage: skip [FUNCTION NAME]\n\
If no function name is given, ignore the current function."),
                  &skiplist, "skip ", 1, &cmdlist);

  c = add_cmd ("file", class_breakpoint, skip_file_command, _("\
Ignore a file while stepping.\n\
Usage: skip file [FILENAME]\n\
If no filename is given, ignore the current file."),
	       &skiplist);
  set_cmd_completer (c, filename_completer);

  c = add_cmd ("function", class_breakpoint, skip_function_command, _("\
Ignore a function while stepping.\n\
Usage: skip function [FUNCTION NAME]\n\
If no function name is given, skip the current function."),
	       &skiplist);
  set_cmd_completer (c, location_completer);

  add_cmd ("enable", class_breakpoint, skip_enable_command, _("\
Enable skip entries.  You can specify numbers (e.g. \"skip enable 1 3\"), \
ranges (e.g. \"skip enable 4-8\"), or both (e.g. \"skip enable 1 3 4-8\").\n\n\
If you don't specify any numbers or ranges, we'll enable all skip entries.\n\n\
Usage: skip enable [NUMBERS AND/OR RANGES]"),
	   &skiplist);

  add_cmd ("disable", class_breakpoint, skip_disable_command, _("\
Disable skip entries.  You can specify numbers (e.g. \"skip disable 1 3\"), \
ranges (e.g. \"skip disable 4-8\"), or both (e.g. \"skip disable 1 3 4-8\").\n\n\
If you don't specify any numbers or ranges, we'll disable all skip entries.\n\n\
Usage: skip disable [NUMBERS AND/OR RANGES]"),
	   &skiplist);

  add_cmd ("delete", class_breakpoint, skip_delete_command, _("\
Delete skip entries.  You can specify numbers (e.g. \"skip delete 1 3\"), \
ranges (e.g. \"skip delete 4-8\"), or both (e.g. \"skip delete 1 3 4-8\").\n\n\
If you don't specify any numbers or ranges, we'll delete all skip entries.\n\n\
Usage: skip delete [NUMBERS AND/OR RANGES]"),
           &skiplist);

  add_info ("skip", skip_info, _("\
Display the status of skips.  You can specify numbers (e.g. \"skip info 1 3\"), \
ranges (e.g. \"skip info 4-8\"), or both (e.g. \"skip info 1 3 4-8\").\n\n\
If you don't specify any numbers or ranges, we'll show all skips.\n\n\
Usage: skip info [NUMBERS AND/OR RANGES]\n\
The \"Type\" column indicates one of:\n\
\tfile        - ignored file\n\
\tfunction    - ignored function"));
}

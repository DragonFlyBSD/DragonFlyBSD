/* Everything about breakpoints, for GDB.

   Copyright (C) 1986, 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995,
   1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007,
   2008, 2009 Free Software Foundation, Inc.

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
#include <ctype.h>
#include "hashtab.h"
#include "symtab.h"
#include "frame.h"
#include "breakpoint.h"
#include "tracepoint.h"
#include "gdbtypes.h"
#include "expression.h"
#include "gdbcore.h"
#include "gdbcmd.h"
#include "value.h"
#include "command.h"
#include "inferior.h"
#include "gdbthread.h"
#include "target.h"
#include "language.h"
#include "gdb_string.h"
#include "demangle.h"
#include "annotate.h"
#include "symfile.h"
#include "objfiles.h"
#include "source.h"
#include "linespec.h"
#include "completer.h"
#include "gdb.h"
#include "ui-out.h"
#include "cli/cli-script.h"
#include "gdb_assert.h"
#include "block.h"
#include "solib.h"
#include "solist.h"
#include "observer.h"
#include "exceptions.h"
#include "memattr.h"
#include "ada-lang.h"
#include "top.h"
#include "wrapper.h"
#include "valprint.h"
#include "jit.h"
#include "xml-syscall.h"

/* readline include files */
#include "readline/readline.h"
#include "readline/history.h"

/* readline defines this.  */
#undef savestring

#include "mi/mi-common.h"

/* Arguments to pass as context to some catch command handlers.  */
#define CATCH_PERMANENT ((void *) (uintptr_t) 0)
#define CATCH_TEMPORARY ((void *) (uintptr_t) 1)

/* Prototypes for local functions. */

static void enable_delete_command (char *, int);

static void enable_delete_breakpoint (struct breakpoint *);

static void enable_once_command (char *, int);

static void enable_once_breakpoint (struct breakpoint *);

static void disable_command (char *, int);

static void enable_command (char *, int);

static void map_breakpoint_numbers (char *, void (*)(struct breakpoint *));

static void ignore_command (char *, int);

static int breakpoint_re_set_one (void *);

static void clear_command (char *, int);

static void catch_command (char *, int);

static void watch_command (char *, int);

static int can_use_hardware_watchpoint (struct value *);

static void break_command_1 (char *, int, int);

static void mention (struct breakpoint *);

/* This function is used in gdbtk sources and thus can not be made static.  */
struct breakpoint *set_raw_breakpoint (struct gdbarch *gdbarch,
					      struct symtab_and_line,
					      enum bptype);

static void check_duplicates (struct breakpoint *);

static void breakpoint_adjustment_warning (CORE_ADDR, CORE_ADDR, int, int);

static CORE_ADDR adjust_breakpoint_address (struct gdbarch *gdbarch,
					    CORE_ADDR bpaddr,
                                            enum bptype bptype);

static void describe_other_breakpoints (struct gdbarch *, CORE_ADDR,
					struct obj_section *, int);

static void breakpoints_info (char *, int);

static void breakpoint_1 (int, int);

static bpstat bpstat_alloc (const struct bp_location *, bpstat);

static int breakpoint_cond_eval (void *);

static void cleanup_executing_breakpoints (void *);

static void commands_command (char *, int);

static void condition_command (char *, int);

static int get_number_trailer (char **, int);

void set_breakpoint_count (int);

typedef enum
  {
    mark_inserted,
    mark_uninserted
  }
insertion_state_t;

static int remove_breakpoint (struct bp_location *, insertion_state_t);

static enum print_stop_action print_it_typical (bpstat);

static enum print_stop_action print_bp_stop_message (bpstat bs);

static int watchpoint_check (void *);

static void maintenance_info_breakpoints (char *, int);

static int hw_breakpoint_used_count (void);

static int hw_watchpoint_used_count (enum bptype, int *);

static void hbreak_command (char *, int);

static void thbreak_command (char *, int);

static void watch_command_1 (char *, int, int);

static void rwatch_command (char *, int);

static void awatch_command (char *, int);

static void do_enable_breakpoint (struct breakpoint *, enum bpdisp);

static void stop_command (char *arg, int from_tty);

static void stopin_command (char *arg, int from_tty);

static void stopat_command (char *arg, int from_tty);

static char *ep_parse_optional_if_clause (char **arg);

static char *ep_parse_optional_filename (char **arg);

static void catch_exception_command_1 (enum exception_event_kind ex_event, 
				       char *arg, int tempflag, int from_tty);

static void tcatch_command (char *arg, int from_tty);

static void ep_skip_leading_whitespace (char **s);

static int single_step_breakpoint_inserted_here_p (CORE_ADDR pc);

static void free_bp_location (struct bp_location *loc);

static struct bp_location *allocate_bp_location (struct breakpoint *bpt);

static void update_global_location_list (int);

static void update_global_location_list_nothrow (int);

static int is_hardware_watchpoint (struct breakpoint *bpt);

static void insert_breakpoint_locations (void);

static int syscall_catchpoint_p (struct breakpoint *b);

static void tracepoints_info (char *, int);

static void delete_trace_command (char *, int);

static void enable_trace_command (char *, int);

static void disable_trace_command (char *, int);

static void trace_pass_command (char *, int);

static void skip_prologue_sal (struct symtab_and_line *sal);


/* Flag indicating that a command has proceeded the inferior past the
   current breakpoint.  */

static int breakpoint_proceeded;

static const char *
bpdisp_text (enum bpdisp disp)
{
  /* NOTE: the following values are a part of MI protocol and represent
     values of 'disp' field returned when inferior stops at a breakpoint.  */
  static char *bpdisps[] = {"del", "dstp", "dis", "keep"};
  return bpdisps[(int) disp];
}

/* Prototypes for exported functions. */
/* If FALSE, gdb will not use hardware support for watchpoints, even
   if such is available. */
static int can_use_hw_watchpoints;

static void
show_can_use_hw_watchpoints (struct ui_file *file, int from_tty,
			     struct cmd_list_element *c,
			     const char *value)
{
  fprintf_filtered (file, _("\
Debugger's willingness to use watchpoint hardware is %s.\n"),
		    value);
}

/* If AUTO_BOOLEAN_FALSE, gdb will not attempt to create pending breakpoints.
   If AUTO_BOOLEAN_TRUE, gdb will automatically create pending breakpoints
   for unrecognized breakpoint locations.  
   If AUTO_BOOLEAN_AUTO, gdb will query when breakpoints are unrecognized.  */
static enum auto_boolean pending_break_support;
static void
show_pending_break_support (struct ui_file *file, int from_tty,
			    struct cmd_list_element *c,
			    const char *value)
{
  fprintf_filtered (file, _("\
Debugger's behavior regarding pending breakpoints is %s.\n"),
		    value);
}

/* If 1, gdb will automatically use hardware breakpoints for breakpoints
   set with "break" but falling in read-only memory. 
   If 0, gdb will warn about such breakpoints, but won't automatically
   use hardware breakpoints.  */
static int automatic_hardware_breakpoints;
static void
show_automatic_hardware_breakpoints (struct ui_file *file, int from_tty,
				     struct cmd_list_element *c,
				     const char *value)
{
  fprintf_filtered (file, _("\
Automatic usage of hardware breakpoints is %s.\n"),
		    value);
}

/* If on, gdb will keep breakpoints inserted even as inferior is
   stopped, and immediately insert any new breakpoints.  If off, gdb
   will insert breakpoints into inferior only when resuming it, and
   will remove breakpoints upon stop.  If auto, GDB will behave as ON
   if in non-stop mode, and as OFF if all-stop mode.*/

static const char always_inserted_auto[] = "auto";
static const char always_inserted_on[] = "on";
static const char always_inserted_off[] = "off";
static const char *always_inserted_enums[] = {
  always_inserted_auto,
  always_inserted_off,
  always_inserted_on,
  NULL
};
static const char *always_inserted_mode = always_inserted_auto;
static void
show_always_inserted_mode (struct ui_file *file, int from_tty,
		     struct cmd_list_element *c, const char *value)
{
  if (always_inserted_mode == always_inserted_auto)
    fprintf_filtered (file, _("\
Always inserted breakpoint mode is %s (currently %s).\n"),
		      value,
		      breakpoints_always_inserted_mode () ? "on" : "off");
  else
    fprintf_filtered (file, _("Always inserted breakpoint mode is %s.\n"), value);
}

int
breakpoints_always_inserted_mode (void)
{
  return (always_inserted_mode == always_inserted_on
	  || (always_inserted_mode == always_inserted_auto && non_stop));
}

void _initialize_breakpoint (void);

/* Are we executing breakpoint commands?  */
static int executing_breakpoint_commands;

/* Are overlay event breakpoints enabled? */
static int overlay_events_enabled;

/* Are we executing startup code?  */
static int executing_startup;

/* Walk the following statement or block through all breakpoints.
   ALL_BREAKPOINTS_SAFE does so even if the statment deletes the current
   breakpoint.  */

#define ALL_BREAKPOINTS(B)  for (B = breakpoint_chain; B; B = B->next)

#define ALL_BREAKPOINTS_SAFE(B,TMP)	\
	for (B = breakpoint_chain;	\
	     B ? (TMP=B->next, 1): 0;	\
	     B = TMP)

/* Similar iterators for the low-level breakpoints.  */

#define ALL_BP_LOCATIONS(B)  for (B = bp_location_chain; B; B = B->global_next)

#define ALL_BP_LOCATIONS_SAFE(B,TMP)	\
	for (B = bp_location_chain;	\
	     B ? (TMP=B->global_next, 1): 0;	\
	     B = TMP)

/* Iterator for tracepoints only.  */

#define ALL_TRACEPOINTS(B)  \
  for (B = breakpoint_chain; B; B = B->next)  \
    if ((B)->type == bp_tracepoint)

/* Chains of all breakpoints defined.  */

struct breakpoint *breakpoint_chain;

struct bp_location *bp_location_chain;

/* The locations that no longer correspond to any breakpoint,
   unlinked from bp_location_chain, but for which a hit
   may still be reported by a target.  */
VEC(bp_location_p) *moribund_locations = NULL;

/* Number of last breakpoint made.  */

int breakpoint_count;

/* Number of last tracepoint made.  */

int tracepoint_count;

/* Return whether a breakpoint is an active enabled breakpoint.  */
static int
breakpoint_enabled (struct breakpoint *b)
{
  return (b->enable_state == bp_enabled);
}

/* Set breakpoint count to NUM.  */

void
set_breakpoint_count (int num)
{
  breakpoint_count = num;
  set_internalvar_integer (lookup_internalvar ("bpnum"), num);
}

/* Used in run_command to zero the hit count when a new run starts. */

void
clear_breakpoint_hit_counts (void)
{
  struct breakpoint *b;

  ALL_BREAKPOINTS (b)
    b->hit_count = 0;
}

/* Default address, symtab and line to put a breakpoint at
   for "break" command with no arg.
   if default_breakpoint_valid is zero, the other three are
   not valid, and "break" with no arg is an error.

   This set by print_stack_frame, which calls set_default_breakpoint.  */

int default_breakpoint_valid;
CORE_ADDR default_breakpoint_address;
struct symtab *default_breakpoint_symtab;
int default_breakpoint_line;

/* *PP is a string denoting a breakpoint.  Get the number of the breakpoint.
   Advance *PP after the string and any trailing whitespace.

   Currently the string can either be a number or "$" followed by the name
   of a convenience variable.  Making it an expression wouldn't work well
   for map_breakpoint_numbers (e.g. "4 + 5 + 6").

   If the string is a NULL pointer, that denotes the last breakpoint.
   
   TRAILER is a character which can be found after the number; most
   commonly this is `-'.  If you don't want a trailer, use \0.  */ 
static int
get_number_trailer (char **pp, int trailer)
{
  int retval = 0;	/* default */
  char *p = *pp;

  if (p == NULL)
    /* Empty line means refer to the last breakpoint.  */
    return breakpoint_count;
  else if (*p == '$')
    {
      /* Make a copy of the name, so we can null-terminate it
         to pass to lookup_internalvar().  */
      char *varname;
      char *start = ++p;
      LONGEST val;

      while (isalnum (*p) || *p == '_')
	p++;
      varname = (char *) alloca (p - start + 1);
      strncpy (varname, start, p - start);
      varname[p - start] = '\0';
      if (get_internalvar_integer (lookup_internalvar (varname), &val))
	retval = (int) val;
      else
	{
	  printf_filtered (_("Convenience variable must have integer value.\n"));
	  retval = 0;
	}
    }
  else
    {
      if (*p == '-')
	++p;
      while (*p >= '0' && *p <= '9')
	++p;
      if (p == *pp)
	/* There is no number here.  (e.g. "cond a == b").  */
	{
	  /* Skip non-numeric token */
	  while (*p && !isspace((int) *p))
	    ++p;
	  /* Return zero, which caller must interpret as error. */
	  retval = 0;
	}
      else
	retval = atoi (*pp);
    }
  if (!(isspace (*p) || *p == '\0' || *p == trailer))
    {
      /* Trailing junk: return 0 and let caller print error msg. */
      while (!(isspace (*p) || *p == '\0' || *p == trailer))
	++p;
      retval = 0;
    }
  while (isspace (*p))
    p++;
  *pp = p;
  return retval;
}


/* Like get_number_trailer, but don't allow a trailer.  */
int
get_number (char **pp)
{
  return get_number_trailer (pp, '\0');
}

/* Parse a number or a range.
 * A number will be of the form handled by get_number.
 * A range will be of the form <number1> - <number2>, and 
 * will represent all the integers between number1 and number2,
 * inclusive.
 *
 * While processing a range, this fuction is called iteratively;
 * At each call it will return the next value in the range.
 *
 * At the beginning of parsing a range, the char pointer PP will
 * be advanced past <number1> and left pointing at the '-' token.
 * Subsequent calls will not advance the pointer until the range
 * is completed.  The call that completes the range will advance
 * pointer PP past <number2>.
 */

int 
get_number_or_range (char **pp)
{
  static int last_retval, end_value;
  static char *end_ptr;
  static int in_range = 0;

  if (**pp != '-')
    {
      /* Default case: pp is pointing either to a solo number, 
	 or to the first number of a range.  */
      last_retval = get_number_trailer (pp, '-');
      if (**pp == '-')
	{
	  char **temp;

	  /* This is the start of a range (<number1> - <number2>).
	     Skip the '-', parse and remember the second number,
	     and also remember the end of the final token.  */

	  temp = &end_ptr; 
	  end_ptr = *pp + 1; 
	  while (isspace ((int) *end_ptr))
	    end_ptr++;	/* skip white space */
	  end_value = get_number (temp);
	  if (end_value < last_retval) 
	    {
	      error (_("inverted range"));
	    }
	  else if (end_value == last_retval)
	    {
	      /* degenerate range (number1 == number2).  Advance the
		 token pointer so that the range will be treated as a
		 single number.  */ 
	      *pp = end_ptr;
	    }
	  else
	    in_range = 1;
	}
    }
  else if (! in_range)
    error (_("negative value"));
  else
    {
      /* pp points to the '-' that betokens a range.  All
	 number-parsing has already been done.  Return the next
	 integer value (one greater than the saved previous value).
	 Do not advance the token pointer 'pp' until the end of range
	 is reached.  */

      if (++last_retval == end_value)
	{
	  /* End of range reached; advance token pointer.  */
	  *pp = end_ptr;
	  in_range = 0;
	}
    }
  return last_retval;
}

/* Return the breakpoint with the specified number, or NULL
   if the number does not refer to an existing breakpoint.  */

struct breakpoint *
get_breakpoint (int num)
{
  struct breakpoint *b;

  ALL_BREAKPOINTS (b)
    if (b->number == num)
      return b;
  
  return NULL;
}


/* condition N EXP -- set break condition of breakpoint N to EXP.  */

static void
condition_command (char *arg, int from_tty)
{
  struct breakpoint *b;
  char *p;
  int bnum;

  if (arg == 0)
    error_no_arg (_("breakpoint number"));

  p = arg;
  bnum = get_number (&p);
  if (bnum == 0)
    error (_("Bad breakpoint argument: '%s'"), arg);

  ALL_BREAKPOINTS (b)
    if (b->number == bnum)
      {
	struct bp_location *loc = b->loc;
	for (; loc; loc = loc->next)
	  {
	    if (loc->cond)
	      {
		xfree (loc->cond);
		loc->cond = 0;
	      }
	  }
	if (b->cond_string != NULL)
	  xfree (b->cond_string);

	if (*p == 0)
	  {
	    b->cond_string = NULL;
	    if (from_tty)
	      printf_filtered (_("Breakpoint %d now unconditional.\n"), bnum);
	  }
	else
	  {
	    arg = p;
	    /* I don't know if it matters whether this is the string the user
	       typed in or the decompiled expression.  */
	    b->cond_string = xstrdup (arg);
	    b->condition_not_parsed = 0;
	    for (loc = b->loc; loc; loc = loc->next)
	      {
		arg = p;
		loc->cond =
		  parse_exp_1 (&arg, block_for_pc (loc->address), 0);
		if (*arg)
		  error (_("Junk at end of expression"));
	      }
	  }
	breakpoints_changed ();
	observer_notify_breakpoint_modified (b->number);
	return;
      }

  error (_("No breakpoint number %d."), bnum);
}

/* Set the command list of B to COMMANDS.  */

void
breakpoint_set_commands (struct breakpoint *b, struct command_line *commands)
{
  free_command_lines (&b->commands);
  b->commands = commands;
  breakpoints_changed ();
  observer_notify_breakpoint_modified (b->number);
}

static void
commands_command (char *arg, int from_tty)
{
  struct breakpoint *b;
  char *p;
  int bnum;
  struct command_line *l;

  /* If we allowed this, we would have problems with when to
     free the storage, if we change the commands currently
     being read from.  */

  if (executing_breakpoint_commands)
    error (_("Can't use the \"commands\" command among a breakpoint's commands."));

  p = arg;
  bnum = get_number (&p);

  if (p && *p)
    error (_("Unexpected extra arguments following breakpoint number."));

  ALL_BREAKPOINTS (b)
    if (b->number == bnum)
      {
	char *tmpbuf = xstrprintf ("Type commands for when breakpoint %d is hit, one per line.", 
				 bnum);
	struct cleanup *cleanups = make_cleanup (xfree, tmpbuf);
	l = read_command_lines (tmpbuf, from_tty, 1);
	do_cleanups (cleanups);
	breakpoint_set_commands (b, l);
	return;
    }
  error (_("No breakpoint number %d."), bnum);
}

/* Like commands_command, but instead of reading the commands from
   input stream, takes them from an already parsed command structure.

   This is used by cli-script.c to DTRT with breakpoint commands
   that are part of if and while bodies.  */
enum command_control_type
commands_from_control_command (char *arg, struct command_line *cmd)
{
  struct breakpoint *b;
  char *p;
  int bnum;

  /* If we allowed this, we would have problems with when to
     free the storage, if we change the commands currently
     being read from.  */

  if (executing_breakpoint_commands)
    error (_("Can't use the \"commands\" command among a breakpoint's commands."));

  /* An empty string for the breakpoint number means the last
     breakpoint, but get_number expects a NULL pointer.  */
  if (arg && !*arg)
    p = NULL;
  else
    p = arg;
  bnum = get_number (&p);

  if (p && *p)
    error (_("Unexpected extra arguments following breakpoint number."));

  ALL_BREAKPOINTS (b)
    if (b->number == bnum)
      {
	free_command_lines (&b->commands);
	if (cmd->body_count != 1)
	  error (_("Invalid \"commands\" block structure."));
	/* We need to copy the commands because if/while will free the
	   list after it finishes execution.  */
	b->commands = copy_command_lines (cmd->body_list[0]);
	breakpoints_changed ();
	observer_notify_breakpoint_modified (b->number);
	return simple_control;
      }
  error (_("No breakpoint number %d."), bnum);
}

/* Update BUF, which is LEN bytes read from the target address MEMADDR,
   by replacing any memory breakpoints with their shadowed contents.  */

void
breakpoint_restore_shadows (gdb_byte *buf, ULONGEST memaddr, LONGEST len)
{
  struct bp_location *b;
  CORE_ADDR bp_addr = 0;
  int bp_size = 0;
  int bptoffset = 0;

  ALL_BP_LOCATIONS (b)
  {
    if (b->owner->type == bp_none)
      warning (_("reading through apparently deleted breakpoint #%d?"),
              b->owner->number);

    if (b->loc_type != bp_loc_software_breakpoint)
      continue;
    if (!b->inserted)
      continue;
    /* Addresses and length of the part of the breakpoint that
       we need to copy.  */
    bp_addr = b->target_info.placed_address;
    bp_size = b->target_info.shadow_len;
    if (bp_size == 0)
      /* bp isn't valid, or doesn't shadow memory.  */
      continue;

    if (bp_addr + bp_size <= memaddr)
      /* The breakpoint is entirely before the chunk of memory we
         are reading.  */
      continue;

    if (bp_addr >= memaddr + len)
      /* The breakpoint is entirely after the chunk of memory we are
         reading. */
      continue;

    /* Offset within shadow_contents.  */
    if (bp_addr < memaddr)
      {
	/* Only copy the second part of the breakpoint.  */
	bp_size -= memaddr - bp_addr;
	bptoffset = memaddr - bp_addr;
	bp_addr = memaddr;
      }

    if (bp_addr + bp_size > memaddr + len)
      {
	/* Only copy the first part of the breakpoint.  */
	bp_size -= (bp_addr + bp_size) - (memaddr + len);
      }

    memcpy (buf + bp_addr - memaddr,
	    b->target_info.shadow_contents + bptoffset, bp_size);
  }
}


/* A wrapper function for inserting catchpoints.  */
static void
insert_catchpoint (struct ui_out *uo, void *args)
{
  struct breakpoint *b = (struct breakpoint *) args;
  int val = -1;

  gdb_assert (b->type == bp_catchpoint);
  gdb_assert (b->ops != NULL && b->ops->insert != NULL);

  b->ops->insert (b);
}

static int
is_hardware_watchpoint (struct breakpoint *bpt)
{
  return (bpt->type == bp_hardware_watchpoint
	  || bpt->type == bp_read_watchpoint
	  || bpt->type == bp_access_watchpoint);
}

/* Find the current value of a watchpoint on EXP.  Return the value in
   *VALP and *RESULTP and the chain of intermediate and final values
   in *VAL_CHAIN.  RESULTP and VAL_CHAIN may be NULL if the caller does
   not need them.

   If a memory error occurs while evaluating the expression, *RESULTP will
   be set to NULL.  *RESULTP may be a lazy value, if the result could
   not be read from memory.  It is used to determine whether a value
   is user-specified (we should watch the whole value) or intermediate
   (we should watch only the bit used to locate the final value).

   If the final value, or any intermediate value, could not be read
   from memory, *VALP will be set to NULL.  *VAL_CHAIN will still be
   set to any referenced values.  *VALP will never be a lazy value.
   This is the value which we store in struct breakpoint.

   If VAL_CHAIN is non-NULL, *VAL_CHAIN will be released from the
   value chain.  The caller must free the values individually.  If
   VAL_CHAIN is NULL, all generated values will be left on the value
   chain.  */

static void
fetch_watchpoint_value (struct expression *exp, struct value **valp,
			struct value **resultp, struct value **val_chain)
{
  struct value *mark, *new_mark, *result;
  volatile struct gdb_exception ex;

  *valp = NULL;
  if (resultp)
    *resultp = NULL;
  if (val_chain)
    *val_chain = NULL;

  /* Evaluate the expression.  */
  mark = value_mark ();
  result = NULL;

  TRY_CATCH (ex, RETURN_MASK_ALL)
    {
      result = evaluate_expression (exp);
    }
  if (ex.reason < 0)
    {
      /* Ignore memory errors, we want watchpoints pointing at
	 inaccessible memory to still be created; otherwise, throw the
	 error to some higher catcher.  */
      switch (ex.error)
	{
	case MEMORY_ERROR:
	  break;
	default:
	  throw_exception (ex);
	  break;
	}
    }

  new_mark = value_mark ();
  if (mark == new_mark)
    return;
  if (resultp)
    *resultp = result;

  /* Make sure it's not lazy, so that after the target stops again we
     have a non-lazy previous value to compare with.  */
  if (result != NULL
      && (!value_lazy (result) || gdb_value_fetch_lazy (result)))
    *valp = result;

  if (val_chain)
    {
      /* Return the chain of intermediate values.  We use this to
	 decide which addresses to watch.  */
      *val_chain = new_mark;
      value_release_to_mark (mark);
    }
}

/* Assuming that B is a watchpoint:
   - Reparse watchpoint expression, if REPARSE is non-zero
   - Evaluate expression and store the result in B->val
   - Evaluate the condition if there is one, and store the result
     in b->loc->cond.
   - Update the list of values that must be watched in B->loc.

   If the watchpoint disposition is disp_del_at_next_stop, then do nothing.
   If this is local watchpoint that is out of scope, delete it.  */
static void
update_watchpoint (struct breakpoint *b, int reparse)
{
  int within_current_scope;
  struct frame_id saved_frame_id;
  struct bp_location *loc;
  bpstat bs;

  /* We don't free locations.  They are stored in bp_location_chain and
     update_global_locations will eventually delete them and remove
     breakpoints if needed.  */
  b->loc = NULL;

  if (b->disposition == disp_del_at_next_stop)
    return;
 
  /* Save the current frame's ID so we can restore it after
     evaluating the watchpoint expression on its own frame.  */
  /* FIXME drow/2003-09-09: It would be nice if evaluate_expression
     took a frame parameter, so that we didn't have to change the
     selected frame.  */
  saved_frame_id = get_frame_id (get_selected_frame (NULL));

  /* Determine if the watchpoint is within scope.  */
  if (b->exp_valid_block == NULL)
    within_current_scope = 1;
  else
    {
      struct frame_info *fi;
      fi = frame_find_by_id (b->watchpoint_frame);
      within_current_scope = (fi != NULL);
      if (within_current_scope)
	select_frame (fi);
    }

  if (within_current_scope && reparse)
    {
      char *s;
      if (b->exp)
	{
	  xfree (b->exp);
	  b->exp = NULL;
	}
      s = b->exp_string;
      b->exp = parse_exp_1 (&s, b->exp_valid_block, 0);
      /* If the meaning of expression itself changed, the old value is
	 no longer relevant.  We don't want to report a watchpoint hit
	 to the user when the old value and the new value may actually
	 be completely different objects.  */
      value_free (b->val);
      b->val = NULL;
      b->val_valid = 0;
    }

  /* If we failed to parse the expression, for example because
     it refers to a global variable in a not-yet-loaded shared library,
     don't try to insert watchpoint.  We don't automatically delete
     such watchpoint, though, since failure to parse expression
     is different from out-of-scope watchpoint.  */
  if (within_current_scope && b->exp)
    {
      struct value *val_chain, *v, *result, *next;

      fetch_watchpoint_value (b->exp, &v, &result, &val_chain);

      /* Avoid setting b->val if it's already set.  The meaning of
	 b->val is 'the last value' user saw, and we should update
	 it only if we reported that last value to user.  As it
	 happens, the code that reports it updates b->val directly.  */
      if (!b->val_valid)
	{
	  b->val = v;
	  b->val_valid = 1;
	}

	/* Change the type of breakpoint between hardware assisted or an
	   ordinary watchpoint depending on the hardware support and free
	   hardware slots.  REPARSE is set when the inferior is started.  */
	if ((b->type == bp_watchpoint || b->type == bp_hardware_watchpoint)
	    && reparse)
	  {
	    int i, mem_cnt, other_type_used;

	    i = hw_watchpoint_used_count (bp_hardware_watchpoint,
					  &other_type_used);
	    mem_cnt = can_use_hardware_watchpoint (val_chain);

	    if (!mem_cnt)
	      b->type = bp_watchpoint;
	    else
	      {
		int target_resources_ok = target_can_use_hardware_watchpoint
		  (bp_hardware_watchpoint, i + mem_cnt, other_type_used);
		if (target_resources_ok <= 0)
		  b->type = bp_watchpoint;
		else
		  b->type = bp_hardware_watchpoint;
	      }
	  }

      /* Look at each value on the value chain.  */
      for (v = val_chain; v; v = next)
	{
	  /* If it's a memory location, and GDB actually needed
	     its contents to evaluate the expression, then we
	     must watch it.  If the first value returned is
	     still lazy, that means an error occurred reading it;
	     watch it anyway in case it becomes readable.  */
	  if (VALUE_LVAL (v) == lval_memory
	      && (v == val_chain || ! value_lazy (v)))
	    {
	      struct type *vtype = check_typedef (value_type (v));

	      /* We only watch structs and arrays if user asked
		 for it explicitly, never if they just happen to
		 appear in the middle of some value chain.  */
	      if (v == result
		  || (TYPE_CODE (vtype) != TYPE_CODE_STRUCT
		      && TYPE_CODE (vtype) != TYPE_CODE_ARRAY))
		{
		  CORE_ADDR addr;
		  int len, type;
		  struct bp_location *loc, **tmp;

		  addr = value_address (v);
		  len = TYPE_LENGTH (value_type (v));
		  type = hw_write;
		  if (b->type == bp_read_watchpoint)
		    type = hw_read;
		  else if (b->type == bp_access_watchpoint)
		    type = hw_access;
		  
		  loc = allocate_bp_location (b);
		  for (tmp = &(b->loc); *tmp != NULL; tmp = &((*tmp)->next))
		    ;
		  *tmp = loc;
		  loc->gdbarch = get_type_arch (value_type (v));
		  loc->address = addr;
		  loc->length = len;
		  loc->watchpoint_type = type;
		}
	    }

	  next = value_next (v);
	  if (v != b->val)
	    value_free (v);
	}

      /* We just regenerated the list of breakpoint locations.
         The new location does not have its condition field set to anything
         and therefore, we must always reparse the cond_string, independently
         of the value of the reparse flag.  */
      if (b->cond_string != NULL)
	{
	  char *s = b->cond_string;
	  b->loc->cond = parse_exp_1 (&s, b->exp_valid_block, 0);
	}
    }
  else if (!within_current_scope)
    {
      printf_filtered (_("\
Watchpoint %d deleted because the program has left the block \n\
in which its expression is valid.\n"),
		       b->number);
      if (b->related_breakpoint)
	b->related_breakpoint->disposition = disp_del_at_next_stop;
      b->disposition = disp_del_at_next_stop;
    }

  /* Restore the selected frame.  */
  select_frame (frame_find_by_id (saved_frame_id));
}


/* Returns 1 iff breakpoint location should be
   inserted in the inferior.  */
static int
should_be_inserted (struct bp_location *bpt)
{
  if (!breakpoint_enabled (bpt->owner))
    return 0;

  if (bpt->owner->disposition == disp_del_at_next_stop)
    return 0;

  if (!bpt->enabled || bpt->shlib_disabled || bpt->duplicate)
    return 0;

  /* Tracepoints are inserted by the target at a time of its choosing,
     not by us.  */
  if (bpt->owner->type == bp_tracepoint)
    return 0;

  return 1;
}

/* Insert a low-level "breakpoint" of some type.  BPT is the breakpoint.
   Any error messages are printed to TMP_ERROR_STREAM; and DISABLED_BREAKS,
   and HW_BREAKPOINT_ERROR are used to report problems.

   NOTE drow/2003-09-09: This routine could be broken down to an object-style
   method for each breakpoint or catchpoint type.  */
static int
insert_bp_location (struct bp_location *bpt,
		    struct ui_file *tmp_error_stream,
		    int *disabled_breaks,
		    int *hw_breakpoint_error)
{
  int val = 0;

  if (!should_be_inserted (bpt) || bpt->inserted)
    return 0;

  /* Initialize the target-specific information.  */
  memset (&bpt->target_info, 0, sizeof (bpt->target_info));
  bpt->target_info.placed_address = bpt->address;

  if (bpt->loc_type == bp_loc_software_breakpoint
      || bpt->loc_type == bp_loc_hardware_breakpoint)
    {
      if (bpt->owner->type != bp_hardware_breakpoint)
	{
	  /* If the explicitly specified breakpoint type
	     is not hardware breakpoint, check the memory map to see
	     if the breakpoint address is in read only memory or not.
	     Two important cases are:
	     - location type is not hardware breakpoint, memory
	     is readonly.  We change the type of the location to
	     hardware breakpoint.
	     - location type is hardware breakpoint, memory is read-write.
	     This means we've previously made the location hardware one, but
	     then the memory map changed, so we undo.
	     
	     When breakpoints are removed, remove_breakpoints will
	     use location types we've just set here, the only possible
	     problem is that memory map has changed during running program,
	     but it's not going to work anyway with current gdb.  */
	  struct mem_region *mr 
	    = lookup_mem_region (bpt->target_info.placed_address);
	  
	  if (mr)
	    {
	      if (automatic_hardware_breakpoints)
		{
		  int changed = 0;
		  enum bp_loc_type new_type;
		  
		  if (mr->attrib.mode != MEM_RW)
		    new_type = bp_loc_hardware_breakpoint;
		  else 
		    new_type = bp_loc_software_breakpoint;
		  
		  if (new_type != bpt->loc_type)
		    {
		      static int said = 0;
		      bpt->loc_type = new_type;
		      if (!said)
			{
			  fprintf_filtered (gdb_stdout, _("\
Note: automatically using hardware breakpoints for read-only addresses.\n"));
			  said = 1;
			}
		    }
		}
	      else if (bpt->loc_type == bp_loc_software_breakpoint
		       && mr->attrib.mode != MEM_RW)	    
		warning (_("cannot set software breakpoint at readonly address %s"),
			 paddress (bpt->gdbarch, bpt->address));
	    }
	}
        
      /* First check to see if we have to handle an overlay.  */
      if (overlay_debugging == ovly_off
	  || bpt->section == NULL
	  || !(section_is_overlay (bpt->section)))
	{
	  /* No overlay handling: just set the breakpoint.  */

	  if (bpt->loc_type == bp_loc_hardware_breakpoint)
	    val = target_insert_hw_breakpoint (bpt->gdbarch,
					       &bpt->target_info);
	  else
	    val = target_insert_breakpoint (bpt->gdbarch,
					    &bpt->target_info);
	}
      else
	{
	  /* This breakpoint is in an overlay section.  
	     Shall we set a breakpoint at the LMA?  */
	  if (!overlay_events_enabled)
	    {
	      /* Yes -- overlay event support is not active, 
		 so we must try to set a breakpoint at the LMA.
		 This will not work for a hardware breakpoint.  */
	      if (bpt->loc_type == bp_loc_hardware_breakpoint)
		warning (_("hardware breakpoint %d not supported in overlay!"),
			 bpt->owner->number);
	      else
		{
		  CORE_ADDR addr = overlay_unmapped_address (bpt->address,
							     bpt->section);
		  /* Set a software (trap) breakpoint at the LMA.  */
		  bpt->overlay_target_info = bpt->target_info;
		  bpt->overlay_target_info.placed_address = addr;
		  val = target_insert_breakpoint (bpt->gdbarch,
						  &bpt->overlay_target_info);
		  if (val != 0)
		    fprintf_unfiltered (tmp_error_stream,
					"Overlay breakpoint %d failed: in ROM?\n",
					bpt->owner->number);
		}
	    }
	  /* Shall we set a breakpoint at the VMA? */
	  if (section_is_mapped (bpt->section))
	    {
	      /* Yes.  This overlay section is mapped into memory.  */
	      if (bpt->loc_type == bp_loc_hardware_breakpoint)
		val = target_insert_hw_breakpoint (bpt->gdbarch,
						   &bpt->target_info);
	      else
		val = target_insert_breakpoint (bpt->gdbarch,
						&bpt->target_info);
	    }
	  else
	    {
	      /* No.  This breakpoint will not be inserted.  
		 No error, but do not mark the bp as 'inserted'.  */
	      return 0;
	    }
	}

      if (val)
	{
	  /* Can't set the breakpoint.  */
	  if (solib_name_from_address (bpt->address))
	    {
	      /* See also: disable_breakpoints_in_shlibs. */
	      val = 0;
	      bpt->shlib_disabled = 1;
	      if (!*disabled_breaks)
		{
		  fprintf_unfiltered (tmp_error_stream, 
				      "Cannot insert breakpoint %d.\n", 
				      bpt->owner->number);
		  fprintf_unfiltered (tmp_error_stream, 
				      "Temporarily disabling shared library breakpoints:\n");
		}
	      *disabled_breaks = 1;
	      fprintf_unfiltered (tmp_error_stream,
				  "breakpoint #%d\n", bpt->owner->number);
	    }
	  else
	    {
	      if (bpt->loc_type == bp_loc_hardware_breakpoint)
		{
		  *hw_breakpoint_error = 1;
		  fprintf_unfiltered (tmp_error_stream, 
				      "Cannot insert hardware breakpoint %d.\n",
				      bpt->owner->number);
		}
	      else
		{
		  fprintf_unfiltered (tmp_error_stream, 
				      "Cannot insert breakpoint %d.\n", 
				      bpt->owner->number);
		  fprintf_filtered (tmp_error_stream, 
				    "Error accessing memory address ");
		  fputs_filtered (paddress (bpt->gdbarch, bpt->address),
				  tmp_error_stream);
		  fprintf_filtered (tmp_error_stream, ": %s.\n",
				    safe_strerror (val));
		}

	    }
	}
      else
	bpt->inserted = 1;

      return val;
    }

  else if (bpt->loc_type == bp_loc_hardware_watchpoint
	   /* NOTE drow/2003-09-08: This state only exists for removing
	      watchpoints.  It's not clear that it's necessary... */
	   && bpt->owner->disposition != disp_del_at_next_stop)
    {
      val = target_insert_watchpoint (bpt->address, 
				      bpt->length,
				      bpt->watchpoint_type);
      bpt->inserted = (val != -1);
    }

  else if (bpt->owner->type == bp_catchpoint)
    {
      struct gdb_exception e = catch_exception (uiout, insert_catchpoint,
						bpt->owner, RETURN_MASK_ERROR);
      exception_fprintf (gdb_stderr, e, "warning: inserting catchpoint %d: ",
			 bpt->owner->number);
      if (e.reason < 0)
	bpt->owner->enable_state = bp_disabled;
      else
	bpt->inserted = 1;

      /* We've already printed an error message if there was a problem
	 inserting this catchpoint, and we've disabled the catchpoint,
	 so just return success.  */
      return 0;
    }

  return 0;
}

/* Make sure all breakpoints are inserted in inferior.
   Throws exception on any error.
   A breakpoint that is already inserted won't be inserted
   again, so calling this function twice is safe.  */
void
insert_breakpoints (void)
{
  struct breakpoint *bpt;

  ALL_BREAKPOINTS (bpt)
    if (is_hardware_watchpoint (bpt))
      update_watchpoint (bpt, 0 /* don't reparse. */);

  update_global_location_list (1);

  /* update_global_location_list does not insert breakpoints when
     always_inserted_mode is not enabled.  Explicitly insert them
     now.  */
  if (!breakpoints_always_inserted_mode ())
    insert_breakpoint_locations ();
}

/* insert_breakpoints is used when starting or continuing the program.
   remove_breakpoints is used when the program stops.
   Both return zero if successful,
   or an `errno' value if could not write the inferior.  */

static void
insert_breakpoint_locations (void)
{
  struct breakpoint *bpt;
  struct bp_location *b, *temp;
  int error = 0;
  int val = 0;
  int disabled_breaks = 0;
  int hw_breakpoint_error = 0;

  struct ui_file *tmp_error_stream = mem_fileopen ();
  struct cleanup *cleanups = make_cleanup_ui_file_delete (tmp_error_stream);
  
  /* Explicitly mark the warning -- this will only be printed if
     there was an error.  */
  fprintf_unfiltered (tmp_error_stream, "Warning:\n");
	
  ALL_BP_LOCATIONS_SAFE (b, temp)
    {
      if (!should_be_inserted (b) || b->inserted)
	continue;

      /* There is no point inserting thread-specific breakpoints if the
	 thread no longer exists.  */
      if (b->owner->thread != -1
	  && !valid_thread_id (b->owner->thread))
	continue;

      val = insert_bp_location (b, tmp_error_stream,
				    &disabled_breaks,
				    &hw_breakpoint_error);
      if (val)
	error = val;
    }

  /* If we failed to insert all locations of a watchpoint,
     remove them, as half-inserted watchpoint is of limited use.  */
  ALL_BREAKPOINTS (bpt)  
    {
      int some_failed = 0;
      struct bp_location *loc;

      if (!is_hardware_watchpoint (bpt))
	continue;

      if (!breakpoint_enabled (bpt))
	continue;

      if (bpt->disposition == disp_del_at_next_stop)
	continue;
      
      for (loc = bpt->loc; loc; loc = loc->next)
	if (!loc->inserted)
	  {
	    some_failed = 1;
	    break;
	  }
      if (some_failed)
	{
	  for (loc = bpt->loc; loc; loc = loc->next)
	    if (loc->inserted)
	      remove_breakpoint (loc, mark_uninserted);

	  hw_breakpoint_error = 1;
	  fprintf_unfiltered (tmp_error_stream,
			      "Could not insert hardware watchpoint %d.\n", 
			      bpt->number);
	  error = -1;
	}
    }

  if (error)
    {
      /* If a hardware breakpoint or watchpoint was inserted, add a
         message about possibly exhausted resources.  */
      if (hw_breakpoint_error)
	{
	  fprintf_unfiltered (tmp_error_stream, 
			      "Could not insert hardware breakpoints:\n\
You may have requested too many hardware breakpoints/watchpoints.\n");
	}
      target_terminal_ours_for_output ();
      error_stream (tmp_error_stream);
    }

  do_cleanups (cleanups);
}

int
remove_breakpoints (void)
{
  struct bp_location *b;
  int val = 0;

  ALL_BP_LOCATIONS (b)
  {
    if (b->inserted)
      val |= remove_breakpoint (b, mark_uninserted);
  }
  return val;
}

int
remove_hw_watchpoints (void)
{
  struct bp_location *b;
  int val = 0;

  ALL_BP_LOCATIONS (b)
  {
    if (b->inserted && b->loc_type == bp_loc_hardware_watchpoint)
      val |= remove_breakpoint (b, mark_uninserted);
  }
  return val;
}

int
reattach_breakpoints (int pid)
{
  struct bp_location *b;
  int val;
  struct cleanup *old_chain = save_inferior_ptid ();
  struct ui_file *tmp_error_stream = mem_fileopen ();
  int dummy1 = 0, dummy2 = 0;

  make_cleanup_ui_file_delete (tmp_error_stream);

  inferior_ptid = pid_to_ptid (pid);
  ALL_BP_LOCATIONS (b)
  {
    if (b->inserted)
      {
	b->inserted = 0;
	val = insert_bp_location (b, tmp_error_stream,
				  &dummy1, &dummy2);
	if (val != 0)
	  {
	    do_cleanups (old_chain);
	    return val;
	  }
      }
  }
  do_cleanups (old_chain);
  return 0;
}

static int internal_breakpoint_number = -1;

static struct breakpoint *
create_internal_breakpoint (struct gdbarch *gdbarch,
			    CORE_ADDR address, enum bptype type)
{
  struct symtab_and_line sal;
  struct breakpoint *b;

  init_sal (&sal);		/* initialize to zeroes */

  sal.pc = address;
  sal.section = find_pc_overlay (sal.pc);

  b = set_raw_breakpoint (gdbarch, sal, type);
  b->number = internal_breakpoint_number--;
  b->disposition = disp_donttouch;

  return b;
}

static void
create_overlay_event_breakpoint (char *func_name)
{
  struct objfile *objfile;

  ALL_OBJFILES (objfile)
    {
      struct breakpoint *b;
      struct minimal_symbol *m;

      m = lookup_minimal_symbol_text (func_name, objfile);
      if (m == NULL)
        continue;

      b = create_internal_breakpoint (get_objfile_arch (objfile),
				      SYMBOL_VALUE_ADDRESS (m),
                                      bp_overlay_event);
      b->addr_string = xstrdup (func_name);

      if (overlay_debugging == ovly_auto)
        {
          b->enable_state = bp_enabled;
          overlay_events_enabled = 1;
        }
      else
       {
         b->enable_state = bp_disabled;
         overlay_events_enabled = 0;
       }
    }
  update_global_location_list (1);
}

static void
create_longjmp_master_breakpoint (char *func_name)
{
  struct objfile *objfile;

  ALL_OBJFILES (objfile)
    {
      struct breakpoint *b;
      struct minimal_symbol *m;

      if (!gdbarch_get_longjmp_target_p (get_objfile_arch (objfile)))
	continue;

      m = lookup_minimal_symbol_text (func_name, objfile);
      if (m == NULL)
        continue;

      b = create_internal_breakpoint (get_objfile_arch (objfile),
				      SYMBOL_VALUE_ADDRESS (m),
                                      bp_longjmp_master);
      b->addr_string = xstrdup (func_name);
      b->enable_state = bp_disabled;
    }
  update_global_location_list (1);
}

void
update_breakpoints_after_exec (void)
{
  struct breakpoint *b;
  struct breakpoint *temp;
  struct bp_location *bploc;

  /* We're about to delete breakpoints from GDB's lists.  If the
     INSERTED flag is true, GDB will try to lift the breakpoints by
     writing the breakpoints' "shadow contents" back into memory.  The
     "shadow contents" are NOT valid after an exec, so GDB should not
     do that.  Instead, the target is responsible from marking
     breakpoints out as soon as it detects an exec.  We don't do that
     here instead, because there may be other attempts to delete
     breakpoints after detecting an exec and before reaching here.  */
  ALL_BP_LOCATIONS (bploc)
    gdb_assert (!bploc->inserted);

  ALL_BREAKPOINTS_SAFE (b, temp)
  {
    /* Solib breakpoints must be explicitly reset after an exec(). */
    if (b->type == bp_shlib_event)
      {
	delete_breakpoint (b);
	continue;
      }

    /* JIT breakpoints must be explicitly reset after an exec(). */
    if (b->type == bp_jit_event)
      {
	delete_breakpoint (b);
	continue;
      }

    /* Thread event breakpoints must be set anew after an exec(),
       as must overlay event and longjmp master breakpoints.  */
    if (b->type == bp_thread_event || b->type == bp_overlay_event
	|| b->type == bp_longjmp_master)
      {
	delete_breakpoint (b);
	continue;
      }

    /* Step-resume breakpoints are meaningless after an exec(). */
    if (b->type == bp_step_resume)
      {
	delete_breakpoint (b);
	continue;
      }

    /* Longjmp and longjmp-resume breakpoints are also meaningless
       after an exec.  */
    if (b->type == bp_longjmp || b->type == bp_longjmp_resume)
      {
	delete_breakpoint (b);
	continue;
      }

    if (b->type == bp_catchpoint)
      {
        /* For now, none of the bp_catchpoint breakpoints need to
           do anything at this point.  In the future, if some of
           the catchpoints need to something, we will need to add
           a new method, and call this method from here.  */
        continue;
      }

    /* bp_finish is a special case.  The only way we ought to be able
       to see one of these when an exec() has happened, is if the user
       caught a vfork, and then said "finish".  Ordinarily a finish just
       carries them to the call-site of the current callee, by setting
       a temporary bp there and resuming.  But in this case, the finish
       will carry them entirely through the vfork & exec.

       We don't want to allow a bp_finish to remain inserted now.  But
       we can't safely delete it, 'cause finish_command has a handle to
       the bp on a bpstat, and will later want to delete it.  There's a
       chance (and I've seen it happen) that if we delete the bp_finish
       here, that its storage will get reused by the time finish_command
       gets 'round to deleting the "use to be a bp_finish" breakpoint.
       We really must allow finish_command to delete a bp_finish.

       In the absense of a general solution for the "how do we know
       it's safe to delete something others may have handles to?"
       problem, what we'll do here is just uninsert the bp_finish, and
       let finish_command delete it.

       (We know the bp_finish is "doomed" in the sense that it's
       momentary, and will be deleted as soon as finish_command sees
       the inferior stopped.  So it doesn't matter that the bp's
       address is probably bogus in the new a.out, unlike e.g., the
       solib breakpoints.)  */

    if (b->type == bp_finish)
      {
	continue;
      }

    /* Without a symbolic address, we have little hope of the
       pre-exec() address meaning the same thing in the post-exec()
       a.out. */
    if (b->addr_string == NULL)
      {
	delete_breakpoint (b);
	continue;
      }
  }
  /* FIXME what about longjmp breakpoints?  Re-create them here?  */
  create_overlay_event_breakpoint ("_ovly_debug_event");
  create_longjmp_master_breakpoint ("longjmp");
  create_longjmp_master_breakpoint ("_longjmp");
  create_longjmp_master_breakpoint ("siglongjmp");
  create_longjmp_master_breakpoint ("_siglongjmp");
}

int
detach_breakpoints (int pid)
{
  struct bp_location *b;
  int val = 0;
  struct cleanup *old_chain = save_inferior_ptid ();

  if (pid == PIDGET (inferior_ptid))
    error (_("Cannot detach breakpoints of inferior_ptid"));

  /* Set inferior_ptid; remove_breakpoint uses this global.  */
  inferior_ptid = pid_to_ptid (pid);
  ALL_BP_LOCATIONS (b)
  {
    if (b->inserted)
      val |= remove_breakpoint (b, mark_inserted);
  }
  do_cleanups (old_chain);
  return val;
}

static int
remove_breakpoint (struct bp_location *b, insertion_state_t is)
{
  int val;

  if (b->owner->enable_state == bp_permanent)
    /* Permanent breakpoints cannot be inserted or removed.  */
    return 0;

  /* The type of none suggests that owner is actually deleted.
     This should not ever happen.  */
  gdb_assert (b->owner->type != bp_none);

  if (b->loc_type == bp_loc_software_breakpoint
      || b->loc_type == bp_loc_hardware_breakpoint)
    {
      /* "Normal" instruction breakpoint: either the standard
	 trap-instruction bp (bp_breakpoint), or a
	 bp_hardware_breakpoint.  */

      /* First check to see if we have to handle an overlay.  */
      if (overlay_debugging == ovly_off
	  || b->section == NULL
	  || !(section_is_overlay (b->section)))
	{
	  /* No overlay handling: just remove the breakpoint.  */

	  if (b->loc_type == bp_loc_hardware_breakpoint)
	    val = target_remove_hw_breakpoint (b->gdbarch, &b->target_info);
	  else
	    val = target_remove_breakpoint (b->gdbarch, &b->target_info);
	}
      else
	{
	  /* This breakpoint is in an overlay section.  
	     Did we set a breakpoint at the LMA?  */
	  if (!overlay_events_enabled)
	      {
		/* Yes -- overlay event support is not active, so we
		   should have set a breakpoint at the LMA.  Remove it.  
		*/
		/* Ignore any failures: if the LMA is in ROM, we will
		   have already warned when we failed to insert it.  */
		if (b->loc_type == bp_loc_hardware_breakpoint)
		  target_remove_hw_breakpoint (b->gdbarch,
					       &b->overlay_target_info);
		else
		  target_remove_breakpoint (b->gdbarch,
					    &b->overlay_target_info);
	      }
	  /* Did we set a breakpoint at the VMA? 
	     If so, we will have marked the breakpoint 'inserted'.  */
	  if (b->inserted)
	    {
	      /* Yes -- remove it.  Previously we did not bother to
		 remove the breakpoint if the section had been
		 unmapped, but let's not rely on that being safe.  We
		 don't know what the overlay manager might do.  */
	      if (b->loc_type == bp_loc_hardware_breakpoint)
		val = target_remove_hw_breakpoint (b->gdbarch,
						   &b->target_info);

	      /* However, we should remove *software* breakpoints only
		 if the section is still mapped, or else we overwrite
		 wrong code with the saved shadow contents.  */
	      else if (section_is_mapped (b->section))
		val = target_remove_breakpoint (b->gdbarch,
						&b->target_info);
	      else
		val = 0;
	    }
	  else
	    {
	      /* No -- not inserted, so no need to remove.  No error.  */
	      val = 0;
	    }
	}

      /* In some cases, we might not be able to remove a breakpoint
	 in a shared library that has already been removed, but we
	 have not yet processed the shlib unload event.  */
      if (val && solib_name_from_address (b->address))
	val = 0;

      if (val)
	return val;
      b->inserted = (is == mark_inserted);
    }
  else if (b->loc_type == bp_loc_hardware_watchpoint)
    {
      struct value *v;
      struct value *n;

      b->inserted = (is == mark_inserted);
      val = target_remove_watchpoint (b->address, b->length, 
				      b->watchpoint_type);

      /* Failure to remove any of the hardware watchpoints comes here.  */
      if ((is == mark_uninserted) && (b->inserted))
	warning (_("Could not remove hardware watchpoint %d."),
		 b->owner->number);
    }
  else if (b->owner->type == bp_catchpoint
           && breakpoint_enabled (b->owner)
           && !b->duplicate)
    {
      gdb_assert (b->owner->ops != NULL && b->owner->ops->remove != NULL);

      val = b->owner->ops->remove (b->owner);
      if (val)
	return val;
      b->inserted = (is == mark_inserted);
    }

  return 0;
}

/* Clear the "inserted" flag in all breakpoints.  */

void
mark_breakpoints_out (void)
{
  struct bp_location *bpt;

  ALL_BP_LOCATIONS (bpt)
    bpt->inserted = 0;
}

/* Clear the "inserted" flag in all breakpoints and delete any
   breakpoints which should go away between runs of the program.

   Plus other such housekeeping that has to be done for breakpoints
   between runs.

   Note: this function gets called at the end of a run (by
   generic_mourn_inferior) and when a run begins (by
   init_wait_for_inferior). */



void
breakpoint_init_inferior (enum inf_context context)
{
  struct breakpoint *b, *temp;
  struct bp_location *bpt;
  int ix;

  /* If breakpoint locations are shared across processes, then there's
     nothing to do.  */
  if (gdbarch_has_global_breakpoints (target_gdbarch))
    return;

  ALL_BP_LOCATIONS (bpt)
    if (bpt->owner->enable_state != bp_permanent)
      bpt->inserted = 0;

  ALL_BREAKPOINTS_SAFE (b, temp)
  {
    switch (b->type)
      {
      case bp_call_dummy:
      case bp_watchpoint_scope:

	/* If the call dummy breakpoint is at the entry point it will
	   cause problems when the inferior is rerun, so we better
	   get rid of it. 

	   Also get rid of scope breakpoints.  */
	delete_breakpoint (b);
	break;

      case bp_watchpoint:
      case bp_hardware_watchpoint:
      case bp_read_watchpoint:
      case bp_access_watchpoint:

	/* Likewise for watchpoints on local expressions.  */
	if (b->exp_valid_block != NULL)
	  delete_breakpoint (b);
	else if (context == inf_starting) 
	  {
	    /* Reset val field to force reread of starting value
	       in insert_breakpoints.  */
	    if (b->val)
	      value_free (b->val);
	    b->val = NULL;
	    b->val_valid = 0;
	  }
	break;
      default:
	break;
      }
  }

  /* Get rid of the moribund locations.  */
  for (ix = 0; VEC_iterate (bp_location_p, moribund_locations, ix, bpt); ++ix)
    free_bp_location (bpt);
  VEC_free (bp_location_p, moribund_locations);
}

/* breakpoint_here_p (PC) returns non-zero if an enabled breakpoint
   exists at PC.  It returns ordinary_breakpoint_here if it's an
   ordinary breakpoint, or permanent_breakpoint_here if it's a
   permanent breakpoint.
   - When continuing from a location with an ordinary breakpoint, we
     actually single step once before calling insert_breakpoints.
   - When continuing from a localion with a permanent breakpoint, we
     need to use the `SKIP_PERMANENT_BREAKPOINT' macro, provided by
     the target, to advance the PC past the breakpoint.  */

enum breakpoint_here
breakpoint_here_p (CORE_ADDR pc)
{
  const struct bp_location *bpt;
  int any_breakpoint_here = 0;

  ALL_BP_LOCATIONS (bpt)
    {
      if (bpt->loc_type != bp_loc_software_breakpoint
	  && bpt->loc_type != bp_loc_hardware_breakpoint)
	continue;

      if ((breakpoint_enabled (bpt->owner)
	   || bpt->owner->enable_state == bp_permanent)
	  && bpt->address == pc)	/* bp is enabled and matches pc */
	{
	  if (overlay_debugging 
	      && section_is_overlay (bpt->section) 
	      && !section_is_mapped (bpt->section))
	    continue;		/* unmapped overlay -- can't be a match */
	  else if (bpt->owner->enable_state == bp_permanent)
	    return permanent_breakpoint_here;
	  else
	    any_breakpoint_here = 1;
	}
    }

  return any_breakpoint_here ? ordinary_breakpoint_here : 0;
}

/* Return true if there's a moribund breakpoint at PC.  */

int
moribund_breakpoint_here_p (CORE_ADDR pc)
{
  struct bp_location *loc;
  int ix;

  for (ix = 0; VEC_iterate (bp_location_p, moribund_locations, ix, loc); ++ix)
    if (loc->address == pc)
      return 1;

  return 0;
}

/* Returns non-zero if there's a breakpoint inserted at PC, which is
   inserted using regular breakpoint_chain/bp_location_chain mechanism.
   This does not check for single-step breakpoints, which are
   inserted and removed using direct target manipulation.  */

int
regular_breakpoint_inserted_here_p (CORE_ADDR pc)
{
  const struct bp_location *bpt;

  ALL_BP_LOCATIONS (bpt)
    {
      if (bpt->loc_type != bp_loc_software_breakpoint
	  && bpt->loc_type != bp_loc_hardware_breakpoint)
	continue;

      if (bpt->inserted
	  && bpt->address == pc)	/* bp is inserted and matches pc */
	{
	  if (overlay_debugging 
	      && section_is_overlay (bpt->section) 
	      && !section_is_mapped (bpt->section))
	    continue;		/* unmapped overlay -- can't be a match */
	  else
	    return 1;
	}
    }
  return 0;
}

/* Returns non-zero iff there's either regular breakpoint
   or a single step breakpoint inserted at PC.  */

int
breakpoint_inserted_here_p (CORE_ADDR pc)
{
  if (regular_breakpoint_inserted_here_p (pc))
    return 1;

  if (single_step_breakpoint_inserted_here_p (pc))
    return 1;

  return 0;
}

/* This function returns non-zero iff there is a software breakpoint
   inserted at PC.  */

int
software_breakpoint_inserted_here_p (CORE_ADDR pc)
{
  const struct bp_location *bpt;
  int any_breakpoint_here = 0;

  ALL_BP_LOCATIONS (bpt)
    {
      if (bpt->loc_type != bp_loc_software_breakpoint)
	continue;

      if (bpt->inserted
	  && bpt->address == pc)	/* bp is enabled and matches pc */
	{
	  if (overlay_debugging 
	      && section_is_overlay (bpt->section) 
	      && !section_is_mapped (bpt->section))
	    continue;		/* unmapped overlay -- can't be a match */
	  else
	    return 1;
	}
    }

  /* Also check for software single-step breakpoints.  */
  if (single_step_breakpoint_inserted_here_p (pc))
    return 1;

  return 0;
}

/* breakpoint_thread_match (PC, PTID) returns true if the breakpoint at
   PC is valid for process/thread PTID.  */

int
breakpoint_thread_match (CORE_ADDR pc, ptid_t ptid)
{
  const struct bp_location *bpt;
  /* The thread and task IDs associated to PTID, computed lazily.  */
  int thread = -1;
  int task = 0;
  
  ALL_BP_LOCATIONS (bpt)
    {
      if (bpt->loc_type != bp_loc_software_breakpoint
	  && bpt->loc_type != bp_loc_hardware_breakpoint)
	continue;

      if (!breakpoint_enabled (bpt->owner)
	  && bpt->owner->enable_state != bp_permanent)
	continue;

      if (bpt->address != pc)
	continue;

      if (bpt->owner->thread != -1)
	{
	  /* This is a thread-specific breakpoint.  Check that ptid
	     matches that thread.  If thread hasn't been computed yet,
	     it is now time to do so.  */
	  if (thread == -1)
	    thread = pid_to_thread_id (ptid);
	  if (bpt->owner->thread != thread)
	    continue;
	}

      if (bpt->owner->task != 0)
        {
	  /* This is a task-specific breakpoint.  Check that ptid
	     matches that task.  If task hasn't been computed yet,
	     it is now time to do so.  */
	  if (task == 0)
	    task = ada_get_task_number (ptid);
	  if (bpt->owner->task != task)
	    continue;
        }

      if (overlay_debugging 
	  && section_is_overlay (bpt->section) 
	  && !section_is_mapped (bpt->section))
	continue;	    /* unmapped overlay -- can't be a match */

      return 1;
    }

  return 0;
}


/* bpstat stuff.  External routines' interfaces are documented
   in breakpoint.h.  */

int
ep_is_catchpoint (struct breakpoint *ep)
{
  return (ep->type == bp_catchpoint);
}

void 
bpstat_free (bpstat bs)
{
  if (bs->old_val != NULL)
    value_free (bs->old_val);
  free_command_lines (&bs->commands);
  xfree (bs);
}

/* Clear a bpstat so that it says we are not at any breakpoint.
   Also free any storage that is part of a bpstat.  */

void
bpstat_clear (bpstat *bsp)
{
  bpstat p;
  bpstat q;

  if (bsp == 0)
    return;
  p = *bsp;
  while (p != NULL)
    {
      q = p->next;
      bpstat_free (p);
      p = q;
    }
  *bsp = NULL;
}

/* Return a copy of a bpstat.  Like "bs1 = bs2" but all storage that
   is part of the bpstat is copied as well.  */

bpstat
bpstat_copy (bpstat bs)
{
  bpstat p = NULL;
  bpstat tmp;
  bpstat retval = NULL;

  if (bs == NULL)
    return bs;

  for (; bs != NULL; bs = bs->next)
    {
      tmp = (bpstat) xmalloc (sizeof (*tmp));
      memcpy (tmp, bs, sizeof (*tmp));
      if (bs->commands != NULL)
	tmp->commands = copy_command_lines (bs->commands);
      if (bs->old_val != NULL)
	{
	  tmp->old_val = value_copy (bs->old_val);
	  release_value (tmp->old_val);
	}

      if (p == NULL)
	/* This is the first thing in the chain.  */
	retval = tmp;
      else
	p->next = tmp;
      p = tmp;
    }
  p->next = NULL;
  return retval;
}

/* Find the bpstat associated with this breakpoint */

bpstat
bpstat_find_breakpoint (bpstat bsp, struct breakpoint *breakpoint)
{
  if (bsp == NULL)
    return NULL;

  for (; bsp != NULL; bsp = bsp->next)
    {
      if (bsp->breakpoint_at && bsp->breakpoint_at->owner == breakpoint)
	return bsp;
    }
  return NULL;
}

/* Find a step_resume breakpoint associated with this bpstat.
   (If there are multiple step_resume bp's on the list, this function
   will arbitrarily pick one.)

   It is an error to use this function if BPSTAT doesn't contain a
   step_resume breakpoint.

   See wait_for_inferior's use of this function.  */
struct breakpoint *
bpstat_find_step_resume_breakpoint (bpstat bsp)
{
  int current_thread;

  gdb_assert (bsp != NULL);

  current_thread = pid_to_thread_id (inferior_ptid);

  for (; bsp != NULL; bsp = bsp->next)
    {
      if ((bsp->breakpoint_at != NULL)
	  && (bsp->breakpoint_at->owner->type == bp_step_resume)
	  && (bsp->breakpoint_at->owner->thread == current_thread
	      || bsp->breakpoint_at->owner->thread == -1))
	return bsp->breakpoint_at->owner;
    }

  internal_error (__FILE__, __LINE__, _("No step_resume breakpoint found."));
}


/* Put in *NUM the breakpoint number of the first breakpoint we are stopped
   at.  *BSP upon return is a bpstat which points to the remaining
   breakpoints stopped at (but which is not guaranteed to be good for
   anything but further calls to bpstat_num).
   Return 0 if passed a bpstat which does not indicate any breakpoints.
   Return -1 if stopped at a breakpoint that has been deleted since
   we set it.
   Return 1 otherwise.  */

int
bpstat_num (bpstat *bsp, int *num)
{
  struct breakpoint *b;

  if ((*bsp) == NULL)
    return 0;			/* No more breakpoint values */

  /* We assume we'll never have several bpstats that
     correspond to a single breakpoint -- otherwise, 
     this function might return the same number more
     than once and this will look ugly.  */
  b = (*bsp)->breakpoint_at ? (*bsp)->breakpoint_at->owner : NULL;
  *bsp = (*bsp)->next;
  if (b == NULL)
    return -1;			/* breakpoint that's been deleted since */

  *num = b->number;		/* We have its number */
  return 1;
}

/* Modify BS so that the actions will not be performed.  */

void
bpstat_clear_actions (bpstat bs)
{
  for (; bs != NULL; bs = bs->next)
    {
      free_command_lines (&bs->commands);
      if (bs->old_val != NULL)
	{
	  value_free (bs->old_val);
	  bs->old_val = NULL;
	}
    }
}

/* Called when a command is about to proceed the inferior.  */

static void
breakpoint_about_to_proceed (void)
{
  if (!ptid_equal (inferior_ptid, null_ptid))
    {
      struct thread_info *tp = inferior_thread ();

      /* Allow inferior function calls in breakpoint commands to not
	 interrupt the command list.  When the call finishes
	 successfully, the inferior will be standing at the same
	 breakpoint as if nothing happened.  */
      if (tp->in_infcall)
	return;
    }

  breakpoint_proceeded = 1;
}

/* Stub for cleaning up our state if we error-out of a breakpoint command */
static void
cleanup_executing_breakpoints (void *ignore)
{
  executing_breakpoint_commands = 0;
}

/* Execute all the commands associated with all the breakpoints at this
   location.  Any of these commands could cause the process to proceed
   beyond this point, etc.  We look out for such changes by checking
   the global "breakpoint_proceeded" after each command.

   Returns true if a breakpoint command resumed the inferior.  In that
   case, it is the caller's responsibility to recall it again with the
   bpstat of the current thread.  */

static int
bpstat_do_actions_1 (bpstat *bsp)
{
  bpstat bs;
  struct cleanup *old_chain;
  int again = 0;

  /* Avoid endless recursion if a `source' command is contained
     in bs->commands.  */
  if (executing_breakpoint_commands)
    return 0;

  executing_breakpoint_commands = 1;
  old_chain = make_cleanup (cleanup_executing_breakpoints, 0);

  /* This pointer will iterate over the list of bpstat's. */
  bs = *bsp;

  breakpoint_proceeded = 0;
  for (; bs != NULL; bs = bs->next)
    {
      struct command_line *cmd;
      struct cleanup *this_cmd_tree_chain;

      /* Take ownership of the BSP's command tree, if it has one.

         The command tree could legitimately contain commands like
         'step' and 'next', which call clear_proceed_status, which
         frees stop_bpstat's command tree.  To make sure this doesn't
         free the tree we're executing out from under us, we need to
         take ownership of the tree ourselves.  Since a given bpstat's
         commands are only executed once, we don't need to copy it; we
         can clear the pointer in the bpstat, and make sure we free
         the tree when we're done.  */
      cmd = bs->commands;
      bs->commands = 0;
      this_cmd_tree_chain = make_cleanup_free_command_lines (&cmd);

      while (cmd != NULL)
	{
	  execute_control_command (cmd);

	  if (breakpoint_proceeded)
	    break;
	  else
	    cmd = cmd->next;
	}

      /* We can free this command tree now.  */
      do_cleanups (this_cmd_tree_chain);

      if (breakpoint_proceeded)
	{
	  if (target_can_async_p ())
	    /* If we are in async mode, then the target might be still
	       running, not stopped at any breakpoint, so nothing for
	       us to do here -- just return to the event loop.  */
	    ;
	  else
	    /* In sync mode, when execute_control_command returns
	       we're already standing on the next breakpoint.
	       Breakpoint commands for that stop were not run, since
	       execute_command does not run breakpoint commands --
	       only command_line_handler does, but that one is not
	       involved in execution of breakpoint commands.  So, we
	       can now execute breakpoint commands.  It should be
	       noted that making execute_command do bpstat actions is
	       not an option -- in this case we'll have recursive
	       invocation of bpstat for each breakpoint with a
	       command, and can easily blow up GDB stack.  Instead, we
	       return true, which will trigger the caller to recall us
	       with the new stop_bpstat.  */
	    again = 1;
	  break;
	}
    }
  do_cleanups (old_chain);
  return again;
}

void
bpstat_do_actions (void)
{
  /* Do any commands attached to breakpoint we are stopped at.  */
  while (!ptid_equal (inferior_ptid, null_ptid)
	 && target_has_execution
	 && !is_exited (inferior_ptid)
	 && !is_executing (inferior_ptid))
    /* Since in sync mode, bpstat_do_actions may resume the inferior,
       and only return when it is stopped at the next breakpoint, we
       keep doing breakpoint actions until it returns false to
       indicate the inferior was not resumed.  */
    if (!bpstat_do_actions_1 (&inferior_thread ()->stop_bpstat))
      break;
}

/* Print out the (old or new) value associated with a watchpoint.  */

static void
watchpoint_value_print (struct value *val, struct ui_file *stream)
{
  if (val == NULL)
    fprintf_unfiltered (stream, _("<unreadable>"));
  else
    {
      struct value_print_options opts;
      get_user_print_options (&opts);
      value_print (val, stream, &opts);
    }
}

/* This is the normal print function for a bpstat.  In the future,
   much of this logic could (should?) be moved to bpstat_stop_status,
   by having it set different print_it values.

   Current scheme: When we stop, bpstat_print() is called.  It loops
   through the bpstat list of things causing this stop, calling the
   print_bp_stop_message function on each one. The behavior of the
   print_bp_stop_message function depends on the print_it field of
   bpstat. If such field so indicates, call this function here.

   Return values from this routine (ultimately used by bpstat_print()
   and normal_stop() to decide what to do): 
   PRINT_NOTHING: Means we already printed all we needed to print,
   don't print anything else.
   PRINT_SRC_ONLY: Means we printed something, and we do *not* desire
   that something to be followed by a location.
   PRINT_SCR_AND_LOC: Means we printed something, and we *do* desire
   that something to be followed by a location.
   PRINT_UNKNOWN: Means we printed nothing or we need to do some more
   analysis.  */

static enum print_stop_action
print_it_typical (bpstat bs)
{
  struct cleanup *old_chain;
  struct breakpoint *b;
  const struct bp_location *bl;
  struct ui_stream *stb;
  int bp_temp = 0;
  enum print_stop_action result;

  /* bs->breakpoint_at can be NULL if it was a momentary breakpoint
     which has since been deleted.  */
  if (bs->breakpoint_at == NULL)
    return PRINT_UNKNOWN;
  bl = bs->breakpoint_at;
  b = bl->owner;

  stb = ui_out_stream_new (uiout);
  old_chain = make_cleanup_ui_out_stream_delete (stb);

  switch (b->type)
    {
    case bp_breakpoint:
    case bp_hardware_breakpoint:
      bp_temp = bs->breakpoint_at->owner->disposition == disp_del;
      if (bl->address != bl->requested_address)
	breakpoint_adjustment_warning (bl->requested_address,
	                               bl->address,
				       b->number, 1);
      annotate_breakpoint (b->number);
      if (bp_temp) 
	ui_out_text (uiout, "\nTemporary breakpoint ");
      else
	ui_out_text (uiout, "\nBreakpoint ");
      if (ui_out_is_mi_like_p (uiout))
	{
	  ui_out_field_string (uiout, "reason", 
			  async_reason_lookup (EXEC_ASYNC_BREAKPOINT_HIT));
	  ui_out_field_string (uiout, "disp", bpdisp_text (b->disposition));
	}
      ui_out_field_int (uiout, "bkptno", b->number);
      ui_out_text (uiout, ", ");
      result = PRINT_SRC_AND_LOC;
      break;

    case bp_shlib_event:
      /* Did we stop because the user set the stop_on_solib_events
	 variable?  (If so, we report this as a generic, "Stopped due
	 to shlib event" message.) */
      printf_filtered (_("Stopped due to shared library event\n"));
      result = PRINT_NOTHING;
      break;

    case bp_thread_event:
      /* Not sure how we will get here. 
	 GDB should not stop for these breakpoints.  */
      printf_filtered (_("Thread Event Breakpoint: gdb should not stop!\n"));
      result = PRINT_NOTHING;
      break;

    case bp_overlay_event:
      /* By analogy with the thread event, GDB should not stop for these. */
      printf_filtered (_("Overlay Event Breakpoint: gdb should not stop!\n"));
      result = PRINT_NOTHING;
      break;

    case bp_longjmp_master:
      /* These should never be enabled.  */
      printf_filtered (_("Longjmp Master Breakpoint: gdb should not stop!\n"));
      result = PRINT_NOTHING;
      break;

    case bp_watchpoint:
    case bp_hardware_watchpoint:
      annotate_watchpoint (b->number);
      if (ui_out_is_mi_like_p (uiout))
	ui_out_field_string
	  (uiout, "reason",
	   async_reason_lookup (EXEC_ASYNC_WATCHPOINT_TRIGGER));
      mention (b);
      make_cleanup_ui_out_tuple_begin_end (uiout, "value");
      ui_out_text (uiout, "\nOld value = ");
      watchpoint_value_print (bs->old_val, stb->stream);
      ui_out_field_stream (uiout, "old", stb);
      ui_out_text (uiout, "\nNew value = ");
      watchpoint_value_print (b->val, stb->stream);
      ui_out_field_stream (uiout, "new", stb);
      ui_out_text (uiout, "\n");
      /* More than one watchpoint may have been triggered.  */
      result = PRINT_UNKNOWN;
      break;

    case bp_read_watchpoint:
      if (ui_out_is_mi_like_p (uiout))
	ui_out_field_string
	  (uiout, "reason",
	   async_reason_lookup (EXEC_ASYNC_READ_WATCHPOINT_TRIGGER));
      mention (b);
      make_cleanup_ui_out_tuple_begin_end (uiout, "value");
      ui_out_text (uiout, "\nValue = ");
      watchpoint_value_print (b->val, stb->stream);
      ui_out_field_stream (uiout, "value", stb);
      ui_out_text (uiout, "\n");
      result = PRINT_UNKNOWN;
      break;

    case bp_access_watchpoint:
      if (bs->old_val != NULL)
	{
	  annotate_watchpoint (b->number);
	  if (ui_out_is_mi_like_p (uiout))
	    ui_out_field_string
	      (uiout, "reason",
	       async_reason_lookup (EXEC_ASYNC_ACCESS_WATCHPOINT_TRIGGER));
	  mention (b);
	  make_cleanup_ui_out_tuple_begin_end (uiout, "value");
	  ui_out_text (uiout, "\nOld value = ");
	  watchpoint_value_print (bs->old_val, stb->stream);
	  ui_out_field_stream (uiout, "old", stb);
	  ui_out_text (uiout, "\nNew value = ");
	}
      else 
	{
	  mention (b);
	  if (ui_out_is_mi_like_p (uiout))
	    ui_out_field_string
	      (uiout, "reason",
	       async_reason_lookup (EXEC_ASYNC_ACCESS_WATCHPOINT_TRIGGER));
	  make_cleanup_ui_out_tuple_begin_end (uiout, "value");
	  ui_out_text (uiout, "\nValue = ");
	}
      watchpoint_value_print (b->val, stb->stream);
      ui_out_field_stream (uiout, "new", stb);
      ui_out_text (uiout, "\n");
      result = PRINT_UNKNOWN;
      break;

    /* Fall through, we don't deal with these types of breakpoints
       here. */

    case bp_finish:
      if (ui_out_is_mi_like_p (uiout))
	ui_out_field_string
	  (uiout, "reason",
	   async_reason_lookup (EXEC_ASYNC_FUNCTION_FINISHED));
      result = PRINT_UNKNOWN;
      break;

    case bp_until:
      if (ui_out_is_mi_like_p (uiout))
	ui_out_field_string
	  (uiout, "reason",
	   async_reason_lookup (EXEC_ASYNC_LOCATION_REACHED));
      result = PRINT_UNKNOWN;
      break;

    case bp_none:
    case bp_longjmp:
    case bp_longjmp_resume:
    case bp_step_resume:
    case bp_watchpoint_scope:
    case bp_call_dummy:
    case bp_tracepoint:
    case bp_jit_event:
    default:
      result = PRINT_UNKNOWN;
      break;
    }

  do_cleanups (old_chain);
  return result;
}

/* Generic routine for printing messages indicating why we
   stopped. The behavior of this function depends on the value
   'print_it' in the bpstat structure.  Under some circumstances we
   may decide not to print anything here and delegate the task to
   normal_stop(). */

static enum print_stop_action
print_bp_stop_message (bpstat bs)
{
  switch (bs->print_it)
    {
    case print_it_noop:
      /* Nothing should be printed for this bpstat entry. */
      return PRINT_UNKNOWN;
      break;

    case print_it_done:
      /* We still want to print the frame, but we already printed the
         relevant messages. */
      return PRINT_SRC_AND_LOC;
      break;

    case print_it_normal:
      {
	const struct bp_location *bl = bs->breakpoint_at;
	struct breakpoint *b = bl ? bl->owner : NULL;
	
	/* Normal case.  Call the breakpoint's print_it method, or
	   print_it_typical.  */
	/* FIXME: how breakpoint can ever be NULL here?  */
	if (b != NULL && b->ops != NULL && b->ops->print_it != NULL)
	  return b->ops->print_it (b);
	else
	  return print_it_typical (bs);
      }
	break;

    default:
      internal_error (__FILE__, __LINE__,
		      _("print_bp_stop_message: unrecognized enum value"));
      break;
    }
}

/* Print a message indicating what happened.  This is called from
   normal_stop().  The input to this routine is the head of the bpstat
   list - a list of the eventpoints that caused this stop.  This
   routine calls the generic print routine for printing a message
   about reasons for stopping.  This will print (for example) the
   "Breakpoint n," part of the output.  The return value of this
   routine is one of:

   PRINT_UNKNOWN: Means we printed nothing
   PRINT_SRC_AND_LOC: Means we printed something, and expect subsequent
   code to print the location. An example is 
   "Breakpoint 1, " which should be followed by
   the location.
   PRINT_SRC_ONLY: Means we printed something, but there is no need
   to also print the location part of the message.
   An example is the catch/throw messages, which
   don't require a location appended to the end.  
   PRINT_NOTHING: We have done some printing and we don't need any 
   further info to be printed.*/

enum print_stop_action
bpstat_print (bpstat bs)
{
  int val;

  /* Maybe another breakpoint in the chain caused us to stop.
     (Currently all watchpoints go on the bpstat whether hit or not.
     That probably could (should) be changed, provided care is taken
     with respect to bpstat_explains_signal).  */
  for (; bs; bs = bs->next)
    {
      val = print_bp_stop_message (bs);
      if (val == PRINT_SRC_ONLY 
	  || val == PRINT_SRC_AND_LOC 
	  || val == PRINT_NOTHING)
	return val;
    }

  /* We reached the end of the chain, or we got a null BS to start
     with and nothing was printed. */
  return PRINT_UNKNOWN;
}

/* Evaluate the expression EXP and return 1 if value is zero.
   This is used inside a catch_errors to evaluate the breakpoint condition. 
   The argument is a "struct expression *" that has been cast to char * to 
   make it pass through catch_errors.  */

static int
breakpoint_cond_eval (void *exp)
{
  struct value *mark = value_mark ();
  int i = !value_true (evaluate_expression ((struct expression *) exp));
  value_free_to_mark (mark);
  return i;
}

/* Allocate a new bpstat and chain it to the current one.  */

static bpstat
bpstat_alloc (const struct bp_location *bl, bpstat cbs /* Current "bs" value */ )
{
  bpstat bs;

  bs = (bpstat) xmalloc (sizeof (*bs));
  cbs->next = bs;
  bs->breakpoint_at = bl;
  /* If the condition is false, etc., don't do the commands.  */
  bs->commands = NULL;
  bs->old_val = NULL;
  bs->print_it = print_it_normal;
  return bs;
}

/* The target has stopped with waitstatus WS.  Check if any hardware
   watchpoints have triggered, according to the target.  */

int
watchpoints_triggered (struct target_waitstatus *ws)
{
  int stopped_by_watchpoint = target_stopped_by_watchpoint ();
  CORE_ADDR addr;
  struct breakpoint *b;

  if (!stopped_by_watchpoint)
    {
      /* We were not stopped by a watchpoint.  Mark all watchpoints
	 as not triggered.  */
      ALL_BREAKPOINTS (b)
	if (b->type == bp_hardware_watchpoint
	    || b->type == bp_read_watchpoint
	    || b->type == bp_access_watchpoint)
	  b->watchpoint_triggered = watch_triggered_no;

      return 0;
    }

  if (!target_stopped_data_address (&current_target, &addr))
    {
      /* We were stopped by a watchpoint, but we don't know where.
	 Mark all watchpoints as unknown.  */
      ALL_BREAKPOINTS (b)
	if (b->type == bp_hardware_watchpoint
	    || b->type == bp_read_watchpoint
	    || b->type == bp_access_watchpoint)
	  b->watchpoint_triggered = watch_triggered_unknown;

      return stopped_by_watchpoint;
    }

  /* The target could report the data address.  Mark watchpoints
     affected by this data address as triggered, and all others as not
     triggered.  */

  ALL_BREAKPOINTS (b)
    if (b->type == bp_hardware_watchpoint
	|| b->type == bp_read_watchpoint
	|| b->type == bp_access_watchpoint)
      {
	struct bp_location *loc;
	struct value *v;

	b->watchpoint_triggered = watch_triggered_no;
	for (loc = b->loc; loc; loc = loc->next)
	  /* Exact match not required.  Within range is
	     sufficient.  */
	  if (target_watchpoint_addr_within_range (&current_target,
						   addr, loc->address,
						   loc->length))
	    {
	      b->watchpoint_triggered = watch_triggered_yes;
	      break;
	    }
      }

  return 1;
}

/* Possible return values for watchpoint_check (this can't be an enum
   because of check_errors).  */
/* The watchpoint has been deleted.  */
#define WP_DELETED 1
/* The value has changed.  */
#define WP_VALUE_CHANGED 2
/* The value has not changed.  */
#define WP_VALUE_NOT_CHANGED 3

#define BP_TEMPFLAG 1
#define BP_HARDWAREFLAG 2

/* Check watchpoint condition.  */

static int
watchpoint_check (void *p)
{
  bpstat bs = (bpstat) p;
  struct breakpoint *b;
  struct frame_info *fr;
  int within_current_scope;

  b = bs->breakpoint_at->owner;

  if (b->exp_valid_block == NULL)
    within_current_scope = 1;
  else
    {
      struct frame_info *frame = get_current_frame ();
      struct gdbarch *frame_arch = get_frame_arch (frame);
      CORE_ADDR frame_pc = get_frame_pc (frame);

      fr = frame_find_by_id (b->watchpoint_frame);
      within_current_scope = (fr != NULL);

      /* If we've gotten confused in the unwinder, we might have
	 returned a frame that can't describe this variable.  */
      if (within_current_scope)
	{
	  struct symbol *function;

	  function = get_frame_function (fr);
	  if (function == NULL
	      || !contained_in (b->exp_valid_block,
				SYMBOL_BLOCK_VALUE (function)))
	    within_current_scope = 0;
	}

      /* in_function_epilogue_p() returns a non-zero value if we're still
	 in the function but the stack frame has already been invalidated.
	 Since we can't rely on the values of local variables after the
	 stack has been destroyed, we are treating the watchpoint in that
	 state as `not changed' without further checking.  Don't mark
	 watchpoints as changed if the current frame is in an epilogue -
	 even if they are in some other frame, our view of the stack
	 is likely to be wrong.  */
      if (gdbarch_in_function_epilogue_p (frame_arch, frame_pc))
	return WP_VALUE_NOT_CHANGED;

      if (within_current_scope)
	/* If we end up stopping, the current frame will get selected
	   in normal_stop.  So this call to select_frame won't affect
	   the user.  */
	select_frame (fr);
    }

  if (within_current_scope)
    {
      /* We use value_{,free_to_}mark because it could be a
         *long* time before we return to the command level and
         call free_all_values.  We can't call free_all_values because
         we might be in the middle of evaluating a function call.  */

      struct value *mark = value_mark ();
      struct value *new_val;

      fetch_watchpoint_value (b->exp, &new_val, NULL, NULL);
      if ((b->val != NULL) != (new_val != NULL)
	  || (b->val != NULL && !value_equal (b->val, new_val)))
	{
	  if (new_val != NULL)
	    {
	      release_value (new_val);
	      value_free_to_mark (mark);
	    }
	  bs->old_val = b->val;
	  b->val = new_val;
	  b->val_valid = 1;
	  /* We will stop here */
	  return WP_VALUE_CHANGED;
	}
      else
	{
	  /* Nothing changed, don't do anything.  */
	  value_free_to_mark (mark);
	  /* We won't stop here */
	  return WP_VALUE_NOT_CHANGED;
	}
    }
  else
    {
      /* This seems like the only logical thing to do because
         if we temporarily ignored the watchpoint, then when
         we reenter the block in which it is valid it contains
         garbage (in the case of a function, it may have two
         garbage values, one before and one after the prologue).
         So we can't even detect the first assignment to it and
         watch after that (since the garbage may or may not equal
         the first value assigned).  */
      /* We print all the stop information in print_it_typical(), but
	 in this case, by the time we call print_it_typical() this bp
	 will be deleted already. So we have no choice but print the
	 information here. */
      if (ui_out_is_mi_like_p (uiout))
	ui_out_field_string
	  (uiout, "reason", async_reason_lookup (EXEC_ASYNC_WATCHPOINT_SCOPE));
      ui_out_text (uiout, "\nWatchpoint ");
      ui_out_field_int (uiout, "wpnum", b->number);
      ui_out_text (uiout, " deleted because the program has left the block in\n\
which its expression is valid.\n");     

      if (b->related_breakpoint)
	b->related_breakpoint->disposition = disp_del_at_next_stop;
      b->disposition = disp_del_at_next_stop;

      return WP_DELETED;
    }
}

/* Return true if it looks like target has stopped due to hitting
   breakpoint location BL.  This function does not check if we
   should stop, only if BL explains the stop.   */
static int
bpstat_check_location (const struct bp_location *bl, CORE_ADDR bp_addr)
{
  struct breakpoint *b = bl->owner;

  if (b->type != bp_watchpoint
      && b->type != bp_hardware_watchpoint
      && b->type != bp_read_watchpoint
      && b->type != bp_access_watchpoint
      && b->type != bp_hardware_breakpoint
      && b->type != bp_catchpoint)	/* a non-watchpoint bp */
    {
      if (bl->address != bp_addr) 	/* address doesn't match */
	return 0;
      if (overlay_debugging		/* unmapped overlay section */
	  && section_is_overlay (bl->section) 
	  && !section_is_mapped (bl->section))
	return 0;
    }
  
  /* Continuable hardware watchpoints are treated as non-existent if the
     reason we stopped wasn't a hardware watchpoint (we didn't stop on
     some data address).  Otherwise gdb won't stop on a break instruction
     in the code (not from a breakpoint) when a hardware watchpoint has
     been defined.  Also skip watchpoints which we know did not trigger
     (did not match the data address).  */
  
  if ((b->type == bp_hardware_watchpoint
       || b->type == bp_read_watchpoint
       || b->type == bp_access_watchpoint)
      && b->watchpoint_triggered == watch_triggered_no)
    return 0;
  
  if (b->type == bp_hardware_breakpoint)
    {
      if (bl->address != bp_addr)
	return 0;
      if (overlay_debugging		/* unmapped overlay section */
	  && section_is_overlay (bl->section) 
	  && !section_is_mapped (bl->section))
	return 0;
    }

  if (b->type == bp_catchpoint)
    {
      gdb_assert (b->ops != NULL && b->ops->breakpoint_hit != NULL);
      if (!b->ops->breakpoint_hit (b))
        return 0;
    }
     
  return 1;
}

/* If BS refers to a watchpoint, determine if the watched values
   has actually changed, and we should stop.  If not, set BS->stop
   to 0.  */
static void
bpstat_check_watchpoint (bpstat bs)
{
  const struct bp_location *bl = bs->breakpoint_at;
  struct breakpoint *b = bl->owner;

  if (b->type == bp_watchpoint
      || b->type == bp_read_watchpoint
      || b->type == bp_access_watchpoint
      || b->type == bp_hardware_watchpoint)
    {
      CORE_ADDR addr;
      struct value *v;
      int must_check_value = 0;
      
      if (b->type == bp_watchpoint)
	/* For a software watchpoint, we must always check the
	   watched value.  */
	must_check_value = 1;
      else if (b->watchpoint_triggered == watch_triggered_yes)
	/* We have a hardware watchpoint (read, write, or access)
	   and the target earlier reported an address watched by
	   this watchpoint.  */
	must_check_value = 1;
      else if (b->watchpoint_triggered == watch_triggered_unknown
	       && b->type == bp_hardware_watchpoint)
	/* We were stopped by a hardware watchpoint, but the target could
	   not report the data address.  We must check the watchpoint's
	   value.  Access and read watchpoints are out of luck; without
	   a data address, we can't figure it out.  */
	must_check_value = 1;
      
      if (must_check_value)
	{
	  char *message = xstrprintf ("Error evaluating expression for watchpoint %d\n",
				      b->number);
	  struct cleanup *cleanups = make_cleanup (xfree, message);
	  int e = catch_errors (watchpoint_check, bs, message,
				RETURN_MASK_ALL);
	  do_cleanups (cleanups);
	  switch (e)
	    {
	    case WP_DELETED:
	      /* We've already printed what needs to be printed.  */
	      bs->print_it = print_it_done;
	      /* Stop.  */
	      break;
	    case WP_VALUE_CHANGED:
	      if (b->type == bp_read_watchpoint)
		{
		  /* Don't stop: read watchpoints shouldn't fire if
		     the value has changed.  This is for targets
		     which cannot set read-only watchpoints.  */
		  bs->print_it = print_it_noop;
		  bs->stop = 0;
		}
	      break;
	    case WP_VALUE_NOT_CHANGED:
	      if (b->type == bp_hardware_watchpoint
		  || b->type == bp_watchpoint)
		{
		  /* Don't stop: write watchpoints shouldn't fire if
		     the value hasn't changed.  */
		  bs->print_it = print_it_noop;
		  bs->stop = 0;
		}
	      /* Stop.  */
	      break;
	    default:
	      /* Can't happen.  */
	    case 0:
	      /* Error from catch_errors.  */
	      printf_filtered (_("Watchpoint %d deleted.\n"), b->number);
	      if (b->related_breakpoint)
		b->related_breakpoint->disposition = disp_del_at_next_stop;
	      b->disposition = disp_del_at_next_stop;
	      /* We've already printed what needs to be printed.  */
	      bs->print_it = print_it_done;
	      break;
	    }
	}
      else	/* must_check_value == 0 */
	{
	  /* This is a case where some watchpoint(s) triggered, but
	     not at the address of this watchpoint, or else no
	     watchpoint triggered after all.  So don't print
	     anything for this watchpoint.  */
	  bs->print_it = print_it_noop;
	  bs->stop = 0;
	}
    }
}


/* Check conditions (condition proper, frame, thread and ignore count)
   of breakpoint referred to by BS.  If we should not stop for this
   breakpoint, set BS->stop to 0.  */
static void
bpstat_check_breakpoint_conditions (bpstat bs, ptid_t ptid)
{
  int thread_id = pid_to_thread_id (ptid);
  const struct bp_location *bl = bs->breakpoint_at;
  struct breakpoint *b = bl->owner;

  if (frame_id_p (b->frame_id)
      && !frame_id_eq (b->frame_id, get_stack_frame_id (get_current_frame ())))
    bs->stop = 0;
  else if (bs->stop)
    {
      int value_is_zero = 0;
      
      /* If this is a scope breakpoint, mark the associated
	 watchpoint as triggered so that we will handle the
	 out-of-scope event.  We'll get to the watchpoint next
	 iteration.  */
      if (b->type == bp_watchpoint_scope)
	b->related_breakpoint->watchpoint_triggered = watch_triggered_yes;
      
      if (bl->cond && bl->owner->disposition != disp_del_at_next_stop)
	{
	  /* We use value_mark and value_free_to_mark because it could
	     be a long time before we return to the command level and
	     call free_all_values.  We can't call free_all_values
	     because we might be in the middle of evaluating a
	     function call.  */
	  struct value *mark = value_mark ();

	  /* Need to select the frame, with all that implies so that
	     the conditions will have the right context.  Because we
	     use the frame, we will not see an inlined function's
	     variables when we arrive at a breakpoint at the start
	     of the inlined function; the current frame will be the
	     call site.  */
	  select_frame (get_current_frame ());
	  value_is_zero
	    = catch_errors (breakpoint_cond_eval, (bl->cond),
			    "Error in testing breakpoint condition:\n",
			    RETURN_MASK_ALL);
	  /* FIXME-someday, should give breakpoint # */
	  value_free_to_mark (mark);
	}
      if (bl->cond && value_is_zero)
	{
	  bs->stop = 0;
	}
      else if (b->thread != -1 && b->thread != thread_id)
	{
	  bs->stop = 0;
	}
      else if (b->ignore_count > 0)
	{
	  b->ignore_count--;
	  annotate_ignore_count_change ();
	  bs->stop = 0;
	  /* Increase the hit count even though we don't
	     stop.  */
	  ++(b->hit_count);
	}	
    }
}


/* Get a bpstat associated with having just stopped at address
   BP_ADDR in thread PTID.

   Determine whether we stopped at a breakpoint, etc, or whether we
   don't understand this stop.  Result is a chain of bpstat's such that:

   if we don't understand the stop, the result is a null pointer.

   if we understand why we stopped, the result is not null.

   Each element of the chain refers to a particular breakpoint or
   watchpoint at which we have stopped.  (We may have stopped for
   several reasons concurrently.)

   Each element of the chain has valid next, breakpoint_at,
   commands, FIXME??? fields.  */

bpstat
bpstat_stop_status (CORE_ADDR bp_addr, ptid_t ptid)
{
  struct breakpoint *b = NULL;
  const struct bp_location *bl;
  struct bp_location *loc;
  /* Root of the chain of bpstat's */
  struct bpstats root_bs[1];
  /* Pointer to the last thing in the chain currently.  */
  bpstat bs = root_bs;
  int ix;
  int need_remove_insert;

  ALL_BP_LOCATIONS (bl)
  {
    b = bl->owner;
    gdb_assert (b);
    if (!breakpoint_enabled (b) && b->enable_state != bp_permanent)
      continue;

    /* For hardware watchpoints, we look only at the first location.
       The watchpoint_check function will work on entire expression,
       not the individual locations.  For read watchopints, the
       watchpoints_triggered function have checked all locations
       alrea
     */
    if (b->type == bp_hardware_watchpoint && bl != b->loc)
      continue;

    if (!bpstat_check_location (bl, bp_addr))
      continue;

    /* Come here if it's a watchpoint, or if the break address matches */

    bs = bpstat_alloc (bl, bs);	/* Alloc a bpstat to explain stop */

    /* Assume we stop.  Should we find watchpoint that is not actually
       triggered, or if condition of breakpoint is false, we'll reset
       'stop' to 0.  */
    bs->stop = 1;
    bs->print = 1;

    bpstat_check_watchpoint (bs);
    if (!bs->stop)
      continue;

    if (b->type == bp_thread_event || b->type == bp_overlay_event
	|| b->type == bp_longjmp_master)
      /* We do not stop for these.  */
      bs->stop = 0;
    else
      bpstat_check_breakpoint_conditions (bs, ptid);
  
    if (bs->stop)
      {
	++(b->hit_count);

	/* We will stop here */
	if (b->disposition == disp_disable)
	  {
	    if (b->enable_state != bp_permanent)
	      b->enable_state = bp_disabled;
	    update_global_location_list (0);
	  }
	if (b->silent)
	  bs->print = 0;
	bs->commands = b->commands;
	if (bs->commands
	    && (strcmp ("silent", bs->commands->line) == 0
		|| (xdb_commands && strcmp ("Q", bs->commands->line) == 0)))
	  {
	    bs->commands = bs->commands->next;
	    bs->print = 0;
	  }
	bs->commands = copy_command_lines (bs->commands);
      }

    /* Print nothing for this entry if we dont stop or if we dont print.  */
    if (bs->stop == 0 || bs->print == 0)
      bs->print_it = print_it_noop;
  }

  for (ix = 0; VEC_iterate (bp_location_p, moribund_locations, ix, loc); ++ix)
    {
      if (loc->address == bp_addr)
	{
	  bs = bpstat_alloc (loc, bs);
	  /* For hits of moribund locations, we should just proceed.  */
	  bs->stop = 0;
	  bs->print = 0;
	  bs->print_it = print_it_noop;
	}
    }

  bs->next = NULL;		/* Terminate the chain */
  bs = root_bs->next;		/* Re-grab the head of the chain */

  /* If we aren't stopping, the value of some hardware watchpoint may
     not have changed, but the intermediate memory locations we are
     watching may have.  Don't bother if we're stopping; this will get
     done later.  */
  for (bs = root_bs->next; bs != NULL; bs = bs->next)
    if (bs->stop)
      break;

  need_remove_insert = 0;
  if (bs == NULL)
    for (bs = root_bs->next; bs != NULL; bs = bs->next)
      if (!bs->stop
	  && bs->breakpoint_at->owner
	  && (bs->breakpoint_at->owner->type == bp_hardware_watchpoint
	      || bs->breakpoint_at->owner->type == bp_read_watchpoint
	      || bs->breakpoint_at->owner->type == bp_access_watchpoint))
	{
	  /* remove/insert can invalidate bs->breakpoint_at, if this
	     location is no longer used by the watchpoint.  Prevent
	     further code from trying to use it.  */
	  bs->breakpoint_at = NULL;
	  need_remove_insert = 1;
	}

  if (need_remove_insert)
    {
      remove_breakpoints ();
      insert_breakpoints ();
    }

  return root_bs->next;
}

/* Tell what to do about this bpstat.  */
struct bpstat_what
bpstat_what (bpstat bs)
{
  /* Classify each bpstat as one of the following.  */
  enum class
    {
      /* This bpstat element has no effect on the main_action.  */
      no_effect = 0,

      /* There was a watchpoint, stop but don't print.  */
      wp_silent,

      /* There was a watchpoint, stop and print.  */
      wp_noisy,

      /* There was a breakpoint but we're not stopping.  */
      bp_nostop,

      /* There was a breakpoint, stop but don't print.  */
      bp_silent,

      /* There was a breakpoint, stop and print.  */
      bp_noisy,

      /* We hit the longjmp breakpoint.  */
      long_jump,

      /* We hit the longjmp_resume breakpoint.  */
      long_resume,

      /* We hit the step_resume breakpoint.  */
      step_resume,

      /* We hit the shared library event breakpoint.  */
      shlib_event,

      /* We hit the jit event breakpoint.  */
      jit_event,

      /* This is just used to count how many enums there are.  */
      class_last
    };

  /* Here is the table which drives this routine.  So that we can
     format it pretty, we define some abbreviations for the
     enum bpstat_what codes.  */
#define kc BPSTAT_WHAT_KEEP_CHECKING
#define ss BPSTAT_WHAT_STOP_SILENT
#define sn BPSTAT_WHAT_STOP_NOISY
#define sgl BPSTAT_WHAT_SINGLE
#define slr BPSTAT_WHAT_SET_LONGJMP_RESUME
#define clr BPSTAT_WHAT_CLEAR_LONGJMP_RESUME
#define sr BPSTAT_WHAT_STEP_RESUME
#define shl BPSTAT_WHAT_CHECK_SHLIBS
#define jit BPSTAT_WHAT_CHECK_JIT

/* "Can't happen."  Might want to print an error message.
   abort() is not out of the question, but chances are GDB is just
   a bit confused, not unusable.  */
#define err BPSTAT_WHAT_STOP_NOISY

  /* Given an old action and a class, come up with a new action.  */
  /* One interesting property of this table is that wp_silent is the same
     as bp_silent and wp_noisy is the same as bp_noisy.  That is because
     after stopping, the check for whether to step over a breakpoint
     (BPSTAT_WHAT_SINGLE type stuff) is handled in proceed() without
     reference to how we stopped.  We retain separate wp_silent and
     bp_silent codes in case we want to change that someday. 

     Another possibly interesting property of this table is that
     there's a partial ordering, priority-like, of the actions.  Once
     you've decided that some action is appropriate, you'll never go
     back and decide something of a lower priority is better.  The
     ordering is:

     kc   < jit clr sgl shl slr sn sr ss
     sgl  < jit shl slr sn sr ss
     slr  < jit err shl sn sr ss
     clr  < jit err shl sn sr ss
     ss   < jit shl sn sr
     sn   < jit shl sr
     jit  < shl sr
     shl  < sr
     sr   <

     What I think this means is that we don't need a damned table
     here.  If you just put the rows and columns in the right order,
     it'd look awfully regular.  We could simply walk the bpstat list
     and choose the highest priority action we find, with a little
     logic to handle the 'err' cases.  */

  /* step_resume entries: a step resume breakpoint overrides another
     breakpoint of signal handling (see comment in wait_for_inferior
     at where we set the step_resume breakpoint).  */

  static const enum bpstat_what_main_action
    table[(int) class_last][(int) BPSTAT_WHAT_LAST] =
  {
  /*                              old action */
  /*               kc   ss   sn   sgl  slr  clr  sr  shl  jit */
/* no_effect */   {kc,  ss,  sn,  sgl, slr, clr, sr, shl, jit},
/* wp_silent */   {ss,  ss,  sn,  ss,  ss,  ss,  sr, shl, jit},
/* wp_noisy */    {sn,  sn,  sn,  sn,  sn,  sn,  sr, shl, jit},
/* bp_nostop */   {sgl, ss,  sn,  sgl, slr, slr, sr, shl, jit},
/* bp_silent */   {ss,  ss,  sn,  ss,  ss,  ss,  sr, shl, jit},
/* bp_noisy */    {sn,  sn,  sn,  sn,  sn,  sn,  sr, shl, jit},
/* long_jump */   {slr, ss,  sn,  slr, slr, err, sr, shl, jit},
/* long_resume */ {clr, ss,  sn,  err, err, err, sr, shl, jit},
/* step_resume */ {sr,  sr,  sr,  sr,  sr,  sr,  sr, sr,  sr },
/* shlib */       {shl, shl, shl, shl, shl, shl, sr, shl, shl},
/* jit_event */   {jit, jit, jit, jit, jit, jit, sr, jit, jit}
  };

#undef kc
#undef ss
#undef sn
#undef sgl
#undef slr
#undef clr
#undef err
#undef sr
#undef ts
#undef shl
#undef jit
  enum bpstat_what_main_action current_action = BPSTAT_WHAT_KEEP_CHECKING;
  struct bpstat_what retval;

  retval.call_dummy = 0;
  for (; bs != NULL; bs = bs->next)
    {
      enum class bs_class = no_effect;
      if (bs->breakpoint_at == NULL)
	/* I suspect this can happen if it was a momentary breakpoint
	   which has since been deleted.  */
	continue;
      if (bs->breakpoint_at->owner == NULL)
	bs_class = bp_nostop;
      else
      switch (bs->breakpoint_at->owner->type)
	{
	case bp_none:
	  continue;

	case bp_breakpoint:
	case bp_hardware_breakpoint:
	case bp_until:
	case bp_finish:
	  if (bs->stop)
	    {
	      if (bs->print)
		bs_class = bp_noisy;
	      else
		bs_class = bp_silent;
	    }
	  else
	    bs_class = bp_nostop;
	  break;
	case bp_watchpoint:
	case bp_hardware_watchpoint:
	case bp_read_watchpoint:
	case bp_access_watchpoint:
	  if (bs->stop)
	    {
	      if (bs->print)
		bs_class = wp_noisy;
	      else
		bs_class = wp_silent;
	    }
	  else
	    /* There was a watchpoint, but we're not stopping. 
	       This requires no further action.  */
	    bs_class = no_effect;
	  break;
	case bp_longjmp:
	  bs_class = long_jump;
	  break;
	case bp_longjmp_resume:
	  bs_class = long_resume;
	  break;
	case bp_step_resume:
	  if (bs->stop)
	    {
	      bs_class = step_resume;
	    }
	  else
	    /* It is for the wrong frame.  */
	    bs_class = bp_nostop;
	  break;
	case bp_watchpoint_scope:
	  bs_class = bp_nostop;
	  break;
	case bp_shlib_event:
	  bs_class = shlib_event;
	  break;
	case bp_jit_event:
	  bs_class = jit_event;
	  break;
	case bp_thread_event:
	case bp_overlay_event:
	case bp_longjmp_master:
	  bs_class = bp_nostop;
	  break;
	case bp_catchpoint:
	  if (bs->stop)
	    {
	      if (bs->print)
		bs_class = bp_noisy;
	      else
		bs_class = bp_silent;
	    }
	  else
	    /* There was a catchpoint, but we're not stopping.  
	       This requires no further action.  */
	    bs_class = no_effect;
	  break;
	case bp_call_dummy:
	  /* Make sure the action is stop (silent or noisy),
	     so infrun.c pops the dummy frame.  */
	  bs_class = bp_silent;
	  retval.call_dummy = 1;
	  break;
	case bp_tracepoint:
	  /* Tracepoint hits should not be reported back to GDB, and
	     if one got through somehow, it should have been filtered
	     out already.  */
	  internal_error (__FILE__, __LINE__,
			  _("bpstat_what: bp_tracepoint encountered"));
	  break;
	}
      current_action = table[(int) bs_class][(int) current_action];
    }
  retval.main_action = current_action;
  return retval;
}

/* Nonzero if we should step constantly (e.g. watchpoints on machines
   without hardware support).  This isn't related to a specific bpstat,
   just to things like whether watchpoints are set.  */

int
bpstat_should_step (void)
{
  struct breakpoint *b;
  ALL_BREAKPOINTS (b)
    if (breakpoint_enabled (b) && b->type == bp_watchpoint && b->loc != NULL)
      return 1;
  return 0;
}



static void print_breakpoint_location (struct breakpoint *b,
				       struct bp_location *loc,
				       char *wrap_indent,
				       struct ui_stream *stb)
{
  if (b->source_file)
    {
      struct symbol *sym 
	= find_pc_sect_function (loc->address, loc->section);
      if (sym)
	{
	  ui_out_text (uiout, "in ");
	  ui_out_field_string (uiout, "func",
			       SYMBOL_PRINT_NAME (sym));
	  ui_out_wrap_hint (uiout, wrap_indent);
	  ui_out_text (uiout, " at ");
	}
      ui_out_field_string (uiout, "file", b->source_file);
      ui_out_text (uiout, ":");
      
      if (ui_out_is_mi_like_p (uiout))
	{
	  struct symtab_and_line sal = find_pc_line (loc->address, 0);
	  char *fullname = symtab_to_fullname (sal.symtab);
	  
	  if (fullname)
	    ui_out_field_string (uiout, "fullname", fullname);
	}
      
      ui_out_field_int (uiout, "line", b->line_number);
    }
  else if (!b->loc)
    {
      ui_out_field_string (uiout, "pending", b->addr_string);
    }
  else
    {
      print_address_symbolic (loc->address, stb->stream, demangle, "");
      ui_out_field_stream (uiout, "at", stb);
    }
}

/* Print B to gdb_stdout. */
static void
print_one_breakpoint_location (struct breakpoint *b,
			       struct bp_location *loc,
			       int loc_number,
			       struct bp_location **last_loc,
			       int print_address_bits)
{
  struct command_line *l;
  struct symbol *sym;
  struct ep_type_description
    {
      enum bptype type;
      char *description;
    };
  static struct ep_type_description bptypes[] =
  {
    {bp_none, "?deleted?"},
    {bp_breakpoint, "breakpoint"},
    {bp_hardware_breakpoint, "hw breakpoint"},
    {bp_until, "until"},
    {bp_finish, "finish"},
    {bp_watchpoint, "watchpoint"},
    {bp_hardware_watchpoint, "hw watchpoint"},
    {bp_read_watchpoint, "read watchpoint"},
    {bp_access_watchpoint, "acc watchpoint"},
    {bp_longjmp, "longjmp"},
    {bp_longjmp_resume, "longjmp resume"},
    {bp_step_resume, "step resume"},
    {bp_watchpoint_scope, "watchpoint scope"},
    {bp_call_dummy, "call dummy"},
    {bp_shlib_event, "shlib events"},
    {bp_thread_event, "thread events"},
    {bp_overlay_event, "overlay events"},
    {bp_longjmp_master, "longjmp master"},
    {bp_catchpoint, "catchpoint"},
    {bp_tracepoint, "tracepoint"},
    {bp_jit_event, "jit events"},
  };
  
  static char bpenables[] = "nynny";
  char wrap_indent[80];
  struct ui_stream *stb = ui_out_stream_new (uiout);
  struct cleanup *old_chain = make_cleanup_ui_out_stream_delete (stb);
  struct cleanup *bkpt_chain;

  int header_of_multiple = 0;
  int part_of_multiple = (loc != NULL);
  struct value_print_options opts;

  get_user_print_options (&opts);

  gdb_assert (!loc || loc_number != 0);
  /* See comment in print_one_breakpoint concerning
     treatment of breakpoints with single disabled
     location.  */
  if (loc == NULL 
      && (b->loc != NULL 
	  && (b->loc->next != NULL || !b->loc->enabled)))
    header_of_multiple = 1;
  if (loc == NULL)
    loc = b->loc;

  annotate_record ();
  bkpt_chain = make_cleanup_ui_out_tuple_begin_end (uiout, "bkpt");

  /* 1 */
  annotate_field (0);
  if (part_of_multiple)
    {
      char *formatted;
      formatted = xstrprintf ("%d.%d", b->number, loc_number);
      ui_out_field_string (uiout, "number", formatted);
      xfree (formatted);
    }
  else
    {
      ui_out_field_int (uiout, "number", b->number);
    }

  /* 2 */
  annotate_field (1);
  if (part_of_multiple)
    ui_out_field_skip (uiout, "type");
  else 
    {
      if (((int) b->type >= (sizeof (bptypes) / sizeof (bptypes[0])))
	  || ((int) b->type != bptypes[(int) b->type].type))
	internal_error (__FILE__, __LINE__,
			_("bptypes table does not describe type #%d."),
			(int) b->type);
      ui_out_field_string (uiout, "type", bptypes[(int) b->type].description);
    }

  /* 3 */
  annotate_field (2);
  if (part_of_multiple)
    ui_out_field_skip (uiout, "disp");
  else
    ui_out_field_string (uiout, "disp", bpdisp_text (b->disposition));


  /* 4 */
  annotate_field (3);
  if (part_of_multiple)
    ui_out_field_string (uiout, "enabled", loc->enabled ? "y" : "n");
  else
      ui_out_field_fmt (uiout, "enabled", "%c", 
 			bpenables[(int) b->enable_state]);
  ui_out_spaces (uiout, 2);

  
  /* 5 and 6 */
  strcpy (wrap_indent, "                           ");
  if (opts.addressprint)
    {
      if (print_address_bits <= 32)
	strcat (wrap_indent, "           ");
      else
	strcat (wrap_indent, "                   ");
    }

  if (b->ops != NULL && b->ops->print_one != NULL)
    {
      /* Although the print_one can possibly print
	 all locations,  calling it here is not likely
	 to get any nice result.  So, make sure there's
	 just one location.  */
      gdb_assert (b->loc == NULL || b->loc->next == NULL);
      b->ops->print_one (b, last_loc);
    }
  else
    switch (b->type)
      {
      case bp_none:
	internal_error (__FILE__, __LINE__,
			_("print_one_breakpoint: bp_none encountered\n"));
	break;

      case bp_watchpoint:
      case bp_hardware_watchpoint:
      case bp_read_watchpoint:
      case bp_access_watchpoint:
	/* Field 4, the address, is omitted (which makes the columns
	   not line up too nicely with the headers, but the effect
	   is relatively readable).  */
	if (opts.addressprint)
	  ui_out_field_skip (uiout, "addr");
	annotate_field (5);
	ui_out_field_string (uiout, "what", b->exp_string);
	break;

      case bp_breakpoint:
      case bp_hardware_breakpoint:
      case bp_until:
      case bp_finish:
      case bp_longjmp:
      case bp_longjmp_resume:
      case bp_step_resume:
      case bp_watchpoint_scope:
      case bp_call_dummy:
      case bp_shlib_event:
      case bp_thread_event:
      case bp_overlay_event:
      case bp_longjmp_master:
      case bp_tracepoint:
      case bp_jit_event:
	if (opts.addressprint)
	  {
	    annotate_field (4);
	    if (header_of_multiple)
	      ui_out_field_string (uiout, "addr", "<MULTIPLE>");
	    else if (b->loc == NULL || loc->shlib_disabled)
	      ui_out_field_string (uiout, "addr", "<PENDING>");
	    else
	      ui_out_field_core_addr (uiout, "addr",
				      loc->gdbarch, loc->address);
	  }
	annotate_field (5);
	if (!header_of_multiple)
	  print_breakpoint_location (b, loc, wrap_indent, stb);
	if (b->loc)
	  *last_loc = b->loc;
	break;
      }

  if (!part_of_multiple)
    {
      if (b->thread != -1)
	{
	  /* FIXME: This seems to be redundant and lost here; see the
	     "stop only in" line a little further down. */
	  ui_out_text (uiout, " thread ");
	  ui_out_field_int (uiout, "thread", b->thread);
	}
      else if (b->task != 0)
	{
	  ui_out_text (uiout, " task ");
	  ui_out_field_int (uiout, "task", b->task);
	}
    }
  
  ui_out_text (uiout, "\n");
  
  if (part_of_multiple && frame_id_p (b->frame_id))
    {
      annotate_field (6);
      ui_out_text (uiout, "\tstop only in stack frame at ");
      /* FIXME: cagney/2002-12-01: Shouldn't be poeking around inside
         the frame ID.  */
      ui_out_field_core_addr (uiout, "frame",
			      b->gdbarch, b->frame_id.stack_addr);
      ui_out_text (uiout, "\n");
    }
  
  if (!part_of_multiple && b->cond_string && !ada_exception_catchpoint_p (b))
    {
      /* We do not print the condition for Ada exception catchpoints
         because the condition is an internal implementation detail
         that we do not want to expose to the user.  */
      annotate_field (7);
      if (b->type == bp_tracepoint)
	ui_out_text (uiout, "\ttrace only if ");
      else
	ui_out_text (uiout, "\tstop only if ");
      ui_out_field_string (uiout, "cond", b->cond_string);
      ui_out_text (uiout, "\n");
    }

  if (!part_of_multiple && b->thread != -1)
    {
      /* FIXME should make an annotation for this */
      ui_out_text (uiout, "\tstop only in thread ");
      ui_out_field_int (uiout, "thread", b->thread);
      ui_out_text (uiout, "\n");
    }
  
  if (!part_of_multiple && b->hit_count)
    {
      /* FIXME should make an annotation for this */
      if (ep_is_catchpoint (b))
	ui_out_text (uiout, "\tcatchpoint");
      else
	ui_out_text (uiout, "\tbreakpoint");
      ui_out_text (uiout, " already hit ");
      ui_out_field_int (uiout, "times", b->hit_count);
      if (b->hit_count == 1)
	ui_out_text (uiout, " time\n");
      else
	ui_out_text (uiout, " times\n");
    }
  
  /* Output the count also if it is zero, but only if this is
     mi. FIXME: Should have a better test for this. */
  if (ui_out_is_mi_like_p (uiout))
    if (!part_of_multiple && b->hit_count == 0)
      ui_out_field_int (uiout, "times", b->hit_count);

  if (!part_of_multiple && b->ignore_count)
    {
      annotate_field (8);
      ui_out_text (uiout, "\tignore next ");
      ui_out_field_int (uiout, "ignore", b->ignore_count);
      ui_out_text (uiout, " hits\n");
    }

  l = b->commands;
  if (!part_of_multiple && l)
    {
      struct cleanup *script_chain;

      annotate_field (9);
      script_chain = make_cleanup_ui_out_tuple_begin_end (uiout, "script");
      print_command_lines (uiout, l, 4);
      do_cleanups (script_chain);
    }

  if (!part_of_multiple && b->pass_count)
    {
      annotate_field (10);
      ui_out_text (uiout, "\tpass count ");
      ui_out_field_int (uiout, "pass", b->pass_count);
      ui_out_text (uiout, " \n");
    }

  if (!part_of_multiple && b->step_count)
    {
      annotate_field (11);
      ui_out_text (uiout, "\tstep count ");
      ui_out_field_int (uiout, "step", b->step_count);
      ui_out_text (uiout, " \n");
    }

  if (!part_of_multiple && b->actions)
    {
      struct action_line *action;
      annotate_field (12);
      for (action = b->actions; action; action = action->next)
	{
	  ui_out_text (uiout, "      A\t");
	  ui_out_text (uiout, action->action);
	  ui_out_text (uiout, "\n");
	}
    }

  if (ui_out_is_mi_like_p (uiout) && !part_of_multiple)
    {
      if (b->addr_string)
	ui_out_field_string (uiout, "original-location", b->addr_string);
      else if (b->exp_string)
	ui_out_field_string (uiout, "original-location", b->exp_string);
    }
	
  do_cleanups (bkpt_chain);
  do_cleanups (old_chain);
}

static void
print_one_breakpoint (struct breakpoint *b,
		      struct bp_location **last_loc, int print_address_bits)
{
  print_one_breakpoint_location (b, NULL, 0, last_loc, print_address_bits);

  /* If this breakpoint has custom print function,
     it's already printed.  Otherwise, print individual
     locations, if any.  */
  if (b->ops == NULL || b->ops->print_one == NULL)
    {
      /* If breakpoint has a single location that is
	 disabled, we print it as if it had
	 several locations, since otherwise it's hard to
	 represent "breakpoint enabled, location disabled"
	 situation.  
	 Note that while hardware watchpoints have
	 several locations internally, that's no a property
	 exposed to user.  */
      if (b->loc 
	  && !is_hardware_watchpoint (b)
	  && (b->loc->next || !b->loc->enabled)
	  && !ui_out_is_mi_like_p (uiout)) 
	{
	  struct bp_location *loc;
	  int n = 1;
	  for (loc = b->loc; loc; loc = loc->next, ++n)
	    print_one_breakpoint_location (b, loc, n, last_loc,
					   print_address_bits);
	}
    }
}

static int
breakpoint_address_bits (struct breakpoint *b)
{
  int print_address_bits = 0;
  struct bp_location *loc;

  for (loc = b->loc; loc; loc = loc->next)
    {
      int addr_bit = gdbarch_addr_bit (b->gdbarch);
      if (addr_bit > print_address_bits)
	print_address_bits = addr_bit;
    }

  return print_address_bits;
}

struct captured_breakpoint_query_args
  {
    int bnum;
  };

static int
do_captured_breakpoint_query (struct ui_out *uiout, void *data)
{
  struct captured_breakpoint_query_args *args = data;
  struct breakpoint *b;
  struct bp_location *dummy_loc = NULL;
  ALL_BREAKPOINTS (b)
    {
      if (args->bnum == b->number)
	{
	  int print_address_bits = breakpoint_address_bits (b);
	  print_one_breakpoint (b, &dummy_loc, print_address_bits);
	  return GDB_RC_OK;
	}
    }
  return GDB_RC_NONE;
}

enum gdb_rc
gdb_breakpoint_query (struct ui_out *uiout, int bnum, char **error_message)
{
  struct captured_breakpoint_query_args args;
  args.bnum = bnum;
  /* For the moment we don't trust print_one_breakpoint() to not throw
     an error. */
  if (catch_exceptions_with_msg (uiout, do_captured_breakpoint_query, &args,
				 error_message, RETURN_MASK_ALL) < 0)
    return GDB_RC_FAIL;
  else
    return GDB_RC_OK;
}

/* Return non-zero if B is user settable (breakpoints, watchpoints,
   catchpoints, et.al.). */

static int
user_settable_breakpoint (const struct breakpoint *b)
{
  return (b->type == bp_breakpoint
	  || b->type == bp_catchpoint
	  || b->type == bp_hardware_breakpoint
	  || b->type == bp_tracepoint
	  || b->type == bp_watchpoint
	  || b->type == bp_read_watchpoint
	  || b->type == bp_access_watchpoint
	  || b->type == bp_hardware_watchpoint);
}
	
/* Print information on user settable breakpoint (watchpoint, etc)
   number BNUM.  If BNUM is -1 print all user settable breakpoints.
   If ALLFLAG is non-zero, include non- user settable breakpoints. */

static void
breakpoint_1 (int bnum, int allflag)
{
  struct breakpoint *b;
  struct bp_location *last_loc = NULL;
  int nr_printable_breakpoints;
  struct cleanup *bkpttbl_chain;
  struct value_print_options opts;
  int print_address_bits = 0;
  
  get_user_print_options (&opts);

  /* Compute the number of rows in the table, as well as the
     size required for address fields.  */
  nr_printable_breakpoints = 0;
  ALL_BREAKPOINTS (b)
    if (bnum == -1
	|| bnum == b->number)
      {
	if (allflag || user_settable_breakpoint (b))
	  {
	    int addr_bit = breakpoint_address_bits (b);
	    if (addr_bit > print_address_bits)
	      print_address_bits = addr_bit;

	    nr_printable_breakpoints++;
	  }
      }

  if (opts.addressprint)
    bkpttbl_chain 
      = make_cleanup_ui_out_table_begin_end (uiout, 6, nr_printable_breakpoints,
                                             "BreakpointTable");
  else
    bkpttbl_chain 
      = make_cleanup_ui_out_table_begin_end (uiout, 5, nr_printable_breakpoints,
                                             "BreakpointTable");

  if (nr_printable_breakpoints > 0)
    annotate_breakpoints_headers ();
  if (nr_printable_breakpoints > 0)
    annotate_field (0);
  ui_out_table_header (uiout, 7, ui_left, "number", "Num");		/* 1 */
  if (nr_printable_breakpoints > 0)
    annotate_field (1);
  ui_out_table_header (uiout, 14, ui_left, "type", "Type");		/* 2 */
  if (nr_printable_breakpoints > 0)
    annotate_field (2);
  ui_out_table_header (uiout, 4, ui_left, "disp", "Disp");		/* 3 */
  if (nr_printable_breakpoints > 0)
    annotate_field (3);
  ui_out_table_header (uiout, 3, ui_left, "enabled", "Enb");	/* 4 */
  if (opts.addressprint)
	{
	  if (nr_printable_breakpoints > 0)
	    annotate_field (4);
	  if (print_address_bits <= 32)
	    ui_out_table_header (uiout, 10, ui_left, "addr", "Address");/* 5 */
	  else
	    ui_out_table_header (uiout, 18, ui_left, "addr", "Address");/* 5 */
	}
  if (nr_printable_breakpoints > 0)
    annotate_field (5);
  ui_out_table_header (uiout, 40, ui_noalign, "what", "What");	/* 6 */
  ui_out_table_body (uiout);
  if (nr_printable_breakpoints > 0)
    annotate_breakpoints_table ();

  ALL_BREAKPOINTS (b)
    if (bnum == -1
	|| bnum == b->number)
      {
	/* We only print out user settable breakpoints unless the
	   allflag is set. */
	if (allflag || user_settable_breakpoint (b))
	  print_one_breakpoint (b, &last_loc, print_address_bits);
      }
  
  do_cleanups (bkpttbl_chain);

  if (nr_printable_breakpoints == 0)
    {
      if (bnum == -1)
	ui_out_message (uiout, 0, "No breakpoints or watchpoints.\n");
      else
	ui_out_message (uiout, 0, "No breakpoint or watchpoint number %d.\n",
			bnum);
    }
  else
    {
      if (last_loc && !server_command)
	set_next_address (last_loc->gdbarch, last_loc->address);
    }

  /* FIXME? Should this be moved up so that it is only called when
     there have been breakpoints? */
  annotate_breakpoints_table_end ();
}

static void
breakpoints_info (char *bnum_exp, int from_tty)
{
  int bnum = -1;

  if (bnum_exp)
    bnum = parse_and_eval_long (bnum_exp);

  breakpoint_1 (bnum, 0);
}

static void
maintenance_info_breakpoints (char *bnum_exp, int from_tty)
{
  int bnum = -1;

  if (bnum_exp)
    bnum = parse_and_eval_long (bnum_exp);

  breakpoint_1 (bnum, 1);
}

static int
breakpoint_has_pc (struct breakpoint *b,
		   CORE_ADDR pc, struct obj_section *section)
{
  struct bp_location *bl = b->loc;
  for (; bl; bl = bl->next)
    {
      if (bl->address == pc
	  && (!overlay_debugging || bl->section == section))
	return 1;	  
    }
  return 0;
}

/* Print a message describing any breakpoints set at PC.  */

static void
describe_other_breakpoints (struct gdbarch *gdbarch, CORE_ADDR pc,
			    struct obj_section *section, int thread)
{
  int others = 0;
  struct breakpoint *b;

  ALL_BREAKPOINTS (b)
    others += breakpoint_has_pc (b, pc, section);
  if (others > 0)
    {
      if (others == 1)
	printf_filtered (_("Note: breakpoint "));
      else /* if (others == ???) */
	printf_filtered (_("Note: breakpoints "));
      ALL_BREAKPOINTS (b)
	if (breakpoint_has_pc (b, pc, section))
	  {
	    others--;
	    printf_filtered ("%d", b->number);
	    if (b->thread == -1 && thread != -1)
	      printf_filtered (" (all threads)");
	    else if (b->thread != -1)
	      printf_filtered (" (thread %d)", b->thread);
	    printf_filtered ("%s%s ",
			     ((b->enable_state == bp_disabled
			       || b->enable_state == bp_call_disabled
			       || b->enable_state == bp_startup_disabled)
			      ? " (disabled)"
			      : b->enable_state == bp_permanent 
			      ? " (permanent)"
			      : ""),
			     (others > 1) ? "," 
			     : ((others == 1) ? " and" : ""));
	  }
      printf_filtered (_("also set at pc "));
      fputs_filtered (paddress (gdbarch, pc), gdb_stdout);
      printf_filtered (".\n");
    }
}

/* Set the default place to put a breakpoint
   for the `break' command with no arguments.  */

void
set_default_breakpoint (int valid, CORE_ADDR addr, struct symtab *symtab,
			int line)
{
  default_breakpoint_valid = valid;
  default_breakpoint_address = addr;
  default_breakpoint_symtab = symtab;
  default_breakpoint_line = line;
}

/* Return true iff it is meaningful to use the address member of
   BPT.  For some breakpoint types, the address member is irrelevant
   and it makes no sense to attempt to compare it to other addresses
   (or use it for any other purpose either).

   More specifically, each of the following breakpoint types will always
   have a zero valued address and we don't want check_duplicates() to mark
   breakpoints of any of these types to be a duplicate of an actual
   breakpoint at address zero:

      bp_watchpoint
      bp_hardware_watchpoint
      bp_read_watchpoint
      bp_access_watchpoint
      bp_catchpoint */

static int
breakpoint_address_is_meaningful (struct breakpoint *bpt)
{
  enum bptype type = bpt->type;

  return (type != bp_watchpoint
	  && type != bp_hardware_watchpoint
	  && type != bp_read_watchpoint
	  && type != bp_access_watchpoint
	  && type != bp_catchpoint);
}

/* Rescan breakpoints at the same address and section as BPT,
   marking the first one as "first" and any others as "duplicates".
   This is so that the bpt instruction is only inserted once.
   If we have a permanent breakpoint at the same place as BPT, make
   that one the official one, and the rest as duplicates.  */

static void
check_duplicates_for (CORE_ADDR address, struct obj_section *section)
{
  struct bp_location *b;
  int count = 0;
  struct bp_location *perm_bp = 0;

  ALL_BP_LOCATIONS (b)
    if (b->owner->enable_state != bp_disabled
	&& b->owner->enable_state != bp_call_disabled
	&& b->owner->enable_state != bp_startup_disabled
	&& b->enabled
	&& !b->shlib_disabled
	&& b->address == address	/* address / overlay match */
	&& (!overlay_debugging || b->section == section)
	&& breakpoint_address_is_meaningful (b->owner))
    {
      /* Have we found a permanent breakpoint?  */
      if (b->owner->enable_state == bp_permanent)
	{
	  perm_bp = b;
	  break;
	}
	
      count++;
      b->duplicate = count > 1;
    }

  /* If we found a permanent breakpoint at this address, go over the
     list again and declare all the other breakpoints there (except
     other permanent breakpoints) to be the duplicates.  */
  if (perm_bp)
    {
      perm_bp->duplicate = 0;

      /* Permanent breakpoint should always be inserted.  */
      if (! perm_bp->inserted)
	internal_error (__FILE__, __LINE__,
			_("allegedly permanent breakpoint is not "
			"actually inserted"));

      ALL_BP_LOCATIONS (b)
	if (b != perm_bp)
	  {
	    if (b->owner->enable_state != bp_permanent
		&& b->owner->enable_state != bp_disabled
		&& b->owner->enable_state != bp_call_disabled
		&& b->owner->enable_state != bp_startup_disabled
		&& b->enabled && !b->shlib_disabled		
		&& b->address == address	/* address / overlay match */
		&& (!overlay_debugging || b->section == section)
		&& breakpoint_address_is_meaningful (b->owner))
	      {
		if (b->inserted)
		  internal_error (__FILE__, __LINE__,
				  _("another breakpoint was inserted on top of "
				  "a permanent breakpoint"));

		b->duplicate = 1;
	      }
	  }
    }
}

static void
check_duplicates (struct breakpoint *bpt)
{
  struct bp_location *bl = bpt->loc;

  if (! breakpoint_address_is_meaningful (bpt))
    return;

  for (; bl; bl = bl->next)
    check_duplicates_for (bl->address, bl->section);    
}

static void
breakpoint_adjustment_warning (CORE_ADDR from_addr, CORE_ADDR to_addr,
                               int bnum, int have_bnum)
{
  char astr1[40];
  char astr2[40];

  strcpy (astr1, hex_string_custom ((unsigned long) from_addr, 8));
  strcpy (astr2, hex_string_custom ((unsigned long) to_addr, 8));
  if (have_bnum)
    warning (_("Breakpoint %d address previously adjusted from %s to %s."),
             bnum, astr1, astr2);
  else
    warning (_("Breakpoint address adjusted from %s to %s."), astr1, astr2);
}

/* Adjust a breakpoint's address to account for architectural constraints
   on breakpoint placement.  Return the adjusted address.  Note: Very
   few targets require this kind of adjustment.  For most targets,
   this function is simply the identity function.  */

static CORE_ADDR
adjust_breakpoint_address (struct gdbarch *gdbarch,
			   CORE_ADDR bpaddr, enum bptype bptype)
{
  if (!gdbarch_adjust_breakpoint_address_p (gdbarch))
    {
      /* Very few targets need any kind of breakpoint adjustment.  */
      return bpaddr;
    }
  else if (bptype == bp_watchpoint
           || bptype == bp_hardware_watchpoint
           || bptype == bp_read_watchpoint
           || bptype == bp_access_watchpoint
           || bptype == bp_catchpoint)
    {
      /* Watchpoints and the various bp_catch_* eventpoints should not
         have their addresses modified.  */
      return bpaddr;
    }
  else
    {
      CORE_ADDR adjusted_bpaddr;

      /* Some targets have architectural constraints on the placement
         of breakpoint instructions.  Obtain the adjusted address.  */
      adjusted_bpaddr = gdbarch_adjust_breakpoint_address (gdbarch, bpaddr);

      /* An adjusted breakpoint address can significantly alter
         a user's expectations.  Print a warning if an adjustment
	 is required.  */
      if (adjusted_bpaddr != bpaddr)
	breakpoint_adjustment_warning (bpaddr, adjusted_bpaddr, 0, 0);

      return adjusted_bpaddr;
    }
}

/* Allocate a struct bp_location.  */

static struct bp_location *
allocate_bp_location (struct breakpoint *bpt)
{
  struct bp_location *loc, *loc_p;

  loc = xmalloc (sizeof (struct bp_location));
  memset (loc, 0, sizeof (*loc));

  loc->owner = bpt;
  loc->cond = NULL;
  loc->shlib_disabled = 0;
  loc->enabled = 1;

  switch (bpt->type)
    {
    case bp_breakpoint:
    case bp_tracepoint:
    case bp_until:
    case bp_finish:
    case bp_longjmp:
    case bp_longjmp_resume:
    case bp_step_resume:
    case bp_watchpoint_scope:
    case bp_call_dummy:
    case bp_shlib_event:
    case bp_thread_event:
    case bp_overlay_event:
    case bp_jit_event:
    case bp_longjmp_master:
      loc->loc_type = bp_loc_software_breakpoint;
      break;
    case bp_hardware_breakpoint:
      loc->loc_type = bp_loc_hardware_breakpoint;
      break;
    case bp_hardware_watchpoint:
    case bp_read_watchpoint:
    case bp_access_watchpoint:
      loc->loc_type = bp_loc_hardware_watchpoint;
      break;
    case bp_watchpoint:
    case bp_catchpoint:
      loc->loc_type = bp_loc_other;
      break;
    default:
      internal_error (__FILE__, __LINE__, _("unknown breakpoint type"));
    }

  return loc;
}

static void free_bp_location (struct bp_location *loc)
{
  if (loc->cond)
    xfree (loc->cond);

  if (loc->function_name)
    xfree (loc->function_name);
  
  xfree (loc);
}

/* Helper to set_raw_breakpoint below.  Creates a breakpoint
   that has type BPTYPE and has no locations as yet.  */
/* This function is used in gdbtk sources and thus can not be made static.  */

static struct breakpoint *
set_raw_breakpoint_without_location (struct gdbarch *gdbarch,
				     enum bptype bptype)
{
  struct breakpoint *b, *b1;

  b = (struct breakpoint *) xmalloc (sizeof (struct breakpoint));
  memset (b, 0, sizeof (*b));

  b->type = bptype;
  b->gdbarch = gdbarch;
  b->language = current_language->la_language;
  b->input_radix = input_radix;
  b->thread = -1;
  b->enable_state = bp_enabled;
  b->next = 0;
  b->silent = 0;
  b->ignore_count = 0;
  b->commands = NULL;
  b->frame_id = null_frame_id;
  b->forked_inferior_pid = null_ptid;
  b->exec_pathname = NULL;
  b->syscalls_to_be_caught = NULL;
  b->ops = NULL;
  b->condition_not_parsed = 0;

  /* Add this breakpoint to the end of the chain
     so that a list of breakpoints will come out in order
     of increasing numbers.  */

  b1 = breakpoint_chain;
  if (b1 == 0)
    breakpoint_chain = b;
  else
    {
      while (b1->next)
	b1 = b1->next;
      b1->next = b;
    }
  return b;
}

/* Initialize loc->function_name.  */
static void
set_breakpoint_location_function (struct bp_location *loc)
{
  if (loc->owner->type == bp_breakpoint
      || loc->owner->type == bp_hardware_breakpoint
      || loc->owner->type == bp_tracepoint)
    {
      find_pc_partial_function (loc->address, &(loc->function_name), 
				NULL, NULL);
      if (loc->function_name)
	loc->function_name = xstrdup (loc->function_name);
    }
}

/* Attempt to determine architecture of location identified by SAL.  */
static struct gdbarch *
get_sal_arch (struct symtab_and_line sal)
{
  if (sal.section)
    return get_objfile_arch (sal.section->objfile);
  if (sal.symtab)
    return get_objfile_arch (sal.symtab->objfile);

  return NULL;
}

/* set_raw_breakpoint is a low level routine for allocating and
   partially initializing a breakpoint of type BPTYPE.  The newly
   created breakpoint's address, section, source file name, and line
   number are provided by SAL.  The newly created and partially
   initialized breakpoint is added to the breakpoint chain and
   is also returned as the value of this function.

   It is expected that the caller will complete the initialization of
   the newly created breakpoint struct as well as output any status
   information regarding the creation of a new breakpoint.  In
   particular, set_raw_breakpoint does NOT set the breakpoint
   number!  Care should be taken to not allow an error to occur
   prior to completing the initialization of the breakpoint.  If this
   should happen, a bogus breakpoint will be left on the chain.  */

struct breakpoint *
set_raw_breakpoint (struct gdbarch *gdbarch,
		    struct symtab_and_line sal, enum bptype bptype)
{
  struct breakpoint *b = set_raw_breakpoint_without_location (gdbarch, bptype);
  CORE_ADDR adjusted_address;
  struct gdbarch *loc_gdbarch;

  loc_gdbarch = get_sal_arch (sal);
  if (!loc_gdbarch)
    loc_gdbarch = b->gdbarch;

  /* Adjust the breakpoint's address prior to allocating a location.
     Once we call allocate_bp_location(), that mostly uninitialized
     location will be placed on the location chain.  Adjustment of the
     breakpoint may cause target_read_memory() to be called and we do
     not want its scan of the location chain to find a breakpoint and
     location that's only been partially initialized.  */
  adjusted_address = adjust_breakpoint_address (loc_gdbarch, sal.pc, b->type);

  b->loc = allocate_bp_location (b);
  b->loc->gdbarch = loc_gdbarch;
  b->loc->requested_address = sal.pc;
  b->loc->address = adjusted_address;

  if (sal.symtab == NULL)
    b->source_file = NULL;
  else
    b->source_file = xstrdup (sal.symtab->filename);
  b->loc->section = sal.section;
  b->line_number = sal.line;

  set_breakpoint_location_function (b->loc);

  breakpoints_changed ();

  return b;
}


/* Note that the breakpoint object B describes a permanent breakpoint
   instruction, hard-wired into the inferior's code.  */
void
make_breakpoint_permanent (struct breakpoint *b)
{
  struct bp_location *bl;
  b->enable_state = bp_permanent;

  /* By definition, permanent breakpoints are already present in the code. 
     Mark all locations as inserted.  For now, make_breakpoint_permanent
     is called in just one place, so it's hard to say if it's reasonable
     to have permanent breakpoint with multiple locations or not,
     but it's easy to implmement.  */
  for (bl = b->loc; bl; bl = bl->next)
    bl->inserted = 1;
}

/* Call this routine when stepping and nexting to enable a breakpoint
   if we do a longjmp() in THREAD.  When we hit that breakpoint, call
   set_longjmp_resume_breakpoint() to figure out where we are going. */

void
set_longjmp_breakpoint (int thread)
{
  struct breakpoint *b, *temp;

  /* To avoid having to rescan all objfile symbols at every step,
     we maintain a list of continually-inserted but always disabled
     longjmp "master" breakpoints.  Here, we simply create momentary
     clones of those and enable them for the requested thread.  */
  ALL_BREAKPOINTS_SAFE (b, temp)
    if (b->type == bp_longjmp_master)
      {
	struct breakpoint *clone = clone_momentary_breakpoint (b);
	clone->type = bp_longjmp;
	clone->thread = thread;
      }
}

/* Delete all longjmp breakpoints from THREAD.  */
void
delete_longjmp_breakpoint (int thread)
{
  struct breakpoint *b, *temp;

  ALL_BREAKPOINTS_SAFE (b, temp)
    if (b->type == bp_longjmp)
      {
	if (b->thread == thread)
	  delete_breakpoint (b);
      }
}

void
enable_overlay_breakpoints (void)
{
  struct breakpoint *b;

  ALL_BREAKPOINTS (b)
    if (b->type == bp_overlay_event)
    {
      b->enable_state = bp_enabled;
      update_global_location_list (1);
      overlay_events_enabled = 1;
    }
}

void
disable_overlay_breakpoints (void)
{
  struct breakpoint *b;

  ALL_BREAKPOINTS (b)
    if (b->type == bp_overlay_event)
    {
      b->enable_state = bp_disabled;
      update_global_location_list (0);
      overlay_events_enabled = 0;
    }
}

struct breakpoint *
create_thread_event_breakpoint (struct gdbarch *gdbarch, CORE_ADDR address)
{
  struct breakpoint *b;

  b = create_internal_breakpoint (gdbarch, address, bp_thread_event);
  
  b->enable_state = bp_enabled;
  /* addr_string has to be used or breakpoint_re_set will delete me.  */
  b->addr_string
    = xstrprintf ("*%s", paddress (b->loc->gdbarch, b->loc->address));

  update_global_location_list_nothrow (1);

  return b;
}

void
remove_thread_event_breakpoints (void)
{
  struct breakpoint *b, *temp;

  ALL_BREAKPOINTS_SAFE (b, temp)
    if (b->type == bp_thread_event)
      delete_breakpoint (b);
}

struct captured_parse_breakpoint_args
  {
    char **arg_p;
    struct symtabs_and_lines *sals_p;
    char ***addr_string_p;
    int *not_found_ptr;
  };

struct lang_and_radix
  {
    enum language lang;
    int radix;
  };

/* Create a breakpoint for JIT code registration and unregistration.  */

struct breakpoint *
create_jit_event_breakpoint (struct gdbarch *gdbarch, CORE_ADDR address)
{
  struct breakpoint *b;

  b = create_internal_breakpoint (gdbarch, address, bp_jit_event);
  update_global_location_list_nothrow (1);
  return b;
}

void
remove_solib_event_breakpoints (void)
{
  struct breakpoint *b, *temp;

  ALL_BREAKPOINTS_SAFE (b, temp)
    if (b->type == bp_shlib_event)
      delete_breakpoint (b);
}

struct breakpoint *
create_solib_event_breakpoint (struct gdbarch *gdbarch, CORE_ADDR address)
{
  struct breakpoint *b;

  b = create_internal_breakpoint (gdbarch, address, bp_shlib_event);
  update_global_location_list_nothrow (1);
  return b;
}

/* Disable any breakpoints that are on code in shared libraries.  Only
   apply to enabled breakpoints, disabled ones can just stay disabled.  */

void
disable_breakpoints_in_shlibs (void)
{
  struct bp_location *loc;

  ALL_BP_LOCATIONS (loc)
  {
    struct breakpoint *b = loc->owner;
    /* We apply the check to all breakpoints, including disabled
       for those with loc->duplicate set.  This is so that when breakpoint
       becomes enabled, or the duplicate is removed, gdb will try to insert
       all breakpoints.  If we don't set shlib_disabled here, we'll try
       to insert those breakpoints and fail.  */
    if (((b->type == bp_breakpoint)
	 || (b->type == bp_hardware_breakpoint)
	 || (b->type == bp_tracepoint))
	&& !loc->shlib_disabled
#ifdef PC_SOLIB
	&& PC_SOLIB (loc->address)
#else
	&& solib_name_from_address (loc->address)
#endif
	)
      {
	loc->shlib_disabled = 1;
      }
  }
}

/* Disable any breakpoints that are in in an unloaded shared library.  Only
   apply to enabled breakpoints, disabled ones can just stay disabled.  */

static void
disable_breakpoints_in_unloaded_shlib (struct so_list *solib)
{
  struct bp_location *loc;
  int disabled_shlib_breaks = 0;

  /* SunOS a.out shared libraries are always mapped, so do not
     disable breakpoints; they will only be reported as unloaded
     through clear_solib when GDB discards its shared library
     list.  See clear_solib for more information.  */
  if (exec_bfd != NULL
      && bfd_get_flavour (exec_bfd) == bfd_target_aout_flavour)
    return;

  ALL_BP_LOCATIONS (loc)
  {
    struct breakpoint *b = loc->owner;
    if ((loc->loc_type == bp_loc_hardware_breakpoint
	 || loc->loc_type == bp_loc_software_breakpoint)
	&& !loc->shlib_disabled
	&& (b->type == bp_breakpoint || b->type == bp_hardware_breakpoint)
	&& solib_contains_address_p (solib, loc->address))
      {
	loc->shlib_disabled = 1;
	/* At this point, we cannot rely on remove_breakpoint
	   succeeding so we must mark the breakpoint as not inserted
	   to prevent future errors occurring in remove_breakpoints.  */
	loc->inserted = 0;
	if (!disabled_shlib_breaks)
	  {
	    target_terminal_ours_for_output ();
	    warning (_("Temporarily disabling breakpoints for unloaded shared library \"%s\""),
		     solib->so_name);
	  }
	disabled_shlib_breaks = 1;
      }
  }
}

/* FORK & VFORK catchpoints.  */

/* Implement the "insert" breakpoint_ops method for fork catchpoints.  */

static void
insert_catch_fork (struct breakpoint *b)
{
  target_insert_fork_catchpoint (PIDGET (inferior_ptid));
}

/* Implement the "remove" breakpoint_ops method for fork catchpoints.  */

static int
remove_catch_fork (struct breakpoint *b)
{
  return target_remove_fork_catchpoint (PIDGET (inferior_ptid));
}

/* Implement the "breakpoint_hit" breakpoint_ops method for fork
   catchpoints.  */

static int
breakpoint_hit_catch_fork (struct breakpoint *b)
{
  return inferior_has_forked (inferior_ptid, &b->forked_inferior_pid);
}

/* Implement the "print_it" breakpoint_ops method for fork catchpoints.  */

static enum print_stop_action
print_it_catch_fork (struct breakpoint *b)
{
  annotate_catchpoint (b->number);
  printf_filtered (_("\nCatchpoint %d (forked process %d), "),
		   b->number, ptid_get_pid (b->forked_inferior_pid));
  return PRINT_SRC_AND_LOC;
}

/* Implement the "print_one" breakpoint_ops method for fork catchpoints.  */

static void
print_one_catch_fork (struct breakpoint *b, struct bp_location **last_loc)
{
  struct value_print_options opts;

  get_user_print_options (&opts);

  /* Field 4, the address, is omitted (which makes the columns
     not line up too nicely with the headers, but the effect
     is relatively readable).  */
  if (opts.addressprint)
    ui_out_field_skip (uiout, "addr");
  annotate_field (5);
  ui_out_text (uiout, "fork");
  if (!ptid_equal (b->forked_inferior_pid, null_ptid))
    {
      ui_out_text (uiout, ", process ");
      ui_out_field_int (uiout, "what",
                        ptid_get_pid (b->forked_inferior_pid));
      ui_out_spaces (uiout, 1);
    }
}

/* Implement the "print_mention" breakpoint_ops method for fork
   catchpoints.  */

static void
print_mention_catch_fork (struct breakpoint *b)
{
  printf_filtered (_("Catchpoint %d (fork)"), b->number);
}

/* The breakpoint_ops structure to be used in fork catchpoints.  */

static struct breakpoint_ops catch_fork_breakpoint_ops =
{
  insert_catch_fork,
  remove_catch_fork,
  breakpoint_hit_catch_fork,
  print_it_catch_fork,
  print_one_catch_fork,
  print_mention_catch_fork
};

/* Implement the "insert" breakpoint_ops method for vfork catchpoints.  */

static void
insert_catch_vfork (struct breakpoint *b)
{
  target_insert_vfork_catchpoint (PIDGET (inferior_ptid));
}

/* Implement the "remove" breakpoint_ops method for vfork catchpoints.  */

static int
remove_catch_vfork (struct breakpoint *b)
{
  return target_remove_vfork_catchpoint (PIDGET (inferior_ptid));
}

/* Implement the "breakpoint_hit" breakpoint_ops method for vfork
   catchpoints.  */

static int
breakpoint_hit_catch_vfork (struct breakpoint *b)
{
  return inferior_has_vforked (inferior_ptid, &b->forked_inferior_pid);
}

/* Implement the "print_it" breakpoint_ops method for vfork catchpoints.  */

static enum print_stop_action
print_it_catch_vfork (struct breakpoint *b)
{
  annotate_catchpoint (b->number);
  printf_filtered (_("\nCatchpoint %d (vforked process %d), "),
		   b->number, ptid_get_pid (b->forked_inferior_pid));
  return PRINT_SRC_AND_LOC;
}

/* Implement the "print_one" breakpoint_ops method for vfork catchpoints.  */

static void
print_one_catch_vfork (struct breakpoint *b, struct bp_location **last_loc)
{
  struct value_print_options opts;

  get_user_print_options (&opts);
  /* Field 4, the address, is omitted (which makes the columns
     not line up too nicely with the headers, but the effect
     is relatively readable).  */
  if (opts.addressprint)
    ui_out_field_skip (uiout, "addr");
  annotate_field (5);
  ui_out_text (uiout, "vfork");
  if (!ptid_equal (b->forked_inferior_pid, null_ptid))
    {
      ui_out_text (uiout, ", process ");
      ui_out_field_int (uiout, "what",
                        ptid_get_pid (b->forked_inferior_pid));
      ui_out_spaces (uiout, 1);
    }
}

/* Implement the "print_mention" breakpoint_ops method for vfork
   catchpoints.  */

static void
print_mention_catch_vfork (struct breakpoint *b)
{
  printf_filtered (_("Catchpoint %d (vfork)"), b->number);
}

/* The breakpoint_ops structure to be used in vfork catchpoints.  */

static struct breakpoint_ops catch_vfork_breakpoint_ops =
{
  insert_catch_vfork,
  remove_catch_vfork,
  breakpoint_hit_catch_vfork,
  print_it_catch_vfork,
  print_one_catch_vfork,
  print_mention_catch_vfork
};

/* Implement the "insert" breakpoint_ops method for syscall
   catchpoints.  */

static void
insert_catch_syscall (struct breakpoint *b)
{
  struct inferior *inf = current_inferior ();

  ++inf->total_syscalls_count;
  if (!b->syscalls_to_be_caught)
    ++inf->any_syscall_count;
  else
    {
      int i, iter;
      for (i = 0;
           VEC_iterate (int, b->syscalls_to_be_caught, i, iter);
           i++)
	{
          int elem;
	  if (iter >= VEC_length (int, inf->syscalls_counts))
	    {
              int old_size = VEC_length (int, inf->syscalls_counts);
              uintptr_t vec_addr_offset = old_size * ((uintptr_t) sizeof (int));
              uintptr_t vec_addr;
              VEC_safe_grow (int, inf->syscalls_counts, iter + 1);
              vec_addr = (uintptr_t) VEC_address (int, inf->syscalls_counts) +
		vec_addr_offset;
              memset ((void *) vec_addr, 0,
                      (iter + 1 - old_size) * sizeof (int));
	    }
          elem = VEC_index (int, inf->syscalls_counts, iter);
          VEC_replace (int, inf->syscalls_counts, iter, ++elem);
	}
    }

  target_set_syscall_catchpoint (PIDGET (inferior_ptid),
				 inf->total_syscalls_count != 0,
				 inf->any_syscall_count,
				 VEC_length (int, inf->syscalls_counts),
				 VEC_address (int, inf->syscalls_counts));
}

/* Implement the "remove" breakpoint_ops method for syscall
   catchpoints.  */

static int
remove_catch_syscall (struct breakpoint *b)
{
  struct inferior *inf = current_inferior ();

  --inf->total_syscalls_count;
  if (!b->syscalls_to_be_caught)
    --inf->any_syscall_count;
  else
    {
      int i, iter;
      for (i = 0;
           VEC_iterate (int, b->syscalls_to_be_caught, i, iter);
           i++)
	{
          int elem;
	  if (iter >= VEC_length (int, inf->syscalls_counts))
	    /* Shouldn't happen.  */
	    continue;
          elem = VEC_index (int, inf->syscalls_counts, iter);
          VEC_replace (int, inf->syscalls_counts, iter, --elem);
        }
    }

  return target_set_syscall_catchpoint (PIDGET (inferior_ptid),
					inf->total_syscalls_count != 0,
					inf->any_syscall_count,
					VEC_length (int, inf->syscalls_counts),
					VEC_address (int, inf->syscalls_counts));
}

/* Implement the "breakpoint_hit" breakpoint_ops method for syscall
   catchpoints.  */

static int
breakpoint_hit_catch_syscall (struct breakpoint *b)
{
  /* We must check if we are catching specific syscalls in this breakpoint.
     If we are, then we must guarantee that the called syscall is the same
     syscall we are catching.  */
  int syscall_number = 0;

  if (!inferior_has_called_syscall (inferior_ptid, &syscall_number))
    return 0;

  /* Now, checking if the syscall is the same.  */
  if (b->syscalls_to_be_caught)
    {
      int i, iter;
      for (i = 0;
           VEC_iterate (int, b->syscalls_to_be_caught, i, iter);
           i++)
	if (syscall_number == iter)
	  break;
      /* Not the same.  */
      if (!iter)
	return 0;
    }

  return 1;
}

/* Implement the "print_it" breakpoint_ops method for syscall
   catchpoints.  */

static enum print_stop_action
print_it_catch_syscall (struct breakpoint *b)
{
  /* These are needed because we want to know in which state a
     syscall is.  It can be in the TARGET_WAITKIND_SYSCALL_ENTRY
     or TARGET_WAITKIND_SYSCALL_RETURN, and depending on it we
     must print "called syscall" or "returned from syscall".  */
  ptid_t ptid;
  struct target_waitstatus last;
  struct syscall s;
  struct cleanup *old_chain;
  char *syscall_id;

  get_last_target_status (&ptid, &last);

  get_syscall_by_number (last.value.syscall_number, &s);

  annotate_catchpoint (b->number);

  if (s.name == NULL)
    syscall_id = xstrprintf ("%d", last.value.syscall_number);
  else
    syscall_id = xstrprintf ("'%s'", s.name);

  old_chain = make_cleanup (xfree, syscall_id);

  if (last.kind == TARGET_WAITKIND_SYSCALL_ENTRY)
    printf_filtered (_("\nCatchpoint %d (call to syscall %s), "),
                     b->number, syscall_id);
  else if (last.kind == TARGET_WAITKIND_SYSCALL_RETURN)
    printf_filtered (_("\nCatchpoint %d (returned from syscall %s), "),
                     b->number, syscall_id);

  do_cleanups (old_chain);

  return PRINT_SRC_AND_LOC;
}

/* Implement the "print_one" breakpoint_ops method for syscall
   catchpoints.  */

static void
print_one_catch_syscall (struct breakpoint *b,
                         struct bp_location **last_loc)
{
  struct value_print_options opts;

  get_user_print_options (&opts);
  /* Field 4, the address, is omitted (which makes the columns
     not line up too nicely with the headers, but the effect
     is relatively readable).  */
  if (opts.addressprint)
    ui_out_field_skip (uiout, "addr");
  annotate_field (5);

  if (b->syscalls_to_be_caught
      && VEC_length (int, b->syscalls_to_be_caught) > 1)
    ui_out_text (uiout, "syscalls \"");
  else
    ui_out_text (uiout, "syscall \"");

  if (b->syscalls_to_be_caught)
    {
      int i, iter;
      char *text = xstrprintf ("%s", "");
      for (i = 0;
           VEC_iterate (int, b->syscalls_to_be_caught, i, iter);
           i++)
        {
          char *x = text;
          struct syscall s;
          get_syscall_by_number (iter, &s);

          if (s.name != NULL)
            text = xstrprintf ("%s%s, ", text, s.name);
          else
            text = xstrprintf ("%s%d, ", text, iter);

          /* We have to xfree the last 'text' (now stored at 'x')
             because xstrprintf dinamically allocates new space for it
             on every call.  */
	  xfree (x);
        }
      /* Remove the last comma.  */
      text[strlen (text) - 2] = '\0';
      ui_out_field_string (uiout, "what", text);
    }
  else
    ui_out_field_string (uiout, "what", "<any syscall>");
  ui_out_text (uiout, "\" ");
}

/* Implement the "print_mention" breakpoint_ops method for syscall
   catchpoints.  */

static void
print_mention_catch_syscall (struct breakpoint *b)
{
  if (b->syscalls_to_be_caught)
    {
      int i, iter;

      if (VEC_length (int, b->syscalls_to_be_caught) > 1)
        printf_filtered (_("Catchpoint %d (syscalls"), b->number);
      else
        printf_filtered (_("Catchpoint %d (syscall"), b->number);

      for (i = 0;
           VEC_iterate (int, b->syscalls_to_be_caught, i, iter);
           i++)
        {
          struct syscall s;
          get_syscall_by_number (iter, &s);

          if (s.name)
            printf_filtered (" '%s' [%d]", s.name, s.number);
          else
            printf_filtered (" %d", s.number);
        }
      printf_filtered (")");
    }
  else
    printf_filtered (_("Catchpoint %d (any syscall)"),
                     b->number);
}

/* The breakpoint_ops structure to be used in syscall catchpoints.  */

static struct breakpoint_ops catch_syscall_breakpoint_ops =
{
  insert_catch_syscall,
  remove_catch_syscall,
  breakpoint_hit_catch_syscall,
  print_it_catch_syscall,
  print_one_catch_syscall,
  print_mention_catch_syscall
};

/* Returns non-zero if 'b' is a syscall catchpoint.  */

static int
syscall_catchpoint_p (struct breakpoint *b)
{
  return (b->ops == &catch_syscall_breakpoint_ops);
}

/* Create a new breakpoint of the bp_catchpoint kind and return it,
   but does NOT mention it nor update the global location list.
   This is useful if you need to fill more fields in the
   struct breakpoint before calling mention.
 
   If TEMPFLAG is non-zero, then make the breakpoint temporary.
   If COND_STRING is not NULL, then store it in the breakpoint.
   OPS, if not NULL, is the breakpoint_ops structure associated
   to the catchpoint.  */

static struct breakpoint *
create_catchpoint_without_mention (struct gdbarch *gdbarch, int tempflag,
				   char *cond_string,
				   struct breakpoint_ops *ops)
{
  struct symtab_and_line sal;
  struct breakpoint *b;

  init_sal (&sal);

  b = set_raw_breakpoint (gdbarch, sal, bp_catchpoint);
  set_breakpoint_count (breakpoint_count + 1);
  b->number = breakpoint_count;

  b->cond_string = (cond_string == NULL) ? NULL : xstrdup (cond_string);
  b->thread = -1;
  b->addr_string = NULL;
  b->enable_state = bp_enabled;
  b->disposition = tempflag ? disp_del : disp_donttouch;
  b->ops = ops;

  return b;
}

/* Create a new breakpoint of the bp_catchpoint kind and return it.
 
   If TEMPFLAG is non-zero, then make the breakpoint temporary.
   If COND_STRING is not NULL, then store it in the breakpoint.
   OPS, if not NULL, is the breakpoint_ops structure associated
   to the catchpoint.  */

static struct breakpoint *
create_catchpoint (struct gdbarch *gdbarch, int tempflag,
		   char *cond_string, struct breakpoint_ops *ops)
{
  struct breakpoint *b =
    create_catchpoint_without_mention (gdbarch, tempflag, cond_string, ops);

  mention (b);
  update_global_location_list (1);

  return b;
}

static void
create_fork_vfork_event_catchpoint (struct gdbarch *gdbarch,
				    int tempflag, char *cond_string,
                                    struct breakpoint_ops *ops)
{
  struct breakpoint *b
    = create_catchpoint (gdbarch, tempflag, cond_string, ops);

  /* FIXME: We should put this information in a breakpoint private data
     area.  */
  b->forked_inferior_pid = null_ptid;
}

/* Exec catchpoints.  */

static void
insert_catch_exec (struct breakpoint *b)
{
  target_insert_exec_catchpoint (PIDGET (inferior_ptid));
}

static int
remove_catch_exec (struct breakpoint *b)
{
  return target_remove_exec_catchpoint (PIDGET (inferior_ptid));
}

static int
breakpoint_hit_catch_exec (struct breakpoint *b)
{
  return inferior_has_execd (inferior_ptid, &b->exec_pathname);
}

static enum print_stop_action
print_it_catch_exec (struct breakpoint *b)
{
  annotate_catchpoint (b->number);
  printf_filtered (_("\nCatchpoint %d (exec'd %s), "), b->number,
		   b->exec_pathname);
  return PRINT_SRC_AND_LOC;
}

static void
print_one_catch_exec (struct breakpoint *b, struct bp_location **last_loc)
{
  struct value_print_options opts;

  get_user_print_options (&opts);

  /* Field 4, the address, is omitted (which makes the columns
     not line up too nicely with the headers, but the effect
     is relatively readable).  */
  if (opts.addressprint)
    ui_out_field_skip (uiout, "addr");
  annotate_field (5);
  ui_out_text (uiout, "exec");
  if (b->exec_pathname != NULL)
    {
      ui_out_text (uiout, ", program \"");
      ui_out_field_string (uiout, "what", b->exec_pathname);
      ui_out_text (uiout, "\" ");
    }
}

static void
print_mention_catch_exec (struct breakpoint *b)
{
  printf_filtered (_("Catchpoint %d (exec)"), b->number);
}

static struct breakpoint_ops catch_exec_breakpoint_ops =
{
  insert_catch_exec,
  remove_catch_exec,
  breakpoint_hit_catch_exec,
  print_it_catch_exec,
  print_one_catch_exec,
  print_mention_catch_exec
};

static void
create_syscall_event_catchpoint (int tempflag, VEC(int) *filter,
                                 struct breakpoint_ops *ops)
{
  struct gdbarch *gdbarch = get_current_arch ();
  struct breakpoint *b =
    create_catchpoint_without_mention (gdbarch, tempflag, NULL, ops);

  b->syscalls_to_be_caught = filter;

  /* Now, we have to mention the breakpoint and update the global
     location list.  */
  mention (b);
  update_global_location_list (1);
}

static int
hw_breakpoint_used_count (void)
{
  struct breakpoint *b;
  int i = 0;

  ALL_BREAKPOINTS (b)
  {
    if (b->type == bp_hardware_breakpoint && breakpoint_enabled (b))
      i++;
  }

  return i;
}

static int
hw_watchpoint_used_count (enum bptype type, int *other_type_used)
{
  struct breakpoint *b;
  int i = 0;

  *other_type_used = 0;
  ALL_BREAKPOINTS (b)
  {
    if (breakpoint_enabled (b))
      {
	if (b->type == type)
	  i++;
	else if ((b->type == bp_hardware_watchpoint
		  || b->type == bp_read_watchpoint
		  || b->type == bp_access_watchpoint))
	  *other_type_used = 1;
      }
  }
  return i;
}

void
disable_watchpoints_before_interactive_call_start (void)
{
  struct breakpoint *b;

  ALL_BREAKPOINTS (b)
  {
    if (((b->type == bp_watchpoint)
	 || (b->type == bp_hardware_watchpoint)
	 || (b->type == bp_read_watchpoint)
	 || (b->type == bp_access_watchpoint))
	&& breakpoint_enabled (b))
      {
	b->enable_state = bp_call_disabled;
	update_global_location_list (0);
      }
  }
}

void
enable_watchpoints_after_interactive_call_stop (void)
{
  struct breakpoint *b;

  ALL_BREAKPOINTS (b)
  {
    if (((b->type == bp_watchpoint)
	 || (b->type == bp_hardware_watchpoint)
	 || (b->type == bp_read_watchpoint)
	 || (b->type == bp_access_watchpoint))
	&& (b->enable_state == bp_call_disabled))
      {
	b->enable_state = bp_enabled;
	update_global_location_list (1);
      }
  }
}

void
disable_breakpoints_before_startup (void)
{
  struct breakpoint *b;
  int found = 0;

  ALL_BREAKPOINTS (b)
    {
      if ((b->type == bp_breakpoint
	   || b->type == bp_hardware_breakpoint)
	  && breakpoint_enabled (b))
	{
	  b->enable_state = bp_startup_disabled;
	  found = 1;
	}
    }

  if (found)
    update_global_location_list (0);

  executing_startup = 1;
}

void
enable_breakpoints_after_startup (void)
{
  struct breakpoint *b;
  int found = 0;

  executing_startup = 0;

  ALL_BREAKPOINTS (b)
    {
      if ((b->type == bp_breakpoint
	   || b->type == bp_hardware_breakpoint)
	  && b->enable_state == bp_startup_disabled)
	{
	  b->enable_state = bp_enabled;
	  found = 1;
	}
    }

  if (found)
    breakpoint_re_set ();
}


/* Set a breakpoint that will evaporate an end of command
   at address specified by SAL.
   Restrict it to frame FRAME if FRAME is nonzero.  */

struct breakpoint *
set_momentary_breakpoint (struct gdbarch *gdbarch, struct symtab_and_line sal,
			  struct frame_id frame_id, enum bptype type)
{
  struct breakpoint *b;

  /* If FRAME_ID is valid, it should be a real frame, not an inlined
     one.  */
  gdb_assert (!frame_id_inlined_p (frame_id));

  b = set_raw_breakpoint (gdbarch, sal, type);
  b->enable_state = bp_enabled;
  b->disposition = disp_donttouch;
  b->frame_id = frame_id;

  /* If we're debugging a multi-threaded program, then we
     want momentary breakpoints to be active in only a 
     single thread of control.  */
  if (in_thread_list (inferior_ptid))
    b->thread = pid_to_thread_id (inferior_ptid);

  update_global_location_list_nothrow (1);

  return b;
}

/* Make a deep copy of momentary breakpoint ORIG.  Returns NULL if
   ORIG is NULL.  */

struct breakpoint *
clone_momentary_breakpoint (struct breakpoint *orig)
{
  struct breakpoint *copy;

  /* If there's nothing to clone, then return nothing.  */
  if (orig == NULL)
    return NULL;

  copy = set_raw_breakpoint_without_location (orig->gdbarch, orig->type);
  copy->loc = allocate_bp_location (copy);
  set_breakpoint_location_function (copy->loc);

  copy->loc->gdbarch = orig->loc->gdbarch;
  copy->loc->requested_address = orig->loc->requested_address;
  copy->loc->address = orig->loc->address;
  copy->loc->section = orig->loc->section;

  if (orig->source_file == NULL)
    copy->source_file = NULL;
  else
    copy->source_file = xstrdup (orig->source_file);

  copy->line_number = orig->line_number;
  copy->frame_id = orig->frame_id;
  copy->thread = orig->thread;

  copy->enable_state = bp_enabled;
  copy->disposition = disp_donttouch;
  copy->number = internal_breakpoint_number--;

  update_global_location_list_nothrow (0);
  return copy;
}

struct breakpoint *
set_momentary_breakpoint_at_pc (struct gdbarch *gdbarch, CORE_ADDR pc,
				enum bptype type)
{
  struct symtab_and_line sal;

  sal = find_pc_line (pc, 0);
  sal.pc = pc;
  sal.section = find_pc_overlay (pc);
  sal.explicit_pc = 1;

  return set_momentary_breakpoint (gdbarch, sal, null_frame_id, type);
}


/* Tell the user we have just set a breakpoint B.  */

static void
mention (struct breakpoint *b)
{
  int say_where = 0;
  struct cleanup *ui_out_chain;
  struct value_print_options opts;

  get_user_print_options (&opts);

  /* FIXME: This is misplaced; mention() is called by things (like
     hitting a watchpoint) other than breakpoint creation.  It should
     be possible to clean this up and at the same time replace the
     random calls to breakpoint_changed with this hook.  */
  observer_notify_breakpoint_created (b->number);

  if (b->ops != NULL && b->ops->print_mention != NULL)
    b->ops->print_mention (b);
  else
    switch (b->type)
      {
      case bp_none:
	printf_filtered (_("(apparently deleted?) Eventpoint %d: "), b->number);
	break;
      case bp_watchpoint:
	ui_out_text (uiout, "Watchpoint ");
	ui_out_chain = make_cleanup_ui_out_tuple_begin_end (uiout, "wpt");
	ui_out_field_int (uiout, "number", b->number);
	ui_out_text (uiout, ": ");
	ui_out_field_string (uiout, "exp", b->exp_string);
	do_cleanups (ui_out_chain);
	break;
      case bp_hardware_watchpoint:
	ui_out_text (uiout, "Hardware watchpoint ");
	ui_out_chain = make_cleanup_ui_out_tuple_begin_end (uiout, "wpt");
	ui_out_field_int (uiout, "number", b->number);
	ui_out_text (uiout, ": ");
	ui_out_field_string (uiout, "exp", b->exp_string);
	do_cleanups (ui_out_chain);
	break;
      case bp_read_watchpoint:
	ui_out_text (uiout, "Hardware read watchpoint ");
	ui_out_chain = make_cleanup_ui_out_tuple_begin_end (uiout, "hw-rwpt");
	ui_out_field_int (uiout, "number", b->number);
	ui_out_text (uiout, ": ");
	ui_out_field_string (uiout, "exp", b->exp_string);
	do_cleanups (ui_out_chain);
	break;
      case bp_access_watchpoint:
	ui_out_text (uiout, "Hardware access (read/write) watchpoint ");
	ui_out_chain = make_cleanup_ui_out_tuple_begin_end (uiout, "hw-awpt");
	ui_out_field_int (uiout, "number", b->number);
	ui_out_text (uiout, ": ");
	ui_out_field_string (uiout, "exp", b->exp_string);
	do_cleanups (ui_out_chain);
	break;
      case bp_breakpoint:
	if (ui_out_is_mi_like_p (uiout))
	  {
	    say_where = 0;
	    break;
	  }
	if (b->disposition == disp_del)
	  printf_filtered (_("Temporary breakpoint"));
	else
	  printf_filtered (_("Breakpoint"));
	printf_filtered (_(" %d"), b->number);
	say_where = 1;
	break;
      case bp_hardware_breakpoint:
	if (ui_out_is_mi_like_p (uiout))
	  {
	    say_where = 0;
	    break;
	  }
	printf_filtered (_("Hardware assisted breakpoint %d"), b->number);
	say_where = 1;
	break;
      case bp_tracepoint:
	if (ui_out_is_mi_like_p (uiout))
	  {
	    say_where = 0;
	    break;
	  }
	printf_filtered (_("Tracepoint"));
	printf_filtered (_(" %d"), b->number);
	say_where = 1;
	break;

      case bp_until:
      case bp_finish:
      case bp_longjmp:
      case bp_longjmp_resume:
      case bp_step_resume:
      case bp_call_dummy:
      case bp_watchpoint_scope:
      case bp_shlib_event:
      case bp_thread_event:
      case bp_overlay_event:
      case bp_jit_event:
      case bp_longjmp_master:
	break;
      }

  if (say_where)
    {
      /* i18n: cagney/2005-02-11: Below needs to be merged into a
	 single string.  */
      if (b->loc == NULL)
	{
	  printf_filtered (_(" (%s) pending."), b->addr_string);
	}
      else
	{
	  if (opts.addressprint || b->source_file == NULL)
	    {
	      printf_filtered (" at ");
	      fputs_filtered (paddress (b->loc->gdbarch, b->loc->address),
			      gdb_stdout);
	    }
	  if (b->source_file)
	    printf_filtered (": file %s, line %d.",
			     b->source_file, b->line_number);
	  
	  if (b->loc->next)
	    {
	      struct bp_location *loc = b->loc;
	      int n = 0;
	      for (; loc; loc = loc->next)
		++n;
	      printf_filtered (" (%d locations)", n);		
	    }

	}
    }
  if (ui_out_is_mi_like_p (uiout))
    return;
  printf_filtered ("\n");
}


static struct bp_location *
add_location_to_breakpoint (struct breakpoint *b,
			    const struct symtab_and_line *sal)
{
  struct bp_location *loc, **tmp;

  loc = allocate_bp_location (b);
  for (tmp = &(b->loc); *tmp != NULL; tmp = &((*tmp)->next))
    ;
  *tmp = loc;
  loc->gdbarch = get_sal_arch (*sal);
  if (!loc->gdbarch)
    loc->gdbarch = b->gdbarch;
  loc->requested_address = sal->pc;
  loc->address = adjust_breakpoint_address (loc->gdbarch,
					    loc->requested_address, b->type);
  loc->section = sal->section;

  set_breakpoint_location_function (loc);
  return loc;
}


/* Return 1 if LOC is pointing to a permanent breakpoint, 
   return 0 otherwise.  */

static int
bp_loc_is_permanent (struct bp_location *loc)
{
  int len;
  CORE_ADDR addr;
  const gdb_byte *brk;
  gdb_byte *target_mem;
  struct cleanup *cleanup;
  int retval = 0;

  gdb_assert (loc != NULL);

  addr = loc->address;
  brk = gdbarch_breakpoint_from_pc (loc->gdbarch, &addr, &len);

  /* Software breakpoints unsupported?  */
  if (brk == NULL)
    return 0;

  target_mem = alloca (len);

  /* Enable the automatic memory restoration from breakpoints while
     we read the memory.  Otherwise we could say about our temporary
     breakpoints they are permanent.  */
  cleanup = make_show_memory_breakpoints_cleanup (0);

  if (target_read_memory (loc->address, target_mem, len) == 0
      && memcmp (target_mem, brk, len) == 0)
    retval = 1;

  do_cleanups (cleanup);

  return retval;
}



/* Create a breakpoint with SAL as location.  Use ADDR_STRING
   as textual description of the location, and COND_STRING
   as condition expression.  */

static void
create_breakpoint (struct gdbarch *gdbarch,
		   struct symtabs_and_lines sals, char *addr_string,
		   char *cond_string,
		   enum bptype type, enum bpdisp disposition,
		   int thread, int task, int ignore_count, 
		   struct breakpoint_ops *ops, int from_tty, int enabled)
{
  struct breakpoint *b = NULL;
  int i;

  if (type == bp_hardware_breakpoint)
    {
      int i = hw_breakpoint_used_count ();
      int target_resources_ok = 
	target_can_use_hardware_watchpoint (bp_hardware_breakpoint, 
					    i + 1, 0);
      if (target_resources_ok == 0)
	error (_("No hardware breakpoint support in the target."));
      else if (target_resources_ok < 0)
	error (_("Hardware breakpoints used exceeds limit."));
    }

  for (i = 0; i < sals.nelts; ++i)
    {
      struct symtab_and_line sal = sals.sals[i];
      struct bp_location *loc;

      if (from_tty)
	{
	  struct gdbarch *loc_gdbarch = get_sal_arch (sal);
	  if (!loc_gdbarch)
	    loc_gdbarch = gdbarch;

	  describe_other_breakpoints (loc_gdbarch,
				      sal.pc, sal.section, thread);
	}

      if (i == 0)
	{
	  b = set_raw_breakpoint (gdbarch, sal, type);
	  set_breakpoint_count (breakpoint_count + 1);
	  b->number = breakpoint_count;
	  b->thread = thread;
	  b->task = task;
  
	  b->cond_string = cond_string;
	  b->ignore_count = ignore_count;
	  b->enable_state = enabled ? bp_enabled : bp_disabled;
	  b->disposition = disposition;

	  if (enabled && executing_startup
	      && (b->type == bp_breakpoint
		  || b->type == bp_hardware_breakpoint))
	    b->enable_state = bp_startup_disabled;

	  loc = b->loc;
	}
      else
	{
	  loc = add_location_to_breakpoint (b, &sal);
	}

      if (bp_loc_is_permanent (loc))
	make_breakpoint_permanent (b);

      if (b->cond_string)
	{
	  char *arg = b->cond_string;
	  loc->cond = parse_exp_1 (&arg, block_for_pc (loc->address), 0);
	  if (*arg)
              error (_("Garbage %s follows condition"), arg);
	}
    }   

  if (addr_string)
    b->addr_string = addr_string;
  else
    /* addr_string has to be used or breakpoint_re_set will delete
       me.  */
    b->addr_string
      = xstrprintf ("*%s", paddress (b->loc->gdbarch, b->loc->address));

  b->ops = ops;
  mention (b);
}

/* Remove element at INDEX_TO_REMOVE from SAL, shifting other
   elements to fill the void space.  */
static void
remove_sal (struct symtabs_and_lines *sal, int index_to_remove)
{
  int i = index_to_remove+1;
  int last_index = sal->nelts-1;

  for (;i <= last_index; ++i)
    sal->sals[i-1] = sal->sals[i];

  --(sal->nelts);
}

/* If appropriate, obtains all sals that correspond
   to the same file and line as SAL.  This is done
   only if SAL does not have explicit PC and has
   line and file information.  If we got just a single
   expanded sal, return the original.

   Otherwise, if SAL.explicit_line is not set, filter out 
   all sals for which the name of enclosing function 
   is different from SAL. This makes sure that if we have
   breakpoint originally set in template instantiation, say
   foo<int>(), we won't expand SAL to locations at the same
   line in all existing instantiations of 'foo'.

*/
static struct symtabs_and_lines
expand_line_sal_maybe (struct symtab_and_line sal)
{
  struct symtabs_and_lines expanded;
  CORE_ADDR original_pc = sal.pc;
  char *original_function = NULL;
  int found;
  int i;

  /* If we have explicit pc, don't expand.
     If we have no line number, we can't expand.  */
  if (sal.explicit_pc || sal.line == 0 || sal.symtab == NULL)
    {
      expanded.nelts = 1;
      expanded.sals = xmalloc (sizeof (struct symtab_and_line));
      expanded.sals[0] = sal;
      return expanded;
    }

  sal.pc = 0;
  find_pc_partial_function (original_pc, &original_function, NULL, NULL);
  
  expanded = expand_line_sal (sal);
  if (expanded.nelts == 1)
    {
      /* We had one sal, we got one sal.  Without futher
	 processing, just return the original sal.  */
      xfree (expanded.sals);
      expanded.nelts = 1;
      expanded.sals = xmalloc (sizeof (struct symtab_and_line));
      sal.pc = original_pc;
      expanded.sals[0] = sal;
      return expanded;      
    }

  if (!sal.explicit_line)
    {
      CORE_ADDR func_addr, func_end;
      for (i = 0; i < expanded.nelts; ++i)
	{
	  CORE_ADDR pc = expanded.sals[i].pc;
	  char *this_function;
	  if (find_pc_partial_function (pc, &this_function, 
					&func_addr, &func_end))
	    {
	      if (this_function
		  && strcmp (this_function, original_function) != 0)
		{
		  remove_sal (&expanded, i);
		  --i;
		}
	      else if (func_addr == pc)	    
		{	     
		  /* We're at beginning of a function, and should
		     skip prologue.  */
		  struct symbol *sym = find_pc_function (pc);
		  if (sym)
		    expanded.sals[i] = find_function_start_sal (sym, 1);
		  else
		    {
		      /* Since find_pc_partial_function returned true,
			 we should really always find the section here.  */
		      struct obj_section *section = find_pc_section (pc);
		      if (section)
			{
			  struct gdbarch *gdbarch
			    = get_objfile_arch (section->objfile);
			  expanded.sals[i].pc
			    = gdbarch_skip_prologue (gdbarch, pc);
			}
		    }
		}
	    }
	}
    }
  else
    {
      for (i = 0; i < expanded.nelts; ++i)
	{
	  /* If this SAL corresponds to a breakpoint inserted using a
	     line number, then skip the function prologue if necessary.  */
	  skip_prologue_sal (&expanded.sals[i]);
	}
    }

  
  if (expanded.nelts <= 1)
    {
      /* This is un ugly workaround. If we get zero
       expanded sals then something is really wrong.
      Fix that by returnign the original sal. */
      xfree (expanded.sals);
      expanded.nelts = 1;
      expanded.sals = xmalloc (sizeof (struct symtab_and_line));
      sal.pc = original_pc;
      expanded.sals[0] = sal;
      return expanded;      
    }

  if (original_pc)
    {
      found = 0;
      for (i = 0; i < expanded.nelts; ++i)
	if (expanded.sals[i].pc == original_pc)
	  {
	    found = 1;
	    break;
	  }
      gdb_assert (found);
    }

  return expanded;
}

/* Add SALS.nelts breakpoints to the breakpoint table.  For each
   SALS.sal[i] breakpoint, include the corresponding ADDR_STRING[i]
   value.  COND_STRING, if not NULL, specified the condition to be
   used for all breakpoints.  Essentially the only case where
   SALS.nelts is not 1 is when we set a breakpoint on an overloaded
   function.  In that case, it's still not possible to specify
   separate conditions for different overloaded functions, so
   we take just a single condition string.
   
   NOTE: If the function succeeds, the caller is expected to cleanup
   the arrays ADDR_STRING, COND_STRING, and SALS (but not the
   array contents).  If the function fails (error() is called), the
   caller is expected to cleanups both the ADDR_STRING, COND_STRING,
   COND and SALS arrays and each of those arrays contents. */

static void
create_breakpoints (struct gdbarch *gdbarch,
		    struct symtabs_and_lines sals, char **addr_string,
		    char *cond_string,
		    enum bptype type, enum bpdisp disposition,
		    int thread, int task, int ignore_count, 
		    struct breakpoint_ops *ops, int from_tty,
		    int enabled)
{
  int i;
  for (i = 0; i < sals.nelts; ++i)
    {
      struct symtabs_and_lines expanded = 
	expand_line_sal_maybe (sals.sals[i]);

      create_breakpoint (gdbarch, expanded, addr_string[i],
			 cond_string, type, disposition,
			 thread, task, ignore_count, ops, from_tty, enabled);
    }
}

/* Parse ARG which is assumed to be a SAL specification possibly
   followed by conditionals.  On return, SALS contains an array of SAL
   addresses found. ADDR_STRING contains a vector of (canonical)
   address strings. ARG points to the end of the SAL. */

static void
parse_breakpoint_sals (char **address,
		       struct symtabs_and_lines *sals,
		       char ***addr_string,
		       int *not_found_ptr)
{
  char *addr_start = *address;
  *addr_string = NULL;
  /* If no arg given, or if first arg is 'if ', use the default
     breakpoint. */
  if ((*address) == NULL
      || (strncmp ((*address), "if", 2) == 0 && isspace ((*address)[2])))
    {
      if (default_breakpoint_valid)
	{
	  struct symtab_and_line sal;
	  init_sal (&sal);		/* initialize to zeroes */
	  sals->sals = (struct symtab_and_line *)
	    xmalloc (sizeof (struct symtab_and_line));
	  sal.pc = default_breakpoint_address;
	  sal.line = default_breakpoint_line;
	  sal.symtab = default_breakpoint_symtab;
	  sal.section = find_pc_overlay (sal.pc);

	  /* "break" without arguments is equivalent to "break *PC" where PC is
	     the default_breakpoint_address.  So make sure to set
	     sal.explicit_pc to prevent GDB from trying to expand the list of
	     sals to include all other instances with the same symtab and line.
	   */
	  sal.explicit_pc = 1;

	  sals->sals[0] = sal;
	  sals->nelts = 1;
	}
      else
	error (_("No default breakpoint address now."));
    }
  else
    {
      /* Force almost all breakpoints to be in terms of the
         current_source_symtab (which is decode_line_1's default).  This
         should produce the results we want almost all of the time while
         leaving default_breakpoint_* alone.  
         ObjC: However, don't match an Objective-C method name which
         may have a '+' or '-' succeeded by a '[' */
	 
      struct symtab_and_line cursal = get_current_source_symtab_and_line ();
			
      if (default_breakpoint_valid
	  && (!cursal.symtab
 	      || ((strchr ("+-", (*address)[0]) != NULL)
 		  && ((*address)[1] != '['))))
	*sals = decode_line_1 (address, 1, default_breakpoint_symtab,
			       default_breakpoint_line, addr_string, 
			       not_found_ptr);
      else
	*sals = decode_line_1 (address, 1, (struct symtab *) NULL, 0,
		               addr_string, not_found_ptr);
    }
  /* For any SAL that didn't have a canonical string, fill one in. */
  if (sals->nelts > 0 && *addr_string == NULL)
    *addr_string = xcalloc (sals->nelts, sizeof (char **));
  if (addr_start != (*address))
    {
      int i;
      for (i = 0; i < sals->nelts; i++)
	{
	  /* Add the string if not present. */
	  if ((*addr_string)[i] == NULL)
	    (*addr_string)[i] = savestring (addr_start, (*address) - addr_start);
	}
    }
}


/* Convert each SAL into a real PC.  Verify that the PC can be
   inserted as a breakpoint.  If it can't throw an error. */

static void
breakpoint_sals_to_pc (struct symtabs_and_lines *sals,
		       char *address)
{    
  int i;
  for (i = 0; i < sals->nelts; i++)
    resolve_sal_pc (&sals->sals[i]);
}

static void
do_captured_parse_breakpoint (struct ui_out *ui, void *data)
{
  struct captured_parse_breakpoint_args *args = data;
  
  parse_breakpoint_sals (args->arg_p, args->sals_p, args->addr_string_p, 
		         args->not_found_ptr);
}

/* Given TOK, a string specification of condition and thread, as
   accepted by the 'break' command, extract the condition
   string and thread number and set *COND_STRING and *THREAD.
   PC identifies the context at which the condition should be parsed.  
   If no condition is found, *COND_STRING is set to NULL.
   If no thread is found, *THREAD is set to -1.  */
static void 
find_condition_and_thread (char *tok, CORE_ADDR pc, 
			   char **cond_string, int *thread, int *task)
{
  *cond_string = NULL;
  *thread = -1;
  while (tok && *tok)
    {
      char *end_tok;
      int toklen;
      char *cond_start = NULL;
      char *cond_end = NULL;
      while (*tok == ' ' || *tok == '\t')
	tok++;
      
      end_tok = tok;
      
      while (*end_tok != ' ' && *end_tok != '\t' && *end_tok != '\000')
	end_tok++;
      
      toklen = end_tok - tok;
      
      if (toklen >= 1 && strncmp (tok, "if", toklen) == 0)
	{
	  struct expression *expr;

	  tok = cond_start = end_tok + 1;
	  expr = parse_exp_1 (&tok, block_for_pc (pc), 0);
	  xfree (expr);
	  cond_end = tok;
	  *cond_string = savestring (cond_start, 
				     cond_end - cond_start);
	}
      else if (toklen >= 1 && strncmp (tok, "thread", toklen) == 0)
	{
	  char *tmptok;
	  
	  tok = end_tok + 1;
	  tmptok = tok;
	  *thread = strtol (tok, &tok, 0);
	  if (tok == tmptok)
	    error (_("Junk after thread keyword."));
	  if (!valid_thread_id (*thread))
	    error (_("Unknown thread %d."), *thread);
	}
      else if (toklen >= 1 && strncmp (tok, "task", toklen) == 0)
	{
	  char *tmptok;

	  tok = end_tok + 1;
	  tmptok = tok;
	  *task = strtol (tok, &tok, 0);
	  if (tok == tmptok)
	    error (_("Junk after task keyword."));
	  if (!valid_task_id (*task))
	    error (_("Unknown task %d\n"), *task);
	}
      else
	error (_("Junk at end of arguments."));
    }
}

/* Set a breakpoint.  This function is shared between
   CLI and MI functions for setting a breakpoint.
   This function has two major modes of operations,
   selected by the PARSE_CONDITION_AND_THREAD parameter.
   If non-zero, the function will parse arg, extracting
   breakpoint location, address and thread. Otherwise,
   ARG is just the location of breakpoint, with condition
   and thread specified by the COND_STRING and THREAD
   parameters.  */

static void
break_command_really (struct gdbarch *gdbarch,
		      char *arg, char *cond_string, int thread,
		      int parse_condition_and_thread,
		      int tempflag, int hardwareflag, int traceflag,
		      int ignore_count,
		      enum auto_boolean pending_break_support,
		      struct breakpoint_ops *ops,
		      int from_tty,
		      int enabled)
{
  struct gdb_exception e;
  struct symtabs_and_lines sals;
  struct symtab_and_line pending_sal;
  char *copy_arg;
  char *err_msg;
  char *addr_start = arg;
  char **addr_string;
  struct cleanup *old_chain;
  struct cleanup *bkpt_chain = NULL;
  struct captured_parse_breakpoint_args parse_args;
  int i;
  int pending = 0;
  int not_found = 0;
  enum bptype type_wanted;
  int task = 0;

  sals.sals = NULL;
  sals.nelts = 0;
  addr_string = NULL;

  parse_args.arg_p = &arg;
  parse_args.sals_p = &sals;
  parse_args.addr_string_p = &addr_string;
  parse_args.not_found_ptr = &not_found;

  e = catch_exception (uiout, do_captured_parse_breakpoint, 
		       &parse_args, RETURN_MASK_ALL);

  /* If caller is interested in rc value from parse, set value.  */
  switch (e.reason)
    {
    case RETURN_QUIT:
      throw_exception (e);
    case RETURN_ERROR:
      switch (e.error)
	{
	case NOT_FOUND_ERROR:

	  /* If pending breakpoint support is turned off, throw
	     error.  */

	  if (pending_break_support == AUTO_BOOLEAN_FALSE)
	    throw_exception (e);

	  exception_print (gdb_stderr, e);

          /* If pending breakpoint support is auto query and the user
	     selects no, then simply return the error code.  */
	  if (pending_break_support == AUTO_BOOLEAN_AUTO
	      && !nquery ("Make breakpoint pending on future shared library load? "))
	    return;

	  /* At this point, either the user was queried about setting
	     a pending breakpoint and selected yes, or pending
	     breakpoint behavior is on and thus a pending breakpoint
	     is defaulted on behalf of the user.  */
	  copy_arg = xstrdup (addr_start);
	  addr_string = &copy_arg;
	  sals.nelts = 1;
	  sals.sals = &pending_sal;
	  pending_sal.pc = 0;
	  pending = 1;
	  break;
	default:
	  throw_exception (e);
	}
    default:
      if (!sals.nelts)
	return;
    }

  /* Create a chain of things that always need to be cleaned up. */
  old_chain = make_cleanup (null_cleanup, 0);

  if (!pending)
    {
      /* Make sure that all storage allocated to SALS gets freed.  */
      make_cleanup (xfree, sals.sals);
      
      /* Cleanup the addr_string array but not its contents. */
      make_cleanup (xfree, addr_string);
    }

  /* ----------------------------- SNIP -----------------------------
     Anything added to the cleanup chain beyond this point is assumed
     to be part of a breakpoint.  If the breakpoint create succeeds
     then the memory is not reclaimed.  */
  bkpt_chain = make_cleanup (null_cleanup, 0);

  /* Mark the contents of the addr_string for cleanup.  These go on
     the bkpt_chain and only occur if the breakpoint create fails.  */
  for (i = 0; i < sals.nelts; i++)
    {
      if (addr_string[i] != NULL)
	make_cleanup (xfree, addr_string[i]);
    }

  /* Resolve all line numbers to PC's and verify that the addresses
     are ok for the target.  */
  if (!pending)
    breakpoint_sals_to_pc (&sals, addr_start);

  type_wanted = (traceflag
		 ? bp_tracepoint
		 : (hardwareflag ? bp_hardware_breakpoint : bp_breakpoint));

  /* Verify that condition can be parsed, before setting any
     breakpoints.  Allocate a separate condition expression for each
     breakpoint. */
  if (!pending)
    {
      if (parse_condition_and_thread)
        {
            /* Here we only parse 'arg' to separate condition
               from thread number, so parsing in context of first
               sal is OK.  When setting the breakpoint we'll 
               re-parse it in context of each sal.  */
            cond_string = NULL;
            thread = -1;
            find_condition_and_thread (arg, sals.sals[0].pc, &cond_string,
                                       &thread, &task);
            if (cond_string)
                make_cleanup (xfree, cond_string);
        }
      else
        {
            /* Create a private copy of condition string.  */
            if (cond_string)
            {
                cond_string = xstrdup (cond_string);
                make_cleanup (xfree, cond_string);
            }
        }
      create_breakpoints (gdbarch, sals, addr_string, cond_string, type_wanted,
			  tempflag ? disp_del : disp_donttouch,
			  thread, task, ignore_count, ops, from_tty, enabled);
    }
  else
    {
      struct symtab_and_line sal = {0};
      struct breakpoint *b;

      make_cleanup (xfree, copy_arg);

      b = set_raw_breakpoint_without_location (gdbarch, type_wanted);
      set_breakpoint_count (breakpoint_count + 1);
      b->number = breakpoint_count;
      b->thread = -1;
      b->addr_string = addr_string[0];
      b->cond_string = NULL;
      b->ignore_count = ignore_count;
      b->disposition = tempflag ? disp_del : disp_donttouch;
      b->condition_not_parsed = 1;
      b->ops = ops;
      b->enable_state = enabled ? bp_enabled : bp_disabled;

      if (enabled && executing_startup
	  && (b->type == bp_breakpoint
	      || b->type == bp_hardware_breakpoint))
	b->enable_state = bp_startup_disabled;

      mention (b);
    }
  
  if (sals.nelts > 1)
    warning (_("Multiple breakpoints were set.\n"
	       "Use the \"delete\" command to delete unwanted breakpoints."));
  /* That's it.  Discard the cleanups for data inserted into the
     breakpoint.  */
  discard_cleanups (bkpt_chain);
  /* But cleanup everything else.  */
  do_cleanups (old_chain);

  /* error call may happen here - have BKPT_CHAIN already discarded.  */
  update_global_location_list (1);
}

/* Set a breakpoint. 
   ARG is a string describing breakpoint address,
   condition, and thread.
   FLAG specifies if a breakpoint is hardware on,
   and if breakpoint is temporary, using BP_HARDWARE_FLAG
   and BP_TEMPFLAG.  */
   
static void
break_command_1 (char *arg, int flag, int from_tty)
{
  int hardwareflag = flag & BP_HARDWAREFLAG;
  int tempflag = flag & BP_TEMPFLAG;

  break_command_really (get_current_arch (),
			arg,
			NULL, 0, 1 /* parse arg */,
			tempflag, hardwareflag, 0 /* traceflag */,
			0 /* Ignore count */,
			pending_break_support, 
			NULL /* breakpoint_ops */,
			from_tty,
			1 /* enabled */);
}


void
set_breakpoint (struct gdbarch *gdbarch,
		char *address, char *condition,
		int hardwareflag, int tempflag,
		int thread, int ignore_count,
		int pending, int enabled)
{
  break_command_really (gdbarch,
			address, condition, thread,
			0 /* condition and thread are valid.  */,
			tempflag, hardwareflag, 0 /* traceflag */,
			ignore_count,
			pending 
			? AUTO_BOOLEAN_TRUE : AUTO_BOOLEAN_FALSE,
			NULL, 0, enabled);
}

/* Adjust SAL to the first instruction past the function prologue.
   The end of the prologue is determined using the line table from
   the debugging information.  explicit_pc and explicit_line are
   not modified.

   If SAL is already past the prologue, then do nothing.  */

static void
skip_prologue_sal (struct symtab_and_line *sal)
{
  struct symbol *sym = find_pc_function (sal->pc);
  struct symtab_and_line start_sal;

  if (sym == NULL)
    return;

  start_sal = find_function_start_sal (sym, 1);
  if (sal->pc < start_sal.pc)
    {
      start_sal.explicit_line = sal->explicit_line;
      start_sal.explicit_pc = sal->explicit_pc;
      *sal = start_sal;
    }
}

/* Helper function for break_command_1 and disassemble_command.  */

void
resolve_sal_pc (struct symtab_and_line *sal)
{
  CORE_ADDR pc;

  if (sal->pc == 0 && sal->symtab != NULL)
    {
      if (!find_line_pc (sal->symtab, sal->line, &pc))
	error (_("No line %d in file \"%s\"."),
	       sal->line, sal->symtab->filename);
      sal->pc = pc;

      /* If this SAL corresponds to a breakpoint inserted using
         a line number, then skip the function prologue if necessary.  */
      if (sal->explicit_line)
	{
	  /* Preserve the original line number.  */
	  int saved_line = sal->line;
	  skip_prologue_sal (sal);
	  sal->line = saved_line;
	}
    }

  if (sal->section == 0 && sal->symtab != NULL)
    {
      struct blockvector *bv;
      struct block *b;
      struct symbol *sym;

      bv = blockvector_for_pc_sect (sal->pc, 0, &b, sal->symtab);
      if (bv != NULL)
	{
	  sym = block_linkage_function (b);
	  if (sym != NULL)
	    {
	      fixup_symbol_section (sym, sal->symtab->objfile);
	      sal->section = SYMBOL_OBJ_SECTION (sym);
	    }
	  else
	    {
	      /* It really is worthwhile to have the section, so we'll just
	         have to look harder. This case can be executed if we have 
	         line numbers but no functions (as can happen in assembly 
	         source).  */

	      struct minimal_symbol *msym;

	      msym = lookup_minimal_symbol_by_pc (sal->pc);
	      if (msym)
		sal->section = SYMBOL_OBJ_SECTION (msym);
	    }
	}
    }
}

void
break_command (char *arg, int from_tty)
{
  break_command_1 (arg, 0, from_tty);
}

void
tbreak_command (char *arg, int from_tty)
{
  break_command_1 (arg, BP_TEMPFLAG, from_tty);
}

static void
hbreak_command (char *arg, int from_tty)
{
  break_command_1 (arg, BP_HARDWAREFLAG, from_tty);
}

static void
thbreak_command (char *arg, int from_tty)
{
  break_command_1 (arg, (BP_TEMPFLAG | BP_HARDWAREFLAG), from_tty);
}

static void
stop_command (char *arg, int from_tty)
{
  printf_filtered (_("Specify the type of breakpoint to set.\n\
Usage: stop in <function | address>\n\
       stop at <line>\n"));
}

static void
stopin_command (char *arg, int from_tty)
{
  int badInput = 0;

  if (arg == (char *) NULL)
    badInput = 1;
  else if (*arg != '*')
    {
      char *argptr = arg;
      int hasColon = 0;

      /* look for a ':'.  If this is a line number specification, then
         say it is bad, otherwise, it should be an address or
         function/method name */
      while (*argptr && !hasColon)
	{
	  hasColon = (*argptr == ':');
	  argptr++;
	}

      if (hasColon)
	badInput = (*argptr != ':');	/* Not a class::method */
      else
	badInput = isdigit (*arg);	/* a simple line number */
    }

  if (badInput)
    printf_filtered (_("Usage: stop in <function | address>\n"));
  else
    break_command_1 (arg, 0, from_tty);
}

static void
stopat_command (char *arg, int from_tty)
{
  int badInput = 0;

  if (arg == (char *) NULL || *arg == '*')	/* no line number */
    badInput = 1;
  else
    {
      char *argptr = arg;
      int hasColon = 0;

      /* look for a ':'.  If there is a '::' then get out, otherwise
         it is probably a line number. */
      while (*argptr && !hasColon)
	{
	  hasColon = (*argptr == ':');
	  argptr++;
	}

      if (hasColon)
	badInput = (*argptr == ':');	/* we have class::method */
      else
	badInput = !isdigit (*arg);	/* not a line number */
    }

  if (badInput)
    printf_filtered (_("Usage: stop at <line>\n"));
  else
    break_command_1 (arg, 0, from_tty);
}

/* accessflag:  hw_write:  watch write, 
                hw_read:   watch read, 
		hw_access: watch access (read or write) */
static void
watch_command_1 (char *arg, int accessflag, int from_tty)
{
  struct gdbarch *gdbarch = get_current_arch ();
  struct breakpoint *b, *scope_breakpoint = NULL;
  struct symtab_and_line sal;
  struct expression *exp;
  struct block *exp_valid_block;
  struct value *val, *mark;
  struct frame_info *frame;
  char *exp_start = NULL;
  char *exp_end = NULL;
  char *tok, *id_tok_start, *end_tok;
  int toklen;
  char *cond_start = NULL;
  char *cond_end = NULL;
  struct expression *cond = NULL;
  int i, other_type_used, target_resources_ok = 0;
  enum bptype bp_type;
  int mem_cnt = 0;
  int thread = -1;

  init_sal (&sal);		/* initialize to zeroes */

  /* Make sure that we actually have parameters to parse.  */
  if (arg != NULL && arg[0] != '\0')
    {
      toklen = strlen (arg); /* Size of argument list.  */

      /* Points tok to the end of the argument list.  */
      tok = arg + toklen - 1;

      /* Go backwards in the parameters list. Skip the last parameter.
         If we're expecting a 'thread <thread_num>' parameter, this should
         be the thread identifier.  */
      while (tok > arg && (*tok == ' ' || *tok == '\t'))
        tok--;
      while (tok > arg && (*tok != ' ' && *tok != '\t'))
        tok--;

      /* Points end_tok to the beginning of the last token.  */
      id_tok_start = tok + 1;

      /* Go backwards in the parameters list. Skip one more parameter.
         If we're expecting a 'thread <thread_num>' parameter, we should
         reach a "thread" token.  */
      while (tok > arg && (*tok == ' ' || *tok == '\t'))
        tok--;

      end_tok = tok;

      while (tok > arg && (*tok != ' ' && *tok != '\t'))
        tok--;

      /* Move the pointer forward to skip the whitespace and
         calculate the length of the token.  */
      tok++;
      toklen = end_tok - tok;

      if (toklen >= 1 && strncmp (tok, "thread", toklen) == 0)
        {
          /* At this point we've found a "thread" token, which means
             the user is trying to set a watchpoint that triggers
             only in a specific thread.  */
          char *endp;

          /* Extract the thread ID from the next token.  */
          thread = strtol (id_tok_start, &endp, 0);

          /* Check if the user provided a valid numeric value for the
             thread ID.  */
          if (*endp != ' ' && *endp != '\t' && *endp != '\0')
            error (_("Invalid thread ID specification %s."), id_tok_start);

          /* Check if the thread actually exists.  */
          if (!valid_thread_id (thread))
            error (_("Unknown thread %d."), thread);

          /* Truncate the string and get rid of the thread <thread_num>
             parameter before the parameter list is parsed by the
             evaluate_expression() function.  */
          *tok = '\0';
        }
    }

  /* Parse the rest of the arguments.  */
  innermost_block = NULL;
  exp_start = arg;
  exp = parse_exp_1 (&arg, 0, 0);
  exp_end = arg;
  /* Remove trailing whitespace from the expression before saving it.
     This makes the eventual display of the expression string a bit
     prettier.  */
  while (exp_end > exp_start && (exp_end[-1] == ' ' || exp_end[-1] == '\t'))
    --exp_end;

  exp_valid_block = innermost_block;
  mark = value_mark ();
  fetch_watchpoint_value (exp, &val, NULL, NULL);
  if (val != NULL)
    release_value (val);

  tok = arg;
  while (*tok == ' ' || *tok == '\t')
    tok++;
  end_tok = tok;

  while (*end_tok != ' ' && *end_tok != '\t' && *end_tok != '\000')
    end_tok++;

  toklen = end_tok - tok;
  if (toklen >= 1 && strncmp (tok, "if", toklen) == 0)
    {
      tok = cond_start = end_tok + 1;
      cond = parse_exp_1 (&tok, 0, 0);
      cond_end = tok;
    }
  if (*tok)
    error (_("Junk at end of command."));

  if (accessflag == hw_read)
    bp_type = bp_read_watchpoint;
  else if (accessflag == hw_access)
    bp_type = bp_access_watchpoint;
  else
    bp_type = bp_hardware_watchpoint;

  mem_cnt = can_use_hardware_watchpoint (val);
  if (mem_cnt == 0 && bp_type != bp_hardware_watchpoint)
    error (_("Expression cannot be implemented with read/access watchpoint."));
  if (mem_cnt != 0)
    {
      i = hw_watchpoint_used_count (bp_type, &other_type_used);
      target_resources_ok = 
	target_can_use_hardware_watchpoint (bp_type, i + mem_cnt, 
					    other_type_used);
      if (target_resources_ok == 0 && bp_type != bp_hardware_watchpoint)
	error (_("Target does not support this type of hardware watchpoint."));

      if (target_resources_ok < 0 && bp_type != bp_hardware_watchpoint)
	error (_("Target can only support one kind of HW watchpoint at a time."));
    }

  /* Change the type of breakpoint to an ordinary watchpoint if a hardware
     watchpoint could not be set.  */
  if (!mem_cnt || target_resources_ok <= 0)
    bp_type = bp_watchpoint;

  frame = block_innermost_frame (exp_valid_block);

  /* If the expression is "local", then set up a "watchpoint scope"
     breakpoint at the point where we've left the scope of the watchpoint
     expression.  Create the scope breakpoint before the watchpoint, so
     that we will encounter it first in bpstat_stop_status.  */
  if (innermost_block && frame)
    {
      if (frame_id_p (frame_unwind_caller_id (frame)))
	{
 	  scope_breakpoint
	    = create_internal_breakpoint (frame_unwind_caller_arch (frame),
					  frame_unwind_caller_pc (frame),
					  bp_watchpoint_scope);

	  scope_breakpoint->enable_state = bp_enabled;

	  /* Automatically delete the breakpoint when it hits.  */
	  scope_breakpoint->disposition = disp_del;

	  /* Only break in the proper frame (help with recursion).  */
	  scope_breakpoint->frame_id = frame_unwind_caller_id (frame);

	  /* Set the address at which we will stop.  */
	  scope_breakpoint->loc->gdbarch
	    = frame_unwind_caller_arch (frame);
	  scope_breakpoint->loc->requested_address
	    = frame_unwind_caller_pc (frame);
	  scope_breakpoint->loc->address
	    = adjust_breakpoint_address (scope_breakpoint->loc->gdbarch,
					 scope_breakpoint->loc->requested_address,
					 scope_breakpoint->type);
	}
    }

  /* Now set up the breakpoint.  */
  b = set_raw_breakpoint (gdbarch, sal, bp_type);
  set_breakpoint_count (breakpoint_count + 1);
  b->number = breakpoint_count;
  b->thread = thread;
  b->disposition = disp_donttouch;
  b->exp = exp;
  b->exp_valid_block = exp_valid_block;
  b->exp_string = savestring (exp_start, exp_end - exp_start);
  b->val = val;
  b->val_valid = 1;
  b->loc->cond = cond;
  if (cond_start)
    b->cond_string = savestring (cond_start, cond_end - cond_start);
  else
    b->cond_string = 0;

  if (frame)
    b->watchpoint_frame = get_frame_id (frame);
  else
    b->watchpoint_frame = null_frame_id;

  if (scope_breakpoint != NULL)
    {
      /* The scope breakpoint is related to the watchpoint.  We will
	 need to act on them together.  */
      b->related_breakpoint = scope_breakpoint;
      scope_breakpoint->related_breakpoint = b;
    }

  value_free_to_mark (mark);
  mention (b);
  update_global_location_list (1);
}

/* Return count of locations need to be watched and can be handled
   in hardware.  If the watchpoint can not be handled
   in hardware return zero.  */

static int
can_use_hardware_watchpoint (struct value *v)
{
  int found_memory_cnt = 0;
  struct value *head = v;

  /* Did the user specifically forbid us to use hardware watchpoints? */
  if (!can_use_hw_watchpoints)
    return 0;

  /* Make sure that the value of the expression depends only upon
     memory contents, and values computed from them within GDB.  If we
     find any register references or function calls, we can't use a
     hardware watchpoint.

     The idea here is that evaluating an expression generates a series
     of values, one holding the value of every subexpression.  (The
     expression a*b+c has five subexpressions: a, b, a*b, c, and
     a*b+c.)  GDB's values hold almost enough information to establish
     the criteria given above --- they identify memory lvalues,
     register lvalues, computed values, etcetera.  So we can evaluate
     the expression, and then scan the chain of values that leaves
     behind to decide whether we can detect any possible change to the
     expression's final value using only hardware watchpoints.

     However, I don't think that the values returned by inferior
     function calls are special in any way.  So this function may not
     notice that an expression involving an inferior function call
     can't be watched with hardware watchpoints.  FIXME.  */
  for (; v; v = value_next (v))
    {
      if (VALUE_LVAL (v) == lval_memory)
	{
	  if (value_lazy (v))
	    /* A lazy memory lvalue is one that GDB never needed to fetch;
	       we either just used its address (e.g., `a' in `a.b') or
	       we never needed it at all (e.g., `a' in `a,b').  */
	    ;
	  else
	    {
	      /* Ahh, memory we actually used!  Check if we can cover
                 it with hardware watchpoints.  */
	      struct type *vtype = check_typedef (value_type (v));

	      /* We only watch structs and arrays if user asked for it
		 explicitly, never if they just happen to appear in a
		 middle of some value chain.  */
	      if (v == head
		  || (TYPE_CODE (vtype) != TYPE_CODE_STRUCT
		      && TYPE_CODE (vtype) != TYPE_CODE_ARRAY))
		{
		  CORE_ADDR vaddr = value_address (v);
		  int       len   = TYPE_LENGTH (value_type (v));

		  if (!target_region_ok_for_hw_watchpoint (vaddr, len))
		    return 0;
		  else
		    found_memory_cnt++;
		}
	    }
	}
      else if (VALUE_LVAL (v) != not_lval
	       && deprecated_value_modifiable (v) == 0)
	return 0;	/* ??? What does this represent? */
      else if (VALUE_LVAL (v) == lval_register)
	return 0;	/* cannot watch a register with a HW watchpoint */
    }

  /* The expression itself looks suitable for using a hardware
     watchpoint, but give the target machine a chance to reject it.  */
  return found_memory_cnt;
}

void
watch_command_wrapper (char *arg, int from_tty)
{
  watch_command (arg, from_tty);
}

static void
watch_command (char *arg, int from_tty)
{
  watch_command_1 (arg, hw_write, from_tty);
}

void
rwatch_command_wrapper (char *arg, int from_tty)
{
  rwatch_command (arg, from_tty);
}

static void
rwatch_command (char *arg, int from_tty)
{
  watch_command_1 (arg, hw_read, from_tty);
}

void
awatch_command_wrapper (char *arg, int from_tty)
{
  awatch_command (arg, from_tty);
}

static void
awatch_command (char *arg, int from_tty)
{
  watch_command_1 (arg, hw_access, from_tty);
}


/* Helper routines for the until_command routine in infcmd.c.  Here
   because it uses the mechanisms of breakpoints.  */

struct until_break_command_continuation_args
{
  struct breakpoint *breakpoint;
  struct breakpoint *breakpoint2;
};

/* This function is called by fetch_inferior_event via the
   cmd_continuation pointer, to complete the until command. It takes
   care of cleaning up the temporary breakpoints set up by the until
   command. */
static void
until_break_command_continuation (void *arg)
{
  struct until_break_command_continuation_args *a = arg;

  delete_breakpoint (a->breakpoint);
  if (a->breakpoint2)
    delete_breakpoint (a->breakpoint2);
}

void
until_break_command (char *arg, int from_tty, int anywhere)
{
  struct symtabs_and_lines sals;
  struct symtab_and_line sal;
  struct frame_info *frame = get_selected_frame (NULL);
  struct breakpoint *breakpoint;
  struct breakpoint *breakpoint2 = NULL;
  struct cleanup *old_chain;

  clear_proceed_status ();

  /* Set a breakpoint where the user wants it and at return from
     this function */

  if (default_breakpoint_valid)
    sals = decode_line_1 (&arg, 1, default_breakpoint_symtab,
			  default_breakpoint_line, (char ***) NULL, NULL);
  else
    sals = decode_line_1 (&arg, 1, (struct symtab *) NULL, 
			  0, (char ***) NULL, NULL);

  if (sals.nelts != 1)
    error (_("Couldn't get information on specified line."));

  sal = sals.sals[0];
  xfree (sals.sals);	/* malloc'd, so freed */

  if (*arg)
    error (_("Junk at end of arguments."));

  resolve_sal_pc (&sal);

  if (anywhere)
    /* If the user told us to continue until a specified location,
       we don't specify a frame at which we need to stop.  */
    breakpoint = set_momentary_breakpoint (get_frame_arch (frame), sal,
					   null_frame_id, bp_until);
  else
    /* Otherwise, specify the selected frame, because we want to stop only
       at the very same frame.  */
    breakpoint = set_momentary_breakpoint (get_frame_arch (frame), sal,
					   get_stack_frame_id (frame),
					   bp_until);

  old_chain = make_cleanup_delete_breakpoint (breakpoint);

  /* Keep within the current frame, or in frames called by the current
     one.  */

  if (frame_id_p (frame_unwind_caller_id (frame)))
    {
      sal = find_pc_line (frame_unwind_caller_pc (frame), 0);
      sal.pc = frame_unwind_caller_pc (frame);
      breakpoint2 = set_momentary_breakpoint (frame_unwind_caller_arch (frame),
					      sal,
					      frame_unwind_caller_id (frame),
					      bp_until);
      make_cleanup_delete_breakpoint (breakpoint2);
    }

  proceed (-1, TARGET_SIGNAL_DEFAULT, 0);

  /* If we are running asynchronously, and proceed call above has actually
     managed to start the target, arrange for breakpoints to be
     deleted when the target stops.  Otherwise, we're already stopped and
     delete breakpoints via cleanup chain.  */

  if (target_can_async_p () && is_running (inferior_ptid))
    {
      struct until_break_command_continuation_args *args;
      args = xmalloc (sizeof (*args));

      args->breakpoint = breakpoint;
      args->breakpoint2 = breakpoint2;

      discard_cleanups (old_chain);
      add_continuation (inferior_thread (),
			until_break_command_continuation, args,
			xfree);
    }
  else
    do_cleanups (old_chain);
}

static void
ep_skip_leading_whitespace (char **s)
{
  if ((s == NULL) || (*s == NULL))
    return;
  while (isspace (**s))
    *s += 1;
}

/* This function attempts to parse an optional "if <cond>" clause
   from the arg string.  If one is not found, it returns NULL.

   Else, it returns a pointer to the condition string.  (It does not
   attempt to evaluate the string against a particular block.)  And,
   it updates arg to point to the first character following the parsed
   if clause in the arg string. */

static char *
ep_parse_optional_if_clause (char **arg)
{
  char *cond_string;

  if (((*arg)[0] != 'i') || ((*arg)[1] != 'f') || !isspace ((*arg)[2]))
    return NULL;

  /* Skip the "if" keyword. */
  (*arg) += 2;

  /* Skip any extra leading whitespace, and record the start of the
     condition string. */
  ep_skip_leading_whitespace (arg);
  cond_string = *arg;

  /* Assume that the condition occupies the remainder of the arg string. */
  (*arg) += strlen (cond_string);

  return cond_string;
}

/* This function attempts to parse an optional filename from the arg
   string.  If one is not found, it returns NULL.

   Else, it returns a pointer to the parsed filename.  (This function
   makes no attempt to verify that a file of that name exists, or is
   accessible.)  And, it updates arg to point to the first character
   following the parsed filename in the arg string.

   Note that clients needing to preserve the returned filename for
   future access should copy it to their own buffers. */
static char *
ep_parse_optional_filename (char **arg)
{
  static char filename[1024];
  char *arg_p = *arg;
  int i;
  char c;

  if ((*arg_p == '\0') || isspace (*arg_p))
    return NULL;

  for (i = 0;; i++)
    {
      c = *arg_p;
      if (isspace (c))
	c = '\0';
      filename[i] = c;
      if (c == '\0')
	break;
      arg_p++;
    }
  *arg = arg_p;

  return filename;
}

/* Commands to deal with catching events, such as signals, exceptions,
   process start/exit, etc.  */

typedef enum
{
  catch_fork_temporary, catch_vfork_temporary,
  catch_fork_permanent, catch_vfork_permanent
}
catch_fork_kind;

static void
catch_fork_command_1 (char *arg, int from_tty, struct cmd_list_element *command)
{
  struct gdbarch *gdbarch = get_current_arch ();
  char *cond_string = NULL;
  catch_fork_kind fork_kind;
  int tempflag;

  fork_kind = (catch_fork_kind) (uintptr_t) get_cmd_context (command);
  tempflag = (fork_kind == catch_fork_temporary
	      || fork_kind == catch_vfork_temporary);

  if (!arg)
    arg = "";
  ep_skip_leading_whitespace (&arg);

  /* The allowed syntax is:
     catch [v]fork
     catch [v]fork if <cond>

     First, check if there's an if clause. */
  cond_string = ep_parse_optional_if_clause (&arg);

  if ((*arg != '\0') && !isspace (*arg))
    error (_("Junk at end of arguments."));

  /* If this target supports it, create a fork or vfork catchpoint
     and enable reporting of such events. */
  switch (fork_kind)
    {
    case catch_fork_temporary:
    case catch_fork_permanent:
      create_fork_vfork_event_catchpoint (gdbarch, tempflag, cond_string,
                                          &catch_fork_breakpoint_ops);
      break;
    case catch_vfork_temporary:
    case catch_vfork_permanent:
      create_fork_vfork_event_catchpoint (gdbarch, tempflag, cond_string,
                                          &catch_vfork_breakpoint_ops);
      break;
    default:
      error (_("unsupported or unknown fork kind; cannot catch it"));
      break;
    }
}

static void
catch_exec_command_1 (char *arg, int from_tty, struct cmd_list_element *command)
{
  struct gdbarch *gdbarch = get_current_arch ();
  int tempflag;
  char *cond_string = NULL;

  tempflag = get_cmd_context (command) == CATCH_TEMPORARY;

  if (!arg)
    arg = "";
  ep_skip_leading_whitespace (&arg);

  /* The allowed syntax is:
     catch exec
     catch exec if <cond>

     First, check if there's an if clause. */
  cond_string = ep_parse_optional_if_clause (&arg);

  if ((*arg != '\0') && !isspace (*arg))
    error (_("Junk at end of arguments."));

  /* If this target supports it, create an exec catchpoint
     and enable reporting of such events. */
  create_catchpoint (gdbarch, tempflag, cond_string,
		     &catch_exec_breakpoint_ops);
}

static enum print_stop_action
print_exception_catchpoint (struct breakpoint *b)
{
  int bp_temp, bp_throw;

  annotate_catchpoint (b->number);

  bp_throw = strstr (b->addr_string, "throw") != NULL;
  if (b->loc->address != b->loc->requested_address)
    breakpoint_adjustment_warning (b->loc->requested_address,
	                           b->loc->address,
				   b->number, 1);
  bp_temp = b->disposition == disp_del;
  ui_out_text (uiout, 
	       bp_temp ? "Temporary catchpoint "
		       : "Catchpoint ");
  if (!ui_out_is_mi_like_p (uiout))
    ui_out_field_int (uiout, "bkptno", b->number);
  ui_out_text (uiout,
	       bp_throw ? " (exception thrown), "
		        : " (exception caught), ");
  if (ui_out_is_mi_like_p (uiout))
    {
      ui_out_field_string (uiout, "reason", 
			   async_reason_lookup (EXEC_ASYNC_BREAKPOINT_HIT));
      ui_out_field_string (uiout, "disp", bpdisp_text (b->disposition));
      ui_out_field_int (uiout, "bkptno", b->number);
    }
  return PRINT_SRC_AND_LOC;
}

static void
print_one_exception_catchpoint (struct breakpoint *b, struct bp_location **last_loc)
{
  struct value_print_options opts;
  get_user_print_options (&opts);
  if (opts.addressprint)
    {
      annotate_field (4);
      if (b->loc == NULL || b->loc->shlib_disabled)
	ui_out_field_string (uiout, "addr", "<PENDING>");
      else
	ui_out_field_core_addr (uiout, "addr",
				b->loc->gdbarch, b->loc->address);
    }
  annotate_field (5);
  if (b->loc)
    *last_loc = b->loc;
  if (strstr (b->addr_string, "throw") != NULL)
    ui_out_field_string (uiout, "what", "exception throw");
  else
    ui_out_field_string (uiout, "what", "exception catch");
}

static void
print_mention_exception_catchpoint (struct breakpoint *b)
{
  int bp_temp;
  int bp_throw;

  bp_temp = b->disposition == disp_del;
  bp_throw = strstr (b->addr_string, "throw") != NULL;
  ui_out_text (uiout, bp_temp ? _("Temporary catchpoint ")
			      : _("Catchpoint "));
  ui_out_field_int (uiout, "bkptno", b->number);
  ui_out_text (uiout, bp_throw ? _(" (throw)")
			       : _(" (catch)"));
}

static struct breakpoint_ops gnu_v3_exception_catchpoint_ops = {
  NULL, /* insert */
  NULL, /* remove */
  NULL, /* breakpoint_hit */
  print_exception_catchpoint,
  print_one_exception_catchpoint,
  print_mention_exception_catchpoint
};

static int
handle_gnu_v3_exceptions (int tempflag, char *cond_string,
			  enum exception_event_kind ex_event, int from_tty)
{
  char *trigger_func_name;
 
  if (ex_event == EX_EVENT_CATCH)
    trigger_func_name = "__cxa_begin_catch";
  else
    trigger_func_name = "__cxa_throw";

  break_command_really (get_current_arch (),
			trigger_func_name, cond_string, -1,
			0 /* condition and thread are valid.  */,
			tempflag, 0, 0,
			0,
			AUTO_BOOLEAN_TRUE /* pending */,
			&gnu_v3_exception_catchpoint_ops, from_tty,
			1 /* enabled */);

  return 1;
}

/* Deal with "catch catch" and "catch throw" commands */

static void
catch_exception_command_1 (enum exception_event_kind ex_event, char *arg,
			   int tempflag, int from_tty)
{
  char *cond_string = NULL;
  struct symtab_and_line *sal = NULL;

  if (!arg)
    arg = "";
  ep_skip_leading_whitespace (&arg);

  cond_string = ep_parse_optional_if_clause (&arg);

  if ((*arg != '\0') && !isspace (*arg))
    error (_("Junk at end of arguments."));

  if (ex_event != EX_EVENT_THROW
      && ex_event != EX_EVENT_CATCH)
    error (_("Unsupported or unknown exception event; cannot catch it"));

  if (handle_gnu_v3_exceptions (tempflag, cond_string, ex_event, from_tty))
    return;

  warning (_("Unsupported with this platform/compiler combination."));
}

/* Implementation of "catch catch" command.  */

static void
catch_catch_command (char *arg, int from_tty, struct cmd_list_element *command)
{
  int tempflag = get_cmd_context (command) == CATCH_TEMPORARY;
  catch_exception_command_1 (EX_EVENT_CATCH, arg, tempflag, from_tty);
}

/* Implementation of "catch throw" command.  */

static void
catch_throw_command (char *arg, int from_tty, struct cmd_list_element *command)
{
  int tempflag = get_cmd_context (command) == CATCH_TEMPORARY;
  catch_exception_command_1 (EX_EVENT_THROW, arg, tempflag, from_tty);
}

/* Create a breakpoint struct for Ada exception catchpoints.  */

static void
create_ada_exception_breakpoint (struct gdbarch *gdbarch,
				 struct symtab_and_line sal,
                                 char *addr_string,
                                 char *exp_string,
                                 char *cond_string,
                                 struct expression *cond,
                                 struct breakpoint_ops *ops,
                                 int tempflag,
                                 int from_tty)
{
  struct breakpoint *b;

  if (from_tty)
    {
      struct gdbarch *loc_gdbarch = get_sal_arch (sal);
      if (!loc_gdbarch)
	loc_gdbarch = gdbarch;

      describe_other_breakpoints (loc_gdbarch, sal.pc, sal.section, -1);
      /* FIXME: brobecker/2006-12-28: Actually, re-implement a special
         version for exception catchpoints, because two catchpoints
         used for different exception names will use the same address.
         In this case, a "breakpoint ... also set at..." warning is
         unproductive.  Besides. the warning phrasing is also a bit
         inapropriate, we should use the word catchpoint, and tell
         the user what type of catchpoint it is.  The above is good
         enough for now, though.  */
    }

  b = set_raw_breakpoint (gdbarch, sal, bp_breakpoint);
  set_breakpoint_count (breakpoint_count + 1);

  b->enable_state = bp_enabled;
  b->disposition = tempflag ? disp_del : disp_donttouch;
  b->number = breakpoint_count;
  b->ignore_count = 0;
  b->loc->cond = cond;
  b->addr_string = addr_string;
  b->language = language_ada;
  b->cond_string = cond_string;
  b->exp_string = exp_string;
  b->thread = -1;
  b->ops = ops;

  mention (b);
  update_global_location_list (1);
}

/* Implement the "catch exception" command.  */

static void
catch_ada_exception_command (char *arg, int from_tty,
			     struct cmd_list_element *command)
{
  struct gdbarch *gdbarch = get_current_arch ();
  int tempflag;
  struct symtab_and_line sal;
  enum bptype type;
  char *addr_string = NULL;
  char *exp_string = NULL;
  char *cond_string = NULL;
  struct expression *cond = NULL;
  struct breakpoint_ops *ops = NULL;

  tempflag = get_cmd_context (command) == CATCH_TEMPORARY;

  if (!arg)
    arg = "";
  sal = ada_decode_exception_location (arg, &addr_string, &exp_string,
                                       &cond_string, &cond, &ops);
  create_ada_exception_breakpoint (gdbarch, sal, addr_string, exp_string,
                                   cond_string, cond, ops, tempflag,
                                   from_tty);
}

/* Cleanup function for a syscall filter list.  */
static void
clean_up_filters (void *arg)
{
  VEC(int) *iter = *(VEC(int) **) arg;
  VEC_free (int, iter);
}

/* Splits the argument using space as delimiter.  Returns an xmalloc'd
   filter list, or NULL if no filtering is required.  */
static VEC(int) *
catch_syscall_split_args (char *arg)
{
  VEC(int) *result = NULL;
  struct cleanup *cleanup = make_cleanup (clean_up_filters, &result);

  while (*arg != '\0')
    {
      int i, syscall_number;
      char *endptr;
      char cur_name[128];
      struct syscall s;

      /* Skip whitespace.  */
      while (isspace (*arg))
	arg++;

      for (i = 0; i < 127 && arg[i] && !isspace (arg[i]); ++i)
	cur_name[i] = arg[i];
      cur_name[i] = '\0';
      arg += i;

      /* Check if the user provided a syscall name or a number.  */
      syscall_number = (int) strtol (cur_name, &endptr, 0);
      if (*endptr == '\0')
	{
	  get_syscall_by_number (syscall_number, &s);

	  if (s.name == NULL)
	    /* We can issue just a warning, but still create the catchpoint.
	       This is because, even not knowing the syscall name that
	       this number represents, we can still try to catch the syscall
	       number.  */
	    warning (_("The number '%d' does not represent a known syscall."),
		     syscall_number);
	}
      else
	{
	  /* We have a name.  Let's check if it's valid and convert it
	     to a number.  */
	  get_syscall_by_name (cur_name, &s);

	  if (s.number == UNKNOWN_SYSCALL)
	    /* Here we have to issue an error instead of a warning, because
	       GDB cannot do anything useful if there's no syscall number to
	       be caught.  */
	    error (_("Unknown syscall name '%s'."), cur_name);
	}

      /* Ok, it's valid.  */
      VEC_safe_push (int, result, s.number);
    }

  discard_cleanups (cleanup);
  return result;
}

/* Implement the "catch syscall" command.  */

static void
catch_syscall_command_1 (char *arg, int from_tty, struct cmd_list_element *command)
{
  int tempflag;
  VEC(int) *filter;
  struct syscall s;
  struct gdbarch *gdbarch = get_current_arch ();

  /* Checking if the feature if supported.  */
  if (gdbarch_get_syscall_number_p (gdbarch) == 0)
    error (_("The feature 'catch syscall' is not supported on \
this architeture yet."));

  tempflag = get_cmd_context (command) == CATCH_TEMPORARY;

  ep_skip_leading_whitespace (&arg);

  /* We need to do this first "dummy" translation in order
     to get the syscall XML file loaded or, most important,
     to display a warning to the user if there's no XML file
     for his/her architecture.  */
  get_syscall_by_number (0, &s);

  /* The allowed syntax is:
     catch syscall
     catch syscall <name | number> [<name | number> ... <name | number>]

     Let's check if there's a syscall name.  */

  if (arg != NULL)
    filter = catch_syscall_split_args (arg);
  else
    filter = NULL;

  create_syscall_event_catchpoint (tempflag, filter,
				   &catch_syscall_breakpoint_ops);
}

/* Implement the "catch assert" command.  */

static void
catch_assert_command (char *arg, int from_tty, struct cmd_list_element *command)
{
  struct gdbarch *gdbarch = get_current_arch ();
  int tempflag;
  struct symtab_and_line sal;
  char *addr_string = NULL;
  struct breakpoint_ops *ops = NULL;

  tempflag = get_cmd_context (command) == CATCH_TEMPORARY;

  if (!arg)
    arg = "";
  sal = ada_decode_assert_location (arg, &addr_string, &ops);
  create_ada_exception_breakpoint (gdbarch, sal, addr_string, NULL, NULL, NULL,
				   ops, tempflag, from_tty);
}

static void
catch_command (char *arg, int from_tty)
{
  error (_("Catch requires an event name."));
}


static void
tcatch_command (char *arg, int from_tty)
{
  error (_("Catch requires an event name."));
}

/* Delete breakpoints by address or line.  */

static void
clear_command (char *arg, int from_tty)
{
  struct breakpoint *b;
  VEC(breakpoint_p) *found = 0;
  int ix;
  int default_match;
  struct symtabs_and_lines sals;
  struct symtab_and_line sal;
  int i;

  if (arg)
    {
      sals = decode_line_spec (arg, 1);
      default_match = 0;
    }
  else
    {
      sals.sals = (struct symtab_and_line *)
	xmalloc (sizeof (struct symtab_and_line));
      make_cleanup (xfree, sals.sals);
      init_sal (&sal);		/* initialize to zeroes */
      sal.line = default_breakpoint_line;
      sal.symtab = default_breakpoint_symtab;
      sal.pc = default_breakpoint_address;
      if (sal.symtab == 0)
	error (_("No source file specified."));

      sals.sals[0] = sal;
      sals.nelts = 1;

      default_match = 1;
    }

  /* We don't call resolve_sal_pc here. That's not
     as bad as it seems, because all existing breakpoints
     typically have both file/line and pc set.  So, if
     clear is given file/line, we can match this to existing
     breakpoint without obtaining pc at all.

     We only support clearing given the address explicitly 
     present in breakpoint table.  Say, we've set breakpoint 
     at file:line. There were several PC values for that file:line,
     due to optimization, all in one block.
     We've picked one PC value. If "clear" is issued with another
     PC corresponding to the same file:line, the breakpoint won't
     be cleared.  We probably can still clear the breakpoint, but 
     since the other PC value is never presented to user, user
     can only find it by guessing, and it does not seem important
     to support that.  */

  /* For each line spec given, delete bps which correspond
     to it.  Do it in two passes, solely to preserve the current
     behavior that from_tty is forced true if we delete more than
     one breakpoint.  */

  found = NULL;
  for (i = 0; i < sals.nelts; i++)
    {
      /* If exact pc given, clear bpts at that pc.
         If line given (pc == 0), clear all bpts on specified line.
         If defaulting, clear all bpts on default line
         or at default pc.

         defaulting    sal.pc != 0    tests to do

         0              1             pc
         1              1             pc _and_ line
         0              0             line
         1              0             <can't happen> */

      sal = sals.sals[i];

      /* Find all matching breakpoints and add them to
	 'found'.  */
      ALL_BREAKPOINTS (b)
	{
	  int match = 0;
	  /* Are we going to delete b? */
	  if (b->type != bp_none
	      && b->type != bp_watchpoint
	      && b->type != bp_hardware_watchpoint
	      && b->type != bp_read_watchpoint
	      && b->type != bp_access_watchpoint)
	    {
	      struct bp_location *loc = b->loc;
	      for (; loc; loc = loc->next)
		{
		  int pc_match = sal.pc 
		    && (loc->address == sal.pc)
		    && (!section_is_overlay (loc->section)
			|| loc->section == sal.section);
		  int line_match = ((default_match || (0 == sal.pc))
				    && b->source_file != NULL
				    && sal.symtab != NULL
				    && strcmp (b->source_file, sal.symtab->filename) == 0
				    && b->line_number == sal.line);
		  if (pc_match || line_match)
		    {
		      match = 1;
		      break;
		    }
		}
	    }

	  if (match)
	    VEC_safe_push(breakpoint_p, found, b);
	}
    }
  /* Now go thru the 'found' chain and delete them.  */
  if (VEC_empty(breakpoint_p, found))
    {
      if (arg)
	error (_("No breakpoint at %s."), arg);
      else
	error (_("No breakpoint at this line."));
    }

  if (VEC_length(breakpoint_p, found) > 1)
    from_tty = 1;		/* Always report if deleted more than one */
  if (from_tty)
    {
      if (VEC_length(breakpoint_p, found) == 1)
	printf_unfiltered (_("Deleted breakpoint "));
      else
	printf_unfiltered (_("Deleted breakpoints "));
    }
  breakpoints_changed ();

  for (ix = 0; VEC_iterate(breakpoint_p, found, ix, b); ix++)
    {
      if (from_tty)
	printf_unfiltered ("%d ", b->number);
      delete_breakpoint (b);
    }
  if (from_tty)
    putchar_unfiltered ('\n');
}

/* Delete breakpoint in BS if they are `delete' breakpoints and
   all breakpoints that are marked for deletion, whether hit or not.
   This is called after any breakpoint is hit, or after errors.  */

void
breakpoint_auto_delete (bpstat bs)
{
  struct breakpoint *b, *temp;

  for (; bs; bs = bs->next)
    if (bs->breakpoint_at 
	&& bs->breakpoint_at->owner
	&& bs->breakpoint_at->owner->disposition == disp_del
	&& bs->stop)
      delete_breakpoint (bs->breakpoint_at->owner);

  ALL_BREAKPOINTS_SAFE (b, temp)
  {
    if (b->disposition == disp_del_at_next_stop)
      delete_breakpoint (b);
  }
}

/* A cleanup function which destroys a vector.  */

static void
do_vec_free (void *p)
{
  VEC(bp_location_p) **vec = p;
  if (*vec)
    VEC_free (bp_location_p, *vec);
}

/* If SHOULD_INSERT is false, do not insert any breakpoint locations
   into the inferior, only remove already-inserted locations that no
   longer should be inserted.  Functions that delete a breakpoint or
   breakpoints should pass false, so that deleting a breakpoint
   doesn't have the side effect of inserting the locations of other
   breakpoints that are marked not-inserted, but should_be_inserted
   returns true on them.

   This behaviour is useful is situations close to tear-down -- e.g.,
   after an exec, while the target still has execution, but breakpoint
   shadows of the previous executable image should *NOT* be restored
   to the new image; or before detaching, where the target still has
   execution and wants to delete breakpoints from GDB's lists, and all
   breakpoints had already been removed from the inferior.  */

static void
update_global_location_list (int should_insert)
{
  struct breakpoint *b;
  struct bp_location **next = &bp_location_chain;
  struct bp_location *loc;
  struct bp_location *loc2;
  VEC(bp_location_p) *old_locations = NULL;
  int ret;
  int ix;
  struct cleanup *cleanups;

  cleanups = make_cleanup (do_vec_free, &old_locations);
  /* Store old locations for future reference.  */
  for (loc = bp_location_chain; loc; loc = loc->global_next)
    VEC_safe_push (bp_location_p, old_locations, loc);

  bp_location_chain = NULL;
  ALL_BREAKPOINTS (b)
    {
      for (loc = b->loc; loc; loc = loc->next)
	{
	  *next = loc;
	  next = &(loc->global_next);
	  *next = NULL;
	}
    }

  /* Identify bp_location instances that are no longer present in the new
     list, and therefore should be freed.  Note that it's not necessary that
     those locations should be removed from inferior -- if there's another
     location at the same address (previously marked as duplicate),
     we don't need to remove/insert the location.  */
  for (ix = 0; VEC_iterate(bp_location_p, old_locations, ix, loc); ++ix)
    {
      /* Tells if 'loc' is found amoung the new locations.  If not, we
	 have to free it.  */
      int found_object = 0;
      /* Tells if the location should remain inserted in the target.  */
      int keep_in_target = 0;
      int removed = 0;
      for (loc2 = bp_location_chain; loc2; loc2 = loc2->global_next)
	if (loc2 == loc)
	  {
	    found_object = 1;
	    break;
	  }

      /* If this location is no longer present, and inserted, look if there's
	 maybe a new location at the same address.  If so, mark that one 
	 inserted, and don't remove this one.  This is needed so that we 
	 don't have a time window where a breakpoint at certain location is not
	 inserted.  */

      if (loc->inserted)
	{
	  /* If the location is inserted now, we might have to remove it.  */

	  if (found_object && should_be_inserted (loc))
	    {
	      /* The location is still present in the location list, and still
		 should be inserted.  Don't do anything.  */
	      keep_in_target = 1;
	    }
	  else
	    {
	      /* The location is either no longer present, or got disabled.
		 See if there's another location at the same address, in which 
		 case we don't need to remove this one from the target.  */
	      if (breakpoint_address_is_meaningful (loc->owner))
		for (loc2 = bp_location_chain; loc2; loc2 = loc2->global_next)
		  {
		    /* For the sake of should_insert_location.  The
		       call to check_duplicates will fix up this later.  */
		    loc2->duplicate = 0;
		    if (should_be_inserted (loc2)
			&& loc2 != loc && loc2->address == loc->address)
		      {		  
			loc2->inserted = 1;
			loc2->target_info = loc->target_info;
			keep_in_target = 1;
			break;
		      }
		  }
	    }

	  if (!keep_in_target)
	    {
	      if (remove_breakpoint (loc, mark_uninserted))
		{
		  /* This is just about all we can do.  We could keep this
		     location on the global list, and try to remove it next
		     time, but there's no particular reason why we will
		     succeed next time.  
		     
		     Note that at this point, loc->owner is still valid,
		     as delete_breakpoint frees the breakpoint only
		     after calling us.  */
		  printf_filtered (_("warning: Error removing breakpoint %d\n"), 
				   loc->owner->number);
		}
	      removed = 1;
	    }
	}

      if (!found_object)
	{
	  if (removed && non_stop)
	    {
	      /* This location was removed from the targets.  In non-stop mode,
		 a race condition is possible where we've removed a breakpoint,
		 but stop events for that breakpoint are already queued and will
		 arrive later.  To suppress spurious SIGTRAPs reported to user,
		 we keep this breakpoint location for a bit, and will retire it
		 after we see 3 * thread_count events.
		 The theory here is that reporting of events should,
		 "on the average", be fair, so after that many event we'll see
		 events from all threads that have anything of interest, and no
		 longer need to keep this breakpoint.  This is just a
		 heuristic, but if it's wrong, we'll report unexpected SIGTRAP,
		 which is usability issue, but not a correctness problem.  */
	      loc->events_till_retirement = 3 * (thread_count () + 1);
	      loc->owner = NULL;

	      VEC_safe_push (bp_location_p, moribund_locations, loc);
	    }
	  else
	    free_bp_location (loc);
	}
    }

  ALL_BREAKPOINTS (b)
    {
      check_duplicates (b);
    }

  if (breakpoints_always_inserted_mode () && should_insert
      && (have_live_inferiors ()
	  || (gdbarch_has_global_breakpoints (target_gdbarch))))
    insert_breakpoint_locations ();

  do_cleanups (cleanups);
}

void
breakpoint_retire_moribund (void)
{
  struct bp_location *loc;
  int ix;

  for (ix = 0; VEC_iterate (bp_location_p, moribund_locations, ix, loc); ++ix)
    if (--(loc->events_till_retirement) == 0)
      {
	free_bp_location (loc);
	VEC_unordered_remove (bp_location_p, moribund_locations, ix);
	--ix;
      }
}

static void
update_global_location_list_nothrow (int inserting)
{
  struct gdb_exception e;
  TRY_CATCH (e, RETURN_MASK_ERROR)
    update_global_location_list (inserting);
}

/* Clear BPT from a BPS.  */
static void
bpstat_remove_breakpoint (bpstat bps, struct breakpoint *bpt)
{
  bpstat bs;
  for (bs = bps; bs; bs = bs->next)
    if (bs->breakpoint_at && bs->breakpoint_at->owner == bpt)
      {
	bs->breakpoint_at = NULL;
	bs->old_val = NULL;
	/* bs->commands will be freed later.  */
      }
}

/* Callback for iterate_over_threads.  */
static int
bpstat_remove_breakpoint_callback (struct thread_info *th, void *data)
{
  struct breakpoint *bpt = data;
  bpstat_remove_breakpoint (th->stop_bpstat, bpt);
  return 0;
}

/* Delete a breakpoint and clean up all traces of it in the data
   structures. */

void
delete_breakpoint (struct breakpoint *bpt)
{
  struct breakpoint *b;
  struct bp_location *loc, *next;

  gdb_assert (bpt != NULL);

  /* Has this bp already been deleted?  This can happen because multiple
     lists can hold pointers to bp's.  bpstat lists are especial culprits.

     One example of this happening is a watchpoint's scope bp.  When the
     scope bp triggers, we notice that the watchpoint is out of scope, and
     delete it.  We also delete its scope bp.  But the scope bp is marked
     "auto-deleting", and is already on a bpstat.  That bpstat is then
     checked for auto-deleting bp's, which are deleted.

     A real solution to this problem might involve reference counts in bp's,
     and/or giving them pointers back to their referencing bpstat's, and
     teaching delete_breakpoint to only free a bp's storage when no more
     references were extent.  A cheaper bandaid was chosen.  */
  if (bpt->type == bp_none)
    return;

  observer_notify_breakpoint_deleted (bpt->number);

  if (breakpoint_chain == bpt)
    breakpoint_chain = bpt->next;

  ALL_BREAKPOINTS (b)
    if (b->next == bpt)
    {
      b->next = bpt->next;
      break;
    }

  free_command_lines (&bpt->commands);
  if (bpt->cond_string != NULL)
    xfree (bpt->cond_string);
  if (bpt->addr_string != NULL)
    xfree (bpt->addr_string);
  if (bpt->exp != NULL)
    xfree (bpt->exp);
  if (bpt->exp_string != NULL)
    xfree (bpt->exp_string);
  if (bpt->val != NULL)
    value_free (bpt->val);
  if (bpt->source_file != NULL)
    xfree (bpt->source_file);
  if (bpt->exec_pathname != NULL)
    xfree (bpt->exec_pathname);
  clean_up_filters (&bpt->syscalls_to_be_caught);

  /* Be sure no bpstat's are pointing at it after it's been freed.  */
  /* FIXME, how can we find all bpstat's?
     We just check stop_bpstat for now.  Note that we cannot just
     remove bpstats pointing at bpt from the stop_bpstat list
     entirely, as breakpoint commands are associated with the bpstat;
     if we remove it here, then the later call to
         bpstat_do_actions (&stop_bpstat);
     in event-top.c won't do anything, and temporary breakpoints
     with commands won't work.  */

  iterate_over_threads (bpstat_remove_breakpoint_callback, bpt);

  /* Now that breakpoint is removed from breakpoint
     list, update the global location list.  This
     will remove locations that used to belong to
     this breakpoint.  Do this before freeing
     the breakpoint itself, since remove_breakpoint
     looks at location's owner.  It might be better
     design to have location completely self-contained,
     but it's not the case now.  */
  update_global_location_list (0);


  /* On the chance that someone will soon try again to delete this same
     bp, we mark it as deleted before freeing its storage. */
  bpt->type = bp_none;

  xfree (bpt);
}

static void
do_delete_breakpoint_cleanup (void *b)
{
  delete_breakpoint (b);
}

struct cleanup *
make_cleanup_delete_breakpoint (struct breakpoint *b)
{
  return make_cleanup (do_delete_breakpoint_cleanup, b);
}

void
delete_command (char *arg, int from_tty)
{
  struct breakpoint *b, *temp;

  dont_repeat ();

  if (arg == 0)
    {
      int breaks_to_delete = 0;

      /* Delete all breakpoints if no argument.
         Do not delete internal or call-dummy breakpoints, these
         have to be deleted with an explicit breakpoint number argument.  */
      ALL_BREAKPOINTS (b)
      {
	if (b->type != bp_call_dummy
	    && b->type != bp_shlib_event
	    && b->type != bp_jit_event
	    && b->type != bp_thread_event
	    && b->type != bp_overlay_event
	    && b->type != bp_longjmp_master
	    && b->number >= 0)
	  {
	    breaks_to_delete = 1;
	    break;
	  }
      }

      /* Ask user only if there are some breakpoints to delete.  */
      if (!from_tty
	  || (breaks_to_delete && query (_("Delete all breakpoints? "))))
	{
	  ALL_BREAKPOINTS_SAFE (b, temp)
	  {
	    if (b->type != bp_call_dummy
		&& b->type != bp_shlib_event
		&& b->type != bp_thread_event
		&& b->type != bp_jit_event
		&& b->type != bp_overlay_event
		&& b->type != bp_longjmp_master
		&& b->number >= 0)
	      delete_breakpoint (b);
	  }
	}
    }
  else
    map_breakpoint_numbers (arg, delete_breakpoint);
}

static int
all_locations_are_pending (struct bp_location *loc)
{
  for (; loc; loc = loc->next)
    if (!loc->shlib_disabled)
      return 0;
  return 1;
}

/* Subroutine of update_breakpoint_locations to simplify it.
   Return non-zero if multiple fns in list LOC have the same name.
   Null names are ignored.  */

static int
ambiguous_names_p (struct bp_location *loc)
{
  struct bp_location *l;
  htab_t htab = htab_create_alloc (13, htab_hash_string,
				   (int (*) (const void *, const void *)) streq,
				   NULL, xcalloc, xfree);

  for (l = loc; l != NULL; l = l->next)
    {
      const char **slot;
      const char *name = l->function_name;

      /* Allow for some names to be NULL, ignore them.  */
      if (name == NULL)
	continue;

      slot = (const char **) htab_find_slot (htab, (const void *) name,
					     INSERT);
      /* NOTE: We can assume slot != NULL here because xcalloc never returns
	 NULL.  */
      if (*slot != NULL)
	{
	  htab_delete (htab);
	  return 1;
	}
      *slot = name;
    }

  htab_delete (htab);
  return 0;
}

static void
update_breakpoint_locations (struct breakpoint *b,
			     struct symtabs_and_lines sals)
{
  int i;
  char *s;
  struct bp_location *existing_locations = b->loc;

  /* If there's no new locations, and all existing locations
     are pending, don't do anything.  This optimizes
     the common case where all locations are in the same
     shared library, that was unloaded. We'd like to
     retain the location, so that when the library
     is loaded again, we don't loose the enabled/disabled
     status of the individual locations.  */
  if (all_locations_are_pending (existing_locations) && sals.nelts == 0)
    return;

  b->loc = NULL;

  for (i = 0; i < sals.nelts; ++i)
    {
      struct bp_location *new_loc = 
	add_location_to_breakpoint (b, &(sals.sals[i]));

      /* Reparse conditions, they might contain references to the
	 old symtab.  */
      if (b->cond_string != NULL)
	{
	  struct gdb_exception e;

	  s = b->cond_string;
	  TRY_CATCH (e, RETURN_MASK_ERROR)
	    {
	      new_loc->cond = parse_exp_1 (&s, block_for_pc (sals.sals[i].pc), 
					   0);
	    }
	  if (e.reason < 0)
	    {
	      warning (_("failed to reevaluate condition for breakpoint %d: %s"), 
		       b->number, e.message);
	      new_loc->enabled = 0;
	    }
	}

      if (b->source_file != NULL)
	xfree (b->source_file);
      if (sals.sals[i].symtab == NULL)
	b->source_file = NULL;
      else
	b->source_file = xstrdup (sals.sals[i].symtab->filename);

      if (b->line_number == 0)
	b->line_number = sals.sals[i].line;
    }

  /* Update locations of permanent breakpoints.  */
  if (b->enable_state == bp_permanent)
    make_breakpoint_permanent (b);

  /* If possible, carry over 'disable' status from existing breakpoints.  */
  {
    struct bp_location *e = existing_locations;
    /* If there are multiple breakpoints with the same function name,
       e.g. for inline functions, comparing function names won't work.
       Instead compare pc addresses; this is just a heuristic as things
       may have moved, but in practice it gives the correct answer
       often enough until a better solution is found.  */
    int have_ambiguous_names = ambiguous_names_p (b->loc);

    for (; e; e = e->next)
      {
	if (!e->enabled && e->function_name)
	  {
	    struct bp_location *l = b->loc;
	    if (have_ambiguous_names)
	      {
		for (; l; l = l->next)
		  if (e->address == l->address)
		    {
		      l->enabled = 0;
		      break;
		    }
	      }
	    else
	      {
		for (; l; l = l->next)
		  if (l->function_name
		      && strcmp (e->function_name, l->function_name) == 0)
		    {
		      l->enabled = 0;
		      break;
		    }
	      }
	  }
      }
  }

  update_global_location_list (1);
}


/* Reset a breakpoint given it's struct breakpoint * BINT.
   The value we return ends up being the return value from catch_errors.
   Unused in this case.  */

static int
breakpoint_re_set_one (void *bint)
{
  /* get past catch_errs */
  struct breakpoint *b = (struct breakpoint *) bint;
  struct value *mark;
  int i;
  int not_found = 0;
  int *not_found_ptr = &not_found;
  struct symtabs_and_lines sals = {};
  struct symtabs_and_lines expanded;
  char *s;
  enum enable_state save_enable;
  struct gdb_exception e;
  struct cleanup *cleanups;

  switch (b->type)
    {
    case bp_none:
      warning (_("attempted to reset apparently deleted breakpoint #%d?"),
	       b->number);
      return 0;
    case bp_breakpoint:
    case bp_hardware_breakpoint:
    case bp_tracepoint:
      /* Do not attempt to re-set breakpoints disabled during startup.  */
      if (b->enable_state == bp_startup_disabled)
	return 0;

      if (b->addr_string == NULL)
	{
	  /* Anything without a string can't be re-set. */
	  delete_breakpoint (b);
	  return 0;
	}

      set_language (b->language);
      input_radix = b->input_radix;
      s = b->addr_string;
      TRY_CATCH (e, RETURN_MASK_ERROR)
	{
	  sals = decode_line_1 (&s, 1, (struct symtab *) NULL, 0, (char ***) NULL,
				not_found_ptr);
	}
      if (e.reason < 0)
	{
	  int not_found_and_ok = 0;
	  /* For pending breakpoints, it's expected that parsing
	     will fail until the right shared library is loaded.
	     User has already told to create pending breakpoints and
	     don't need extra messages.  If breakpoint is in bp_shlib_disabled
	     state, then user already saw the message about that breakpoint
	     being disabled, and don't want to see more errors.  */
	  if (not_found 
	      && (b->condition_not_parsed 
		  || (b->loc && b->loc->shlib_disabled)
		  || b->enable_state == bp_disabled))
	    not_found_and_ok = 1;

	  if (!not_found_and_ok)
	    {
	      /* We surely don't want to warn about the same breakpoint
		 10 times.  One solution, implemented here, is disable
		 the breakpoint on error.  Another solution would be to
		 have separate 'warning emitted' flag.  Since this
		 happens only when a binary has changed, I don't know
		 which approach is better.  */
	      b->enable_state = bp_disabled;
	      throw_exception (e);
	    }
	}

      if (not_found)
	break;
      
      gdb_assert (sals.nelts == 1);
      resolve_sal_pc (&sals.sals[0]);
      if (b->condition_not_parsed && s && s[0])
	{
	  char *cond_string = 0;
	  int thread = -1;
	  int task = 0;

	  find_condition_and_thread (s, sals.sals[0].pc, 
				     &cond_string, &thread, &task);
	  if (cond_string)
	    b->cond_string = cond_string;
	  b->thread = thread;
	  b->task = task;
	  b->condition_not_parsed = 0;
	}
      expanded = expand_line_sal_maybe (sals.sals[0]);
      cleanups = make_cleanup (xfree, sals.sals);
      update_breakpoint_locations (b, expanded);
      do_cleanups (cleanups);
      break;

    case bp_watchpoint:
    case bp_hardware_watchpoint:
    case bp_read_watchpoint:
    case bp_access_watchpoint:
      /* Watchpoint can be either on expression using entirely global variables,
	 or it can be on local variables.

	 Watchpoints of the first kind are never auto-deleted, and even persist
	 across program restarts. Since they can use variables from shared 
	 libraries, we need to reparse expression as libraries are loaded
	 and unloaded.

	 Watchpoints on local variables can also change meaning as result
	 of solib event. For example, if a watchpoint uses both a local and
	 a global variables in expression, it's a local watchpoint, but
	 unloading of a shared library will make the expression invalid.
	 This is not a very common use case, but we still re-evaluate
	 expression, to avoid surprises to the user. 

	 Note that for local watchpoints, we re-evaluate it only if
	 watchpoints frame id is still valid.  If it's not, it means
	 the watchpoint is out of scope and will be deleted soon. In fact,
	 I'm not sure we'll ever be called in this case.  

	 If a local watchpoint's frame id is still valid, then
	 b->exp_valid_block is likewise valid, and we can safely use it.  
	 
	 Don't do anything about disabled watchpoints, since they will
	 be reevaluated again when enabled.  */
      update_watchpoint (b, 1 /* reparse */);
      break;
      /* We needn't really do anything to reset these, since the mask
         that requests them is unaffected by e.g., new libraries being
         loaded. */
    case bp_catchpoint:
      break;

    default:
      printf_filtered (_("Deleting unknown breakpoint type %d\n"), b->type);
      /* fall through */
      /* Delete overlay event and longjmp master breakpoints; they will be
	 reset later by breakpoint_re_set.  */
    case bp_overlay_event:
    case bp_longjmp_master:
      delete_breakpoint (b);
      break;

      /* This breakpoint is special, it's set up when the inferior
         starts and we really don't want to touch it.  */
    case bp_shlib_event:

      /* Like bp_shlib_event, this breakpoint type is special.
	 Once it is set up, we do not want to touch it.  */
    case bp_thread_event:

      /* Keep temporary breakpoints, which can be encountered when we step
         over a dlopen call and SOLIB_ADD is resetting the breakpoints.
         Otherwise these should have been blown away via the cleanup chain
         or by breakpoint_init_inferior when we rerun the executable.  */
    case bp_until:
    case bp_finish:
    case bp_watchpoint_scope:
    case bp_call_dummy:
    case bp_step_resume:
    case bp_longjmp:
    case bp_longjmp_resume:
    case bp_jit_event:
      break;
    }

  return 0;
}

/* Re-set all breakpoints after symbols have been re-loaded.  */
void
breakpoint_re_set (void)
{
  struct breakpoint *b, *temp;
  enum language save_language;
  int save_input_radix;

  save_language = current_language->la_language;
  save_input_radix = input_radix;
  ALL_BREAKPOINTS_SAFE (b, temp)
  {
    /* Format possible error msg */
    char *message = xstrprintf ("Error in re-setting breakpoint %d: ",
				b->number);
    struct cleanup *cleanups = make_cleanup (xfree, message);
    catch_errors (breakpoint_re_set_one, b, message, RETURN_MASK_ALL);
    do_cleanups (cleanups);
  }
  set_language (save_language);
  input_radix = save_input_radix;

  jit_breakpoint_re_set ();

  create_overlay_event_breakpoint ("_ovly_debug_event");
  create_longjmp_master_breakpoint ("longjmp");
  create_longjmp_master_breakpoint ("_longjmp");
  create_longjmp_master_breakpoint ("siglongjmp");
  create_longjmp_master_breakpoint ("_siglongjmp");
}

/* Reset the thread number of this breakpoint:

   - If the breakpoint is for all threads, leave it as-is.
   - Else, reset it to the current thread for inferior_ptid. */
void
breakpoint_re_set_thread (struct breakpoint *b)
{
  if (b->thread != -1)
    {
      if (in_thread_list (inferior_ptid))
	b->thread = pid_to_thread_id (inferior_ptid);
    }
}

/* Set ignore-count of breakpoint number BPTNUM to COUNT.
   If from_tty is nonzero, it prints a message to that effect,
   which ends with a period (no newline).  */

void
set_ignore_count (int bptnum, int count, int from_tty)
{
  struct breakpoint *b;

  if (count < 0)
    count = 0;

  ALL_BREAKPOINTS (b)
    if (b->number == bptnum)
    {
      b->ignore_count = count;
      if (from_tty)
	{
	  if (count == 0)
	    printf_filtered (_("Will stop next time breakpoint %d is reached."),
			     bptnum);
	  else if (count == 1)
	    printf_filtered (_("Will ignore next crossing of breakpoint %d."),
			     bptnum);
	  else
	    printf_filtered (_("Will ignore next %d crossings of breakpoint %d."),
			     count, bptnum);
	}
      breakpoints_changed ();
      observer_notify_breakpoint_modified (b->number);
      return;
    }

  error (_("No breakpoint number %d."), bptnum);
}

void
make_breakpoint_silent (struct breakpoint *b)
{
  /* Silence the breakpoint.  */
  b->silent = 1;
}

/* Command to set ignore-count of breakpoint N to COUNT.  */

static void
ignore_command (char *args, int from_tty)
{
  char *p = args;
  int num;

  if (p == 0)
    error_no_arg (_("a breakpoint number"));

  num = get_number (&p);
  if (num == 0)
    error (_("bad breakpoint number: '%s'"), args);
  if (*p == 0)
    error (_("Second argument (specified ignore-count) is missing."));

  set_ignore_count (num,
		    longest_to_int (value_as_long (parse_and_eval (p))),
		    from_tty);
  if (from_tty)
    printf_filtered ("\n");
}

/* Call FUNCTION on each of the breakpoints
   whose numbers are given in ARGS.  */

static void
map_breakpoint_numbers (char *args, void (*function) (struct breakpoint *))
{
  char *p = args;
  char *p1;
  int num;
  struct breakpoint *b, *tmp;
  int match;

  if (p == 0)
    error_no_arg (_("one or more breakpoint numbers"));

  while (*p)
    {
      match = 0;
      p1 = p;

      num = get_number_or_range (&p1);
      if (num == 0)
	{
	  warning (_("bad breakpoint number at or near '%s'"), p);
	}
      else
	{
	  ALL_BREAKPOINTS_SAFE (b, tmp)
	    if (b->number == num)
	      {
		struct breakpoint *related_breakpoint = b->related_breakpoint;
		match = 1;
		function (b);
		if (related_breakpoint)
		  function (related_breakpoint);
		break;
	      }
	  if (match == 0)
	    printf_unfiltered (_("No breakpoint number %d.\n"), num);
	}
      p = p1;
    }
}

static struct bp_location *
find_location_by_number (char *number)
{
  char *dot = strchr (number, '.');
  char *p1;
  int bp_num;
  int loc_num;
  struct breakpoint *b;
  struct bp_location *loc;  

  *dot = '\0';

  p1 = number;
  bp_num = get_number_or_range (&p1);
  if (bp_num == 0)
    error (_("Bad breakpoint number '%s'"), number);

  ALL_BREAKPOINTS (b)
    if (b->number == bp_num)
      {
	break;
      }

  if (!b || b->number != bp_num)
    error (_("Bad breakpoint number '%s'"), number);
  
  p1 = dot+1;
  loc_num = get_number_or_range (&p1);
  if (loc_num == 0)
    error (_("Bad breakpoint location number '%s'"), number);

  --loc_num;
  loc = b->loc;
  for (;loc_num && loc; --loc_num, loc = loc->next)
    ;
  if (!loc)
    error (_("Bad breakpoint location number '%s'"), dot+1);
    
  return loc;  
}


/* Set ignore-count of breakpoint number BPTNUM to COUNT.
   If from_tty is nonzero, it prints a message to that effect,
   which ends with a period (no newline).  */

void
disable_breakpoint (struct breakpoint *bpt)
{
  /* Never disable a watchpoint scope breakpoint; we want to
     hit them when we leave scope so we can delete both the
     watchpoint and its scope breakpoint at that time.  */
  if (bpt->type == bp_watchpoint_scope)
    return;

  /* You can't disable permanent breakpoints.  */
  if (bpt->enable_state == bp_permanent)
    return;

  bpt->enable_state = bp_disabled;

  update_global_location_list (0);

  observer_notify_breakpoint_modified (bpt->number);
}

static void
disable_command (char *args, int from_tty)
{
  struct breakpoint *bpt;
  if (args == 0)
    ALL_BREAKPOINTS (bpt)
      switch (bpt->type)
      {
      case bp_none:
	warning (_("attempted to disable apparently deleted breakpoint #%d?"),
		 bpt->number);
	continue;
      case bp_breakpoint:
      case bp_tracepoint:
      case bp_catchpoint:
      case bp_hardware_breakpoint:
      case bp_watchpoint:
      case bp_hardware_watchpoint:
      case bp_read_watchpoint:
      case bp_access_watchpoint:
	disable_breakpoint (bpt);
      default:
	continue;
      }
  else if (strchr (args, '.'))
    {
      struct bp_location *loc = find_location_by_number (args);
      if (loc)
	loc->enabled = 0;
      update_global_location_list (0);
    }
  else
    map_breakpoint_numbers (args, disable_breakpoint);
}

static void
do_enable_breakpoint (struct breakpoint *bpt, enum bpdisp disposition)
{
  int target_resources_ok, other_type_used;
  struct value *mark;

  if (bpt->type == bp_hardware_breakpoint)
    {
      int i;
      i = hw_breakpoint_used_count ();
      target_resources_ok = 
	target_can_use_hardware_watchpoint (bp_hardware_breakpoint, 
					    i + 1, 0);
      if (target_resources_ok == 0)
	error (_("No hardware breakpoint support in the target."));
      else if (target_resources_ok < 0)
	error (_("Hardware breakpoints used exceeds limit."));
    }

  if (bpt->type == bp_watchpoint
      || bpt->type == bp_hardware_watchpoint
      || bpt->type == bp_read_watchpoint
      || bpt->type == bp_access_watchpoint)
    {
      struct gdb_exception e;

      TRY_CATCH (e, RETURN_MASK_ALL)
	{
	  update_watchpoint (bpt, 1 /* reparse */);
	}
      if (e.reason < 0)
	{
	  exception_fprintf (gdb_stderr, e, _("Cannot enable watchpoint %d: "),
			     bpt->number);
	  return;
	}
    }

  if (bpt->enable_state != bp_permanent)
    bpt->enable_state = bp_enabled;
  bpt->disposition = disposition;
  update_global_location_list (1);
  breakpoints_changed ();
  
  observer_notify_breakpoint_modified (bpt->number);
}


void
enable_breakpoint (struct breakpoint *bpt)
{
  do_enable_breakpoint (bpt, bpt->disposition);
}

/* The enable command enables the specified breakpoints (or all defined
   breakpoints) so they once again become (or continue to be) effective
   in stopping the inferior.  */

static void
enable_command (char *args, int from_tty)
{
  struct breakpoint *bpt;
  if (args == 0)
    ALL_BREAKPOINTS (bpt)
      switch (bpt->type)
      {
      case bp_none:
	warning (_("attempted to enable apparently deleted breakpoint #%d?"),
		 bpt->number);
	continue;
      case bp_breakpoint:
      case bp_tracepoint:
      case bp_catchpoint:
      case bp_hardware_breakpoint:
      case bp_watchpoint:
      case bp_hardware_watchpoint:
      case bp_read_watchpoint:
      case bp_access_watchpoint:
	enable_breakpoint (bpt);
      default:
	continue;
      }
  else if (strchr (args, '.'))
    {
      struct bp_location *loc = find_location_by_number (args);
      if (loc)
	loc->enabled = 1;
      update_global_location_list (1);
    }
  else
    map_breakpoint_numbers (args, enable_breakpoint);
}

static void
enable_once_breakpoint (struct breakpoint *bpt)
{
  do_enable_breakpoint (bpt, disp_disable);
}

static void
enable_once_command (char *args, int from_tty)
{
  map_breakpoint_numbers (args, enable_once_breakpoint);
}

static void
enable_delete_breakpoint (struct breakpoint *bpt)
{
  do_enable_breakpoint (bpt, disp_del);
}

static void
enable_delete_command (char *args, int from_tty)
{
  map_breakpoint_numbers (args, enable_delete_breakpoint);
}

static void
set_breakpoint_cmd (char *args, int from_tty)
{
}

static void
show_breakpoint_cmd (char *args, int from_tty)
{
}

/* Use default_breakpoint_'s, or nothing if they aren't valid.  */

struct symtabs_and_lines
decode_line_spec_1 (char *string, int funfirstline)
{
  struct symtabs_and_lines sals;
  if (string == 0)
    error (_("Empty line specification."));
  if (default_breakpoint_valid)
    sals = decode_line_1 (&string, funfirstline,
			  default_breakpoint_symtab,
			  default_breakpoint_line,
			  (char ***) NULL, NULL);
  else
    sals = decode_line_1 (&string, funfirstline,
			  (struct symtab *) NULL, 0, (char ***) NULL, NULL);
  if (*string)
    error (_("Junk at end of line specification: %s"), string);
  return sals;
}

/* Create and insert a raw software breakpoint at PC.  Return an
   identifier, which should be used to remove the breakpoint later.
   In general, places which call this should be using something on the
   breakpoint chain instead; this function should be eliminated
   someday.  */

void *
deprecated_insert_raw_breakpoint (struct gdbarch *gdbarch, CORE_ADDR pc)
{
  struct bp_target_info *bp_tgt;

  bp_tgt = xmalloc (sizeof (struct bp_target_info));
  memset (bp_tgt, 0, sizeof (struct bp_target_info));

  bp_tgt->placed_address = pc;
  if (target_insert_breakpoint (gdbarch, bp_tgt) != 0)
    {
      /* Could not insert the breakpoint.  */
      xfree (bp_tgt);
      return NULL;
    }

  return bp_tgt;
}

/* Remove a breakpoint BP inserted by deprecated_insert_raw_breakpoint.  */

int
deprecated_remove_raw_breakpoint (struct gdbarch *gdbarch, void *bp)
{
  struct bp_target_info *bp_tgt = bp;
  int ret;

  ret = target_remove_breakpoint (gdbarch, bp_tgt);
  xfree (bp_tgt);

  return ret;
}

/* One (or perhaps two) breakpoints used for software single stepping.  */

static void *single_step_breakpoints[2];
static struct gdbarch *single_step_gdbarch[2];

/* Create and insert a breakpoint for software single step.  */

void
insert_single_step_breakpoint (struct gdbarch *gdbarch, CORE_ADDR next_pc)
{
  void **bpt_p;

  if (single_step_breakpoints[0] == NULL)
    {
      bpt_p = &single_step_breakpoints[0];
      single_step_gdbarch[0] = gdbarch;
    }
  else
    {
      gdb_assert (single_step_breakpoints[1] == NULL);
      bpt_p = &single_step_breakpoints[1];
      single_step_gdbarch[1] = gdbarch;
    }

  /* NOTE drow/2006-04-11: A future improvement to this function would be
     to only create the breakpoints once, and actually put them on the
     breakpoint chain.  That would let us use set_raw_breakpoint.  We could
     adjust the addresses each time they were needed.  Doing this requires
     corresponding changes elsewhere where single step breakpoints are
     handled, however.  So, for now, we use this.  */

  *bpt_p = deprecated_insert_raw_breakpoint (gdbarch, next_pc);
  if (*bpt_p == NULL)
    error (_("Could not insert single-step breakpoint at %s"),
	     paddress (gdbarch, next_pc));
}

/* Remove and delete any breakpoints used for software single step.  */

void
remove_single_step_breakpoints (void)
{
  gdb_assert (single_step_breakpoints[0] != NULL);

  /* See insert_single_step_breakpoint for more about this deprecated
     call.  */
  deprecated_remove_raw_breakpoint (single_step_gdbarch[0],
				    single_step_breakpoints[0]);
  single_step_gdbarch[0] = NULL;
  single_step_breakpoints[0] = NULL;

  if (single_step_breakpoints[1] != NULL)
    {
      deprecated_remove_raw_breakpoint (single_step_gdbarch[1],
					single_step_breakpoints[1]);
      single_step_gdbarch[1] = NULL;
      single_step_breakpoints[1] = NULL;
    }
}

/* Check whether a software single-step breakpoint is inserted at PC.  */

static int
single_step_breakpoint_inserted_here_p (CORE_ADDR pc)
{
  int i;

  for (i = 0; i < 2; i++)
    {
      struct bp_target_info *bp_tgt = single_step_breakpoints[i];
      if (bp_tgt && bp_tgt->placed_address == pc)
	return 1;
    }

  return 0;
}

/* Returns 0 if 'bp' is NOT a syscall catchpoint,
   non-zero otherwise.  */
static int
is_syscall_catchpoint_enabled (struct breakpoint *bp)
{
  if (syscall_catchpoint_p (bp)
      && bp->enable_state != bp_disabled
      && bp->enable_state != bp_call_disabled)
    return 1;
  else
    return 0;
}

int
catch_syscall_enabled (void)
{
  struct inferior *inf = current_inferior ();

  return inf->total_syscalls_count != 0;
}

int
catching_syscall_number (int syscall_number)
{
  struct breakpoint *bp;

  ALL_BREAKPOINTS (bp)
    if (is_syscall_catchpoint_enabled (bp))
      {
	if (bp->syscalls_to_be_caught)
	  {
            int i, iter;
            for (i = 0;
                 VEC_iterate (int, bp->syscalls_to_be_caught, i, iter);
                 i++)
	      if (syscall_number == iter)
		return 1;
	  }
	else
	  return 1;
      }

  return 0;
}

/* Complete syscall names.  Used by "catch syscall".  */
static char **
catch_syscall_completer (struct cmd_list_element *cmd,
                         char *text, char *word)
{
  const char **list = get_syscall_names ();
  return (list == NULL) ? NULL : complete_on_enum (list, text, word);
}

/* Tracepoint-specific operations.  */

/* Set tracepoint count to NUM.  */
static void
set_tracepoint_count (int num)
{
  tracepoint_count = num;
  set_internalvar_integer (lookup_internalvar ("tpnum"), num);
}

void
trace_command (char *arg, int from_tty)
{
  break_command_really (get_current_arch (),
			arg,
			NULL, 0, 1 /* parse arg */,
			0 /* tempflag */, 0 /* hardwareflag */,
			1 /* traceflag */,
			0 /* Ignore count */,
			pending_break_support, 
			NULL,
			from_tty,
			1 /* enabled */);
  set_tracepoint_count (breakpoint_count);
}

/* Print information on tracepoint number TPNUM_EXP, or all if
   omitted.  */

static void
tracepoints_info (char *tpnum_exp, int from_tty)
{
  struct breakpoint *b;
  int tps_to_list = 0;

  /* In the no-arguments case, say "No tracepoints" if none found.  */
  if (tpnum_exp == 0)
    {
      ALL_TRACEPOINTS (b)
      {
	if (b->number >= 0)
	  {
	    tps_to_list = 1;
	    break;
	  }
      }
      if (!tps_to_list)
	{
	  ui_out_message (uiout, 0, "No tracepoints.\n");
	  return;
	}
    }

  /* Otherwise be the same as "info break".  */
  breakpoints_info (tpnum_exp, from_tty);
}

/* The 'enable trace' command enables tracepoints.  
   Not supported by all targets.  */
static void
enable_trace_command (char *args, int from_tty)
{
  enable_command (args, from_tty);
}

/* The 'disable trace' command disables tracepoints.  
   Not supported by all targets.  */
static void
disable_trace_command (char *args, int from_tty)
{
  disable_command (args, from_tty);
}

/* Remove a tracepoint (or all if no argument) */
static void
delete_trace_command (char *arg, int from_tty)
{
  struct breakpoint *b, *temp;

  dont_repeat ();

  if (arg == 0)
    {
      int breaks_to_delete = 0;

      /* Delete all breakpoints if no argument.
         Do not delete internal or call-dummy breakpoints, these
         have to be deleted with an explicit breakpoint number argument.  */
      ALL_TRACEPOINTS (b)
      {
	if (b->number >= 0)
	  {
	    breaks_to_delete = 1;
	    break;
	  }
      }

      /* Ask user only if there are some breakpoints to delete.  */
      if (!from_tty
	  || (breaks_to_delete && query (_("Delete all tracepoints? "))))
	{
	  ALL_BREAKPOINTS_SAFE (b, temp)
	  {
	    if (b->type == bp_tracepoint
		&& b->number >= 0)
	      delete_breakpoint (b);
	  }
	}
    }
  else
    map_breakpoint_numbers (arg, delete_breakpoint);
}

/* Set passcount for tracepoint.

   First command argument is passcount, second is tracepoint number.
   If tracepoint number omitted, apply to most recently defined.
   Also accepts special argument "all".  */

static void
trace_pass_command (char *args, int from_tty)
{
  struct breakpoint *t1 = (struct breakpoint *) -1, *t2;
  unsigned int count;
  int all = 0;

  if (args == 0 || *args == 0)
    error (_("passcount command requires an argument (count + optional TP num)"));

  count = strtoul (args, &args, 10);	/* Count comes first, then TP num. */

  while (*args && isspace ((int) *args))
    args++;

  if (*args && strncasecmp (args, "all", 3) == 0)
    {
      args += 3;			/* Skip special argument "all".  */
      all = 1;
      if (*args)
	error (_("Junk at end of arguments."));
    }
  else
    t1 = get_tracepoint_by_number (&args, 1, 1);

  do
    {
      if (t1)
	{
	  ALL_TRACEPOINTS (t2)
	    if (t1 == (struct breakpoint *) -1 || t1 == t2)
	      {
		t2->pass_count = count;
		observer_notify_tracepoint_modified (t2->number);
		if (from_tty)
		  printf_filtered (_("Setting tracepoint %d's passcount to %d\n"),
				   t2->number, count);
	      }
	  if (! all && *args)
	    t1 = get_tracepoint_by_number (&args, 1, 0);
	}
    }
  while (*args);
}

struct breakpoint *
get_tracepoint (int num)
{
  struct breakpoint *t;

  ALL_TRACEPOINTS (t)
    if (t->number == num)
      return t;

  return NULL;
}

/* Utility: parse a tracepoint number and look it up in the list.
   If MULTI_P is true, there might be a range of tracepoints in ARG.
   if OPTIONAL_P is true, then if the argument is missing, the most
   recent tracepoint (tracepoint_count) is returned.  */
struct breakpoint *
get_tracepoint_by_number (char **arg, int multi_p, int optional_p)
{
  extern int tracepoint_count;
  struct breakpoint *t;
  int tpnum;
  char *instring = arg == NULL ? NULL : *arg;

  if (arg == NULL || *arg == NULL || ! **arg)
    {
      if (optional_p)
	tpnum = tracepoint_count;
      else
	error_no_arg (_("tracepoint number"));
    }
  else
    tpnum = multi_p ? get_number_or_range (arg) : get_number (arg);

  if (tpnum <= 0)
    {
      if (instring && *instring)
	printf_filtered (_("bad tracepoint number at or near '%s'\n"), 
			 instring);
      else
	printf_filtered (_("Tracepoint argument missing and no previous tracepoint\n"));
      return NULL;
    }

  ALL_TRACEPOINTS (t)
    if (t->number == tpnum)
    {
      return t;
    }

  /* FIXME: if we are in the middle of a range we don't want to give
     a message.  The current interface to get_number_or_range doesn't
     allow us to discover this.  */
  printf_unfiltered ("No tracepoint number %d.\n", tpnum);
  return NULL;
}

/* save-tracepoints command */
static void
tracepoint_save_command (char *args, int from_tty)
{
  struct breakpoint *tp;
  int any_tp = 0;
  struct action_line *line;
  FILE *fp;
  char *i1 = "    ", *i2 = "      ";
  char *indent, *actionline, *pathname;
  char tmp[40];
  struct cleanup *cleanup;

  if (args == 0 || *args == 0)
    error (_("Argument required (file name in which to save tracepoints)"));

  /* See if we have anything to save.  */
  ALL_TRACEPOINTS (tp)
  {
    any_tp = 1;
    break;
  }
  if (!any_tp)
    {
      warning (_("save-tracepoints: no tracepoints to save."));
      return;
    }

  pathname = tilde_expand (args);
  cleanup = make_cleanup (xfree, pathname);
  fp = fopen (pathname, "w");
  if (!fp)
    error (_("Unable to open file '%s' for saving tracepoints (%s)"),
	   args, safe_strerror (errno));
  make_cleanup_fclose (fp);
  
  ALL_TRACEPOINTS (tp)
  {
    if (tp->addr_string)
      fprintf (fp, "trace %s\n", tp->addr_string);
    else
      {
	sprintf_vma (tmp, tp->loc->address);
	fprintf (fp, "trace *0x%s\n", tmp);
      }

    if (tp->pass_count)
      fprintf (fp, "  passcount %d\n", tp->pass_count);

    if (tp->actions)
      {
	fprintf (fp, "  actions\n");
	indent = i1;
	for (line = tp->actions; line; line = line->next)
	  {
	    struct cmd_list_element *cmd;

	    QUIT;		/* allow user to bail out with ^C */
	    actionline = line->action;
	    while (isspace ((int) *actionline))
	      actionline++;

	    fprintf (fp, "%s%s\n", indent, actionline);
	    if (*actionline != '#')	/* skip for comment lines */
	      {
		cmd = lookup_cmd (&actionline, cmdlist, "", -1, 1);
		if (cmd == 0)
		  error (_("Bad action list item: %s"), actionline);
		if (cmd_cfunc_eq (cmd, while_stepping_pseudocommand))
		  indent = i2;
		else if (cmd_cfunc_eq (cmd, end_actions_pseudocommand))
		  indent = i1;
	      }
	  }
      }
  }
  do_cleanups (cleanup);
  if (from_tty)
    printf_filtered (_("Tracepoints saved to file '%s'.\n"), args);
  return;
}

/* Create a vector of all tracepoints.  */

VEC(breakpoint_p) *
all_tracepoints ()
{
  VEC(breakpoint_p) *tp_vec = 0;
  struct breakpoint *tp;

  ALL_TRACEPOINTS (tp)
  {
    VEC_safe_push (breakpoint_p, tp_vec, tp);
  }

  return tp_vec;
}


/* This help string is used for the break, hbreak, tbreak and thbreak commands.
   It is defined as a macro to prevent duplication.
   COMMAND should be a string constant containing the name of the command.  */
#define BREAK_ARGS_HELP(command) \
command" [LOCATION] [thread THREADNUM] [if CONDITION]\n\
LOCATION may be a line number, function name, or \"*\" and an address.\n\
If a line number is specified, break at start of code for that line.\n\
If a function is specified, break at start of code for that function.\n\
If an address is specified, break at that exact address.\n\
With no LOCATION, uses current execution address of selected stack frame.\n\
This is useful for breaking on return to a stack frame.\n\
\n\
THREADNUM is the number from \"info threads\".\n\
CONDITION is a boolean expression.\n\
\n\
Multiple breakpoints at one place are permitted, and useful if conditional.\n\
\n\
Do \"help breakpoints\" for info on other commands dealing with breakpoints."

/* List of subcommands for "catch".  */
static struct cmd_list_element *catch_cmdlist;

/* List of subcommands for "tcatch".  */
static struct cmd_list_element *tcatch_cmdlist;

/* Like add_cmd, but add the command to both the "catch" and "tcatch"
   lists, and pass some additional user data to the command function.  */
static void
add_catch_command (char *name, char *docstring,
		   void (*sfunc) (char *args, int from_tty,
				  struct cmd_list_element *command),
                   char **(*completer) (struct cmd_list_element *cmd,
                                         char *text, char *word),
		   void *user_data_catch,
		   void *user_data_tcatch)
{
  struct cmd_list_element *command;

  command = add_cmd (name, class_breakpoint, NULL, docstring,
		     &catch_cmdlist);
  set_cmd_sfunc (command, sfunc);
  set_cmd_context (command, user_data_catch);
  set_cmd_completer (command, completer);

  command = add_cmd (name, class_breakpoint, NULL, docstring,
		     &tcatch_cmdlist);
  set_cmd_sfunc (command, sfunc);
  set_cmd_context (command, user_data_tcatch);
  set_cmd_completer (command, completer);
}

void
_initialize_breakpoint (void)
{
  static struct cmd_list_element *breakpoint_set_cmdlist;
  static struct cmd_list_element *breakpoint_show_cmdlist;
  struct cmd_list_element *c;

  observer_attach_solib_unloaded (disable_breakpoints_in_unloaded_shlib);

  breakpoint_chain = 0;
  /* Don't bother to call set_breakpoint_count.  $bpnum isn't useful
     before a breakpoint is set.  */
  breakpoint_count = 0;

  tracepoint_count = 0;

  add_com ("ignore", class_breakpoint, ignore_command, _("\
Set ignore-count of breakpoint number N to COUNT.\n\
Usage is `ignore N COUNT'."));
  if (xdb_commands)
    add_com_alias ("bc", "ignore", class_breakpoint, 1);

  add_com ("commands", class_breakpoint, commands_command, _("\
Set commands to be executed when a breakpoint is hit.\n\
Give breakpoint number as argument after \"commands\".\n\
With no argument, the targeted breakpoint is the last one set.\n\
The commands themselves follow starting on the next line.\n\
Type a line containing \"end\" to indicate the end of them.\n\
Give \"silent\" as the first line to make the breakpoint silent;\n\
then no output is printed when it is hit, except what the commands print."));

  add_com ("condition", class_breakpoint, condition_command, _("\
Specify breakpoint number N to break only if COND is true.\n\
Usage is `condition N COND', where N is an integer and COND is an\n\
expression to be evaluated whenever breakpoint N is reached."));

  c = add_com ("tbreak", class_breakpoint, tbreak_command, _("\
Set a temporary breakpoint.\n\
Like \"break\" except the breakpoint is only temporary,\n\
so it will be deleted when hit.  Equivalent to \"break\" followed\n\
by using \"enable delete\" on the breakpoint number.\n\
\n"
BREAK_ARGS_HELP ("tbreak")));
  set_cmd_completer (c, location_completer);

  c = add_com ("hbreak", class_breakpoint, hbreak_command, _("\
Set a hardware assisted  breakpoint.\n\
Like \"break\" except the breakpoint requires hardware support,\n\
some target hardware may not have this support.\n\
\n"
BREAK_ARGS_HELP ("hbreak")));
  set_cmd_completer (c, location_completer);

  c = add_com ("thbreak", class_breakpoint, thbreak_command, _("\
Set a temporary hardware assisted breakpoint.\n\
Like \"hbreak\" except the breakpoint is only temporary,\n\
so it will be deleted when hit.\n\
\n"
BREAK_ARGS_HELP ("thbreak")));
  set_cmd_completer (c, location_completer);

  add_prefix_cmd ("enable", class_breakpoint, enable_command, _("\
Enable some breakpoints.\n\
Give breakpoint numbers (separated by spaces) as arguments.\n\
With no subcommand, breakpoints are enabled until you command otherwise.\n\
This is used to cancel the effect of the \"disable\" command.\n\
With a subcommand you can enable temporarily."),
		  &enablelist, "enable ", 1, &cmdlist);
  if (xdb_commands)
    add_com ("ab", class_breakpoint, enable_command, _("\
Enable some breakpoints.\n\
Give breakpoint numbers (separated by spaces) as arguments.\n\
With no subcommand, breakpoints are enabled until you command otherwise.\n\
This is used to cancel the effect of the \"disable\" command.\n\
With a subcommand you can enable temporarily."));

  add_com_alias ("en", "enable", class_breakpoint, 1);

  add_abbrev_prefix_cmd ("breakpoints", class_breakpoint, enable_command, _("\
Enable some breakpoints.\n\
Give breakpoint numbers (separated by spaces) as arguments.\n\
This is used to cancel the effect of the \"disable\" command.\n\
May be abbreviated to simply \"enable\".\n"),
		   &enablebreaklist, "enable breakpoints ", 1, &enablelist);

  add_cmd ("once", no_class, enable_once_command, _("\
Enable breakpoints for one hit.  Give breakpoint numbers.\n\
If a breakpoint is hit while enabled in this fashion, it becomes disabled."),
	   &enablebreaklist);

  add_cmd ("delete", no_class, enable_delete_command, _("\
Enable breakpoints and delete when hit.  Give breakpoint numbers.\n\
If a breakpoint is hit while enabled in this fashion, it is deleted."),
	   &enablebreaklist);

  add_cmd ("delete", no_class, enable_delete_command, _("\
Enable breakpoints and delete when hit.  Give breakpoint numbers.\n\
If a breakpoint is hit while enabled in this fashion, it is deleted."),
	   &enablelist);

  add_cmd ("once", no_class, enable_once_command, _("\
Enable breakpoints for one hit.  Give breakpoint numbers.\n\
If a breakpoint is hit while enabled in this fashion, it becomes disabled."),
	   &enablelist);

  add_prefix_cmd ("disable", class_breakpoint, disable_command, _("\
Disable some breakpoints.\n\
Arguments are breakpoint numbers with spaces in between.\n\
To disable all breakpoints, give no argument.\n\
A disabled breakpoint is not forgotten, but has no effect until reenabled."),
		  &disablelist, "disable ", 1, &cmdlist);
  add_com_alias ("dis", "disable", class_breakpoint, 1);
  add_com_alias ("disa", "disable", class_breakpoint, 1);
  if (xdb_commands)
    add_com ("sb", class_breakpoint, disable_command, _("\
Disable some breakpoints.\n\
Arguments are breakpoint numbers with spaces in between.\n\
To disable all breakpoints, give no argument.\n\
A disabled breakpoint is not forgotten, but has no effect until reenabled."));

  add_cmd ("breakpoints", class_alias, disable_command, _("\
Disable some breakpoints.\n\
Arguments are breakpoint numbers with spaces in between.\n\
To disable all breakpoints, give no argument.\n\
A disabled breakpoint is not forgotten, but has no effect until reenabled.\n\
This command may be abbreviated \"disable\"."),
	   &disablelist);

  add_prefix_cmd ("delete", class_breakpoint, delete_command, _("\
Delete some breakpoints or auto-display expressions.\n\
Arguments are breakpoint numbers with spaces in between.\n\
To delete all breakpoints, give no argument.\n\
\n\
Also a prefix command for deletion of other GDB objects.\n\
The \"unset\" command is also an alias for \"delete\"."),
		  &deletelist, "delete ", 1, &cmdlist);
  add_com_alias ("d", "delete", class_breakpoint, 1);
  add_com_alias ("del", "delete", class_breakpoint, 1);
  if (xdb_commands)
    add_com ("db", class_breakpoint, delete_command, _("\
Delete some breakpoints.\n\
Arguments are breakpoint numbers with spaces in between.\n\
To delete all breakpoints, give no argument.\n"));

  add_cmd ("breakpoints", class_alias, delete_command, _("\
Delete some breakpoints or auto-display expressions.\n\
Arguments are breakpoint numbers with spaces in between.\n\
To delete all breakpoints, give no argument.\n\
This command may be abbreviated \"delete\"."),
	   &deletelist);

  add_com ("clear", class_breakpoint, clear_command, _("\
Clear breakpoint at specified line or function.\n\
Argument may be line number, function name, or \"*\" and an address.\n\
If line number is specified, all breakpoints in that line are cleared.\n\
If function is specified, breakpoints at beginning of function are cleared.\n\
If an address is specified, breakpoints at that address are cleared.\n\
\n\
With no argument, clears all breakpoints in the line that the selected frame\n\
is executing in.\n\
\n\
See also the \"delete\" command which clears breakpoints by number."));

  c = add_com ("break", class_breakpoint, break_command, _("\
Set breakpoint at specified line or function.\n"
BREAK_ARGS_HELP ("break")));
  set_cmd_completer (c, location_completer);

  add_com_alias ("b", "break", class_run, 1);
  add_com_alias ("br", "break", class_run, 1);
  add_com_alias ("bre", "break", class_run, 1);
  add_com_alias ("brea", "break", class_run, 1);

  if (xdb_commands)
   add_com_alias ("ba", "break", class_breakpoint, 1);

  if (dbx_commands)
    {
      add_abbrev_prefix_cmd ("stop", class_breakpoint, stop_command, _("\
Break in function/address or break at a line in the current file."),
			     &stoplist, "stop ", 1, &cmdlist);
      add_cmd ("in", class_breakpoint, stopin_command,
	       _("Break in function or address."), &stoplist);
      add_cmd ("at", class_breakpoint, stopat_command,
	       _("Break at a line in the current file."), &stoplist);
      add_com ("status", class_info, breakpoints_info, _("\
Status of user-settable breakpoints, or breakpoint number NUMBER.\n\
The \"Type\" column indicates one of:\n\
\tbreakpoint     - normal breakpoint\n\
\twatchpoint     - watchpoint\n\
The \"Disp\" column contains one of \"keep\", \"del\", or \"dis\" to indicate\n\
the disposition of the breakpoint after it gets hit.  \"dis\" means that the\n\
breakpoint will be disabled.  The \"Address\" and \"What\" columns indicate the\n\
address and file/line number respectively.\n\
\n\
Convenience variable \"$_\" and default examine address for \"x\"\n\
are set to the address of the last breakpoint listed unless the command\n\
is prefixed with \"server \".\n\n\
Convenience variable \"$bpnum\" contains the number of the last\n\
breakpoint set."));
    }

  add_info ("breakpoints", breakpoints_info, _("\
Status of user-settable breakpoints, or breakpoint number NUMBER.\n\
The \"Type\" column indicates one of:\n\
\tbreakpoint     - normal breakpoint\n\
\twatchpoint     - watchpoint\n\
The \"Disp\" column contains one of \"keep\", \"del\", or \"dis\" to indicate\n\
the disposition of the breakpoint after it gets hit.  \"dis\" means that the\n\
breakpoint will be disabled.  The \"Address\" and \"What\" columns indicate the\n\
address and file/line number respectively.\n\
\n\
Convenience variable \"$_\" and default examine address for \"x\"\n\
are set to the address of the last breakpoint listed unless the command\n\
is prefixed with \"server \".\n\n\
Convenience variable \"$bpnum\" contains the number of the last\n\
breakpoint set."));

  if (xdb_commands)
    add_com ("lb", class_breakpoint, breakpoints_info, _("\
Status of user-settable breakpoints, or breakpoint number NUMBER.\n\
The \"Type\" column indicates one of:\n\
\tbreakpoint     - normal breakpoint\n\
\twatchpoint     - watchpoint\n\
The \"Disp\" column contains one of \"keep\", \"del\", or \"dis\" to indicate\n\
the disposition of the breakpoint after it gets hit.  \"dis\" means that the\n\
breakpoint will be disabled.  The \"Address\" and \"What\" columns indicate the\n\
address and file/line number respectively.\n\
\n\
Convenience variable \"$_\" and default examine address for \"x\"\n\
are set to the address of the last breakpoint listed unless the command\n\
is prefixed with \"server \".\n\n\
Convenience variable \"$bpnum\" contains the number of the last\n\
breakpoint set."));

  add_cmd ("breakpoints", class_maintenance, maintenance_info_breakpoints, _("\
Status of all breakpoints, or breakpoint number NUMBER.\n\
The \"Type\" column indicates one of:\n\
\tbreakpoint     - normal breakpoint\n\
\twatchpoint     - watchpoint\n\
\tlongjmp        - internal breakpoint used to step through longjmp()\n\
\tlongjmp resume - internal breakpoint at the target of longjmp()\n\
\tuntil          - internal breakpoint used by the \"until\" command\n\
\tfinish         - internal breakpoint used by the \"finish\" command\n\
The \"Disp\" column contains one of \"keep\", \"del\", or \"dis\" to indicate\n\
the disposition of the breakpoint after it gets hit.  \"dis\" means that the\n\
breakpoint will be disabled.  The \"Address\" and \"What\" columns indicate the\n\
address and file/line number respectively.\n\
\n\
Convenience variable \"$_\" and default examine address for \"x\"\n\
are set to the address of the last breakpoint listed unless the command\n\
is prefixed with \"server \".\n\n\
Convenience variable \"$bpnum\" contains the number of the last\n\
breakpoint set."),
	   &maintenanceinfolist);

  add_prefix_cmd ("catch", class_breakpoint, catch_command, _("\
Set catchpoints to catch events."),
		  &catch_cmdlist, "catch ",
		  0/*allow-unknown*/, &cmdlist);

  add_prefix_cmd ("tcatch", class_breakpoint, tcatch_command, _("\
Set temporary catchpoints to catch events."),
		  &tcatch_cmdlist, "tcatch ",
		  0/*allow-unknown*/, &cmdlist);

  /* Add catch and tcatch sub-commands.  */
  add_catch_command ("catch", _("\
Catch an exception, when caught.\n\
With an argument, catch only exceptions with the given name."),
		     catch_catch_command,
                     NULL,
		     CATCH_PERMANENT,
		     CATCH_TEMPORARY);
  add_catch_command ("throw", _("\
Catch an exception, when thrown.\n\
With an argument, catch only exceptions with the given name."),
		     catch_throw_command,
                     NULL,
		     CATCH_PERMANENT,
		     CATCH_TEMPORARY);
  add_catch_command ("fork", _("Catch calls to fork."),
		     catch_fork_command_1,
                     NULL,
		     (void *) (uintptr_t) catch_fork_permanent,
		     (void *) (uintptr_t) catch_fork_temporary);
  add_catch_command ("vfork", _("Catch calls to vfork."),
		     catch_fork_command_1,
                     NULL,
		     (void *) (uintptr_t) catch_vfork_permanent,
		     (void *) (uintptr_t) catch_vfork_temporary);
  add_catch_command ("exec", _("Catch calls to exec."),
		     catch_exec_command_1,
                     NULL,
		     CATCH_PERMANENT,
		     CATCH_TEMPORARY);
  add_catch_command ("syscall", _("\
Catch system calls by their names and/or numbers.\n\
Arguments say which system calls to catch.  If no arguments\n\
are given, every system call will be caught.\n\
Arguments, if given, should be one or more system call names\n\
(if your system supports that), or system call numbers."),
		     catch_syscall_command_1,
		     catch_syscall_completer,
		     CATCH_PERMANENT,
		     CATCH_TEMPORARY);
  add_catch_command ("exception", _("\
Catch Ada exceptions, when raised.\n\
With an argument, catch only exceptions with the given name."),
		     catch_ada_exception_command,
                     NULL,
		     CATCH_PERMANENT,
		     CATCH_TEMPORARY);
  add_catch_command ("assert", _("\
Catch failed Ada assertions, when raised.\n\
With an argument, catch only exceptions with the given name."),
		     catch_assert_command,
                     NULL,
		     CATCH_PERMANENT,
		     CATCH_TEMPORARY);

  c = add_com ("watch", class_breakpoint, watch_command, _("\
Set a watchpoint for an expression.\n\
A watchpoint stops execution of your program whenever the value of\n\
an expression changes."));
  set_cmd_completer (c, expression_completer);

  c = add_com ("rwatch", class_breakpoint, rwatch_command, _("\
Set a read watchpoint for an expression.\n\
A watchpoint stops execution of your program whenever the value of\n\
an expression is read."));
  set_cmd_completer (c, expression_completer);

  c = add_com ("awatch", class_breakpoint, awatch_command, _("\
Set a watchpoint for an expression.\n\
A watchpoint stops execution of your program whenever the value of\n\
an expression is either read or written."));
  set_cmd_completer (c, expression_completer);

  add_info ("watchpoints", breakpoints_info,
	    _("Synonym for ``info breakpoints''."));


  /* XXX: cagney/2005-02-23: This should be a boolean, and should
     respond to changes - contrary to the description.  */
  add_setshow_zinteger_cmd ("can-use-hw-watchpoints", class_support,
			    &can_use_hw_watchpoints, _("\
Set debugger's willingness to use watchpoint hardware."), _("\
Show debugger's willingness to use watchpoint hardware."), _("\
If zero, gdb will not use hardware for new watchpoints, even if\n\
such is available.  (However, any hardware watchpoints that were\n\
created before setting this to nonzero, will continue to use watchpoint\n\
hardware.)"),
			    NULL,
			    show_can_use_hw_watchpoints,
			    &setlist, &showlist);

  can_use_hw_watchpoints = 1;

  /* Tracepoint manipulation commands.  */

  c = add_com ("trace", class_breakpoint, trace_command, _("\
Set a tracepoint at specified line or function.\n\
\n"
BREAK_ARGS_HELP ("trace") "\n\
Do \"help tracepoints\" for info on other tracepoint commands."));
  set_cmd_completer (c, location_completer);

  add_com_alias ("tp", "trace", class_alias, 0);
  add_com_alias ("tr", "trace", class_alias, 1);
  add_com_alias ("tra", "trace", class_alias, 1);
  add_com_alias ("trac", "trace", class_alias, 1);

  add_info ("tracepoints", tracepoints_info, _("\
Status of tracepoints, or tracepoint number NUMBER.\n\
Convenience variable \"$tpnum\" contains the number of the\n\
last tracepoint set."));

  add_info_alias ("tp", "tracepoints", 1);

  add_cmd ("tracepoints", class_trace, delete_trace_command, _("\
Delete specified tracepoints.\n\
Arguments are tracepoint numbers, separated by spaces.\n\
No argument means delete all tracepoints."),
	   &deletelist);

  c = add_cmd ("tracepoints", class_trace, disable_trace_command, _("\
Disable specified tracepoints.\n\
Arguments are tracepoint numbers, separated by spaces.\n\
No argument means disable all tracepoints."),
	   &disablelist);
  deprecate_cmd (c, "disable");

  c = add_cmd ("tracepoints", class_trace, enable_trace_command, _("\
Enable specified tracepoints.\n\
Arguments are tracepoint numbers, separated by spaces.\n\
No argument means enable all tracepoints."),
	   &enablelist);
  deprecate_cmd (c, "enable");

  add_com ("passcount", class_trace, trace_pass_command, _("\
Set the passcount for a tracepoint.\n\
The trace will end when the tracepoint has been passed 'count' times.\n\
Usage: passcount COUNT TPNUM, where TPNUM may also be \"all\";\n\
if TPNUM is omitted, passcount refers to the last tracepoint defined."));

  c = add_com ("save-tracepoints", class_trace, tracepoint_save_command, _("\
Save current tracepoint definitions as a script.\n\
Use the 'source' command in another debug session to restore them."));
  set_cmd_completer (c, filename_completer);

  add_prefix_cmd ("breakpoint", class_maintenance, set_breakpoint_cmd, _("\
Breakpoint specific settings\n\
Configure various breakpoint-specific variables such as\n\
pending breakpoint behavior"),
		  &breakpoint_set_cmdlist, "set breakpoint ",
		  0/*allow-unknown*/, &setlist);
  add_prefix_cmd ("breakpoint", class_maintenance, show_breakpoint_cmd, _("\
Breakpoint specific settings\n\
Configure various breakpoint-specific variables such as\n\
pending breakpoint behavior"),
		  &breakpoint_show_cmdlist, "show breakpoint ",
		  0/*allow-unknown*/, &showlist);

  add_setshow_auto_boolean_cmd ("pending", no_class,
				&pending_break_support, _("\
Set debugger's behavior regarding pending breakpoints."), _("\
Show debugger's behavior regarding pending breakpoints."), _("\
If on, an unrecognized breakpoint location will cause gdb to create a\n\
pending breakpoint.  If off, an unrecognized breakpoint location results in\n\
an error.  If auto, an unrecognized breakpoint location results in a\n\
user-query to see if a pending breakpoint should be created."),
				NULL,
				show_pending_break_support,
				&breakpoint_set_cmdlist,
				&breakpoint_show_cmdlist);

  pending_break_support = AUTO_BOOLEAN_AUTO;

  add_setshow_boolean_cmd ("auto-hw", no_class,
			   &automatic_hardware_breakpoints, _("\
Set automatic usage of hardware breakpoints."), _("\
Show automatic usage of hardware breakpoints."), _("\
If set, the debugger will automatically use hardware breakpoints for\n\
breakpoints set with \"break\" but falling in read-only memory.  If not set,\n\
a warning will be emitted for such breakpoints."),
			   NULL,
			   show_automatic_hardware_breakpoints,
			   &breakpoint_set_cmdlist,
			   &breakpoint_show_cmdlist);

  add_setshow_enum_cmd ("always-inserted", class_support,
			always_inserted_enums, &always_inserted_mode, _("\
Set mode for inserting breakpoints."), _("\
Show mode for inserting breakpoints."), _("\
When this mode is off, breakpoints are inserted in inferior when it is\n\
resumed, and removed when execution stops.  When this mode is on,\n\
breakpoints are inserted immediately and removed only when the user\n\
deletes the breakpoint.  When this mode is auto (which is the default),\n\
the behaviour depends on the non-stop setting (see help set non-stop).\n\
In this case, if gdb is controlling the inferior in non-stop mode, gdb\n\
behaves as if always-inserted mode is on; if gdb is controlling the\n\
inferior in all-stop mode, gdb behaves as if always-inserted mode is off."),
			   NULL,
			   &show_always_inserted_mode,
			   &breakpoint_set_cmdlist,
			   &breakpoint_show_cmdlist);
  
  automatic_hardware_breakpoints = 1;

  observer_attach_about_to_proceed (breakpoint_about_to_proceed);
}

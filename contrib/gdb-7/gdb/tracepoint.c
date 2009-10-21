/* Tracing functionality for remote targets in custom GDB protocol

   Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006,
   2007, 2008, 2009 Free Software Foundation, Inc.

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
#include "symtab.h"
#include "frame.h"
#include "gdbtypes.h"
#include "expression.h"
#include "gdbcmd.h"
#include "value.h"
#include "target.h"
#include "language.h"
#include "gdb_string.h"
#include "inferior.h"
#include "breakpoint.h"
#include "tracepoint.h"
#include "remote.h"
extern int remote_supports_cond_tracepoints (void);
#include "linespec.h"
#include "regcache.h"
#include "completer.h"
#include "block.h"
#include "dictionary.h"
#include "observer.h"
#include "user-regs.h"
#include "valprint.h"
#include "gdbcore.h"
#include "objfiles.h"

#include "ax.h"
#include "ax-gdb.h"

/* readline include files */
#include "readline/readline.h"
#include "readline/history.h"

/* readline defines this.  */
#undef savestring

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

/* Maximum length of an agent aexpression.
   This accounts for the fact that packets are limited to 400 bytes
   (which includes everything -- including the checksum), and assumes
   the worst case of maximum length for each of the pieces of a
   continuation packet.

   NOTE: expressions get mem2hex'ed otherwise this would be twice as
   large.  (400 - 31)/2 == 184 */
#define MAX_AGENT_EXPR_LEN	184


extern void (*deprecated_readline_begin_hook) (char *, ...);
extern char *(*deprecated_readline_hook) (char *);
extern void (*deprecated_readline_end_hook) (void);

/* GDB commands implemented in other modules:
 */  

extern void output_command (char *, int);

/* 
   Tracepoint.c:

   This module defines the following debugger commands:
   trace            : set a tracepoint on a function, line, or address.
   info trace       : list all debugger-defined tracepoints.
   delete trace     : delete one or more tracepoints.
   enable trace     : enable one or more tracepoints.
   disable trace    : disable one or more tracepoints.
   actions          : specify actions to be taken at a tracepoint.
   passcount        : specify a pass count for a tracepoint.
   tstart           : start a trace experiment.
   tstop            : stop a trace experiment.
   tstatus          : query the status of a trace experiment.
   tfind            : find a trace frame in the trace buffer.
   tdump            : print everything collected at the current tracepoint.
   save-tracepoints : write tracepoint setup into a file.

   This module defines the following user-visible debugger variables:
   $trace_frame : sequence number of trace frame currently being debugged.
   $trace_line  : source line of trace frame currently being debugged.
   $trace_file  : source file of trace frame currently being debugged.
   $tracepoint  : tracepoint number of trace frame currently being debugged.
 */


/* ======= Important global variables: ======= */

/* Number of last traceframe collected.  */
static int traceframe_number;

/* Tracepoint for last traceframe collected.  */
static int tracepoint_number;

/* Symbol for function for last traceframe collected */
static struct symbol *traceframe_fun;

/* Symtab and line for last traceframe collected */
static struct symtab_and_line traceframe_sal;

/* Tracing command lists */
static struct cmd_list_element *tfindlist;

/* ======= Important command functions: ======= */
static void trace_actions_command (char *, int);
static void trace_start_command (char *, int);
static void trace_stop_command (char *, int);
static void trace_status_command (char *, int);
static void trace_find_command (char *, int);
static void trace_find_pc_command (char *, int);
static void trace_find_tracepoint_command (char *, int);
static void trace_find_line_command (char *, int);
static void trace_find_range_command (char *, int);
static void trace_find_outside_command (char *, int);
static void tracepoint_save_command (char *, int);
static void trace_dump_command (char *, int);

/* support routines */

struct collection_list;
static void add_aexpr (struct collection_list *, struct agent_expr *);
static char *mem2hex (gdb_byte *, char *, int);
static void add_register (struct collection_list *collection,
			  unsigned int regno);
static struct cleanup *make_cleanup_free_actions (struct breakpoint *t);
static void free_actions_list (char **actions_list);
static void free_actions_list_cleanup_wrapper (void *);

extern void _initialize_tracepoint (void);

/* Utility: returns true if "target remote" */
static int
target_is_remote (void)
{
  if (current_target.to_shortname &&
      (strcmp (current_target.to_shortname, "remote") == 0
       || strcmp (current_target.to_shortname, "extended-remote") == 0))
    return 1;
  else
    return 0;
}

/* Utility: generate error from an incoming stub packet.  */
static void
trace_error (char *buf)
{
  if (*buf++ != 'E')
    return;			/* not an error msg */
  switch (*buf)
    {
    case '1':			/* malformed packet error */
      if (*++buf == '0')	/*   general case: */
	error (_("tracepoint.c: error in outgoing packet."));
      else
	error (_("tracepoint.c: error in outgoing packet at field #%ld."),
	       strtol (buf, NULL, 16));
    case '2':
      error (_("trace API error 0x%s."), ++buf);
    default:
      error (_("Target returns error code '%s'."), buf);
    }
}

/* Utility: wait for reply from stub, while accepting "O" packets.  */
static char *
remote_get_noisy_reply (char **buf_p,
			long *sizeof_buf)
{
  do				/* Loop on reply from remote stub.  */
    {
      char *buf;
      QUIT;			/* allow user to bail out with ^C */
      getpkt (buf_p, sizeof_buf, 0);
      buf = *buf_p;
      if (buf[0] == 0)
	error (_("Target does not support this command."));
      else if (buf[0] == 'E')
	trace_error (buf);
      else if (buf[0] == 'O' &&
	       buf[1] != 'K')
	remote_console_output (buf + 1);	/* 'O' message from stub */
      else
	return buf;		/* here's the actual reply */
    }
  while (1);
}

/* Set traceframe number to NUM.  */
static void
set_traceframe_num (int num)
{
  traceframe_number = num;
  set_internalvar_integer (lookup_internalvar ("trace_frame"), num);
}

/* Set tracepoint number to NUM.  */
static void
set_tracepoint_num (int num)
{
  tracepoint_number = num;
  set_internalvar_integer (lookup_internalvar ("tracepoint"), num);
}

/* Set externally visible debug variables for querying/printing
   the traceframe context (line, function, file) */

static void
set_traceframe_context (struct frame_info *trace_frame)
{
  CORE_ADDR trace_pc;

  if (trace_frame == NULL)		/* Cease debugging any trace buffers.  */
    {
      traceframe_fun = 0;
      traceframe_sal.pc = traceframe_sal.line = 0;
      traceframe_sal.symtab = NULL;
      clear_internalvar (lookup_internalvar ("trace_func"));
      clear_internalvar (lookup_internalvar ("trace_file"));
      set_internalvar_integer (lookup_internalvar ("trace_line"), -1);
      return;
    }

  /* Save as globals for internal use.  */
  trace_pc = get_frame_pc (trace_frame);
  traceframe_sal = find_pc_line (trace_pc, 0);
  traceframe_fun = find_pc_function (trace_pc);

  /* Save linenumber as "$trace_line", a debugger variable visible to
     users.  */
  set_internalvar_integer (lookup_internalvar ("trace_line"),
			   traceframe_sal.line);

  /* Save func name as "$trace_func", a debugger variable visible to
     users.  */
  if (traceframe_fun == NULL
      || SYMBOL_LINKAGE_NAME (traceframe_fun) == NULL)
    clear_internalvar (lookup_internalvar ("trace_func"));
  else
    set_internalvar_string (lookup_internalvar ("trace_func"),
			    SYMBOL_LINKAGE_NAME (traceframe_fun));

  /* Save file name as "$trace_file", a debugger variable visible to
     users.  */
  if (traceframe_sal.symtab == NULL
      || traceframe_sal.symtab->filename == NULL)
    clear_internalvar (lookup_internalvar ("trace_file"));
  else
    set_internalvar_string (lookup_internalvar ("trace_file"),
			    traceframe_sal.symtab->filename);
}

/* ACTIONS functions: */

/* Prototypes for action-parsing utility commands  */
static void read_actions (struct breakpoint *);

/* The three functions:
   collect_pseudocommand, 
   while_stepping_pseudocommand, and 
   end_actions_pseudocommand
   are placeholders for "commands" that are actually ONLY to be used
   within a tracepoint action list.  If the actual function is ever called,
   it means that somebody issued the "command" at the top level,
   which is always an error.  */

void
end_actions_pseudocommand (char *args, int from_tty)
{
  error (_("This command cannot be used at the top level."));
}

void
while_stepping_pseudocommand (char *args, int from_tty)
{
  error (_("This command can only be used in a tracepoint actions list."));
}

static void
collect_pseudocommand (char *args, int from_tty)
{
  error (_("This command can only be used in a tracepoint actions list."));
}

/* Enter a list of actions for a tracepoint.  */
static void
trace_actions_command (char *args, int from_tty)
{
  struct breakpoint *t;
  char tmpbuf[128];
  char *end_msg = "End with a line saying just \"end\".";

  t = get_tracepoint_by_number (&args, 0, 1);
  if (t)
    {
      sprintf (tmpbuf, "Enter actions for tracepoint %d, one per line.",
	       t->number);

      if (from_tty)
	{
	  if (deprecated_readline_begin_hook)
	    (*deprecated_readline_begin_hook) ("%s  %s\n", tmpbuf, end_msg);
	  else if (input_from_terminal_p ())
	    printf_filtered ("%s\n%s\n", tmpbuf, end_msg);
	}

      free_actions (t);
      t->step_count = 0;	/* read_actions may set this */
      read_actions (t);

      if (deprecated_readline_end_hook)
	(*deprecated_readline_end_hook) ();
      /* tracepoints_changed () */
    }
  /* else just return */
}

/* worker function */
static void
read_actions (struct breakpoint *t)
{
  char *line;
  char *prompt1 = "> ", *prompt2 = "  > ";
  char *prompt = prompt1;
  enum actionline_type linetype;
  extern FILE *instream;
  struct action_line *next = NULL, *temp;
  struct cleanup *old_chain;

  /* Control-C quits instantly if typed while in this loop
     since it should not wait until the user types a newline.  */
  immediate_quit++;
  /* FIXME: kettenis/20010823: Something is wrong here.  In this file
     STOP_SIGNAL is never defined.  So this code has been left out, at
     least for quite a while now.  Replacing STOP_SIGNAL with SIGTSTP
     leads to compilation failures since the variable job_control
     isn't declared.  Leave this alone for now.  */
#ifdef STOP_SIGNAL
  if (job_control)
    signal (STOP_SIGNAL, handle_stop_sig);
#endif
  old_chain = make_cleanup_free_actions (t);
  while (1)
    {
      /* Make sure that all output has been output.  Some machines may
         let you get away with leaving out some of the gdb_flush, but
         not all.  */
      wrap_here ("");
      gdb_flush (gdb_stdout);
      gdb_flush (gdb_stderr);

      if (deprecated_readline_hook && instream == NULL)
	line = (*deprecated_readline_hook) (prompt);
      else if (instream == stdin && ISATTY (instream))
	{
	  line = gdb_readline_wrapper (prompt);
	  if (line && *line)	/* add it to command history */
	    add_history (line);
	}
      else
	line = gdb_readline (0);

      if (!line)
        {
          line = xstrdup ("end");
          printf_filtered ("end\n");
        }
      
      linetype = validate_actionline (&line, t);
      if (linetype == BADLINE)
	continue;		/* already warned -- collect another line */

      temp = xmalloc (sizeof (struct action_line));
      temp->next = NULL;
      temp->action = line;

      if (next == NULL)		/* first action for this tracepoint? */
	t->actions = next = temp;
      else
	{
	  next->next = temp;
	  next = temp;
	}

      if (linetype == STEPPING)	/* begin "while-stepping" */
	{
	  if (prompt == prompt2)
	    {
	      warning (_("Already processing 'while-stepping'"));
	      continue;
	    }
	  else
	    prompt = prompt2;	/* change prompt for stepping actions */
	}
      else if (linetype == END)
	{
	  if (prompt == prompt2)
	    {
	      prompt = prompt1;	/* end of single-stepping actions */
	    }
	  else
	    {			/* end of actions */
	      if (t->actions->next == NULL)
		{
		  /* An "end" all by itself with no other actions
		     means this tracepoint has no actions.
		     Discard empty list.  */
		  free_actions (t);
		}
	      break;
	    }
	}
    }
#ifdef STOP_SIGNAL
  if (job_control)
    signal (STOP_SIGNAL, SIG_DFL);
#endif
  immediate_quit--;
  discard_cleanups (old_chain);
}

/* worker function */
enum actionline_type
validate_actionline (char **line, struct breakpoint *t)
{
  struct cmd_list_element *c;
  struct expression *exp = NULL;
  struct cleanup *old_chain = NULL;
  char *p;

  /* if EOF is typed, *line is NULL */
  if (*line == NULL)
    return END;

  for (p = *line; isspace ((int) *p);)
    p++;

  /* Symbol lookup etc.  */
  if (*p == '\0')	/* empty line: just prompt for another line.  */
    return BADLINE;

  if (*p == '#')		/* comment line */
    return GENERIC;

  c = lookup_cmd (&p, cmdlist, "", -1, 1);
  if (c == 0)
    {
      warning (_("'%s' is not an action that I know, or is ambiguous."), 
	       p);
      return BADLINE;
    }

  if (cmd_cfunc_eq (c, collect_pseudocommand))
    {
      struct agent_expr *aexpr;
      struct agent_reqs areqs;

      do
	{			/* repeat over a comma-separated list */
	  QUIT;			/* allow user to bail out with ^C */
	  while (isspace ((int) *p))
	    p++;

	  if (*p == '$')	/* look for special pseudo-symbols */
	    {
	      if ((0 == strncasecmp ("reg", p + 1, 3)) ||
		  (0 == strncasecmp ("arg", p + 1, 3)) ||
		  (0 == strncasecmp ("loc", p + 1, 3)))
		{
		  p = strchr (p, ',');
		  continue;
		}
	      /* else fall thru, treat p as an expression and parse it!  */
	    }
	  exp = parse_exp_1 (&p, block_for_pc (t->loc->address), 1);
	  old_chain = make_cleanup (free_current_contents, &exp);

	  if (exp->elts[0].opcode == OP_VAR_VALUE)
	    {
	      if (SYMBOL_CLASS (exp->elts[2].symbol) == LOC_CONST)
		{
		  warning (_("constant %s (value %ld) will not be collected."),
			   SYMBOL_PRINT_NAME (exp->elts[2].symbol),
			   SYMBOL_VALUE (exp->elts[2].symbol));
		  return BADLINE;
		}
	      else if (SYMBOL_CLASS (exp->elts[2].symbol) == LOC_OPTIMIZED_OUT)
		{
		  warning (_("%s is optimized away and cannot be collected."),
			   SYMBOL_PRINT_NAME (exp->elts[2].symbol));
		  return BADLINE;
		}
	    }

	  /* We have something to collect, make sure that the expr to
	     bytecode translator can handle it and that it's not too
	     long.  */
	  aexpr = gen_trace_for_expr (t->loc->address, exp);
	  make_cleanup_free_agent_expr (aexpr);

	  if (aexpr->len > MAX_AGENT_EXPR_LEN)
	    error (_("expression too complicated, try simplifying"));

	  ax_reqs (aexpr, &areqs);
	  (void) make_cleanup (xfree, areqs.reg_mask);

	  if (areqs.flaw != agent_flaw_none)
	    error (_("malformed expression"));

	  if (areqs.min_height < 0)
	    error (_("gdb: Internal error: expression has min height < 0"));

	  if (areqs.max_height > 20)
	    error (_("expression too complicated, try simplifying"));

	  do_cleanups (old_chain);
	}
      while (p && *p++ == ',');
      return GENERIC;
    }
  else if (cmd_cfunc_eq (c, while_stepping_pseudocommand))
    {
      char *steparg;		/* in case warning is necessary */

      while (isspace ((int) *p))
	p++;
      steparg = p;

      if (*p == '\0' ||
	  (t->step_count = strtol (p, &p, 0)) == 0)
	{
	  warning (_("'%s': bad step-count; command ignored."), *line);
	  return BADLINE;
	}
      return STEPPING;
    }
  else if (cmd_cfunc_eq (c, end_actions_pseudocommand))
    return END;
  else
    {
      warning (_("'%s' is not a supported tracepoint action."), *line);
      return BADLINE;
    }
}

/* worker function */
void
free_actions (struct breakpoint *t)
{
  struct action_line *line, *next;

  for (line = t->actions; line; line = next)
    {
      next = line->next;
      if (line->action)
	xfree (line->action);
      xfree (line);
    }
  t->actions = NULL;
}

static void
do_free_actions_cleanup (void *t)
{
  free_actions (t);
}

static struct cleanup *
make_cleanup_free_actions (struct breakpoint *t)
{
  return make_cleanup (do_free_actions_cleanup, t);
}

enum {
  memrange_absolute = -1
};

struct memrange
{
  int type;		/* memrange_absolute for absolute memory range,
                           else basereg number */
  bfd_signed_vma start;
  bfd_signed_vma end;
};

struct collection_list
  {
    unsigned char regs_mask[32];	/* room for up to 256 regs */
    long listsize;
    long next_memrange;
    struct memrange *list;
    long aexpr_listsize;	/* size of array pointed to by expr_list elt */
    long next_aexpr_elt;
    struct agent_expr **aexpr_list;

  }
tracepoint_list, stepping_list;

/* MEMRANGE functions: */

static int memrange_cmp (const void *, const void *);

/* compare memranges for qsort */
static int
memrange_cmp (const void *va, const void *vb)
{
  const struct memrange *a = va, *b = vb;

  if (a->type < b->type)
    return -1;
  if (a->type > b->type)
    return 1;
  if (a->type == memrange_absolute)
    {
      if ((bfd_vma) a->start < (bfd_vma) b->start)
	return -1;
      if ((bfd_vma) a->start > (bfd_vma) b->start)
	return 1;
    }
  else
    {
      if (a->start < b->start)
	return -1;
      if (a->start > b->start)
	return 1;
    }
  return 0;
}

/* Sort the memrange list using qsort, and merge adjacent memranges.  */
static void
memrange_sortmerge (struct collection_list *memranges)
{
  int a, b;

  qsort (memranges->list, memranges->next_memrange,
	 sizeof (struct memrange), memrange_cmp);
  if (memranges->next_memrange > 0)
    {
      for (a = 0, b = 1; b < memranges->next_memrange; b++)
	{
	  if (memranges->list[a].type == memranges->list[b].type &&
	      memranges->list[b].start - memranges->list[a].end <=
	      MAX_REGISTER_SIZE)
	    {
	      /* memrange b starts before memrange a ends; merge them.  */
	      if (memranges->list[b].end > memranges->list[a].end)
		memranges->list[a].end = memranges->list[b].end;
	      continue;		/* next b, same a */
	    }
	  a++;			/* next a */
	  if (a != b)
	    memcpy (&memranges->list[a], &memranges->list[b],
		    sizeof (struct memrange));
	}
      memranges->next_memrange = a + 1;
    }
}

/* Add a register to a collection list.  */
static void
add_register (struct collection_list *collection, unsigned int regno)
{
  if (info_verbose)
    printf_filtered ("collect register %d\n", regno);
  if (regno >= (8 * sizeof (collection->regs_mask)))
    error (_("Internal: register number %d too large for tracepoint"),
	   regno);
  collection->regs_mask[regno / 8] |= 1 << (regno % 8);
}

/* Add a memrange to a collection list */
static void
add_memrange (struct collection_list *memranges, 
	      int type, bfd_signed_vma base,
	      unsigned long len)
{
  if (info_verbose)
    {
      printf_filtered ("(%d,", type);
      printf_vma (base);
      printf_filtered (",%ld)\n", len);
    }

  /* type: memrange_absolute == memory, other n == basereg */
  memranges->list[memranges->next_memrange].type = type;
  /* base: addr if memory, offset if reg relative.  */
  memranges->list[memranges->next_memrange].start = base;
  /* len: we actually save end (base + len) for convenience */
  memranges->list[memranges->next_memrange].end = base + len;
  memranges->next_memrange++;
  if (memranges->next_memrange >= memranges->listsize)
    {
      memranges->listsize *= 2;
      memranges->list = xrealloc (memranges->list,
				  memranges->listsize);
    }

  if (type != memrange_absolute)		/* Better collect the base register!  */
    add_register (memranges, type);
}

/* Add a symbol to a collection list.  */
static void
collect_symbol (struct collection_list *collect, 
		struct symbol *sym,
		struct gdbarch *gdbarch,
		long frame_regno, long frame_offset)
{
  unsigned long len;
  unsigned int reg;
  bfd_signed_vma offset;

  len = TYPE_LENGTH (check_typedef (SYMBOL_TYPE (sym)));
  switch (SYMBOL_CLASS (sym))
    {
    default:
      printf_filtered ("%s: don't know symbol class %d\n",
		       SYMBOL_PRINT_NAME (sym),
		       SYMBOL_CLASS (sym));
      break;
    case LOC_CONST:
      printf_filtered ("constant %s (value %ld) will not be collected.\n",
		       SYMBOL_PRINT_NAME (sym), SYMBOL_VALUE (sym));
      break;
    case LOC_STATIC:
      offset = SYMBOL_VALUE_ADDRESS (sym);
      if (info_verbose)
	{
	  char tmp[40];

	  sprintf_vma (tmp, offset);
	  printf_filtered ("LOC_STATIC %s: collect %ld bytes at %s.\n",
			   SYMBOL_PRINT_NAME (sym), len,
			   tmp /* address */);
	}
      add_memrange (collect, memrange_absolute, offset, len);
      break;
    case LOC_REGISTER:
      reg = SYMBOL_REGISTER_OPS (sym)->register_number (sym, gdbarch);
      if (info_verbose)
	printf_filtered ("LOC_REG[parm] %s: ", 
			 SYMBOL_PRINT_NAME (sym));
      add_register (collect, reg);
      /* Check for doubles stored in two registers.  */
      /* FIXME: how about larger types stored in 3 or more regs?  */
      if (TYPE_CODE (SYMBOL_TYPE (sym)) == TYPE_CODE_FLT &&
	  len > register_size (gdbarch, reg))
	add_register (collect, reg + 1);
      break;
    case LOC_REF_ARG:
      printf_filtered ("Sorry, don't know how to do LOC_REF_ARG yet.\n");
      printf_filtered ("       (will not collect %s)\n",
		       SYMBOL_PRINT_NAME (sym));
      break;
    case LOC_ARG:
      reg = frame_regno;
      offset = frame_offset + SYMBOL_VALUE (sym);
      if (info_verbose)
	{
	  printf_filtered ("LOC_LOCAL %s: Collect %ld bytes at offset ",
			   SYMBOL_PRINT_NAME (sym), len);
	  printf_vma (offset);
	  printf_filtered (" from frame ptr reg %d\n", reg);
	}
      add_memrange (collect, reg, offset, len);
      break;
    case LOC_REGPARM_ADDR:
      reg = SYMBOL_VALUE (sym);
      offset = 0;
      if (info_verbose)
	{
	  printf_filtered ("LOC_REGPARM_ADDR %s: Collect %ld bytes at offset ",
			   SYMBOL_PRINT_NAME (sym), len);
	  printf_vma (offset);
	  printf_filtered (" from reg %d\n", reg);
	}
      add_memrange (collect, reg, offset, len);
      break;
    case LOC_LOCAL:
      reg = frame_regno;
      offset = frame_offset + SYMBOL_VALUE (sym);
      if (info_verbose)
	{
	  printf_filtered ("LOC_LOCAL %s: Collect %ld bytes at offset ",
			   SYMBOL_PRINT_NAME (sym), len);
	  printf_vma (offset);
	  printf_filtered (" from frame ptr reg %d\n", reg);
	}
      add_memrange (collect, reg, offset, len);
      break;
    case LOC_UNRESOLVED:
      printf_filtered ("Don't know LOC_UNRESOLVED %s\n", 
		       SYMBOL_PRINT_NAME (sym));
      break;
    case LOC_OPTIMIZED_OUT:
      printf_filtered ("%s has been optimized out of existence.\n",
		       SYMBOL_PRINT_NAME (sym));
      break;
    }
}

/* Add all locals (or args) symbols to collection list */
static void
add_local_symbols (struct collection_list *collect,
		   struct gdbarch *gdbarch, CORE_ADDR pc,
		   long frame_regno, long frame_offset, int type)
{
  struct symbol *sym;
  struct block *block;
  struct dict_iterator iter;
  int count = 0;

  block = block_for_pc (pc);
  while (block != 0)
    {
      QUIT;			/* allow user to bail out with ^C */
      ALL_BLOCK_SYMBOLS (block, iter, sym)
	{
	  if (SYMBOL_IS_ARGUMENT (sym)
	      ? type == 'A'	/* collecting Arguments */
	      : type == 'L')	/* collecting Locals */
	    {
	      count++;
	      collect_symbol (collect, sym, gdbarch,
			      frame_regno, frame_offset);
	    }
	}
      if (BLOCK_FUNCTION (block))
	break;
      else
	block = BLOCK_SUPERBLOCK (block);
    }
  if (count == 0)
    warning (_("No %s found in scope."), 
	     type == 'L' ? "locals" : "args");
}

/* worker function */
static void
clear_collection_list (struct collection_list *list)
{
  int ndx;

  list->next_memrange = 0;
  for (ndx = 0; ndx < list->next_aexpr_elt; ndx++)
    {
      free_agent_expr (list->aexpr_list[ndx]);
      list->aexpr_list[ndx] = NULL;
    }
  list->next_aexpr_elt = 0;
  memset (list->regs_mask, 0, sizeof (list->regs_mask));
}

/* reduce a collection list to string form (for gdb protocol) */
static char **
stringify_collection_list (struct collection_list *list, char *string)
{
  char temp_buf[2048];
  char tmp2[40];
  int count;
  int ndx = 0;
  char *(*str_list)[];
  char *end;
  long i;

  count = 1 + list->next_memrange + list->next_aexpr_elt + 1;
  str_list = (char *(*)[]) xmalloc (count * sizeof (char *));

  for (i = sizeof (list->regs_mask) - 1; i > 0; i--)
    if (list->regs_mask[i] != 0)	/* skip leading zeroes in regs_mask */
      break;
  if (list->regs_mask[i] != 0)	/* prepare to send regs_mask to the stub */
    {
      if (info_verbose)
	printf_filtered ("\nCollecting registers (mask): 0x");
      end = temp_buf;
      *end++ = 'R';
      for (; i >= 0; i--)
	{
	  QUIT;			/* allow user to bail out with ^C */
	  if (info_verbose)
	    printf_filtered ("%02X", list->regs_mask[i]);
	  sprintf (end, "%02X", list->regs_mask[i]);
	  end += 2;
	}
      (*str_list)[ndx] = xstrdup (temp_buf);
      ndx++;
    }
  if (info_verbose)
    printf_filtered ("\n");
  if (list->next_memrange > 0 && info_verbose)
    printf_filtered ("Collecting memranges: \n");
  for (i = 0, count = 0, end = temp_buf; i < list->next_memrange; i++)
    {
      QUIT;			/* allow user to bail out with ^C */
      sprintf_vma (tmp2, list->list[i].start);
      if (info_verbose)
	{
	  printf_filtered ("(%d, %s, %ld)\n", 
			   list->list[i].type, 
			   tmp2, 
			   (long) (list->list[i].end - list->list[i].start));
	}
      if (count + 27 > MAX_AGENT_EXPR_LEN)
	{
	  (*str_list)[ndx] = savestring (temp_buf, count);
	  ndx++;
	  count = 0;
	  end = temp_buf;
	}

      {
        bfd_signed_vma length = list->list[i].end - list->list[i].start;

        /* The "%X" conversion specifier expects an unsigned argument,
           so passing -1 (memrange_absolute) to it directly gives you
           "FFFFFFFF" (or more, depending on sizeof (unsigned)).
           Special-case it.  */
        if (list->list[i].type == memrange_absolute)
          sprintf (end, "M-1,%s,%lX", tmp2, (long) length);
        else
          sprintf (end, "M%X,%s,%lX", list->list[i].type, tmp2, (long) length);
      }

      count += strlen (end);
      end = temp_buf + count;
    }

  for (i = 0; i < list->next_aexpr_elt; i++)
    {
      QUIT;			/* allow user to bail out with ^C */
      if ((count + 10 + 2 * list->aexpr_list[i]->len) > MAX_AGENT_EXPR_LEN)
	{
	  (*str_list)[ndx] = savestring (temp_buf, count);
	  ndx++;
	  count = 0;
	  end = temp_buf;
	}
      sprintf (end, "X%08X,", list->aexpr_list[i]->len);
      end += 10;		/* 'X' + 8 hex digits + ',' */
      count += 10;

      end = mem2hex (list->aexpr_list[i]->buf, 
		     end, list->aexpr_list[i]->len);
      count += 2 * list->aexpr_list[i]->len;
    }

  if (count != 0)
    {
      (*str_list)[ndx] = savestring (temp_buf, count);
      ndx++;
      count = 0;
      end = temp_buf;
    }
  (*str_list)[ndx] = NULL;

  if (ndx == 0)
    {
      xfree (str_list);
      return NULL;
    }
  else
    return *str_list;
}

static void
free_actions_list_cleanup_wrapper (void *al)
{
  free_actions_list (al);
}

static void
free_actions_list (char **actions_list)
{
  int ndx;

  if (actions_list == 0)
    return;

  for (ndx = 0; actions_list[ndx]; ndx++)
    xfree (actions_list[ndx]);

  xfree (actions_list);
}

/* Render all actions into gdb protocol.  */
static void
encode_actions (struct breakpoint *t, char ***tdp_actions,
		char ***stepping_actions)
{
  static char tdp_buff[2048], step_buff[2048];
  char *action_exp;
  struct expression *exp = NULL;
  struct action_line *action;
  int i;
  struct value *tempval;
  struct collection_list *collect;
  struct cmd_list_element *cmd;
  struct agent_expr *aexpr;
  int frame_reg;
  LONGEST frame_offset;


  clear_collection_list (&tracepoint_list);
  clear_collection_list (&stepping_list);
  collect = &tracepoint_list;

  *tdp_actions = NULL;
  *stepping_actions = NULL;

  gdbarch_virtual_frame_pointer (t->gdbarch,
				 t->loc->address, &frame_reg, &frame_offset);

  for (action = t->actions; action; action = action->next)
    {
      QUIT;			/* allow user to bail out with ^C */
      action_exp = action->action;
      while (isspace ((int) *action_exp))
	action_exp++;

      if (*action_exp == '#')	/* comment line */
	return;

      cmd = lookup_cmd (&action_exp, cmdlist, "", -1, 1);
      if (cmd == 0)
	error (_("Bad action list item: %s"), action_exp);

      if (cmd_cfunc_eq (cmd, collect_pseudocommand))
	{
	  do
	    {			/* repeat over a comma-separated list */
	      QUIT;		/* allow user to bail out with ^C */
	      while (isspace ((int) *action_exp))
		action_exp++;

	      if (0 == strncasecmp ("$reg", action_exp, 4))
		{
		  for (i = 0; i < gdbarch_num_regs (t->gdbarch); i++)
		    add_register (collect, i);
		  action_exp = strchr (action_exp, ',');	/* more? */
		}
	      else if (0 == strncasecmp ("$arg", action_exp, 4))
		{
		  add_local_symbols (collect,
				     t->gdbarch,
				     t->loc->address,
				     frame_reg,
				     frame_offset,
				     'A');
		  action_exp = strchr (action_exp, ',');	/* more? */
		}
	      else if (0 == strncasecmp ("$loc", action_exp, 4))
		{
		  add_local_symbols (collect,
				     t->gdbarch,
				     t->loc->address,
				     frame_reg,
				     frame_offset,
				     'L');
		  action_exp = strchr (action_exp, ',');	/* more? */
		}
	      else
		{
		  unsigned long addr, len;
		  struct cleanup *old_chain = NULL;
		  struct cleanup *old_chain1 = NULL;
		  struct agent_reqs areqs;

		  exp = parse_exp_1 (&action_exp, 
				     block_for_pc (t->loc->address), 1);
		  old_chain = make_cleanup (free_current_contents, &exp);

		  switch (exp->elts[0].opcode)
		    {
		    case OP_REGISTER:
		      {
			const char *name = &exp->elts[2].string;

			i = user_reg_map_name_to_regnum (t->gdbarch,
							 name, strlen (name));
			if (i == -1)
			  internal_error (__FILE__, __LINE__,
					  _("Register $%s not available"),
					  name);
			if (info_verbose)
			  printf_filtered ("OP_REGISTER: ");
			add_register (collect, i);
			break;
		      }

		    case UNOP_MEMVAL:
		      /* safe because we know it's a simple expression */
		      tempval = evaluate_expression (exp);
		      addr = value_address (tempval);
		      len = TYPE_LENGTH (check_typedef (exp->elts[1].type));
		      add_memrange (collect, memrange_absolute, addr, len);
		      break;

		    case OP_VAR_VALUE:
		      collect_symbol (collect,
				      exp->elts[2].symbol,
				      t->gdbarch,
				      frame_reg,
				      frame_offset);
		      break;

		    default:	/* full-fledged expression */
		      aexpr = gen_trace_for_expr (t->loc->address, exp);

		      old_chain1 = make_cleanup_free_agent_expr (aexpr);

		      ax_reqs (aexpr, &areqs);
		      if (areqs.flaw != agent_flaw_none)
			error (_("malformed expression"));

		      if (areqs.min_height < 0)
			error (_("gdb: Internal error: expression has min height < 0"));
		      if (areqs.max_height > 20)
			error (_("expression too complicated, try simplifying"));

		      discard_cleanups (old_chain1);
		      add_aexpr (collect, aexpr);

		      /* take care of the registers */
		      if (areqs.reg_mask_len > 0)
			{
			  int ndx1;
			  int ndx2;

			  for (ndx1 = 0; ndx1 < areqs.reg_mask_len; ndx1++)
			    {
			      QUIT;	/* allow user to bail out with ^C */
			      if (areqs.reg_mask[ndx1] != 0)
				{
				  /* assume chars have 8 bits */
				  for (ndx2 = 0; ndx2 < 8; ndx2++)
				    if (areqs.reg_mask[ndx1] & (1 << ndx2))
				      /* it's used -- record it */
				      add_register (collect, 
						    ndx1 * 8 + ndx2);
				}
			    }
			}
		      break;
		    }		/* switch */
		  do_cleanups (old_chain);
		}		/* do */
	    }
	  while (action_exp && *action_exp++ == ',');
	}			/* if */
      else if (cmd_cfunc_eq (cmd, while_stepping_pseudocommand))
	{
	  collect = &stepping_list;
	}
      else if (cmd_cfunc_eq (cmd, end_actions_pseudocommand))
	{
	  if (collect == &stepping_list)	/* end stepping actions */
	    collect = &tracepoint_list;
	  else
	    break;		/* end tracepoint actions */
	}
    }				/* for */
  memrange_sortmerge (&tracepoint_list);
  memrange_sortmerge (&stepping_list);

  *tdp_actions = stringify_collection_list (&tracepoint_list, 
					    tdp_buff);
  *stepping_actions = stringify_collection_list (&stepping_list, 
						 step_buff);
}

static void
add_aexpr (struct collection_list *collect, struct agent_expr *aexpr)
{
  if (collect->next_aexpr_elt >= collect->aexpr_listsize)
    {
      collect->aexpr_list =
	xrealloc (collect->aexpr_list,
		2 * collect->aexpr_listsize * sizeof (struct agent_expr *));
      collect->aexpr_listsize *= 2;
    }
  collect->aexpr_list[collect->next_aexpr_elt] = aexpr;
  collect->next_aexpr_elt++;
}

static char *target_buf;
static long target_buf_size;

/* Set "transparent" memory ranges

   Allow trace mechanism to treat text-like sections
   (and perhaps all read-only sections) transparently, 
   i.e. don't reject memory requests from these address ranges
   just because they haven't been collected.  */

static void
remote_set_transparent_ranges (void)
{
  asection *s;
  bfd_size_type size;
  bfd_vma lma;
  int anysecs = 0;

  if (!exec_bfd)
    return;			/* No information to give.  */

  strcpy (target_buf, "QTro");
  for (s = exec_bfd->sections; s; s = s->next)
    {
      char tmp1[40], tmp2[40];

      if ((s->flags & SEC_LOAD) == 0 ||
      /* (s->flags & SEC_CODE)     == 0 || */
	  (s->flags & SEC_READONLY) == 0)
	continue;

      anysecs = 1;
      lma = s->lma;
      size = bfd_get_section_size (s);
      sprintf_vma (tmp1, lma);
      sprintf_vma (tmp2, lma + size);
      sprintf (target_buf + strlen (target_buf), 
	       ":%s,%s", tmp1, tmp2);
    }
  if (anysecs)
    {
      putpkt (target_buf);
      getpkt (&target_buf, &target_buf_size, 0);
    }
}

/* tstart command:

   Tell target to clear any previous trace experiment.
   Walk the list of tracepoints, and send them (and their actions)
   to the target.  If no errors, 
   Tell target to start a new trace experiment.  */

void download_tracepoint (struct breakpoint *t);

static void
trace_start_command (char *args, int from_tty)
{
  VEC(breakpoint_p) *tp_vec = NULL;
  int ix;
  struct breakpoint *t;

  dont_repeat ();	/* Like "run", dangerous to repeat accidentally.  */

  if (target_is_remote ())
    {
      putpkt ("QTinit");
      remote_get_noisy_reply (&target_buf, &target_buf_size);
      if (strcmp (target_buf, "OK"))
	error (_("Target does not support this command."));

      tp_vec = all_tracepoints ();
      for (ix = 0; VEC_iterate (breakpoint_p, tp_vec, ix, t); ix++)
	{
	  download_tracepoint (t);
	}
      VEC_free (breakpoint_p, tp_vec);

      /* Tell target to treat text-like sections as transparent.  */
      remote_set_transparent_ranges ();
      /* Now insert traps and begin collecting data.  */
      putpkt ("QTStart");
      remote_get_noisy_reply (&target_buf, &target_buf_size);
      if (strcmp (target_buf, "OK"))
	error (_("Bogus reply from target: %s"), target_buf);
      set_traceframe_num (-1);	/* All old traceframes invalidated.  */
      set_tracepoint_num (-1);
      set_traceframe_context (NULL);
      trace_running_p = 1;
      if (deprecated_trace_start_stop_hook)
	deprecated_trace_start_stop_hook (1, from_tty);

    }
  else
    error (_("Trace can only be run on remote targets."));
}

/* Send the definition of a single tracepoint to the target.  */

void
download_tracepoint (struct breakpoint *t)
{
  char tmp[40];
  char buf[2048];
  char **tdp_actions;
  char **stepping_actions;
  int ndx;
  struct cleanup *old_chain = NULL;
  struct agent_expr *aexpr;
  struct cleanup *aexpr_chain = NULL;

  sprintf_vma (tmp, (t->loc ? t->loc->address : 0));
  sprintf (buf, "QTDP:%x:%s:%c:%lx:%x", t->number, 
	   tmp, /* address */
	   (t->enable_state == bp_enabled ? 'E' : 'D'),
	   t->step_count, t->pass_count);
  /* If the tracepoint has a conditional, make it into an agent
     expression and append to the definition.  */
  if (t->loc->cond)
    {
      /* Only test support at download time, we may not know target
	 capabilities at definition time.  */
      if (remote_supports_cond_tracepoints ())
	{
	  aexpr = gen_eval_for_expr (t->loc->address, t->loc->cond);
	  aexpr_chain = make_cleanup_free_agent_expr (aexpr);
	  sprintf (buf + strlen (buf), ":X%x,", aexpr->len);
	  mem2hex (aexpr->buf, buf + strlen (buf), aexpr->len);
	  do_cleanups (aexpr_chain);
	}
      else
	warning (_("Target does not support conditional tracepoints, ignoring tp %d cond"), t->number);
    }

  if (t->actions)
    strcat (buf, "-");
  putpkt (buf);
  remote_get_noisy_reply (&target_buf, &target_buf_size);
  if (strcmp (target_buf, "OK"))
    error (_("Target does not support tracepoints."));

  if (!t->actions)
    return;

  encode_actions (t, &tdp_actions, &stepping_actions);
  old_chain = make_cleanup (free_actions_list_cleanup_wrapper,
			    tdp_actions);
  (void) make_cleanup (free_actions_list_cleanup_wrapper, stepping_actions);

  /* do_single_steps (t); */
  if (tdp_actions)
    {
      for (ndx = 0; tdp_actions[ndx]; ndx++)
	{
	  QUIT;	/* allow user to bail out with ^C */
	  sprintf (buf, "QTDP:-%x:%s:%s%c",
		   t->number, tmp, /* address */
		   tdp_actions[ndx],
		   ((tdp_actions[ndx + 1] || stepping_actions)
		    ? '-' : 0));
	  putpkt (buf);
	  remote_get_noisy_reply (&target_buf,
				  &target_buf_size);
	  if (strcmp (target_buf, "OK"))
	    error (_("Error on target while setting tracepoints."));
	}
    }
  if (stepping_actions)
    {
      for (ndx = 0; stepping_actions[ndx]; ndx++)
	{
	  QUIT;	/* allow user to bail out with ^C */
	  sprintf (buf, "QTDP:-%x:%s:%s%s%s",
		   t->number, tmp, /* address */
		   ((ndx == 0) ? "S" : ""),
		   stepping_actions[ndx],
		   (stepping_actions[ndx + 1] ? "-" : ""));
	  putpkt (buf);
	  remote_get_noisy_reply (&target_buf,
				  &target_buf_size);
	  if (strcmp (target_buf, "OK"))
	    error (_("Error on target while setting tracepoints."));
	}
    }
  do_cleanups (old_chain);
}

/* tstop command */
static void
trace_stop_command (char *args, int from_tty)
{
  if (target_is_remote ())
    {
      putpkt ("QTStop");
      remote_get_noisy_reply (&target_buf, &target_buf_size);
      if (strcmp (target_buf, "OK"))
	error (_("Bogus reply from target: %s"), target_buf);
      trace_running_p = 0;
      if (deprecated_trace_start_stop_hook)
	deprecated_trace_start_stop_hook (0, from_tty);
    }
  else
    error (_("Trace can only be run on remote targets."));
}

unsigned long trace_running_p;

/* tstatus command */
static void
trace_status_command (char *args, int from_tty)
{
  if (target_is_remote ())
    {
      putpkt ("qTStatus");
      remote_get_noisy_reply (&target_buf, &target_buf_size);

      if (target_buf[0] != 'T' ||
	  (target_buf[1] != '0' && target_buf[1] != '1'))
	error (_("Bogus reply from target: %s"), target_buf);

      /* exported for use by the GUI */
      trace_running_p = (target_buf[1] == '1');
    }
  else
    error (_("Trace can only be run on remote targets."));
}

/* Worker function for the various flavors of the tfind command.  */
static void
finish_tfind_command (char **msg,
		      long *sizeof_msg,
		      int from_tty)
{
  int target_frameno = -1, target_tracept = -1;
  struct frame_id old_frame_id;
  char *reply;

  old_frame_id = get_frame_id (get_current_frame ());

  putpkt (*msg);
  reply = remote_get_noisy_reply (msg, sizeof_msg);

  while (reply && *reply)
    switch (*reply)
      {
      case 'F':
	if ((target_frameno = (int) strtol (++reply, &reply, 16)) == -1)
	  {
	    /* A request for a non-existant trace frame has failed.
	       Our response will be different, depending on FROM_TTY:

	       If FROM_TTY is true, meaning that this command was 
	       typed interactively by the user, then give an error
	       and DO NOT change the state of traceframe_number etc.

	       However if FROM_TTY is false, meaning that we're either
	       in a script, a loop, or a user-defined command, then 
	       DON'T give an error, but DO change the state of
	       traceframe_number etc. to invalid.

	       The rationalle is that if you typed the command, you
	       might just have committed a typo or something, and you'd
	       like to NOT lose your current debugging state.  However
	       if you're in a user-defined command or especially in a
	       loop, then you need a way to detect that the command
	       failed WITHOUT aborting.  This allows you to write
	       scripts that search thru the trace buffer until the end,
	       and then continue on to do something else.  */

	    if (from_tty)
	      error (_("Target failed to find requested trace frame."));
	    else
	      {
		if (info_verbose)
		  printf_filtered ("End of trace buffer.\n");
		/* The following will not recurse, since it's
		   special-cased.  */
		trace_find_command ("-1", from_tty);
		reply = NULL;	/* Break out of loop 
				   (avoid recursive nonsense).  */
	      }
	  }
	break;
      case 'T':
	if ((target_tracept = (int) strtol (++reply, &reply, 16)) == -1)
	  error (_("Target failed to find requested trace frame."));
	break;
      case 'O':		/* "OK"? */
	if (reply[1] == 'K' && reply[2] == '\0')
	  reply += 2;
	else
	  error (_("Bogus reply from target: %s"), reply);
	break;
      default:
	error (_("Bogus reply from target: %s"), reply);
      }

  reinit_frame_cache ();
  registers_changed ();
  set_traceframe_num (target_frameno);
  set_tracepoint_num (target_tracept);
  if (target_frameno == -1)
    set_traceframe_context (NULL);
  else
    set_traceframe_context (get_current_frame ());

  if (from_tty)
    {
      enum print_what print_what;

      /* NOTE: in immitation of the step command, try to determine
         whether we have made a transition from one function to
         another.  If so, we'll print the "stack frame" (ie. the new
         function and it's arguments) -- otherwise we'll just show the
         new source line.  */

      if (frame_id_eq (old_frame_id,
		       get_frame_id (get_current_frame ())))
	print_what = SRC_LINE;
      else
	print_what = SRC_AND_LOC;

      print_stack_frame (get_selected_frame (NULL), 1, print_what);
      do_displays ();
    }
}

/* trace_find_command takes a trace frame number n, 
   sends "QTFrame:<n>" to the target, 
   and accepts a reply that may contain several optional pieces
   of information: a frame number, a tracepoint number, and an
   indication of whether this is a trap frame or a stepping frame.

   The minimal response is just "OK" (which indicates that the 
   target does not give us a frame number or a tracepoint number).
   Instead of that, the target may send us a string containing
   any combination of:
   F<hexnum>    (gives the selected frame number)
   T<hexnum>    (gives the selected tracepoint number)
 */

/* tfind command */
static void
trace_find_command (char *args, int from_tty)
{ /* this should only be called with a numeric argument */
  int frameno = -1;

  if (target_is_remote ())
    {
      if (deprecated_trace_find_hook)
	deprecated_trace_find_hook (args, from_tty);

      if (args == 0 || *args == 0)
	{ /* TFIND with no args means find NEXT trace frame.  */
	  if (traceframe_number == -1)
	    frameno = 0;	/* "next" is first one */
	  else
	    frameno = traceframe_number + 1;
	}
      else if (0 == strcmp (args, "-"))
	{
	  if (traceframe_number == -1)
	    error (_("not debugging trace buffer"));
	  else if (from_tty && traceframe_number == 0)
	    error (_("already at start of trace buffer"));

	  frameno = traceframe_number - 1;
	}
      else
	frameno = parse_and_eval_long (args);

      if (frameno < -1)
	error (_("invalid input (%d is less than zero)"), frameno);

      sprintf (target_buf, "QTFrame:%x", frameno);
      finish_tfind_command (&target_buf, &target_buf_size, from_tty);
    }
  else
    error (_("Trace can only be run on remote targets."));
}

/* tfind end */
static void
trace_find_end_command (char *args, int from_tty)
{
  trace_find_command ("-1", from_tty);
}

/* tfind none */
static void
trace_find_none_command (char *args, int from_tty)
{
  trace_find_command ("-1", from_tty);
}

/* tfind start */
static void
trace_find_start_command (char *args, int from_tty)
{
  trace_find_command ("0", from_tty);
}

/* tfind pc command */
static void
trace_find_pc_command (char *args, int from_tty)
{
  CORE_ADDR pc;
  char tmp[40];

  if (target_is_remote ())
    {
      if (args == 0 || *args == 0)
	pc = regcache_read_pc (get_current_regcache ());
      else
	pc = parse_and_eval_address (args);

      sprintf_vma (tmp, pc);
      sprintf (target_buf, "QTFrame:pc:%s", tmp);
      finish_tfind_command (&target_buf, &target_buf_size, from_tty);
    }
  else
    error (_("Trace can only be run on remote targets."));
}

/* tfind tracepoint command */
static void
trace_find_tracepoint_command (char *args, int from_tty)
{
  int tdp;

  if (target_is_remote ())
    {
      if (args == 0 || *args == 0)
	{
	  if (tracepoint_number == -1)
	    error (_("No current tracepoint -- please supply an argument."));
	  else
	    tdp = tracepoint_number;	/* default is current TDP */
	}
      else
	tdp = parse_and_eval_long (args);

      sprintf (target_buf, "QTFrame:tdp:%x", tdp);
      finish_tfind_command (&target_buf, &target_buf_size, from_tty);
    }
  else
    error (_("Trace can only be run on remote targets."));
}

/* TFIND LINE command:

   This command will take a sourceline for argument, just like BREAK
   or TRACE (ie. anything that "decode_line_1" can handle).

   With no argument, this command will find the next trace frame 
   corresponding to a source line OTHER THAN THE CURRENT ONE.  */

static void
trace_find_line_command (char *args, int from_tty)
{
  static CORE_ADDR start_pc, end_pc;
  struct symtabs_and_lines sals;
  struct symtab_and_line sal;
  struct cleanup *old_chain;
  char   startpc_str[40], endpc_str[40];

  if (target_is_remote ())
    {
      if (args == 0 || *args == 0)
	{
	  sal = find_pc_line (get_frame_pc (get_current_frame ()), 0);
	  sals.nelts = 1;
	  sals.sals = (struct symtab_and_line *)
	    xmalloc (sizeof (struct symtab_and_line));
	  sals.sals[0] = sal;
	}
      else
	{
	  sals = decode_line_spec (args, 1);
	  sal = sals.sals[0];
	}

      old_chain = make_cleanup (xfree, sals.sals);
      if (sal.symtab == 0)
	{
	  struct gdbarch *gdbarch = get_current_arch ();

	  printf_filtered ("TFIND: No line number information available");
	  if (sal.pc != 0)
	    {
	      /* This is useful for "info line *0x7f34".  If we can't
	         tell the user about a source line, at least let them
	         have the symbolic address.  */
	      printf_filtered (" for address ");
	      wrap_here ("  ");
	      print_address (gdbarch, sal.pc, gdb_stdout);
	      printf_filtered (";\n -- will attempt to find by PC. \n");
	    }
	  else
	    {
	      printf_filtered (".\n");
	      return;		/* No line, no PC; what can we do?  */
	    }
	}
      else if (sal.line > 0
	       && find_line_pc_range (sal, &start_pc, &end_pc))
	{
	  struct gdbarch *gdbarch = get_objfile_arch (sal.symtab->objfile);

	  if (start_pc == end_pc)
	    {
	      printf_filtered ("Line %d of \"%s\"",
			       sal.line, sal.symtab->filename);
	      wrap_here ("  ");
	      printf_filtered (" is at address ");
	      print_address (gdbarch, start_pc, gdb_stdout);
	      wrap_here ("  ");
	      printf_filtered (" but contains no code.\n");
	      sal = find_pc_line (start_pc, 0);
	      if (sal.line > 0 &&
		  find_line_pc_range (sal, &start_pc, &end_pc) &&
		  start_pc != end_pc)
		printf_filtered ("Attempting to find line %d instead.\n",
				 sal.line);
	      else
		error (_("Cannot find a good line."));
	    }
	}
      else
	/* Is there any case in which we get here, and have an address
	   which the user would want to see?  If we have debugging
	   symbols and no line numbers?  */
	error (_("Line number %d is out of range for \"%s\"."),
	       sal.line, sal.symtab->filename);

      sprintf_vma (startpc_str, start_pc);
      sprintf_vma (endpc_str, end_pc - 1);
      /* Find within range of stated line.  */
      if (args && *args)
	sprintf (target_buf, "QTFrame:range:%s:%s", 
		 startpc_str, endpc_str);
      /* Find OUTSIDE OF range of CURRENT line.  */
      else
	sprintf (target_buf, "QTFrame:outside:%s:%s", 
		 startpc_str, endpc_str);
      finish_tfind_command (&target_buf, &target_buf_size,
			    from_tty);
      do_cleanups (old_chain);
    }
  else
    error (_("Trace can only be run on remote targets."));
}

/* tfind range command */
static void
trace_find_range_command (char *args, int from_tty)
{
  static CORE_ADDR start, stop;
  char start_str[40], stop_str[40];
  char *tmp;

  if (target_is_remote ())
    {
      if (args == 0 || *args == 0)
	{ /* XXX FIXME: what should default behavior be?  */
	  printf_filtered ("Usage: tfind range <startaddr>,<endaddr>\n");
	  return;
	}

      if (0 != (tmp = strchr (args, ',')))
	{
	  *tmp++ = '\0';	/* terminate start address */
	  while (isspace ((int) *tmp))
	    tmp++;
	  start = parse_and_eval_address (args);
	  stop = parse_and_eval_address (tmp);
	}
      else
	{			/* no explicit end address? */
	  start = parse_and_eval_address (args);
	  stop = start + 1;	/* ??? */
	}

      sprintf_vma (start_str, start);
      sprintf_vma (stop_str, stop);
      sprintf (target_buf, "QTFrame:range:%s:%s", start_str, stop_str);
      finish_tfind_command (&target_buf, &target_buf_size, from_tty);
    }
  else
    error (_("Trace can only be run on remote targets."));
}

/* tfind outside command */
static void
trace_find_outside_command (char *args, int from_tty)
{
  CORE_ADDR start, stop;
  char start_str[40], stop_str[40];
  char *tmp;

  if (target_is_remote ())
    {
      if (args == 0 || *args == 0)
	{ /* XXX FIXME: what should default behavior be? */
	  printf_filtered ("Usage: tfind outside <startaddr>,<endaddr>\n");
	  return;
	}

      if (0 != (tmp = strchr (args, ',')))
	{
	  *tmp++ = '\0';	/* terminate start address */
	  while (isspace ((int) *tmp))
	    tmp++;
	  start = parse_and_eval_address (args);
	  stop = parse_and_eval_address (tmp);
	}
      else
	{			/* no explicit end address? */
	  start = parse_and_eval_address (args);
	  stop = start + 1;	/* ??? */
	}

      sprintf_vma (start_str, start);
      sprintf_vma (stop_str, stop);
      sprintf (target_buf, "QTFrame:outside:%s:%s", start_str, stop_str);
      finish_tfind_command (&target_buf, &target_buf_size, from_tty);
    }
  else
    error (_("Trace can only be run on remote targets."));
}

/* info scope command: list the locals for a scope.  */
static void
scope_info (char *args, int from_tty)
{
  struct symtabs_and_lines sals;
  struct symbol *sym;
  struct minimal_symbol *msym;
  struct block *block;
  char **canonical, *symname, *save_args = args;
  struct dict_iterator iter;
  int j, count = 0;
  struct gdbarch *gdbarch;
  int regno;

  if (args == 0 || *args == 0)
    error (_("requires an argument (function, line or *addr) to define a scope"));

  sals = decode_line_1 (&args, 1, NULL, 0, &canonical, NULL);
  if (sals.nelts == 0)
    return;		/* presumably decode_line_1 has already warned */

  /* Resolve line numbers to PC */
  resolve_sal_pc (&sals.sals[0]);
  block = block_for_pc (sals.sals[0].pc);

  while (block != 0)
    {
      QUIT;			/* allow user to bail out with ^C */
      ALL_BLOCK_SYMBOLS (block, iter, sym)
	{
	  QUIT;			/* allow user to bail out with ^C */
	  if (count == 0)
	    printf_filtered ("Scope for %s:\n", save_args);
	  count++;

	  symname = SYMBOL_PRINT_NAME (sym);
	  if (symname == NULL || *symname == '\0')
	    continue;		/* probably botched, certainly useless */

	  gdbarch = get_objfile_arch (SYMBOL_SYMTAB (sym)->objfile);

	  printf_filtered ("Symbol %s is ", symname);
	  switch (SYMBOL_CLASS (sym))
	    {
	    default:
	    case LOC_UNDEF:	/* messed up symbol? */
	      printf_filtered ("a bogus symbol, class %d.\n",
			       SYMBOL_CLASS (sym));
	      count--;		/* don't count this one */
	      continue;
	    case LOC_CONST:
	      printf_filtered ("a constant with value %ld (0x%lx)",
			       SYMBOL_VALUE (sym), SYMBOL_VALUE (sym));
	      break;
	    case LOC_CONST_BYTES:
	      printf_filtered ("constant bytes: ");
	      if (SYMBOL_TYPE (sym))
		for (j = 0; j < TYPE_LENGTH (SYMBOL_TYPE (sym)); j++)
		  fprintf_filtered (gdb_stdout, " %02x",
				    (unsigned) SYMBOL_VALUE_BYTES (sym)[j]);
	      break;
	    case LOC_STATIC:
	      printf_filtered ("in static storage at address ");
	      printf_filtered ("%s", paddress (gdbarch,
					       SYMBOL_VALUE_ADDRESS (sym)));
	      break;
	    case LOC_REGISTER:
	      /* GDBARCH is the architecture associated with the objfile
		 the symbol is defined in; the target architecture may be
		 different, and may provide additional registers.  However,
		 we do not know the target architecture at this point.
		 We assume the objfile architecture will contain all the
		 standard registers that occur in debug info in that
		 objfile.  */
	      regno = SYMBOL_REGISTER_OPS (sym)->register_number (sym, gdbarch);

	      if (SYMBOL_IS_ARGUMENT (sym))
		printf_filtered ("an argument in register $%s",
				 gdbarch_register_name (gdbarch, regno));
	      else
		printf_filtered ("a local variable in register $%s",
				 gdbarch_register_name (gdbarch, regno));
	      break;
	    case LOC_ARG:
	      printf_filtered ("an argument at stack/frame offset %ld",
			       SYMBOL_VALUE (sym));
	      break;
	    case LOC_LOCAL:
	      printf_filtered ("a local variable at frame offset %ld",
			       SYMBOL_VALUE (sym));
	      break;
	    case LOC_REF_ARG:
	      printf_filtered ("a reference argument at offset %ld",
			       SYMBOL_VALUE (sym));
	      break;
	    case LOC_REGPARM_ADDR:
	      /* Note comment at LOC_REGISTER.  */
	      regno = SYMBOL_REGISTER_OPS (sym)->register_number (sym, gdbarch);
	      printf_filtered ("the address of an argument, in register $%s",
			       gdbarch_register_name (gdbarch, regno));
	      break;
	    case LOC_TYPEDEF:
	      printf_filtered ("a typedef.\n");
	      continue;
	    case LOC_LABEL:
	      printf_filtered ("a label at address ");
	      printf_filtered ("%s", paddress (gdbarch,
					       SYMBOL_VALUE_ADDRESS (sym)));
	      break;
	    case LOC_BLOCK:
	      printf_filtered ("a function at address ");
	      printf_filtered ("%s",
		paddress (gdbarch, BLOCK_START (SYMBOL_BLOCK_VALUE (sym))));
	      break;
	    case LOC_UNRESOLVED:
	      msym = lookup_minimal_symbol (SYMBOL_LINKAGE_NAME (sym),
					    NULL, NULL);
	      if (msym == NULL)
		printf_filtered ("Unresolved Static");
	      else
		{
		  printf_filtered ("static storage at address ");
		  printf_filtered ("%s",
		    paddress (gdbarch, SYMBOL_VALUE_ADDRESS (msym)));
		}
	      break;
	    case LOC_OPTIMIZED_OUT:
	      printf_filtered ("optimized out.\n");
	      continue;
	    case LOC_COMPUTED:
	      SYMBOL_COMPUTED_OPS (sym)->describe_location (sym, gdb_stdout);
	      break;
	    }
	  if (SYMBOL_TYPE (sym))
	    printf_filtered (", length %d.\n",
			     TYPE_LENGTH (check_typedef (SYMBOL_TYPE (sym))));
	}
      if (BLOCK_FUNCTION (block))
	break;
      else
	block = BLOCK_SUPERBLOCK (block);
    }
  if (count <= 0)
    printf_filtered ("Scope for %s contains no locals or arguments.\n",
		     save_args);
}

/* worker function (cleanup) */
static void
replace_comma (void *data)
{
  char *comma = data;
  *comma = ',';
}

/* tdump command */
static void
trace_dump_command (char *args, int from_tty)
{
  struct regcache *regcache;
  struct gdbarch *gdbarch;
  struct breakpoint *t;
  struct action_line *action;
  char *action_exp, *next_comma;
  struct cleanup *old_cleanups;
  int stepping_actions = 0;
  int stepping_frame = 0;

  if (!target_is_remote ())
    {
      error (_("Trace can only be run on remote targets."));
      return;
    }

  if (tracepoint_number == -1)
    {
      warning (_("No current trace frame."));
      return;
    }

  t = get_tracepoint (tracepoint_number);

  if (t == NULL)
    error (_("No known tracepoint matches 'current' tracepoint #%d."),
	   tracepoint_number);

  old_cleanups = make_cleanup (null_cleanup, NULL);

  printf_filtered ("Data collected at tracepoint %d, trace frame %d:\n",
		   tracepoint_number, traceframe_number);

  /* The current frame is a trap frame if the frame PC is equal
     to the tracepoint PC.  If not, then the current frame was
     collected during single-stepping.  */

  regcache = get_current_regcache ();
  gdbarch = get_regcache_arch (regcache);

  stepping_frame = (t->loc->address != (regcache_read_pc (regcache)
				   - gdbarch_decr_pc_after_break (gdbarch)));

  for (action = t->actions; action; action = action->next)
    {
      struct cmd_list_element *cmd;

      QUIT;			/* allow user to bail out with ^C */
      action_exp = action->action;
      while (isspace ((int) *action_exp))
	action_exp++;

      /* The collection actions to be done while stepping are
         bracketed by the commands "while-stepping" and "end".  */

      if (*action_exp == '#')	/* comment line */
	continue;

      cmd = lookup_cmd (&action_exp, cmdlist, "", -1, 1);
      if (cmd == 0)
	error (_("Bad action list item: %s"), action_exp);

      if (cmd_cfunc_eq (cmd, while_stepping_pseudocommand))
	stepping_actions = 1;
      else if (cmd_cfunc_eq (cmd, end_actions_pseudocommand))
	stepping_actions = 0;
      else if (cmd_cfunc_eq (cmd, collect_pseudocommand))
	{
	  /* Display the collected data.
	     For the trap frame, display only what was collected at
	     the trap.  Likewise for stepping frames, display only
	     what was collected while stepping.  This means that the
	     two boolean variables, STEPPING_FRAME and
	     STEPPING_ACTIONS should be equal.  */
	  if (stepping_frame == stepping_actions)
	    {
	      do
		{		/* repeat over a comma-separated list */
		  QUIT;		/* allow user to bail out with ^C */
		  if (*action_exp == ',')
		    action_exp++;
		  while (isspace ((int) *action_exp))
		    action_exp++;

		  next_comma = strchr (action_exp, ',');

		  if (0 == strncasecmp (action_exp, "$reg", 4))
		    registers_info (NULL, from_tty);
		  else if (0 == strncasecmp (action_exp, "$loc", 4))
		    locals_info (NULL, from_tty);
		  else if (0 == strncasecmp (action_exp, "$arg", 4))
		    args_info (NULL, from_tty);
		  else
		    {		/* variable */
		      if (next_comma)
			{
			  make_cleanup (replace_comma, next_comma);
			  *next_comma = '\0';
			}
		      printf_filtered ("%s = ", action_exp);
		      output_command (action_exp, from_tty);
		      printf_filtered ("\n");
		    }
		  if (next_comma)
		    *next_comma = ',';
		  action_exp = next_comma;
		}
	      while (action_exp && *action_exp == ',');
	    }
	}
    }
  discard_cleanups (old_cleanups);
}

/* Convert the memory pointed to by mem into hex, placing result in buf.
 * Return a pointer to the last char put in buf (null)
 * "stolen" from sparc-stub.c
 */

static const char hexchars[] = "0123456789abcdef";

static char *
mem2hex (gdb_byte *mem, char *buf, int count)
{
  gdb_byte ch;

  while (count-- > 0)
    {
      ch = *mem++;

      *buf++ = hexchars[ch >> 4];
      *buf++ = hexchars[ch & 0xf];
    }

  *buf = 0;

  return buf;
}

int
get_traceframe_number (void)
{
  return traceframe_number;
}


/* module initialization */
void
_initialize_tracepoint (void)
{
  struct cmd_list_element *c;

  traceframe_number = -1;
  tracepoint_number = -1;

  if (tracepoint_list.list == NULL)
    {
      tracepoint_list.listsize = 128;
      tracepoint_list.list = xmalloc
	(tracepoint_list.listsize * sizeof (struct memrange));
    }
  if (tracepoint_list.aexpr_list == NULL)
    {
      tracepoint_list.aexpr_listsize = 128;
      tracepoint_list.aexpr_list = xmalloc
	(tracepoint_list.aexpr_listsize * sizeof (struct agent_expr *));
    }

  if (stepping_list.list == NULL)
    {
      stepping_list.listsize = 128;
      stepping_list.list = xmalloc
	(stepping_list.listsize * sizeof (struct memrange));
    }

  if (stepping_list.aexpr_list == NULL)
    {
      stepping_list.aexpr_listsize = 128;
      stepping_list.aexpr_list = xmalloc
	(stepping_list.aexpr_listsize * sizeof (struct agent_expr *));
    }

  add_info ("scope", scope_info,
	    _("List the variables local to a scope"));

  add_cmd ("tracepoints", class_trace, NULL,
	   _("Tracing of program execution without stopping the program."),
	   &cmdlist);

  add_com ("tdump", class_trace, trace_dump_command,
	   _("Print everything collected at the current tracepoint."));

  add_prefix_cmd ("tfind", class_trace, trace_find_command, _("\
Select a trace frame;\n\
No argument means forward by one frame; '-' means backward by one frame."),
		  &tfindlist, "tfind ", 1, &cmdlist);

  add_cmd ("outside", class_trace, trace_find_outside_command, _("\
Select a trace frame whose PC is outside the given range.\n\
Usage: tfind outside addr1, addr2"),
	   &tfindlist);

  add_cmd ("range", class_trace, trace_find_range_command, _("\
Select a trace frame whose PC is in the given range.\n\
Usage: tfind range addr1,addr2"),
	   &tfindlist);

  add_cmd ("line", class_trace, trace_find_line_command, _("\
Select a trace frame by source line.\n\
Argument can be a line number (with optional source file), \n\
a function name, or '*' followed by an address.\n\
Default argument is 'the next source line that was traced'."),
	   &tfindlist);

  add_cmd ("tracepoint", class_trace, trace_find_tracepoint_command, _("\
Select a trace frame by tracepoint number.\n\
Default is the tracepoint for the current trace frame."),
	   &tfindlist);

  add_cmd ("pc", class_trace, trace_find_pc_command, _("\
Select a trace frame by PC.\n\
Default is the current PC, or the PC of the current trace frame."),
	   &tfindlist);

  add_cmd ("end", class_trace, trace_find_end_command, _("\
Synonym for 'none'.\n\
De-select any trace frame and resume 'live' debugging."),
	   &tfindlist);

  add_cmd ("none", class_trace, trace_find_none_command,
	   _("De-select any trace frame and resume 'live' debugging."),
	   &tfindlist);

  add_cmd ("start", class_trace, trace_find_start_command,
	   _("Select the first trace frame in the trace buffer."),
	   &tfindlist);

  add_com ("tstatus", class_trace, trace_status_command,
	   _("Display the status of the current trace data collection."));

  add_com ("tstop", class_trace, trace_stop_command,
	   _("Stop trace data collection."));

  add_com ("tstart", class_trace, trace_start_command,
	   _("Start trace data collection."));

  add_com ("end", class_trace, end_actions_pseudocommand, _("\
Ends a list of commands or actions.\n\
Several GDB commands allow you to enter a list of commands or actions.\n\
Entering \"end\" on a line by itself is the normal way to terminate\n\
such a list.\n\n\
Note: the \"end\" command cannot be used at the gdb prompt."));

  add_com ("while-stepping", class_trace, while_stepping_pseudocommand, _("\
Specify single-stepping behavior at a tracepoint.\n\
Argument is number of instructions to trace in single-step mode\n\
following the tracepoint.  This command is normally followed by\n\
one or more \"collect\" commands, to specify what to collect\n\
while single-stepping.\n\n\
Note: this command can only be used in a tracepoint \"actions\" list."));

  add_com_alias ("ws", "while-stepping", class_alias, 0);
  add_com_alias ("stepping", "while-stepping", class_alias, 0);

  add_com ("collect", class_trace, collect_pseudocommand, _("\
Specify one or more data items to be collected at a tracepoint.\n\
Accepts a comma-separated list of (one or more) expressions.  GDB will\n\
collect all data (variables, registers) referenced by that expression.\n\
Also accepts the following special arguments:\n\
    $regs   -- all registers.\n\
    $args   -- all function arguments.\n\
    $locals -- all variables local to the block/function scope.\n\
Note: this command can only be used in a tracepoint \"actions\" list."));

  add_com ("actions", class_trace, trace_actions_command, _("\
Specify the actions to be taken at a tracepoint.\n\
Tracepoint actions may include collecting of specified data, \n\
single-stepping, or enabling/disabling other tracepoints, \n\
depending on target's capabilities."));

  target_buf_size = 2048;
  target_buf = xmalloc (target_buf_size);
}

/* Parser for linespec for the GNU debugger, GDB.

   Copyright (C) 1986-2005, 2007-2012 Free Software Foundation, Inc.

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
#include "symtab.h"
#include "frame.h"
#include "command.h"
#include "symfile.h"
#include "objfiles.h"
#include "source.h"
#include "demangle.h"
#include "value.h"
#include "completer.h"
#include "cp-abi.h"
#include "cp-support.h"
#include "parser-defs.h"
#include "block.h"
#include "objc-lang.h"
#include "linespec.h"
#include "exceptions.h"
#include "language.h"
#include "interps.h"
#include "mi/mi-cmds.h"
#include "target.h"
#include "arch-utils.h"
#include <ctype.h>
#include "cli/cli-utils.h"
#include "filenames.h"
#include "ada-lang.h"

typedef struct symtab *symtab_p;
DEF_VEC_P (symtab_p);

typedef struct symbol *symbolp;
DEF_VEC_P (symbolp);

typedef struct type *typep;
DEF_VEC_P (typep);

/* An address entry is used to ensure that any given location is only
   added to the result a single time.  It holds an address and the
   program space from which the address came.  */

struct address_entry
{
  struct program_space *pspace;
  CORE_ADDR addr;
};

/* An instance of this is used to keep all state while linespec
   operates.  This instance is passed around as a 'this' pointer to
   the various implementation methods.  */

struct linespec_state
{
  /* The program space as seen when the module was entered.  */
  struct program_space *program_space;

  /* The default symtab to use, if no other symtab is specified.  */
  struct symtab *default_symtab;

  /* The default line to use.  */
  int default_line;

  /* If the linespec started with "FILE:", this holds all the matching
     symtabs.  Otherwise, it will hold a single NULL entry, meaning
     that the default symtab should be used.  */
  VEC (symtab_p) *file_symtabs;

  /* If the linespec started with "FILE:", this holds an xmalloc'd
     copy of "FILE".  */
  char *user_filename;

  /* If the linespec is "FUNCTION:LABEL", this holds an xmalloc'd copy
     of "FUNCTION".  */
  char *user_function;

  /* The 'funfirstline' value that was passed in to decode_line_1 or
     decode_line_full.  */
  int funfirstline;

  /* Nonzero if we are running in 'list' mode; see decode_line_list.  */
  int list_mode;

  /* The 'canonical' value passed to decode_line_full, or NULL.  */
  struct linespec_result *canonical;

  /* Canonical strings that mirror the symtabs_and_lines result.  */
  char **canonical_names;

  /* This is a set of address_entry objects which is used to prevent
     duplicate symbols from being entered into the result.  */
  htab_t addr_set;
};

/* This is a helper object that is used when collecting symbols into a
   result.  */

struct collect_info
{
  /* The linespec object in use.  */
  struct linespec_state *state;

  /* The result being accumulated.  */
  struct symtabs_and_lines result;
};

/* Prototypes for local functions.  */

static void initialize_defaults (struct symtab **default_symtab,
				 int *default_line);

static struct symtabs_and_lines decode_indirect (struct linespec_state *self,
						 char **argptr);

static char *locate_first_half (char **argptr, int *is_quote_enclosed);

static struct symtabs_and_lines decode_objc (struct linespec_state *self,
					     char **argptr);

static struct symtabs_and_lines decode_compound (struct linespec_state *self,
						 char **argptr,
						 char *saved_arg,
						 char *p);

static VEC (symbolp) *lookup_prefix_sym (char **argptr, char *p,
					 VEC (symtab_p) *,
					 char **);

static struct symtabs_and_lines find_method (struct linespec_state *self,
					     char *saved_arg,
					     char *copy,
					     const char *class_name,
					     VEC (symbolp) *sym_classes);

static void cplusplus_error (const char *name, const char *fmt, ...)
     ATTRIBUTE_NORETURN ATTRIBUTE_PRINTF (2, 3);

static char *find_toplevel_char (char *s, char c);

static int is_objc_method_format (const char *s);

static VEC (symtab_p) *symtabs_from_filename (char **argptr,
					      char *p, int is_quote_enclosed,
					      char **user_filename);

static VEC (symbolp) *find_function_symbols (char **argptr, char *p,
					     int is_quote_enclosed,
					     char **user_function);

static struct symtabs_and_lines decode_all_digits (struct linespec_state *self,
						   char **argptr,
						   char *q);

static struct symtabs_and_lines decode_dollar (struct linespec_state *self,
					       char *copy);

static int decode_label (struct linespec_state *self,
			 VEC (symbolp) *function_symbols,
			 char *copy,
			 struct symtabs_and_lines *result);

static struct symtabs_and_lines decode_variable (struct linespec_state *self,
						 char *copy);

static int symbol_to_sal (struct symtab_and_line *result,
			  int funfirstline, struct symbol *sym);

static void add_matching_symbols_to_info (const char *name,
					  struct collect_info *info,
					  struct program_space *pspace);

static void add_all_symbol_names_from_pspace (struct collect_info *info,
					      struct program_space *pspace,
					      VEC (const_char_ptr) *names);

/* Helper functions.  */

/* Add SAL to SALS.  */

static void
add_sal_to_sals_basic (struct symtabs_and_lines *sals,
		       struct symtab_and_line *sal)
{
  ++sals->nelts;
  sals->sals = xrealloc (sals->sals, sals->nelts * sizeof (sals->sals[0]));
  sals->sals[sals->nelts - 1] = *sal;
}

/* Add SAL to SALS, and also update SELF->CANONICAL_NAMES to reflect
   the new sal, if needed.  If not NULL, SYMNAME is the name of the
   symbol to use when constructing the new canonical name.  */

static void
add_sal_to_sals (struct linespec_state *self,
		 struct symtabs_and_lines *sals,
		 struct symtab_and_line *sal,
		 const char *symname)
{
  add_sal_to_sals_basic (sals, sal);

  if (self->canonical)
    {
      char *canonical_name = NULL;

      self->canonical_names = xrealloc (self->canonical_names,
					sals->nelts * sizeof (char *));
      if (sal->symtab && sal->symtab->filename)
	{
	  char *filename = sal->symtab->filename;

	  /* Note that the filter doesn't have to be a valid linespec
	     input.  We only apply the ":LINE" treatment to Ada for
	     the time being.  */
	  if (symname != NULL && sal->line != 0
	      && current_language->la_language == language_ada)
	    canonical_name = xstrprintf ("%s:%s:%d", filename, symname,
					 sal->line);
	  else if (symname != NULL)
	    canonical_name = xstrprintf ("%s:%s", filename, symname);
	  else
	    canonical_name = xstrprintf ("%s:%d", filename, sal->line);
	}

      self->canonical_names[sals->nelts - 1] = canonical_name;
    }
}

/* A hash function for address_entry.  */

static hashval_t
hash_address_entry (const void *p)
{
  const struct address_entry *aep = p;
  hashval_t hash;

  hash = iterative_hash_object (aep->pspace, 0);
  return iterative_hash_object (aep->addr, hash);
}

/* An equality function for address_entry.  */

static int
eq_address_entry (const void *a, const void *b)
{
  const struct address_entry *aea = a;
  const struct address_entry *aeb = b;

  return aea->pspace == aeb->pspace && aea->addr == aeb->addr;
}

/* Check whether the address, represented by PSPACE and ADDR, is
   already in the set.  If so, return 0.  Otherwise, add it and return
   1.  */

static int
maybe_add_address (htab_t set, struct program_space *pspace, CORE_ADDR addr)
{
  struct address_entry e, *p;
  void **slot;

  e.pspace = pspace;
  e.addr = addr;
  slot = htab_find_slot (set, &e, INSERT);
  if (*slot)
    return 0;

  p = XNEW (struct address_entry);
  memcpy (p, &e, sizeof (struct address_entry));
  *slot = p;

  return 1;
}

/* Issue a helpful hint on using the command completion feature on
   single quoted demangled C++ symbols as part of the completion
   error.  */

static void
cplusplus_error (const char *name, const char *fmt, ...)
{
  struct ui_file *tmp_stream;
  char *message;

  tmp_stream = mem_fileopen ();
  make_cleanup_ui_file_delete (tmp_stream);

  {
    va_list args;

    va_start (args, fmt);
    vfprintf_unfiltered (tmp_stream, fmt, args);
    va_end (args);
  }

  while (*name == '\'')
    name++;
  fprintf_unfiltered (tmp_stream,
		      ("Hint: try '%s<TAB> or '%s<ESC-?>\n"
		       "(Note leading single quote.)"),
		      name, name);

  message = ui_file_xstrdup (tmp_stream, NULL);
  make_cleanup (xfree, message);
  throw_error (NOT_FOUND_ERROR, "%s", message);
}

/* A helper for iterate_over_all_matching_symtabs that is passed as a
   callback to the expand_symtabs_matching method.  */

static int
iterate_name_matcher (const struct language_defn *language,
		      const char *name, void *d)
{
  const char **dname = d;

  if (language->la_symbol_name_compare (name, *dname) == 0)
    return 1;
  return 0;
}

/* A helper that walks over all matching symtabs in all objfiles and
   calls CALLBACK for each symbol matching NAME.  If SEARCH_PSPACE is
   not NULL, then the search is restricted to just that program
   space.  */

static void
iterate_over_all_matching_symtabs (const char *name,
				   const domain_enum domain,
				   int (*callback) (struct symbol *, void *),
				   void *data,
				   struct program_space *search_pspace)
{
  struct objfile *objfile;
  struct program_space *pspace;

  ALL_PSPACES (pspace)
  {
    if (search_pspace != NULL && search_pspace != pspace)
      continue;
    if (pspace->executing_startup)
      continue;

    set_current_program_space (pspace);

    ALL_OBJFILES (objfile)
    {
      struct symtab *symtab;

      if (objfile->sf)
	objfile->sf->qf->expand_symtabs_matching (objfile, NULL,
						  iterate_name_matcher,
						  ALL_DOMAIN,
						  &name);

      ALL_OBJFILE_SYMTABS (objfile, symtab)
	{
	  if (symtab->primary)
	    {
	      struct block *block;

	      block = BLOCKVECTOR_BLOCK (BLOCKVECTOR (symtab), STATIC_BLOCK);
	      LA_ITERATE_OVER_SYMBOLS (block, name, domain, callback, data);
	    }
	}
    }
  }
}

/* Returns the block to be used for symbol searches for the given SYMTAB,
   which may be NULL.  */

static struct block *
get_search_block (struct symtab *symtab)
{
  struct block *block;

  if (symtab != NULL)
    block = BLOCKVECTOR_BLOCK (BLOCKVECTOR (symtab), STATIC_BLOCK);
  else
    {
      enum language save_language;

      /* get_selected_block can change the current language when there is
	 no selected frame yet.  */
      save_language = current_language->la_language;
      block = get_selected_block (0);
      set_language (save_language);
    }

  return block;
}

/* A helper for find_method.  This finds all methods in type T which
   match NAME.  It adds resulting symbol names to RESULT_NAMES, and
   adds T's direct superclasses to SUPERCLASSES.  */

static void
find_methods (struct type *t, const char *name,
	      VEC (const_char_ptr) **result_names,
	      VEC (typep) **superclasses)
{
  int i1 = 0;
  int ibase;
  char *class_name = type_name_no_tag (t);
  char *canon;

  /* Ignore this class if it doesn't have a name.  This is ugly, but
     unless we figure out how to get the physname without the name of
     the class, then the loop can't do any good.  */
  if (class_name)
    {
      int method_counter;
      int name_len = strlen (name);

      CHECK_TYPEDEF (t);

      /* Loop over each method name.  At this level, all overloads of a name
         are counted as a single name.  There is an inner loop which loops over
         each overload.  */

      for (method_counter = TYPE_NFN_FIELDS (t) - 1;
	   method_counter >= 0;
	   --method_counter)
	{
	  char *method_name = TYPE_FN_FIELDLIST_NAME (t, method_counter);
	  char dem_opname[64];

	  if (strncmp (method_name, "__", 2) == 0 ||
	      strncmp (method_name, "op", 2) == 0 ||
	      strncmp (method_name, "type", 4) == 0)
	    {
	      if (cplus_demangle_opname (method_name, dem_opname, DMGL_ANSI))
		method_name = dem_opname;
	      else if (cplus_demangle_opname (method_name, dem_opname, 0))
		method_name = dem_opname;
	    }

	  if (strcmp_iw (method_name, name) == 0)
	    {
	      int field_counter;

	      for (field_counter = (TYPE_FN_FIELDLIST_LENGTH (t, method_counter)
				    - 1);
		   field_counter >= 0;
		   --field_counter)
		{
		  struct fn_field *f;
		  const char *phys_name;

		  f = TYPE_FN_FIELDLIST1 (t, method_counter);
		  if (TYPE_FN_FIELD_STUB (f, field_counter))
		    continue;
		  phys_name = TYPE_FN_FIELD_PHYSNAME (f, field_counter);
		  VEC_safe_push (const_char_ptr, *result_names, phys_name);
		}
	    }
	}
    }

  for (ibase = 0; ibase < TYPE_N_BASECLASSES (t); ibase++)
    VEC_safe_push (typep, *superclasses, TYPE_BASECLASS (t, ibase));
}

/* Find an instance of the character C in the string S that is outside
   of all parenthesis pairs, single-quoted strings, and double-quoted
   strings.  Also, ignore the char within a template name, like a ','
   within foo<int, int>.  */

static char *
find_toplevel_char (char *s, char c)
{
  int quoted = 0;		/* zero if we're not in quotes;
				   '"' if we're in a double-quoted string;
				   '\'' if we're in a single-quoted string.  */
  int depth = 0;		/* Number of unclosed parens we've seen.  */
  char *scan;

  for (scan = s; *scan; scan++)
    {
      if (quoted)
	{
	  if (*scan == quoted)
	    quoted = 0;
	  else if (*scan == '\\' && *(scan + 1))
	    scan++;
	}
      else if (*scan == c && ! quoted && depth == 0)
	return scan;
      else if (*scan == '"' || *scan == '\'')
	quoted = *scan;
      else if (*scan == '(' || *scan == '<')
	depth++;
      else if ((*scan == ')' || *scan == '>') && depth > 0)
	depth--;
    }

  return 0;
}

/* Determines if the gives string corresponds to an Objective-C method
   representation, such as -[Foo bar:] or +[Foo bar].  Objective-C symbols
   are allowed to have spaces and parentheses in them.  */

static int 
is_objc_method_format (const char *s)
{
  if (s == NULL || *s == '\0')
    return 0;
  /* Handle arguments with the format FILENAME:SYMBOL.  */
  if ((s[0] == ':') && (strchr ("+-", s[1]) != NULL) 
      && (s[2] == '[') && strchr(s, ']'))
    return 1;
  /* Handle arguments that are just SYMBOL.  */
  else if ((strchr ("+-", s[0]) != NULL) && (s[1] == '[') && strchr(s, ']'))
    return 1;
  return 0;
}

/* Given FILTERS, a list of canonical names, filter the sals in RESULT
   and store the result in SELF->CANONICAL.  */

static void
filter_results (struct linespec_state *self,
		struct symtabs_and_lines *result,
		VEC (const_char_ptr) *filters)
{
  int i;
  const char *name;

  for (i = 0; VEC_iterate (const_char_ptr, filters, i, name); ++i)
    {
      struct linespec_sals lsal;
      int j;

      memset (&lsal, 0, sizeof (lsal));

      for (j = 0; j < result->nelts; ++j)
	{
	  if (strcmp (name, self->canonical_names[j]) == 0)
	    add_sal_to_sals_basic (&lsal.sals, &result->sals[j]);
	}

      if (lsal.sals.nelts > 0)
	{
	  lsal.canonical = xstrdup (name);
	  VEC_safe_push (linespec_sals, self->canonical->sals, &lsal);
	}
    }

  self->canonical->pre_expanded = 0;
}

/* Store RESULT into SELF->CANONICAL.  */

static void
convert_results_to_lsals (struct linespec_state *self,
			  struct symtabs_and_lines *result)
{
  struct linespec_sals lsal;

  lsal.canonical = NULL;
  lsal.sals = *result;
  VEC_safe_push (linespec_sals, self->canonical->sals, &lsal);
}

/* Handle multiple results in RESULT depending on SELECT_MODE.  This
   will either return normally, throw an exception on multiple
   results, or present a menu to the user.  On return, the SALS vector
   in SELF->CANONICAL is set up properly.  */

static void
decode_line_2 (struct linespec_state *self,
	       struct symtabs_and_lines *result,
	       const char *select_mode)
{
  const char *iter;
  char *args, *prompt;
  int i;
  struct cleanup *old_chain;
  VEC (const_char_ptr) *item_names = NULL, *filters = NULL;
  struct get_number_or_range_state state;

  gdb_assert (select_mode != multiple_symbols_all);
  gdb_assert (self->canonical != NULL);

  old_chain = make_cleanup (VEC_cleanup (const_char_ptr), &item_names);
  make_cleanup (VEC_cleanup (const_char_ptr), &filters);
  for (i = 0; i < result->nelts; ++i)
    {
      int j, found = 0;
      const char *iter;

      gdb_assert (self->canonical_names[i] != NULL);
      for (j = 0; VEC_iterate (const_char_ptr, item_names, j, iter); ++j)
	{
	  if (strcmp (iter, self->canonical_names[i]) == 0)
	    {
	      found = 1;
	      break;
	    }
	}

      if (!found)
	VEC_safe_push (const_char_ptr, item_names, self->canonical_names[i]);
    }

  if (select_mode == multiple_symbols_cancel
      && VEC_length (const_char_ptr, item_names) > 1)
    error (_("canceled because the command is ambiguous\n"
	     "See set/show multiple-symbol."));
  
  if (select_mode == multiple_symbols_all
      || VEC_length (const_char_ptr, item_names) == 1)
    {
      do_cleanups (old_chain);
      convert_results_to_lsals (self, result);
      return;
    }

  printf_unfiltered (_("[0] cancel\n[1] all\n"));
  for (i = 0; VEC_iterate (const_char_ptr, item_names, i, iter); ++i)
    printf_unfiltered ("[%d] %s\n", i + 2, iter);

  prompt = getenv ("PS2");
  if (prompt == NULL)
    {
      prompt = "> ";
    }
  args = command_line_input (prompt, 0, "overload-choice");

  if (args == 0 || *args == 0)
    error_no_arg (_("one or more choice numbers"));

  init_number_or_range (&state, args);
  while (!state.finished)
    {
      int num;

      num = get_number_or_range (&state);

      if (num == 0)
	error (_("canceled"));
      else if (num == 1)
	{
	  /* We intentionally make this result in a single breakpoint,
	     contrary to what older versions of gdb did.  The
	     rationale is that this lets a user get the
	     multiple_symbols_all behavior even with the 'ask'
	     setting; and he can get separate breakpoints by entering
	     "2-57" at the query.  */
	  do_cleanups (old_chain);
	  convert_results_to_lsals (self, result);
	  return;
	}

      num -= 2;
      if (num >= VEC_length (const_char_ptr, item_names))
	printf_unfiltered (_("No choice number %d.\n"), num);
      else
	{
	  const char *elt = VEC_index (const_char_ptr, item_names, num);

	  if (elt != NULL)
	    {
	      VEC_safe_push (const_char_ptr, filters, elt);
	      VEC_replace (const_char_ptr, item_names, num, NULL);
	    }
	  else
	    {
	      printf_unfiltered (_("duplicate request for %d ignored.\n"),
				 num);
	    }
	}
    }

  filter_results (self, result, filters);
  do_cleanups (old_chain);
}

/* Valid delimiters for linespec keywords "if", "thread" or "task".  */

static int
is_linespec_boundary (char c)
{
  return c == ' ' || c == '\t' || c == '\0' || c == ',';
}

/* A helper function for decode_line_1 and friends which skips P
   past any method overload information at the beginning of P, e.g.,
   "(const struct foo *)".

   This function assumes that P has already been validated to contain
   overload information, and it will assert if *P != '('.  */
static char *
find_method_overload_end (char *p)
{
  int depth = 0;

  gdb_assert (*p == '(');

  while (*p)
    {
      if (*p == '(')
	++depth;
      else if (*p == ')')
	{
	  if (--depth == 0)
	    {
	      ++p;
	      break;
	    }
	}
      ++p;
    }

  return p;
}

/* Keep important information used when looking up a name.  This includes
   template parameters, overload information, and important keywords, including
   the possible Java trailing type.  */

static char *
keep_name_info (char *p, int on_boundary)
{
  const char *quotes = get_gdb_completer_quote_characters ();
  char *saved_p = p;
  int nest = 0;

  while (*p)
    {
      if (strchr (quotes, *p))
	break;

      if (*p == ',' && !nest)
	break;

      if (on_boundary && !nest)
	{
	  const char *const words[] = { "if", "thread", "task" };
	  int wordi;

	  for (wordi = 0; wordi < ARRAY_SIZE (words); wordi++)
	    if (strncmp (p, words[wordi], strlen (words[wordi])) == 0
		&& is_linespec_boundary (p[strlen (words[wordi])]))
	      break;
	  if (wordi < ARRAY_SIZE (words))
	    break;
	}

      if (*p == '(' || *p == '<' || *p == '[')
	nest++;
      else if ((*p == ')' || *p == '>' || *p == ']') && nest > 0)
	nest--;

      p++;

      /* The ',' check could fail on "operator ,".  */
      p += cp_validate_operator (p);

      on_boundary = is_linespec_boundary (p[-1]);
    }

  while (p > saved_p && is_linespec_boundary (p[-1]))
    p--;

  return p;
}


/* The parser of linespec itself.  */

/* Parse a string that specifies a line number.
   Pass the address of a char * variable; that variable will be
   advanced over the characters actually parsed.

   The string can be:

   LINENUM -- that line number in current file.  PC returned is 0.
   FILE:LINENUM -- that line in that file.  PC returned is 0.
   FUNCTION -- line number of openbrace of that function.
   PC returned is the start of the function.
   LABEL -- a label in the current scope
   VARIABLE -- line number of definition of that variable.
   PC returned is 0.
   FILE:FUNCTION -- likewise, but prefer functions in that file.
   *EXPR -- line in which address EXPR appears.

   This may all be followed by an "if EXPR", which we ignore.

   FUNCTION may be an undebuggable function found in minimal symbol table.

   If the argument FUNFIRSTLINE is nonzero, we want the first line
   of real code inside a function when a function is specified, and it is
   not OK to specify a variable or type to get its line number.

   DEFAULT_SYMTAB specifies the file to use if none is specified.
   It defaults to current_source_symtab.
   DEFAULT_LINE specifies the line number to use for relative
   line numbers (that start with signs).  Defaults to current_source_line.
   If CANONICAL is non-NULL, store an array of strings containing the canonical
   line specs there if necessary.  Currently overloaded member functions and
   line numbers or static functions without a filename yield a canonical
   line spec.  The array and the line spec strings are allocated on the heap,
   it is the callers responsibility to free them.

   Note that it is possible to return zero for the symtab
   if no file is validly specified.  Callers must check that.
   Also, the line number returned may be invalid.  */

/* We allow single quotes in various places.  This is a hideous
   kludge, which exists because the completer can't yet deal with the
   lack of single quotes.  FIXME: write a linespec_completer which we
   can use as appropriate instead of make_symbol_completion_list.  */

struct symtabs_and_lines
decode_line_internal (struct linespec_state *self, char **argptr)
{
  char *p;
  char *q;

  char *copy;
  /* This says whether or not something in *ARGPTR is quoted with
     completer_quotes (i.e. with single quotes).  */
  int is_quoted;
  /* Is *ARGPTR enclosed in double quotes?  */
  int is_quote_enclosed;
  int is_objc_method = 0;
  char *saved_arg = *argptr;
  /* If IS_QUOTED, the end of the quoted bit.  */
  char *end_quote = NULL;
  /* Is *ARGPTR enclosed in single quotes?  */
  int is_squote_enclosed = 0;
  /* The "first half" of the linespec.  */
  char *first_half;

  /* If we are parsing `function:label', this holds the symbols
     matching the function name.  */
  VEC (symbolp) *function_symbols = NULL;
  /* If FUNCTION_SYMBOLS is not NULL, then this is the exception that
     was thrown when trying to parse a filename.  */
  volatile struct gdb_exception file_exception;

  struct cleanup *cleanup = make_cleanup (null_cleanup, NULL);

  /* Defaults have defaults.  */

  initialize_defaults (&self->default_symtab, &self->default_line);
  
  /* See if arg is *PC.  */

  if (**argptr == '*')
    {
      do_cleanups (cleanup);
      return decode_indirect (self, argptr);
    }

  is_quoted = (strchr (get_gdb_completer_quote_characters (),
		       **argptr) != NULL);

  if (is_quoted)
    {
      end_quote = skip_quoted (*argptr);
      if (*end_quote == '\0')
	is_squote_enclosed = 1;
    }

  /* Check to see if it's a multipart linespec (with colons or
     periods).  */

  /* Locate the end of the first half of the linespec.
     After the call, for instance, if the argptr string is "foo.c:123"
     p will point at "123".  If there is only one part, like "foo", p
     will point to "".  If this is a C++ name, like "A::B::foo", p will
     point to "::B::foo".  Argptr is not changed by this call.  */

  first_half = p = locate_first_half (argptr, &is_quote_enclosed);

  /* First things first: if ARGPTR starts with a filename, get its
     symtab and strip the filename from ARGPTR.  */
  TRY_CATCH (file_exception, RETURN_MASK_ERROR)
    {
      self->file_symtabs = symtabs_from_filename (argptr, p, is_quote_enclosed,
						  &self->user_filename);
    }

  if (VEC_empty (symtab_p, self->file_symtabs))
    {
      /* A NULL entry means to use GLOBAL_DEFAULT_SYMTAB.  */
      VEC_safe_push (symtab_p, self->file_symtabs, NULL);
    }

  if (file_exception.reason >= 0)
    {
      /* Check for single quotes on the non-filename part.  */
      is_quoted = (**argptr
		   && strchr (get_gdb_completer_quote_characters (),
			      **argptr) != NULL);
      if (is_quoted)
	end_quote = skip_quoted (*argptr);

      /* Locate the next "half" of the linespec.  */
      first_half = p = locate_first_half (argptr, &is_quote_enclosed);
    }

  /* Check if this is an Objective-C method (anything that starts with
     a '+' or '-' and a '[').  */
  if (is_objc_method_format (p))
    is_objc_method = 1;

  /* Check if the symbol could be an Objective-C selector.  */

  {
    struct symtabs_and_lines values;

    values = decode_objc (self, argptr);
    if (values.sals != NULL)
      {
	do_cleanups (cleanup);
	return values;
      }
  }

  /* Does it look like there actually were two parts?  */

  if (p[0] == ':' || p[0] == '.')
    {
      /* Is it a C++ or Java compound data structure?
	 The check on p[1] == ':' is capturing the case of "::",
	 since p[0]==':' was checked above.
	 Note that the call to decode_compound does everything
	 for us, including the lookup on the symbol table, so we
	 can return now.  */
	
      if (p[0] == '.' || p[1] == ':')
	{
	  struct symtabs_and_lines values;
	  volatile struct gdb_exception ex;
	  char *saved_argptr = *argptr;

	  if (is_quote_enclosed)
	    ++saved_arg;

	  /* Initialize it just to avoid a GCC false warning.  */
	  memset (&values, 0, sizeof (values));

	  TRY_CATCH (ex, RETURN_MASK_ERROR)
	    {
	      values = decode_compound (self, argptr, saved_arg, p);
	    }
	  if ((is_quoted || is_squote_enclosed) && **argptr == '\'')
	    *argptr = *argptr + 1;

	  if (ex.reason >= 0)
	    {
	      do_cleanups (cleanup);
	      return values;
	    }

	  if (ex.error != NOT_FOUND_ERROR)
	    throw_exception (ex);

	  *argptr = saved_argptr;
	}
      else
	{
	  /* If there was an exception looking up a specified filename earlier,
	     then check whether we were really given `function:label'.   */
	  if (file_exception.reason < 0)
	    {
	      function_symbols = find_function_symbols (argptr, p,
							is_quote_enclosed,
							&self->user_function);

	      /* If we did not find a function, re-throw the original
		 exception.  */
	      if (!function_symbols)
		throw_exception (file_exception);

	      make_cleanup (VEC_cleanup (symbolp), &function_symbols);
	    }

	  /* Check for single quotes on the non-filename part.  */
	  if (!is_quoted)
	    {
	      is_quoted = (**argptr
			   && strchr (get_gdb_completer_quote_characters (),
				      **argptr) != NULL);
	      if (is_quoted)
		end_quote = skip_quoted (*argptr);
	    }
	}
    }

  /* self->file_symtabs holds the  specified file symtabs, or 0 if no file
     specified.
     If we are parsing `function:symbol', then FUNCTION_SYMBOLS holds the
     functions before the `:'.
     arg no longer contains the file name.  */

  /* If the filename was quoted, we must re-check the quotation.  */

  if (end_quote == first_half && *end_quote!= '\0')
    {
      is_quoted = (**argptr
		   && strchr (get_gdb_completer_quote_characters (),
			      **argptr) != NULL);
      if (is_quoted)
	end_quote = skip_quoted (*argptr);
    }

  /* Check whether arg is all digits (and sign).  */

  q = *argptr;
  if (*q == '-' || *q == '+')
    q++;
  while (*q >= '0' && *q <= '9')
    q++;

  if (q != *argptr && (*q == 0 || *q == ' ' || *q == '\t' || *q == ',')
      && function_symbols == NULL)
    {
      struct symtabs_and_lines values;

      /* We found a token consisting of all digits -- at least one digit.  */
      values = decode_all_digits (self, argptr, q);
      do_cleanups (cleanup);
      return values;
    }

  /* Arg token is not digits => try it as a variable name
     Find the next token (everything up to end or next whitespace).  */

  if (**argptr == '$')		/* May be a convenience variable.  */
    /* One or two $ chars possible.  */
    p = skip_quoted (*argptr + (((*argptr)[1] == '$') ? 2 : 1));
  else if (is_quoted || is_squote_enclosed)
    {
      p = end_quote;
      if (p[-1] != '\'')
	error (_("Unmatched single quote."));
    }
  else if (is_objc_method)
    {
      /* allow word separators in method names for Obj-C.  */
      p = skip_quoted_chars (*argptr, NULL, "");
    }
  else
    {
      p = skip_quoted (*argptr);
    }

  /* Keep any important naming information.  */
  p = keep_name_info (p, p == saved_arg || is_linespec_boundary (p[-1]));

  copy = (char *) alloca (p - *argptr + 1);
  memcpy (copy, *argptr, p - *argptr);
  copy[p - *argptr] = '\0';
  if (p != *argptr
      && copy[0]
      && copy[0] == copy[p - *argptr - 1]
      && strchr (get_gdb_completer_quote_characters (), copy[0]) != NULL)
    {
      copy[p - *argptr - 1] = '\0';
      copy++;
    }
  else if (is_quoted || is_squote_enclosed)
    copy[p - *argptr - 1] = '\0';
  
  *argptr = skip_spaces (p);

  /* If it starts with $: may be a legitimate variable or routine name
     (e.g. HP-UX millicode routines such as $$dyncall), or it may
     be history value, or it may be a convenience variable.  */

  if (*copy == '$' && function_symbols == NULL)
    {
      struct symtabs_and_lines values;

      values = decode_dollar (self, copy);
      do_cleanups (cleanup);
      return values;
    }

  /* Try the token as a label, but only if no file was specified,
     because we can only really find labels in the current scope.  */

  if (VEC_length (symtab_p, self->file_symtabs) == 1
      && VEC_index (symtab_p, self->file_symtabs, 0) == NULL)
    {
      struct symtabs_and_lines label_result;
      if (decode_label (self, function_symbols, copy, &label_result))
	{
	  do_cleanups (cleanup);
	  return label_result;
	}
    }

  if (function_symbols)
    throw_exception (file_exception);

  /* Look up that token as a variable.
     If file specified, use that file's per-file block to start with.  */

  {
    struct symtabs_and_lines values;

    values = decode_variable (self, copy);
    do_cleanups (cleanup);
    return values;
  }
}

/* A constructor for linespec_state.  */

static void
linespec_state_constructor (struct linespec_state *self,
			    int flags,
			    struct symtab *default_symtab,
			    int default_line,
			    struct linespec_result *canonical)
{
  memset (self, 0, sizeof (*self));
  self->funfirstline = (flags & DECODE_LINE_FUNFIRSTLINE) ? 1 : 0;
  self->list_mode = (flags & DECODE_LINE_LIST_MODE) ? 1 : 0;
  self->default_symtab = default_symtab;
  self->default_line = default_line;
  self->canonical = canonical;
  self->program_space = current_program_space;
  self->addr_set = htab_create_alloc (10, hash_address_entry, eq_address_entry,
				      xfree, xcalloc, xfree);
}

/* A destructor for linespec_state.  */

static void
linespec_state_destructor (void *arg)
{
  struct linespec_state *self = arg;

  xfree (self->user_filename);
  xfree (self->user_function);
  VEC_free (symtab_p, self->file_symtabs);
  htab_delete (self->addr_set);
}

/* See linespec.h.  */

void
decode_line_full (char **argptr, int flags,
		  struct symtab *default_symtab,
		  int default_line, struct linespec_result *canonical,
		  const char *select_mode,
		  const char *filter)
{
  struct symtabs_and_lines result;
  struct linespec_state state;
  struct cleanup *cleanups;
  char *arg_start = *argptr;
  VEC (const_char_ptr) *filters = NULL;

  gdb_assert (canonical != NULL);
  /* The filter only makes sense for 'all'.  */
  gdb_assert (filter == NULL || select_mode == multiple_symbols_all);
  gdb_assert (select_mode == NULL
	      || select_mode == multiple_symbols_all
	      || select_mode == multiple_symbols_ask
	      || select_mode == multiple_symbols_cancel);
  gdb_assert ((flags & DECODE_LINE_LIST_MODE) == 0);

  linespec_state_constructor (&state, flags,
			      default_symtab, default_line, canonical);
  cleanups = make_cleanup (linespec_state_destructor, &state);
  save_current_program_space ();

  result = decode_line_internal (&state, argptr);

  gdb_assert (result.nelts == 1 || canonical->pre_expanded);
  gdb_assert (canonical->addr_string != NULL);
  canonical->pre_expanded = 1;

  /* Fill in the missing canonical names.  */
  if (result.nelts > 0)
    {
      int i;

      if (state.canonical_names == NULL)
	state.canonical_names = xcalloc (result.nelts, sizeof (char *));
      make_cleanup (xfree, state.canonical_names);
      for (i = 0; i < result.nelts; ++i)
	{
	  if (state.canonical_names[i] == NULL)
	    state.canonical_names[i] = savestring (arg_start,
						   *argptr - arg_start);
	  make_cleanup (xfree, state.canonical_names[i]);
	}
    }

  if (select_mode == NULL)
    {
      if (ui_out_is_mi_like_p (interp_ui_out (top_level_interpreter ())))
	select_mode = multiple_symbols_all;
      else
	select_mode = multiple_symbols_select_mode ();
    }

  if (select_mode == multiple_symbols_all)
    {
      if (filter != NULL)
	{
	  make_cleanup (VEC_cleanup (const_char_ptr), &filters);
	  VEC_safe_push (const_char_ptr, filters, filter);
	  filter_results (&state, &result, filters);
	}
      else
	convert_results_to_lsals (&state, &result);
    }
  else
    decode_line_2 (&state, &result, select_mode);

  do_cleanups (cleanups);
}

struct symtabs_and_lines
decode_line_1 (char **argptr, int flags,
	       struct symtab *default_symtab,
	       int default_line)
{
  struct symtabs_and_lines result;
  struct linespec_state state;
  struct cleanup *cleanups;

  linespec_state_constructor (&state, flags,
			      default_symtab, default_line, NULL);
  cleanups = make_cleanup (linespec_state_destructor, &state);
  save_current_program_space ();

  result = decode_line_internal (&state, argptr);
  do_cleanups (cleanups);
  return result;
}



/* First, some functions to initialize stuff at the beggining of the
   function.  */

static void
initialize_defaults (struct symtab **default_symtab, int *default_line)
{
  if (*default_symtab == 0)
    {
      /* Use whatever we have for the default source line.  We don't use
         get_current_or_default_symtab_and_line as it can recurse and call
	 us back!  */
      struct symtab_and_line cursal = 
	get_current_source_symtab_and_line ();
      
      *default_symtab = cursal.symtab;
      *default_line = cursal.line;
    }
}



/* Decode arg of the form *PC.  */

static struct symtabs_and_lines
decode_indirect (struct linespec_state *self, char **argptr)
{
  struct symtabs_and_lines values;
  CORE_ADDR pc;
  char *initial = *argptr;
  
  if (current_program_space->executing_startup)
    /* The error message doesn't really matter, because this case
       should only hit during breakpoint reset.  */
    throw_error (NOT_FOUND_ERROR, _("cannot evaluate expressions while "
				    "program space is in startup"));

  (*argptr)++;
  pc = value_as_address (parse_to_comma_and_eval (argptr));

  values.sals = (struct symtab_and_line *)
    xmalloc (sizeof (struct symtab_and_line));

  values.nelts = 1;
  values.sals[0] = find_pc_line (pc, 0);
  values.sals[0].pc = pc;
  values.sals[0].section = find_pc_overlay (pc);
  values.sals[0].explicit_pc = 1;

  if (self->canonical)
    self->canonical->addr_string = savestring (initial, *argptr - initial);

  return values;
}



/* Locate the first half of the linespec, ending in a colon, period,
   or whitespace.  (More or less.)  Also, check to see if *ARGPTR is
   enclosed in double quotes; if so, set is_quote_enclosed, advance
   ARGPTR past that and zero out the trailing double quote.
   If ARGPTR is just a simple name like "main", p will point to ""
   at the end.  */

static char *
locate_first_half (char **argptr, int *is_quote_enclosed)
{
  char *ii;
  char *p, *p1;
  int has_comma;

  /* Maybe we were called with a line range FILENAME:LINENUM,FILENAME:LINENUM
     and we must isolate the first half.  Outer layers will call again later
     for the second half.

     Don't count commas that appear in argument lists of overloaded
     functions, or in quoted strings.  It's stupid to go to this much
     trouble when the rest of the function is such an obvious roach hotel.  */
  ii = find_toplevel_char (*argptr, ',');
  has_comma = (ii != 0);

  /* Temporarily zap out second half to not confuse the code below.
     This is undone below.  Do not change ii!!  */
  if (has_comma)
    {
      *ii = '\0';
    }

  /* Maybe arg is FILE : LINENUM or FILE : FUNCTION.  May also be
     CLASS::MEMBER, or NAMESPACE::NAME.  Look for ':', but ignore
     inside of <>.  */

  p = *argptr;
  if (p[0] == '"')
    {
      *is_quote_enclosed = 1;
      (*argptr)++;
      p++;
    }
  else
    {
      *is_quote_enclosed = 0;
      if (strchr (get_gdb_completer_quote_characters (), *p))
	{
	  ++(*argptr);
	  ++p;
	}
    }


  /* Check for a drive letter in the filename.  This is done on all hosts
     to capture cross-compilation environments.  On Unixen, directory
     separators are illegal in filenames, so if the user enters "e:/foo.c",
     he is referring to a directory named "e:" and a source file named
     "foo.c", and we still want to keep these two pieces together.  */
  if (isalpha (p[0]) && p[1] == ':' && IS_DIR_SEPARATOR (p[2]))
    p += 3;

  for (; *p; p++)
    {
      if (p[0] == '<')
	{
	  char *temp_end = find_template_name_end (p);

	  if (!temp_end)
	    error (_("malformed template specification in command"));
	  p = temp_end;
	}

      if (p[0] == '(')
	p = find_method_overload_end (p);

      /* Check for a colon and a plus or minus and a [ (which
         indicates an Objective-C method).  */
      if (is_objc_method_format (p))
	{
	  break;
	}
      /* Check for the end of the first half of the linespec.  End of
         line, a tab, a colon or a space.  But if enclosed in double
	 quotes we do not break on enclosed spaces.  */
      if (!*p
	  || p[0] == '\t'
	  || (p[0] == ':')
	  || ((p[0] == ' ') && !*is_quote_enclosed))
	break;
      if (p[0] == '.' && strchr (p, ':') == NULL)
	{
	  /* Java qualified method.  Find the *last* '.', since the
	     others are package qualifiers.  Stop at any open parenthesis
	     which might provide overload information.  */
	  for (p1 = p; *p1 && *p1 != '('; p1++)
	    {
	      if (*p1 == '.')
		p = p1;
	    }
	  break;
	}
    }
  p = skip_spaces (p);

  /* If the closing double quote was left at the end, remove it.  */
  if (*is_quote_enclosed)
    {
      char *closing_quote = strchr (p - 1, '"');

      if (closing_quote && closing_quote[1] == '\0')
	*closing_quote = '\0';
    }

  /* Now that we've safely parsed the first half, put back ',' so
     outer layers can see it.  */
  if (has_comma)
    *ii = ',';

  return p;
}



/* Here's where we recognise an Objective-C Selector.  An Objective C
   selector may be implemented by more than one class, therefore it
   may represent more than one method/function.  This gives us a
   situation somewhat analogous to C++ overloading.  If there's more
   than one method that could represent the selector, then use some of
   the existing C++ code to let the user choose one.  */

static struct symtabs_and_lines
decode_objc (struct linespec_state *self, char **argptr)
{
  struct collect_info info;
  VEC (const_char_ptr) *symbol_names = NULL;
  char *new_argptr;
  struct cleanup *cleanup = make_cleanup (VEC_cleanup (const_char_ptr),
					  &symbol_names);

  info.state = self;
  info.result.sals = NULL;
  info.result.nelts = 0;

  new_argptr = find_imps (*argptr, &symbol_names); 
  if (VEC_empty (const_char_ptr, symbol_names))
    {
      do_cleanups (cleanup);
      return info.result;
    }

  add_all_symbol_names_from_pspace (&info, NULL, symbol_names);

  if (info.result.nelts > 0)
    {
      char *saved_arg;

      saved_arg = alloca (new_argptr - *argptr + 1);
      memcpy (saved_arg, *argptr, new_argptr - *argptr);
      saved_arg[new_argptr - *argptr] = '\0';

      if (self->canonical)
	{
	  self->canonical->pre_expanded = 1;
	  if (self->user_filename)
	    self->canonical->addr_string
	      = xstrprintf ("%s:%s", self->user_filename, saved_arg);
	  else
	    self->canonical->addr_string = xstrdup (saved_arg);
	}
    }

  *argptr = new_argptr;

  do_cleanups (cleanup);
  return info.result;
}

/* This handles C++ and Java compound data structures.  P should point
   at the first component separator, i.e. double-colon or period.  As
   an example, on entrance to this function we could have ARGPTR
   pointing to "AAA::inA::fun" and P pointing to "::inA::fun".  */

static struct symtabs_and_lines
decode_compound (struct linespec_state *self,
		 char **argptr, char *the_real_saved_arg, char *p)
{
  struct symtabs_and_lines values;
  char *p2;
  char *saved_arg2 = *argptr;
  char *temp_end;
  struct symbol *sym;
  char *copy;
  VEC (symbolp) *sym_classes;
  char *saved_arg, *class_name;
  struct cleanup *cleanup = make_cleanup (null_cleanup, NULL);

  /* If the user specified any completer quote characters in the input,
     strip them.  They are superfluous.  */
  saved_arg = alloca (strlen (the_real_saved_arg) + 1);
  {
    char *dst = saved_arg;
    char *src = the_real_saved_arg;
    char *quotes = get_gdb_completer_quote_characters ();
    while (*src != '\0')
      {
	if (strchr (quotes, *src) == NULL)
	  *dst++ = *src;
	++src;
      }
    *dst = '\0';
  }

  /* First check for "global" namespace specification, of the form
     "::foo".  If found, skip over the colons and jump to normal
     symbol processing.  I.e. the whole line specification starts with
     "::" (note the condition that *argptr == p).  */
  if (p[0] == ':' 
      && ((*argptr == p) || (p[-1] == ' ') || (p[-1] == '\t')))
    saved_arg2 += 2;

  /* Given our example "AAA::inA::fun", we have two cases to consider:

     1) AAA::inA is the name of a class.  In that case, presumably it
        has a method called "fun"; we then look up that method using
        find_method.

     2) AAA::inA isn't the name of a class.  In that case, either the
        user made a typo, AAA::inA is the name of a namespace, or it is
        the name of a minimal symbol.
	In this case we just delegate to decode_variable.

     Thus, our first task is to find everything before the last set of
     double-colons and figure out if it's the name of a class.  So we
     first loop through all of the double-colons.  */

  p2 = p;		/* Save for restart.  */

  /* This is very messy.  Following the example above we have now the
     following pointers:
     p -> "::inA::fun"
     argptr -> "AAA::inA::fun
     saved_arg -> "AAA::inA::fun
     saved_arg2 -> "AAA::inA::fun
     p2 -> "::inA::fun".  */

  /* In the loop below, with these strings, we'll make 2 passes, each
     is marked in comments.  */

  while (1)
    {
      static char *break_characters = " \t(";

      /* Move pointer up to next possible class/namespace token.  */

      p = p2 + 1;	/* Restart with old value +1.  */

      /* PASS1: at this point p2->"::inA::fun", so p->":inA::fun",
	 i.e. if there is a double-colon, p will now point to the
	 second colon.  */
      /* PASS2: p2->"::fun", p->":fun" */

      /* Move pointer ahead to next double-colon.  */
      while (*p
	     && strchr (break_characters, *p) == NULL
	     && strchr (get_gdb_completer_quote_characters (), *p) == NULL)
	{
	  if (current_language->la_language == language_cplus)
	    p += cp_validate_operator (p);

	  if (p[0] == '<')
	    {
	      temp_end = find_template_name_end (p);
	      if (!temp_end)
		error (_("malformed template specification in command"));
	      p = temp_end;
	    }
	  /* Note that, since, at the start of this loop, p would be
	     pointing to the second colon in a double-colon, we only
	     satisfy the condition below if there is another
	     double-colon to the right (after).  I.e. there is another
	     component that can be a class or a namespace.  I.e, if at
	     the beginning of this loop (PASS1), we had
	     p->":inA::fun", we'll trigger this when p has been
	     advanced to point to "::fun".  */
	  /* PASS2: we will not trigger this.  */
	  else if ((p[0] == ':') && (p[1] == ':'))
	    break;	/* Found double-colon.  */
	  else
	    {
	      /* PASS2: We'll keep getting here, until P points to one of the
		 break characters, at which point we exit this loop.  */
	      if (*p)
		{
		  if (p[1] == '('
		      && strncmp (&p[1], CP_ANONYMOUS_NAMESPACE_STR,
				  CP_ANONYMOUS_NAMESPACE_LEN) == 0)
		    p += CP_ANONYMOUS_NAMESPACE_LEN;
		  else if (strchr (break_characters, *p) == NULL)
		    ++p;
		}
	    }
	}

      if (*p != ':')
	break;		/* Out of the while (1).  This would happen
			   for instance if we have looked up
			   unsuccessfully all the components of the
			   string, and p->""(PASS2).  */

      /* We get here if p points to one of the break characters or "" (i.e.,
	 string ended).  */
      /* Save restart for next time around.  */
      p2 = p;
      /* Restore argptr as it was on entry to this function.  */
      *argptr = saved_arg2;
      /* PASS1: at this point p->"::fun" argptr->"AAA::inA::fun",
	 p2->"::fun".  */

      /* All ready for next pass through the loop.  */
    }			/* while (1) */


  /* Start of lookup in the symbol tables.  */

  /* Lookup in the symbol table the substring between argptr and
     p.  Note, this call changes the value of argptr.  */
  /* Before the call, argptr->"AAA::inA::fun",
     p->"", p2->"::fun".  After the call: argptr->"fun", p, p2
     unchanged.  */
  sym_classes = lookup_prefix_sym (argptr, p2, self->file_symtabs,
				   &class_name);
  make_cleanup (VEC_cleanup (symbolp), &sym_classes);
  make_cleanup (xfree, class_name);

  /* If a class has been found, then we're in case 1 above.  So we
     look up "fun" as a method of those classes.  */
  if (!VEC_empty (symbolp, sym_classes))
    {
      /* Arg token is not digits => try it as a function name.
	 Find the next token (everything up to end or next
	 blank).  */
      if (**argptr
	  && strchr (get_gdb_completer_quote_characters (),
		     **argptr) != NULL)
	{
	  p = skip_quoted (*argptr);
	  *argptr = *argptr + 1;
	}
      else
	{
	  /* At this point argptr->"fun".  */
	  char *a;

	  p = *argptr;
	  while (*p && *p != ' ' && *p != '\t' && *p != ',' && *p != ':'
		 && *p != '(')
	    p++;
	  /* At this point p->"".  String ended.  */
	  /* Nope, C++ operators could have spaces in them
	     ("foo::operator <" or "foo::operator delete []").
	     I apologize, this is a bit hacky...  */
	  if (current_language->la_language == language_cplus
	      && *p == ' ' && p - 8 - *argptr + 1 > 0)
	    {
	      /* The above loop has already swallowed "operator".  */
	      p += cp_validate_operator (p - 8) - 8;
	    }

	  /* Keep any important naming information.  */
	  p = keep_name_info (p, 1);
	}

      /* Allocate our own copy of the substring between argptr and
	 p.  */
      copy = (char *) alloca (p - *argptr + 1);
      memcpy (copy, *argptr, p - *argptr);
      copy[p - *argptr] = '\0';
      if (p != *argptr
	  && copy[p - *argptr - 1]
	  && strchr (get_gdb_completer_quote_characters (),
		     copy[p - *argptr - 1]) != NULL)
	copy[p - *argptr - 1] = '\0';

      /* At this point copy->"fun", p->"".  */

      /* No line number may be specified.  */
      *argptr = skip_spaces (p);
      /* At this point arptr->"".  */

      /* Look for copy as a method of sym_class.  */
      /* At this point copy->"fun", sym_class is "AAA:inA",
	 saved_arg->"AAA::inA::fun".  This concludes the scanning of
	 the string for possible components matches.  If we find it
	 here, we return.  If not, and we are at the and of the string,
	 we'll lookup the whole string in the symbol tables.  */

      values = find_method (self, saved_arg, copy, class_name, sym_classes);

      do_cleanups (cleanup);
      return values;
    } /* End if symbol found.  */


  /* We couldn't find a class, so we're in case 2 above.  We check the
     entire name as a symbol instead.  The simplest way to do this is
     to just throw an exception and let our caller fall through to
     decode_variable.  */

  throw_error (NOT_FOUND_ERROR, _("see caller, this text doesn't matter"));
}

/* An instance of this type is used when collecting prefix symbols for
   decode_compound.  */

struct decode_compound_collector
{
  /* The result vector.  */
  VEC (symbolp) *symbols;

  /* A hash table of all symbols we found.  We use this to avoid
     adding any symbol more than once.  */
  htab_t unique_syms;
};

/* A callback for iterate_over_symbols that is used by
   lookup_prefix_sym to collect type symbols.  */

static int
collect_one_symbol (struct symbol *sym, void *d)
{
  struct decode_compound_collector *collector = d;
  void **slot;
  struct type *t;

  if (SYMBOL_CLASS (sym) != LOC_TYPEDEF)
    return 1;

  t = SYMBOL_TYPE (sym);
  CHECK_TYPEDEF (t);
  if (TYPE_CODE (t) != TYPE_CODE_STRUCT
      && TYPE_CODE (t) != TYPE_CODE_UNION
      && TYPE_CODE (t) != TYPE_CODE_NAMESPACE)
    return 1;

  slot = htab_find_slot (collector->unique_syms, sym, INSERT);
  if (!*slot)
    {
      *slot = sym;
      VEC_safe_push (symbolp, collector->symbols, sym);
    }

  return 1;
}

/* Return the symbol corresponding to the substring of *ARGPTR ending
   at P, allowing whitespace.  Also, advance *ARGPTR past the symbol
   name in question, the compound object separator ("::" or "."), and
   whitespace.  Note that *ARGPTR is changed whether or not the
   this call finds anything (i.e we return NULL).  As an
   example, say ARGPTR is "AAA::inA::fun" and P is "::inA::fun".  */

static VEC (symbolp) *
lookup_prefix_sym (char **argptr, char *p, VEC (symtab_p) *file_symtabs,
		   char **class_name)
{
  char *p1;
  char *copy;
  int ix;
  struct symtab *elt;
  struct decode_compound_collector collector;
  struct cleanup *outer;
  struct cleanup *cleanup;
  struct block *search_block;

  /* Extract the class name.  */
  p1 = p;
  while (p != *argptr && p[-1] == ' ')
    --p;
  copy = (char *) xmalloc (p - *argptr + 1);
  memcpy (copy, *argptr, p - *argptr);
  copy[p - *argptr] = 0;
  *class_name = copy;
  outer = make_cleanup (xfree, copy);

  /* Discard the class name from the argptr.  */
  p = p1 + (p1[0] == ':' ? 2 : 1);
  p = skip_spaces (p);
  *argptr = p;

  /* At this point p1->"::inA::fun", p->"inA::fun" copy->"AAA",
     argptr->"inA::fun".  */

  collector.symbols = NULL;
  make_cleanup (VEC_cleanup (symbolp), &collector.symbols);

  collector.unique_syms = htab_create_alloc (1, htab_hash_pointer,
					     htab_eq_pointer, NULL,
					     xcalloc, xfree);
  cleanup = make_cleanup_htab_delete (collector.unique_syms);

  for (ix = 0; VEC_iterate (symtab_p, file_symtabs, ix, elt); ++ix)
    {
      if (elt == NULL)
	{
	  iterate_over_all_matching_symtabs (copy, STRUCT_DOMAIN,
					     collect_one_symbol, &collector,
					     NULL);
	  iterate_over_all_matching_symtabs (copy, VAR_DOMAIN,
					     collect_one_symbol, &collector,
					     NULL);
	}
      else
	{
	  struct block *search_block;

	  /* Program spaces that are executing startup should have
	     been filtered out earlier.  */
	  gdb_assert (!SYMTAB_PSPACE (elt)->executing_startup);
	  set_current_program_space (SYMTAB_PSPACE (elt));
	  search_block = get_search_block (elt);
	  LA_ITERATE_OVER_SYMBOLS (search_block, copy, STRUCT_DOMAIN,
				   collect_one_symbol, &collector);
	  LA_ITERATE_OVER_SYMBOLS (search_block, copy, VAR_DOMAIN,
				   collect_one_symbol, &collector);
	}
    }

  do_cleanups (cleanup);
  discard_cleanups (outer);
  return collector.symbols;
}

/* A qsort comparison function for symbols.  The resulting order does
   not actually matter; we just need to be able to sort them so that
   symbols with the same program space end up next to each other.  */

static int
compare_symbols (const void *a, const void *b)
{
  struct symbol * const *sa = a;
  struct symbol * const *sb = b;
  uintptr_t uia, uib;

  uia = (uintptr_t) SYMTAB_PSPACE (SYMBOL_SYMTAB (*sa));
  uib = (uintptr_t) SYMTAB_PSPACE (SYMBOL_SYMTAB (*sb));

  if (uia < uib)
    return -1;
  if (uia > uib)
    return 1;

  uia = (uintptr_t) *sa;
  uib = (uintptr_t) *sb;

  if (uia < uib)
    return -1;
  if (uia > uib)
    return 1;

  return 0;
}

/* Look for all the matching instances of each symbol in NAMES.  Only
   instances from PSPACE are considered; other program spaces are
   handled by our caller.  If PSPACE is NULL, then all program spaces
   are considered.  Results are stored into INFO.  */

static void
add_all_symbol_names_from_pspace (struct collect_info *info,
				  struct program_space *pspace,
				  VEC (const_char_ptr) *names)
{
  int ix;
  const char *iter;

  for (ix = 0; VEC_iterate (const_char_ptr, names, ix, iter); ++ix)
    add_matching_symbols_to_info (iter, info, pspace);
}

static void
find_superclass_methods (VEC (typep) *superclasses,
			 const char *name,
			 VEC (const_char_ptr) **result_names)
{
  int old_len = VEC_length (const_char_ptr, *result_names);
  VEC (typep) *iter_classes;
  struct cleanup *cleanup = make_cleanup (null_cleanup, NULL);

  iter_classes = superclasses;
  while (1)
    {
      VEC (typep) *new_supers = NULL;
      int ix;
      struct type *t;

      make_cleanup (VEC_cleanup (typep), &new_supers);
      for (ix = 0; VEC_iterate (typep, iter_classes, ix, t); ++ix)
	find_methods (t, name, result_names, &new_supers);

      if (VEC_length (const_char_ptr, *result_names) != old_len
	  || VEC_empty (typep, new_supers))
	break;

      iter_classes = new_supers;
    }

  do_cleanups (cleanup);
}

/* This finds the method COPY in the class whose type is given by one
   of the symbols in SYM_CLASSES.  */

static struct symtabs_and_lines
find_method (struct linespec_state *self, char *saved_arg,
	     char *copy, const char *class_name, VEC (symbolp) *sym_classes)
{
  char *canon;
  struct symbol *sym;
  struct cleanup *cleanup = make_cleanup (null_cleanup, NULL);
  int ix;
  int last_result_len;
  VEC (typep) *superclass_vec;
  VEC (const_char_ptr) *result_names;
  struct collect_info info;
  char *name_iter;

  /* NAME is typed by the user: it needs to be canonicalized before
     searching the symbol tables.  */
  canon = cp_canonicalize_string_no_typedefs (copy);
  if (canon != NULL)
    {
      copy = canon;
      make_cleanup (xfree, copy);
    }

  /* Sort symbols so that symbols with the same program space are next
     to each other.  */
  qsort (VEC_address (symbolp, sym_classes),
	 VEC_length (symbolp, sym_classes),
	 sizeof (symbolp),
	 compare_symbols);

  info.state = self;
  info.result.sals = NULL;
  info.result.nelts = 0;

  /* Iterate over all the types, looking for the names of existing
     methods matching COPY.  If we cannot find a direct method in a
     given program space, then we consider inherited methods; this is
     not ideal (ideal would be to respect C++ hiding rules), but it
     seems good enough and is what GDB has historically done.  We only
     need to collect the names because later we find all symbols with
     those names.  This loop is written in a somewhat funny way
     because we collect data across the program space before deciding
     what to do.  */
  superclass_vec = NULL;
  make_cleanup (VEC_cleanup (typep), &superclass_vec);
  result_names = NULL;
  make_cleanup (VEC_cleanup (const_char_ptr), &result_names);
  last_result_len = 0;
  for (ix = 0; VEC_iterate (symbolp, sym_classes, ix, sym); ++ix)
    {
      struct type *t;
      struct program_space *pspace;

      /* Program spaces that are executing startup should have
	 been filtered out earlier.  */
      gdb_assert (!SYMTAB_PSPACE (SYMBOL_SYMTAB (sym))->executing_startup);
      pspace = SYMTAB_PSPACE (SYMBOL_SYMTAB (sym));
      set_current_program_space (pspace);
      t = check_typedef (SYMBOL_TYPE (sym));
      find_methods (t, copy, &result_names, &superclass_vec);

      /* Handle all items from a single program space at once; and be
	 sure not to miss the last batch.  */
      if (ix == VEC_length (symbolp, sym_classes) - 1
	  || (pspace
	      != SYMTAB_PSPACE (SYMBOL_SYMTAB (VEC_index (symbolp, sym_classes,
							  ix + 1)))))
	{
	  /* If we did not find a direct implementation anywhere in
	     this program space, consider superclasses.  */
	  if (VEC_length (const_char_ptr, result_names) == last_result_len)
	    find_superclass_methods (superclass_vec, copy, &result_names);

	  /* We have a list of candidate symbol names, so now we
	     iterate over the symbol tables looking for all
	     matches in this pspace.  */
	  add_all_symbol_names_from_pspace (&info, pspace, result_names);

	  VEC_truncate (typep, superclass_vec, 0);
	  last_result_len = VEC_length (const_char_ptr, result_names);
	}
    }

  if (info.result.nelts > 0)
    {
      if (self->canonical)
	{
	  self->canonical->pre_expanded = 1;
	  if (self->user_filename)
	    self->canonical->addr_string
	      = xstrprintf ("%s:%s", self->user_filename, saved_arg);
	  else
	    self->canonical->addr_string = xstrdup (saved_arg);
	}

      do_cleanups (cleanup);

      return info.result;
    }

  if (copy[0] == '~')
    cplusplus_error (saved_arg,
		     "the class `%s' does not have destructor defined\n",
		     class_name);
  else
    cplusplus_error (saved_arg,
		     "the class %s does not have any method named %s\n",
		     class_name, copy);
}



/* This object is used when collecting all matching symtabs.  */

struct symtab_collector
{
  /* The result vector of symtabs.  */
  VEC (symtab_p) *symtabs;

  /* This is used to ensure the symtabs are unique.  */
  htab_t symtab_table;
};

/* Callback for iterate_over_symtabs.  */

static int
add_symtabs_to_list (struct symtab *symtab, void *d)
{
  struct symtab_collector *data = d;
  void **slot;

  slot = htab_find_slot (data->symtab_table, symtab, INSERT);
  if (!*slot)
    {
      *slot = symtab;
      VEC_safe_push (symtab_p, data->symtabs, symtab);
    }

  return 0;
}

/* Given a file name, return a VEC of all matching symtabs.  */

static VEC (symtab_p) *
collect_symtabs_from_filename (const char *file)
{
  struct symtab_collector collector;
  struct cleanup *cleanups;
  struct program_space *pspace;

  collector.symtabs = NULL;
  collector.symtab_table = htab_create (1, htab_hash_pointer, htab_eq_pointer,
					NULL);
  cleanups = make_cleanup_htab_delete (collector.symtab_table);

  /* Find that file's data.  */
  ALL_PSPACES (pspace)
  {
    if (pspace->executing_startup)
      continue;

    set_current_program_space (pspace);
    iterate_over_symtabs (file, add_symtabs_to_list, &collector);
  }

  do_cleanups (cleanups);
  return collector.symtabs;
}

/* Return all the symtabs associated to the filename given by the
   substring of *ARGPTR ending at P, and advance ARGPTR past that
   filename.  */

static VEC (symtab_p) *
symtabs_from_filename (char **argptr, char *p, int is_quote_enclosed,
		       char **user_filename)
{
  char *p1;
  char *copy;
  struct cleanup *outer;
  VEC (symtab_p) *result;
  
  p1 = p;
  while (p != *argptr && p[-1] == ' ')
    --p;
  if ((*p == '"') && is_quote_enclosed)
    --p;
  copy = xmalloc (p - *argptr + 1);
  outer = make_cleanup (xfree, copy);
  memcpy (copy, *argptr, p - *argptr);
  /* It may have the ending quote right after the file name.  */
  if ((is_quote_enclosed && copy[p - *argptr - 1] == '"')
      || copy[p - *argptr - 1] == '\'')
    copy[p - *argptr - 1] = 0;
  else
    copy[p - *argptr] = 0;

  result = collect_symtabs_from_filename (copy);

  if (VEC_empty (symtab_p, result))
    {
      if (!have_full_symbols () && !have_partial_symbols ())
	throw_error (NOT_FOUND_ERROR,
		     _("No symbol table is loaded.  "
		       "Use the \"file\" command."));
      throw_error (NOT_FOUND_ERROR, _("No source file named %s."), copy);
    }

  /* Discard the file name from the arg.  */
  if (*p1 == '\0')
    *argptr = p1;
  else
    *argptr = skip_spaces (p1 + 1);

  discard_cleanups (outer);
  *user_filename = copy;
  return result;
}

/* A callback used by iterate_over_all_matching_symtabs that collects
   symbols for find_function_symbols.  */

static int
collect_function_symbols (struct symbol *sym, void *arg)
{
  VEC (symbolp) **syms = arg;

  if (SYMBOL_CLASS (sym) == LOC_BLOCK)
    VEC_safe_push (symbolp, *syms, sym);

  return 1;
}

/* Look up a function symbol in *ARGPTR.  If found, advance *ARGPTR
   and return the symbol.  If not found, return NULL.  */

static VEC (symbolp) *
find_function_symbols (char **argptr, char *p, int is_quote_enclosed,
		       char **user_function)
{
  char *p1;
  char *copy;
  VEC (symbolp) *result = NULL;

  p1 = p;
  while (p != *argptr && p[-1] == ' ')
    --p;
  if ((*p == '"') && is_quote_enclosed)
    --p;
  copy = (char *) xmalloc (p - *argptr + 1);
  *user_function = copy;
  memcpy (copy, *argptr, p - *argptr);
  /* It may have the ending quote right after the file name.  */
  if ((is_quote_enclosed && copy[p - *argptr - 1] == '"')
      || copy[p - *argptr - 1] == '\'')
    copy[p - *argptr - 1] = 0;
  else
    copy[p - *argptr] = 0;

  iterate_over_all_matching_symtabs (copy, VAR_DOMAIN,
				     collect_function_symbols, &result, NULL);

  if (VEC_empty (symbolp, result))
    VEC_free (symbolp, result);
  else
    {
      /* Discard the file name from the arg.  */
      *argptr = skip_spaces (p1 + 1);
    }

  return result;
}



/* A helper for decode_all_digits that handles the 'list_mode' case.  */

static void
decode_digits_list_mode (struct linespec_state *self,
			 struct symtabs_and_lines *values,
			 struct symtab_and_line val)
{
  int ix;
  struct symtab *elt;

  gdb_assert (self->list_mode);

  for (ix = 0; VEC_iterate (symtab_p, self->file_symtabs, ix, elt); ++ix)
    {
      /* The logic above should ensure this.  */
      gdb_assert (elt != NULL);

      set_current_program_space (SYMTAB_PSPACE (elt));

      /* Simplistic search just for the list command.  */
      val.symtab = find_line_symtab (elt, val.line, NULL, NULL);
      if (val.symtab == NULL)
	val.symtab = elt;
      val.pspace = SYMTAB_PSPACE (elt);
      val.pc = 0;
      val.explicit_line = 1;

      add_sal_to_sals (self, values, &val, NULL);
    }
}

/* A helper for decode_all_digits that iterates over the symtabs,
   adding lines to the VEC.  */

static void
decode_digits_ordinary (struct linespec_state *self,
			int line,
			struct symtabs_and_lines *sals,
			struct linetable_entry **best_entry)
{
  int ix;
  struct symtab *elt;

  for (ix = 0; VEC_iterate (symtab_p, self->file_symtabs, ix, elt); ++ix)
    {
      int i;
      VEC (CORE_ADDR) *pcs;
      CORE_ADDR pc;

      /* The logic above should ensure this.  */
      gdb_assert (elt != NULL);

      set_current_program_space (SYMTAB_PSPACE (elt));

      pcs = find_pcs_for_symtab_line (elt, line, best_entry);
      for (i = 0; VEC_iterate (CORE_ADDR, pcs, i, pc); ++i)
	{
	  struct symtab_and_line sal;

	  init_sal (&sal);
	  sal.pspace = SYMTAB_PSPACE (elt);
	  sal.symtab = elt;
	  sal.line = line;
	  sal.pc = pc;
	  add_sal_to_sals_basic (sals, &sal);
	}

      VEC_free (CORE_ADDR, pcs);
    }
}

/* This decodes a line where the argument is all digits (possibly
   preceded by a sign).  Q should point to the end of those digits;
   the other arguments are as usual.  */

static struct symtabs_and_lines
decode_all_digits (struct linespec_state *self,
		   char **argptr,
		   char *q)
{
  struct symtabs_and_lines values;
  struct symtab_and_line val;
  int use_default = 0;
  char *saved_arg = *argptr;

  enum sign
    {
      none, plus, minus
    }
  sign = none;

  init_sal (&val);
  values.sals = NULL;
  values.nelts = 0;

  /* This is where we need to make sure that we have good defaults.
     We must guarantee that this section of code is never executed
     when we are called with just a function name, since
     set_default_source_symtab_and_line uses
     select_source_symtab that calls us with such an argument.  */

  if (VEC_length (symtab_p, self->file_symtabs) == 1
      && VEC_index (symtab_p, self->file_symtabs, 0) == NULL)
    {
      set_current_program_space (self->program_space);

      /* Make sure we have at least a default source file.  */
      set_default_source_symtab_and_line ();
      initialize_defaults (&self->default_symtab, &self->default_line);
      VEC_pop (symtab_p, self->file_symtabs);
      VEC_free (symtab_p, self->file_symtabs);
      self->file_symtabs
	= collect_symtabs_from_filename (self->default_symtab->filename);
      use_default = 1;
    }

  if (**argptr == '+')
    sign = plus, (*argptr)++;
  else if (**argptr == '-')
    sign = minus, (*argptr)++;
  val.line = atoi (*argptr);
  switch (sign)
    {
    case plus:
      if (q == *argptr)
	val.line = 5;
      if (use_default)
	val.line = self->default_line + val.line;
      break;
    case minus:
      if (q == *argptr)
	val.line = 15;
      if (use_default)
	val.line = self->default_line - val.line;
      else
	val.line = 1;
      break;
    case none:
      break;		/* No need to adjust val.line.  */
    }

  *argptr = skip_spaces (q);

  if (self->list_mode)
    decode_digits_list_mode (self, &values, val);
  else
    {
      struct linetable_entry *best_entry = NULL;
      int *filter;
      struct block **blocks;
      struct cleanup *cleanup;
      struct symtabs_and_lines intermediate_results;
      int i, j;

      intermediate_results.sals = NULL;
      intermediate_results.nelts = 0;

      decode_digits_ordinary (self, val.line, &intermediate_results,
			      &best_entry);
      if (intermediate_results.nelts == 0 && best_entry != NULL)
	decode_digits_ordinary (self, best_entry->line, &intermediate_results,
				&best_entry);

      cleanup = make_cleanup (xfree, intermediate_results.sals);

      /* For optimized code, compiler can scatter one source line
	 accross disjoint ranges of PC values, even when no duplicate
	 functions or inline functions are involved.  For example,
	 'for (;;)' inside non-template non-inline non-ctor-or-dtor
	 function can result in two PC ranges.  In this case, we don't
	 want to set breakpoint on first PC of each range.  To filter
	 such cases, we use containing blocks -- for each PC found
	 above we see if there are other PCs that are in the same
	 block.  If yes, the other PCs are filtered out.  */

      filter = xmalloc (intermediate_results.nelts * sizeof (int));
      make_cleanup (xfree, filter);
      blocks = xmalloc (intermediate_results.nelts * sizeof (struct block *));
      make_cleanup (xfree, blocks);

      for (i = 0; i < intermediate_results.nelts; ++i)
	{
	  set_current_program_space (intermediate_results.sals[i].pspace);

	  filter[i] = 1;
	  blocks[i] = block_for_pc_sect (intermediate_results.sals[i].pc,
					 intermediate_results.sals[i].section);
	}

      for (i = 0; i < intermediate_results.nelts; ++i)
	{
	  if (blocks[i] != NULL)
	    for (j = i + 1; j < intermediate_results.nelts; ++j)
	      {
		if (blocks[j] == blocks[i])
		  {
		    filter[j] = 0;
		    break;
		  }
	      }
	}

      for (i = 0; i < intermediate_results.nelts; ++i)
	if (filter[i])
	  {
	    struct symbol *sym = (blocks[i]
				  ? block_containing_function (blocks[i])
				  : NULL);

	    if (self->funfirstline)
	      skip_prologue_sal (&intermediate_results.sals[i]);
	    /* Make sure the line matches the request, not what was
	       found.  */
	    intermediate_results.sals[i].line = val.line;
	    add_sal_to_sals (self, &values, &intermediate_results.sals[i],
			     sym ? SYMBOL_NATURAL_NAME (sym) : NULL);
	  }

      do_cleanups (cleanup);
    }

  if (values.nelts == 0)
    {
      if (self->user_filename)
	throw_error (NOT_FOUND_ERROR, _("No line %d in file \"%s\"."),
		     val.line, self->user_filename);
      else
	throw_error (NOT_FOUND_ERROR, _("No line %d in the current file."),
		     val.line);
    }

  if (self->canonical)
    {
      char *copy = savestring (saved_arg, q - saved_arg);

      self->canonical->pre_expanded = 1;
      gdb_assert (self->user_filename || use_default);
      self->canonical->addr_string
	= xstrprintf ("%s:%s", (self->user_filename
				? self->user_filename
				: self->default_symtab->filename),
		      copy);
      xfree (copy);
    }

  return values;
}



/* Decode a linespec starting with a dollar sign.  */

static struct symtabs_and_lines
decode_dollar (struct linespec_state *self, char *copy)
{
  LONGEST valx;
  int index = 0;
  struct symtabs_and_lines values;
  struct symtab_and_line val;
  char *p;
  struct symbol *sym;
  struct minimal_symbol *msymbol;
  int ix;
  struct symtab *elt;

  p = (copy[1] == '$') ? copy + 2 : copy + 1;
  while (*p >= '0' && *p <= '9')
    p++;
  if (!*p)		/* Reached end of token without hitting non-digit.  */
    {
      /* We have a value history reference.  */
      struct value *val_history;

      sscanf ((copy[1] == '$') ? copy + 2 : copy + 1, "%d", &index);
      val_history = access_value_history ((copy[1] == '$') ? -index : index);
      if (TYPE_CODE (value_type (val_history)) != TYPE_CODE_INT)
	error (_("History values used in line "
		 "specs must have integer values."));
      valx = value_as_long (val_history);
    }
  else
    {
      /* Not all digits -- may be user variable/function or a
	 convenience variable.  */

      volatile struct gdb_exception exc;

      /* Avoid "may be used uninitialized" warning.  */
      values.sals = NULL;
      values.nelts = 0;

      TRY_CATCH (exc, RETURN_MASK_ERROR)
	{
	  values = decode_variable (self, copy);
	}

      if (exc.reason == 0)
	return values;

      if (exc.error != NOT_FOUND_ERROR)
	throw_exception (exc);

      /* Not a user variable or function -- must be convenience variable.  */
      if (!get_internalvar_integer (lookup_internalvar (copy + 1), &valx))
	error (_("Convenience variables used in line "
		 "specs must have integer values."));
    }

  init_sal (&val);

  values.sals = NULL;
  values.nelts = 0;

  for (ix = 0; VEC_iterate (symtab_p, self->file_symtabs, ix, elt); ++ix)
    {
      if (elt == NULL)
	{
	  elt = self->default_symtab;
	  set_current_program_space (self->program_space);
	}
      else
	set_current_program_space (SYMTAB_PSPACE (elt));

      /* Either history value or convenience value from above, in valx.  */
      val.symtab = elt;
      val.line = valx;
      val.pc = 0;
      val.pspace = elt ? SYMTAB_PSPACE (elt) : current_program_space;

      add_sal_to_sals (self, &values, &val, NULL);
    }

  if (self->canonical)
    {
      self->canonical->pre_expanded = 1;
      if (self->user_filename)
	self->canonical->addr_string = xstrprintf ("%s:%s",
						   self->user_filename, copy);
      else
	self->canonical->addr_string = xstrdup (copy);
    }

  return values;
}



/* A helper for decode_line_1 that tries to find a label.  The label
   is searched for in the current block.
   FUNCTION_SYMBOLS is a list of the enclosing functions; or NULL if none
   specified.
   COPY is the name of the label to find.
   CANONICAL is the same as the "canonical" argument to decode_line_1.
   RESULT is a pointer to a symtabs_and_lines structure which will be
   filled in on success.
   This function returns 1 if a label was found, 0 otherwise.  */

static int
decode_label (struct linespec_state *self,
	      VEC (symbolp) *function_symbols, char *copy,
	      struct symtabs_and_lines *result)
{
  struct symbol *fn_sym;
  int ix;

  if (function_symbols == NULL)
    {
      struct block *block;
      struct symbol *sym;
      struct symtab_and_line sal;
      struct symtabs_and_lines values;

      values.nelts = 0;
      values.sals = NULL;

      set_current_program_space (self->program_space);
      block = get_search_block (NULL);

      for (;
	   block && !BLOCK_FUNCTION (block);
	   block = BLOCK_SUPERBLOCK (block))
	;
      if (!block)
	return 0;
      fn_sym = BLOCK_FUNCTION (block);

      sym = lookup_symbol (copy, block, LABEL_DOMAIN, 0);

      if (sym == NULL)
	return 0;

      symbol_to_sal (&sal, self->funfirstline, sym);
      add_sal_to_sals (self, &values, &sal,
		       SYMBOL_NATURAL_NAME (fn_sym));

      if (self->canonical)
	{
	  self->canonical->special_display = 1;
	  self->canonical->addr_string
	    = xstrprintf ("%s:%s", SYMBOL_NATURAL_NAME (fn_sym),
			  copy);
	}

      *result = values;

      return 1;
    }

  result->sals = NULL;
  result->nelts = 0;

  for (ix = 0; VEC_iterate (symbolp, function_symbols, ix, fn_sym); ++ix)
    {
      struct block *block;
      struct symbol *sym;

      set_current_program_space (SYMTAB_PSPACE (SYMBOL_SYMTAB (fn_sym)));
      block = SYMBOL_BLOCK_VALUE (fn_sym);
      sym = lookup_symbol (copy, block, LABEL_DOMAIN, 0);

      if (sym != NULL)
	{
	  struct symtab_and_line sal;
	  char *symname;

	  symbol_to_sal (&sal, self->funfirstline, sym);
	  symname = xstrprintf ("%s:%s",
				SYMBOL_NATURAL_NAME (fn_sym),
				SYMBOL_NATURAL_NAME (sym));
	  add_sal_to_sals (self, result, &sal, symname);
	  xfree (symname);
	}
    }

  if (self->canonical && result->nelts > 0)
    {
      self->canonical->pre_expanded = 1;
      self->canonical->special_display = 1;

      gdb_assert (self->user_function);
      self->canonical->addr_string
	= xstrprintf ("%s:%s", self->user_function, copy);
    }

  return result->nelts > 0;
}

/* A callback used to possibly add a symbol to the results.  */

static int
collect_symbols (struct symbol *sym, void *data)
{
  struct collect_info *info = data;
  struct symtab_and_line sal;

  if (symbol_to_sal (&sal, info->state->funfirstline, sym)
      && maybe_add_address (info->state->addr_set,
			    SYMTAB_PSPACE (SYMBOL_SYMTAB (sym)),
			    sal.pc))
    add_sal_to_sals (info->state, &info->result, &sal,
		     SYMBOL_NATURAL_NAME (sym));

  return 1;
}

/* We've found a minimal symbol MSYMBOL to associate with our
   linespec; add it to the result symtabs_and_lines.  */

static void
minsym_found (struct linespec_state *self, struct objfile *objfile,
	      struct minimal_symbol *msymbol,
	      struct symtabs_and_lines *result)
{
  struct gdbarch *gdbarch = get_objfile_arch (objfile);
  CORE_ADDR pc;
  struct symtab_and_line sal;

  sal = find_pc_sect_line (SYMBOL_VALUE_ADDRESS (msymbol),
			   (struct obj_section *) 0, 0);
  sal.section = SYMBOL_OBJ_SECTION (msymbol);

  /* The minimal symbol might point to a function descriptor;
     resolve it to the actual code address instead.  */
  pc = gdbarch_convert_from_func_ptr_addr (gdbarch, sal.pc, &current_target);
  if (pc != sal.pc)
    sal = find_pc_sect_line (pc, NULL, 0);

  if (self->funfirstline)
    skip_prologue_sal (&sal);

  if (maybe_add_address (self->addr_set, objfile->pspace, sal.pc))
    add_sal_to_sals (self, result, &sal, SYMBOL_NATURAL_NAME (msymbol));
}

/* A helper struct which just holds a minimal symbol and the object
   file from which it came.  */

typedef struct minsym_and_objfile
{
  struct minimal_symbol *minsym;
  struct objfile *objfile;
} minsym_and_objfile_d;

DEF_VEC_O (minsym_and_objfile_d);

/* A helper struct to pass some data through
   iterate_over_minimal_symbols.  */

struct collect_minsyms
{
  /* The objfile we're examining.  */
  struct objfile *objfile;

  /* The funfirstline setting from the initial call.  */
  int funfirstline;

  /* The list_mode setting from the initial call.  */
  int list_mode;

  /* The resulting symbols.  */
  VEC (minsym_and_objfile_d) *msyms;
};

/* A helper function to classify a minimal_symbol_type according to
   priority.  */

static int
classify_mtype (enum minimal_symbol_type t)
{
  switch (t)
    {
    case mst_file_text:
    case mst_file_data:
    case mst_file_bss:
      /* Intermediate priority.  */
      return 1;

    case mst_solib_trampoline:
      /* Lowest priority.  */
      return 2;

    default:
      /* Highest priority.  */
      return 0;
    }
}

/* Callback for qsort that sorts symbols by priority.  */

static int
compare_msyms (const void *a, const void *b)
{
  const minsym_and_objfile_d *moa = a;
  const minsym_and_objfile_d *mob = b;
  enum minimal_symbol_type ta = MSYMBOL_TYPE (moa->minsym);
  enum minimal_symbol_type tb = MSYMBOL_TYPE (mob->minsym);

  return classify_mtype (ta) - classify_mtype (tb);
}

/* Callback for iterate_over_minimal_symbols that adds the symbol to
   the result.  */

static void
add_minsym (struct minimal_symbol *minsym, void *d)
{
  struct collect_minsyms *info = d;
  minsym_and_objfile_d mo;

  /* Exclude data symbols when looking for breakpoint locations.   */
  if (!info->list_mode)
    switch (minsym->type)
      {
	case mst_slot_got_plt:
	case mst_data:
	case mst_bss:
	case mst_abs:
	case mst_file_data:
	case mst_file_bss:
	  {
	    /* Make sure this minsym is not a function descriptor
	       before we decide to discard it.  */
	    struct gdbarch *gdbarch = info->objfile->gdbarch;
	    CORE_ADDR addr = gdbarch_convert_from_func_ptr_addr
			       (gdbarch, SYMBOL_VALUE_ADDRESS (minsym),
				&current_target);

	    if (addr == SYMBOL_VALUE_ADDRESS (minsym))
	      return;
	  }
      }

  mo.minsym = minsym;
  mo.objfile = info->objfile;
  VEC_safe_push (minsym_and_objfile_d, info->msyms, &mo);
}

/* Search minimal symbols in all objfiles for NAME.  If SEARCH_PSPACE
   is not NULL, the search is restricted to just that program
   space.  */

static void
search_minsyms_for_name (struct collect_info *info, const char *name,
			 struct program_space *search_pspace)
{
  struct objfile *objfile;
  struct program_space *pspace;

  ALL_PSPACES (pspace)
  {
    struct collect_minsyms local;
    struct cleanup *cleanup;

    if (search_pspace != NULL && search_pspace != pspace)
      continue;
    if (pspace->executing_startup)
      continue;

    set_current_program_space (pspace);

    memset (&local, 0, sizeof (local));
    local.funfirstline = info->state->funfirstline;
    local.list_mode = info->state->list_mode;

    cleanup = make_cleanup (VEC_cleanup (minsym_and_objfile_d),
			    &local.msyms);

    ALL_OBJFILES (objfile)
    {
      local.objfile = objfile;
      iterate_over_minimal_symbols (objfile, name, add_minsym, &local);
    }

    if (!VEC_empty (minsym_and_objfile_d, local.msyms))
      {
	int classification;
	int ix;
	minsym_and_objfile_d *item;

	qsort (VEC_address (minsym_and_objfile_d, local.msyms),
	       VEC_length (minsym_and_objfile_d, local.msyms),
	       sizeof (minsym_and_objfile_d),
	       compare_msyms);

	/* Now the minsyms are in classification order.  So, we walk
	   over them and process just the minsyms with the same
	   classification as the very first minsym in the list.  */
	item = VEC_index (minsym_and_objfile_d, local.msyms, 0);
	classification = classify_mtype (MSYMBOL_TYPE (item->minsym));

	for (ix = 0;
	     VEC_iterate (minsym_and_objfile_d, local.msyms, ix, item);
	     ++ix)
	  {
	    if (classify_mtype (MSYMBOL_TYPE (item->minsym)) != classification)
	      break;

	    minsym_found (info->state, item->objfile, item->minsym,
			  &info->result);
	  }
      }

    do_cleanups (cleanup);
  }
}

/* A helper function to add all symbols matching NAME to INFO.  If
   PSPACE is not NULL, the search is restricted to just that program
   space.  */

static void
add_matching_symbols_to_info (const char *name,
			      struct collect_info *info,
			      struct program_space *pspace)
{
  int ix;
  struct symtab *elt;

  for (ix = 0; VEC_iterate (symtab_p, info->state->file_symtabs, ix, elt); ++ix)
    {
      struct symbol *sym;

      if (elt == NULL)
	{
	  iterate_over_all_matching_symtabs (name, VAR_DOMAIN,
					     collect_symbols, info,
					     pspace);
	  search_minsyms_for_name (info, name, pspace);
	}
      else if (pspace == NULL || pspace == SYMTAB_PSPACE (elt))
	{
	  /* Program spaces that are executing startup should have
	     been filtered out earlier.  */
	  gdb_assert (!SYMTAB_PSPACE (elt)->executing_startup);
	  set_current_program_space (SYMTAB_PSPACE (elt));
	  LA_ITERATE_OVER_SYMBOLS (get_search_block (elt), name,
				   VAR_DOMAIN, collect_symbols,
				   info);
	}
    }
}

/* Decode a linespec that's a variable.  If FILE_SYMTAB is non-NULL,
   look in that symtab's static variables first.  */ 

static struct symtabs_and_lines
decode_variable (struct linespec_state *self, char *copy)
{
  struct collect_info info;
  const char *lookup_name;
  char *canon;
  struct cleanup *cleanup;

  info.state = self;
  info.result.sals = NULL;
  info.result.nelts = 0;

  cleanup = demangle_for_lookup (copy, current_language->la_language,
				 &lookup_name);
  if (current_language->la_language == language_ada)
    {
      /* In Ada, the symbol lookups are performed using the encoded
         name rather than the demangled name.  */
      lookup_name = ada_name_for_lookup (copy);
      make_cleanup (xfree, (void *) lookup_name);
    }

  canon = cp_canonicalize_string_no_typedefs (lookup_name);
  if (canon != NULL)
    {
      make_cleanup (xfree, canon);
      lookup_name = canon;
    }

  add_matching_symbols_to_info (lookup_name, &info, NULL);

  if (info.result.nelts > 0)
    {
      if (self->canonical)
	{
	  self->canonical->pre_expanded = 1;
	  if (self->user_filename)
	    self->canonical->addr_string
	      = xstrprintf ("%s:%s", self->user_filename, copy);
	  else
	    self->canonical->addr_string = xstrdup (copy);
	}
      return info.result;
    }

  if (!have_full_symbols ()
      && !have_partial_symbols ()
      && !have_minimal_symbols ())
    throw_error (NOT_FOUND_ERROR,
		 _("No symbol table is loaded.  Use the \"file\" command."));
  if (self->user_filename)
    throw_error (NOT_FOUND_ERROR, _("Function \"%s\" not defined in \"%s\"."),
		 copy, self->user_filename);
  else
    throw_error (NOT_FOUND_ERROR, _("Function \"%s\" not defined."), copy);
}




/* Now come some functions that are called from multiple places within
   decode_line_1.  */

static int
symbol_to_sal (struct symtab_and_line *result,
	       int funfirstline, struct symbol *sym)
{
  if (SYMBOL_CLASS (sym) == LOC_BLOCK)
    {
      *result = find_function_start_sal (sym, funfirstline);
      return 1;
    }
  else
    {
      if (SYMBOL_CLASS (sym) == LOC_LABEL && SYMBOL_VALUE_ADDRESS (sym) != 0)
	{
	  init_sal (result);
	  result->symtab = SYMBOL_SYMTAB (sym);
	  result->line = SYMBOL_LINE (sym);
	  result->pc = SYMBOL_VALUE_ADDRESS (sym);
	  result->pspace = SYMTAB_PSPACE (SYMBOL_SYMTAB (sym));
	  result->explicit_pc = 1;
	  return 1;
	}
      else if (funfirstline)
	{
	  /* Nothing.  */
	}
      else if (SYMBOL_LINE (sym) != 0)
	{
	  /* We know its line number.  */
	  init_sal (result);
	  result->symtab = SYMBOL_SYMTAB (sym);
	  result->line = SYMBOL_LINE (sym);
	  result->pspace = SYMTAB_PSPACE (SYMBOL_SYMTAB (sym));
	  return 1;
	}
    }

  return 0;
}

/* See the comment in linespec.h.  */

void
init_linespec_result (struct linespec_result *lr)
{
  memset (lr, 0, sizeof (*lr));
}

/* See the comment in linespec.h.  */

void
destroy_linespec_result (struct linespec_result *ls)
{
  int i;
  struct linespec_sals *lsal;

  xfree (ls->addr_string);
  for (i = 0; VEC_iterate (linespec_sals, ls->sals, i, lsal); ++i)
    {
      xfree (lsal->canonical);
      xfree (lsal->sals.sals);
    }
  VEC_free (linespec_sals, ls->sals);
}

/* Cleanup function for a linespec_result.  */

static void
cleanup_linespec_result (void *a)
{
  destroy_linespec_result (a);
}

/* See the comment in linespec.h.  */

struct cleanup *
make_cleanup_destroy_linespec_result (struct linespec_result *ls)
{
  return make_cleanup (cleanup_linespec_result, ls);
}

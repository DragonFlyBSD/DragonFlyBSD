/* GDB routines for supporting auto-loaded scripts.

   Copyright (C) 2010-2012 Free Software Foundation, Inc.

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
#include "filenames.h"
#include "gdb_string.h"
#include "gdb_regex.h"
#include "top.h"
#include "exceptions.h"
#include "command.h"
#include "gdbcmd.h"
#include "observer.h"
#include "progspace.h"
#include "objfiles.h"
#include "python.h"
#include "cli/cli-cmds.h"

/* Internal-use flag to enable/disable auto-loading.
   This is true if we should auto-load python code when an objfile is opened,
   false otherwise.

   Both auto_load_scripts && gdbpy_global_auto_load must be true to enable
   auto-loading.

   This flag exists to facilitate deferring auto-loading during start-up
   until after ./.gdbinit has been read; it may augment the search directories
   used to find the scripts.  */
int gdbpy_global_auto_load = 1;

#ifdef HAVE_PYTHON

#include "python-internal.h"

/* NOTE: It's trivial to also support auto-loading normal gdb scripts.
   There has yet to be a need so it's not implemented.  */

/* The suffix of per-objfile scripts to auto-load.
   E.g. When the program loads libfoo.so, look for libfoo-gdb.py.  */
#define GDBPY_AUTO_FILE_NAME "-gdb.py"

/* The section to look for scripts (in file formats that support sections).
   Each entry in this section is a byte of value 1, and then the nul-terminated
   name of the script.  The script name may include a directory.
   The leading byte is to allow upward compatible extensions.  */
#define GDBPY_AUTO_SECTION_NAME ".debug_gdb_scripts"

/* For scripts specified in .debug_gdb_scripts, multiple objfiles may load
   the same script.  There's no point in loading the script multiple times,
   and there can be a lot of objfiles and scripts, so we keep track of scripts
   loaded this way.  */

struct auto_load_pspace_info
{
  /* For each program space we keep track of loaded scripts.  */
  struct htab *loaded_scripts;

  /* Non-zero if we've issued the warning about an auto-load script not being
     found.  We only want to issue this warning once.  */
  int script_not_found_warning_printed;
};

/* Objects of this type are stored in the loaded script hash table.  */

struct loaded_script
{
  /* Name as provided by the objfile.  */
  const char *name;
  /* Full path name or NULL if script wasn't found (or was otherwise
     inaccessible).  */
  const char *full_path;
};

/* User-settable option to enable/disable auto-loading:
   set auto-load-scripts on|off
   This is true if we should auto-load associated scripts when an objfile
   is opened, false otherwise.
   At the moment, this only affects python scripts, but there's no reason
   one couldn't also have other kinds of auto-loaded scripts, and there's
   no reason to have them each controlled by a separate flag.
   So we elide "python" from the name here and in the option.
   The fact that it lives here is just an implementation detail.  */
static int auto_load_scripts = 1;

/* Per-program-space data key.  */
static const struct program_space_data *auto_load_pspace_data;

static void
auto_load_pspace_data_cleanup (struct program_space *pspace, void *arg)
{
  struct auto_load_pspace_info *info;

  info = program_space_data (pspace, auto_load_pspace_data);
  if (info != NULL)
    {
      if (info->loaded_scripts)
	htab_delete (info->loaded_scripts);
      xfree (info);
    }
}

/* Get the current autoload data.  If none is found yet, add it now.  This
   function always returns a valid object.  */

static struct auto_load_pspace_info *
get_auto_load_pspace_data (struct program_space *pspace)
{
  struct auto_load_pspace_info *info;

  info = program_space_data (pspace, auto_load_pspace_data);
  if (info == NULL)
    {
      info = XZALLOC (struct auto_load_pspace_info);
      set_program_space_data (pspace, auto_load_pspace_data, info);
    }

  return info;
}

/* Hash function for the loaded script hash.  */

static hashval_t
hash_loaded_script_entry (const void *data)
{
  const struct loaded_script *e = data;

  return htab_hash_string (e->name);
}

/* Equality function for the loaded script hash.  */

static int
eq_loaded_script_entry (const void *a, const void *b)
{
  const struct loaded_script *ea = a;
  const struct loaded_script *eb = b;

  return strcmp (ea->name, eb->name) == 0;
}

/* Initialize the table to track loaded scripts.
   Each entry is hashed by the full path name.  */

static void
init_loaded_scripts_info (struct auto_load_pspace_info *pspace_info)
{
  /* Choose 31 as the starting size of the hash table, somewhat arbitrarily.
     Space for each entry is obtained with one malloc so we can free them
     easily.  */

  pspace_info->loaded_scripts = htab_create (31,
					     hash_loaded_script_entry,
					     eq_loaded_script_entry,
					     xfree);

  pspace_info->script_not_found_warning_printed = FALSE;
}

/* Wrapper on get_auto_load_pspace_data to also allocate the hash table
   for loading scripts.  */

static struct auto_load_pspace_info *
get_auto_load_pspace_data_for_loading (struct program_space *pspace)
{
  struct auto_load_pspace_info *info;

  info = get_auto_load_pspace_data (pspace);
  if (info->loaded_scripts == NULL)
    init_loaded_scripts_info (info);

  return info;
}

/* Add script NAME to hash table HTAB.
   FULL_PATH is NULL if the script wasn't found.
   The result is true if the script was already in the hash table.  */

static int
maybe_add_script (struct htab *htab, const char *name, const char *full_path)
{
  struct loaded_script **slot, entry;
  int in_hash_table;

  entry.name = name;
  entry.full_path = full_path;
  slot = (struct loaded_script **) htab_find_slot (htab, &entry, INSERT);
  in_hash_table = *slot != NULL;

  /* If this script is not in the hash table, add it.  */

  if (! in_hash_table)
    {
      char *p;

      /* Allocate all space in one chunk so it's easier to free.  */
      *slot = xmalloc (sizeof (**slot)
		       + strlen (name) + 1
		       + (full_path != NULL ? (strlen (full_path) + 1) : 0));
      p = ((char*) *slot) + sizeof (**slot);
      strcpy (p, name);
      (*slot)->name = p;
      if (full_path != NULL)
	{
	  p += strlen (p) + 1;
	  strcpy (p, full_path);
	  (*slot)->full_path = p;
	}
      else
	(*slot)->full_path = NULL;
    }

  return in_hash_table;
}

/* Load scripts specified in OBJFILE.
   START,END delimit a buffer containing a list of nul-terminated
   file names.
   SOURCE_NAME is used in error messages.

   Scripts are found per normal "source -s" command processing.
   First the script is looked for in $cwd.  If not found there the
   source search path is used.

   The section contains a list of path names of files containing
   python code to load.  Each path is null-terminated.  */

static void
source_section_scripts (struct objfile *objfile, const char *source_name,
			const char *start, const char *end)
{
  const char *p;
  struct auto_load_pspace_info *pspace_info;

  pspace_info = get_auto_load_pspace_data_for_loading (current_program_space);

  for (p = start; p < end; ++p)
    {
      const char *file;
      FILE *stream;
      char *full_path;
      int opened, in_hash_table;

      if (*p != 1)
	{
	  warning (_("Invalid entry in %s section"), GDBPY_AUTO_SECTION_NAME);
	  /* We could try various heuristics to find the next valid entry,
	     but it's safer to just punt.  */
	  break;
	}
      file = ++p;

      while (p < end && *p != '\0')
	++p;
      if (p == end)
	{
	  char *buf = alloca (p - file + 1);

	  memcpy (buf, file, p - file);
	  buf[p - file] = '\0';
	  warning (_("Non-null-terminated path in %s: %s"),
		   source_name, buf);
	  /* Don't load it.  */
	  break;
	}
      if (p == file)
	{
	  warning (_("Empty path in %s"), source_name);
	  continue;
	}

      opened = find_and_open_script (file, 1 /*search_path*/,
				     &stream, &full_path);

      /* If one script isn't found it's not uncommon for more to not be
	 found either.  We don't want to print an error message for each
	 script, too much noise.  Instead, we print the warning once and tell
	 the user how to find the list of scripts that weren't loaded.

	 IWBN if complaints.c were more general-purpose.  */

      in_hash_table = maybe_add_script (pspace_info->loaded_scripts, file,
					opened ? full_path : NULL);

      if (! opened)
	{
	  /* We don't throw an error, the program is still debuggable.  */
	  if (! pspace_info->script_not_found_warning_printed)
	    {
	      warning (_("Missing auto-load scripts referenced in section %s\n\
of file %s\n\
Use `info auto-load-scripts [REGEXP]' to list them."),
		       GDBPY_AUTO_SECTION_NAME, objfile->name);
	      pspace_info->script_not_found_warning_printed = TRUE;
	    }
	}
      else
	{
	  /* If this file is not currently loaded, load it.  */
	  if (! in_hash_table)
	    source_python_script_for_objfile (objfile, full_path);
	  fclose (stream);
	  xfree (full_path);
	}
    }
}

/* Load scripts specified in section SECTION_NAME of OBJFILE.  */

static void
auto_load_section_scripts (struct objfile *objfile, const char *section_name)
{
  bfd *abfd = objfile->obfd;
  asection *scripts_sect;
  bfd_size_type size;
  char *p;
  struct cleanup *cleanups;

  scripts_sect = bfd_get_section_by_name (abfd, section_name);
  if (scripts_sect == NULL)
    return;

  size = bfd_get_section_size (scripts_sect);
  p = xmalloc (size);
  
  cleanups = make_cleanup (xfree, p);

  if (bfd_get_section_contents (abfd, scripts_sect, p, (file_ptr) 0, size))
    source_section_scripts (objfile, section_name, p, p + size);
  else
    warning (_("Couldn't read %s section of %s"),
	     section_name, bfd_get_filename (abfd));

  do_cleanups (cleanups);
}

/* Clear the table of loaded section scripts.  */

static void
clear_section_scripts (void)
{
  struct program_space *pspace = current_program_space;
  struct auto_load_pspace_info *info;

  info = program_space_data (pspace, auto_load_pspace_data);
  if (info != NULL && info->loaded_scripts != NULL)
    {
      htab_delete (info->loaded_scripts);
      info->loaded_scripts = NULL;
      info->script_not_found_warning_printed = FALSE;
    }
}

/* Look for the auto-load script associated with OBJFILE and load it.  */

static void
auto_load_objfile_script (struct objfile *objfile, const char *suffix)
{
  char *realname;
  char *filename, *debugfile;
  int len;
  FILE *input;
  struct cleanup *cleanups;

  realname = gdb_realpath (objfile->name);
  len = strlen (realname);
  filename = xmalloc (len + strlen (suffix) + 1);
  memcpy (filename, realname, len);
  strcpy (filename + len, suffix);

  cleanups = make_cleanup (xfree, filename);
  make_cleanup (xfree, realname);

  input = fopen (filename, "r");
  debugfile = filename;

  if (!input && debug_file_directory)
    {
      /* Also try the same file in the separate debug info directory.  */
      debugfile = xmalloc (strlen (filename)
			   + strlen (debug_file_directory) + 1);
      strcpy (debugfile, debug_file_directory);
      /* FILENAME is absolute, so we don't need a "/" here.  */
      strcat (debugfile, filename);

      make_cleanup (xfree, debugfile);
      input = fopen (debugfile, "r");
    }

  if (!input && gdb_datadir)
    {
      /* Also try the same file in a subdirectory of gdb's data
	 directory.  */
      debugfile = xmalloc (strlen (gdb_datadir) + strlen (filename)
			   + strlen ("/auto-load") + 1);
      strcpy (debugfile, gdb_datadir);
      strcat (debugfile, "/auto-load");
      /* FILENAME is absolute, so we don't need a "/" here.  */
      strcat (debugfile, filename);

      make_cleanup (xfree, debugfile);
      input = fopen (debugfile, "r");
    }

  if (input)
    {
      struct auto_load_pspace_info *pspace_info;

      /* Add this script to the hash table too so "info auto-load-scripts"
	 can print it.  */
      pspace_info =
	get_auto_load_pspace_data_for_loading (current_program_space);
      maybe_add_script (pspace_info->loaded_scripts, debugfile, debugfile);

      /* To preserve existing behaviour we don't check for whether the
	 script was already in the table, and always load it.
	 It's highly unlikely that we'd ever load it twice,
	 and these scripts are required to be idempotent under multiple
	 loads anyway.  */
      source_python_script_for_objfile (objfile, debugfile);
      fclose (input);
    }

  do_cleanups (cleanups);
}

/* This is a new_objfile observer callback to auto-load scripts.

   Two flavors of auto-loaded scripts are supported.
   1) based on the path to the objfile
   2) from .debug_gdb_scripts section  */

static void
auto_load_new_objfile (struct objfile *objfile)
{
  if (!objfile)
    {
      /* OBJFILE is NULL when loading a new "main" symbol-file.  */
      clear_section_scripts ();
      return;
    }

  load_auto_scripts_for_objfile (objfile);
}

/* Load any auto-loaded scripts for OBJFILE.  */

void
load_auto_scripts_for_objfile (struct objfile *objfile)
{
  if (auto_load_scripts && gdbpy_global_auto_load)
    {
      auto_load_objfile_script (objfile, GDBPY_AUTO_FILE_NAME);
      auto_load_section_scripts (objfile, GDBPY_AUTO_SECTION_NAME);
    }
}

/* Collect scripts to be printed in a vec.  */

typedef struct loaded_script *loaded_script_ptr;
DEF_VEC_P (loaded_script_ptr);

/* Traversal function for htab_traverse.
   Collect the entry if it matches the regexp.  */

static int
collect_matching_scripts (void **slot, void *info)
{
  struct loaded_script *script = *slot;
  VEC (loaded_script_ptr) **scripts_ptr = info;

  if (re_exec (script->name))
    VEC_safe_push (loaded_script_ptr, *scripts_ptr, script);

  return 1;
}

/* Print SCRIPT.  */

static void
print_script (struct loaded_script *script)
{
  struct ui_out *uiout = current_uiout;
  struct cleanup *chain;

  chain = make_cleanup_ui_out_tuple_begin_end (uiout, NULL);

  ui_out_field_string (uiout, "loaded", script->full_path ? "Yes" : "Missing");
  ui_out_field_string (uiout, "script", script->name);
  ui_out_text (uiout, "\n");

  /* If the name isn't the full path, print it too.  */
  if (script->full_path != NULL
      && strcmp (script->name, script->full_path) != 0)
    {
      ui_out_text (uiout, "\tfull name: ");
      ui_out_field_string (uiout, "full_path", script->full_path);
      ui_out_text (uiout, "\n");
    }

  do_cleanups (chain);
}

/* Helper for info_auto_load_scripts to sort the scripts by name.  */

static int
sort_scripts_by_name (const void *ap, const void *bp)
{
  const struct loaded_script *a = *(const struct loaded_script **) ap;
  const struct loaded_script *b = *(const struct loaded_script **) bp;

  return FILENAME_CMP (a->name, b->name);
}

/* "info auto-load-scripts" command.  */

static void
info_auto_load_scripts (char *pattern, int from_tty)
{
  struct ui_out *uiout = current_uiout;
  struct auto_load_pspace_info *pspace_info;
  struct cleanup *script_chain;
  VEC (loaded_script_ptr) *scripts;
  int nr_scripts;

  dont_repeat ();

  pspace_info = get_auto_load_pspace_data (current_program_space);

  if (pattern && *pattern)
    {
      char *re_err = re_comp (pattern);

      if (re_err)
	error (_("Invalid regexp: %s"), re_err);
    }
  else
    {
      re_comp ("");
    }

  /* We need to know the number of rows before we build the table.
     Plus we want to sort the scripts by name.
     So first traverse the hash table collecting the matching scripts.  */

  scripts = VEC_alloc (loaded_script_ptr, 10);
  script_chain = make_cleanup (VEC_cleanup (loaded_script_ptr), &scripts);

  if (pspace_info != NULL && pspace_info->loaded_scripts != NULL)
    {
      immediate_quit++;
      /* Pass a pointer to scripts as VEC_safe_push can realloc space.  */
      htab_traverse_noresize (pspace_info->loaded_scripts,
			      collect_matching_scripts, &scripts);
      immediate_quit--;
    }

  nr_scripts = VEC_length (loaded_script_ptr, scripts);
  make_cleanup_ui_out_table_begin_end (uiout, 2, nr_scripts,
				       "AutoLoadedScriptsTable");

  ui_out_table_header (uiout, 7, ui_left, "loaded", "Loaded");
  ui_out_table_header (uiout, 70, ui_left, "script", "Script");
  ui_out_table_body (uiout);

  if (nr_scripts > 0)
    {
      int i;
      loaded_script_ptr script;

      qsort (VEC_address (loaded_script_ptr, scripts),
	     VEC_length (loaded_script_ptr, scripts),
	     sizeof (loaded_script_ptr), sort_scripts_by_name);
      for (i = 0; VEC_iterate (loaded_script_ptr, scripts, i, script); ++i)
	print_script (script);
    }

  do_cleanups (script_chain);

  if (nr_scripts == 0)
    {
      if (pattern && *pattern)
	ui_out_message (uiout, 0, "No auto-load scripts matching %s.\n",
			pattern);
      else
	ui_out_message (uiout, 0, "No auto-load scripts.\n");
    }
}

void
gdbpy_initialize_auto_load (void)
{
  auto_load_pspace_data
    = register_program_space_data_with_cleanup (auto_load_pspace_data_cleanup);

  observer_attach_new_objfile (auto_load_new_objfile);

  add_setshow_boolean_cmd ("auto-load-scripts", class_support,
			   &auto_load_scripts, _("\
Set the debugger's behaviour regarding auto-loaded scripts."), _("\
Show the debugger's behaviour regarding auto-loaded scripts."), _("\
If enabled, auto-loaded scripts are loaded when the debugger reads\n\
an executable or shared library."),
			   NULL, NULL,
			   &setlist,
			   &showlist);

  add_info ("auto-load-scripts",
	    info_auto_load_scripts,
	    _("Print the list of automatically loaded scripts.\n\
Usage: info auto-load-scripts [REGEXP]"));
}

#else /* ! HAVE_PYTHON */

void
load_auto_scripts_for_objfile (struct objfile *objfile)
{
}

#endif /* ! HAVE_PYTHON */

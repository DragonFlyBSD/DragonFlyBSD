/*
 * gripes.c
 *
 * Copyright (c) 1990, 1991, John W. Eaton.
 *
 * You may distribute under the terms of the GNU General Public
 * License as specified in the file COPYING that comes with the man
 * distribution.
 *
 * John W. Eaton
 * jwe@che.utexas.edu
 * Department of Chemical Engineering
 * The University of Texas at Austin
 * Austin, Texas  78712
 */

#include <stdio.h>
#include <stdlib.h>
#include "gripes.h"

extern char *prognam;

void
gripe_no_name (char *section)
{
  if (section)
    fprintf (stderr, "What manual page do you want from section %s?\n",
	     section);
  else
    fprintf (stderr, "What manual page do you want?\n");

  fflush (stderr);
}

void
gripe_reading_man_file (char *name)
{
  fprintf (stderr, "Read access denied for file %s\n", name);

  fflush (stderr);
}

void
gripe_converting_name (char *name, int to_cat)
{
  if (to_cat)
    fprintf (stderr, "Error converting %s to cat name\n", name);
  else
    fprintf (stderr, "Error converting %s to man name\n", name);

  fflush (stderr);

  exit (1);
}

void
gripe_system_command (int status)
{
  fprintf (stderr, "Error executing formatting or display command.\n");
  fprintf (stderr, "system command exited with status %d\n", status);

  fflush (stderr);
}

void
gripe_not_found (char *name, char *section)
{
  if (section)
    fprintf (stderr, "No entry for %s in section %s of the manual\n",
	     name, section);
  else
    fprintf (stderr, "No manual entry for %s\n", name);

  fflush (stderr);
}

void
gripe_incompatible (const char *s)
{
  fprintf (stderr, "%s: incompatible options %s\n", prognam, s);

  fflush (stderr);

  exit (1);
}

void
gripe_getting_mp_config (char *file)
{
  fprintf (stderr, "%s: unable to find the file %s\n", prognam, file);

  fflush (stderr);

  exit (1);
}

void
gripe_reading_mp_config (char *file)
{
  fprintf (stderr, "%s: unable to make sense of the file %s\n", prognam, file);

  fflush (stderr);

  exit (1);
}

void
gripe_invalid_section (char *section)
{
  fprintf (stderr, "%s: invalid section (%s) selected\n", prognam, section);

  fflush (stderr);

  exit (1);
}

void
gripe_manpath (void)
{
  fprintf (stderr, "%s: manpath is null\n", prognam);

  fflush (stderr);

  exit (1);
}

void
gripe_alloc (int bytes, const char *object)
{
  fprintf (stderr, "%s: can't malloc %d bytes for %s\n",
	   prognam, bytes, object);

  fflush (stderr);

  exit (1);
}

void
gripe_roff_command_from_file (char *file)
{
  fprintf (stderr, "Error parsing *roff command from file %s\n", file);

  fflush (stderr);
}

void
gripe_roff_command_from_env (void)
{
  fprintf (stderr, "Error parsing MANROFFSEQ.  Using system defaults.\n");

  fflush (stderr);
}

void
gripe_roff_command_from_command_line (void)
{
  fprintf (stderr, "Error parsing *roff command from command line.\n");

  fflush (stderr);
}

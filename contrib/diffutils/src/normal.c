/* Normal-format output routines for GNU DIFF.

   Copyright (C) 1988-1989, 1993, 1995, 1998, 2001, 2006, 2009-2010 Free
   Software Foundation, Inc.

   This file is part of GNU DIFF.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "diff.h"

static void print_normal_hunk (struct change *);

/* Print the edit-script SCRIPT as a normal diff.
   INF points to an array of descriptions of the two files.  */

void
print_normal_script (struct change *script)
{
  print_script (script, find_change, print_normal_hunk);
}

/* Print a hunk of a normal diff.
   This is a contiguous portion of a complete edit script,
   describing changes in consecutive lines.  */

static void
print_normal_hunk (struct change *hunk)
{
  lin first0, last0, first1, last1;
  register lin i;

  /* Determine range of line numbers involved in each file.  */
  enum changes changes = analyze_hunk (hunk, &first0, &last0, &first1, &last1);
  if (!changes)
    return;

  begin_output ();

  /* Print out the line number header for this hunk */
  print_number_range (',', &files[0], first0, last0);
  fputc (change_letter[changes], outfile);
  print_number_range (',', &files[1], first1, last1);
  fputc ('\n', outfile);

  /* Print the lines that the first file has.  */
  if (changes & OLD)
    for (i = first0; i <= last0; i++)
      print_1_line ("<", &files[0].linbuf[i]);

  if (changes == CHANGED)
    fputs ("---\n", outfile);

  /* Print the lines that the second file has.  */
  if (changes & NEW)
    for (i = first1; i <= last1; i++)
      print_1_line (">", &files[1].linbuf[i]);
}

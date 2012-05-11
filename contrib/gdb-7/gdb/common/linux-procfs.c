/* Linux-specific PROCFS manipulation routines.
   Copyright (C) 2009-2012 Free Software Foundation, Inc.

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

#ifdef GDBSERVER
#include "server.h"
#else
#include "defs.h"
#include "gdb_string.h"
#endif

#include "linux-procfs.h"

/* Return the TGID of LWPID from /proc/pid/status.  Returns -1 if not
   found.  */

int
linux_proc_get_tgid (int lwpid)
{
  FILE *status_file;
  char buf[100];
  int tgid = -1;

  snprintf (buf, sizeof (buf), "/proc/%d/status", (int) lwpid);
  status_file = fopen (buf, "r");
  if (status_file != NULL)
    {
      while (fgets (buf, sizeof (buf), status_file))
	{
	  if (strncmp (buf, "Tgid:", 5) == 0)
	    {
	      tgid = strtoul (buf + strlen ("Tgid:"), NULL, 10);
	      break;
	    }
	}

      fclose (status_file);
    }

  return tgid;
}

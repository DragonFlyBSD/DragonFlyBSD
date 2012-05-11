/* Linux-specific functions to retrieve OS data.
   
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
#endif

#include "linux-osdata.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <utmp.h>
#include <time.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "xml-utils.h"
#include "buffer.h"
#include "gdb_assert.h"
#include "gdb_dirent.h"

int
linux_common_core_of_thread (ptid_t ptid)
{
  char filename[sizeof ("/proc//task//stat")
		 + 2 * 20 /* decimal digits for 2 numbers, max 2^64 bit each */
		 + 1];
  FILE *f;
  char *content = NULL;
  char *p;
  char *ts = 0;
  int content_read = 0;
  int i;
  int core;

  sprintf (filename, "/proc/%d/task/%ld/stat",
	   ptid_get_pid (ptid), ptid_get_lwp (ptid));
  f = fopen (filename, "r");
  if (!f)
    return -1;

  for (;;)
    {
      int n;
      content = xrealloc (content, content_read + 1024);
      n = fread (content + content_read, 1, 1024, f);
      content_read += n;
      if (n < 1024)
	{
	  content[content_read] = '\0';
	  break;
	}
    }

  p = strchr (content, '(');

  /* Skip ")".  */
  if (p != NULL)
    p = strchr (p, ')');
  if (p != NULL)
    p++;

  /* If the first field after program name has index 0, then core number is
     the field with index 36.  There's no constant for that anywhere.  */
  if (p != NULL)
    p = strtok_r (p, " ", &ts);
  for (i = 0; p != NULL && i != 36; ++i)
    p = strtok_r (NULL, " ", &ts);

  if (p == NULL || sscanf (p, "%d", &core) == 0)
    core = -1;

  xfree (content);
  fclose (f);

  return core;
}

static void
command_from_pid (char *command, int maxlen, pid_t pid)
{
  char *stat_path = xstrprintf ("/proc/%d/stat", pid); 
  FILE *fp = fopen (stat_path, "r");
  
  command[0] = '\0';
 
  if (fp)
    {
      /* sizeof (cmd) should be greater or equal to TASK_COMM_LEN (in
	 include/linux/sched.h in the Linux kernel sources) plus two
	 (for the brackets).  */
      char cmd[32]; 
      pid_t stat_pid;
      int items_read = fscanf (fp, "%d %32s", &stat_pid, cmd);
	  
      if (items_read == 2 && pid == stat_pid)
	{
	  cmd[strlen (cmd) - 1] = '\0'; /* Remove trailing parenthesis.  */
	  strncpy (command, cmd + 1, maxlen); /* Ignore leading parenthesis.  */
	}

      fclose (fp);
    }
  else
    {
      /* Return the PID if a /proc entry for the process cannot be found.  */
      snprintf (command, maxlen, "%d", pid);
    }

  command[maxlen - 1] = '\0'; /* Ensure string is null-terminated.  */
	
  xfree (stat_path);
}

/* Returns the command-line of the process with the given PID. The returned
   string needs to be freed using xfree after use.  */

static char *
commandline_from_pid (pid_t pid)
{
  char *pathname = xstrprintf ("/proc/%d/cmdline", pid);
  char *commandline = NULL;
  FILE *f = fopen (pathname, "r");

  if (f)
    {
      size_t len = 0;

      while (!feof (f))
	{
	  char buf[1024];
	  size_t read_bytes = fread (buf, 1, sizeof (buf), f);
     
	  if (read_bytes)
	    {
	      commandline = (char *) xrealloc (commandline, len + read_bytes + 1);
	      memcpy (commandline + len, buf, read_bytes);
	      len += read_bytes;
	    }
	}

      fclose (f);

      if (commandline)
	{
	  size_t i;

	  /* Replace null characters with spaces.  */
	  for (i = 0; i < len; ++i)
	    if (commandline[i] == '\0')
	      commandline[i] = ' ';

	  commandline[len] = '\0';
	}
      else
	{
	  /* Return the command in square brackets if the command-line is empty.  */
	  commandline = (char *) xmalloc (32);
	  commandline[0] = '[';
	  command_from_pid (commandline + 1, 31, pid);

	  len = strlen (commandline);
	  if (len < 31)
	    strcat (commandline, "]");
	}
    }

  xfree (pathname);

  return commandline;
}

static void
user_from_uid (char *user, int maxlen, uid_t uid)
{
  struct passwd *pwentry = getpwuid (uid);
  
  if (pwentry)
    {
      strncpy (user, pwentry->pw_name, maxlen);
      user[maxlen - 1] = '\0'; /* Ensure that the user name is null-terminated.  */
    }
  else
    user[0] = '\0';
}

static int
get_process_owner (uid_t *owner, pid_t pid)
{
  struct stat statbuf;
  char procentry[sizeof ("/proc/4294967295")];

  sprintf (procentry, "/proc/%d", pid);
  
  if (stat (procentry, &statbuf) == 0 && S_ISDIR (statbuf.st_mode))
    {
      *owner = statbuf.st_uid;
      return 0;
    }
  else
    return -1;
}

static int
get_number_of_cpu_cores (void)
{
  int cores = 0;
  FILE *f = fopen ("/proc/cpuinfo", "r");

  while (!feof (f))
    {
      char buf[512];
      char *p = fgets (buf, sizeof (buf), f);

      if (p && strncmp (buf, "processor", 9) == 0)
	++cores;
    }

  fclose (f);

  return cores;
}

/* CORES points to an array of at least get_number_of_cpu_cores () elements.  */

static int
get_cores_used_by_process (pid_t pid, int *cores)
{
  char taskdir[sizeof ("/proc/4294967295/task")];
  DIR *dir;
  struct dirent *dp;
  int task_count = 0;

  sprintf (taskdir, "/proc/%d/task", pid);
  dir = opendir (taskdir);
  if (dir)
    {
      while ((dp = readdir (dir)) != NULL)
	{
	  pid_t tid;
	  int core;

	  if (!isdigit (dp->d_name[0])
	      || NAMELEN (dp) > sizeof ("4294967295") - 1)
	    continue;

	  tid = atoi (dp->d_name);
	  core = linux_common_core_of_thread (ptid_build (pid, tid, 0));

	  if (core >= 0)
	    {
	      ++cores[core];
	      ++task_count;
	    }
	}

      closedir (dir);
    }

  return task_count;
}

static LONGEST
linux_xfer_osdata_processes (gdb_byte *readbuf,
			     ULONGEST offset, LONGEST len)
{
  /* We make the process list snapshot when the object starts to be read.  */
  static const char *buf;
  static LONGEST len_avail = -1;
  static struct buffer buffer;

  if (offset == 0)
    {
      DIR *dirp;

      if (len_avail != -1 && len_avail != 0)
	buffer_free (&buffer);
      len_avail = 0;
      buf = NULL;
      buffer_init (&buffer);
      buffer_grow_str (&buffer, "<osdata type=\"processes\">\n");

      dirp = opendir ("/proc");
      if (dirp)
	{
	  const int num_cores = get_number_of_cpu_cores ();
	  struct dirent *dp;

	  while ((dp = readdir (dirp)) != NULL)
	    {
	      pid_t pid;
	      uid_t owner;
	      char user[UT_NAMESIZE];
	      char *command_line;
	      int *cores;
	      int task_count;
	      char *cores_str;
	      int i;

	      if (!isdigit (dp->d_name[0])
		  || NAMELEN (dp) > sizeof ("4294967295") - 1)
		continue;

	      sscanf (dp->d_name, "%d", &pid);
	      command_line = commandline_from_pid (pid);

	      if (get_process_owner (&owner, pid) == 0)
		user_from_uid (user, sizeof (user), owner);
	      else
		strcpy (user, "?");

	      /* Find CPU cores used by the process.  */
	      cores = (int *) xcalloc (num_cores, sizeof (int));
	      task_count = get_cores_used_by_process (pid, cores);
	      cores_str = (char *) xcalloc (task_count, sizeof ("4294967295") + 1);

	      for (i = 0; i < num_cores && task_count > 0; ++i)
		if (cores[i])
		  {
		    char core_str[sizeof ("4294967205")];

		    sprintf (core_str, "%d", i);
		    strcat (cores_str, core_str);

		    task_count -= cores[i];
		    if (task_count > 0)
		      strcat (cores_str, ",");
		  }

	      xfree (cores);
	      
	      buffer_xml_printf (
		  &buffer,
		  "<item>"
		  "<column name=\"pid\">%d</column>"
		  "<column name=\"user\">%s</column>"
		  "<column name=\"command\">%s</column>"
		  "<column name=\"cores\">%s</column>"
		  "</item>",
		  pid,
		  user,
		  command_line ? command_line : "",
		  cores_str);

	      xfree (command_line);     
	      xfree (cores_str);
	    }
	  
	  closedir (dirp);
	}

      buffer_grow_str0 (&buffer, "</osdata>\n");
      buf = buffer_finish (&buffer);
      len_avail = strlen (buf);
    }

  if (offset >= len_avail)
    {
      /* Done.  Get rid of the buffer.  */
      buffer_free (&buffer);
      buf = NULL;
      len_avail = 0;
      return 0;
    }

  if (len > len_avail - offset)
    len = len_avail - offset;
  memcpy (readbuf, buf + offset, len);

  return len;
}

static LONGEST
linux_xfer_osdata_threads (gdb_byte *readbuf,
			   ULONGEST offset, LONGEST len)
{
  /* We make the process list snapshot when the object starts to be read.  */
  static const char *buf;
  static LONGEST len_avail = -1;
  static struct buffer buffer;

  if (offset == 0)
    {
      DIR *dirp;

      if (len_avail != -1 && len_avail != 0)
	buffer_free (&buffer);
      len_avail = 0;
      buf = NULL;
      buffer_init (&buffer);
      buffer_grow_str (&buffer, "<osdata type=\"threads\">\n");

      dirp = opendir ("/proc");
      if (dirp)
	{
	  struct dirent *dp;

	  while ((dp = readdir (dirp)) != NULL)
	    {
	      struct stat statbuf;
	      char procentry[sizeof ("/proc/4294967295")];

	      if (!isdigit (dp->d_name[0])
		  || NAMELEN (dp) > sizeof ("4294967295") - 1)
		continue;

	      sprintf (procentry, "/proc/%s", dp->d_name);
	      if (stat (procentry, &statbuf) == 0
		  && S_ISDIR (statbuf.st_mode))
		{
		  DIR *dirp2;
		  char *pathname;
		  pid_t pid;
		  char command[32];

		  pathname = xstrprintf ("/proc/%s/task", dp->d_name);
		  
		  pid = atoi (dp->d_name);
		  command_from_pid (command, sizeof (command), pid);

		  dirp2 = opendir (pathname);

		  if (dirp2)
		    {
		      struct dirent *dp2;

		      while ((dp2 = readdir (dirp2)) != NULL)
			{
			  pid_t tid;
			  int core;

			  if (!isdigit (dp2->d_name[0])
			      || NAMELEN (dp2) > sizeof ("4294967295") - 1)
			    continue;

			  tid = atoi (dp2->d_name);
			  core = linux_common_core_of_thread (ptid_build (pid, tid, 0));

			  buffer_xml_printf (
			    &buffer,
			    "<item>"
			    "<column name=\"pid\">%d</column>"
			    "<column name=\"command\">%s</column>"
			    "<column name=\"tid\">%d</column>"
			    "<column name=\"core\">%d</column>"
			    "</item>",
			    pid,
			    command,
			    tid,
			    core);
			}

		      closedir (dirp2);
		    }

		  xfree (pathname);
		}
	    }

	  closedir (dirp);
	}

      buffer_grow_str0 (&buffer, "</osdata>\n");
      buf = buffer_finish (&buffer);
      len_avail = strlen (buf);
    }

  if (offset >= len_avail)
    {
      /* Done.  Get rid of the buffer.  */
      buffer_free (&buffer);
      buf = NULL;
      len_avail = 0;
      return 0;
    }

  if (len > len_avail - offset)
    len = len_avail - offset;
  memcpy (readbuf, buf + offset, len);

  return len;
}

struct osdata_type {
  char *type;
  char *description;
  LONGEST (*getter) (gdb_byte *readbuf, ULONGEST offset, LONGEST len);
} osdata_table[] = {
  { "processes", "Listing of all processes", linux_xfer_osdata_processes },
  { "threads", "Listing of all threads", linux_xfer_osdata_threads },
  { NULL, NULL, NULL }
};

LONGEST
linux_common_xfer_osdata (const char *annex, gdb_byte *readbuf,
			  ULONGEST offset, LONGEST len)
{
  if (!annex || *annex == '\0')
    {
      static const char *buf;
      static LONGEST len_avail = -1;
      static struct buffer buffer;

      if (offset == 0)
	{
	  int i;

	  if (len_avail != -1 && len_avail != 0)
	    buffer_free (&buffer);
	  len_avail = 0;
	  buf = NULL;
	  buffer_init (&buffer);
	  buffer_grow_str (&buffer, "<osdata type=\"types\">\n");

	  for (i = 0; osdata_table[i].type; ++i)
	    buffer_xml_printf (
			       &buffer,
			       "<item>"
			       "<column name=\"Type\">%s</column>"
			       "<column name=\"Description\">%s</column>"
			       "</item>",
			       osdata_table[i].type,
			       osdata_table[i].description);

	  buffer_grow_str0 (&buffer, "</osdata>\n");
	  buf = buffer_finish (&buffer);
	  len_avail = strlen (buf);
	}

      if (offset >= len_avail)
	{
	  /* Done.  Get rid of the buffer.  */
	  buffer_free (&buffer);
	  buf = NULL;
	  len_avail = 0;
	  return 0;
	}

      if (len > len_avail - offset)
	len = len_avail - offset;
      memcpy (readbuf, buf + offset, len);

      return len;
    }
  else
    {
      int i;

      for (i = 0; osdata_table[i].type; ++i)
	{
	  if (strcmp (annex, osdata_table[i].type) == 0)
	    {
	      gdb_assert (readbuf);
	      
	      return (osdata_table[i].getter) (readbuf, offset, len);
	    }
	}

      return 0;
    }
}


/* savedir.c -- save the list of files in a directory in a string
   Copyright (C) 1990, 1997-2001, 2009-2010 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

/* Written by David MacKenzie <djm@gnu.ai.mit.edu>. */

#include <config.h>

#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <stddef.h>

#ifdef CLOSEDIR_VOID
/* Fake a return value. */
# define CLOSEDIR(d) (closedir (d), 0)
#else
# define CLOSEDIR(d) closedir (d)
#endif

#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>
#include "savedir.h"
#include "xalloc.h"

static char *path;
static size_t pathlen;

extern int isdir (const char *name);

static int
isdir1 (const char *dir, const char *file)
{
  size_t dirlen = strlen (dir);
  size_t filelen = strlen (file);

  while (dirlen && dir[dirlen - 1] == '/')
    dirlen--;

  if ((dirlen + filelen + 2) > pathlen)
    {
      pathlen *= 2;
      if ((dirlen + filelen + 2) > pathlen)
        pathlen = dirlen + filelen + 2;

      path = xrealloc (path, pathlen);
    }

  memcpy (path, dir, dirlen);
  path[dirlen] = '/';
  strcpy (path + dirlen + 1, file);
  return isdir (path);
}

/* Return a freshly allocated string containing the filenames
   in directory DIR, separated by '\0' characters;
   the end is marked by two '\0' characters in a row.
   NAME_SIZE is the number of bytes to initially allocate
   for the string; it will be enlarged as needed.
   Return NULL if DIR cannot be opened or if out of memory. */
char *
savedir (const char *dir, off_t name_size, struct exclude *included_patterns,
         struct exclude *excluded_patterns, struct exclude *excluded_directory_patterns )
{
  DIR *dirp;
  struct dirent *dp;
  char *name_space;
  char *namep;

  dirp = opendir (dir);
  if (dirp == NULL)
    return NULL;

  /* Be sure name_size is at least `1' so there's room for
     the final NUL byte.  */
  if (name_size <= 0)
    name_size = 1;

  name_space = (char *) malloc (name_size);
  if (name_space == NULL)
    {
      closedir (dirp);
      return NULL;
    }
  namep = name_space;

  while ((dp = readdir (dirp)) != NULL)
    {
      /* Skip "." and ".." (some NFS file systems' directories lack them). */
      if (dp->d_name[0] != '.'
          || (dp->d_name[1] != '\0'
              && (dp->d_name[1] != '.' || dp->d_name[2] != '\0')))
        {
          size_t namlen = strlen (dp->d_name);
          size_t size_needed = (namep - name_space) + namlen + 2;

          if ((included_patterns || excluded_patterns)
              && !isdir1 (dir, dp->d_name))
            {
              if (included_patterns
                  && excluded_file_name (included_patterns, dp->d_name))
                continue;
              if (excluded_patterns
                  && excluded_file_name (excluded_patterns, dp->d_name))
                continue;
            }

          if ( excluded_directory_patterns
              && isdir1 (dir, dp->d_name) )
            {
              if (excluded_directory_patterns
                  && excluded_file_name (excluded_directory_patterns, dp->d_name))
                continue;
            }

          if (size_needed > name_size)
            {
              char *new_name_space;

              while (size_needed > name_size)
                name_size += 1024;

              new_name_space = realloc (name_space, name_size);
              if (new_name_space == NULL)
                {
                  closedir (dirp);
                  goto fail;
                }
              namep = new_name_space + (namep - name_space);
              name_space = new_name_space;
            }
          strcpy (namep, dp->d_name);
          namep += namlen + 1;
        }
    }
  *namep = '\0';
  if (CLOSEDIR (dirp))
    {
     fail:
      free (name_space);
      name_space = NULL;
    }
  if (path)
    {
      free (path);
      path = NULL;
      pathlen = 0;
    }
  return name_space;
}

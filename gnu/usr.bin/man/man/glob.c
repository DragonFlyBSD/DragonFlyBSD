/* File-name wildcard pattern matching for GNU.
   Copyright (C) 1985, 1988, 1989, 1990, 1991 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 1, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* To whomever it may concern: I have never seen the code which most
   Unix programs use to perform this function.  I wrote this from scratch
   based on specifications for the pattern matching.  --RMS.  */

#ifdef SHELL
#include "config.h"
#endif /* SHELL */

#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>

#define direct dirent
#define D_NAMLEN(d) strlen((d)->d_name)
#define REAL_DIR_ENTRY(dp) (dp->d_ino != 0)

#define bcopy(s, d, n) memcpy ((d), (s), (n))
#define index strchr
#define rindex strrchr

#ifndef	alloca
#define alloca __builtin_alloca
#endif

int glob_pattern_p (char *);
int glob_match (char *, char *, int);
char **glob_vector (char *, const char *);
char **glob_filename (char *);

/* Nonzero if '*' and '?' do not match an initial '.' for glob_filename.  */
int noglob_dot_filenames = 1;

static int glob_match_after_star (char *, char *);

static int
collate_range_cmp (int a, int b)
{
	int r;
	static char s[2][2];

	if ((unsigned char)a == (unsigned char)b)
		return 0;
	s[0][0] = a;
	s[1][0] = b;
	if ((r = strcoll(s[0], s[1])) == 0)
		r = (unsigned char)a - (unsigned char)b;
	return r;
}

/* Return nonzero if PATTERN has any special globbing chars in it.  */

int
glob_pattern_p (char *pattern)
{
  char *p = pattern;
  char c;
  int open = 0;

  while ((c = *p++) != '\0')
    switch (c)
      {
      case '?':
      case '*':
	return 1;

      case '[':		/* Only accept an open brace if there is a close */
	open++;		/* brace to match it.  Bracket expressions must be */
	continue;	/* complete, according to Posix.2 */
      case ']':
	if (open)
	  return 1;
	continue;

      case '\\':
	if (*p++ == '\0')
	  return 0;
      }

  return 0;
}


/* Match the pattern PATTERN against the string TEXT;
   return 1 if it matches, 0 otherwise.

   A match means the entire string TEXT is used up in matching.

   In the pattern string, `*' matches any sequence of characters,
   `?' matches any character, [SET] matches any character in the specified set,
   [!SET] matches any character not in the specified set.

   A set is composed of characters or ranges; a range looks like
   character hyphen character (as in 0-9 or A-Z).
   [0-9a-zA-Z_] is the set of characters allowed in C identifiers.
   Any other character in the pattern must be matched exactly.

   To suppress the special syntactic significance of any of `[]*?!-\',
   and match the character exactly, precede it with a `\'.

   If DOT_SPECIAL is nonzero,
   `*' and `?' do not match `.' at the beginning of TEXT.  */

int
glob_match (char *pattern, char *text, int dot_special)
{
  char *p = pattern, *t = text;
  char c;

  while ((c = *p++) != '\0')
    switch (c)
      {
      case '?':
	if (*t == '\0' || (dot_special && t == text && *t == '.'))
	  return 0;
	else
	  ++t;
	break;

      case '\\':
	if (*p++ != *t++)
	  return 0;
	break;

      case '*':
	if (dot_special && t == text && *t == '.')
	  return 0;
	return glob_match_after_star (p, t);

      case '[':
	{
	  char c1 = *t++;
	  int invert;
	  char *cp1 = p;

	  if (c1 == '\0')
	    return 0;

	  invert = (*p == '!');

	  if (invert)
	    p++;

	  c = *p++;
	  while (1)
	    {
	      char cstart = c, cend = c;

	      if (c == '\\')
		{
		  cstart = *p++;
		  cend = cstart;
		}

	      if (cstart == '\0')
		{
		  /* Missing ']'. */
		  if (c1 != '[')
		    return 0;
		  /* matched a single bracket */
		  p = cp1;
		  goto breakbracket;
		}

	      c = *p++;

	      if (c == '-')
		{
		  cend = *p++;
		  if (cend == '\\')
		    cend = *p++;
		  if (cend == '\0')
		    return 0;
		  c = *p++;
		}
	      if (   collate_range_cmp (c1, cstart) >= 0
		  && collate_range_cmp (c1, cend) <= 0
		 )
		goto match;
	      if (c == ']')
		break;
	    }
	  if (!invert)
	    return 0;
	  break;

	match:
	  /* Skip the rest of the [...] construct that already matched.  */
	  while (c != ']')
	    {
	      if (c == '\0')
		return 0;
	      c = *p++;
	      if (c == '\0')
		return 0;
	      if (c == '\\')
		p++;
	    }
	  if (invert)
	    return 0;
	  breakbracket:
	  break;
	}

      default:
	if (c != *t++)
	  return 0;
      }

  return *t == '\0';
}

/* Like glob_match, but match PATTERN against any final segment of TEXT.  */

static int
glob_match_after_star (char *pattern, char *text)
{
  char *p = pattern, *t = text;
  char c, c1;

  while ((c = *p++) == '?' || c == '*')
    if (c == '?' && *t++ == '\0')
      return 0;

  if (c == '\0')
    return 1;

  if (c == '\\')
    c1 = *p;
  else
    c1 = c;

  --p;
  while (1)
    {
      if ((c == '[' || *t == c1) && glob_match (p, t, 0))
	return 1;
      if (*t++ == '\0')
	return 0;
    }
}

/* Return a vector of names of files in directory DIR
   whose names match glob pattern PAT.
   The names are not in any particular order.
   Wildcards at the beginning of PAT do not match an initial period
   if noglob_dot_filenames is nonzero.

   The vector is terminated by an element that is a null pointer.

   To free the space allocated, first free the vector's elements,
   then free the vector.

   Return NULL if cannot get enough memory to hold the pointer
   and the names.

   Return -1 if cannot access directory DIR.
   Look in errno for more information.  */

char **
glob_vector (char *pat, const char *dir)
{
  struct globval
  {
    struct globval *next;
    char *name;
  };

  DIR *d;
  struct direct *dp;
  struct globval *lastlink;
  struct globval *nextlink;
  char *nextname;
  unsigned int count;
  int lose;
  char **name_vector;
  unsigned int i;

  d = opendir (dir);
  if (d == NULL)
    return (char **) -1;

  lastlink = NULL;
  count = 0;
  lose = 0;

  /* Scan the directory, finding all names that match.
     For each name that matches, allocate a struct globval
     on the stack and store the name in it.
     Chain those structs together; lastlink is the front of the chain.  */
  while (1)
    {
#if defined (SHELL)
      /* Make globbing interruptible in the bash shell. */
      extern int interrupt_state;

      if (interrupt_state)
	{
	  closedir (d);
	  lose = 1;
	  goto lost;
	}
#endif /* SHELL */

      dp = readdir (d);
      if (dp == NULL)
	break;
      if (REAL_DIR_ENTRY (dp)
	  && glob_match (pat, dp->d_name, noglob_dot_filenames))
	{
	  nextlink = (struct globval *) alloca (sizeof (struct globval));
	  nextlink->next = lastlink;
	  i = D_NAMLEN (dp) + 1;
	  nextname = (char *) malloc (i);
	  if (nextname == NULL)
	    {
	      lose = 1;
	      break;
	    }
	  lastlink = nextlink;
	  nextlink->name = nextname;
	  bcopy (dp->d_name, nextname, i);
	  count++;
	}
    }
  closedir (d);

  if (!lose)
    {
      name_vector = (char **) malloc ((count + 1) * sizeof (char *));
      lose |= name_vector == NULL;
    }

  /* Have we run out of memory?  */
#ifdef	SHELL
 lost:
#endif
  if (lose)
    {
      /* Here free the strings we have got.  */
      while (lastlink)
	{
	  free (lastlink->name);
	  lastlink = lastlink->next;
	}
      return NULL;
    }

  /* Copy the name pointers from the linked list into the vector.  */
  for (i = 0; i < count; ++i)
    {
      name_vector[i] = lastlink->name;
      lastlink = lastlink->next;
    }

  name_vector[count] = NULL;
  return name_vector;
}

/* Return a new array, replacing ARRAY, which is the concatenation
   of each string in ARRAY to DIR.
   Return NULL if out of memory.  */

static char **
glob_dir_to_array (char *dir, char **array)
{
  unsigned int i, l;
  int add_slash = 0;
  char **result;

  l = strlen (dir);
  if (l == 0)
    return array;

  if (dir[l - 1] != '/')
    add_slash++;

  for (i = 0; array[i] != NULL; i++)
    ;

  result = (char **) malloc ((i + 1) * sizeof (char *));
  if (result == NULL)
    return NULL;

  for (i = 0; array[i] != NULL; i++)
    {
      result[i] = (char *) malloc (1 + l + add_slash + strlen (array[i]));
      if (result[i] == NULL)
	return NULL;
      strcpy (result[i], dir);
      if (add_slash)
	result[i][l] = '/';
      strcpy (result[i] + l + add_slash, array[i]);
    }
  result[i] = NULL;

  /* Free the input array.  */
  for (i = 0; array[i] != NULL; i++)
    free (array[i]);
  free ((char *) array);
  return result;
}

/* Do globbing on PATHNAME.  Return an array of pathnames that match,
   marking the end of the array with a null-pointer as an element.
   If no pathnames match, then the array is empty (first element is null).
   If there isn't enough memory, then return NULL.
   If a file system error occurs, return -1; `errno' has the error code.

   Wildcards at the beginning of PAT, or following a slash,
   do not match an initial period if noglob_dot_filenames is nonzero.  */

char **
glob_filename (char *pathname)
{
  char **result;
  unsigned int result_size;
  char *directory_name, *filename;
  unsigned int directory_len;

  result = (char **) malloc (sizeof (char *));
  result_size = 1;
  if (result == NULL)
    return NULL;

  result[0] = NULL;

  /* Find the filename.  */
  filename = rindex (pathname, '/');
  if (filename == NULL)
    {
      filename = pathname;
      directory_name = NULL;
      directory_len = 0;
    }
  else
    {
      directory_len = (filename - pathname) + 1;
      directory_name = (char *) alloca (directory_len + 1);
      bcopy (pathname, directory_name, directory_len);
      directory_name[directory_len] = '\0';
      ++filename;
    }

  /* If directory_name contains globbing characters, then we
     have to expand the previous levels.  Just recurse. */
  if (glob_pattern_p (directory_name))
    {
      char **directories;
      unsigned int i;

      if (directory_name[directory_len - 1] == '/')
	directory_name[directory_len - 1] = '\0';

      directories = glob_filename (directory_name);
      if (directories == NULL)
	goto memory_error;
      else if (directories == (char **) -1)
	return (char **) -1;
      else if (*directories == NULL)
	{
	  free ((char *) directories);
	  return (char **) -1;
	}

      /* We have successfully globbed the preceding directory name.
	 For each name in DIRECTORIES, call glob_vector on it and
	 FILENAME.  Concatenate the results together.  */
      for (i = 0; directories[i] != NULL; i++)
	{
	  char **temp_results = glob_vector (filename, directories[i]);
	  if (temp_results == NULL)
	    goto memory_error;
	  else if (temp_results == (char **) -1)
	    /* This filename is probably not a directory.  Ignore it.  */
	    ;
	  else
	    {
	      char **array = glob_dir_to_array (directories[i], temp_results);
	      unsigned int l;

	      l = 0;
	      while (array[l] != NULL)
		++l;

	      result = (char **) realloc (result,
					  (result_size + l) * sizeof (char *));
	      if (result == NULL)
		goto memory_error;

	      for (l = 0; array[l] != NULL; ++l)
		result[result_size++ - 1] = array[l];
	      result[result_size - 1] = NULL;
	      free ((char *) array);
	    }
	}
      /* Free the directories.  */
      for (i = 0; directories[i] != NULL; i++)
	free (directories[i]);
      free ((char *) directories);

      return result;
    }

  /* If there is only a directory name, return it. */
  if (*filename == '\0')
    {
      result = (char **) realloc ((char *) result, 2 * sizeof (char *));
      if (result != NULL)
	{
	  result[0] = (char *) malloc (directory_len + 1);
	  if (result[0] == NULL)
	    {
	      goto memory_error;
	    }
	  bcopy (directory_name, result[0], directory_len + 1);
	  result[1] = NULL;
	}
      return result;
    }
  else
    {
      /* Otherwise, just return what glob_vector
	 returns appended to the directory name. */
      char **temp_results = glob_vector (filename,
					 (directory_len == 0
					  ? "." : directory_name));

      if (temp_results == NULL || temp_results == (char **) -1)
	{
	  return temp_results;
	}

      temp_results = glob_dir_to_array (directory_name, temp_results);
      return temp_results;
    }

  /* We get to memory error if the program has run out of memory, or
     if this is the shell, and we have been interrupted. */
 memory_error:
  if (result != NULL)
    {
      unsigned int i;
      for (i = 0; result[i] != NULL; ++i)
	free (result[i]);
      free ((char *) result);
    }
#if defined (SHELL)
  {
    extern int interrupt_state;

    if (interrupt_state)
      throw_to_top_level ();
  }
#endif /* SHELL */
  return NULL;
}

#ifdef TEST

int
main (int argc, char **argv)
{
  char **value;
  int i, optind;

  for (optind = 1; optind < argc; optind++)
    {
      value = glob_filename (argv[optind]);
      if (value == NULL)
	puts ("virtual memory exhausted");
      else if (value == (char **) -1)
	perror (argv[optind]);
      else
	for (i = 0; value[i] != NULL; i++)
	  puts (value[i]);
    }
  exit (0);
}

#endif /* TEST */

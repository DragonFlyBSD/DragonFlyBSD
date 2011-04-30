/* search.c -- searching large bodies of text.
   $Id: search.c,v 1.9 2008/06/11 09:55:42 gray Exp $

   Copyright (C) 1993, 1997, 1998, 2002, 2004, 2007, 2008
   Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

   Originally written by Brian Fox (bfox@ai.mit.edu). */

#include "info.h"
#include <regex.h>

#include "search.h"
#include "nodes.h"

/* The search functions take two arguments:

     1) a string to search for, and

     2) a pointer to a SEARCH_BINDING which contains the buffer, start,
        and end of the search.

   They return a long, which is the offset from the start of the buffer
   at which the match was found.  An offset of -1 indicates failure. */

/* A function which makes a binding with buffer and bounds. */
SEARCH_BINDING *
make_binding (char *buffer, long int start, long int end)
{
  SEARCH_BINDING *binding;

  binding = xmalloc (sizeof (SEARCH_BINDING));
  binding->buffer = buffer;
  binding->start = start;
  binding->end = end;
  binding->flags = 0;

  return binding;
}

/* Make a copy of BINDING without duplicating the data. */
SEARCH_BINDING *
copy_binding (SEARCH_BINDING *binding)
{
  SEARCH_BINDING *copy;

  copy = make_binding (binding->buffer, binding->start, binding->end);
  copy->flags = binding->flags;
  return copy;
}


/* **************************************************************** */
/*                                                                  */
/*                 The Actual Searching Functions                   */
/*                                                                  */
/* **************************************************************** */

/* Search forwards or backwards for the text delimited by BINDING.
   The search is forwards if BINDING->start is greater than BINDING->end. */
long
search (char *string, SEARCH_BINDING *binding)
{
  long result;

  /* If the search is backwards, then search backwards, otherwise forwards. */
  if (binding->start > binding->end)
    result = search_backward (string, binding);
  else
    result = search_forward (string, binding);

  return result;
}

/* Search forwards or backwards for anything matching the regexp in the text
   delimited by BINDING. The search is forwards if BINDING->start is greater
   than BINDING->end.

   If PRET is specified, it receives a copy of BINDING at the end of a
   succeded search.  Its START and END fields contain bounds of the found
   string instance. 
*/
long
regexp_search (char *regexp, SEARCH_BINDING *binding, long length,
	       SEARCH_BINDING *pret)
{
  static char *previous_regexp = NULL;
  static char *previous_content = NULL;
  static int was_insensitive = 0;
  static regex_t preg;
  static regmatch_t *matches;
  static int match_alloc = 0;
  static int match_count = 0;
  regoff_t pos;

  if (previous_regexp == NULL
      || ((binding->flags & S_FoldCase) != was_insensitive)
      || (strcmp (previous_regexp, regexp) != 0))
    {
      /* need to compile a new regexp */
      int result;
      char *unescaped_regexp;
      char *p, *q;

      previous_content = NULL;

      if (previous_regexp != NULL)
        {
          free (previous_regexp);
          previous_regexp = NULL;
          regfree (&preg);
        }

      was_insensitive = binding->flags & S_FoldCase;

      /* expand the \n and \t in regexp */
      unescaped_regexp = xmalloc (1 + strlen (regexp));
      for (p = regexp, q = unescaped_regexp; *p != '\0'; p++, q++)
        {
          if (*p == '\\')
            switch(*++p)
              {
              case 'n':
                *q = '\n';
                break;
              case 't':
                *q = '\t';
                break;
              case '\0':
                *q = '\\';
                p--;
                break;
              default:
                *q++ = '\\';
                *q = *p;
                break;
              }
          else
            *q = *p;
        }
      *q = '\0';

      result = regcomp (&preg, unescaped_regexp,
                       REG_EXTENDED|
                       REG_NEWLINE|
                       (was_insensitive ? REG_ICASE : 0));
      free (unescaped_regexp);

      if (result != 0)
        {
          int size = regerror (result, &preg, NULL, 0);
          char *buf = xmalloc (size);
          regerror (result, &preg, buf, size);
          info_error (_("regexp error: %s"), buf, NULL);
          return -1;
        }

      previous_regexp = xstrdup(regexp);
    }

  if (previous_content != binding->buffer)
    {
      /* new buffer to search in, let's scan it */
      regoff_t start = 0;
      char saved_char;

      previous_content = binding->buffer;
      saved_char = previous_content[length-1];
      previous_content[length-1] = '\0';

      for (match_count = 0; start < length; )
        {
          int result = 0;
          if (match_count >= match_alloc)
            {
              /* match list full. Initially allocate 256 entries, then double
                 every time we fill it */
              match_alloc = (match_alloc > 0 ? match_alloc * 2 : 256);
              matches = xrealloc (matches,
				  match_alloc * sizeof(regmatch_t));
            }

          result = regexec (&preg, &previous_content[start],
                            1, &matches[match_count], 0);
          if (result == 0)
            {
              if (matches[match_count].rm_eo == 0)
                {
                  /* ignore empty matches */
                  start++;
                }
              else
                {
                  matches[match_count].rm_so += start;
                  matches[match_count].rm_eo += start;
                  start = matches[match_count++].rm_eo;
                }
            }
          else
            {
              break;
            }
        }
      previous_content[length-1] = saved_char;
    }

  pos = binding->start;
  if (pos > binding->end)
    {
      /* searching backward */
      int i;
      for (i = match_count - 1; i >= 0; i--)
        {
          if (matches[i].rm_so <= pos)
	    {
	      if (pret)
		{
		  pret->buffer = binding->buffer;
		  pret->flags = binding->flags;
		  pret->start = matches[i].rm_so;
		  pret->end = matches[i].rm_eo;
		}
	      return matches[i].rm_so;
	    }
        }
    }
  else
    {
      /* searching forward */
      int i;
      for (i = 0; i < match_count; i++)
        {
          if (matches[i].rm_so >= pos)
            {
	      if (pret)
		{
		  pret->buffer = binding->buffer;
		  pret->flags = binding->flags;
		  pret->start = matches[i].rm_so;
		  pret->end = matches[i].rm_eo;
		}
              if (binding->flags & S_SkipDest)
                return matches[i].rm_eo;
              else
                return matches[i].rm_so;
            }
        }
    }

  /* not found */
  return -1;
}

/* Search forwards for STRING through the text delimited in BINDING. */
long
search_forward (char *string, SEARCH_BINDING *binding)
{
  register int c, i, len;
  register char *buff, *end;
  char *alternate = NULL;

  len = strlen (string);

  /* We match characters in the search buffer against STRING and ALTERNATE.
     ALTERNATE is a case reversed version of STRING; this is cheaper than
     case folding each character before comparison.   Alternate is only
     used if the case folding bit is turned on in the passed BINDING. */

  if (binding->flags & S_FoldCase)
    {
      alternate = xstrdup (string);

      for (i = 0; i < len; i++)
        {
          if (islower (alternate[i]))
            alternate[i] = toupper (alternate[i]);
          else if (isupper (alternate[i]))
            alternate[i] = tolower (alternate[i]);
        }
    }

  buff = binding->buffer + binding->start;
  end = binding->buffer + binding->end + 1;

  while (buff < (end - len))
    {
      for (i = 0; i < len; i++)
        {
          c = buff[i];

          if ((c != string[i]) && (!alternate || c != alternate[i]))
            break;
        }

      if (!string[i])
        {
          if (alternate)
            free (alternate);
          if (binding->flags & S_SkipDest)
            buff += len;
          return buff - binding->buffer;
        }

      buff++;
    }

  if (alternate)
    free (alternate);

  return -1;
}

/* Search for STRING backwards through the text delimited in BINDING. */
long
search_backward (char *input_string, SEARCH_BINDING *binding)
{
  register int c, i, len;
  register char *buff, *end;
  char *string;
  char *alternate = NULL;

  len = strlen (input_string);

  /* Reverse the characters in the search string. */
  string = xmalloc (1 + len);
  for (c = 0, i = len - 1; input_string[c]; c++, i--)
    string[i] = input_string[c];

  string[c] = '\0';

  /* We match characters in the search buffer against STRING and ALTERNATE.
     ALTERNATE is a case reversed version of STRING; this is cheaper than
     case folding each character before comparison.   ALTERNATE is only
     used if the case folding bit is turned on in the passed BINDING. */

  if (binding->flags & S_FoldCase)
    {
      alternate = xstrdup (string);

      for (i = 0; i < len; i++)
        {
          if (islower (alternate[i]))
            alternate[i] = toupper (alternate[i]);
          else if (isupper (alternate[i]))
            alternate[i] = tolower (alternate[i]);
        }
    }

  buff = binding->buffer + binding->start - 1;
  end = binding->buffer + binding->end;

  while (buff > (end + len))
    {
      for (i = 0; i < len; i++)
        {
          c = *(buff - i);

          if (c != string[i] && (!alternate || c != alternate[i]))
            break;
        }

      if (!string[i])
        {
          free (string);
          if (alternate)
            free (alternate);

          if (binding->flags & S_SkipDest)
            buff -= len;
          return 1 + buff - binding->buffer;
        }

      buff--;
    }

  free (string);
  if (alternate)
    free (alternate);

  return -1;
}

/* Find STRING in LINE, returning the offset of the end of the string.
   Return an offset of -1 if STRING does not appear in LINE.  The search
   is bound by the end of the line (i.e., either NEWLINE or 0). */
int
string_in_line (char *string, char *line)
{
  register int end;
  SEARCH_BINDING binding;

  /* Find the end of the line. */
  for (end = 0; line[end] && line[end] != '\n'; end++);

  /* Search for STRING within these confines. */
  binding.buffer = line;
  binding.start = 0;
  binding.end = end;
  binding.flags = S_FoldCase | S_SkipDest;

  return search_forward (string, &binding);
}

/* Return non-zero if STRING is the first text to appear at BINDING. */
int
looking_at (char *string, SEARCH_BINDING *binding)
{
  long search_end;

  search_end = search (string, binding);

  /* If the string was not found, SEARCH_END is -1.  If the string was found,
     but not right away, SEARCH_END is != binding->start.  Otherwise, the
     string was found at binding->start. */
  return search_end == binding->start;
}

/* **************************************************************** */
/*                                                                  */
/*                    Small String Searches                         */
/*                                                                  */
/* **************************************************************** */

/* Function names that start with "skip" are passed a string, and return
   an offset from the start of that string.  Function names that start
   with "find" are passed a SEARCH_BINDING, and return an absolute position
   marker of the item being searched for.  "Find" functions return a value
   of -1 if the item being looked for couldn't be found. */

/* Return the index of the first non-whitespace character in STRING. */
int
skip_whitespace (char *string)
{
  register int i;

  for (i = 0; string && whitespace (string[i]); i++);
  return i;
}

/* Return the index of the first non-whitespace or newline character in
   STRING. */
int
skip_whitespace_and_newlines (char *string)
{
  register int i;

  for (i = 0; string && whitespace_or_newline (string[i]); i++);
  return i;
}

/* Return the index of the first whitespace character in STRING. */
int
skip_non_whitespace (char *string)
{
  register int i;

  for (i = 0; string && string[i] && !whitespace (string[i]); i++);
  return i;
}

/* Return the index of the first non-node character in STRING.  Note that
   this function contains quite a bit of hair to ignore periods in some
   special cases.  This is because we here at GNU ship some info files which
   contain nodenames that contain periods.  No such nodename can start with
   a period, or continue with whitespace, newline, or ')' immediately following
   the period.  If second argument NEWLINES_OKAY is non-zero, newlines should
   be skipped while parsing out the nodename specification. */
int
skip_node_characters (char *string, int newlines_okay)
{
  register int c, i = 0;
  int paren_seen = 0;
  int paren = 0;

  /* Handle special case.  This is when another function has parsed out the
     filename component of the node name, and we just want to parse out the
     nodename proper.  In that case, a period at the start of the nodename
     indicates an empty nodename. */
  if (string && *string == '.')
    return 0;

  if (string && *string == '(')
    {
      paren++;
      paren_seen++;
      i++;
    }

  for (; string && (c = string[i]); i++)
    {
      if (paren)
        {
          if (c == '(')
            paren++;
          else if (c == ')')
            paren--;

          continue;
        }
      
      /* If the character following the close paren is a space or period,
         then this node name has no more characters associated with it. */
      if (c == '\t' ||
          c == ','  ||
          c == INFO_TAGSEP ||
          ((!newlines_okay) && (c == '\n')) ||
          ((paren_seen && string[i - 1] == ')') &&
           (c == ' ' || c == '.')) ||
          (c == '.' &&
           (
#if 0
/* This test causes a node name ending in a period, like `This.', not to
   be found.  The trailing . is stripped.  This occurs in the jargon
   file (`I see no X here.' is a node name).  */
           (!string[i + 1]) ||
#endif
            (whitespace_or_newline (string[i + 1])) ||
            (string[i + 1] == ')'))))
        break;
    }
  return i;
}


/* **************************************************************** */
/*                                                                  */
/*                   Searching FILE_BUFFER's                        */
/*                                                                  */
/* **************************************************************** */

/* Return the absolute position of the first occurence of a node separator in
   BINDING-buffer.  The search starts at BINDING->start.  Return -1 if no node
   separator was found. */
long
find_node_separator (SEARCH_BINDING *binding)
{
  register long i;
  char *body;

  body = binding->buffer;

  /* A node is started by [^L]^_[^L]\n.  That is to say, the C-l's are
     optional, but the DELETE and NEWLINE are not.  This separator holds
     true for all separated elements in an Info file, including the tags
     table (if present) and the indirect tags table (if present). */
  for (i = binding->start; i < binding->end - 1; i++)
    if (((body[i] == INFO_FF && body[i + 1] == INFO_COOKIE) &&
         (body[i + 2] == '\n' ||
          (body[i + 2] == INFO_FF && body[i + 3] == '\n'))) ||
        ((body[i] == INFO_COOKIE) &&
         (body[i + 1] == '\n' ||
          (body[i + 1] == INFO_FF && body[i + 2] == '\n'))))
      return i;
  return -1;
}

/* Return the length of the node separator characters that BODY is
   currently pointing at. */
int
skip_node_separator (char *body)
{
  register int i;

  i = 0;

  if (body[i] == INFO_FF)
    i++;

  if (body[i++] != INFO_COOKIE)
    return 0;

  if (body[i] == INFO_FF)
    i++;

  if (body[i++] != '\n')
    return 0;

  return i;
}

/* Return the number of characters from STRING to the start of
   the next line. */
int
skip_line (char *string)
{
  register int i;

  for (i = 0; string && string[i] && string[i] != '\n'; i++);

  if (string[i] == '\n')
    i++;

  return i;
}

/* Return the absolute position of the beginning of a tags table in this
   binding starting the search at binding->start. */
long
find_tags_table (SEARCH_BINDING *binding)
{
  SEARCH_BINDING tmp_search;
  long position;

  tmp_search.buffer = binding->buffer;
  tmp_search.start = binding->start;
  tmp_search.end = binding->end;
  tmp_search.flags = S_FoldCase;

  while ((position = find_node_separator (&tmp_search)) != -1 )
    {
      tmp_search.start = position;
      tmp_search.start += skip_node_separator (tmp_search.buffer
          + tmp_search.start);

      if (looking_at (TAGS_TABLE_BEG_LABEL, &tmp_search))
        return position;
    }
  return -1;
}

/* Return the absolute position of the node named NODENAME in BINDING.
   This is a brute force search, and we wish to avoid it when possible.
   This function is called when a tag (indirect or otherwise) doesn't
   really point to the right node.  It returns the absolute position of
   the separator preceding the node. */
long
find_node_in_binding (char *nodename, SEARCH_BINDING *binding)
{
  long position;
  int offset, namelen;
  SEARCH_BINDING tmp_search;

  namelen = strlen (nodename);

  tmp_search.buffer = binding->buffer;
  tmp_search.start = binding->start;
  tmp_search.end = binding->end;
  tmp_search.flags = 0;

  while ((position = find_node_separator (&tmp_search)) != -1)
    {
      tmp_search.start = position;
      tmp_search.start += skip_node_separator
        (tmp_search.buffer + tmp_search.start);

      offset = string_in_line
        (INFO_NODE_LABEL, tmp_search.buffer + tmp_search.start);

      if (offset == -1)
        continue;

      tmp_search.start += offset;
      tmp_search.start += skip_whitespace (tmp_search.buffer + tmp_search.start);
      offset = skip_node_characters
        (tmp_search.buffer + tmp_search.start, DONT_SKIP_NEWLINES);

      /* Notice that this is an exact match.  You cannot grovel through
         the buffer with this function looking for random nodes. */
       if ((offset == namelen) &&
           (tmp_search.buffer[tmp_search.start] == nodename[0]) &&
           (strncmp (tmp_search.buffer + tmp_search.start, nodename, offset) == 0))
         return position;
    }
  return -1;
}

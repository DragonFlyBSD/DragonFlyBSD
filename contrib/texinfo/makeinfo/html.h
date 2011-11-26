/* html.h -- declarations for html-related utilities.
   $Id: html.h,v 1.11 2008/05/19 18:26:48 karl Exp $

   Copyright (C) 1999, 2000, 2002, 2004, 2007, 2008
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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef HTML_H
#define HTML_H

/* A stack of font tags.  */
typedef struct hstack
{
  struct hstack *next;
  char *tag;
  char *attribs;
} HSTACK;

/* Nonzero if we have output a title, from @titlefont or @settitle.  */
extern int html_title_written;

/* Filename to which to write list of index entries, and stream for them */
extern char *internal_links_filename;
extern FILE *internal_links_stream;

/* Perform the <head> output.  */
extern void html_output_head (void);

/* Escape &<>.  */
extern char *escape_string (char *);

/* Open or close TAG according to START_OR_END.  */
extern void insert_html_tag (int start_or_end, char *tag);

/* Output HTML <link> to NODE, plus extra ATTRIBUTES.  */
extern void add_link (char *nodename, char *attributes);

/* Escape URL-special characters.  */
extern char *escaped_anchor_name (const char *name);
extern void add_escaped_anchor_name (char *name, int old);

/* See html.c.  */
extern void add_anchor_name (char *nodename, int href);
extern void add_url_name (char *nodename, int href);
extern void add_nodename_to_filename (char *nodename, int href);
extern char *nodename_to_filename (char *nodename);
extern int rollback_empty_tag (char *tag);

#if defined (VA_FPRINTF) && __STDC__
extern void insert_html_tag_with_attribute (int start_or_end, char *tag, char *format, ...);
#else
extern void insert_html_tag_with_attribute ();
#endif

#endif /* !HTML_H */

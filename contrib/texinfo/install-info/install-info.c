/* install-info -- create Info directory entry(ies) for an Info file.
   $Id: install-info.c,v 1.13 2008/05/18 16:54:02 karl Exp $

   Copyright (C) 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004,
   2005, 2007, 2008 Free Software Foundation, Inc.

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

#include "system.h"
#include <getopt.h>
#include <regex.h>
#include <argz.h>

#define TAB_WIDTH 8

static char *progname = "install-info";

struct spec_entry;
struct spec_section;

struct line_data *findlines (char *data, int size, int *nlinesp);
void insert_entry_here (struct spec_entry *entry, int line_number,
                        struct line_data *dir_lines, int n_entries); 
int compare_section_names (const void *s1, const void *s2);
int compare_entries_text (const void *e1, const void *e2); 

/* Data structures.  */


/* Record info about a single line from a file as read into core.  */
struct line_data
{
  /* The start of the line.  */
  char *start;
  /* The number of characters in the line,
     excluding the terminating newline.  */
  int size;
  /* Vector containing pointers to the entries to add before this line.
     The vector is null-terminated.  */
  struct spec_entry **add_entries_before;
  /* Vector containing pointers to the sections to add before this line.
     The vector is not null-terminated.  */
  struct spec_section **add_sections_before;
  /* The vector ADD_SECTIONS_BEFORE_HERE contains exactly this many
     pointers to sections. */
  int num_sections_to_add;
  /* 1 means don't output this line.  */
  int delete;
};


/* This is used for a list of the specified menu section names
   in which entries should be added.  */
struct spec_section
{
  struct spec_section *next;
  char *name;
  /* 1 means we have not yet found an existing section with this name
     in the dir file--so we will need to add a new section.  */
  int missing;
};


/* This is used for a list of the entries specified to be added.  */
struct spec_entry
{
  struct spec_entry *next;
  char *text;
  size_t text_len;
  /* A pointer to the list of sections to which this entry should be
     added.  */
  struct spec_section *entry_sections;
  /* A pointer to a section that is beyond the end of the chain whose
     head is pointed to by entry_sections.  */
  struct spec_section *entry_sections_tail;
  /* Non-zero means that the entry doesn't have a name specified.  This
     can only happen if a --description preceeds a --name option. */
  int missing_name;
  /* Non-zero means that the entry doesn't have a description.  This
     happens when a --name option is given prior to a --description 
     option. */
  int missing_description;
  /* Non-zero means that the entry doesn't have an Info file specified.  
     This means that the entry was taken from the command-line but it
     only contains the name, and not the info file's basename, which
     we get later on.  This only happens on entries that originate
     from --name options. */
  int missing_basename;
};


/* This is used for a list of nodes found by parsing the dir file.  */
struct node
{
  struct node *next;
  /* The node name.  */
  char *name;
  /* The line number of the line where the node starts.
     This is the line that contains control-underscore.  */
  int start_line;
  /* The line number of the line where the node ends,
     which is the end of the file or where the next line starts.  */
  int end_line;
  /* Start of first line in this node's menu
     (the line after the * Menu: line).  */
  char *menu_start;
  /* The start of the chain of sections in this node's menu.  */
  struct menu_section *sections;
  /* The last menu section in the chain.  */
  struct menu_section *last_section;
};


/* This is used for a list of sections found in a node's menu.
   Each  struct node  has such a list in the  sections  field.  */
struct menu_section
{
  struct menu_section *next;
  char *name;
  /* Line number of start of section.  */
  int start_line;
  /* Line number of end of section.  */
  int end_line;
};

/* This table defines all the long-named options, says whether they
   use an argument, and maps them into equivalent single-letter options.  */

struct option longopts[] =
{
  { "add-once",  no_argument, NULL, '1'},
  { "align",     required_argument, NULL, 'A'},
  { "append-new-sections", no_argument, NULL, 'a'},
  { "calign",    required_argument, NULL, 'C'},
  { "debug",     no_argument, NULL, 'g' },
  { "delete",    no_argument, NULL, 'r' },
  { "dir-file",  required_argument, NULL, 'd' },
  { "entry",     required_argument, NULL, 'e' },
  { "name",      required_argument, NULL, 't' },
  { "menuentry", required_argument, NULL, 't' },
  { "description", required_argument, NULL, 'c' },
  { "help",      no_argument, NULL, 'h' },
  { "no-indent", no_argument, NULL, 'I' },
  { "infodir",   required_argument, NULL, 'D' },
  { "info-dir",  required_argument, NULL, 'D' },
  { "info-file", required_argument, NULL, 'i' },
  { "item",      required_argument, NULL, 'e' },
  { "keep-old",  no_argument, NULL, 'k' },
  { "maxwidth",  required_argument, NULL, 'W'},
  { "max-width", required_argument, NULL, 'W'},
  { "quiet",     no_argument, NULL, 'q' },
  { "remove",    no_argument, NULL, 'r' },
  { "remove-exactly",    no_argument, NULL, 'x' },
  { "section",           required_argument, NULL, 's' },
  { "regex",     required_argument, NULL, 'R' },
  { "silent",    no_argument, NULL, 'q' },
  { "test",      no_argument, NULL, 'n' },
  { "dry-run",   no_argument, NULL, 'n' },
  { "version",   no_argument, NULL, 'V' },
  { 0 }
};

regex_t *psecreg = NULL;

/* Nonzero means that the name specified for the Info file will be used
   (without removing .gz, .info extension or leading path) to match the
   entries that must be removed.  */
int remove_exactly = 0;

/* Nonzero means that sections that don't have entries in them will be
   deleted. */
int remove_empty_sections = 1;

/* Nonzero means that new Info entries into the DIR file will go into all 
   sections that match with --section-regex or --section.  Zero means 
   that new entries wil go into the first section that matches.*/
int add_entries_into_all_matching_sections = 1;

/* Nonzero means we do not replace same-named info entries. */
int keep_old_flag = 0;

/* Nonzero means --test was specified, to inhibit updating the dir file.  */
int chicken_flag = 0;

/* Zero means that entries will not be formatted when they are either 
   added or replaced. */
int indent_flag = 1;

/* Zero means that new sections will be added at the end of the DIR file. */
int order_new_sections_alphabetically_flag = 1;


/* Error message functions.  */

/* Print error message.  S1 is printf control string, S2 and S3 args for it. */

/* VARARGS1 */
void
error (const char *s1, const char *s2, const char *s3)
{
  fprintf (stderr, "%s: ", progname);
  fprintf (stderr, s1, s2, s3);
  putc ('\n', stderr);
}

/* VARARGS1 */
void
warning (const char *s1, const char *s2, const char *s3)
{
  fprintf (stderr, _("%s: warning: "), progname);
  fprintf (stderr, s1, s2, s3);
  putc ('\n', stderr);
}

/* Print error message and exit.  */

void
fatal (const char *s1, const char *s2, const char *s3)
{
  error (s1, s2, s3);
  xexit (1);
}

/* Return a newly-allocated string
   whose contents concatenate those of S1, S2, S3.  */
char *
concat (const char *s1, const char *s2, const char *s3)
{
  int len1 = strlen (s1), len2 = strlen (s2), len3 = strlen (s3);
  char *result = (char *) xmalloc (len1 + len2 + len3 + 1);

  strcpy (result, s1);
  strcpy (result + len1, s2);
  strcpy (result + len1 + len2, s3);
  *(result + len1 + len2 + len3) = 0;

  return result;
}

/* Return a string containing SIZE characters
   copied from starting at STRING.  */

char *
copy_string (const char *string, int size)
{
  int i;
  char *copy = (char *) xmalloc (size + 1);
  for (i = 0; i < size; i++)
    copy[i] = string[i];
  copy[size] = 0;
  return copy;
}

/* Print fatal error message based on errno, with file name NAME.  */

void
pfatal_with_name (const char *name)
{
  char *s = concat ("", strerror (errno), _(" for %s"));
  fatal (s, name, 0);
}

/* Compare the menu item names in LINE1 (line length LEN1)
   and LINE2 (line length LEN2).  Return 1 if the item name
   in LINE1 is less, 0 otherwise.  */

static int
menu_line_lessp (char *line1, int len1, char *line2, int len2)
{
  int minlen = (len1 < len2 ? len1 : len2);
  int i;

  for (i = 0; i < minlen; i++)
    {
      /* If one item name is a prefix of the other,
         the former one is less.  */
      if (line1[i] == ':' && line2[i] != ':')
        return 1;
      if (line2[i] == ':' && line1[i] != ':')
        return 0;
      /* If they both continue and differ, one is less.  */
      if (line1[i] < line2[i])
        return 1;
      if (line1[i] > line2[i])
        return 0;
    }
  /* With a properly formatted dir file,
     we can only get here if the item names are equal.  */
  return 0;
}

/* Compare the menu item names in LINE1 (line length LEN1)
   and LINE2 (line length LEN2).  Return 1 if the item names are equal,
   0 otherwise.  */

static int
menu_line_equal (char *line1, int len1, char *line2, int len2)
{
  int minlen = (len1 < len2 ? len1 : len2);
  int i;

  for (i = 0; i < minlen; i++)
    {
      /* If both item names end here, they are equal.  */
      if (line1[i] == ':' && line2[i] == ':')
        return 1;
      /* If they both continue and differ, one is less.  */
      if (line1[i] != line2[i])
        return 0;
    }
  /* With a properly formatted dir file,
     we can only get here if the item names are equal.  */
  return 1;
}


/* Given the full text of a menu entry, null terminated,
   return just the menu item name (copied).  */

char *
extract_menu_item_name (char *item_text)
{
  char *p;

  if (*item_text == '*')
    item_text++;
  while (*item_text == ' ')
    item_text++;

  p = item_text;
  while (*p && *p != ':') p++;
  return copy_string (item_text, p - item_text);
}

/* Given the full text of a menu entry, terminated by null or newline,
   return just the menu item file (copied).  */

char *
extract_menu_file_name (char *item_text)
{
  char *p = item_text;

  /* If we have text that looks like * ITEM: (FILE)NODE...,
     extract just FILE.  Otherwise return "(none)".  */

  if (*p == '*')
    p++;
  while (*p == ' ')
    p++;

  /* Skip to and past the colon.  */
  while (*p && *p != '\n' && *p != ':') p++;
  if (*p == ':') p++;

  /* Skip past the open-paren.  */
  while (1)
    {
      if (*p == '(')
        break;
      else if (*p == ' ' || *p == '\t')
        p++;
      else
        return "(none)";
    }
  p++;

  item_text = p;

  /* File name ends just before the close-paren.  */
  while (*p && *p != '\n' && *p != ')') p++;
  if (*p != ')')
    return "(none)";

  return copy_string (item_text, p - item_text);
}



/* Return FNAME with any [.info][.gz] suffix removed.  */

static char *
strip_info_suffix (char *fname)
{
  char *ret = xstrdup (fname);
  unsigned len = strlen (ret);

  if (len > 3 && FILENAME_CMP (ret + len - 3, ".gz") == 0)
    {
      len -= 3;
      ret[len] = 0;
    }
  else if (len > 4 && FILENAME_CMP (ret + len - 4, ".bz2") == 0)
    {
      len -= 4;
      ret[len] = 0;
    }
  else if (len > 5 && FILENAME_CMP (ret + len - 5, ".lzma") == 0)
   {
      len -= 5;
      ret[len] =0;
   }

  if (len > 5 && FILENAME_CMP (ret + len - 5, ".info") == 0)
    {
      len -= 5;
      ret[len] = 0;
    }
  else if (len > 4 && FILENAME_CMP (ret + len - 4, ".inf") == 0)
    {
      len -= 4;
      ret[len] = 0;
    }
#ifdef __MSDOS__
  else if (len > 4 && (FILENAME_CMP (ret + len - 4, ".inz") == 0
                       || FILENAME_CMP (ret + len - 4, ".igz") == 0))
    {
      len -= 4;
      ret[len] = 0;
    }
#endif /* __MSDOS__ */

  return ret;
}


/* Return true if ITEM matches NAME and is followed by TERM_CHAR.  ITEM
   can also be followed by `.gz', `.info.gz', or `.info' (and then
   TERM_CHAR) and still match.  */

static int
menu_item_equal (const char *item, char term_char, const char *name)
{
  int ret;
  const char *item_basename = item;
  unsigned name_len = strlen (name);

  /* We must compare the basename in ITEM, since we are passed the
     basename of the original info file.  Otherwise, a new entry like
     "lilypond/lilypond" won't match "lilypond".
     
     Actually, it seems to me that we should really compare the whole
     name, and not just the basename.  Couldn't there be dir1/foo.info
     and dir2/foo.info?  Also, it seems like we should be using the
     filename from the new dir entries, not the filename on the command
     line.  Not worrying about those things right now, though.  --karl,
     26mar04.  */
  if (!remove_exactly) {
  while (*item_basename && !IS_SLASH (*item_basename)
         && *item_basename != term_char)
    item_basename++;
  if (! *item_basename || *item_basename == term_char)
    item_basename = item;  /* no /, use original */
  else
    item_basename++;       /* have /, move past it */
  }
    
  /* First, ITEM must actually match NAME (usually it won't).  */
  ret = mbsncasecmp (item_basename, name, name_len) == 0;
  if (ret)
    {
      /* Then, `foobar' doesn't match `foo', so be sure we've got all of
         ITEM.  The various suffixes should never actually appear in the
         dir file, but sometimes people put them in.  */
      static char *suffixes[]
        = { "", ".info.gz", ".info", ".inf", ".gz",
#ifdef __MSDOS__
            ".inz", ".igz",
#endif
            NULL };
      unsigned i;
      ret = 0;
      for (i = 0; !ret && suffixes[i]; i++)
        {
          char *suffix = suffixes[i];
          unsigned suffix_len = strlen (suffix);
          ret = mbsncasecmp (item_basename + name_len, suffix, suffix_len) == 0
                && item_basename[name_len + suffix_len] == term_char;
        }
    }

  return ret;
}



void
suggest_asking_for_help (void)
{
  fprintf (stderr, _("\tTry `%s --help' for a complete list of options.\n"),
           progname);
  xexit (1);
}

void
print_help (void)
{
  printf (_("Usage: %s [OPTION]... [INFO-FILE [DIR-FILE]]\n"), progname);
  puts ("");
  puts (_("Add or remove entries in INFO-FILE from the Info directory DIR-FILE."));
  puts ("");

  puts (_("\
Options:\n\
 --debug             report what is being done.\n\
 --delete            delete existing entries for INFO-FILE from DIR-FILE;\n\
                      don't insert any new entries.\n\
 --description=TEXT  the description of the entry is TEXT; used with\n\
                      the --name option to become synonymous with the\n\
                      --entry option.\n\
 --dir-file=NAME     specify file name of Info directory file;\n\
                      equivalent to using the DIR-FILE argument.\n\
 --dry-run           same as --test."));

  puts (_("\
 --entry=TEXT        insert TEXT as an Info directory entry.\n\
                      TEXT is written as an Info menu item line followed\n\
                       by zero or more extra lines starting with whitespace.\n\
                      If you specify more than one entry, all are added.\n\
                      If you don't specify any entries, they are determined\n\
                       from information in the Info file itself.\n\
                      When removing, TEXT specifies the entry to remove.\n\
                      TEXT is only removed as a last resort, if the\n\
                      entry as determined from the Info file is not present,\n\
                      and the basename of the Info file isn't found either."));

  puts (_("\
 --help              display this help and exit.\n\
 --info-dir=DIR      same as --dir-file=DIR/dir.\n\
 --info-file=FILE    specify Info file to install in the directory;\n\
                      equivalent to using the INFO-FILE argument.\n\
 --item=TEXT         same as --entry=TEXT.\n\
 --keep-old          do not replace entries, or remove empty sections.\n\
 --menuentry=TEXT    same as --name=TEXT.\n\
 --name=TEXT         the name of the entry is TEXT; used with --description\n\
                      to become synonymous with the --entry option.\n\
 --no-indent         do not format new entries in the DIR file.\n\
 --quiet             suppress warnings."));

  puts (_("\
 --regex=R           put this file's entries in all sections that match the\n\
                      regular expression R (ignoring case).\n\
 --remove            same as --delete.\n\
 --remove-exactly    only remove if the info file name matches exactly;\n\
                      suffixes such as .info and .gz are not ignored.\n\
 --section=SEC       put entries in section SEC of the directory.\n\
                      If you specify more than one section, all the entries\n\
                       are added in each of the sections.\n\
                      If you don't specify any sections, they are determined\n\
                       from information in the Info file itself.\n\
 --section R SEC     equivalent to --regex=R --section=SEC --add-once."));

  puts (_("\
 --silent            suppress warnings.\n\
 --test              suppress updating of DIR-FILE.\n\
 --version           display version information and exit."));

  puts ("");
  
  puts (_("\
Email bug reports to bug-texinfo@gnu.org,\n\
general questions and discussion to help-texinfo@gnu.org.\n\
Texinfo home page: http://www.gnu.org/software/texinfo/"));
}


/* If DIRFILE does not exist, and we are not in test mode, create a
   minimal one (or abort).  If it already exists, do nothing.  */

static void
ensure_dirfile_exists (char *dirfile)
{
  int desc;
  
  if (chicken_flag)
    return;
    
  desc = open (dirfile, O_RDONLY);
  if (desc < 0 && errno == ENOENT)
    {
      FILE *f;
      char *readerr = strerror (errno);
      close (desc);
      f = fopen (dirfile, "w");
      if (f)
        {
          fprintf (f, _("This is the file .../info/dir, which contains the\n\
topmost node of the Info hierarchy, called (dir)Top.\n\
The first time you invoke Info you start off looking at this node.\n\
\x1f\n\
%s\tThis is the top of the INFO tree\n\
\n\
  This (the Directory node) gives a menu of major topics.\n\
  Typing \"q\" exits, \"?\" lists all Info commands, \"d\" returns here,\n\
  \"h\" gives a primer for first-timers,\n\
  \"mEmacs<Return>\" visits the Emacs manual, etc.\n\
\n\
  In Emacs, you can click mouse button 2 on a menu item or cross reference\n\
  to select it.\n\
\n\
%s\n\
"), "File: dir,\tNode: Top",  /* These keywords must not be translated.  */
    "* Menu:"
);
          if (fclose (f) < 0)
            pfatal_with_name (dirfile);
        }
      else
        {
          /* Didn't exist, but couldn't open for writing.  */
          fprintf (stderr,
                   _("%s: could not read (%s) and could not create (%s)\n"),
                   dirfile, readerr, strerror (errno));
          xexit (1);
        }
    }
  else
    close (desc); /* It already existed, so fine.  */
}

/* Open FILENAME and return the resulting stream pointer.  If it doesn't
   exist, try FILENAME.gz.  If that doesn't exist either, call
   CREATE_CALLBACK (with FILENAME as arg) to create it, if that is
   non-NULL.  If still no luck, fatal error.

   If we do open it, return the actual name of the file opened in
   OPENED_FILENAME and the compress program to use to (de)compress it in
   COMPRESSION_PROGRAM.  The compression program is determined by the
   magic number, not the filename.  */

FILE *
open_possibly_compressed_file (char *filename,
    void (*create_callback) (char *),
    char **opened_filename, char **compression_program, int *is_pipe) 
{
  char *local_opened_filename, *local_compression_program;
  int nread;
  char data[13];
  FILE *f;

  /* We let them pass NULL if they don't want this info, but it's easier
     to always determine it.  */
  if (!opened_filename)
    opened_filename = &local_opened_filename;

  *opened_filename = filename;
  f = fopen (*opened_filename, FOPEN_RBIN);
  if (!f)
    {
      *opened_filename = concat (filename, ".gz", "");
      f = fopen (*opened_filename, FOPEN_RBIN);
  if (!f)
    {
      free (*opened_filename);
      *opened_filename = concat (filename, ".bz2", "");
      f = fopen (*opened_filename, FOPEN_RBIN);
    }
  if (!f)
    {
     free (*opened_filename);
     *opened_filename = concat (filename, ".lzma", "");
     f = fopen (*opened_filename, FOPEN_RBIN);
    }

#ifdef __MSDOS__
      if (!f)
        {
          free (*opened_filename);
          *opened_filename = concat (filename, ".igz", "");
          f = fopen (*opened_filename, FOPEN_RBIN);
        }
      if (!f)
        {
          free (*opened_filename);
          *opened_filename = concat (filename, ".inz", "");
          f = fopen (*opened_filename, FOPEN_RBIN);
        }
#endif
      if (!f)
        {
          if (create_callback)
            { /* That didn't work either.  Create the file if we can.  */
              (*create_callback) (filename);

              /* And try opening it again.  */
              free (*opened_filename);
              *opened_filename = filename;
              f = fopen (*opened_filename, FOPEN_RBIN);
              if (!f)
                pfatal_with_name (filename);
            }
          else
            pfatal_with_name (filename);
        }
    }

  /* Read first few bytes of file rather than relying on the filename.
     If the file is shorter than this it can't be usable anyway.  */
  nread = fread (data, sizeof (data), 1, f);
  if (nread != 1)
    {
      /* Empty files don't set errno, so we get something like
         "install-info: No error for foo", which is confusing.  */
      if (nread == 0)
        fatal (_("%s: empty file"), *opened_filename, 0);
      pfatal_with_name (*opened_filename);
    }

  if (!compression_program)
    compression_program = &local_compression_program;

  if (data[0] == '\x1f' && data[1] == '\x8b')
#if STRIP_DOT_EXE
    /* An explicit .exe yields a better diagnostics from popen below
       if they don't have gzip installed.  */
    *compression_program = "gzip.exe";
#else
    *compression_program = "gzip";
#endif
  else if (data[0] == 'B' && data[1] == 'Z' && data[2] == 'h')
#ifndef STRIP_DOT_EXE
    *compression_program = "bzip2.exe";
#else
    *compression_program = "bzip2";
#endif
  else if (data[0] == 'B' && data[1] == 'Z' && data[2] == '0')
#ifndef STRIP_DOT_EXE
    *compression_program = "bzip.exe";
#else
    *compression_program = "bzip";
#endif
    /* We (try to) match against old lzma format (which lacks proper
       header, two first matches), as well as the new format (last match).  */
  else if ((data[9] == 0x00 && data[10] == 0x00 && data[11] == 0x00
            && data[12] == 0x00)
           || (data[5] == '\xFF' && data[6] == '\xFF' && data[7] == '\xFF'
               && data[8] == '\xFF' && data[9] == '\xFF' && data[10] == '\xFF'
               && data[11] == '\xFF' && data[12] == '\xFF') 
           || (data[0] == '\xFF' && data[1] == 'L' && data[2] == 'Z'
               && data[3] == 'M' && data[4] == 'A' && data[5] == 0x00))
#ifndef STRIP_DOT_EXE
    *compression_program = "lzma.exe";
#else
    *compression_program = "lzma";
#endif
  else
    *compression_program = NULL;

  if (*compression_program)
    { /* It's compressed, so fclose the file and then open a pipe.  */
      char *command = concat (*compression_program," -cd <", *opened_filename);
      if (fclose (f) < 0)
        pfatal_with_name (*opened_filename);
      f = popen (command, "r");
      if (f)
        *is_pipe = 1;
      else
        pfatal_with_name (command);
    }
  else
    { /* It's a plain file, seek back over the magic bytes.  */
      if (fseek (f, 0, 0) < 0)
        pfatal_with_name (*opened_filename);
#if O_BINARY
      /* Since this is a text file, and we opened it in binary mode,
         switch back to text mode.  */
      f = freopen (*opened_filename, "r", f);
#endif
      *is_pipe = 0;
    }

  return f;
}

/* Read all of file FILENAME into memory and return the address of the
   data.  Store the size of the data into SIZEP.  If need be, uncompress
   (i.e., try FILENAME.gz et al. if FILENAME does not exist) and store
   the actual file name that was opened into OPENED_FILENAME (if it is
   non-NULL), and the companion compression program (if any, else NULL)
   into COMPRESSION_PROGRAM (if that is non-NULL).  If trouble, do
   a fatal error.  */

char *
readfile (char *filename, int *sizep,
    void (*create_callback) (char *), char **opened_filename,
    char **compression_program)
{
  char *real_name;
  FILE *f;
  int pipe_p;
  int filled = 0;
  int data_size = 8192;
  char *data = xmalloc (data_size);

  /* If they passed the space for the file name to return, use it.  */
  f = open_possibly_compressed_file (filename, create_callback,
                                     opened_filename ? opened_filename
                                                     : &real_name,
                                     compression_program, &pipe_p);

  for (;;)
    {
      int nread = fread (data + filled, 1, data_size - filled, f);
      if (nread < 0)
        pfatal_with_name (real_name);
      if (nread == 0)
        break;

      filled += nread;
      if (filled == data_size)
        {
          data_size += 65536;
          data = xrealloc (data, data_size);
        }
    }

  /* We'll end up wasting space if we're not passing the filename back
     and it is not just FILENAME, but so what.  */
  /* We need to close the stream, since on some systems the pipe created
     by popen is simulated by a temporary file which only gets removed
     inside pclose.  */
  if (pipe_p)
    pclose (f);
  else
    fclose (f);

  *sizep = filled;
  return data;
}

/* Output the old dir file, interpolating the new sections
   and/or new entries where appropriate.  If COMPRESSION_PROGRAM is not
   null, pipe to it to create DIRFILE.  Thus if we read dir.gz on input,
   we'll write dir.gz on output.  */

static void
output_dirfile (char *dirfile, int dir_nlines, struct line_data *dir_lines,
                int n_entries_to_add, struct spec_entry *entries_to_add,
                struct spec_section *input_sections, char *compression_program)
{
  int n_entries_added = 0;
  int i;
  FILE *output;

  if (compression_program)
    {
      char *command = concat (compression_program, ">", dirfile);
      output = popen (command, "w");
    }
  else
    output = fopen (dirfile, "w");

  if (!output)
    {
      perror (dirfile);
      xexit (1);
    }

  for (i = 0; i <= dir_nlines; i++)
    {
      int j;

      /* If we decided to output some new entries before this line,
         output them now.  */
      if (dir_lines[i].add_entries_before)
        for (j = 0; j < n_entries_to_add; j++)
          {
            struct spec_entry *this = dir_lines[i].add_entries_before[j];
            if (this == 0)
              break;
            if (n_entries_added >= 1 && 
                !add_entries_into_all_matching_sections)
              break;
            fputs (this->text, output);
            n_entries_added++;
          }
      /* If we decided to add some sections here
         because there are no such sections in the file,
         output them now.  
         FIXME:  we add all sections here, but they should
         be interspersed throughout the DIR file in 
         alphabetic order. */
      if (dir_lines[i].add_sections_before)
        {
          struct spec_section *spec;
          struct spec_entry *entry;
          struct spec_entry **entries;
          int n_entries = 0;

          /* If we specified --add-once, and we've added an entry, then
             it's time to bail. */
          if (n_entries_added >= 1 && 
              !add_entries_into_all_matching_sections)
            break;

          qsort (dir_lines[i].add_sections_before, 
                 dir_lines[i].num_sections_to_add, 
                 sizeof (struct spec_section *), compare_section_names);

          /* Count the entries and allocate a vector for all of them.  */
          for (entry = entries_to_add; entry; entry = entry->next)
            n_entries++;
          entries = ((struct spec_entry **)
                     xmalloc (n_entries * sizeof (struct spec_entry *)));

          /* Fill the vector ENTRIES with pointers to all the sections,
             and sort them.  */
          j = 0;
          for (entry = entries_to_add; entry; entry = entry->next)
            entries[j++] = entry;
          qsort (entries, n_entries, sizeof (struct spec_entry *),
                 compare_entries_text);

          /* Generate the new sections in alphabetical order.  In each
             new section, output all of the entries that belong to that
             section, in alphabetical order.  */
          for (j = 0; j < dir_lines[i].num_sections_to_add; j++)
            {
              spec = dir_lines[i].add_sections_before[j];
              if (spec->missing)
                {
                  int k;

                  putc ('\n', output);
                  fputs (spec->name, output);
                  putc ('\n', output);
                  spec->missing = 0;
                  for (k = 0; k < n_entries; k++)
                    {
                      struct spec_section *spec1;
                      /* Did they at all want this entry to be put into
                         this section?  */
                      entry = entries[k];
                      for (spec1 = entry->entry_sections;
                           spec1 && spec1 != entry->entry_sections_tail;
                           spec1 = spec1->next)
                        {
                          if (!strcmp (spec1->name, spec->name))
                            break;
                        }
                      if (spec1 && spec1 != entry->entry_sections_tail)
                        fputs (entry->text, output);
                    }
                }
            }

          n_entries_added++;
          free (entries);
        }

      /* Output the original dir lines unless marked for deletion.  */
      if (i < dir_nlines && !dir_lines[i].delete)
        {
          fwrite (dir_lines[i].start, 1, dir_lines[i].size, output);
          putc ('\n', output);
        }
    }

  /* Some systems, such as MS-DOS, simulate pipes with temporary files.
     On those systems, the compressor actually gets run inside pclose,
     so we must call pclose.  */
  if (compression_program)
    pclose (output);
  else
    fclose (output);
}

/* Parse the input to find the section names and the entry names it
   specifies.  Return the number of entries to add from this file.  */
int
parse_input (const struct line_data *lines, int nlines,
             struct spec_section **sections, struct spec_entry **entries,
             int delete_flag) 
{
  int n_entries = 0;
  int prefix_length = strlen ("INFO-DIR-SECTION ");
  struct spec_section *head = *sections, *tail = NULL;
  int reset_tail = 0;
  char *start_of_this_entry = 0;
  int ignore_sections = *sections != 0;
  int ignore_entries  = delete_flag ? 0: *entries  != 0;

  int i;

  if (ignore_sections && ignore_entries)
    return 0;

  /* Loop here processing lines from the input file.  Each
     INFO-DIR-SECTION entry is added to the SECTIONS linked list.
     Each START-INFO-DIR-ENTRY block is added to the ENTRIES linked
     list, and all its entries inherit the chain of SECTION entries
     defined by the last group of INFO-DIR-SECTION entries we have
     seen until that point.  */
  for (i = 0; i < nlines; i++)
    {
      if (!ignore_sections
          && !strncmp ("INFO-DIR-SECTION ", lines[i].start, prefix_length))
        {
          struct spec_section *next
            = (struct spec_section *) xmalloc (sizeof (struct spec_section));
          next->name = copy_string (lines[i].start + prefix_length,
                                    lines[i].size - prefix_length);
          next->next = *sections;
          next->missing = 1;
          if (reset_tail)
            {
              tail = *sections;
              reset_tail = 0;
            }
          *sections = next;
          head = *sections;
        }
      /* If entries were specified explicitly with command options,
         ignore the entries in the input file.  */
      else if (!ignore_entries)
        {
          if (!strncmp ("START-INFO-DIR-ENTRY", lines[i].start, lines[i].size)
              && sizeof ("START-INFO-DIR-ENTRY") - 1 == lines[i].size)
            {
              if (!*sections)
                {
                  /* We found an entry, but didn't yet see any sections
                     specified.  Default to section "Miscellaneous".  */
                  *sections = (struct spec_section *)
                    xmalloc (sizeof (struct spec_section));
                  (*sections)->name = "Miscellaneous";
                  (*sections)->next = 0;
                  (*sections)->missing = 1;
                  head = *sections;
                }
              /* Next time we see INFO-DIR-SECTION, we will reset the
                 tail pointer.  */
              reset_tail = 1;

              if (start_of_this_entry != 0)
                fatal (_("START-INFO-DIR-ENTRY without matching END-INFO-DIR-ENTRY"), 0, 0);
              start_of_this_entry = lines[i + 1].start;
            }
          else if (start_of_this_entry)
            {
              if ((!strncmp ("* ", lines[i].start, 2)
                   && lines[i].start > start_of_this_entry)
                  || (!strncmp ("END-INFO-DIR-ENTRY",
                                lines[i].start, lines[i].size)
                      && sizeof ("END-INFO-DIR-ENTRY") - 1 == lines[i].size))
                {
                  /* We found an end of this entry.  Allocate another
                     entry, fill its data, and add it to the linked
                     list.  */
                  struct spec_entry *next
                    = (struct spec_entry *) xmalloc (sizeof (struct spec_entry));
                  next->text
                    = copy_string (start_of_this_entry,
                                   lines[i].start - start_of_this_entry);
                  next->text_len = lines[i].start - start_of_this_entry;
                  next->entry_sections = head;
                  next->entry_sections_tail = tail;
                  next->next = *entries;
                  *entries = next;
                  n_entries++;
                  if (!strncmp ("END-INFO-DIR-ENTRY",
                                lines[i].start, lines[i].size)
                      && sizeof ("END-INFO-DIR-ENTRY") - 1 == lines[i].size)
                    start_of_this_entry = 0;
                  else
                    start_of_this_entry = lines[i].start;
                }
              else if (!strncmp ("END-INFO-DIR-ENTRY",
                                 lines[i].start, lines[i].size)
                       && sizeof ("END-INFO-DIR-ENTRY") - 1 == lines[i].size)
                fatal (_("END-INFO-DIR-ENTRY without matching START-INFO-DIR-ENTRY"), 0, 0);
            }
        }
    }
  if (start_of_this_entry != 0)
    fatal (_("START-INFO-DIR-ENTRY without matching END-INFO-DIR-ENTRY"),
           0, 0);

  /* If we ignored the INFO-DIR-ENTRY directives, we need now go back
     and plug the names of all the sections we found into every
     element of the ENTRIES list.  */
  if (ignore_entries && *entries)
    {
      struct spec_entry *entry;

      for (entry = *entries; entry; entry = entry->next)
        {
          entry->entry_sections = head;
          entry->entry_sections_tail = tail;
        }
    }

  return n_entries;
}


/* Parse the dir file whose basename is BASE_NAME.  Find all the
   nodes, and their menus, and the sections of their menus.  */
static void
parse_dir_file (struct line_data *lines, int nlines, struct node **nodes)
{
  int node_header_flag = 0;
  int i;

  *nodes = 0;
  for (i = 0; i < nlines; i++)
    {
      /* Parse node header lines.  */
      if (node_header_flag)
        {
          int j, end;
          for (j = 0; j < lines[i].size; j++)
            /* Find the node name and store it in the `struct node'.  */
            if (!strncmp ("Node:", lines[i].start + j, 5))
              {
                char *line = lines[i].start;
                /* Find the start of the node name.  */
                j += 5;
                while (line[j] == ' ' || line[j] == '\t')
                  j++;
                /* Find the end of the node name.  */
                end = j;
                while (line[end] != 0 && line[end] != ',' && line[end] != '\n'
                       && line[end] != '\t')
                  end++;
                (*nodes)->name = copy_string (line + j, end - j);
              }
          node_header_flag = 0;
        }

      /* Notice the start of a node.  */
      if (*lines[i].start == 037)
        {
          struct node *next = (struct node *) xmalloc (sizeof (struct node));

          next->next = *nodes;
          next->name = NULL;
          next->start_line = i;
          next->end_line = 0;
          next->menu_start = NULL;
          next->sections = NULL;
          next->last_section = NULL;

          if (*nodes != 0)
            (*nodes)->end_line = i;
          /* Fill in the end of the last menu section
             of the previous node.  */
          if (*nodes != 0 && (*nodes)->last_section != 0)
            (*nodes)->last_section->end_line = i;

          *nodes = next;

          /* The following line is the header of this node;
             parse it.  */
          node_header_flag = 1;
        }

      /* Notice the lines that start menus.  */
      if (*nodes != 0 && !strncmp ("* Menu:", lines[i].start, 7))
        (*nodes)->menu_start = lines[i + 1].start;

      /* Notice sections in menus.  */
      if (*nodes != 0
          && (*nodes)->menu_start != 0
          && *lines[i].start != '\n'
          && *lines[i].start != '*'
          && *lines[i].start != ' '
          && *lines[i].start != '\t')
        {
          /* Add this menu section to the node's list.
             This list grows in forward order.  */
          struct menu_section *next
            = (struct menu_section *) xmalloc (sizeof (struct menu_section));

          next->start_line = i + 1;
          next->next = 0;
          next->end_line = 0;
          next->name = copy_string (lines[i].start, lines[i].size);
          if ((*nodes)->sections)
            {
              (*nodes)->last_section->next = next;
              (*nodes)->last_section->end_line = i;
            }
          else
            (*nodes)->sections = next;
          (*nodes)->last_section = next;
        }

    }

  /* Finish the info about the end of the last node.  */
  if (*nodes != 0)
    {
      (*nodes)->end_line = nlines;
      if ((*nodes)->last_section != 0)
        (*nodes)->last_section->end_line = nlines;
    }
}


/* Iterate through NLINES LINES looking for an entry that has a name
   that matches NAME.  If such an entry is found, flag the entry for 
   deletion later on. */

int
mark_entry_for_deletion (struct line_data *lines, int nlines, char *name)
{
  int something_deleted = 0;
  int i;
  for (i = 0; i < nlines; i++)
    {
      /* Check for an existing entry that should be deleted.
         Delete all entries which specify this file name.  */
      if (*lines[i].start == '*')
        {
          char *q;
          char *p = lines[i].start;

          p++; /* skip * */
          while (*p == ' ') p++; /* ignore following spaces */
          q = p; /* remember this, it's the beginning of the menu item.  */

          /* Read menu item.  */
          while (*p != 0 && *p != ':')
            p++;
          p++; /* skip : */

          if (*p == ':')
            { /* XEmacs-style entry, as in * Mew::Messaging.  */
              if (menu_item_equal (q, ':', name))
                {
                  lines[i].delete = 1;
                  something_deleted = 1;
                }
            }
          else
            { /* Emacs-style entry, as in * Emacs: (emacs).  */
              while (*p == ' ') p++; /* skip spaces after : */
              if (*p == '(')         /* if at parenthesized (FILENAME) */
                {
                  p++;
                  if (menu_item_equal (p, ')', name))
                    {
                      lines[i].delete = 1;
                      something_deleted = 1;
                    }
                }
            }
        }

      /* Treat lines that start with whitespace
         as continuations; if we are deleting an entry,
         delete all its continuations as well.  */
      else if (i > 0 && (*lines[i].start == ' ' || *lines[i].start == '\t'))
        {
          lines[i].delete = lines[i - 1].delete;
        }
    }
  return something_deleted;
}


/* Assuming the current column is COLUMN, return the column that
   printing C will move the cursor to.
   The first column is 0.
   This function is used to assist in indenting of entries. */

static size_t
adjust_column (size_t column, char c)
{
  if (c == '\b')
    {
      if (column > 0)
        column--;
    }
  else if (c == '\r')
    column = 0;
  else if (c == '\t')
    column += TAB_WIDTH - column % TAB_WIDTH;
  else                          /* if (isprint (c)) */
    column++;
  return column;
}

/* Indent the Info entry's NAME and DESCRIPTION.  Lines are wrapped at the
   WIDTH column.  The description on first line is indented at the CALIGN-th 
   column, and all subsequent lines are indented at the ALIGN-th column.  
   The resulting Info entry is put into OUTSTR.
   NAME is of the form "* TEXT (TEXT)[:TEXT].".
 */
static int
format_entry (char *name, size_t name_len, char *desc, size_t desc_len, 
              int calign, int align, size_t width, 
              char **outstr, size_t *outstr_len)
{
  int i, j;
  char c;
  size_t column = 0;            /* Screen column where next char will go */
  size_t offset_out = 0;        /* Index in `line_out' for next char. */
  static char *line_out = NULL;
  static size_t allocated_out = 0;
  int saved_errno;
  if (!desc || !name)
    return 1;

  *outstr = malloc (width  + 
                    (((desc_len  + width) / (width - align)) * width) * 2 
                    * sizeof (char));
  *outstr[0] = '\0';

  strncat (*outstr, name, name_len);

  column = name_len;

  if (name_len > calign - 2)
    {
      /* Name is too long to have description on the same line. */
      if (desc_len > 1)
        {
          strncat (*outstr, "\n", 1);
          column = 0;
          for (j = 0; j < calign - 1; j++)
            {
              column = adjust_column (column, ' ');
              strncat (*outstr, " ", 1);
            }
        }
    }
  else
    for (j = 0; j < calign - name_len - 1; j++)
      {
        if (desc_len <= 2)
          break;
        column = adjust_column (column, ' ');
        strncat (*outstr, " ", 1);
      }

  for (i = 0; i < desc_len; i++)
    {
      if (desc_len <= 2)
        break;
      c = desc[i];
      if (offset_out + 1 >= allocated_out)
        {
          allocated_out = offset_out + 1;
          line_out = (char *) realloc ((void *)line_out, allocated_out);
        }

      if (c == '\n')
        {
          line_out[offset_out++] = c;
          strncat (*outstr, line_out, offset_out);
          column = offset_out = 0;
          continue;
        }

    rescan:
      column = adjust_column (column, c);

      if (column > width)
        {
          /* This character would make the line too long.
             Print the line plus a newline, and make this character
             start the next line. */

          int found_blank = 0;
          size_t logical_end = offset_out;

          /* Look for the last blank. */
          while (logical_end)
            {
              --logical_end;
              if (line_out[logical_end] == ' '
                  || line_out[logical_end] == '\t')
                {
                  found_blank = 1;
                  break;
                }
            }

          if (found_blank)
            {
              size_t i;

              /* Found a blank.  Don't output the part after it. */
              logical_end++;
              strncat (*outstr, line_out, logical_end);
              strncat (*outstr, "\n", 1);
              for (j = 0; j < align - 1; j++)
                {
                  column = adjust_column (column, ' ');
                  strncat (*outstr, " ", 1);
                }

              /* Move the remainder to the beginning of the next 
                 line.
                 The areas being copied here might overlap. */
              memmove (line_out, line_out + logical_end,
                       offset_out - logical_end);
              offset_out -= logical_end;
              for (column = i = 0; i < offset_out; i++)
                column = adjust_column (column, line_out[i]);
              goto rescan;
            }

          if (offset_out == 0)
            {
              line_out[offset_out++] = c;
              continue;
            }

          line_out[offset_out++] = '\n';
          strncat (*outstr, line_out, offset_out);
          column = offset_out = 0;
          goto rescan;
        }

      line_out[offset_out++] = c;
    }

  saved_errno = errno;

  if (desc_len <= 2)
    strncat (*outstr, "\n", 1);

  if (offset_out)
    strncat (*outstr, line_out, offset_out);

  *outstr_len = strlen (*outstr);
  return 1;
}


/* Extract the NAME and DESCRIPTION from ENTRY.  NAME and DESCRIPTION must be
   free'd.
 */
static void
split_entry (const char *entry, char **name, size_t *name_len,
             char **description, size_t *description_len)
{
  char *endptr;

  /* on the first line, the description starts after the first ". ";
     that's a period and space -- our heuristic to handle item names like
     "config.status", and node names like "config.status Invocation".
     Also accept period-tab and period-newline.  */
  char *ptr = strchr (entry, '.');
  while (ptr && ptr[1] != ' ' && ptr[1] != '\t' && ptr[1] != '\n') {
    ptr = strchr (ptr + 1, '.');
  }
  
  /* Maybe there's no period, and no description */
  if (!ptr)
    {
      size_t length = strlen (entry);
      if (length == 0)
        return;
      *name = strdup (entry);
      *name_len = length + 1;
      return;
    }

  /* The name is everything up to and including the period. */
  *name_len = (size_t) (ptr - entry + 1);
  *name = xmalloc (*name_len + 1);
  (*name)[0] = '\0';
  strncat (*name, entry, *name_len);

  ptr++;
  *description = xmalloc (strlen (entry));
  (*description)[0] = '\0';

  while (ptr[0] != '\0')
    {
      /* Eat up the whitespace after the name, and at the start of a line. */
      while (isspace(ptr[0]))
        ptr++;

      /* Okay, we're at the start of the description. */
      if (ptr[0] == '\0')
        continue;

      /* See how far the description goes... */
      endptr = strchr (ptr, '\n');
      /* Either the description continues up to the next newline. */
      if (endptr)
        {
          size_t length  = (size_t) (endptr - ptr) / sizeof (char);
          strncat (*description, ptr, length);
          ptr = endptr;
          /* First of all, we eat the newline here.  But then what?
             Sometimes the newline separates 2 sentences, so we
             end up with the next word starting directly after the period,
             instead of after the customary 2 spaces in english. 
             If the previous character was a `.', then we should add 2
             spaces if there is anything on the next line.
             if it's a comma, then we should put one space.
             If it's neither, we just put a space.
             If it's some other whitespace, we shouldn't do anything. */
          ptr++;
          if (length > 1 && strlen (ptr) > 0)
            {
              endptr--;
              /* *ENDPTR is the 2nd last character */
              if (*endptr == '.')
                strncat (*description, "  ", 2);
              else if (!isspace (*endptr))
                strncat (*description, " ", 1);
            }
        }
      /* Or the description continues to the end of the string. */
      else
        {
          /* Just show the rest when there's no newline. */
          size_t length = strlen (ptr);
          strncat (*description, ptr, length);
          ptr += length;
        }
    }
  /* Descriptions end in a new line. */
  strncat (*description, "\n", 1);
  *description_len = strlen (*description);
}


/* Indent all ENTRIES according to some formatting options. 
   CALIGN_CLI is the starting column for the first line of the description.
   ALIGN_CLI is the starting column for all subsequent lines of the 
   description.  MAXWIDTH_CLI is the number of columns in the line. 
   When CALIGN_CLI, ALIGN_CLI, or MAXWIDTH_CLI is -1, choose a sane default. */

static void
reformat_new_entries (struct spec_entry *entries, int calign_cli, int align_cli, 
                      int maxwidth_cli)
{
  struct spec_entry *entry;
  for (entry = entries; entry; entry = entry->next)
    {
      int calign = -1, align = -1, maxwidth = -1;
      char *name = NULL, *desc = NULL;
      size_t name_len = 0, desc_len = 0;
      split_entry (entry->text, &name, &name_len, &desc, &desc_len);
      free (entry->text);

      /* Specify sane defaults if we need to */
      if (calign_cli == -1 || align_cli == -1)
        {
          struct spec_section *section;
          calign = calign_cli;
          align = align_cli;
          for (section = entry->entry_sections; 
               section && section != entry->entry_sections_tail;
               section = section->next)
            {
              if (!strcmp (section->name, "Individual utilities"))
                {
                  if (calign == -1)
                    calign = 48 + 1;
                  if (align == -1)
                    align = 50 + 1;
                  break;
                }
            }
          if (calign == -1)
            calign = 32 + 1;
          if (align == -1)
            align = 34 + 1;
        }
      else
        {
          calign = calign_cli;
          align = align_cli;
        }

      if (maxwidth_cli == -1)
        maxwidth = 79;

      format_entry (name, name_len, desc, desc_len, calign, align, 
                    maxwidth, &entry->text, &entry->text_len);
    }
}

/* Insert NAME into every entry in ENTRIES that requires it. 
   NAME is the basename of the Info file being installed. 
   The idea here is that there was a --name on the command-line
   and we need to put the basename in the empty parentheses. */
void
add_missing_basenames (struct spec_entry *entries, char *name)
{
  struct spec_entry *entry;
  for (entry = entries; entry; entry = entry->next)
    {
      if (entry->missing_basename)
        {
          /* Insert NAME into the right place in ENTRY->TEXT. */
          char *info, *rest, *text;
          size_t name_len = strlen (name);
          char *ptr = strstr (entry->text, ": (). ");
          if (!ptr)
            return;
          ptr[0] = '\0';
          rest = ptr += strlen (": (). ");

          info = xmalloc (name_len + 7);
          snprintf (info, name_len + 7, ": (%s). ", name);
          text = concat (entry->text, info, rest);
          free (info);
          if (entry->text)
            free (entry->text);
          entry->text = text;
          entry->text_len = strlen (entry->text);
          entry->missing_name = 0;
          entry->missing_basename = 0;
        }
    }
}


/* Add NAME to the start of any entry in ENTRIES that is missing a name 
   component.  If NAME doesn't start with `*', it is formatted to look 
   like an Info entry.  */
void
add_missing_names (struct spec_entry *entries, char *name)
{
  struct spec_entry *entry;
  for (entry = entries; entry; entry = entry->next)
    {
      if (entry->missing_name)
        {
          char *text;
          /* Prepend NAME onto ENTRY->TEXT. */
          int add_nl = 1;
          if (entry->text)
            if (entry->text[entry->text_len - 1] == '\n')
              add_nl = 0;

          if (name[0] == '*')
            text = concat (name, entry->text == NULL ? "" : entry->text, 
                           add_nl ? "\n" : "");
          else
            {
              size_t full_name_len = strlen (name) * 2 + 9;
              char *full_name = xmalloc (full_name_len);
              snprintf (full_name, full_name_len, "* %s: (%s).", name, name);
              text = concat (full_name, 
                             entry->text == NULL ? "" : entry->text, 
                             add_nl ? "\n" : "");
              free (full_name);
            }
          if (entry->text)
            free (entry->text);
          entry->text = text;
          entry->text_len = strlen (entry->text);
          entry->missing_name = 0;
          entry->missing_basename = 0;
        }
    }
}

/* Append DESC to every entry in ENTRIES that needs it. */

void
add_missing_descriptions (struct spec_entry *entries, char *desc)
{
  struct spec_entry *entry;
  for (entry = entries; entry; entry = entry->next)
    {
      if (entry->missing_description)
        {
          char *text;
          int add_nl = 1;
          if (strlen (desc) > 1)
            if (desc[strlen (desc) - 1] == '\n')
              add_nl = 0;
          /* Append DESC onto ENTRY->TEXT. */
          text = concat (entry->text == NULL ? "" : entry->text, desc,
                               add_nl ? "\n" : "");
          if (entry->text)
            free (entry->text);
          entry->text = text;
          entry->text_len = strlen (entry->text);
        }
    }
}


/* Detect old-style Debian `--section REGEX TITLE' semantics in ARGV.
   When detected the options are munged to look like:
     `--regex REGEX --section TITLE --add-once'
   Return 1 if munging took place, return 0 if not.
   Otherwise return a negative number if something went wrong.
   NEW_ARGC, and NEW_ARGV are filled with the newly munged options
   when munging took place.
 */
static int
munge_old_style_debian_options (int argc, char **argv, 
                                int *new_argc, char ***new_argv)
{
  char *opt = NULL;
  int i, err;
  char *argz = NULL;
  size_t argz_len = 0;
  const char *regex, *title;
  int munge = 0;

  /* Flip through the options to detect the old `--section REGEX TITLE' 
     syntax */
  for (i = 0; i < argc; i++)
    {
      if (strcmp (argv[i], "--section") == 0)
        {
          FILE *fileptr;
          /* Go forward one arg and obtain the REGEX. */
          if (i + 1 < argc)
            i++;
          else
            return -1;
          regex = argv[i];
          /* Go forward another arg and obtain the TITLE. */
          if (i + 1 < argc)
            i++;
          else
            return -1;
          title = argv[i];
          /* When the title starts with a `-' it's probably an option,
             and not a title. */
          if (title[0] == '-')
            break;
          /* When the title is a filename it's probably an Info file, or
             a dir file, and not a title. */
          fileptr = fopen (title, "r");
          if (fileptr)
            {
              fclose (fileptr);
              break;
            }
          /* Okay, it looks like we're using the old debian syntax 
             for --section. */
          munge = 1;
        
          /* Okay, we munge the options to look like this:
             --regex=REGEX --section=TITLE --add-once */
          opt = xmalloc (strlen (regex) + sizeof ("--regex="));
          if (sprintf (opt, "--regex=%s", regex) == -1)
            err = 1;
          if (!err)
            err = argz_add (&argz, &argz_len, opt);
          free (opt); opt = NULL;

          opt = xmalloc (strlen (regex) + sizeof ("--section="));
          if (sprintf (opt, "--section=%s", title) == -1)
            err = 1;
          if (!err)
            err = argz_add (&argz, &argz_len, opt);
          free (opt); opt = NULL;

          if (!err)
            err = argz_add (&argz, &argz_len, "--add-once");
        }
      else
        err = argz_add (&argz, &argz_len, argv[i]); 
      if (err)
        return -1;
    }

  if (munge)
    {
      *new_argc = argz_count (argz, argz_len);
      *new_argv = xmalloc ((*new_argc + 1) * sizeof (char *));

      opt = NULL; i = 0;
      while ((opt = argz_next (argz, argz_len, opt)))
        {
          (*new_argv)[i] = xstrdup (opt);
          i++;
        }
      (*new_argv)[*new_argc] = NULL;
    }
  free (argz);
  return munge;
}


int
main (int argc, char *argv[])
{
  char *opened_dirfilename;
  char *compression_program;
  char *infile_sans_info;
  char *infile = 0, *dirfile = 0;
  int calign = -1;
  int align  = -1;
  int maxwidth = -1;

  /* Record the text of the Info file, as a sequence of characters
     and as a sequence of lines.  */
  char *input_data = NULL;
  int input_size = 0;
  struct line_data *input_lines = NULL;
  int input_nlines = 0;

  /* Record here the specified section names and directory entries.  */
  struct spec_section *input_sections = NULL;
  struct spec_entry *entries_to_add = NULL;
  struct spec_entry *entries_to_add_from_file = NULL;
  int n_entries_to_add = 0;

  /* Record the old text of the dir file, as plain characters,
     as lines, and as nodes.  */
  char *dir_data;
  int dir_size;
  int dir_nlines;
  struct line_data *dir_lines;
  struct node *dir_nodes;

  /* Nonzero means --delete was specified (just delete existing entries).  */
  int delete_flag = 0;
  int something_deleted = 0;

  /* Nonzero means -quiet/--silent was specified.  */
  int quiet_flag = 0;

  /* Nonzero means --debug was specified.  */
  int debug_flag = 0;

  int i;

#ifdef HAVE_SETLOCALE
  /* Set locale via LC_ALL.  */
  setlocale (LC_ALL, "");
#endif

  /* Set the text message domain.  */
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  munge_old_style_debian_options (argc, argv, &argc, &argv);

  while (1)
    {
      int opt = getopt_long (argc, argv, 
                             "i:d:e:s:t:E:c:C:W:A:hHrk1Ia", longopts, 0);

      if (opt == EOF)
        break;

      switch (opt)
        {
        case 0:
          /* If getopt returns 0, then it has already processed a
             long-named option.  We should do nothing.  */
          break;

        case 1:
          abort ();

        case '1':
          add_entries_into_all_matching_sections = 0;
          break;

        case 'a':
          order_new_sections_alphabetically_flag = 0;
          break;

        case 'A':
          {
            char *end = NULL;
            unsigned long int val;
            val = strtoul (optarg, &end, 0);
            if (end == NULL || end == optarg || *end != '\0')
              suggest_asking_for_help ();
            align = val;
            if (align <= 0)
              suggest_asking_for_help ();
          }
          break;

        case 'c':
        
          {
            struct spec_entry *next;
            size_t length = strlen (optarg);

            if (!entries_to_add)
              {
                next = 
                  (struct spec_entry *) xmalloc (sizeof (struct spec_entry));
            
                next->text = NULL;
                next->text_len = 0;
                next->entry_sections = NULL;
                next->entry_sections_tail = NULL;
                next->missing_name = 1;
                next->missing_basename = 1;
                next->next = entries_to_add;
                entries_to_add = next;
                n_entries_to_add++;
              }
            else
              next = entries_to_add;
                
            next->missing_description = 0;
            if (next->text)
              {
                char *nl = strrchr (next->text, '\n');
                if (nl)
                  nl[0] = '\0';
              }
            /* Concat the description onto the current entry, adding a 
               newline if we need one.  Prepend a space if we have no
               previous text, since eventually we will be adding the
               "* foo ()." and we want to end up with a ". " for parsing.  */
            next->text = concat (next->text ? next->text : " ",
                                 optarg, 
                                 optarg[length - 1] == '\n' ? "" : "\n");
            next->text_len = strlen (next->text);
          }
          break;

        case 'C':
          {
            char *end = NULL;
            unsigned long int val;
            val = strtoul (optarg, &end, 0);
            if (end == NULL || end == optarg || *end != '\0')
              suggest_asking_for_help ();
            calign = val;
            if (calign <= 0)
              suggest_asking_for_help ();
          }
          break;

        case 'd':
          if (dirfile)
            {
              fprintf (stderr, _("%s: already have dir file: %s\n"),
                       progname, dirfile);
              suggest_asking_for_help ();
            }
          dirfile = optarg;
          break;

        case 'D':
          if (dirfile)
            {
              fprintf (stderr, _("%s: already have dir file: %s\n"),
                       progname, dirfile);
              suggest_asking_for_help ();
            }
          dirfile = concat (optarg, "", "/dir");
          break;

        case 't':
          {
            struct spec_entry *next
              = (struct spec_entry *) xmalloc (sizeof (struct spec_entry));

            size_t length;
            if (optarg[0] != '*')
              {
                /* Make enough space for "* foo: (). ". */
                length = strlen (optarg) + 9;
                next->text = xmalloc (length);
                snprintf (next->text, length, "* %s: (). ", optarg);
                next->missing_basename = 1;
                /* The basename will be inserted in between the parentheses
                   at a later time.  See add_missing_basenames. */
              }
            else
              {
                /* Make enough space for "foo ". */
                length = strlen (optarg) + 2;
                next->text = xmalloc (length);
                snprintf (next->text, length, "%s ", optarg);
                next->missing_basename = 0;
                /* FIXME: check for info entry correctness in TEXT. 
                   e.g. `* Aaa: (bbb).' */
              }

            next->text_len = length - 1;
            next->entry_sections = NULL;
            next->entry_sections_tail = NULL;
            next->next = entries_to_add;
            next->missing_name = 0;
            next->missing_description = 1;
            entries_to_add = next;
            n_entries_to_add++;
          }
          break;

        case 'e':
          {
            struct spec_entry *next
              = (struct spec_entry *) xmalloc (sizeof (struct spec_entry));
            int olen = strlen (optarg);
            if (! (*optarg != 0 && optarg[olen - 1] == '\n'))
              {
                optarg = concat (optarg, "\n", "");
                olen++;
              }
            next->text = optarg;
            next->text_len = olen;
            next->entry_sections = NULL;
            next->entry_sections_tail = NULL;
            next->next = entries_to_add;
            next->missing_name = 0;
            next->missing_basename = 0;
            next->missing_description = 0;
            entries_to_add = next;
            n_entries_to_add++;
          }
          break;

        case 'g':
          debug_flag = 1;
          break;

        case 'h':
        case 'H':
          print_help ();
          xexit (0);

        case 'i':
          if (infile)
            {
              fprintf (stderr, _("%s: Specify the Info file only once.\n"),
                       progname);
              suggest_asking_for_help ();
            }
          infile = optarg;
          break;

        case 'I':
          indent_flag = 0;
          break;

        case 'k':
          keep_old_flag = 1;
          break;

        case 'n':
          chicken_flag = 1;
          break;

        case 'q':
          quiet_flag = 1;
          break;

        case 'r':
          delete_flag = 1;
          break;

        case 'R':
          {
            int error;
            if (psecreg)
              {
                warning 
                  (_("Extra regular expression specified, ignoring `%s'"),
                   optarg, 0);
                break;
              }
            psecreg = (regex_t *) xmalloc (sizeof (regex_t));

            error = regcomp (psecreg, optarg, REG_ICASE|REG_NOSUB);
            if (error != 0)
              {
                int errbuf_size = regerror (error, psecreg, NULL, 0);
                char *errbuf = (char *) xmalloc (errbuf_size);
                regerror (error, psecreg, errbuf, errbuf_size);
                fatal (_("Error in regular expression `%s': %s"),
                       optarg, errbuf);
              };
          }
          break;

        case 's':
          {
            struct spec_section *next
              = (struct spec_section *) xmalloc (sizeof (struct spec_section));
            next->name = optarg;
            next->next = input_sections;
            next->missing = 1;
            input_sections = next;
          }
          break;

        case 'V':
          printf ("install-info (GNU %s) %s\n", PACKAGE, VERSION);
          puts ("");
          printf (_("Copyright (C) %s Free Software Foundation, Inc.\n\
License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>\n\
This is free software: you are free to change and redistribute it.\n\
There is NO WARRANTY, to the extent permitted by law.\n"),
              "2008");
          xexit (0);

        case 'W':
          {
            char *end = NULL;
            unsigned long int val;
            val = strtoul (optarg, &end, 0);
            if (end == NULL || end == optarg || *end != '\0')
              suggest_asking_for_help ();
            maxwidth = val;
            if (maxwidth <= 0)
              suggest_asking_for_help ();
          }
          break;

        case 'x':
          delete_flag = 1;
          remove_exactly = 1;
          break;

        default:
          suggest_asking_for_help ();
        }
    }

  /* Interpret the non-option arguments as file names.  */
  for (; optind < argc; ++optind)
    {
      if (infile == 0)
        infile = argv[optind];
      else if (dirfile == 0)
        dirfile = argv[optind];
      else
        error (_("excess command line argument `%s'"), argv[optind], 0);
    }

  if (!infile)
    fatal (_("No input file specified; try --help for more information."),
           0, 0);
  if (!dirfile)
    fatal (_("No dir file specified; try --help for more information."), 0, 0);

  /* Now read in the Info dir file.  */
  if (debug_flag)
    printf ("debug: reading dir file %s\n", dirfile);
  dir_data = readfile (dirfile, &dir_size, ensure_dirfile_exists,
                       &opened_dirfilename, &compression_program);
  dir_lines = findlines (dir_data, dir_size, &dir_nlines);

  parse_dir_file (dir_lines, dir_nlines, &dir_nodes);

  if (!delete_flag)
    {
      /* Find which sections match our regular expression. */
      if (psecreg)
        {
          struct node *node;
          struct menu_section *section;
          for (node = dir_nodes; node ; node = node->next)
            for (section = node->sections; section ; section = section->next)
              if (regexec (psecreg, section->name, 0, NULL, 0) == 0)
                {
                  /* we have a match! */
                  struct spec_section *next = 
                    (struct spec_section *) xmalloc 
                    (sizeof (struct spec_section));
                  next->name = section->name;
                  next->next = input_sections;
                  next->missing = 0;
                  input_sections = next;
                }
        }

    }

  /* We will be comparing the entries in the dir file against the
     current filename, so need to strip off any directory prefix and/or
     [.info][.gz] suffix.  */
  if (!remove_exactly) {
    char *infile_basename = infile + strlen (infile);

    if (HAVE_DRIVE (infile))
      infile += 2;      /* get past the drive spec X: */

    while (infile_basename > infile && !IS_SLASH (infile_basename[-1]))
      infile_basename--;

    infile_sans_info = strip_info_suffix (infile_basename);
  } else
    infile_sans_info = xstrdup(infile);

  /* Now Read the Info file and parse it into lines, unless we're 
     removing exactly.  */
  if (!remove_exactly)
    {
      if (debug_flag)
        printf ("debug: reading input file %s\n", infile);
      input_data = readfile (infile, &input_size, NULL, NULL, NULL);
      input_lines = findlines (input_data, input_size, &input_nlines);
    }

  i = parse_input (input_lines, input_nlines,
                   &input_sections, &entries_to_add_from_file, delete_flag);
  if (!delete_flag)
    {
      /* If there are no entries on the command-line at all, so we use the 
         entries found in the Info file itself (if any). */
      if (entries_to_add == NULL)
        {
          entries_to_add = entries_to_add_from_file;
          n_entries_to_add = i;
        }
      /* There are entries on the command-line, and they override the entries
         found in the Info file. */
      else if (entries_to_add)
        {
          if (entries_to_add_from_file == NULL)
            {
              /* No entries found in the file anyway.  Fill in any 
                 missing names with the info file's basename.  We're out
                 of luck for any missing descriptions. */
              add_missing_names (entries_to_add, infile_sans_info);
              /* add_missing_descriptions (entries_to_add, "\n"); */
            }
          else
            {
              /* Fill in any missing names or descriptions with what was
                 found in the Info file. */
              char *desc = NULL;
              size_t desc_len = 0;
              char *name = NULL;
              size_t name_len = 0;
              split_entry (entries_to_add_from_file->text, &name, &name_len,
                           &desc, &desc_len);
              if (name)
                {
                  /* If the name doesn't look right, bail and use the 
                     name based on the Info file. */
                  if (name[0] != '*')
                    add_missing_names (entries_to_add, infile_sans_info);
                  else
                    add_missing_names (entries_to_add, name);
                  free (name);
                }

              if (desc)
                {
                  add_missing_descriptions (entries_to_add, desc);
                  free (desc);
                }
            }
        }
            
      /* Lastly, fill in any missing basenames that might still be hanging
         around from --name options on the command-line. */
      add_missing_basenames (entries_to_add, infile_sans_info);

      /* Reformat the new entries if we're doing that. */
      if (indent_flag)
        {
          char *no_indent = getenv ("INSTALL_INFO_NO_INDENT");
          if (!no_indent)
            reformat_new_entries (entries_to_add, calign, align, maxwidth);
        }

      /* If we got no sections, default to "Miscellaneous".  */
      if (input_sections == NULL)
        {
          input_sections = (struct spec_section *)
            xmalloc (sizeof (struct spec_section));
          input_sections->name = "Miscellaneous";
          input_sections->next = NULL;
          input_sections->missing = 1;
        }

      if (entries_to_add == 0)
        { /* No need to abort here, the original info file may not
             have the requisite Texinfo commands.  This is not
             something an installer should have to correct (it's a
             problem for the maintainer), and there's no need to cause
             subsequent parts of `make install' to fail.  */
          if (!quiet_flag)
            warning (_("no info dir entry in `%s'"), infile, 0);
          xexit (0);
        }

      /* If the entries came from the command-line arguments, their
         entry_sections pointers are not yet set.  Walk the chain of
         the entries and for each entry update entry_sections to point
         to the head of the list of sections where this entry should
         be put.  Note that all the entries specified on the command
         line get put into ALL the sections we've got, either from the
         Info file, or (under --section) from the command line,
         because in the loop below every entry inherits the entire
         chain of sections.  */
      if (n_entries_to_add > 0 && entries_to_add->entry_sections == NULL)
        {
          struct spec_entry *ep;

          for (ep = entries_to_add; ep; ep = ep->next)
            ep->entry_sections = input_sections;
        }
    }

  if (delete_flag)
    {
      something_deleted = mark_entry_for_deletion (dir_lines, dir_nlines, 
                                                   infile_sans_info);
      if (!something_deleted && !remove_exactly)
        {
          struct spec_entry *entry;
          for (entry = entries_to_add; entry; entry = entry->next)
            {
              /* If the entry came from the info file... */
              if (entry->entry_sections != NULL)
                {
                  char *name = extract_menu_item_name (entry->text);
                  something_deleted = 
                    mark_entry_for_deletion (dir_lines, dir_nlines, name);
                  free (name);
                }
            }
      
          if (!something_deleted)
            {
              struct spec_entry *entry;
              for (entry = entries_to_add; entry; entry = entry->next)
                {
                  /* If the entry came from the command-line... */
                  if (entry->entry_sections == NULL)
                    something_deleted = 
                      mark_entry_for_deletion (dir_lines, dir_nlines, 
                                               entry->text);
                }
            }
        }
    }
    
  /* Check for sections with zero entries and mark them for deletion. */
  if (delete_flag && something_deleted && !keep_old_flag)
    {
      struct node *node;
      struct menu_section *section;
      int section_empty;

      for (node = dir_nodes; node ; node = node->next)
        for (section = node->sections; section ; section = section->next)
          {
            section_empty = 1;
            for (i = section->end_line; i > section->start_line; i--)
              {
                if (dir_lines[i - 1].delete == 0 && 
                    dir_lines[i - 1].size != 0)
                  {
                    section_empty = 0;
                    break;
                  }
              }

            if (section_empty)
              {
                /* This gets rid of any trailing empty lines at the end  
                   of the section, and the title too. */
                for (i = section->end_line; i >= section->start_line; i--)
                  dir_lines[i - 1].delete = 1;
              }
          }
    }

  /* Decide where to add the new entries (unless --delete was used).
     Find the menu sections to add them in.
     In each section, find the proper alphabetical place to add
     each of the entries.  */
  if (!delete_flag)
    {
      struct node *node;
      struct menu_section *section;
      struct spec_section *spec;

      for (node = dir_nodes; node; node = node->next)
        for (section = node->sections; section; section = section->next)
          {
            for (i = section->end_line; i > section->start_line; i--)
              if (dir_lines[i - 1].size != 0)
                break;
            section->end_line = i;

            for (spec = input_sections; spec; spec = spec->next)
              if (!strcmp (spec->name, section->name))
                break;
            if (spec)
              {
                int add_at_line = section->end_line;
                struct spec_entry *entry;
                /* Say we have found at least one section with this name,
                   so we need not add such a section.  */
                spec->missing = 0;
                /* For each entry, find the right place in this section
                   to add it.  */
                for (entry = entries_to_add; entry; entry = entry->next)
                  {
                    /* Did they at all want this entry to be put into
                       this section?  */
                    for (spec = entry->entry_sections;
                         spec && spec != entry->entry_sections_tail;
                         spec = spec->next)
                      {
                        if (!strcmp (spec->name, section->name))
                          break;
                      }
                    if (!spec || spec == entry->entry_sections_tail)
                      continue;

                    /* Subtract one because dir_lines is zero-based,
                       but the `end_line' and `start_line' members are
                       one-based.  */
                    for (i = section->end_line - 1;
                         i >= section->start_line - 1; i--)
                      {
                        /* If an entry exists with the same name,
                           and was not marked for deletion
                           (which means it is for some other file),
                           we are in trouble.  */
                        if (dir_lines[i].start[0] == '*'
                            && menu_line_equal (entry->text, entry->text_len,
                                                dir_lines[i].start,
                                                dir_lines[i].size)
                            && !dir_lines[i].delete)
                          {
                            if (keep_old_flag)
                              {
                                add_at_line = -1;
                                break;
                              }
                            else
                              {
                                int j;
                                dir_lines[i].delete = 1;
                                for (j = i + 1; j < section->end_line; j++)
                                  {
                                    if (dir_lines[j].start[0] == '*')
                                      break;
                                    dir_lines[j].delete = 1;
                                  }
                              }
                          }
                        if (dir_lines[i].start[0] == '*'
                            && menu_line_lessp (entry->text, entry->text_len,
                                                dir_lines[i].start,
                                                dir_lines[i].size))
                          add_at_line = i;
                      }
                    if (add_at_line < 0)
                      continue;
                    insert_entry_here (entry, add_at_line,
                                       dir_lines, n_entries_to_add);
                  }
              }
          }

    }
  /* Decide where to add the new sections (unless --delete was used).
     Alphabetically find the menu sections to add them before.  */
  if (!delete_flag)
    {
      struct node *node;
      struct node *top = NULL;

      /* Find the `Top' node. */
      for (node = dir_nodes; node; node = node->next)
        if (node->name && strcmp (node->name, "Top") == 0)
          top = node;

      if (top)
        {
          struct spec_section *spec;
          int found = 0;
          struct line_data *target_line = NULL;
          for (spec = input_sections; spec; spec = spec->next)
            {
              found = 0;
              target_line = NULL;
              if (!spec->missing)
                continue;
              if (order_new_sections_alphabetically_flag)
                {
                  struct menu_section *section;
                  struct menu_section *prev_section = NULL;
              
                  /* Look for the first section name that 
                     exceeds SPEC->NAME. */
                  for (section = top->sections; section ; 
                       section = section->next)
                    {
                      found = (mbscasecmp (spec->name, section->name) < 0);
                      if (found)
                        {
                          /* Mark the section for addition at this point. */
                          if (prev_section)
                            target_line = &dir_lines[prev_section->end_line];
                          else
                            target_line = 
                              &dir_lines[top->sections->start_line - 2];

                          break;
                        }
                      prev_section = section;
                    }
                }
                  
              /* When we can't put a section anywhere, we put it at the 
                 bottom of the file. */
              if (!found)
                target_line = &dir_lines[top->end_line];

              /* Add the section to our list of sections being added
                 at this point of the DIR file. */
              target_line->num_sections_to_add++;
              target_line->add_sections_before = 
                (struct spec_section **) xrealloc 
                (target_line->add_sections_before, 
                 (target_line->num_sections_to_add *
                  sizeof (struct spec_section *)));
              i = target_line->num_sections_to_add - 1;
              target_line->add_sections_before[i] = spec;
            }
        }
    }

  if (delete_flag && !something_deleted && !quiet_flag)
    warning (_("no entries found for `%s'; nothing deleted"), infile, 0);

  if (debug_flag)
    printf ("debug: writing dir file %s\n", opened_dirfilename);
  if (chicken_flag)
    printf ("test mode, not updating dir file %s\n", opened_dirfilename);
  else
    output_dirfile (opened_dirfilename, dir_nlines, dir_lines,
                    n_entries_to_add, entries_to_add,
                    input_sections, compression_program);

  xexit (0);
  return 0; /* Avoid bogus warnings.  */
}

/* Divide the text at DATA (of SIZE bytes) into lines.
   Return a vector of struct line_data describing the lines.
   Store the length of that vector into *NLINESP.  */

struct line_data *
findlines (char *data, int size, int *nlinesp)
{
  int i;
  int lineflag = 1;
  int lines_allocated = 511;
  int filled = 0;
  struct line_data *lines
    = xmalloc ((lines_allocated + 1) * sizeof (struct line_data));

  for (i = 0; i < size; i++)
    {
      if (lineflag)
        {
          if (filled == lines_allocated)
            {
              /* try to keep things somewhat page-aligned */
              lines_allocated = ((lines_allocated + 1) * 2) - 1;
              lines = xrealloc (lines, (lines_allocated + 1)
                                       * sizeof (struct line_data));
            }
          lines[filled].start = &data[i];
          lines[filled].add_entries_before = 0;
          lines[filled].add_sections_before = NULL;
          lines[filled].num_sections_to_add = 0;
          lines[filled].delete = 0;
          if (filled > 0)
            lines[filled - 1].size
              = lines[filled].start - lines[filled - 1].start - 1;
          filled++;
        }
      lineflag = (data[i] == '\n');
    }
  if (filled > 0)
    lines[filled - 1].size = &data[i] - lines[filled - 1].start - lineflag;

  /* Do not leave garbage in the last element.  */
  lines[filled].start = NULL;
  lines[filled].add_entries_before = NULL;
  lines[filled].add_sections_before = NULL;
  lines[filled].num_sections_to_add = 0;
  lines[filled].delete = 0;
  lines[filled].size = 0;

  *nlinesp = filled;
  return lines;
}

/* This is the comparison function for qsort for a vector of pointers to
   struct spec_section.  (Have to use const void * as the parameter type
   to avoid incompatible-with-qsort warnings.)
   Compare the section names.  */

int
compare_section_names (const void *p1, const void *p2)
{
  struct spec_section **sec1 = (struct spec_section **) p1;
  struct spec_section **sec2 = (struct spec_section **) p2;
  char *name1 = (*sec1)->name;
  char *name2 = (*sec2)->name;
  return strcmp (name1, name2);
}

/* This is the comparison function for qsort
   for a vector of pointers to struct spec_entry.
   Compare the entries' text.  */

int
compare_entries_text (const void *p1, const void *p2)
{
  struct spec_entry **entry1 = (struct spec_entry **) p1;
  struct spec_entry **entry2 = (struct spec_entry **) p2;
  char *text1 = (*entry1)->text;
  char *text2 = (*entry2)->text;
  char *colon1 = strchr (text1, ':');
  char *colon2 = strchr (text2, ':');
  int len1, len2;

  if (!colon1)
    len1 = strlen (text1);
  else
    len1 = colon1 - text1;
  if (!colon2)
    len2 = strlen (text2);
  else
    len2 = colon2 - text2;
  return mbsncasecmp (text1, text2, len1 <= len2 ? len1 : len2);
}

/* Insert ENTRY into the add_entries_before vector
   for line number LINE_NUMBER of the dir file.
   DIR_LINES and N_ENTRIES carry information from like-named variables
   in main.  */

void
insert_entry_here (struct spec_entry *entry, int line_number,
                   struct line_data *dir_lines, int n_entries)
{
  int i, j;

  if (dir_lines[line_number].add_entries_before == 0)
    {
      dir_lines[line_number].add_entries_before
        = (struct spec_entry **) xmalloc (n_entries * sizeof (struct spec_entry *));
      for (i = 0; i < n_entries; i++)
        dir_lines[line_number].add_entries_before[i] = 0;
    }

  /* Find the place where this entry belongs.  If there are already
     several entries to add before LINE_NUMBER, make sure they are in
     alphabetical order.  */
  for (i = 0; i < n_entries; i++)
    if (dir_lines[line_number].add_entries_before[i] == 0
        || menu_line_lessp (entry->text, strlen (entry->text),
                            dir_lines[line_number].add_entries_before[i]->text,
                            strlen (dir_lines[line_number].add_entries_before[i]->text)))
      break;

  if (i == n_entries)
    abort ();

  /* If we need to plug ENTRY into the middle of the
     ADD_ENTRIES_BEFORE array, move the entries which should be output
     after this one down one notch, before adding a new one.  */
  if (dir_lines[line_number].add_entries_before[i] != 0)
    for (j = n_entries - 1; j > i; j--)
      dir_lines[line_number].add_entries_before[j]
        = dir_lines[line_number].add_entries_before[j - 1];

  dir_lines[line_number].add_entries_before[i] = entry;
}

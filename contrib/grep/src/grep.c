/* grep.c - main driver file for grep.
   Copyright (C) 1992, 1997-2002, 2004-2020 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* Written July 1992 by Mike Haertel.  */

#include <config.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <wchar.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include "system.h"

#include "argmatch.h"
#include "c-ctype.h"
#include "c-stack.h"
#include "closeout.h"
#include "colorize.h"
#include "die.h"
#include "error.h"
#include "exclude.h"
#include "exitfail.h"
#include "fcntl-safer.h"
#include "fts_.h"
#include "getopt.h"
#include "getprogname.h"
#include "grep.h"
#include "intprops.h"
#include "propername.h"
#include "quote.h"
#include "safe-read.h"
#include "search.h"
#include "c-strcase.h"
#include "version-etc.h"
#include "xalloc.h"
#include "xbinary-io.h"
#include "xstrtol.h"

enum { SEP_CHAR_SELECTED = ':' };
enum { SEP_CHAR_REJECTED = '-' };
static char const SEP_STR_GROUP[] = "--";

/* When stdout is connected to a regular file, save its stat
   information here, so that we can automatically skip it, thus
   avoiding a potential (racy) infinite loop.  */
static struct stat out_stat;

/* if non-zero, display usage information and exit */
static int show_help;

/* Print the version on standard output and exit.  */
static bool show_version;

/* Suppress diagnostics for nonexistent or unreadable files.  */
static bool suppress_errors;

/* If nonzero, use color markers.  */
static int color_option;

/* Show only the part of a line matching the expression. */
static bool only_matching;

/* If nonzero, make sure first content char in a line is on a tab stop. */
static bool align_tabs;

/* Print width of line numbers and byte offsets.  Nonzero if ALIGN_TABS.  */
static int offset_width;

/* See below */
struct FL_pair
  {
    char const *filename;
    size_t lineno;
  };

/* A list of lineno,filename pairs corresponding to -f FILENAME
   arguments. Since we store the concatenation of all patterns in
   a single array, KEYS, be they from the command line via "-e PAT"
   or read from one or more -f-specified FILENAMES.  Given this
   invocation, grep -f <(seq 5) -f <(seq 2) -f <(seq 3) FILE, there
   will be three entries in LF_PAIR: {1, x} {6, y} {8, z}, where
   x, y and z are just place-holders for shell-generated names.  */
static struct FL_pair *fl_pair;
static size_t n_fl_pair_slots;
/* Count not only -f-specified files, but also individual -e operands
   and any command-line argument that serves as a regular expression.  */
static size_t n_pattern_files;

/* The number of patterns seen so far.
   It is advanced by fl_add and, when needed, used in pattern_file_name
   to derive a file-relative line number.  */
static size_t n_patterns;

/* Return the number of newline bytes in BUF with size SIZE.  */
static size_t _GL_ATTRIBUTE_PURE
count_nl_bytes (char const *buf, size_t size)
{
  char const *p = buf;
  char const *end_p = buf + size;
  size_t n = 0;
  while ((p = memchr (p, '\n', end_p - p)))
    p++, n++;
  return n;
}

/* Append a FILENAME,line-number pair to FL_PAIR, and update
   pattern-related counts from the contents of BUF with SIZE bytes.  */
static void
fl_add (char const *buf, size_t size, char const *filename)
{
  if (n_fl_pair_slots <= n_pattern_files)
    fl_pair = x2nrealloc (fl_pair, &n_fl_pair_slots, sizeof *fl_pair);

  fl_pair[n_pattern_files].lineno = n_patterns + 1;
  fl_pair[n_pattern_files].filename = filename;
  n_pattern_files++;
  n_patterns += count_nl_bytes (buf, size);
}

/* Map the line number, LINENO, of one of the input patterns to the
   name of the file from which it came.  If it was read from stdin
   or if it was specified on the command line, return "-".  */
char const * _GL_ATTRIBUTE_PURE
pattern_file_name (size_t lineno, size_t *new_lineno)
{
  size_t i;
  for (i = 1; i < n_pattern_files; i++)
    {
      if (lineno < fl_pair[i].lineno)
        break;
    }

  *new_lineno = lineno - fl_pair[i - 1].lineno + 1;
  return fl_pair[i - 1].filename;
}

#if HAVE_ASAN
/* Record the starting address and length of the sole poisoned region,
   so that we can unpoison it later, just before each following read.  */
static void const *poison_buf;
static size_t poison_len;

static void
clear_asan_poison (void)
{
  if (poison_buf)
    __asan_unpoison_memory_region (poison_buf, poison_len);
}

static void
asan_poison (void const *addr, size_t size)
{
  poison_buf = addr;
  poison_len = size;

  __asan_poison_memory_region (poison_buf, poison_len);
}
#else
static void clear_asan_poison (void) { }
static void asan_poison (void const volatile *addr, size_t size) { }
#endif

/* The group separator used when context is requested. */
static const char *group_separator = SEP_STR_GROUP;

/* The context and logic for choosing default --color screen attributes
   (foreground and background colors, etc.) are the following.
      -- There are eight basic colors available, each with its own
         nominal luminosity to the human eye and foreground/background
         codes (black [0 %, 30/40], blue [11 %, 34/44], red [30 %, 31/41],
         magenta [41 %, 35/45], green [59 %, 32/42], cyan [70 %, 36/46],
         yellow [89 %, 33/43], and white [100 %, 37/47]).
      -- Sometimes, white as a background is actually implemented using
         a shade of light gray, so that a foreground white can be visible
         on top of it (but most often not).
      -- Sometimes, black as a foreground is actually implemented using
         a shade of dark gray, so that it can be visible on top of a
         background black (but most often not).
      -- Sometimes, more colors are available, as extensions.
      -- Other attributes can be selected/deselected (bold [1/22],
         underline [4/24], standout/inverse [7/27], blink [5/25], and
         invisible/hidden [8/28]).  They are sometimes implemented by
         using colors instead of what their names imply; e.g., bold is
         often achieved by using brighter colors.  In practice, only bold
         is really available to us, underline sometimes being mapped by
         the terminal to some strange color choice, and standout best
         being left for use by downstream programs such as less(1).
      -- We cannot assume that any of the extensions or special features
         are available for the purpose of choosing defaults for everyone.
      -- The most prevalent default terminal backgrounds are pure black
         and pure white, and are not necessarily the same shades of
         those as if they were selected explicitly with SGR sequences.
         Some terminals use dark or light pictures as default background,
         but those are covered over by an explicit selection of background
         color with an SGR sequence; their users will appreciate their
         background pictures not be covered like this, if possible.
      -- Some uses of colors attributes is to make some output items
         more understated (e.g., context lines); this cannot be achieved
         by changing the background color.
      -- For these reasons, the grep color defaults should strive not
         to change the background color from its default, unless it's
         for a short item that should be highlighted, not understated.
      -- The grep foreground color defaults (without an explicitly set
         background) should provide enough contrast to be readable on any
         terminal with either a black (dark) or white (light) background.
         This only leaves red, magenta, green, and cyan (and their bold
         counterparts) and possibly bold blue.  */
/* The color strings used for matched text.
   The user can overwrite them using the deprecated
   environment variable GREP_COLOR or the new GREP_COLORS.  */
static const char *selected_match_color = "01;31";	/* bold red */
static const char *context_match_color  = "01;31";	/* bold red */

/* Other colors.  Defaults look damn good.  */
static const char *filename_color = "35";	/* magenta */
static const char *line_num_color = "32";	/* green */
static const char *byte_num_color = "32";	/* green */
static const char *sep_color      = "36";	/* cyan */
static const char *selected_line_color = "";	/* default color pair */
static const char *context_line_color  = "";	/* default color pair */

/* Select Graphic Rendition (SGR, "\33[...m") strings.  */
/* Also Erase in Line (EL) to Right ("\33[K") by default.  */
/*    Why have EL to Right after SGR?
         -- The behavior of line-wrapping when at the bottom of the
            terminal screen and at the end of the current line is often
            such that a new line is introduced, entirely cleared with
            the current background color which may be different from the
            default one (see the boolean back_color_erase terminfo(5)
            capability), thus scrolling the display by one line.
            The end of this new line will stay in this background color
            even after reverting to the default background color with
            "\33[m', unless it is explicitly cleared again with "\33[K"
            (which is the behavior the user would instinctively expect
            from the whole thing).  There may be some unavoidable
            background-color flicker at the end of this new line because
            of this (when timing with the monitor's redraw is just right).
         -- The behavior of HT (tab, "\t") is usually the same as that of
            Cursor Forward Tabulation (CHT) with a default parameter
            of 1 ("\33[I"), i.e., it performs pure movement to the next
            tab stop, without any clearing of either content or screen
            attributes (including background color); try
               printf 'asdfqwerzxcv\rASDF\tZXCV\n'
            in a bash(1) shell to demonstrate this.  This is not what the
            user would instinctively expect of HT (but is ok for CHT).
            The instinctive behavior would include clearing the terminal
            cells that are skipped over by HT with blank cells in the
            current screen attributes, including background color;
            the boolean dest_tabs_magic_smso terminfo(5) capability
            indicates this saner behavior for HT, but only some rare
            terminals have it (although it also indicates a special
            glitch with standout mode in the Teleray terminal for which
            it was initially introduced).  The remedy is to add "\33K"
            after each SGR sequence, be it START (to fix the behavior
            of any HT after that before another SGR) or END (to fix the
            behavior of an HT in default background color that would
            follow a line-wrapping at the bottom of the screen in another
            background color, and to complement doing it after START).
            Piping grep's output through a pager such as less(1) avoids
            any HT problems since the pager performs tab expansion.

      Generic disadvantages of this remedy are:
         -- Some very rare terminals might support SGR but not EL (nobody
            will use "grep --color" on a terminal that does not support
            SGR in the first place).
         -- Having these extra control sequences might somewhat complicate
            the task of any program trying to parse "grep --color"
            output in order to extract structuring information from it.
      A specific disadvantage to doing it after SGR START is:
         -- Even more possible background color flicker (when timing
            with the monitor's redraw is just right), even when not at the
            bottom of the screen.
      There are no additional disadvantages specific to doing it after
      SGR END.

      It would be impractical for GNU grep to become a full-fledged
      terminal program linked against ncurses or the like, so it will
      not detect terminfo(5) capabilities.  */
static const char *sgr_start = "\33[%sm\33[K";
static const char *sgr_end   = "\33[m\33[K";

/* SGR utility functions.  */
static void
pr_sgr_start (char const *s)
{
  if (*s)
    print_start_colorize (sgr_start, s);
}
static void
pr_sgr_end (char const *s)
{
  if (*s)
    print_end_colorize (sgr_end);
}
static void
pr_sgr_start_if (char const *s)
{
  if (color_option)
    pr_sgr_start (s);
}
static void
pr_sgr_end_if (char const *s)
{
  if (color_option)
    pr_sgr_end (s);
}

struct color_cap
  {
    const char *name;
    const char **var;
    void (*fct) (void);
  };

static void
color_cap_mt_fct (void)
{
  /* Our caller just set selected_match_color.  */
  context_match_color = selected_match_color;
}

static void
color_cap_rv_fct (void)
{
  /* By this point, it was 1 (or already -1).  */
  color_option = -1;  /* That's still != 0.  */
}

static void
color_cap_ne_fct (void)
{
  sgr_start = "\33[%sm";
  sgr_end   = "\33[m";
}

/* For GREP_COLORS.  */
static const struct color_cap color_dict[] =
  {
    { "mt", &selected_match_color, color_cap_mt_fct }, /* both ms/mc */
    { "ms", &selected_match_color, NULL }, /* selected matched text */
    { "mc", &context_match_color,  NULL }, /* context matched text */
    { "fn", &filename_color,       NULL }, /* filename */
    { "ln", &line_num_color,       NULL }, /* line number */
    { "bn", &byte_num_color,       NULL }, /* byte (sic) offset */
    { "se", &sep_color,            NULL }, /* separator */
    { "sl", &selected_line_color,  NULL }, /* selected lines */
    { "cx", &context_line_color,   NULL }, /* context lines */
    { "rv", NULL,                  color_cap_rv_fct }, /* -v reverses sl/cx */
    { "ne", NULL,                  color_cap_ne_fct }, /* no EL on SGR_* */
    { NULL, NULL,                  NULL }
  };

/* Saved errno value from failed output functions on stdout.  */
static int stdout_errno;

static void
putchar_errno (int c)
{
  if (putchar (c) < 0)
    stdout_errno = errno;
}

static void
fputs_errno (char const *s)
{
  if (fputs (s, stdout) < 0)
    stdout_errno = errno;
}

static void _GL_ATTRIBUTE_FORMAT_PRINTF (1, 2)
printf_errno (char const *format, ...)
{
  va_list ap;
  va_start (ap, format);
  if (vfprintf (stdout, format, ap) < 0)
    stdout_errno = errno;
  va_end (ap);
}

static void
fwrite_errno (void const *ptr, size_t size, size_t nmemb)
{
  if (fwrite (ptr, size, nmemb, stdout) != nmemb)
    stdout_errno = errno;
}

static void
fflush_errno (void)
{
  if (fflush (stdout) != 0)
    stdout_errno = errno;
}

static struct exclude *excluded_patterns[2];
static struct exclude *excluded_directory_patterns[2];
/* Short options.  */
static char const short_options[] =
"0123456789A:B:C:D:EFGHIPTUVX:abcd:e:f:hiLlm:noqRrsuvwxyZz";

/* Non-boolean long options that have no corresponding short equivalents.  */
enum
{
  BINARY_FILES_OPTION = CHAR_MAX + 1,
  COLOR_OPTION,
  EXCLUDE_DIRECTORY_OPTION,
  EXCLUDE_OPTION,
  EXCLUDE_FROM_OPTION,
  GROUP_SEPARATOR_OPTION,
  INCLUDE_OPTION,
  LINE_BUFFERED_OPTION,
  LABEL_OPTION,
  NO_IGNORE_CASE_OPTION
};

/* Long options equivalences. */
static struct option const long_options[] =
{
  {"basic-regexp",    no_argument, NULL, 'G'},
  {"extended-regexp", no_argument, NULL, 'E'},
  {"fixed-regexp",    no_argument, NULL, 'F'},
  {"fixed-strings",   no_argument, NULL, 'F'},
  {"perl-regexp",     no_argument, NULL, 'P'},
  {"after-context", required_argument, NULL, 'A'},
  {"before-context", required_argument, NULL, 'B'},
  {"binary-files", required_argument, NULL, BINARY_FILES_OPTION},
  {"byte-offset", no_argument, NULL, 'b'},
  {"context", required_argument, NULL, 'C'},
  {"color", optional_argument, NULL, COLOR_OPTION},
  {"colour", optional_argument, NULL, COLOR_OPTION},
  {"count", no_argument, NULL, 'c'},
  {"devices", required_argument, NULL, 'D'},
  {"directories", required_argument, NULL, 'd'},
  {"exclude", required_argument, NULL, EXCLUDE_OPTION},
  {"exclude-from", required_argument, NULL, EXCLUDE_FROM_OPTION},
  {"exclude-dir", required_argument, NULL, EXCLUDE_DIRECTORY_OPTION},
  {"file", required_argument, NULL, 'f'},
  {"files-with-matches", no_argument, NULL, 'l'},
  {"files-without-match", no_argument, NULL, 'L'},
  {"group-separator", required_argument, NULL, GROUP_SEPARATOR_OPTION},
  {"help", no_argument, &show_help, 1},
  {"include", required_argument, NULL, INCLUDE_OPTION},
  {"ignore-case", no_argument, NULL, 'i'},
  {"no-ignore-case", no_argument, NULL, NO_IGNORE_CASE_OPTION},
  {"initial-tab", no_argument, NULL, 'T'},
  {"label", required_argument, NULL, LABEL_OPTION},
  {"line-buffered", no_argument, NULL, LINE_BUFFERED_OPTION},
  {"line-number", no_argument, NULL, 'n'},
  {"line-regexp", no_argument, NULL, 'x'},
  {"max-count", required_argument, NULL, 'm'},

  {"no-filename", no_argument, NULL, 'h'},
  {"no-group-separator", no_argument, NULL, GROUP_SEPARATOR_OPTION},
  {"no-messages", no_argument, NULL, 's'},
  {"null", no_argument, NULL, 'Z'},
  {"null-data", no_argument, NULL, 'z'},
  {"only-matching", no_argument, NULL, 'o'},
  {"quiet", no_argument, NULL, 'q'},
  {"recursive", no_argument, NULL, 'r'},
  {"dereference-recursive", no_argument, NULL, 'R'},
  {"regexp", required_argument, NULL, 'e'},
  {"invert-match", no_argument, NULL, 'v'},
  {"silent", no_argument, NULL, 'q'},
  {"text", no_argument, NULL, 'a'},
  {"binary", no_argument, NULL, 'U'},
  {"unix-byte-offsets", no_argument, NULL, 'u'},
  {"version", no_argument, NULL, 'V'},
  {"with-filename", no_argument, NULL, 'H'},
  {"word-regexp", no_argument, NULL, 'w'},
  {0, 0, 0, 0}
};

/* Define flags declared in grep.h. */
bool match_icase;
bool match_words;
bool match_lines;
char eolbyte;

/* For error messages. */
/* The input file name, or (if standard input) null or a --label argument.  */
static char const *filename;
/* Omit leading "./" from file names in diagnostics.  */
static bool omit_dot_slash;
static bool errseen;

/* True if output from the current input file has been suppressed
   because an output line had an encoding error.  */
static bool encoding_error_output;

enum directories_type
  {
    READ_DIRECTORIES = 2,
    RECURSE_DIRECTORIES,
    SKIP_DIRECTORIES
  };

/* How to handle directories.  */
static char const *const directories_args[] =
{
  "read", "recurse", "skip", NULL
};
static enum directories_type const directories_types[] =
{
  READ_DIRECTORIES, RECURSE_DIRECTORIES, SKIP_DIRECTORIES
};
ARGMATCH_VERIFY (directories_args, directories_types);

static enum directories_type directories = READ_DIRECTORIES;

enum { basic_fts_options = FTS_CWDFD | FTS_NOSTAT | FTS_TIGHT_CYCLE_CHECK };
static int fts_options = basic_fts_options | FTS_COMFOLLOW | FTS_PHYSICAL;

/* How to handle devices. */
static enum
  {
    READ_COMMAND_LINE_DEVICES,
    READ_DEVICES,
    SKIP_DEVICES
  } devices = READ_COMMAND_LINE_DEVICES;

static bool grepfile (int, char const *, bool, bool);
static bool grepdesc (int, bool);

static bool
is_device_mode (mode_t m)
{
  return S_ISCHR (m) || S_ISBLK (m) || S_ISSOCK (m) || S_ISFIFO (m);
}

static bool
skip_devices (bool command_line)
{
  return (devices == SKIP_DEVICES
          || ((devices == READ_COMMAND_LINE_DEVICES) & !command_line));
}

/* Return if ST->st_size is defined.  Assume the file is not a
   symbolic link.  */
static bool
usable_st_size (struct stat const *st)
{
  return S_ISREG (st->st_mode) || S_TYPEISSHM (st) || S_TYPEISTMO (st);
}

/* Lame substitutes for SEEK_DATA and SEEK_HOLE on platforms lacking them.
   Do not rely on these finding data or holes if they equal SEEK_SET.  */
#ifndef SEEK_DATA
enum { SEEK_DATA = SEEK_SET };
#endif
#ifndef SEEK_HOLE
enum { SEEK_HOLE = SEEK_SET };
#endif

/* True if lseek with SEEK_CUR or SEEK_DATA failed on the current input.  */
static bool seek_failed;
static bool seek_data_failed;

/* Functions we'll use to search. */
typedef void *(*compile_fp_t) (char *, size_t, reg_syntax_t);
typedef size_t (*execute_fp_t) (void *, char const *, size_t, size_t *,
                                char const *);
static execute_fp_t execute;
static void *compiled_pattern;

static char const *
input_filename (void)
{
  if (!filename)
    filename = _("(standard input)");
  return filename;
}

/* Unless requested, diagnose an error about the input file.  */
static void
suppressible_error (int errnum)
{
  if (! suppress_errors)
    error (0, errnum, "%s", input_filename ());
  errseen = true;
}

/* If there has already been a write error, don't bother closing
   standard output, as that might elicit a duplicate diagnostic.  */
static void
clean_up_stdout (void)
{
  if (! stdout_errno)
    close_stdout ();
}

/* A cast to TYPE of VAL.  Use this when TYPE is a pointer type, VAL
   is properly aligned for TYPE, and 'gcc -Wcast-align' cannot infer
   the alignment and would otherwise complain about the cast.  */
#if 4 < __GNUC__ + (6 <= __GNUC_MINOR__)
# define CAST_ALIGNED(type, val)                           \
    ({ __typeof__ (val) val_ = val;                        \
       _Pragma ("GCC diagnostic push")                     \
       _Pragma ("GCC diagnostic ignored \"-Wcast-align\"") \
       (type) val_;                                        \
       _Pragma ("GCC diagnostic pop")                      \
    })
#else
# define CAST_ALIGNED(type, val) ((type) (val))
#endif

/* An unsigned type suitable for fast matching.  */
typedef uintmax_t uword;

struct localeinfo localeinfo;

/* A mask to test for unibyte characters, with the pattern repeated to
   fill a uword.  For a multibyte character encoding where
   all bytes are unibyte characters, this is 0.  For UTF-8, this is
   0x808080....  For encodings where unibyte characters have no discerned
   pattern, this is all 1s.  The unsigned char C is a unibyte
   character if C & UNIBYTE_MASK is zero.  If the uword W is the
   concatenation of bytes, the bytes are all unibyte characters
   if W & UNIBYTE_MASK is zero.  */
static uword unibyte_mask;

static void
initialize_unibyte_mask (void)
{
  /* For each encoding error I that MASK does not already match,
     accumulate I's most significant 1 bit by ORing it into MASK.
     Although any 1 bit of I could be used, in practice high-order
     bits work better.  */
  unsigned char mask = 0;
  int ms1b = 1;
  for (int i = 1; i <= UCHAR_MAX; i++)
    if ((localeinfo.sbclen[i] != 1) & ! (mask & i))
      {
        while (ms1b * 2 <= i)
          ms1b *= 2;
        mask |= ms1b;
      }

  /* Now MASK will detect any encoding-error byte, although it may
     cry wolf and it may not be optimal.  Build a uword-length mask by
     repeating MASK.  */
  uword uword_max = -1;
  unibyte_mask = uword_max / UCHAR_MAX * mask;
}

/* Skip the easy bytes in a buffer that is guaranteed to have a sentinel
   that is not easy, and return a pointer to the first non-easy byte.
   The easy bytes all have UNIBYTE_MASK off.  */
static char const * _GL_ATTRIBUTE_PURE
skip_easy_bytes (char const *buf)
{
  /* Search a byte at a time until the pointer is aligned, then a
     uword at a time until a match is found, then a byte at a time to
     identify the exact byte.  The uword search may go slightly past
     the buffer end, but that's benign.  */
  char const *p;
  uword const *s;
  for (p = buf; (uintptr_t) p % sizeof (uword) != 0; p++)
    if (to_uchar (*p) & unibyte_mask)
      return p;
  for (s = CAST_ALIGNED (uword const *, p); ! (*s & unibyte_mask); s++)
    continue;
  for (p = (char const *) s; ! (to_uchar (*p) & unibyte_mask); p++)
    continue;
  return p;
}

/* Return true if BUF, of size SIZE, has an encoding error.
   BUF must be followed by at least sizeof (uword) bytes,
   the first of which may be modified.  */
static bool
buf_has_encoding_errors (char *buf, size_t size)
{
  if (! unibyte_mask)
    return false;

  mbstate_t mbs = { 0 };
  size_t clen;

  buf[size] = -1;
  for (char const *p = buf; (p = skip_easy_bytes (p)) < buf + size; p += clen)
    {
      clen = mbrlen (p, buf + size - p, &mbs);
      if ((size_t) -2 <= clen)
        return true;
    }

  return false;
}


/* Return true if BUF, of size SIZE, has a null byte.
   BUF must be followed by at least one byte,
   which may be arbitrarily written to or read from.  */
static bool
buf_has_nulls (char *buf, size_t size)
{
  buf[size] = 0;
  return strlen (buf) != size;
}

/* Return true if a file is known to contain null bytes.
   SIZE bytes have already been read from the file
   with descriptor FD and status ST.  */
static bool
file_must_have_nulls (size_t size, int fd, struct stat const *st)
{
  /* If the file has holes, it must contain a null byte somewhere.  */
  if (SEEK_HOLE != SEEK_SET && !seek_failed
      && usable_st_size (st) && size < st->st_size)
    {
      off_t cur = size;
      if (O_BINARY || fd == STDIN_FILENO)
        {
          cur = lseek (fd, 0, SEEK_CUR);
          if (cur < 0)
            return false;
        }

      /* Look for a hole after the current location.  */
      off_t hole_start = lseek (fd, cur, SEEK_HOLE);
      if (0 <= hole_start)
        {
          if (lseek (fd, cur, SEEK_SET) < 0)
            suppressible_error (errno);
          if (hole_start < st->st_size)
            return true;
        }
    }

  return false;
}

/* Convert STR to a nonnegative integer, storing the result in *OUT.
   STR must be a valid context length argument; report an error if it
   isn't.  Silently ceiling *OUT at the maximum value, as that is
   practically equivalent to infinity for grep's purposes.  */
static void
context_length_arg (char const *str, intmax_t *out)
{
  switch (xstrtoimax (str, 0, 10, out, ""))
    {
    case LONGINT_OK:
    case LONGINT_OVERFLOW:
      if (0 <= *out)
        break;
      FALLTHROUGH;
    default:
      die (EXIT_TROUBLE, 0, "%s: %s", str,
           _("invalid context length argument"));
    }
}

/* Return the add_exclude options suitable for excluding a file name.
   If COMMAND_LINE, it is a command-line file name.  */
static int
exclude_options (bool command_line)
{
  return EXCLUDE_WILDCARDS | (command_line ? 0 : EXCLUDE_ANCHORED);
}

/* Return true if the file with NAME should be skipped.
   If COMMAND_LINE, it is a command-line argument.
   If IS_DIR, it is a directory.  */
static bool
skipped_file (char const *name, bool command_line, bool is_dir)
{
  struct exclude **pats;
  if (! is_dir)
    pats = excluded_patterns;
  else if (directories == SKIP_DIRECTORIES)
    return true;
  else if (command_line && omit_dot_slash)
    return false;
  else
    pats = excluded_directory_patterns;
  return pats[command_line] && excluded_file_name (pats[command_line], name);
}

/* Hairy buffering mechanism for grep.  The intent is to keep
   all reads aligned on a page boundary and multiples of the
   page size, unless a read yields a partial page.  */

static char *buffer;		/* Base of buffer. */
static size_t bufalloc;		/* Allocated buffer size, counting slop. */
static int bufdesc;		/* File descriptor. */
static char *bufbeg;		/* Beginning of user-visible stuff. */
static char *buflim;		/* Limit of user-visible stuff. */
static size_t pagesize;		/* alignment of memory pages */
static off_t bufoffset;		/* Read offset.  */
static off_t after_last_match;	/* Pointer after last matching line that
                                   would have been output if we were
                                   outputting characters. */
static bool skip_nuls;		/* Skip '\0' in data.  */
static bool skip_empty_lines;	/* Skip empty lines in data.  */
static uintmax_t totalnl;	/* Total newline count before lastnl. */

/* Initial buffer size, not counting slop. */
enum { INITIAL_BUFSIZE = 96 * 1024 };

/* Return VAL aligned to the next multiple of ALIGNMENT.  VAL can be
   an integer or a pointer.  Both args must be free of side effects.  */
#define ALIGN_TO(val, alignment) \
  ((size_t) (val) % (alignment) == 0 \
   ? (val) \
   : (val) + ((alignment) - (size_t) (val) % (alignment)))

/* Add two numbers that count input bytes or lines, and report an
   error if the addition overflows.  */
static uintmax_t
add_count (uintmax_t a, uintmax_t b)
{
  uintmax_t sum = a + b;
  if (sum < a)
    die (EXIT_TROUBLE, 0, _("input is too large to count"));
  return sum;
}

/* Return true if BUF (of size SIZE) is all zeros.  */
static bool
all_zeros (char const *buf, size_t size)
{
  for (char const *p = buf; p < buf + size; p++)
    if (*p)
      return false;
  return true;
}

/* Reset the buffer for a new file, returning false if we should skip it.
   Initialize on the first time through. */
static bool
reset (int fd, struct stat const *st)
{
  bufbeg = buflim = ALIGN_TO (buffer + 1, pagesize);
  bufbeg[-1] = eolbyte;
  bufdesc = fd;
  bufoffset = fd == STDIN_FILENO ? lseek (fd, 0, SEEK_CUR) : 0;
  seek_failed = bufoffset < 0;

  /* Assume SEEK_DATA fails if SEEK_CUR does.  */
  seek_data_failed = seek_failed;

  if (seek_failed)
    {
      if (errno != ESPIPE)
        {
          suppressible_error (errno);
          return false;
        }
      bufoffset = 0;
    }
  return true;
}

/* Read new stuff into the buffer, saving the specified
   amount of old stuff.  When we're done, 'bufbeg' points
   to the beginning of the buffer contents, and 'buflim'
   points just after the end.  Return false if there's an error.  */
static bool
fillbuf (size_t save, struct stat const *st)
{
  size_t fillsize;
  bool cc = true;
  char *readbuf;
  size_t readsize;

  /* Offset from start of buffer to start of old stuff
     that we want to save.  */
  size_t saved_offset = buflim - save - buffer;

  if (pagesize <= buffer + bufalloc - sizeof (uword) - buflim)
    {
      readbuf = buflim;
      bufbeg = buflim - save;
    }
  else
    {
      size_t minsize = save + pagesize;
      size_t newsize;
      size_t newalloc;
      char *newbuf;

      /* Grow newsize until it is at least as great as minsize.  */
      for (newsize = bufalloc - pagesize - sizeof (uword);
           newsize < minsize;
           newsize *= 2)
        if ((SIZE_MAX - pagesize - sizeof (uword)) / 2 < newsize)
          xalloc_die ();

      /* Try not to allocate more memory than the file size indicates,
         as that might cause unnecessary memory exhaustion if the file
         is large.  However, do not use the original file size as a
         heuristic if we've already read past the file end, as most
         likely the file is growing.  */
      if (usable_st_size (st))
        {
          off_t to_be_read = st->st_size - bufoffset;
          off_t maxsize_off = save + to_be_read;
          if (0 <= to_be_read && to_be_read <= maxsize_off
              && maxsize_off == (size_t) maxsize_off
              && minsize <= (size_t) maxsize_off
              && (size_t) maxsize_off < newsize)
            newsize = maxsize_off;
        }

      /* Add enough room so that the buffer is aligned and has room
         for byte sentinels fore and aft, and so that a uword can
         be read aft.  */
      newalloc = newsize + pagesize + sizeof (uword);

      newbuf = bufalloc < newalloc ? xmalloc (bufalloc = newalloc) : buffer;
      readbuf = ALIGN_TO (newbuf + 1 + save, pagesize);
      bufbeg = readbuf - save;
      memmove (bufbeg, buffer + saved_offset, save);
      bufbeg[-1] = eolbyte;
      if (newbuf != buffer)
        {
          free (buffer);
          buffer = newbuf;
        }
    }

  clear_asan_poison ();

  readsize = buffer + bufalloc - sizeof (uword) - readbuf;
  readsize -= readsize % pagesize;

  while (true)
    {
      fillsize = safe_read (bufdesc, readbuf, readsize);
      if (fillsize == SAFE_READ_ERROR)
        {
          fillsize = 0;
          cc = false;
        }
      bufoffset += fillsize;

      if (((fillsize == 0) | !skip_nuls) || !all_zeros (readbuf, fillsize))
        break;
      totalnl = add_count (totalnl, fillsize);

      if (SEEK_DATA != SEEK_SET && !seek_data_failed)
        {
          /* Solaris SEEK_DATA fails with errno == ENXIO in a hole at EOF.  */
          off_t data_start = lseek (bufdesc, bufoffset, SEEK_DATA);
          if (data_start < 0 && errno == ENXIO
              && usable_st_size (st) && bufoffset < st->st_size)
            data_start = lseek (bufdesc, 0, SEEK_END);

          if (data_start < 0)
            seek_data_failed = true;
          else
            {
              totalnl = add_count (totalnl, data_start - bufoffset);
              bufoffset = data_start;
            }
        }
    }

  buflim = readbuf + fillsize;

  /* Initialize the following word, because skip_easy_bytes and some
     matchers read (but do not use) those bytes.  This avoids false
     positive reports of these bytes being used uninitialized.  */
  memset (buflim, 0, sizeof (uword));

  /* Mark the part of the buffer not filled by the read or set by
     the above memset call as ASAN-poisoned.  */
  asan_poison (buflim + sizeof (uword),
               bufalloc - (buflim - buffer) - sizeof (uword));

  return cc;
}

/* Flags controlling the style of output. */
static enum
{
  BINARY_BINARY_FILES,
  TEXT_BINARY_FILES,
  WITHOUT_MATCH_BINARY_FILES
} binary_files;		/* How to handle binary files.  */

/* Options for output as a list of matching/non-matching files */
static enum
{
  LISTFILES_NONE,
  LISTFILES_MATCHING,
  LISTFILES_NONMATCHING,
} list_files;

/* Whether to output filenames.  1 means yes, 0 means no, and -1 means
   'grep -r PATTERN FILE' was used and it is not known yet whether
   FILE is a directory (which means yes) or not (which means no).  */
static int out_file;

static int filename_mask;	/* If zero, output nulls after filenames.  */
static bool out_quiet;		/* Suppress all normal output. */
static bool out_invert;		/* Print nonmatching stuff. */
static bool out_line;		/* Print line numbers. */
static bool out_byte;		/* Print byte offsets. */
static intmax_t out_before;	/* Lines of leading context. */
static intmax_t out_after;	/* Lines of trailing context. */
static bool count_matches;	/* Count matching lines.  */
static intmax_t max_count;	/* Max number of selected
                                   lines from an input file.  */
static bool line_buffered;	/* Use line buffering.  */
static char *label = NULL;      /* Fake filename for stdin */


/* Internal variables to keep track of byte count, context, etc. */
static uintmax_t totalcc;	/* Total character count before bufbeg. */
static char const *lastnl;	/* Pointer after last newline counted. */
static char *lastout;		/* Pointer after last character output;
                                   NULL if no character has been output
                                   or if it's conceptually before bufbeg. */
static intmax_t outleft;	/* Maximum number of selected lines.  */
static intmax_t pending;	/* Pending lines of output.
                                   Always kept 0 if out_quiet is true.  */
static bool done_on_match;	/* Stop scanning file on first match.  */
static bool exit_on_match;	/* Exit on first match.  */
static bool dev_null_output;	/* Stdout is known to be /dev/null.  */
static bool binary;		/* Use binary rather than text I/O.  */

static void
nlscan (char const *lim)
{
  size_t newlines = 0;
  char const *beg;
  for (beg = lastnl; beg < lim; beg++)
    {
      beg = memchr (beg, eolbyte, lim - beg);
      if (!beg)
        break;
      newlines++;
    }
  totalnl = add_count (totalnl, newlines);
  lastnl = lim;
}

/* Print the current filename.  */
static void
print_filename (void)
{
  pr_sgr_start_if (filename_color);
  fputs_errno (input_filename ());
  pr_sgr_end_if (filename_color);
}

/* Print a character separator.  */
static void
print_sep (char sep)
{
  pr_sgr_start_if (sep_color);
  putchar_errno (sep);
  pr_sgr_end_if (sep_color);
}

/* Print a line number or a byte offset.  */
static void
print_offset (uintmax_t pos, const char *color)
{
  pr_sgr_start_if (color);
  printf_errno ("%*"PRIuMAX, offset_width, pos);
  pr_sgr_end_if (color);
}

/* Print a whole line head (filename, line, byte).  The output data
   starts at BEG and contains LEN bytes; it is followed by at least
   sizeof (uword) bytes, the first of which may be temporarily modified.
   The output data comes from what is perhaps a larger input line that
   goes until LIM, where LIM[-1] is an end-of-line byte.  Use SEP as
   the separator on output.

   Return true unless the line was suppressed due to an encoding error.  */

static bool
print_line_head (char *beg, size_t len, char const *lim, char sep)
{
  if (binary_files != TEXT_BINARY_FILES)
    {
      char ch = beg[len];
      bool encoding_errors = buf_has_encoding_errors (beg, len);
      beg[len] = ch;
      if (encoding_errors)
        {
          encoding_error_output = true;
          return false;
        }
    }

  if (out_file)
    {
      print_filename ();
      if (filename_mask)
        print_sep (sep);
      else
        putchar_errno (0);
    }

  if (out_line)
    {
      if (lastnl < lim)
        {
          nlscan (beg);
          totalnl = add_count (totalnl, 1);
          lastnl = lim;
        }
      print_offset (totalnl, line_num_color);
      print_sep (sep);
    }

  if (out_byte)
    {
      uintmax_t pos = add_count (totalcc, beg - bufbeg);
      print_offset (pos, byte_num_color);
      print_sep (sep);
    }

  if (align_tabs && (out_file | out_line | out_byte) && len != 0)
    putchar_errno ('\t');

  return true;
}

static char *
print_line_middle (char *beg, char *lim,
                   const char *line_color, const char *match_color)
{
  size_t match_size;
  size_t match_offset;
  char *cur;
  char *mid = NULL;
  char *b;

  for (cur = beg;
       (cur < lim
        && ((match_offset = execute (compiled_pattern, beg, lim - beg,
                                     &match_size, cur)) != (size_t) -1));
       cur = b + match_size)
    {
      b = beg + match_offset;

      /* Avoid matching the empty line at the end of the buffer. */
      if (b == lim)
        break;

      /* Avoid hanging on grep --color "" foo */
      if (match_size == 0)
        {
          /* Make minimal progress; there may be further non-empty matches.  */
          /* XXX - Could really advance by one whole multi-octet character.  */
          match_size = 1;
          if (!mid)
            mid = cur;
        }
      else
        {
          /* This function is called on a matching line only,
             but is it selected or rejected/context?  */
          if (only_matching)
            {
              char sep = out_invert ? SEP_CHAR_REJECTED : SEP_CHAR_SELECTED;
              if (! print_line_head (b, match_size, lim, sep))
                return NULL;
            }
          else
            {
              pr_sgr_start (line_color);
              if (mid)
                {
                  cur = mid;
                  mid = NULL;
                }
              fwrite_errno (cur, 1, b - cur);
            }

          pr_sgr_start_if (match_color);
          fwrite_errno (b, 1, match_size);
          pr_sgr_end_if (match_color);
          if (only_matching)
            putchar_errno (eolbyte);
        }
    }

  if (only_matching)
    cur = lim;
  else if (mid)
    cur = mid;

  return cur;
}

static char *
print_line_tail (char *beg, const char *lim, const char *line_color)
{
  size_t eol_size;
  size_t tail_size;

  eol_size   = (lim > beg && lim[-1] == eolbyte);
  eol_size  += (lim - eol_size > beg && lim[-(1 + eol_size)] == '\r');
  tail_size  =  lim - eol_size - beg;

  if (tail_size > 0)
    {
      pr_sgr_start (line_color);
      fwrite_errno (beg, 1, tail_size);
      beg += tail_size;
      pr_sgr_end (line_color);
    }

  return beg;
}

static void
prline (char *beg, char *lim, char sep)
{
  bool matching;
  const char *line_color;
  const char *match_color;

  if (!only_matching)
    if (! print_line_head (beg, lim - beg - 1, lim, sep))
      return;

  matching = (sep == SEP_CHAR_SELECTED) ^ out_invert;

  if (color_option)
    {
      line_color = (((sep == SEP_CHAR_SELECTED)
                     ^ (out_invert && (color_option < 0)))
                    ? selected_line_color  : context_line_color);
      match_color = (sep == SEP_CHAR_SELECTED
                     ? selected_match_color : context_match_color);
    }
  else
    line_color = match_color = NULL; /* Shouldn't be used.  */

  if ((only_matching && matching)
      || (color_option && (*line_color || *match_color)))
    {
      /* We already know that non-matching lines have no match (to colorize). */
      if (matching && (only_matching || *match_color))
        {
          beg = print_line_middle (beg, lim, line_color, match_color);
          if (! beg)
            return;
        }

      if (!only_matching && *line_color)
        {
          /* This code is exercised at least when grep is invoked like this:
             echo k| GREP_COLORS='sl=01;32' src/grep k --color=always  */
          beg = print_line_tail (beg, lim, line_color);
        }
    }

  if (!only_matching && lim > beg)
    fwrite_errno (beg, 1, lim - beg);

  if (line_buffered)
    fflush_errno ();

  if (stdout_errno)
    die (EXIT_TROUBLE, stdout_errno, _("write error"));

  lastout = lim;
}

/* Print pending lines of trailing context prior to LIM.  */
static void
prpending (char const *lim)
{
  if (!lastout)
    lastout = bufbeg;
  for (; 0 < pending && lastout < lim; pending--)
    {
      char *nl = memchr (lastout, eolbyte, lim - lastout);
      prline (lastout, nl + 1, SEP_CHAR_REJECTED);
    }
}

/* Output the lines between BEG and LIM.  Deal with context.  */
static void
prtext (char *beg, char *lim)
{
  static bool used;	/* Avoid printing SEP_STR_GROUP before any output.  */
  char eol = eolbyte;

  if (!out_quiet && pending > 0)
    prpending (beg);

  char *p = beg;

  if (!out_quiet)
    {
      /* Deal with leading context.  */
      char const *bp = lastout ? lastout : bufbeg;
      intmax_t i;
      for (i = 0; i < out_before; ++i)
        if (p > bp)
          do
            --p;
          while (p[-1] != eol);

      /* Print the group separator unless the output is adjacent to
         the previous output in the file.  */
      if ((0 <= out_before || 0 <= out_after) && used
          && p != lastout && group_separator)
        {
          pr_sgr_start_if (sep_color);
          fputs_errno (group_separator);
          pr_sgr_end_if (sep_color);
          putchar_errno ('\n');
        }

      while (p < beg)
        {
          char *nl = memchr (p, eol, beg - p);
          nl++;
          prline (p, nl, SEP_CHAR_REJECTED);
          p = nl;
        }
    }

  intmax_t n;
  if (out_invert)
    {
      /* One or more lines are output.  */
      for (n = 0; p < lim && n < outleft; n++)
        {
          char *nl = memchr (p, eol, lim - p);
          nl++;
          if (!out_quiet)
            prline (p, nl, SEP_CHAR_SELECTED);
          p = nl;
        }
    }
  else
    {
      /* Just one line is output.  */
      if (!out_quiet)
        prline (beg, lim, SEP_CHAR_SELECTED);
      n = 1;
      p = lim;
    }

  after_last_match = bufoffset - (buflim - p);
  pending = out_quiet ? 0 : MAX (0, out_after);
  used = true;
  outleft -= n;
}

/* Replace all NUL bytes in buffer P (which ends at LIM) with EOL.
   This avoids running out of memory when binary input contains a long
   sequence of zeros, which would otherwise be considered to be part
   of a long line.  P[LIM] should be EOL.  */
static void
zap_nuls (char *p, char *lim, char eol)
{
  if (eol)
    while (true)
      {
        *lim = '\0';
        p += strlen (p);
        *lim = eol;
        if (p == lim)
          break;
        do
          *p++ = eol;
        while (!*p);
      }
}

/* Scan the specified portion of the buffer, matching lines (or
   between matching lines if OUT_INVERT is true).  Return a count of
   lines printed.  Replace all NUL bytes with NUL_ZAPPER as we go.  */
static intmax_t
grepbuf (char *beg, char const *lim)
{
  intmax_t outleft0 = outleft;
  char *endp;

  for (char *p = beg; p < lim; p = endp)
    {
      size_t match_size;
      size_t match_offset = execute (compiled_pattern, p, lim - p,
                                     &match_size, NULL);
      if (match_offset == (size_t) -1)
        {
          if (!out_invert)
            break;
          match_offset = lim - p;
          match_size = 0;
        }
      char *b = p + match_offset;
      endp = b + match_size;
      /* Avoid matching the empty line at the end of the buffer. */
      if (!out_invert && b == lim)
        break;
      if (!out_invert || p < b)
        {
          char *prbeg = out_invert ? p : b;
          char *prend = out_invert ? b : endp;
          prtext (prbeg, prend);
          if (!outleft || done_on_match)
            {
              if (exit_on_match)
                exit (errseen ? exit_failure : EXIT_SUCCESS);
              break;
            }
        }
    }

  return outleft0 - outleft;
}

/* Search a given (non-directory) file.  Return a count of lines printed.
   Set *INEOF to true if end-of-file reached.  */
static intmax_t
grep (int fd, struct stat const *st, bool *ineof)
{
  intmax_t nlines, i;
  size_t residue, save;
  char oldc;
  char *beg;
  char *lim;
  char eol = eolbyte;
  char nul_zapper = '\0';
  bool done_on_match_0 = done_on_match;
  bool out_quiet_0 = out_quiet;

  /* The value of NLINES when nulls were first deduced in the input;
     this is not necessarily the same as the number of matching lines
     before the first null.  -1 if no input nulls have been deduced.  */
  intmax_t nlines_first_null = -1;

  if (! reset (fd, st))
    return 0;

  totalcc = 0;
  lastout = 0;
  totalnl = 0;
  outleft = max_count;
  after_last_match = 0;
  pending = 0;
  skip_nuls = skip_empty_lines && !eol;
  encoding_error_output = false;

  nlines = 0;
  residue = 0;
  save = 0;

  if (! fillbuf (save, st))
    {
      suppressible_error (errno);
      return 0;
    }

  offset_width = 0;
  if (align_tabs)
    {
      /* Width is log of maximum number.  Line numbers are origin-1.  */
      uintmax_t num = usable_st_size (st) ? st->st_size : UINTMAX_MAX;
      num += out_line && num < UINTMAX_MAX;
      do
        offset_width++;
      while ((num /= 10) != 0);
    }

  for (bool firsttime = true; ; firsttime = false)
    {
      if (nlines_first_null < 0 && eol && binary_files != TEXT_BINARY_FILES
          && (buf_has_nulls (bufbeg, buflim - bufbeg)
              || (firsttime && file_must_have_nulls (buflim - bufbeg, fd, st))))
        {
          if (binary_files == WITHOUT_MATCH_BINARY_FILES)
            return 0;
          if (!count_matches)
            done_on_match = out_quiet = true;
          nlines_first_null = nlines;
          nul_zapper = eol;
          skip_nuls = skip_empty_lines;
        }

      lastnl = bufbeg;
      if (lastout)
        lastout = bufbeg;

      beg = bufbeg + save;

      /* no more data to scan (eof) except for maybe a residue -> break */
      if (beg == buflim)
        {
          *ineof = true;
          break;
        }

      zap_nuls (beg, buflim, nul_zapper);

      /* Determine new residue (the length of an incomplete line at the end of
         the buffer, 0 means there is no incomplete last line).  */
      oldc = beg[-1];
      beg[-1] = eol;
      /* FIXME: use rawmemrchr if/when it exists, since we have ensured
         that this use of memrchr is guaranteed never to return NULL.  */
      lim = memrchr (beg - 1, eol, buflim - beg + 1);
      ++lim;
      beg[-1] = oldc;
      if (lim == beg)
        lim = beg - residue;
      beg -= residue;
      residue = buflim - lim;

      if (beg < lim)
        {
          if (outleft)
            nlines += grepbuf (beg, lim);
          if (pending)
            prpending (lim);
          if ((!outleft && !pending)
              || (done_on_match && MAX (0, nlines_first_null) < nlines))
            goto finish_grep;
        }

      /* The last OUT_BEFORE lines at the end of the buffer will be needed as
         leading context if there is a matching line at the begin of the
         next data. Make beg point to their begin.  */
      i = 0;
      beg = lim;
      while (i < out_before && beg > bufbeg && beg != lastout)
        {
          ++i;
          do
            --beg;
          while (beg[-1] != eol);
        }

      /* Detect whether leading context is adjacent to previous output.  */
      if (beg != lastout)
        lastout = 0;

      /* Handle some details and read more data to scan.  */
      save = residue + lim - beg;
      if (out_byte)
        totalcc = add_count (totalcc, buflim - bufbeg - save);
      if (out_line)
        nlscan (beg);
      if (! fillbuf (save, st))
        {
          suppressible_error (errno);
          goto finish_grep;
        }
    }
  if (residue)
    {
      *buflim++ = eol;
      if (outleft)
        nlines += grepbuf (bufbeg + save - residue, buflim);
      if (pending)
        prpending (buflim);
    }

 finish_grep:
  done_on_match = done_on_match_0;
  out_quiet = out_quiet_0;
  if (!out_quiet && (encoding_error_output
                     || (0 <= nlines_first_null && nlines_first_null < nlines)))
    {
      printf_errno (_("Binary file %s matches\n"), input_filename ());
      if (line_buffered)
        fflush_errno ();
    }
  return nlines;
}

static bool
grepdirent (FTS *fts, FTSENT *ent, bool command_line)
{
  bool follow;
  command_line &= ent->fts_level == FTS_ROOTLEVEL;

  if (ent->fts_info == FTS_DP)
    return true;

  if (!command_line
      && skipped_file (ent->fts_name, false,
                       (ent->fts_info == FTS_D || ent->fts_info == FTS_DC
                        || ent->fts_info == FTS_DNR)))
    {
      fts_set (fts, ent, FTS_SKIP);
      return true;
    }

  filename = ent->fts_path;
  if (omit_dot_slash && filename[1])
    filename += 2;
  follow = (fts->fts_options & FTS_LOGICAL
            || (fts->fts_options & FTS_COMFOLLOW && command_line));

  switch (ent->fts_info)
    {
    case FTS_D:
      if (directories == RECURSE_DIRECTORIES)
        return true;
      fts_set (fts, ent, FTS_SKIP);
      break;

    case FTS_DC:
      if (!suppress_errors)
        error (0, 0, _("warning: %s: %s"), filename,
               _("recursive directory loop"));
      return true;

    case FTS_DNR:
    case FTS_ERR:
    case FTS_NS:
      suppressible_error (ent->fts_errno);
      return true;

    case FTS_DEFAULT:
    case FTS_NSOK:
      if (skip_devices (command_line))
        {
          struct stat *st = ent->fts_statp;
          struct stat st1;
          if (! st->st_mode)
            {
              /* The file type is not already known.  Get the file status
                 before opening, since opening might have side effects
                 on a device.  */
              int flag = follow ? 0 : AT_SYMLINK_NOFOLLOW;
              if (fstatat (fts->fts_cwd_fd, ent->fts_accpath, &st1, flag) != 0)
                {
                  suppressible_error (errno);
                  return true;
                }
              st = &st1;
            }
          if (is_device_mode (st->st_mode))
            return true;
        }
      break;

    case FTS_F:
    case FTS_SLNONE:
      break;

    case FTS_SL:
    case FTS_W:
      return true;

    default:
      abort ();
    }

  return grepfile (fts->fts_cwd_fd, ent->fts_accpath, follow, command_line);
}

/* True if errno is ERR after 'open ("symlink", ... O_NOFOLLOW ...)'.
   POSIX specifies ELOOP, but it's EMLINK on FreeBSD and EFTYPE on NetBSD.  */
static bool
open_symlink_nofollow_error (int err)
{
  if (err == ELOOP || err == EMLINK)
    return true;
#ifdef EFTYPE
  if (err == EFTYPE)
    return true;
#endif
  return false;
}

static bool
grepfile (int dirdesc, char const *name, bool follow, bool command_line)
{
  int oflag = (O_RDONLY | O_NOCTTY
               | (IGNORE_DUPLICATE_BRANCH_WARNING
                  (binary ? O_BINARY : 0))
               | (follow ? 0 : O_NOFOLLOW)
               | (skip_devices (command_line) ? O_NONBLOCK : 0));
  int desc = openat_safer (dirdesc, name, oflag);
  if (desc < 0)
    {
      if (follow || ! open_symlink_nofollow_error (errno))
        suppressible_error (errno);
      return true;
    }
  return grepdesc (desc, command_line);
}

/* Read all data from FD, with status ST.  Return true if successful,
   false (setting errno) otherwise.  */
static bool
drain_input (int fd, struct stat const *st)
{
  ssize_t nbytes;
  if (S_ISFIFO (st->st_mode) && dev_null_output)
    {
#ifdef SPLICE_F_MOVE
      /* Should be faster, since it need not copy data to user space.  */
      nbytes = splice (fd, NULL, STDOUT_FILENO, NULL,
                       INITIAL_BUFSIZE, SPLICE_F_MOVE);
      if (0 <= nbytes || errno != EINVAL)
        {
          while (0 < nbytes)
            nbytes = splice (fd, NULL, STDOUT_FILENO, NULL,
                             INITIAL_BUFSIZE, SPLICE_F_MOVE);
          return nbytes == 0;
        }
#endif
    }
  while ((nbytes = safe_read (fd, buffer, bufalloc)))
    if (nbytes == SAFE_READ_ERROR)
      return false;
  return true;
}

/* Finish reading from FD, with status ST and where end-of-file has
   been seen if INEOF.  Typically this is a no-op, but when reading
   from standard input this may adjust the file offset or drain a
   pipe.  */

static void
finalize_input (int fd, struct stat const *st, bool ineof)
{
  if (fd == STDIN_FILENO
      && (outleft
          ? (!ineof
             && (seek_failed
                 || (lseek (fd, 0, SEEK_END) < 0
                     /* Linux proc file system has EINVAL (Bug#25180).  */
                     && errno != EINVAL))
             && ! drain_input (fd, st))
          : (bufoffset != after_last_match && !seek_failed
             && lseek (fd, after_last_match, SEEK_SET) < 0)))
    suppressible_error (errno);
}

static bool
grepdesc (int desc, bool command_line)
{
  intmax_t count;
  bool status = true;
  bool ineof = false;
  struct stat st;

  /* Get the file status, possibly for the second time.  This catches
     a race condition if the directory entry changes after the
     directory entry is read and before the file is opened.  For
     example, normally DESC is a directory only at the top level, but
     there is an exception if some other process substitutes a
     directory for a non-directory while 'grep' is running.  */
  if (fstat (desc, &st) != 0)
    {
      suppressible_error (errno);
      goto closeout;
    }

  if (desc != STDIN_FILENO && skip_devices (command_line)
      && is_device_mode (st.st_mode))
    goto closeout;

  if (desc != STDIN_FILENO && command_line
      && skipped_file (filename, true, S_ISDIR (st.st_mode) != 0))
    goto closeout;

  /* Don't output file names if invoked as 'grep -r PATTERN NONDIRECTORY'.  */
  if (out_file < 0)
    out_file = !!S_ISDIR (st.st_mode);

  if (desc != STDIN_FILENO
      && directories == RECURSE_DIRECTORIES && S_ISDIR (st.st_mode))
    {
      /* Traverse the directory starting with its full name, because
         unfortunately fts provides no way to traverse the directory
         starting from its file descriptor.  */

      FTS *fts;
      FTSENT *ent;
      int opts = fts_options & ~(command_line ? 0 : FTS_COMFOLLOW);
      char *fts_arg[2];

      /* Close DESC now, to conserve file descriptors if the race
         condition occurs many times in a deep recursion.  */
      if (close (desc) != 0)
        suppressible_error (errno);

      fts_arg[0] = (char *) filename;
      fts_arg[1] = NULL;
      fts = fts_open (fts_arg, opts, NULL);

      if (!fts)
        xalloc_die ();
      while ((ent = fts_read (fts)))
        status &= grepdirent (fts, ent, command_line);
      if (errno)
        suppressible_error (errno);
      if (fts_close (fts) != 0)
        suppressible_error (errno);
      return status;
    }
  if (desc != STDIN_FILENO
      && ((directories == SKIP_DIRECTORIES && S_ISDIR (st.st_mode))
          || ((devices == SKIP_DEVICES
               || (devices == READ_COMMAND_LINE_DEVICES && !command_line))
              && is_device_mode (st.st_mode))))
    goto closeout;

  /* If there is a regular file on stdout and the current file refers
     to the same i-node, we have to report the problem and skip it.
     Otherwise when matching lines from some other input reach the
     disk before we open this file, we can end up reading and matching
     those lines and appending them to the file from which we're reading.
     Then we'd have what appears to be an infinite loop that'd terminate
     only upon filling the output file system or reaching a quota.
     However, there is no risk of an infinite loop if grep is generating
     no output, i.e., with --silent, --quiet, -q.
     Similarly, with any of these:
       --max-count=N (-m) (for N >= 2)
       --files-with-matches (-l)
       --files-without-match (-L)
     there is no risk of trouble.
     For --max-count=1, grep stops after printing the first match,
     so there is no risk of malfunction.  But even --max-count=2, with
     input==output, while there is no risk of infloop, there is a race
     condition that could result in "alternate" output.  */
  if (!out_quiet && list_files == LISTFILES_NONE && 1 < max_count
      && S_ISREG (st.st_mode) && SAME_INODE (st, out_stat))
    {
      if (! suppress_errors)
        error (0, 0, _("input file %s is also the output"),
               quote (input_filename ()));
      errseen = true;
      goto closeout;
    }

  count = grep (desc, &st, &ineof);
  if (count_matches)
    {
      if (out_file)
        {
          print_filename ();
          if (filename_mask)
            print_sep (SEP_CHAR_SELECTED);
          else
            putchar_errno (0);
        }
      printf_errno ("%" PRIdMAX "\n", count);
      if (line_buffered)
        fflush_errno ();
    }

  status = !count == !(list_files == LISTFILES_NONMATCHING);

  if (list_files == LISTFILES_NONE || dev_null_output)
    finalize_input (desc, &st, ineof);
  else if (status == 0)
    {
      print_filename ();
      putchar_errno ('\n' & filename_mask);
      if (line_buffered)
        fflush_errno ();
    }

 closeout:
  if (desc != STDIN_FILENO && close (desc) != 0)
    suppressible_error (errno);
  return status;
}

static bool
grep_command_line_arg (char const *arg)
{
  if (STREQ (arg, "-"))
    {
      filename = label;
      if (binary)
        xset_binary_mode (STDIN_FILENO, O_BINARY);
      return grepdesc (STDIN_FILENO, true);
    }
  else
    {
      filename = arg;
      return grepfile (AT_FDCWD, arg, true, true);
    }
}

_Noreturn void usage (int);
void
usage (int status)
{
  if (status != 0)
    {
      fprintf (stderr, _("Usage: %s [OPTION]... PATTERNS [FILE]...\n"),
               getprogname ());
      fprintf (stderr, _("Try '%s --help' for more information.\n"),
               getprogname ());
    }
  else
    {
      printf (_("Usage: %s [OPTION]... PATTERNS [FILE]...\n"), getprogname ());
      printf (_("Search for PATTERNS in each FILE.\n"));
      printf (_("\
Example: %s -i 'hello world' menu.h main.c\n\
PATTERNS can contain multiple patterns separated by newlines.\n\
\n\
Pattern selection and interpretation:\n"), getprogname ());
      printf (_("\
  -E, --extended-regexp     PATTERNS are extended regular expressions\n\
  -F, --fixed-strings       PATTERNS are strings\n\
  -G, --basic-regexp        PATTERNS are basic regular expressions\n\
  -P, --perl-regexp         PATTERNS are Perl regular expressions\n"));
  /* -X is deliberately undocumented.  */
      printf (_("\
  -e, --regexp=PATTERNS     use PATTERNS for matching\n\
  -f, --file=FILE           take PATTERNS from FILE\n\
  -i, --ignore-case         ignore case distinctions in patterns and data\n\
      --no-ignore-case      do not ignore case distinctions (default)\n\
  -w, --word-regexp         match only whole words\n\
  -x, --line-regexp         match only whole lines\n\
  -z, --null-data           a data line ends in 0 byte, not newline\n"));
      printf (_("\
\n\
Miscellaneous:\n\
  -s, --no-messages         suppress error messages\n\
  -v, --invert-match        select non-matching lines\n\
  -V, --version             display version information and exit\n\
      --help                display this help text and exit\n"));
      printf (_("\
\n\
Output control:\n\
  -m, --max-count=NUM       stop after NUM selected lines\n\
  -b, --byte-offset         print the byte offset with output lines\n\
  -n, --line-number         print line number with output lines\n\
      --line-buffered       flush output on every line\n\
  -H, --with-filename       print file name with output lines\n\
  -h, --no-filename         suppress the file name prefix on output\n\
      --label=LABEL         use LABEL as the standard input file name prefix\n\
"));
      printf (_("\
  -o, --only-matching       show only nonempty parts of lines that match\n\
  -q, --quiet, --silent     suppress all normal output\n\
      --binary-files=TYPE   assume that binary files are TYPE;\n\
                            TYPE is 'binary', 'text', or 'without-match'\n\
  -a, --text                equivalent to --binary-files=text\n\
"));
      printf (_("\
  -I                        equivalent to --binary-files=without-match\n\
  -d, --directories=ACTION  how to handle directories;\n\
                            ACTION is 'read', 'recurse', or 'skip'\n\
  -D, --devices=ACTION      how to handle devices, FIFOs and sockets;\n\
                            ACTION is 'read' or 'skip'\n\
  -r, --recursive           like --directories=recurse\n\
  -R, --dereference-recursive  likewise, but follow all symlinks\n\
"));
      printf (_("\
      --include=GLOB        search only files that match GLOB (a file pattern)"
                "\n\
      --exclude=GLOB        skip files that match GLOB\n\
      --exclude-from=FILE   skip files that match any file pattern from FILE\n\
      --exclude-dir=GLOB    skip directories that match GLOB\n\
"));
      printf (_("\
  -L, --files-without-match  print only names of FILEs with no selected lines\n\
  -l, --files-with-matches  print only names of FILEs with selected lines\n\
  -c, --count               print only a count of selected lines per FILE\n\
  -T, --initial-tab         make tabs line up (if needed)\n\
  -Z, --null                print 0 byte after FILE name\n"));
      printf (_("\
\n\
Context control:\n\
  -B, --before-context=NUM  print NUM lines of leading context\n\
  -A, --after-context=NUM   print NUM lines of trailing context\n\
  -C, --context=NUM         print NUM lines of output context\n\
"));
      printf (_("\
  -NUM                      same as --context=NUM\n\
      --color[=WHEN],\n\
      --colour[=WHEN]       use markers to highlight the matching strings;\n\
                            WHEN is 'always', 'never', or 'auto'\n\
  -U, --binary              do not strip CR characters at EOL (MSDOS/Windows)\n\
\n"));
      printf (_("\
When FILE is '-', read standard input.  With no FILE, read '.' if\n\
recursive, '-' otherwise.  With fewer than two FILEs, assume -h.\n\
Exit status is 0 if any line (or file if -L) is selected, 1 otherwise;\n\
if any error occurs and -q is not given, the exit status is 2.\n"));
      emit_bug_reporting_address ();
    }
  exit (status);
}

/* Pattern compilers and matchers.  */

static struct
{
  char name[12];
  int syntax; /* used if compile == GEAcompile */
  compile_fp_t compile;
  execute_fp_t execute;
} const matchers[] = {
  { "grep", RE_SYNTAX_GREP, GEAcompile, EGexecute },
  { "egrep", RE_SYNTAX_EGREP, GEAcompile, EGexecute },
  { "fgrep", 0, Fcompile, Fexecute, },
  { "awk", RE_SYNTAX_AWK, GEAcompile, EGexecute },
  { "gawk", RE_SYNTAX_GNU_AWK, GEAcompile, EGexecute },
  { "posixawk", RE_SYNTAX_POSIX_AWK, GEAcompile, EGexecute },
#if HAVE_LIBPCRE
  { "perl", 0, Pcompile, Pexecute, },
#endif
};
/* Keep these in sync with the 'matchers' table.  */
enum { E_MATCHER_INDEX = 1, F_MATCHER_INDEX = 2, G_MATCHER_INDEX = 0 };

/* Return the index of the matcher corresponding to M if available.
   MATCHER is the index of the previous matcher, or -1 if none.
   Exit in case of conflicts or if M is not available.  */
static int
setmatcher (char const *m, int matcher)
{
  for (int i = 0; i < sizeof matchers / sizeof *matchers; i++)
    if (STREQ (m, matchers[i].name))
      {
        if (0 <= matcher && matcher != i)
          die (EXIT_TROUBLE, 0, _("conflicting matchers specified"));
        return i;
      }

#if !HAVE_LIBPCRE
  if (STREQ (m, "perl"))
    die (EXIT_TROUBLE, 0,
         _("Perl matching not supported in a --disable-perl-regexp build"));
#endif
  die (EXIT_TROUBLE, 0, _("invalid matcher %s"), m);
}

/* Find the white-space-separated options specified by OPTIONS, and
   using BUF to store copies of these options, set ARGV[0], ARGV[1],
   etc. to the option copies.  Return the number N of options found.
   Do not set ARGV[N] to NULL.  If ARGV is NULL, do not store ARGV[0]
   etc.  Backslash can be used to escape whitespace (and backslashes).  */
static size_t
prepend_args (char const *options, char *buf, char **argv)
{
  char const *o = options;
  char *b = buf;
  size_t n = 0;

  for (;;)
    {
      while (c_isspace (to_uchar (*o)))
        o++;
      if (!*o)
        return n;
      if (argv)
        argv[n] = b;
      n++;

      do
        if ((*b++ = *o++) == '\\' && *o)
          b[-1] = *o++;
      while (*o && ! c_isspace (to_uchar (*o)));

      *b++ = '\0';
    }
}

/* Prepend the whitespace-separated options in OPTIONS to the argument
   vector of a main program with argument count *PARGC and argument
   vector *PARGV.  Return the number of options prepended.  */
static int
prepend_default_options (char const *options, int *pargc, char ***pargv)
{
  if (options && *options)
    {
      char *buf = xmalloc (strlen (options) + 1);
      size_t prepended = prepend_args (options, buf, NULL);
      int argc = *pargc;
      char *const *argv = *pargv;
      char **pp;
      enum { MAX_ARGS = MIN (INT_MAX, SIZE_MAX / sizeof *pp - 1) };
      if (MAX_ARGS - argc < prepended)
        xalloc_die ();
      pp = xmalloc ((prepended + argc + 1) * sizeof *pp);
      *pargc = prepended + argc;
      *pargv = pp;
      *pp++ = *argv++;
      pp += prepend_args (options, buf, pp);
      while ((*pp++ = *argv++))
        continue;
      return prepended;
    }

  return 0;
}

/* Get the next non-digit option from ARGC and ARGV.
   Return -1 if there are no more options.
   Process any digit options that were encountered on the way,
   and store the resulting integer into *DEFAULT_CONTEXT.  */
static int
get_nondigit_option (int argc, char *const *argv, intmax_t *default_context)
{
  static int prev_digit_optind = -1;
  int this_digit_optind;
  bool was_digit;
  char buf[INT_BUFSIZE_BOUND (intmax_t) + 4];
  char *p = buf;
  int opt;

  was_digit = false;
  this_digit_optind = optind;
  while (true)
    {
      opt = getopt_long (argc, (char **) argv, short_options,
                         long_options, NULL);
      if (! c_isdigit (opt))
        break;

      if (prev_digit_optind != this_digit_optind || !was_digit)
        {
          /* Reset to start another context length argument.  */
          p = buf;
        }
      else
        {
          /* Suppress trivial leading zeros, to avoid incorrect
             diagnostic on strings like 00000000000.  */
          p -= buf[0] == '0';
        }

      if (p == buf + sizeof buf - 4)
        {
          /* Too many digits.  Append "..." to make context_length_arg
             complain about "X...", where X contains the digits seen
             so far.  */
          strcpy (p, "...");
          p += 3;
          break;
        }
      *p++ = opt;

      was_digit = true;
      prev_digit_optind = this_digit_optind;
      this_digit_optind = optind;
    }
  if (p != buf)
    {
      *p = '\0';
      context_length_arg (buf, default_context);
    }

  return opt;
}

/* Parse GREP_COLORS.  The default would look like:
     GREP_COLORS='ms=01;31:mc=01;31:sl=:cx=:fn=35:ln=32:bn=32:se=36'
   with boolean capabilities (ne and rv) unset (i.e., omitted).
   No character escaping is needed or supported.  */
static void
parse_grep_colors (void)
{
  const char *p;
  char *q;
  char *name;
  char *val;

  p = getenv ("GREP_COLORS"); /* Plural! */
  if (p == NULL || *p == '\0')
    return;

  /* Work off a writable copy.  */
  q = xstrdup (p);

  name = q;
  val = NULL;
  /* From now on, be well-formed or you're gone.  */
  for (;;)
    if (*q == ':' || *q == '\0')
      {
        char c = *q;
        struct color_cap const *cap;

        *q++ = '\0'; /* Terminate name or val.  */
        /* Empty name without val (empty cap)
         * won't match and will be ignored.  */
        for (cap = color_dict; cap->name; cap++)
          if (STREQ (cap->name, name))
            break;
        /* If name unknown, go on for forward compatibility.  */
        if (cap->var && val)
          *(cap->var) = val;
        if (cap->fct)
          cap->fct ();
        if (c == '\0')
          return;
        name = q;
        val = NULL;
      }
    else if (*q == '=')
      {
        if (q == name || val)
          return;
        *q++ = '\0'; /* Terminate name.  */
        val = q; /* Can be the empty string.  */
      }
    else if (val == NULL)
      q++; /* Accumulate name.  */
    else if (*q == ';' || c_isdigit (*q))
      q++; /* Accumulate val.  Protect the terminal from being sent crap.  */
    else
      return;
}

/* Return true if PAT (of length PATLEN) contains an encoding error.  */
static bool
contains_encoding_error (char const *pat, size_t patlen)
{
  mbstate_t mbs = { 0 };
  size_t i, charlen;

  for (i = 0; i < patlen; i += charlen)
    {
      charlen = mb_clen (pat + i, patlen - i, &mbs);
      if ((size_t) -2 <= charlen)
        return true;
    }
  return false;
}

/* Return the number of bytes in the initial character of PAT, of size
   PATLEN, if Fcompile can handle that character.  Return -1 if
   Fcompile cannot handle it.  MBS is the multibyte conversion state.

   Fcompile can handle a character C if C is single-byte, or if C has no
   case folded counterparts and toupper translates none of its bytes.  */

static int
fgrep_icase_charlen (char const *pat, size_t patlen, mbstate_t *mbs)
{
  int n = localeinfo.sbclen[to_uchar (*pat)];
  if (n < 0)
    {
      wchar_t wc;
      wchar_t folded[CASE_FOLDED_BUFSIZE];
      size_t wn = mbrtowc (&wc, pat, patlen, mbs);
      if (MB_LEN_MAX < wn || case_folded_counterparts (wc, folded))
        return -1;
      for (int i = wn; 0 < --i; )
        {
          unsigned char c = pat[i];
          if (toupper (c) != c)
            return -1;
        }
      n = wn;
    }
  return n;
}

/* Return true if the -F patterns PAT, of size PATLEN, contain only
   single-byte characters or characters not subject to case folding,
   and so can be processed by Fcompile.  */

static bool
fgrep_icase_available (char const *pat, size_t patlen)
{
  mbstate_t mbs = {0,};

  for (size_t i = 0; i < patlen; )
    {
      int n = fgrep_icase_charlen (pat + i, patlen - i, &mbs);
      if (n < 0)
        return false;
      i += n;
    }

  return true;
}

/* Change the pattern *KEYS_P, of size *LEN_P, from fgrep to grep style.  */

void
fgrep_to_grep_pattern (char **keys_p, size_t *len_p)
{
  size_t len = *len_p;
  char *keys = *keys_p;
  mbstate_t mb_state = { 0 };
  char *new_keys = xnmalloc (len + 1, 2);
  char *p = new_keys;
  size_t n;

  for (; len; keys += n, len -= n)
    {
      n = mb_clen (keys, len, &mb_state);
      switch (n)
        {
        case (size_t) -2:
          n = len;
          FALLTHROUGH;
        default:
          p = mempcpy (p, keys, n);
          break;

        case (size_t) -1:
          memset (&mb_state, 0, sizeof mb_state);
          n = 1;
          FALLTHROUGH;
        case 1:
          switch (*keys)
            {
            case '$': case '*': case '.': case '[': case '\\': case '^':
              *p++ = '\\'; break;
            }
          *p++ = *keys;
          break;
        }
    }

  free (*keys_p);
  *keys_p = new_keys;
  *len_p = p - new_keys;
}

/* If it is easy, convert the MATCHER-style patterns KEYS (of size
   *LEN_P) to -F style, update *LEN_P to a possibly-smaller value, and
   return F_MATCHER_INDEX.  If not, leave KEYS and *LEN_P alone and
   return MATCHER.  This function is conservative and sometimes misses
   conversions, e.g., it does not convert the -E pattern "(a|a|[aa])"
   to the -F pattern "a".  */

static int
try_fgrep_pattern (int matcher, char *keys, size_t *len_p)
{
  int result = matcher;
  size_t len = *len_p;
  char *new_keys = xmalloc (len + 1);
  char *p = new_keys;
  char const *q = keys;
  mbstate_t mb_state = { 0 };

  while (len != 0)
    {
      switch (*q)
        {
        case '$': case '*': case '.': case '[': case '^':
          goto fail;

        case '(': case '+': case '?': case '{': case '|':
          if (matcher != G_MATCHER_INDEX)
            goto fail;
          break;

        case '\\':
          if (1 < len)
            switch (q[1])
              {
              case '\n':
              case 'B': case 'S': case 'W': case'\'': case '<':
              case 'b': case 's': case 'w': case '`': case '>':
              case '1': case '2': case '3': case '4':
              case '5': case '6': case '7': case '8': case '9':
                goto fail;

              case '(': case '+': case '?': case '{': case '|':
                if (matcher == G_MATCHER_INDEX)
                  goto fail;
                FALLTHROUGH;
              default:
                q++, len--;
                break;
              }
          break;
        }

      {
        size_t n;
        if (match_icase)
          {
            int ni = fgrep_icase_charlen (q, len, &mb_state);
            if (ni < 0)
              goto fail;
            n = ni;
          }
        else
          {
            n = mb_clen (q, len, &mb_state);
            if (MB_LEN_MAX < n)
              goto fail;
          }

        p = mempcpy (p, q, n);
        q += n;
        len -= n;
      }
    }

  if (*len_p != p - new_keys)
    {
      *len_p = p - new_keys;
      memcpy (keys, new_keys, p - new_keys);
    }
  result = F_MATCHER_INDEX;

 fail:
  free (new_keys);
  return result;
}

int
main (int argc, char **argv)
{
  char *keys = NULL;
  size_t keycc = 0, oldcc, keyalloc = 0;
  int matcher = -1;
  size_t cc;
  int opt, prepended;
  int prev_optind, last_recursive;
  int fread_errno;
  intmax_t default_context;
  FILE *fp;
  exit_failure = EXIT_TROUBLE;
  initialize_main (&argc, &argv);

  /* Which command-line options have been specified for filename output.
     -1 for -h, 1 for -H, 0 for neither.  */
  int filename_option = 0;

  eolbyte = '\n';
  filename_mask = ~0;

  max_count = INTMAX_MAX;

  /* The value -1 means to use DEFAULT_CONTEXT. */
  out_after = out_before = -1;
  /* Default before/after context: changed by -C/-NUM options */
  default_context = -1;
  /* Changed by -o option */
  only_matching = false;

  /* Internationalization. */
#if defined HAVE_SETLOCALE
  setlocale (LC_ALL, "");
#endif
#if defined ENABLE_NLS
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);
#endif

  init_localeinfo (&localeinfo);

  atexit (clean_up_stdout);
  c_stack_action (NULL);

  last_recursive = 0;

  prepended = prepend_default_options (getenv ("GREP_OPTIONS"), &argc, &argv);
  if (prepended)
    error (0, 0, _("warning: GREP_OPTIONS is deprecated;"
                   " please use an alias or script"));

  while (prev_optind = optind,
         (opt = get_nondigit_option (argc, argv, &default_context)) != -1)
    switch (opt)
      {
      case 'A':
        context_length_arg (optarg, &out_after);
        break;

      case 'B':
        context_length_arg (optarg, &out_before);
        break;

      case 'C':
        /* Set output match context, but let any explicit leading or
           trailing amount specified with -A or -B stand. */
        context_length_arg (optarg, &default_context);
        break;

      case 'D':
        if (STREQ (optarg, "read"))
          devices = READ_DEVICES;
        else if (STREQ (optarg, "skip"))
          devices = SKIP_DEVICES;
        else
          die (EXIT_TROUBLE, 0, _("unknown devices method"));
        break;

      case 'E':
        matcher = setmatcher ("egrep", matcher);
        break;

      case 'F':
        matcher = setmatcher ("fgrep", matcher);
        break;

      case 'P':
        matcher = setmatcher ("perl", matcher);
        break;

      case 'G':
        matcher = setmatcher ("grep", matcher);
        break;

      case 'X': /* undocumented on purpose */
        matcher = setmatcher (optarg, matcher);
        break;

      case 'H':
        filename_option = 1;
        break;

      case 'I':
        binary_files = WITHOUT_MATCH_BINARY_FILES;
        break;

      case 'T':
        align_tabs = true;
        break;

      case 'U':
        if (O_BINARY)
          binary = true;
        break;

      case 'u':
        /* Obsolete option; it has no effect.  FIXME: Diagnose use of
           this option starting in (say) the year 2020.  */
        break;

      case 'V':
        show_version = true;
        break;

      case 'a':
        binary_files = TEXT_BINARY_FILES;
        break;

      case 'b':
        out_byte = true;
        break;

      case 'c':
        count_matches = true;
        break;

      case 'd':
        directories = XARGMATCH ("--directories", optarg,
                                 directories_args, directories_types);
        if (directories == RECURSE_DIRECTORIES)
          last_recursive = prev_optind;
        break;

      case 'e':
        cc = strlen (optarg);
        if (keyalloc < keycc + cc + 1)
          {
            keyalloc = keycc + cc + 1;
            keys = x2realloc (keys, &keyalloc);
          }
        oldcc = keycc;
        memcpy (keys + oldcc, optarg, cc);
        keycc += cc;
        keys[keycc++] = '\n';
        fl_add (keys + oldcc, cc + 1, "");
        break;

      case 'f':
        if (STREQ (optarg, "-"))
          {
            if (binary)
              xset_binary_mode (STDIN_FILENO, O_BINARY);
            fp = stdin;
          }
        else
          {
            fp = fopen (optarg, binary ? "rb" : "r");
            if (!fp)
              die (EXIT_TROUBLE, errno, "%s", optarg);
          }
        oldcc = keycc;
        for (;; keycc += cc)
          {
            if (keyalloc <= keycc + 1)
              keys = x2realloc (keys, &keyalloc);
            cc = fread (keys + keycc, 1, keyalloc - (keycc + 1), fp);
            if (cc == 0)
              break;
          }
        fread_errno = errno;
        if (ferror (fp))
          die (EXIT_TROUBLE, fread_errno, "%s", optarg);
        if (fp != stdin)
          fclose (fp);
        /* Append final newline if file ended in non-newline. */
        if (oldcc != keycc && keys[keycc - 1] != '\n')
          keys[keycc++] = '\n';
        fl_add (keys + oldcc, keycc - oldcc, optarg);
        break;

      case 'h':
        filename_option = -1;
        break;

      case 'i':
      case 'y':			/* For old-timers . . . */
        match_icase = true;
        break;

      case NO_IGNORE_CASE_OPTION:
        match_icase = false;
        break;

      case 'L':
        /* Like -l, except list files that don't contain matches.
           Inspired by the same option in Hume's gre. */
        list_files = LISTFILES_NONMATCHING;
        break;

      case 'l':
        list_files = LISTFILES_MATCHING;
        break;

      case 'm':
        switch (xstrtoimax (optarg, 0, 10, &max_count, ""))
          {
          case LONGINT_OK:
          case LONGINT_OVERFLOW:
            break;

          default:
            die (EXIT_TROUBLE, 0, _("invalid max count"));
          }
        break;

      case 'n':
        out_line = true;
        break;

      case 'o':
        only_matching = true;
        break;

      case 'q':
        exit_on_match = true;
        exit_failure = 0;
        break;

      case 'R':
        fts_options = basic_fts_options | FTS_LOGICAL;
        FALLTHROUGH;
      case 'r':
        directories = RECURSE_DIRECTORIES;
        last_recursive = prev_optind;
        break;

      case 's':
        suppress_errors = true;
        break;

      case 'v':
        out_invert = true;
        break;

      case 'w':
        wordinit ();
        match_words = true;
        break;

      case 'x':
        match_lines = true;
        break;

      case 'Z':
        filename_mask = 0;
        break;

      case 'z':
        eolbyte = '\0';
        break;

      case BINARY_FILES_OPTION:
        if (STREQ (optarg, "binary"))
          binary_files = BINARY_BINARY_FILES;
        else if (STREQ (optarg, "text"))
          binary_files = TEXT_BINARY_FILES;
        else if (STREQ (optarg, "without-match"))
          binary_files = WITHOUT_MATCH_BINARY_FILES;
        else
          die (EXIT_TROUBLE, 0, _("unknown binary-files type"));
        break;

      case COLOR_OPTION:
        if (optarg)
          {
            if (!c_strcasecmp (optarg, "always")
                || !c_strcasecmp (optarg, "yes")
                || !c_strcasecmp (optarg, "force"))
              color_option = 1;
            else if (!c_strcasecmp (optarg, "never")
                     || !c_strcasecmp (optarg, "no")
                     || !c_strcasecmp (optarg, "none"))
              color_option = 0;
            else if (!c_strcasecmp (optarg, "auto")
                     || !c_strcasecmp (optarg, "tty")
                     || !c_strcasecmp (optarg, "if-tty"))
              color_option = 2;
            else
              show_help = 1;
          }
        else
          color_option = 2;
        break;

      case EXCLUDE_OPTION:
      case INCLUDE_OPTION:
        for (int cmd = 0; cmd < 2; cmd++)
          {
            if (!excluded_patterns[cmd])
              excluded_patterns[cmd] = new_exclude ();
            add_exclude (excluded_patterns[cmd], optarg,
                         ((opt == INCLUDE_OPTION ? EXCLUDE_INCLUDE : 0)
                          | exclude_options (cmd)));
          }
        break;
      case EXCLUDE_FROM_OPTION:
        for (int cmd = 0; cmd < 2; cmd++)
          {
            if (!excluded_patterns[cmd])
              excluded_patterns[cmd] = new_exclude ();
            if (add_exclude_file (add_exclude, excluded_patterns[cmd],
                                  optarg, exclude_options (cmd), '\n')
                != 0)
              die (EXIT_TROUBLE, errno, "%s", optarg);
          }
        break;

      case EXCLUDE_DIRECTORY_OPTION:
        strip_trailing_slashes (optarg);
        for (int cmd = 0; cmd < 2; cmd++)
          {
            if (!excluded_directory_patterns[cmd])
              excluded_directory_patterns[cmd] = new_exclude ();
            add_exclude (excluded_directory_patterns[cmd], optarg,
                         exclude_options (cmd));
          }
        break;

      case GROUP_SEPARATOR_OPTION:
        group_separator = optarg;
        break;

      case LINE_BUFFERED_OPTION:
        line_buffered = true;
        break;

      case LABEL_OPTION:
        label = optarg;
        break;

      case 0:
        /* long options */
        break;

      default:
        usage (EXIT_TROUBLE);
        break;

      }

  if (show_version)
    {
      version_etc (stdout, getprogname (), PACKAGE_NAME, VERSION,
                   (char *) NULL);
      puts (_("Written by Mike Haertel and others; see\n"
              "<https://git.sv.gnu.org/cgit/grep.git/tree/AUTHORS>."));
      return EXIT_SUCCESS;
    }

  if (show_help)
    usage (EXIT_SUCCESS);

  if (keys)
    {
      if (keycc == 0)
        {
          /* No keys were specified (e.g. -f /dev/null).  Match nothing.  */
          out_invert ^= true;
          match_lines = match_words = false;
        }
      else
        /* Strip trailing newline. */
        --keycc;
    }
  else if (optind < argc)
    {
      /* Make a copy so that it can be reallocated or freed later.  */
      keycc = strlen (argv[optind]);
      keys = xmemdup (argv[optind++], keycc + 1);
      fl_add (keys, keycc, "");
      n_patterns++;
    }
  else
    usage (EXIT_TROUBLE);

  bool possibly_tty = false;
  struct stat tmp_stat;
  if (! exit_on_match && fstat (STDOUT_FILENO, &tmp_stat) == 0)
    {
      if (S_ISREG (tmp_stat.st_mode))
        out_stat = tmp_stat;
      else if (S_ISCHR (tmp_stat.st_mode))
        {
          struct stat null_stat;
          if (stat ("/dev/null", &null_stat) == 0
              && SAME_INODE (tmp_stat, null_stat))
            dev_null_output = true;
          else
            possibly_tty = true;
        }
    }

  /* POSIX says -c, -l and -q are mutually exclusive.  In this
     implementation, -q overrides -l and -L, which in turn override -c.  */
  if (exit_on_match)
    list_files = LISTFILES_NONE;
  if ((exit_on_match | dev_null_output) || list_files != LISTFILES_NONE)
    {
      count_matches = false;
      done_on_match = true;
    }
  out_quiet = count_matches | done_on_match;

  if (out_after < 0)
    out_after = default_context;
  if (out_before < 0)
    out_before = default_context;

  /* If it is easy to see that matching cannot succeed (e.g., 'grep -f
     /dev/null'), fail without reading the input.  */
  if ((max_count == 0
       || (keycc == 0 && out_invert && !match_lines && !match_words))
      && list_files != LISTFILES_NONMATCHING)
    return EXIT_FAILURE;

  if (color_option == 2)
    color_option = possibly_tty && should_colorize () && isatty (STDOUT_FILENO);
  init_colorize ();

  if (color_option)
    {
      /* Legacy.  */
      char *userval = getenv ("GREP_COLOR");
      if (userval != NULL && *userval != '\0')
        selected_match_color = context_match_color = userval;

      /* New GREP_COLORS has priority.  */
      parse_grep_colors ();
    }

  initialize_unibyte_mask ();

  if (matcher < 0)
    matcher = G_MATCHER_INDEX;

  /* In a single-byte locale, switch from -F to -G if it is a single
     pattern that matches words, where -G is typically faster.  In a
     multi-byte locale, switch if the patterns have an encoding error
     (where -F does not work) or if -i and the patterns will not work
     for -iF.  */
  if (matcher == F_MATCHER_INDEX
      && (! localeinfo.multibyte
          ? n_patterns == 1 && match_words
          : (contains_encoding_error (keys, keycc)
             || (match_icase && !fgrep_icase_available (keys, keycc)))))
    {
      fgrep_to_grep_pattern (&keys, &keycc);
      matcher = G_MATCHER_INDEX;
    }
  /* With two or more patterns, if -F works then switch from either -E
     or -G, as -F is probably faster then.  */
  else if ((matcher == G_MATCHER_INDEX || matcher == E_MATCHER_INDEX)
           && 1 < n_patterns)
    matcher = try_fgrep_pattern (matcher, keys, &keycc);

  execute = matchers[matcher].execute;
  compiled_pattern = matchers[matcher].compile (keys, keycc,
                                                matchers[matcher].syntax);
  /* We need one byte prior and one after.  */
  char eolbytes[3] = { 0, eolbyte, 0 };
  size_t match_size;
  skip_empty_lines = ((execute (compiled_pattern, eolbytes + 1, 1,
                                &match_size, NULL) == 0)
                      == out_invert);

  int num_operands = argc - optind;
  out_file = (filename_option == 0 && num_operands <= 1
              ? - (directories == RECURSE_DIRECTORIES)
              : 0 <= filename_option);

  if (binary)
    xset_binary_mode (STDOUT_FILENO, O_BINARY);

  /* Prefer sysconf for page size, as getpagesize typically returns int.  */
#ifdef _SC_PAGESIZE
  long psize = sysconf (_SC_PAGESIZE);
#else
  long psize = getpagesize ();
#endif
  if (! (0 < psize && psize <= (SIZE_MAX - sizeof (uword)) / 2))
    abort ();
  pagesize = psize;
  bufalloc = ALIGN_TO (INITIAL_BUFSIZE, pagesize) + pagesize + sizeof (uword);
  buffer = xmalloc (bufalloc);

  if (fts_options & FTS_LOGICAL && devices == READ_COMMAND_LINE_DEVICES)
    devices = READ_DEVICES;

  char *const *files;
  if (0 < num_operands)
    {
      files = argv + optind;
    }
  else if (directories == RECURSE_DIRECTORIES && prepended < last_recursive)
    {
      static char *const cwd_only[] = { (char *) ".", NULL };
      files = cwd_only;
      omit_dot_slash = true;
    }
  else
    {
      static char *const stdin_only[] = { (char *) "-", NULL };
      files = stdin_only;
    }

  bool status = true;
  do
    status &= grep_command_line_arg (*files++);
  while (*files != NULL);

  /* We register via atexit to test stdout.  */
  return errseen ? EXIT_TROUBLE : status;
}

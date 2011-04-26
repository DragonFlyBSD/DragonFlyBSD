/* grep.c - main driver file for grep.
   Copyright (C) 1992, 1997-2002, 2004-2010 Free Software Foundation, Inc.

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
#if defined HAVE_SETRLIMIT
# include <sys/time.h>
# include <sys/resource.h>
#endif
#include "mbsupport.h"
#include <wchar.h>
#include <wctype.h>
#include <fcntl.h>
#include <stdio.h>
#include "system.h"

#include "argmatch.h"
#include "c-ctype.h"
#include "closeout.h"
#include "error.h"
#include "exclude.h"
#include "exitfail.h"
#include "getopt.h"
#include "grep.h"
#include "intprops.h"
#include "isdir.h"
#include "progname.h"
#include "propername.h"
#include "savedir.h"
#include "version-etc.h"
#include "xalloc.h"
#include "xstrtol.h"

#define SEP_CHAR_SELECTED ':'
#define SEP_CHAR_REJECTED '-'
#define SEP_STR_GROUP    "--"

#define STREQ(a, b) (strcmp (a, b) == 0)

#define AUTHORS \
  proper_name ("Mike Haertel"), \
  _("others, see <http://git.sv.gnu.org/cgit/grep.git/tree/AUTHORS>")

struct stats
{
  struct stats const *parent;
  struct stat stat;
};

/* base of chain of stat buffers, used to detect directory loops */
static struct stats stats_base;

/* if non-zero, display usage information and exit */
static int show_help;

/* If non-zero, print the version on standard output and exit.  */
static int show_version;

/* If nonzero, suppress diagnostics for nonexistent or unreadable files.  */
static int suppress_errors;

/* If nonzero, use color markers.  */
static int color_option;

/* If nonzero, show only the part of a line matching the expression. */
static int only_matching;

/* If nonzero, make sure first content char in a line is on a tab stop. */
static int align_tabs;

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
#define SGR_START  sgr_start
static const char *sgr_end   = "\33[m\33[K";
#define SGR_END    sgr_end

/* SGR utility macros.  */
#define PR_SGR_FMT(fmt, s) do { if (*(s)) printf((fmt), (s)); } while (0)
#define PR_SGR_FMT_IF(fmt, s) \
  do { if (color_option && *(s)) printf((fmt), (s)); } while (0)
#define PR_SGR_START(s)    PR_SGR_FMT(   SGR_START, (s))
#define PR_SGR_END(s)      PR_SGR_FMT(   SGR_END,   (s))
#define PR_SGR_START_IF(s) PR_SGR_FMT_IF(SGR_START, (s))
#define PR_SGR_END_IF(s)   PR_SGR_FMT_IF(SGR_END,   (s))

struct color_cap
  {
    const char *name;
    const char **var;
    const char *(*fct)(void);
  };

static const char *
color_cap_mt_fct(void)
{
  /* Our caller just set selected_match_color.  */
  context_match_color = selected_match_color;

  return NULL;
}

static const char *
color_cap_rv_fct(void)
{
  /* By this point, it was 1 (or already -1).  */
  color_option = -1;  /* That's still != 0.  */

  return NULL;
}

static const char *
color_cap_ne_fct(void)
{
  sgr_start = "\33[%sm";
  sgr_end   = "\33[m";

  return NULL;
}

/* For GREP_COLORS.  */
static struct color_cap color_dict[] =
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

static struct exclude *excluded_patterns;
static struct exclude *included_patterns;
static struct exclude *excluded_directory_patterns;
/* Short options.  */
static char const short_options[] =
"0123456789A:B:C:D:EFGHIPTUVX:abcd:e:f:hiKLlm:noqRrsuvwxyZz";

/* Non-boolean long options that have no corresponding short equivalents.  */
enum
{
  BINARY_FILES_OPTION = CHAR_MAX + 1,
  COLOR_OPTION,
  INCLUDE_OPTION,
  EXCLUDE_OPTION,
  EXCLUDE_FROM_OPTION,
  LINE_BUFFERED_OPTION,
  LABEL_OPTION,
  EXCLUDE_DIRECTORY_OPTION,
  GROUP_SEPARATOR_OPTION,
  MMAP_OPTION
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
  {"initial-tab", no_argument, NULL, 'T'},
  {"label", required_argument, NULL, LABEL_OPTION},
  {"line-buffered", no_argument, NULL, LINE_BUFFERED_OPTION},
  {"line-number", no_argument, NULL, 'n'},
  {"line-regexp", no_argument, NULL, 'x'},
  {"max-count", required_argument, NULL, 'm'},

  /* FIXME: disabled in Mar 2010; warn towards end of 2011; remove in 2013.  */
  {"mmap", no_argument, NULL, MMAP_OPTION},
  {"no-filename", no_argument, NULL, 'h'},
  {"no-group-separator", no_argument, NULL, GROUP_SEPARATOR_OPTION},
  {"no-messages", no_argument, NULL, 's'},
  {"null", no_argument, NULL, 'Z'},
  {"null-data", no_argument, NULL, 'z'},
  {"only-matching", no_argument, NULL, 'o'},
  {"quiet", no_argument, NULL, 'q'},
  {"recursive", no_argument, NULL, 'r'},
  {"recursive", no_argument, NULL, 'R'},
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
int match_icase;
int match_words;
int match_lines;
unsigned char eolbyte;

/* For error messages. */
/* The name the program was run with, stripped of any leading path. */
static char const *filename;
static int errseen;

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

/* How to handle devices. */
static enum
  {
    READ_DEVICES,
    SKIP_DEVICES
  } devices = READ_DEVICES;

static int grepdir (char const *, struct stats const *);
#if defined HAVE_DOS_FILE_CONTENTS
static inline int undossify_input (char *, size_t);
#endif

/* Functions we'll use to search. */
static compile_fp_t compile;
static execute_fp_t execute;

/* Like error, but suppress the diagnostic if requested.  */
static void
suppressible_error (char const *mesg, int errnum)
{
  if (! suppress_errors)
    error (0, errnum, "%s", mesg);
  errseen = 1;
}

/* Convert STR to a positive integer, storing the result in *OUT.
   STR must be a valid context length argument; report an error if it
   isn't.  */
static void
context_length_arg (char const *str, int *out)
{
  uintmax_t value;
  if (! (xstrtoumax (str, 0, 10, &value, "") == LONGINT_OK
         && 0 <= (*out = value)
         && *out == value))
    {
      error (EXIT_TROUBLE, 0, "%s: %s", str,
             _("invalid context length argument"));
    }
}


/* Hairy buffering mechanism for grep.  The intent is to keep
   all reads aligned on a page boundary and multiples of the
   page size, unless a read yields a partial page.  */

static char *buffer;		/* Base of buffer. */
static size_t bufalloc;		/* Allocated buffer size, counting slop. */
#define INITIAL_BUFSIZE 32768	/* Initial buffer size, not counting slop. */
static int bufdesc;		/* File descriptor. */
static char *bufbeg;		/* Beginning of user-visible stuff. */
static char *buflim;		/* Limit of user-visible stuff. */
static size_t pagesize;		/* alignment of memory pages */
static off_t bufoffset;		/* Read offset; defined on regular files.  */
static off_t after_last_match;	/* Pointer after last matching line that
                                   would have been output if we were
                                   outputting characters. */

/* Return VAL aligned to the next multiple of ALIGNMENT.  VAL can be
   an integer or a pointer.  Both args must be free of side effects.  */
#define ALIGN_TO(val, alignment) \
  ((size_t) (val) % (alignment) == 0 \
   ? (val) \
   : (val) + ((alignment) - (size_t) (val) % (alignment)))

/* Reset the buffer for a new file, returning zero if we should skip it.
   Initialize on the first time through. */
static int
reset (int fd, char const *file, struct stats *stats)
{
  if (! pagesize)
    {
      pagesize = getpagesize ();
      if (pagesize == 0 || 2 * pagesize + 1 <= pagesize)
        abort ();
      bufalloc = ALIGN_TO (INITIAL_BUFSIZE, pagesize) + pagesize + 1;
      buffer = xmalloc (bufalloc);
    }

  bufbeg = buflim = ALIGN_TO (buffer + 1, pagesize);
  bufbeg[-1] = eolbyte;
  bufdesc = fd;

  if (S_ISREG (stats->stat.st_mode))
    {
      if (file)
        bufoffset = 0;
      else
        {
          bufoffset = lseek (fd, 0, SEEK_CUR);
          if (bufoffset < 0)
            {
              error (0, errno, _("lseek failed"));
              return 0;
            }
        }
    }
  return 1;
}

/* Read new stuff into the buffer, saving the specified
   amount of old stuff.  When we're done, 'bufbeg' points
   to the beginning of the buffer contents, and 'buflim'
   points just after the end.  Return zero if there's an error.  */
static int
fillbuf (size_t save, struct stats const *stats)
{
  size_t fillsize = 0;
  int cc = 1;
  char *readbuf;
  size_t readsize;

  /* Offset from start of buffer to start of old stuff
     that we want to save.  */
  size_t saved_offset = buflim - save - buffer;

  if (pagesize <= buffer + bufalloc - buflim)
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
      for (newsize = bufalloc - pagesize - 1; newsize < minsize; newsize *= 2)
        if (newsize * 2 < newsize || newsize * 2 + pagesize + 1 < newsize * 2)
          xalloc_die ();

      /* Try not to allocate more memory than the file size indicates,
         as that might cause unnecessary memory exhaustion if the file
         is large.  However, do not use the original file size as a
         heuristic if we've already read past the file end, as most
         likely the file is growing.  */
      if (S_ISREG (stats->stat.st_mode))
        {
          off_t to_be_read = stats->stat.st_size - bufoffset;
          off_t maxsize_off = save + to_be_read;
          if (0 <= to_be_read && to_be_read <= maxsize_off
              && maxsize_off == (size_t) maxsize_off
              && minsize <= (size_t) maxsize_off
              && (size_t) maxsize_off < newsize)
            newsize = maxsize_off;
        }

      /* Add enough room so that the buffer is aligned and has room
         for byte sentinels fore and aft.  */
      newalloc = newsize + pagesize + 1;

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

  readsize = buffer + bufalloc - readbuf;
  readsize -= readsize % pagesize;

  if (! fillsize)
    {
      ssize_t bytesread;
      while ((bytesread = read (bufdesc, readbuf, readsize)) < 0
             && errno == EINTR)
        continue;
      if (bytesread < 0)
        cc = 0;
      else
        fillsize = bytesread;
    }

  bufoffset += fillsize;
#if defined HAVE_DOS_FILE_CONTENTS
  if (fillsize)
    fillsize = undossify_input (readbuf, fillsize);
#endif
  buflim = readbuf + fillsize;
  return cc;
}

/* Flags controlling the style of output. */
static enum
{
  BINARY_BINARY_FILES,
  TEXT_BINARY_FILES,
  WITHOUT_MATCH_BINARY_FILES
} binary_files;		/* How to handle binary files.  */

static int filename_mask;	/* If zero, output nulls after filenames.  */
static int out_quiet;		/* Suppress all normal output. */
static int out_invert;		/* Print nonmatching stuff. */
static int out_file;		/* Print filenames. */
static int out_line;		/* Print line numbers. */
static int out_byte;		/* Print byte offsets. */
static int out_before;		/* Lines of leading context. */
static int out_after;		/* Lines of trailing context. */
static int count_matches;	/* Count matching lines.  */
static int list_files;		/* List matching files.  */
static int no_filenames;	/* Suppress file names.  */
static off_t max_count;		/* Stop after outputting this many
                                   lines from an input file.  */
static int line_buffered;       /* If nonzero, use line buffering, i.e.
                                   fflush everyline out.  */
static char *label = NULL;      /* Fake filename for stdin */


/* Internal variables to keep track of byte count, context, etc. */
static uintmax_t totalcc;	/* Total character count before bufbeg. */
static char const *lastnl;	/* Pointer after last newline counted. */
static char const *lastout;	/* Pointer after last character output;
                                   NULL if no character has been output
                                   or if it's conceptually before bufbeg. */
static uintmax_t totalnl;	/* Total newline count before lastnl. */
static off_t outleft;		/* Maximum number of lines to be output.  */
static int pending;		/* Pending lines of output.
                                   Always kept 0 if out_quiet is true.  */
static int done_on_match;	/* Stop scanning file on first match.  */
static int exit_on_match;	/* Exit on first match.  */

#if defined HAVE_DOS_FILE_CONTENTS
# include "dosbuf.c"
#endif

/* Add two numbers that count input bytes or lines, and report an
   error if the addition overflows.  */
static uintmax_t
add_count (uintmax_t a, uintmax_t b)
{
  uintmax_t sum = a + b;
  if (sum < a)
    error (EXIT_TROUBLE, 0, _("input is too large to count"));
  return sum;
}

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
  PR_SGR_START_IF(filename_color);
  fputs(filename, stdout);
  PR_SGR_END_IF(filename_color);
}

/* Print a character separator.  */
static void
print_sep (char sep)
{
  PR_SGR_START_IF(sep_color);
  fputc(sep, stdout);
  PR_SGR_END_IF(sep_color);
}

/* Print a line number or a byte offset.  */
static void
print_offset (uintmax_t pos, int min_width, const char *color)
{
  /* Do not rely on printf to print pos, since uintmax_t may be longer
     than long, and long long is not portable.  */

  char buf[sizeof pos * CHAR_BIT];
  char *p = buf + sizeof buf;

  do
    {
      *--p = '0' + pos % 10;
      --min_width;
    }
  while ((pos /= 10) != 0);

  /* Do this to maximize the probability of alignment across lines.  */
  if (align_tabs)
    while (--min_width >= 0)
      *--p = ' ';

  PR_SGR_START_IF(color);
  fwrite (p, 1, buf + sizeof buf - p, stdout);
  PR_SGR_END_IF(color);
}

/* Print a whole line head (filename, line, byte).  */
static void
print_line_head (char const *beg, char const *lim, int sep)
{
  int pending_sep = 0;

  if (out_file)
    {
      print_filename();
      if (filename_mask)
        pending_sep = 1;
      else
        fputc(0, stdout);
    }

  if (out_line)
    {
      if (lastnl < lim)
        {
          nlscan (beg);
          totalnl = add_count (totalnl, 1);
          lastnl = lim;
        }
      if (pending_sep)
        print_sep(sep);
      print_offset (totalnl, 4, line_num_color);
      pending_sep = 1;
    }

  if (out_byte)
    {
      uintmax_t pos = add_count (totalcc, beg - bufbeg);
#if defined HAVE_DOS_FILE_CONTENTS
      pos = dossified_pos (pos);
#endif
      if (pending_sep)
        print_sep(sep);
      print_offset (pos, 6, byte_num_color);
      pending_sep = 1;
    }

  if (pending_sep)
    {
      /* This assumes sep is one column wide.
         Try doing this any other way with Unicode
         (and its combining and wide characters)
         filenames and you're wasting your efforts.  */
      if (align_tabs)
        fputs("\t\b", stdout);

      print_sep(sep);
    }
}

static const char *
print_line_middle (const char *beg, const char *lim,
                   const char *line_color, const char *match_color)
{
  size_t match_size;
  size_t match_offset;
  const char *cur = beg;
  const char *mid = NULL;

  while (cur < lim
         && ((match_offset = execute(beg, lim - beg, &match_size,
                                     beg + (cur - beg))) != (size_t) -1))
    {
      char const *b = beg + match_offset;

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
            print_line_head(b, lim, out_invert ? SEP_CHAR_REJECTED
                                               : SEP_CHAR_SELECTED);
          else
            {
              PR_SGR_START(line_color);
              if (mid)
                {
                  cur = mid;
                  mid = NULL;
                }
              fwrite (cur, sizeof (char), b - cur, stdout);
            }

          PR_SGR_START_IF(match_color);
          fwrite (b, sizeof (char), match_size, stdout);
          PR_SGR_END_IF(match_color);
          if (only_matching)
            fputs("\n", stdout);
        }
      cur = b + match_size;
    }

  if (only_matching)
    cur = lim;
  else if (mid)
    cur = mid;

  return cur;
}

static const char *
print_line_tail (const char *beg, const char *lim, const char *line_color)
{
  size_t  eol_size;
  size_t tail_size;

  eol_size   = (lim > beg && lim[-1] == eolbyte);
  eol_size  += (lim - eol_size > beg && lim[-(1 + eol_size)] == '\r');
  tail_size  =  lim - eol_size - beg;

  if (tail_size > 0)
    {
      PR_SGR_START(line_color);
      fwrite(beg, 1, tail_size, stdout);
      beg += tail_size;
      PR_SGR_END(line_color);
    }

  return beg;
}

static void
prline (char const *beg, char const *lim, int sep)
{
  int matching;
  const char *line_color;
  const char *match_color;

  if (!only_matching)
    print_line_head(beg, lim, sep);

  matching = (sep == SEP_CHAR_SELECTED) ^ !!out_invert;

  if (color_option)
    {
      line_color  = (  (sep == SEP_CHAR_SELECTED)
                     ^ (out_invert && (color_option < 0)))
                  ? selected_line_color  : context_line_color;
      match_color = (sep == SEP_CHAR_SELECTED)
                  ? selected_match_color : context_match_color;
    }
  else
    line_color = match_color = NULL; /* Shouldn't be used.  */

  if (   (only_matching && matching)
      || (color_option  && (*line_color || *match_color)))
    {
      /* We already know that non-matching lines have no match (to colorize).  */
      if (matching && (only_matching || *match_color))
        beg = print_line_middle(beg, lim, line_color, match_color);

      /* FIXME: this test may be removable.  */
      if (!only_matching && *line_color)
        beg = print_line_tail(beg, lim, line_color);
    }

  if (!only_matching && lim > beg)
    fwrite (beg, 1, lim - beg, stdout);

  if (ferror (stdout))
    error (0, errno, _("writing output"));

  lastout = lim;

  if (line_buffered)
    fflush (stdout);
}

/* Print pending lines of trailing context prior to LIM. Trailing context ends
   at the next matching line when OUTLEFT is 0.  */
static void
prpending (char const *lim)
{
  if (!lastout)
    lastout = bufbeg;
  while (pending > 0 && lastout < lim)
    {
      char const *nl = memchr (lastout, eolbyte, lim - lastout);
      size_t match_size;
      --pending;
      if (outleft
          || ((execute(lastout, nl + 1 - lastout,
                       &match_size, NULL) == (size_t) -1)
              == !out_invert))
        prline (lastout, nl + 1, SEP_CHAR_REJECTED);
      else
        pending = 0;
    }
}

/* Print the lines between BEG and LIM.  Deal with context crap.
   If NLINESP is non-null, store a count of lines between BEG and LIM.  */
static void
prtext (char const *beg, char const *lim, int *nlinesp)
{
  static int used;	/* avoid printing SEP_STR_GROUP before any output */
  char const *bp, *p;
  char eol = eolbyte;
  int i, n;

  if (!out_quiet && pending > 0)
    prpending (beg);

  p = beg;

  if (!out_quiet)
    {
      /* Deal with leading context crap. */

      bp = lastout ? lastout : bufbeg;
      for (i = 0; i < out_before; ++i)
        if (p > bp)
          do
            --p;
          while (p[-1] != eol);

      /* We print the SEP_STR_GROUP separator only if our output is
         discontiguous from the last output in the file. */
      if ((out_before || out_after) && used && p != lastout && group_separator)
        {
          PR_SGR_START_IF(sep_color);
          fputs (group_separator, stdout);
          PR_SGR_END_IF(sep_color);
          fputc('\n', stdout);
        }

      while (p < beg)
        {
          char const *nl = memchr (p, eol, beg - p);
          nl++;
          prline (p, nl, SEP_CHAR_REJECTED);
          p = nl;
        }
    }

  if (nlinesp)
    {
      /* Caller wants a line count. */
      for (n = 0; p < lim && n < outleft; n++)
        {
          char const *nl = memchr (p, eol, lim - p);
          nl++;
          if (!out_quiet)
            prline (p, nl, SEP_CHAR_SELECTED);
          p = nl;
        }
      *nlinesp = n;

      /* relying on it that this function is never called when outleft = 0.  */
      after_last_match = bufoffset - (buflim - p);
    }
  else
    if (!out_quiet)
      prline (beg, lim, SEP_CHAR_SELECTED);

  pending = out_quiet ? 0 : out_after;
  used = 1;
}

static size_t
do_execute (char const *buf, size_t size, size_t *match_size, char const *start_ptr)
{
  size_t result;
  const char *line_next;

  /* With the current implementation, using --ignore-case with a multi-byte
     character set is very inefficient when applied to a large buffer
     containing many matches.  We can avoid much of the wasted effort
     by matching line-by-line.

     FIXME: this is just an ugly workaround, and it doesn't really
     belong here.  Also, PCRE is always using this same per-line
     matching algorithm.  Either we fix -i, or we should refactor
     this code---for example, we could add another function pointer
     to struct matcher to split the buffer passed to execute.  It would
     perform the memchr if line-by-line matching is necessary, or just
     return buf + size otherwise.  */
  if (MB_CUR_MAX == 1 || !match_icase)
    return execute(buf, size, match_size, start_ptr);

  for (line_next = buf; line_next < buf + size; )
    {
      const char *line_buf = line_next;
      const char *line_end = memchr (line_buf, eolbyte, (buf + size) - line_buf);
      if (line_end == NULL)
        line_next = line_end = buf + size;
      else
        line_next = line_end + 1;

      if (start_ptr && start_ptr >= line_end)
        continue;

      result = execute (line_buf, line_next - line_buf, match_size, start_ptr);
      if (result != (size_t) -1)
        return (line_buf - buf) + result;
    }

  return (size_t) -1;
}

/* Scan the specified portion of the buffer, matching lines (or
   between matching lines if OUT_INVERT is true).  Return a count of
   lines printed. */
static int
grepbuf (char const *beg, char const *lim)
{
  int nlines, n;
  char const *p;
  size_t match_offset;
  size_t match_size;

  nlines = 0;
  p = beg;
  while ((match_offset = do_execute(p, lim - p, &match_size,
                                    NULL)) != (size_t) -1)
    {
      char const *b = p + match_offset;
      char const *endp = b + match_size;
      /* Avoid matching the empty line at the end of the buffer. */
      if (b == lim)
        break;
      if (!out_invert)
        {
          prtext (b, endp, (int *) 0);
          nlines++;
          outleft--;
          if (!outleft || done_on_match)
            {
              if (exit_on_match)
                exit (EXIT_SUCCESS);
              after_last_match = bufoffset - (buflim - endp);
              return nlines;
            }
        }
      else if (p < b)
        {
          prtext (p, b, &n);
          nlines += n;
          outleft -= n;
          if (!outleft)
            return nlines;
        }
      p = endp;
    }
  if (out_invert && p < lim)
    {
      prtext (p, lim, &n);
      nlines += n;
      outleft -= n;
    }
  return nlines;
}

/* Search a given file.  Normally, return a count of lines printed;
   but if the file is a directory and we search it recursively, then
   return -2 if there was a match, and -1 otherwise.  */
static int
grep (int fd, char const *file, struct stats *stats)
{
  int nlines, i;
  int not_text;
  size_t residue, save;
  char oldc;
  char *beg;
  char *lim;
  char eol = eolbyte;

  if (!reset (fd, file, stats))
    return 0;

  if (file && directories == RECURSE_DIRECTORIES
      && S_ISDIR (stats->stat.st_mode))
    {
      /* Close fd now, so that we don't open a lot of file descriptors
         when we recurse deeply.  */
      if (close (fd) != 0)
        error (0, errno, "%s", file);
      return grepdir (file, stats) - 2;
    }

  totalcc = 0;
  lastout = 0;
  totalnl = 0;
  outleft = max_count;
  after_last_match = 0;
  pending = 0;

  nlines = 0;
  residue = 0;
  save = 0;

  if (! fillbuf (save, stats))
    {
      if (! is_EISDIR (errno, file))
        suppressible_error (filename, errno);
      return 0;
    }

  not_text = (((binary_files == BINARY_BINARY_FILES && !out_quiet)
               || binary_files == WITHOUT_MATCH_BINARY_FILES)
              && memchr (bufbeg, eol ? '\0' : '\200', buflim - bufbeg));
  if (not_text && binary_files == WITHOUT_MATCH_BINARY_FILES)
    return 0;
  done_on_match += not_text;
  out_quiet += not_text;

  for (;;)
    {
      lastnl = bufbeg;
      if (lastout)
        lastout = bufbeg;

      beg = bufbeg + save;

      /* no more data to scan (eof) except for maybe a residue -> break */
      if (beg == buflim)
        break;

      /* Determine new residue (the length of an incomplete line at the end of
         the buffer, 0 means there is no incomplete last line).  */
      oldc = beg[-1];
      beg[-1] = eol;
      for (lim = buflim; lim[-1] != eol; lim--)
        continue;
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
          if((!outleft && !pending) || (nlines && done_on_match && !out_invert))
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

      /* detect if leading context is discontinuous from last printed line.  */
      if (beg != lastout)
        lastout = 0;

      /* Handle some details and read more data to scan.  */
      save = residue + lim - beg;
      if (out_byte)
        totalcc = add_count (totalcc, buflim - bufbeg - save);
      if (out_line)
        nlscan (beg);
      if (! fillbuf (save, stats))
        {
          if (! is_EISDIR (errno, file))
            suppressible_error (filename, errno);
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
  done_on_match -= not_text;
  out_quiet -= not_text;
  if ((not_text & ~out_quiet) && nlines != 0)
    printf (_("Binary file %s matches\n"), filename);
  return nlines;
}

static int
grepfile (char const *file, struct stats *stats)
{
  int desc;
  int count;
  int status;

  if (! file)
    {
      desc = 0;
      filename = label ? label : _("(standard input)");
    }
  else
    {
      if (stat (file, &stats->stat) != 0)
        {
          suppressible_error (file, errno);
          return 1;
        }
      if (directories == SKIP_DIRECTORIES && S_ISDIR (stats->stat.st_mode))
        return 1;
      if (devices == SKIP_DEVICES && (S_ISCHR (stats->stat.st_mode)
                                      || S_ISBLK (stats->stat.st_mode)
                                      || S_ISSOCK (stats->stat.st_mode)
                                      || S_ISFIFO (stats->stat.st_mode)))
        return 1;
      while ((desc = open (file, O_RDONLY)) < 0 && errno == EINTR)
        continue;

      if (desc < 0)
        {
          int e = errno;

          if (is_EISDIR (e, file) && directories == RECURSE_DIRECTORIES)
            {
              if (stat (file, &stats->stat) != 0)
                {
                  error (0, errno, "%s", file);
                  return 1;
                }

              return grepdir (file, stats);
            }

          if (!suppress_errors)
            {
              if (directories == SKIP_DIRECTORIES)
                switch (e)
                  {
#if defined EISDIR
                  case EISDIR:
                    return 1;
#endif
                  case EACCES:
                    /* When skipping directories, don't worry about
                       directories that can't be opened.  */
                    if (isdir (file))
                      return 1;
                    break;
                  }
            }

          suppressible_error (file, e);
          return 1;
        }

      filename = file;
    }

#if defined SET_BINARY
  /* Set input to binary mode.  Pipes are simulated with files
     on DOS, so this includes the case of "foo | grep bar".  */
  if (!isatty (desc))
    SET_BINARY (desc);
#endif

  count = grep (desc, file, stats);
  if (count < 0)
    status = count + 2;
  else
    {
      if (count_matches)
        {
          if (out_file)
            {
              print_filename();
              if (filename_mask)
                print_sep(SEP_CHAR_SELECTED);
              else
                fputc(0, stdout);
            }
          printf ("%d\n", count);
        }

      status = !count;
      if (list_files == 1 - 2 * status)
        {
          print_filename();
          fputc('\n' & filename_mask, stdout);
        }

      if (! file)
        {
          off_t required_offset = outleft ? bufoffset : after_last_match;
          if (required_offset != bufoffset
              && lseek (desc, required_offset, SEEK_SET) < 0
              && S_ISREG (stats->stat.st_mode))
            error (0, errno, "%s", filename);
        }
      else
        while (close (desc) != 0)
          if (errno != EINTR)
            {
              error (0, errno, "%s", file);
              break;
            }
    }

  return status;
}

static int
grepdir (char const *dir, struct stats const *stats)
{
  struct stats const *ancestor;
  char *name_space;
  int status = 1;
  if ( excluded_directory_patterns &&
       excluded_file_name (excluded_directory_patterns, dir)  ) {
       return 1;
  }


  /* Mingw32 does not support st_ino.  No known working hosts use zero
     for st_ino, so assume that the Mingw32 bug applies if it's zero.  */
  if (stats->stat.st_ino)
    for (ancestor = stats;  (ancestor = ancestor->parent) != 0;  )
      if (ancestor->stat.st_ino == stats->stat.st_ino
          && ancestor->stat.st_dev == stats->stat.st_dev)
        {
          if (!suppress_errors)
            error (0, 0, _("warning: %s: %s"), dir,
                   _("recursive directory loop"));
          return 1;
        }

  name_space = savedir (dir, stats->stat.st_size, included_patterns,
                        excluded_patterns, excluded_directory_patterns);

  if (! name_space)
    {
      if (errno)
        suppressible_error (dir, errno);
      else
        xalloc_die ();
    }
  else
    {
      size_t dirlen = strlen (dir);
      int needs_slash = ! (dirlen == FILE_SYSTEM_PREFIX_LEN (dir)
                           || ISSLASH (dir[dirlen - 1]));
      char *file = NULL;
      char const *namep = name_space;
      struct stats child;
      child.parent = stats;
      out_file += !no_filenames;
      while (*namep)
        {
          size_t namelen = strlen (namep);
          file = xrealloc (file, dirlen + 1 + namelen + 1);
          strcpy (file, dir);
          file[dirlen] = '/';
          strcpy (file + dirlen + needs_slash, namep);
          namep += namelen + 1;
          status &= grepfile (file, &child);
        }
      out_file -= !no_filenames;
      free (file);
      free (name_space);
    }

  return status;
}

void usage (int status) __attribute__ ((noreturn));
void
usage (int status)
{
  if (status != 0)
    {
      fprintf (stderr, _("Usage: %s [OPTION]... PATTERN [FILE]...\n"),
               program_name);
      fprintf (stderr, _("Try `%s --help' for more information.\n"),
               program_name);
    }
  else
    {
      printf (_("Usage: %s [OPTION]... PATTERN [FILE]...\n"), program_name);
      printf (_("\
Search for PATTERN in each FILE or standard input.\n"));
      printf ("%s", gettext (before_options));
      printf (_("\
Example: %s -i 'hello world' menu.h main.c\n\
\n\
Regexp selection and interpretation:\n"), program_name);
      if (matchers[1].name)
        printf (_("\
  -E, --extended-regexp     PATTERN is an extended regular expression (ERE)\n\
  -F, --fixed-strings       PATTERN is a set of newline-separated fixed strings\n\
  -G, --basic-regexp        PATTERN is a basic regular expression (BRE)\n\
  -P, --perl-regexp         PATTERN is a Perl regular expression\n"));
  /* -X is undocumented on purpose. */
      printf (_("\
  -e, --regexp=PATTERN      use PATTERN for matching\n\
  -f, --file=FILE           obtain PATTERN from FILE\n\
  -i, --ignore-case         ignore case distinctions\n\
  -w, --word-regexp         force PATTERN to match only whole words\n\
  -x, --line-regexp         force PATTERN to match only whole lines\n\
  -z, --null-data           a data line ends in 0 byte, not newline\n"));
      printf (_("\
\n\
Miscellaneous:\n\
  -s, --no-messages         suppress error messages\n\
  -v, --invert-match        select non-matching lines\n\
  -V, --version             print version information and exit\n\
      --help                display this help and exit\n\
      --mmap                ignored for backwards compatibility\n"));
      printf (_("\
\n\
Output control:\n\
  -m, --max-count=NUM       stop after NUM matches\n\
  -b, --byte-offset         print the byte offset with output lines\n\
  -n, --line-number         print line number with output lines\n\
      --line-buffered       flush output on every line\n\
  -H, --with-filename       print the filename for each match\n\
  -h, --no-filename         suppress the prefixing filename on output\n\
      --label=LABEL         print LABEL as filename for standard input\n\
"));
      printf (_("\
  -o, --only-matching       show only the part of a line matching PATTERN\n\
  -q, --quiet, --silent     suppress all normal output\n\
      --binary-files=TYPE   assume that binary files are TYPE;\n\
                            TYPE is `binary', `text', or `without-match'\n\
  -a, --text                equivalent to --binary-files=text\n\
"));
      printf (_("\
  -I                        equivalent to --binary-files=without-match\n\
  -d, --directories=ACTION  how to handle directories;\n\
                            ACTION is `read', `recurse', or `skip'\n\
  -D, --devices=ACTION      how to handle devices, FIFOs and sockets;\n\
                            ACTION is `read' or `skip'\n\
  -R, -r, --recursive       equivalent to --directories=recurse\n\
"));
      printf (_("\
      --include=FILE_PATTERN  search only files that match FILE_PATTERN\n\
      --exclude=FILE_PATTERN  skip files and directories matching FILE_PATTERN\n\
      --exclude-from=FILE   skip files matching any file pattern from FILE\n\
      --exclude-dir=PATTERN  directories that match PATTERN will be skipped.\n\
"));
      printf (_("\
  -L, --files-without-match  print only names of FILEs containing no match\n\
  -l, --files-with-matches  print only names of FILEs containing matches\n\
  -c, --count               print only a count of matching lines per FILE\n\
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
                            WHEN is `always', `never', or `auto'\n\
  -U, --binary              do not strip CR characters at EOL (MSDOS)\n\
  -u, --unix-byte-offsets   report offsets as if CRs were not there (MSDOS)\n\
\n"));
      printf ("%s", _(after_options));
      printf (_("\
With no FILE, or when FILE is -, read standard input.  If less than two FILEs\n\
are given, assume -h.  Exit status is 0 if any line was selected, 1 otherwise;\n\
if any error occurs and -q was not given, the exit status is 2.\n"));
      printf (_("\nReport bugs to: %s\n"), PACKAGE_BUGREPORT);
      printf (_("GNU Grep home page: <%s>\n"),
              "http://www.gnu.org/software/grep/");
      fputs (_("General help using GNU software: <http://www.gnu.org/gethelp/>\n"),
             stdout);

    }
  exit (status);
}

/* If M is NULL, initialize the matcher to the default.  Otherwise set the
   matcher to M if available.  Exit in case of conflicts or if M is not
   available.  */
static void
setmatcher (char const *m)
{
  static char const *matcher;
  unsigned int i;

  if (!m)
    {
      compile = matchers[0].compile;
      execute = matchers[0].execute;
      if (!matchers[1].name)
        matcher = matchers[0].name;
    }

  else if (matcher)
    {
      if (matcher && STREQ (matcher, m))
        ;

      else if (!matchers[1].name)
        error (EXIT_TROUBLE, 0, _("%s can only use the %s pattern syntax"),
               program_name, matcher);
      else
        error (EXIT_TROUBLE, 0, _("conflicting matchers specified"));
    }

  else
    {
      for (i = 0; matchers[i].name; i++)
        if (STREQ (m, matchers[i].name))
          {
            compile = matchers[i].compile;
            execute = matchers[i].execute;
            matcher = m;
            return;
          }

      error (EXIT_TROUBLE, 0, _("invalid matcher %s"), m);
    }
}

static void
set_limits(void)
{
#if defined HAVE_SETRLIMIT && defined RLIMIT_STACK
  struct rlimit rlim;

  /* I think every platform needs to do this, so that regex.c
     doesn't oveflow the stack.  The default value of
     `re_max_failures' is too large for some platforms: it needs
     more than 3MB-large stack.

     The test for HAVE_SETRLIMIT should go into `configure'.  */
  if (!getrlimit (RLIMIT_STACK, &rlim))
    {
      long newlim;
      extern long int re_max_failures; /* from regex.c */

      /* Approximate the amount regex.c needs, plus some more.  */
      newlim = re_max_failures * 2 * 20 * sizeof (char *);
      if (newlim > rlim.rlim_max)
        {
          newlim = rlim.rlim_max;
          re_max_failures = newlim / (2 * 20 * sizeof (char *));
        }
      if (rlim.rlim_cur < newlim)
        {
          rlim.rlim_cur = newlim;
          setrlimit (RLIMIT_STACK, &rlim);
        }
    }
#endif
}

/* Find the white-space-separated options specified by OPTIONS, and
   using BUF to store copies of these options, set ARGV[0], ARGV[1],
   etc. to the option copies.  Return the number N of options found.
   Do not set ARGV[N] to NULL.  If ARGV is NULL, do not store ARGV[0]
   etc.  Backslash can be used to escape whitespace (and backslashes).  */
static int
prepend_args (char const *options, char *buf, char **argv)
{
  char const *o = options;
  char *b = buf;
  int n = 0;

  for (;;)
    {
      while (c_isspace ((unsigned char) *o))
        o++;
      if (!*o)
        return n;
      if (argv)
        argv[n] = b;
      n++;

      do
        if ((*b++ = *o++) == '\\' && *o)
          b[-1] = *o++;
      while (*o && ! c_isspace ((unsigned char) *o));

      *b++ = '\0';
    }
}

/* Prepend the whitespace-separated options in OPTIONS to the argument
   vector of a main program with argument count *PARGC and argument
   vector *PARGV.  */
static void
prepend_default_options (char const *options, int *pargc, char ***pargv)
{
  if (options && *options)
    {
      char *buf = xmalloc (strlen (options) + 1);
      int prepended = prepend_args (options, buf, (char **) NULL);
      int argc = *pargc;
      char * const *argv = *pargv;
      char **pp = xmalloc ((prepended + argc + 1) * sizeof *pp);
      *pargc = prepended + argc;
      *pargv = pp;
      *pp++ = *argv++;
      pp += prepend_args (options, buf, pp);
      while ((*pp++ = *argv++))
        continue;
    }
}

/* Get the next non-digit option from ARGC and ARGV.
   Return -1 if there are no more options.
   Process any digit options that were encountered on the way,
   and store the resulting integer into *DEFAULT_CONTEXT.  */
static int
get_nondigit_option (int argc, char *const *argv, int *default_context)
{
  static int prev_digit_optind = -1;
  int opt, this_digit_optind, was_digit;
  char buf[sizeof (uintmax_t) * CHAR_BIT + 4];
  char *p = buf;

  was_digit = 0;
  this_digit_optind = optind;
  while (opt = getopt_long (argc, (char **) argv, short_options, long_options,
                            NULL),
         '0' <= opt && opt <= '9')
    {
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

      was_digit = 1;
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

  p = getenv("GREP_COLORS"); /* Plural! */
  if (p == NULL || *p == '\0')
    return;

  /* Work off a writable copy.  */
  q = xmalloc(strlen(p) + 1);
  if (q == NULL)
    return;
  strcpy(q, p);

  name = q;
  val = NULL;
  /* From now on, be well-formed or you're gone.  */
  for (;;)
    if (*q == ':' || *q == '\0')
      {
        char c = *q;
        struct color_cap *cap;

        *q++ = '\0'; /* Terminate name or val.  */
        /* Empty name without val (empty cap)
         * won't match and will be ignored.  */
        for (cap = color_dict; cap->name; cap++)
          if (STREQ (cap->name, name))
            break;
        /* If name unknown, go on for forward compatibility.  */
        if (cap->name)
          {
            if (cap->var)
              {
                if (val)
              *(cap->var) = val;
                else
              error(0, 0, _("in GREP_COLORS=\"%s\", the \"%s\" capacity "
                                "needs a value (\"=...\"); skipped"), p, name);
          }
        else if (val)
          error(0, 0, _("in GREP_COLORS=\"%s\", the \"%s\" capacity "
                "is boolean and cannot take a value (\"=%s\"); skipped"),
                p, name, val);
      }
        if (cap->fct)
          {
            const char *err_str = cap->fct();

            if (err_str)
              error(0, 0, _("in GREP_COLORS=\"%s\", the \"%s\" capacity %s"),
                    p, name, err_str);
          }
        if (c == '\0')
          return;
        name = q;
        val = NULL;
      }
    else if (*q == '=')
      {
        if (q == name || val)
          goto ill_formed;
        *q++ = '\0'; /* Terminate name.  */
        val = q; /* Can be the empty string.  */
      }
    else if (val == NULL)
      q++; /* Accumulate name.  */
    else if (*q == ';' || (*q >= '0' && *q <= '9'))
      q++; /* Accumulate val.  Protect the terminal from being sent crap.  */
    else
      goto ill_formed;

 ill_formed:
  error(0, 0, _("stopped processing of ill-formed GREP_COLORS=\"%s\" "
                "at remaining substring \"%s\""), p, q);
}

int
main (int argc, char **argv)
{
  char *keys;
  size_t keycc, oldcc, keyalloc;
  int with_filenames;
  int opt, cc, status;
  int default_context;
  FILE *fp;

  initialize_main (&argc, &argv);
  set_program_name (argv[0]);
  program_name = argv[0];

  keys = NULL;
  keycc = 0;
  with_filenames = 0;
  eolbyte = '\n';
  filename_mask = ~0;

  max_count = TYPE_MAXIMUM (off_t);

  /* The value -1 means to use DEFAULT_CONTEXT. */
  out_after = out_before = -1;
  /* Default before/after context: chaged by -C/-NUM options */
  default_context = 0;
  /* Changed by -o option */
  only_matching = 0;

  /* Internationalization. */
#if defined HAVE_SETLOCALE
  setlocale (LC_ALL, "");
#endif
#if defined ENABLE_NLS
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);
#endif

  exit_failure = EXIT_TROUBLE;
  atexit (close_stdout);

  prepend_default_options (getenv ("GREP_OPTIONS"), &argc, &argv);
  setmatcher (NULL);

  while ((opt = get_nondigit_option (argc, argv, &default_context)) != -1)
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
          error (EXIT_TROUBLE, 0, _("unknown devices method"));
        break;

      case 'E':
        setmatcher ("egrep");
        break;

      case 'F':
        setmatcher ("fgrep");
        break;

      case 'P':
        setmatcher ("perl");
        break;

      case 'G':
        setmatcher ("grep");
        break;

      case 'X': /* undocumented on purpose */
        setmatcher (optarg);
        break;

      case 'H':
        with_filenames = 1;
        no_filenames = 0;
        break;

      case 'I':
        binary_files = WITHOUT_MATCH_BINARY_FILES;
        break;

      case 'T':
        align_tabs = 1;
        break;

      case 'U':
#if defined HAVE_DOS_FILE_CONTENTS
        dos_use_file_type = DOS_BINARY;
#endif
        break;

      case 'u':
#if defined HAVE_DOS_FILE_CONTENTS
        dos_report_unix_offset = 1;
#endif
        break;

      case 'V':
        show_version = 1;
        break;

      case 'a':
        binary_files = TEXT_BINARY_FILES;
        break;

      case 'b':
        out_byte = 1;
        break;

      case 'c':
        count_matches = 1;
        break;

      case 'd':
        directories = XARGMATCH ("--directories", optarg,
                                 directories_args, directories_types);
        break;

      case 'e':
        cc = strlen (optarg);
        keys = xrealloc (keys, keycc + cc + 1);
        strcpy (&keys[keycc], optarg);
        keycc += cc;
        keys[keycc++] = '\n';
        break;

      case 'f':
        fp = STREQ (optarg, "-") ? stdin : fopen (optarg, "r");
        if (!fp)
          error (EXIT_TROUBLE, errno, "%s", optarg);
        for (keyalloc = 1; keyalloc <= keycc + 1; keyalloc *= 2)
          ;
        keys = xrealloc (keys, keyalloc);
        oldcc = keycc;
        while (!feof (fp)
               && (cc = fread (keys + keycc, 1, keyalloc - 1 - keycc, fp)) > 0)
          {
            keycc += cc;
            if (keycc == keyalloc - 1)
              keys = xrealloc (keys, keyalloc *= 2);
          }
        if (fp != stdin)
          fclose(fp);
        /* Append final newline if file ended in non-newline. */
        if (oldcc != keycc && keys[keycc - 1] != '\n')
          keys[keycc++] = '\n';
        break;

      case 'h':
        with_filenames = 0;
        no_filenames = 1;
        break;

      case 'i':
      case 'y':			/* For old-timers . . . */
        match_icase = 1;
        break;

      case 'L':
        /* Like -l, except list files that don't contain matches.
           Inspired by the same option in Hume's gre. */
        list_files = -1;
        break;

      case 'l':
        list_files = 1;
        break;

      case 'm':
        {
          uintmax_t value;
          switch (xstrtoumax (optarg, 0, 10, &value, ""))
            {
            case LONGINT_OK:
              max_count = value;
              if (0 <= max_count && max_count == value)
                break;
              /* Fall through.  */
            case LONGINT_OVERFLOW:
              max_count = TYPE_MAXIMUM (off_t);
              break;

            default:
              error (EXIT_TROUBLE, 0, _("invalid max count"));
            }
        }
        break;

      case 'n':
        out_line = 1;
        break;

      case 'o':
        only_matching = 1;
        break;

      case 'q':
        exit_on_match = 1;
        exit_failure = 0;
        break;

      case 'R':
      case 'r':
        directories = RECURSE_DIRECTORIES;
        break;

      case 's':
        suppress_errors = 1;
        break;

      case 'v':
        out_invert = 1;
        break;

      case 'w':
        match_words = 1;
        break;

      case 'x':
        match_lines = 1;
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
          error (EXIT_TROUBLE, 0, _("unknown binary-files type"));
        break;

      case COLOR_OPTION:
        if(optarg) {
          if(!strcasecmp(optarg, "always") || !strcasecmp(optarg, "yes") ||
             !strcasecmp(optarg, "force"))
            color_option = 1;
          else if(!strcasecmp(optarg, "never") || !strcasecmp(optarg, "no") ||
                  !strcasecmp(optarg, "none"))
            color_option = 0;
          else if(!strcasecmp(optarg, "auto") || !strcasecmp(optarg, "tty") ||
                  !strcasecmp(optarg, "if-tty"))
            color_option = 2;
          else
            show_help = 1;
        } else
          color_option = 2;
        if (color_option == 2)
          {
            char const *t;
            if (isatty (STDOUT_FILENO) && (t = getenv ("TERM"))
                && !STREQ (t, "dumb"))
              color_option = 1;
            else
              color_option = 0;
          }
        break;

      case EXCLUDE_OPTION:
        if (!excluded_patterns)
          excluded_patterns = new_exclude ();
        add_exclude (excluded_patterns, optarg, EXCLUDE_WILDCARDS);
        break;
      case EXCLUDE_FROM_OPTION:
        if (!excluded_patterns)
          excluded_patterns = new_exclude ();
        if (add_exclude_file (add_exclude, excluded_patterns, optarg,
                              EXCLUDE_WILDCARDS, '\n') != 0)
          {
            error (EXIT_TROUBLE, errno, "%s", optarg);
          }
        break;

      case EXCLUDE_DIRECTORY_OPTION:
        if (!excluded_directory_patterns)
          excluded_directory_patterns = new_exclude ();
        add_exclude (excluded_directory_patterns, optarg, EXCLUDE_WILDCARDS);
        break;

      case INCLUDE_OPTION:
        if (!included_patterns)
          included_patterns = new_exclude ();
        add_exclude (included_patterns, optarg,
                     EXCLUDE_WILDCARDS | EXCLUDE_INCLUDE);
        break;

      case GROUP_SEPARATOR_OPTION:
        group_separator = optarg;
        break;

      case LINE_BUFFERED_OPTION:
        line_buffered = 1;
        break;

      case LABEL_OPTION:
        label = optarg;
        break;

      case MMAP_OPTION:
      case 0:
        /* long options */
        break;

      default:
        usage (EXIT_TROUBLE);
        break;

      }

  /* POSIX.2 says that -q overrides -l, which in turn overrides the
     other output options.  */
  if (exit_on_match)
    list_files = 0;
  if (exit_on_match | list_files)
    {
      count_matches = 0;
      done_on_match = 1;
    }
  out_quiet = count_matches | done_on_match;

  if (out_after < 0)
    out_after = default_context;
  if (out_before < 0)
    out_before = default_context;

  if (color_option)
    {
      /* Legacy.  */
      char *userval = getenv ("GREP_COLOR");
      if (userval != NULL && *userval != '\0')
        selected_match_color = context_match_color = userval;

      /* New GREP_COLORS has priority.  */
      parse_grep_colors();
    }

  if (show_version)
    {
      version_etc (stdout, program_name, PACKAGE_NAME, VERSION, AUTHORS, \
                   (char *) NULL);					\
      exit (EXIT_SUCCESS);						\
    }

  if (show_help)
    usage (EXIT_SUCCESS);

  if (keys)
    {
      if (keycc == 0)
        {
          /* No keys were specified (e.g. -f /dev/null).  Match nothing.  */
          out_invert ^= 1;
          match_lines = match_words = 0;
        }
      else
        /* Strip trailing newline. */
        --keycc;
    }
  else
    if (optind < argc)
      {
        /* A copy must be made in case of an xrealloc() or free() later.  */
        keycc = strlen(argv[optind]);
        keys = xmalloc(keycc + 1);
        strcpy(keys, argv[optind++]);
      }
    else
      usage (EXIT_TROUBLE);

  set_limits();
  compile(keys, keycc);
  free (keys);

  if ((argc - optind > 1 && !no_filenames) || with_filenames)
    out_file = 1;

#ifdef SET_BINARY
  /* Output is set to binary mode because we shouldn't convert
     NL to CR-LF pairs, especially when grepping binary files.  */
  if (!isatty (1))
    SET_BINARY (1);
#endif

  if (max_count == 0)
    exit (EXIT_FAILURE);

  if (optind < argc)
    {
        status = 1;
        do
        {
          char *file = argv[optind];
          if ((included_patterns || excluded_patterns)
              && !isdir (file))
            {
              if (included_patterns
                  && excluded_file_name (included_patterns, file))
                continue;
              if (excluded_patterns
                  && excluded_file_name (excluded_patterns, file))
                continue;
            }
          status &= grepfile (STREQ (file, "-") ? (char *) NULL : file,
                              &stats_base);
        }
        while ( ++optind < argc);
    }
  else
    status = grepfile ((char *) NULL, &stats_base);

  /* We register via atexit() to test stdout.  */
  exit (errseen ? EXIT_TROUBLE : status);
}
/* vim:set shiftwidth=2: */

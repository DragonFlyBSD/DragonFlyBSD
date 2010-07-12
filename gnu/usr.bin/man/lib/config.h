/*
 * config.h
 *
 * If you haven't read the README file, now might be a good time.
 *
 * Sorry it's so long, but there are lots of things you might want to
 * customize for your site.
 *
 * Copyright (c) 1990, 1991, John W. Eaton.
 *
 * You may distribute under the terms of the GNU General Public
 * License as specified in the file COPYING that comes with the man
 * distribution.
 *
 * John W. Eaton
 * jwe@che.utexas.edu
 * Department of Chemical Engineering
 * The University of Texas at Austin
 * Austin, Texas  78712
 */

/*
 * This should be at least the size of the longest path.
 */
#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

/*
 * This is the maximum number of directories expected in the manpath.
 */
#ifndef MAXDIRS
#define MAXDIRS 64
#endif

/*
 * It's probably best to define absolute paths to all of these.  If
 * you don't, you'll be depending on the user's path to be correct
 * when system () is called.  This can result in weird behavior that's
 * hard to track down, especially after you forget how this program
 * works...  If you don't have some of these programs, simply define
 * them to be empty strings (i.e. "").  As a minimum, you must have
 * nroff installed.
 */
#ifndef APROPOS
#define APROPOS "/usr/bin/apropos"
#endif

#ifndef WHATIS
#define WHATIS "/usr/bin/whatis"
#endif

#ifndef PAGER
#define PAGER "more -s"
#endif

#ifndef TROFF
#define TROFF "/usr/bin/groff -S -man"
#endif

#ifndef NROFF
#define NROFF "/usr/bin/groff -S -Wall -mtty-char -man"
#endif

#ifndef EQN
#define EQN "/usr/bin/eqn"
#endif

#ifndef NEQN
#define NEQN "/usr/bin/eqn"
#endif

#ifndef TBL
#define TBL "/usr/bin/tbl"
#endif

#ifndef COL
#define COL "/usr/bin/col"
#endif

#ifndef VGRIND
#define VGRIND "/usr/bin/vgrind"
#endif

#ifndef REFER
#define REFER "/usr/bin/refer"
#endif

#ifndef GRAP
#define GRAP ""
#endif

#ifndef PIC
#define PIC "/usr/bin/pic"
#endif

/*
 * Define the absolute path to the configuration file.
 */
#ifndef MAN_MAIN
  static char config_file[] = "/etc/manpath.config" ;
#endif

/*
 * Define the uncompression program(s) to use for those preformatted
 * pages that end in the given character.  If you add extras here, you
 * may need to change man.c.  [I have no idea what FCAT and YCAT files
 * are! - I will leave them in for now.. -jkh]
 */
/* .F files */
#define FCAT ""
/* .Y files */
#define YCAT ""
/* .Z files */
#define ZCAT "/usr/bin/zcat -q"

/*
 * This is the standard program to use on this system for compressing
 * pages once they have been formatted, and the character to tack on
 * to the end of those files.  The program listed is expected to read
 * from the standard input and write compressed output to the standard
 * output.  These won't actually be used unless compression is enabled.
 */
#define COMPRESSOR "/usr/bin/gzip -c"
#define COMPRESS_EXT ".gz"

/*
 * Define the standard manual sections.  For example, if your man
 * directory tree has subdirectories man1, man2, man3, mann,
 * and man3foo, std_sections[] would have "1", "2", "3", "n", and
 * "3foo".  Directories are searched in the order they appear.  Having
 * extras isn't fatal, it just slows things down a bit.
 *
 * Note that this is just for directories to search.  If you have
 * files like .../man3/foobar.3Xtc, you don't need to have "3Xtc" in
 * the list below -- this is handled separately, so that `man 3Xtc foobar',
 * `man 3 foobar', and `man foobar' should find the file .../man3/foo.3Xtc,
 * (assuming, of course, that there isn't a .../man1/foo.1 or somesuch
 * that we would find first).
 *
 * Note that this list should be in the order that you want the
 * directories to be searched.  Is there a standard for this?  What is
 * the normal order?  If anyone knows, please tell me!
 */
#ifndef MANPATH_MAIN
  static const char *std_sections[] =
    {
       "1", "1aout", "8", "2", "3", "n", "4", "5", "6", "7", "9", "l", NULL
    };
#endif

/*
 * Not all systems define these in stat.h.
 */
#ifndef S_IRUSR
#define	S_IRUSR	00400		/*  read permission: owner */
#endif
#ifndef S_IWUSR
#define	S_IWUSR	00200		/*  write permission: owner */
#endif
#ifndef S_IRGRP
#define	S_IRGRP	00040		/*  read permission: group */
#endif
#ifndef S_IWGRP
#define	S_IWGRP	00020		/*  write permission: group */
#endif
#ifndef S_IROTH
#define	S_IROTH	00004		/*  read permission: other */
#endif
#ifndef S_IWOTH
#define	S_IWOTH	00002		/*  write permission: other */
#endif

/*
 * This is the mode used for formatted pages that we create.  If you
 * are using the setgid option, you should use 664.  If you are not,
 * you should use 666 and make the cat* directories mode 777.
 */
#ifndef CATMODE
#define CATMODE S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH
#endif

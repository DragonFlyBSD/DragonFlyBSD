/*-
 * Copyright (c) 1988, 1989, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1989 by Berkeley Softworks
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Adam de Boor.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * @(#) Copyright (c) 1988, 1989, 1990, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)main.c	8.3 (Berkeley) 3/19/94
 * $FreeBSD: src/usr.bin/make/main.c,v 1.118 2005/02/13 13:33:56 harti Exp $
 * $DragonFly: src/usr.bin/make/main.c,v 1.146 2007/01/19 07:23:43 dillon Exp $
 */

/*
 * main.c
 *	The main file for this entire program. Exit routines etc
 *	reside here.
 *
 * Utility functions defined in this file:
 *	Main_ParseArgLine
 *			Takes a line of arguments, breaks them and
 *			treats them as if they were given when first
 *			invoked. Used by the parse module to implement
 *			the .MFLAGS target.
 */

#ifndef MACHINE
#include <sys/utsname.h>
#endif
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/queue.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "arch.h"
#include "buf.h"
#include "config.h"
#include "dir.h"
#include "globals.h"
#include "job.h"
#include "make.h"
#include "parse.h"
#include "pathnames.h"
#include "shell.h"
#include "str.h"
#include "suff.h"
#include "targ.h"
#include "util.h"
#include "var.h"

extern char **environ;	/* XXX what header declares this variable? */

/*
 * DEFMAXJOBS
 *	This control the default concurrency. On no occasion will more
 *	than DEFMAXJOBS targets be created at once.
 */
#define	DEFMAXJOBS	1

typedef struct CLI {
	/** ordered list of makefiles to read */
	Lst makefiles;

	/** list of variables to print */
	Lst variables;

	/** Targets to be made */
	Lst create;

	/** directories to search when looking for includes */
	struct Path	parseIncPath;

	/** directories to search when looking for system includes */
	struct Path	sysIncPath;

	bool	expandVars;	/* fully expand printed variables */
	bool	builtins;	/* -r flag */
	bool	forceJobs;      /* -j argument given */

	/**
	 * -q flag:
	 * true if we aren't supposed to really make anything, just
	 * see if the targets are out-of-date
	 */
	bool		queryFlag;
} CLI;

/* (-E) vars to override from env */
Lst envFirstVars = Lst_Initializer(envFirstVars);

bool		allPrecious;	/* .PRECIOUS given on line by itself */
bool		beSilent;	/* -s flag */
bool		beVerbose;	/* -v flag */
bool		compatMake;	/* -B argument */
int		debug;		/* -d flag */
bool		ignoreErrors;	/* -i flag */
int		jobLimit;	/* -j argument */
bool		jobsRunning;	/* true if the jobs might be running */
bool		keepgoing;	/* -k flag */
bool		noExecute;	/* -n flag */
bool		touchFlag;	/* -t flag */
bool		usePipes;	/* !-P flag */
uint32_t	warn_cmd;	/* command line warning flags */
uint32_t	warn_flags;	/* actual warning flags */
uint32_t	warn_nocmd;	/* command line no-warning flags */

time_t		now;		/* Time at start of make */
struct GNode	*DEFAULT;	/* .DEFAULT node */


/**
 * Exit with usage message.
 */
static void
usage(void)
{
	fprintf(stderr,
	    "usage: make [-BPSXeiknqrstv] [-C directory] [-D variable]\n"
	    "\t[-d flags] [-E variable] [-f makefile] [-I directory]\n"
	    "\t[-j max_jobs] [-m directory] [-V variable]\n"
	    "\t[variable=value] [target ...]\n");
	exit(2);
}

/**
 * MFLAGS_append
 *	Append a flag with an optional argument to MAKEFLAGS and MFLAGS
 */
static void
MFLAGS_append(const char *flag, char *arg)
{
	char *str;

	Var_Append(".MAKEFLAGS", flag, VAR_GLOBAL);
	if (arg != NULL) {
		str = MAKEFLAGS_quote(arg);
		Var_Append(".MAKEFLAGS", str, VAR_GLOBAL);
		free(str);
	}

	Var_Append("MFLAGS", flag, VAR_GLOBAL);
	if (arg != NULL) {
		str = MAKEFLAGS_quote(arg);
		Var_Append("MFLAGS", str, VAR_GLOBAL);
		free(str);
	}
}

/**
 * Open and parse the given makefile.
 *
 * Results:
 *	true if ok. false if couldn't open file.
 */
static bool
ReadMakefile(Parser *parser, CLI *cli, const char file[], const char curdir[], const char objdir[])
{
	char	path[MAXPATHLEN];
	FILE	*stream;
	char	*fname;
	char	*name;

	if (!strcmp(file, "-")) {
		Parse_File(parser, cli, "(stdin)", stdin);
		Var_SetGlobal("MAKEFILE", "");
		return (true);
	}

	if (strcmp(curdir, objdir) == 0 || file[0] == '/') {
		strcpy(path, file);
	} else {
		/*
		 * we've chdir'd, rebuild the path name
		 */
		snprintf(path, MAXPATHLEN, "%s/%s", curdir, file);
	}
#if THIS_BREAKS_THINGS
	/*
	 * XXX The realpath stuff breaks relative includes
	 * XXX in some cases.   The problem likely is in
	 * XXX parse.c where it does special things in
	 * XXX ParseDoInclude if the file is relateive
	 * XXX or absolute and not a system file.  There
	 * XXX it assumes that if the current file that's
	 * XXX being included is absolute, that any files
	 * XXX that it includes shouldn't do the -I path
	 * XXX stuff, which is inconsistant with historical
	 * XXX behavior.  However, I can't pentrate the mists
	 * XXX further, so I'm putting this workaround in
	 * XXX here until such time as the underlying bug
	 * XXX can be fixed.
	 */
	if (realpath(path, path) == NULL) {
		stream = NULL;
	} else {
		stream = fopen(path, "r");
	}
#else
	stream = fopen(path, "r");
#endif
	if (stream != NULL) {
		if (strcmp(file, ".depend") != 0)
			Var_SetGlobal("MAKEFILE", file);
		Parse_File(parser, cli, path, stream);
		fclose(stream);
		return (true);
	}

	/* look in -I and system include directories. */
	fname = estrdup(file);
	name = NULL;
	if (name == NULL)
		name = Path_FindFile(fname, &cli->parseIncPath);
	if (name == NULL)
		name = Path_FindFile(fname, &cli->sysIncPath);

	if (name != NULL) {
		stream = fopen(name, "r");
		if (stream != NULL) {
			/*
			 * set the MAKEFILE variable desired by System V fans
			 * -- the placement of the setting here means it gets
			 * set to the last makefile specified, as it is set
			 * by SysV make.
			 */
			if (strcmp(file, ".depend") != 0)
				Var_SetGlobal("MAKEFILE", name);
			Parse_File(parser, cli, name, stream);
			fclose(stream);
			return (true);
		}
	}

	return (false);	/* no makefile found */
}

/**
 * Read in the built-in rules first, followed by the specified
 * makefiles or the one of the default makefiles.  Finally .depend
 * makefile.
 */
static void
ReadInputFiles(Parser *parser, CLI *cli, const char curdir[], const char objdir[])
{
	if (cli->builtins) {
		char	defsysmk[] = PATH_DEFSYSMK;	/* Path of sys.mk */
		Lst	sysMkPath = Lst_Initializer(sysMkPath);
		LstNode	*ln;

		Path_Expand(defsysmk, &cli->sysIncPath, &sysMkPath);
		if (Lst_IsEmpty(&sysMkPath))
			Fatal("make: no system rules (%s).", PATH_DEFSYSMK);

		LST_FOREACH(ln, &sysMkPath) {
			char *name = Lst_Datum(ln);
			if (!ReadMakefile(parser, cli, name, curdir, objdir))
				Fatal("make: cannot open %s.", name);
		}
		Lst_Destroy(&sysMkPath, free);
	}

	if (!Lst_IsEmpty(&cli->makefiles)) {
		LstNode *ln;

		LST_FOREACH(ln, &cli->makefiles) {
			char *name = Lst_Datum(ln);
			if (!ReadMakefile(parser, cli, name, curdir, objdir))
				Fatal("make: cannot open %s.", name);
		}
	} else if (ReadMakefile(parser, cli, "BSDmakefile", curdir, objdir)) {
		/* read BSDmakefile */
	} else if (ReadMakefile(parser, cli, "makefile", curdir, objdir)) {
		/* read makefile */
	} else if (ReadMakefile(parser, cli, "Makefile", curdir, objdir)) {
		/* read Makefile */
	} else {
		/* No Makefile found */
	}

	ReadMakefile(parser, cli, ".depend", curdir, objdir);
}

/**
 * Main_ParseWarn
 *
 *	Handle argument to warning option.
 */
int
Main_ParseWarn(const char *arg, int iscmd)
{
	int i, neg;

	static const struct {
		const char	*option;
		uint32_t	flag;
	} options[] = {
		{ "dirsyntax",	WARN_DIRSYNTAX },
		{ NULL,		0 }
	};

	neg = 0;
	if (arg[0] == 'n' && arg[1] == 'o') {
		neg = 1;
		arg += 2;
	}

	for (i = 0; options[i].option != NULL; i++)
		if (strcmp(arg, options[i].option) == 0)
			break;

	if (options[i].option == NULL)
		/* unknown option */
		return (-1);

	if (iscmd) {
		if (!neg) {
			warn_cmd |= options[i].flag;
			warn_nocmd &= ~options[i].flag;
			warn_flags |= options[i].flag;
		} else {
			warn_nocmd |= options[i].flag;
			warn_cmd &= ~options[i].flag;
			warn_flags &= ~options[i].flag;
		}
	} else {
		if (!neg) {
			warn_flags |= (options[i].flag & ~warn_nocmd);
		} else {
			warn_flags &= ~(options[i].flag | warn_cmd);
		}
	}
	return (0);
}

/**
 * MainParseArgs
 *	Parse a given argument vector. Called from main() and from
 *	Main_ParseArgLine() when the .MAKEFLAGS target is used.
 *
 *	XXX: Deal with command line overriding .MAKEFLAGS in makefile
 *
 * Side Effects:
 *	Various global and local flags will be set depending on the flags
 *	given
 */
static void
MainParseArgs(CLI *cli, int argc, char **argv)
{
	int c;
	bool	found_dd = false;

rearg:
	optind = 1;	/* since we're called more than once */
	optreset = 1;
#define OPTFLAGS "ABC:D:E:I:PSV:Xd:ef:ij:km:nqrstvx:"
	for (;;) {
		if ((optind < argc) && strcmp(argv[optind], "--") == 0) {
			found_dd = true;
		}
		if ((c = getopt(argc, argv, OPTFLAGS)) == -1) {
			break;
		}
		switch(c) {

		case 'A':
			arch_fatal = false;
			MFLAGS_append("-A", NULL);
			break;
		case 'C':
			if (chdir(optarg) == -1)
				err(1, "chdir %s", optarg);
			break;
		case 'D':
			Var_SetGlobal(optarg, "1");
			MFLAGS_append("-D", optarg);
			break;
		case 'I':
			Path_AddDir(&cli->parseIncPath, optarg);
			MFLAGS_append("-I", optarg);
			break;
		case 'V':
			Lst_AtEnd(&cli->variables, estrdup(optarg));
			MFLAGS_append("-V", optarg);
			break;
		case 'X':
			cli->expandVars = false;
			break;
		case 'B':
			compatMake = true;
			MFLAGS_append("-B", NULL);
			unsetenv("MAKE_JOBS_FIFO");
			break;
		case 'P':
			usePipes = false;
			MFLAGS_append("-P", NULL);
			break;
		case 'S':
			keepgoing = false;
			MFLAGS_append("-S", NULL);
			break;
		case 'd': {
			char *modules = optarg;

			for (; *modules; ++modules)
				switch (*modules) {
				case 'A':
					debug = ~0;
					break;
				case 'a':
					debug |= DEBUG_ARCH;
					break;
				case 'c':
					debug |= DEBUG_COND;
					break;
				case 'd':
					debug |= DEBUG_DIR;
					break;
				case 'f':
					debug |= DEBUG_FOR;
					break;
				case 'g':
					if (modules[1] == '1') {
						debug |= DEBUG_GRAPH1;
						++modules;
					}
					else if (modules[1] == '2') {
						debug |= DEBUG_GRAPH2;
						++modules;
					}
					break;
				case 'j':
					debug |= DEBUG_JOB;
					break;
				case 'l':
					debug |= DEBUG_LOUD;
					break;
				case 'm':
					debug |= DEBUG_MAKE;
					break;
				case 's':
					debug |= DEBUG_SUFF;
					break;
				case 't':
					debug |= DEBUG_TARG;
					break;
				case 'v':
					debug |= DEBUG_VAR;
					break;
				default:
					warnx("illegal argument to d option "
					    "-- %c", *modules);
					usage();
				}
			MFLAGS_append("-d", optarg);
			break;
		}
		case 'E':
			Lst_AtEnd(&envFirstVars, estrdup(optarg));
			MFLAGS_append("-E", optarg);
			break;
		case 'e':
			checkEnvFirst = true;
			MFLAGS_append("-e", NULL);
			break;
		case 'f':
			Lst_AtEnd(&cli->makefiles, estrdup(optarg));
			break;
		case 'i':
			ignoreErrors = true;
			MFLAGS_append("-i", NULL);
			break;
		case 'j': {
			char *endptr;

			cli->forceJobs = true;
			jobLimit = strtol(optarg, &endptr, 10);
			if (jobLimit <= 0 || *endptr != '\0') {
				warnx("illegal number, -j argument -- %s",
				    optarg);
				usage();
			}
			MFLAGS_append("-j", optarg);
			break;
		}
		case 'k':
			keepgoing = true;
			MFLAGS_append("-k", NULL);
			break;
		case 'm':
			Path_AddDir(&cli->sysIncPath, optarg);
			MFLAGS_append("-m", optarg);
			break;
		case 'n':
			noExecute = true;
			MFLAGS_append("-n", NULL);
			break;
		case 'q':
			cli->queryFlag = true;
			/* Kind of nonsensical, wot? */
			MFLAGS_append("-q", NULL);
			break;
		case 'r':
			cli->builtins = false;
			MFLAGS_append("-r", NULL);
			break;
		case 's':
			beSilent = true;
			MFLAGS_append("-s", NULL);
			break;
		case 't':
			touchFlag = true;
			MFLAGS_append("-t", NULL);
			break;
		case 'v':
			beVerbose = true;
			MFLAGS_append("-v", NULL);
			break;
		case 'x':
			if (Main_ParseWarn(optarg, 1) != -1)
				MFLAGS_append("-x", optarg);
			break;
		default:
		case '?':
			usage();
		}
	}
	argv += optind;
	argc -= optind;

	oldVars = true;

	/*
	 * Parse the rest of the arguments.
	 *	o Check for variable assignments and perform them if so.
	 *	o Check for more flags and restart getopt if so.
	 *      o Anything else is taken to be a target and added
	 *	  to the end of the "create" list.
	 */
	for (; *argv != NULL; ++argv, --argc) {
		if (Parse_IsVar(*argv)) {
			char *ptr = MAKEFLAGS_quote(*argv);

			Var_Append(".MAKEFLAGS", ptr, VAR_GLOBAL);
			Parse_DoVar(*argv, VAR_CMD);
			free(ptr);

		} else if ((*argv)[0] == '-') {
			if ((*argv)[1] == '\0') {
				/*
				 * (*argv) is a single dash, so we
				 * just ignore it.
				 */
			} else if (found_dd) {
				/*
				 * Double dash has been found, ignore
				 * any more options.  But what do we do
				 * with it?  For now treat it like a target.
				 */
				Lst_AtEnd(&cli->create, estrdup(*argv));
			} else {
				/*
				 * (*argv) is a -flag, so backup argv and
				 * argc.  getopt() expects options to start
				 * in the 2nd position.
				 */
				argc++;
				argv--;
				goto rearg;
			}

		} else if ((*argv)[0] == '\0') {
			Punt("illegal (null) argument.");

		} else {
			Lst_AtEnd(&cli->create, estrdup(*argv));
		}
	}
}

/**
 * Main_ParseArgLine
 *	Used by the parse module when a .MFLAGS or .MAKEFLAGS target
 *	is encountered and by main() when reading the .MAKEFLAGS envariable.
 *	Takes a line of arguments and breaks it into its
 *	component words and passes those words and the number of them to the
 *	MainParseArgs function.
 *	The line should have all its leading whitespace removed.
 *
 * Side Effects:
 *	Only those that come from the various arguments.
 */
void
Main_ParseArgLine(CLI *cli, const char line[], int mflags)
{
	ArgArray	aa;

	if (mflags) {
		MAKEFLAGS_break(&aa, line);
	} else {
		brk_string(&aa, line, true);
	}
	MainParseArgs(cli, aa.argc, aa.argv);
	ArgArray_Done(&aa);
}

/**
 * Try to change the current working directory to path, and return
 * the whole path using getcwd().
 *
 * @note for amd managed mount points we really should use pawd(1).
 */
static int
CheckDir(const char path[], char newdir[])
{
	struct stat sb;

	/*
	 * Check if the path is a directory.  If not fail without reporting
	 * an error.
	 */
	if (stat(path, &sb) < 0) {
		return (0);
	}
	if (S_ISDIR(sb.st_mode) == 0) {
		return (0);
	}

	/*
	 * The path refers to a directory, so we try to change into it. If we
	 * fail, or we fail to obtain the path from root to the directory,
	 * then report an error and fail.
	 */
	if (chdir(path) < 0) {
		warn("warning: %s", path);
		return (0);
	}
	if (getcwd(newdir, MAXPATHLEN) == NULL) {
		warn("warning: %s", path);
		return (0);
	}

	/*
	 * Directory in path is accessable, newdir should now contain the
	 * path to it.
	 */
	return (1);
}

/**
 * Determine location of the object directory.
 */
static void
FindObjDir(const char machine[], char curdir[], char objdir[])
{
	struct stat	sa;
	char		newdir[MAXPATHLEN];
	char		mdpath[MAXPATHLEN];
	const char	*env;

	/*
	 * Find a path to where we are... [-C directory] might have changed
	 * our current directory.
	 */
	if (getcwd(curdir, MAXPATHLEN) == NULL)
		err(2, NULL);

	if (stat(curdir, &sa) == -1)
		err(2, "%s", curdir);

	/*
	 * The object directory location is determined using the
	 * following order of preference:
	 *
	 *	1. MAKEOBJDIRPREFIX`cwd`
	 *	2. MAKEOBJDIR
	 *	3. PATH_OBJDIR.${MACHINE}
	 *	4. PATH_OBJDIR
	 *	5. PATH_OBJDIRPREFIX`cwd`
	 *
	 * If one of the first two fails, use the current directory.
	 * If the remaining three all fail, use the current directory.
	 */
	if ((env = getenv("MAKEOBJDIRPREFIX")) != NULL) {
		snprintf(mdpath, MAXPATHLEN, "%s%s", env, curdir);
		if (CheckDir(mdpath, newdir)) {
			strcpy(objdir, newdir);
			return;
		}
		strcpy(objdir, curdir);
		return;
	}

	if ((env = getenv("MAKEOBJDIR")) != NULL) {
		if (CheckDir(env, newdir)) {
			strcpy(objdir, newdir);
			return;
		}
		strcpy(objdir, curdir);
		return;
	}

	snprintf(mdpath, MAXPATHLEN, "%s.%s", PATH_OBJDIR, machine);
	if (CheckDir(mdpath, newdir)) {
		strcpy(objdir, newdir);
		return;
	}

	if (CheckDir(PATH_OBJDIR, newdir)) {
		strcpy(objdir, newdir);
		return;
	}

	snprintf(mdpath, MAXPATHLEN, "%s%s", PATH_OBJDIRPREFIX, curdir);
	if (CheckDir(mdpath, newdir)) {
		strcpy(objdir, newdir);
		return;
	}

	strcpy(objdir, curdir);
}

/**
 * Initialize various make variables.
 *	MAKE also gets this name, for compatibility
 *	.MAKEFLAGS gets set to the empty string just in case.
 *	MFLAGS also gets initialized empty, for compatibility.
 */
static void
InitVariables(const char progname[])
{
	const char	*machine_platform;
	const char	*machine_arch;
	const char	*machine;
	char buf[256];
	size_t bufsiz;

	Var_SetGlobal("MAKE", progname);
	Var_SetGlobal(".MAKEFLAGS", "");
	Var_SetGlobal("MFLAGS", "");

	Var_SetGlobal(".DIRECTIVE_MAKEENV", "YES");
	Var_SetGlobal(".ST_EXPORTVAR", "YES");
#ifdef MAKE_VERSION
	Var_SetGlobal("MAKE_VERSION", MAKE_VERSION);
#endif

	/*
	 * The make program defines MACHINE_PLATFORM, MACHINE and MACHINE_ARCH.
	 * These parameters are taken from the running system but can be
	 * overridden by environment variables.
	 *
	 * MACHINE_PLATFORM	
	 *		- This is the platform, e.g. "vkernel", "pc32", 
	 *		  and so forth.
	 *
	 * MACHINE	- This is the machine architecture and in most
	 *		  cases is the same as the cpu architecture.
	 *
	 * MACHINE_ARCH - This is the cpu architecture, for example "i386".
	 *		  Several different platforms may use the same
	 *		  cpu architecture.
	 *
	 * In most, but not all cases, MACHINE == MACHINE_ARCH.
	 *
	 * PLATFORM distinguishes differences between, say, a virtual kernel
	 * build and a real kernel build.
	 */
	if ((machine_platform = getenv("MACHINE_PLATFORM")) == NULL) {
		bufsiz = sizeof(buf);
		if (sysctlbyname("hw.platform", buf, &bufsiz, NULL, 0) < 0)
			machine_platform = "unknown";
		else
			machine_platform = strdup(buf);
	}

	if ((machine = getenv("MACHINE")) == NULL) {
		bufsiz = sizeof(buf);
		if (sysctlbyname("hw.machine", buf, &bufsiz, NULL, 0) < 0)
			machine = "unknown";
		else
			machine = strdup(buf);
	}

	if ((machine_arch = getenv("MACHINE_ARCH")) == NULL) {
		bufsiz = sizeof(buf);
		if (sysctlbyname("hw.machine_arch", buf, &bufsiz, NULL, 0) < 0)
			machine_arch = "unknown";
		else
			machine_arch = strdup(buf);
	}

	Var_SetGlobal("MACHINE_PLATFORM", machine_platform);
	Var_SetGlobal("MACHINE", machine);
	Var_SetGlobal("MACHINE_ARCH", machine_arch);
}

/**
 * Build targets given in the command line or if none were given
 * use the main target determined by the parsing module.
 */
static int
BuildStuff(CLI *cli)
{
	int	status;
	Lst	targs = Lst_Initializer(targs);

	if (Lst_IsEmpty(&cli->create))
		Parse_MainName(&targs);
	else
		Targ_FindList(&targs, &cli->create, TARG_CREATE);

	/* Traverse the graph, checking on all the targets */
	if (compatMake) {
		Sig_Init(true);
		status = Compat_Run(&targs, cli->queryFlag);
	} else {
		Sig_Init(false);
		status = Make_Run(&targs, cli->queryFlag);
	}

	Lst_Destroy(&targs, NOFREE);
	return (status);
}

/**
 * main
 *	The main function, for obvious reasons. Initializes variables
 *	and a few modules, then parses the arguments give it in the
 *	environment and on the command line. Reads the system makefile
 *	followed by either Makefile, makefile or the file given by the
 *	-f argument. Sets the .MAKEFLAGS PMake variable based on all the
 *	flags it has received by then uses either the Make or the Compat
 *	module to create the initial list of targets.
 *
 * Results:
 *	If -q was given, exits -1 if anything was out-of-date. Else it exits
 *	0.
 *
 * Side Effects:
 *	The program exits when done. Targets are created. etc. etc. etc.
 */
int
main(int argc, char **argv)
{
	CLI		cli;
	Parser		parser;
	Shell		*shell;
	int		status;		/* exit status */
	char		curdir[MAXPATHLEN];	/* startup directory */
	char		objdir[MAXPATHLEN];	/* where we chdir'ed to */
	const char	*make_flags;

	/*------------------------------------------------------------*
	 * This section initializes stuff that require no input.
	 *------------------------------------------------------------*/
	/*
	 * Initialize program globals.
	 */
	beSilent = false;		/* Print commands as executed */
	ignoreErrors = false;		/* Pay attention to non-zero returns */
	noExecute = false;		/* Execute all commands */
	keepgoing = false;		/* Stop on error */
	allPrecious = false;		/* Remove targets when interrupted */
	touchFlag = false;		/* Actually update targets */
	usePipes = true;		/* Catch child output in pipes */
	debug = 0;			/* No debug verbosity, please. */
	jobsRunning = false;

	jobLimit = DEFMAXJOBS;
	compatMake = false;		/* No compat mode */

	/*
	 * Initialize program flags.
	 */
	Lst_Init(&cli.makefiles);
	Lst_Init(&cli.variables);
	Lst_Init(&cli.create);
	TAILQ_INIT(&cli.parseIncPath);
	TAILQ_INIT(&cli.sysIncPath);

	cli.expandVars = true;
	cli.builtins = true;		/* Read the built-in rules */
	cli.queryFlag = false;
	cli.forceJobs = false;

	shell = Shell_Match(DEFSHELLNAME);

	/*
	 * Initialize the various modules.
	 */
	Proc_Init();
	commandShell = shell;
	Targ_Init();
	Suff_Init();
	Dir_Init();

	/*------------------------------------------------------------*
	 * This section initializes stuff that depend on things
	 * in the enviornment, command line, or a input file.
	 *------------------------------------------------------------*/
	Var_Init(environ);

	InitVariables(argv[0]);

	/*
	 * First snag things out of the MAKEFLAGS environment
	 * variable.  Then parse the command line arguments.
	 */
	if ((make_flags = getenv("MAKEFLAGS")) != NULL) {
		Main_ParseArgLine(&cli, make_flags, 1);
	}
	MainParseArgs(&cli, argc, argv);

	FindObjDir(Var_Value("MACHINE", VAR_GLOBAL), curdir, objdir);
	Var_SetGlobal(".CURDIR", curdir);
	Var_SetGlobal(".OBJDIR", objdir);

	/*
	 * Set up the .TARGETS variable to contain the list of targets to be
	 * created. If none specified, make the variable empty -- the parser
	 * will fill the thing in with the default or .MAIN target.
	 */
	if (Lst_IsEmpty(&cli.create)) {
		Var_SetGlobal(".TARGETS", "");
	} else {
		LstNode *ln;

		LST_FOREACH(ln, &cli.create) {
			char *name = Lst_Datum(ln);

			Var_Append(".TARGETS", name, VAR_GLOBAL);
		}
	}

	Dir_CurObj(curdir, objdir);

	/*
	 * If no user-supplied system path was given (through the -m option)
	 * add the directories from the DEFSYSPATH (more than one may be given
	 * as dir1:...:dirn) to the system include path.
	 */
	if (TAILQ_EMPTY(&cli.sysIncPath)) {
		char syspath[] = PATH_DEFSYSPATH;
		char *start = syspath;
		char *cp;

		while ((cp = strsep(&start, ":")) != NULL) {
			Path_AddDir(&cli.sysIncPath, cp);
		}
	}

	if (getenv("MAKE_JOBS_FIFO") != NULL)
		cli.forceJobs = true;

	/*
	 * Be compatible if user did not specify -j and did not explicitly
	 * turned compatibility on
	 */
	if (compatMake == false && cli.forceJobs == false)
		compatMake = true;

	DEFAULT = NULL;
	time(&now);

	parser.create = &cli.create;
	parser.parseIncPath = &cli.parseIncPath;
	parser.sysIncPath = &cli.sysIncPath;

	ReadInputFiles(&parser, &cli, curdir, objdir);

	/*------------------------------------------------------------*
	 * We are finished processing inputs.
	 *------------------------------------------------------------*/

	/* Install all the flags into the MAKE envariable. */
	{
		const char *p;

		p = Var_Value(".MAKEFLAGS", VAR_GLOBAL);
		if (p != NULL && *p != '\0') {
			if (setenv("MAKEFLAGS", p, 1) == -1)
				Punt("setenv: MAKEFLAGS: can't allocate memory");
		}
	}

	/*
	 * For compatibility, look at the directories in the VPATH variable
	 * and add them to the search path, if the variable is defined. The
	 * variable's value is in the same format as the PATH envariable, i.e.
	 * <directory>:<directory>:<directory>...
	 */
	if (Var_Value("VPATH", VAR_CMD) != NULL) {
		Buffer	*buf = Var_Subst("${VPATH}", VAR_CMD, false);
		char	*start = Buf_Data(buf);
		char	*cp;

		while ((cp = strsep(&start, ":")) != NULL) {
			Path_AddDir(&dirSearchPath, cp);
		}

		Buf_Destroy(buf, true);
	}

	/*
	 * Now that all search paths have been read for suffixes et al, it's
	 * time to add the default search path to their lists...
	 */
	Suff_DoPaths();

	/* print the initial graph, if the user requested it */
	if (DEBUG(GRAPH1))
		Targ_PrintGraph(1);

	if (Lst_IsEmpty(&cli.variables)) {
		status = BuildStuff(&cli);
	} else {
		/* Print the values of variables requested by the user. */
		Var_Print(&cli.variables, cli.expandVars);

		/*
		 * XXX
		 * This should be a "don't care", we do not check
		 * the status of any files.  It might make sense to
		 * modify Var_Print() to indicate that one of the
		 * requested variables did not exist, and use that
		 * as the return status.
		 * XXX
		 */
		status = cli.queryFlag ? 1 : 0;
	}

	/* print the graph now it's been processed if the user requested it */
	if (DEBUG(GRAPH2))
		Targ_PrintGraph(2);

#if 0
	TAILQ_DESTROY(&cli.sysIncPath);
	TAILQ_DESTROY(&cli.parseIncPath);
#endif
	Lst_Destroy(&cli.create, free);
	Lst_Destroy(&cli.variables, free);
	Lst_Destroy(&cli.makefiles, free);

	return (status);
}


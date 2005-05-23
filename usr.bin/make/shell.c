/*-
 * Copyright (c) 1988, 1989, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1988, 1989 by Adam de Boor
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
 * $DragonFly: src/usr.bin/make/shell.c,v 1.12 2005/05/23 18:19:05 okumoto Exp $
 */

#include <string.h>
#include <stdlib.h>

#include "make.h"
#include "parse.h"
#include "pathnames.h"
#include "shell.h"
#include "str.h"
#include "util.h"

struct Shell	*commandShell = NULL;

/**
 * Find a matching shell in 'shells' given its final component.
 *
 * @result
 *	A pointer to a Shell structure, or NULL if no shell with
 *	the given name is found.
 */
static struct Shell *
ShellMatch(const char name[])
{
	struct Shell	*shell;
	const char	*shellDir = PATH_DEFSHELLDIR;

	shell = emalloc(sizeof(struct Shell));

	if (strcmp(name, "csh") == 0) {
		/*
		 * CSH description. The csh can do echo control by playing
		 * with the setting of the 'echo' shell variable. Sadly,
		 * however, it is unable to do error control nicely.
		 */
		shell->name		= strdup(name);
		shell->path		= str_concat(shellDir, '/', name);
		shell->hasEchoCtl	= TRUE;
		shell->echoOff		= strdup("unset verbose");
		shell->echoOn		= strdup("set verbose");
		shell->noPrint		= strdup("unset verbose");
		shell->hasErrCtl	= FALSE;
		shell->errCheck		= strdup("echo \"%s\"\n");
		shell->ignErr		= strdup("csh -c \"%s || exit 0\"");
		shell->echo		= strdup("v");
		shell->exit		= strdup("e");

	} else if (strcmp(name, "sh") == 0) {
		/*
		 * SH description. Echo control is also possible and, under
		 * sun UNIX anyway, one can even control error checking.
		 */

		shell->name		= strdup(name);
		shell->path		= str_concat(shellDir, '/', name);
		shell->hasEchoCtl	= TRUE;
		shell->echoOff		= strdup("set -");
		shell->echoOn		= strdup("set -v");
		shell->noPrint		= strdup("set -");
#ifdef OLDBOURNESHELL
		shell->hasErrCtl	= FALSE;
		shell->errCheck		= strdup("echo \"%s\"\n");
		shell->ignErr		= strdup("sh -c '%s || exit 0'\n");
#else
		shell->hasErrCtl	= TRUE;
		shell->errCheck		= strdup("set -e");
		shell->ignErr		= strdup("set +e");
#endif
		shell->echo		= strdup("v");
		shell->exit		= strdup("e");
	} else if (strcmp(name, "ksh") == 0) {
		/*
		 * KSH description. The Korn shell has a superset of
		 * the Bourne shell's functionality.
		 */
		shell->name		= strdup(name);
		shell->path		= str_concat(shellDir, '/', name);
		shell->hasEchoCtl	= TRUE;
		shell->echoOff		= strdup("set -");
		shell->echoOn		= strdup("set -v");
		shell->noPrint		= strdup("set -");
		shell->hasErrCtl	= TRUE;
		shell->errCheck		= strdup("set -e");
		shell->ignErr		= strdup("set +e");
		shell->echo		= strdup("v");
		shell->exit		= strdup("e");
	} else {
		free(shell);
		shell = NULL;
	}

	return (shell);
}

/**
 * Make a new copy of the shell structure including a copy of the strings
 * in it. This also defaults some fields in case they are NULL.
 *
 * Returns:
 *	The function returns a pointer to the new shell structure.
 */
static struct Shell *
ShellCopy(const struct Shell *o)
{
	struct Shell *n;

	n = emalloc(sizeof(struct Shell));
	n->name		= estrdup(o->name);
	n->path		= estrdup(o->path);
	n->hasEchoCtl	= o->hasEchoCtl;
	n->echoOff	= o->echoOff ? estrdup(o->echoOff) : NULL;
	n->echoOn	= o->echoOn ? estrdup(o->echoOn) : NULL;
	n->noPrint	= o->noPrint ? estrdup(o->noPrint) : NULL;
	n->hasErrCtl	= o->hasErrCtl;
	n->errCheck	= o->errCheck ? estrdup(o->errCheck) : estrdup("");
	n->ignErr	= o->ignErr ? estrdup(o->ignErr) : estrdup("%s");
	n->echo		= o->echo ? estrdup(o->echo) : estrdup("");
	n->exit		= o->exit ? estrdup(o->exit) : estrdup("");

	return (n);
}

/**
 * Free a shell structure and all associated strings.
 */
static void
ShellFree(struct Shell *sh)
{

	if (sh != NULL) {
		free(sh->name);
		free(sh->echoOff);
		free(sh->echoOn);
		free(sh->noPrint);
		free(sh->errCheck);
		free(sh->ignErr);
		free(sh->echo);
		free(sh->exit);
		free(sh);
	}
}

/**
 * Given the line following a .SHELL target, parse the
 * line as a shell specification. Returns FALSE if the
 * spec was incorrect.
 *
 * Parse a shell specification and set up commandShell.
 *
 * Results:
 *	TRUE if the specification was correct. FALSE otherwise.
 *
 * Side Effects:
 *	commandShell points to a Shell structure (either predefined or
 *	created from the shell spec)
 *
 * Notes:
 *	A shell specification consists of a .SHELL target, with dependency
 *	operator, followed by a series of blank-separated words. Double
 *	quotes can be used to use blanks in words. A backslash escapes
 *	anything (most notably a double-quote and a space) and
 *	provides the functionality it does in C. Each word consists of
 *	keyword and value separated by an equal sign. There should be no
 *	unnecessary spaces in the word. The keywords are as follows:
 *	    name	    Name of shell.
 *	    path	    Location of shell. Overrides "name" if given
 *	    quiet	    Command to turn off echoing.
 *	    echo	    Command to turn echoing on
 *	    filter	    Result of turning off echoing that shouldn't be
 *			    printed.
 *	    echoFlag	    Flag to turn echoing on at the start
 *	    errFlag	    Flag to turn error checking on at the start
 *	    hasErrCtl	    True if shell has error checking control
 *	    check	    Command to turn on error checking if hasErrCtl
 *			    is TRUE or template of command to echo a command
 *			    for which error checking is off if hasErrCtl is
 *			    FALSE.
 *	    ignore	    Command to turn off error checking if hasErrCtl
 *			    is TRUE or template of command to execute a
 *			    command so as to ignore any errors it returns if
 *			    hasErrCtl is FALSE.
 */
Boolean
Shell_Parse(const char line[])
{
	ArgArray	aa;
	int		argc;
	char		**argv;
	Boolean		fullSpec = FALSE;
	struct Shell	newShell;
	struct Shell	*sh;

	memset(&newShell, 0, sizeof(newShell));

	/*
	 * Parse the specification by keyword but skip the first word
	 */
	brk_string(&aa, line, TRUE);

	for (argc = aa.argc - 1, argv = aa.argv + 1; argc != 0; argc--, argv++)
	{
		char		*eq;

		/*
		 * Split keyword and value
		 */
		if ((eq = strchr(*argv, '=')) == NULL) {
			Parse_Error(PARSE_FATAL,
			    "missing '=' in shell specification keyword '%s'",
			    *argv);
			ArgArray_Done(&aa);
			return (FALSE);
		}
		*eq++ = '\0';

		if (strcmp(*argv, "path") == 0) {
			newShell.path = eq;
		} else if (strcmp(*argv, "name") == 0) {
			newShell.name = eq;
		} else if (strcmp(*argv, "quiet") == 0) {
			newShell.echoOff = eq;
			fullSpec = TRUE;
		} else if (strcmp(*argv, "echo") == 0) {
			newShell.echoOn = eq;
			fullSpec = TRUE;
		} else if (strcmp(*argv, "filter") == 0) {
			newShell.noPrint = eq;
			fullSpec = TRUE;
		} else if (strcmp(*argv, "echoFlag") == 0) {
			newShell.echo = eq;
			fullSpec = TRUE;
		} else if (strcmp(*argv, "errFlag") == 0) {
			newShell.exit = eq;
			fullSpec = TRUE;
		} else if (strcmp(*argv, "hasErrCtl") == 0) {
			newShell.hasErrCtl = (*eq == 'Y' || *eq == 'y' ||
			    *eq == 'T' || *eq == 't');
			fullSpec = TRUE;
		} else if (strcmp(*argv, "check") == 0) {
			newShell.errCheck = eq;
			fullSpec = TRUE;
		} else if (strcmp(*argv, "ignore") == 0) {
			newShell.ignErr = eq;
			fullSpec = TRUE;
		} else {
			Parse_Error(PARSE_FATAL,
			    "unknown keyword in shell specification '%s'",
			    *argv);
			ArgArray_Done(&aa);
			return (FALSE);
		}
	}

	/*
	 * Some checks (could be more)
	 */
	if (fullSpec) {
		if ((newShell.echoOn != NULL) ^ (newShell.echoOff != NULL))
			Parse_Error(PARSE_FATAL,
			    "Shell must have either both "
			    "echoOff and echoOn or none of them");

		if (newShell.echoOn != NULL && newShell.echoOff)
			newShell.hasEchoCtl = TRUE;
	}

	if (newShell.path == NULL) {
		/*
		 * If no path was given, the user wants one of the pre-defined
		 * shells, yes? So we find the one s/he wants with the help of
		 * ShellMatch and set things up the right way.
		 */
		if (newShell.name == NULL) {
			Parse_Error(PARSE_FATAL,
			    "Neither path nor name specified");
			ArgArray_Done(&aa);
			return (FALSE);
		}
		if ((sh = ShellMatch(newShell.name)) == NULL) {
			Parse_Error(PARSE_FATAL, "%s: no matching shell",
			    newShell.name);
			ArgArray_Done(&aa);
			return (FALSE);
		}

	} else {
		/*
		 * The user provided a path. If s/he gave nothing else
		 * (fullSpec is FALSE), try and find a matching shell in the
		 * ones we know of. Else we just take the specification at its
		 * word and copy it to a new location. In either case, we need
		 * to record the path the user gave for the shell.
		 */
		if (newShell.name == NULL) {
			/* get the base name as the name */
			newShell.name = strrchr(newShell.path, '/');
			if (newShell.name == NULL) {
				newShell.name = newShell.path;
			} else {
				newShell.name += 1;
			}
		}

		if (!fullSpec) {
			if ((sh = ShellMatch(newShell.name)) == NULL) {
				Parse_Error(PARSE_FATAL,
				    "%s: no matching shell", newShell.name);
				ArgArray_Done(&aa);
				return (FALSE);
			}
		} else {
			sh = ShellCopy(&newShell);
		}
	}

	/* Release the old shell and set the new shell */
	ShellFree(commandShell);
	commandShell = sh;

	ArgArray_Done(&aa);
	return (TRUE);
}

void
Shell_Init(void)
{
	commandShell = ShellMatch(DEFSHELLNAME);
}


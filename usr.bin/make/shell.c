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
 * $DragonFly: src/usr.bin/make/shell.c,v 1.5 2005/05/19 17:09:20 okumoto Exp $
 */

#include <string.h>
#include <stdlib.h>

#include "make.h"
#include "parse.h"
#include "pathnames.h"
#include "shell.h"
#include "str.h"

/**
 * Descriptions for various shells.
 */
static const struct CShell shells[] = {
	/*
	 * CSH description. The csh can do echo control by playing
	 * with the setting of the 'echo' shell variable. Sadly,
	 * however, it is unable to do error control nicely.
	 */
	{
		"csh",
		TRUE, "unset verbose", "set verbose", "unset verbose",
		FALSE, "echo \"%s\"\n", "csh -c \"%s || exit 0\"",
		"v", "e",
	},
	/*
	 * SH description. Echo control is also possible and, under
	 * sun UNIX anyway, one can even control error checking.
	 */
	{
		"sh",
		TRUE, "set -", "set -v", "set -",
		TRUE, "set -e", "set +e",
#ifdef OLDBOURNESHELL
		FALSE, "echo \"%s\"\n", "sh -c '%s || exit 0'\n",
#endif
		"v", "e",
	},
	/*
	 * KSH description. The Korn shell has a superset of
	 * the Bourne shell's functionality.
	 */
	{
		"ksh",
		TRUE, "set -", "set -v", "set -",
		TRUE, "set -e", "set +e",
		"v", "e",
	},
};

static char	*shellName = NULL;	/* last component of shell */
char		*shellPath = NULL;	/* full pathname of executable image */
struct Shell	*commandShell = NULL;

/**
 * Find a matching shell in 'shells' given its final component.
 *
 * Results:
 *	A pointer to a freshly allocated Shell structure with a copy
 *	of the static structure or NULL if no shell with the given name
 *	is found.
 */
static struct Shell *
JobMatchShell(const char name[])
{
	const struct CShell	*sh;	      /* Pointer into shells table */
	struct Shell		*nsh;

	for (sh = shells; sh < shells + __arysize(shells); sh++)
		if (strcmp(sh->name, name) == 0)
			break;

	if (sh == shells + __arysize(shells))
		return (NULL);

	/* make a copy */
	nsh = emalloc(sizeof(*nsh));

	nsh->name	= estrdup(sh->name);
	nsh->echoOff	= estrdup(sh->echoOff);
	nsh->echoOn	= estrdup(sh->echoOn);
	nsh->hasEchoCtl	= sh->hasEchoCtl;
	nsh->noPrint	= estrdup(sh->noPrint);
	nsh->hasErrCtl	= sh->hasErrCtl;
	nsh->errCheck	= estrdup(sh->errCheck);
	nsh->ignErr	= estrdup(sh->ignErr);
	nsh->echo	= estrdup(sh->echo);
	nsh->exit	= estrdup(sh->exit);

	return (nsh);
}

/**
 * Make a new copy of the shell structure including a copy of the strings
 * in it. This also defaults some fields in case they are NULL.
 *
 * Returns:
 *	The function returns a pointer to the new shell structure.
 */
static struct Shell *
JobCopyShell(const struct Shell *osh)
{
	struct Shell *nsh;

	nsh = emalloc(sizeof(*nsh));
	nsh->name = estrdup(osh->name);

	if (osh->echoOff != NULL)
		nsh->echoOff = estrdup(osh->echoOff);
	else
		nsh->echoOff = NULL;
	if (osh->echoOn != NULL)
		nsh->echoOn = estrdup(osh->echoOn);
	else
		nsh->echoOn = NULL;
	nsh->hasEchoCtl = osh->hasEchoCtl;

	if (osh->noPrint != NULL)
		nsh->noPrint = estrdup(osh->noPrint);
	else
		nsh->noPrint = NULL;

	nsh->hasErrCtl = osh->hasErrCtl;
	if (osh->errCheck == NULL)
		nsh->errCheck = estrdup("");
	else
		nsh->errCheck = estrdup(osh->errCheck);
	if (osh->ignErr == NULL)
		nsh->ignErr = estrdup("%s");
	else
		nsh->ignErr = estrdup(osh->ignErr);

	if (osh->echo == NULL)
		nsh->echo = estrdup("");
	else
		nsh->echo = estrdup(osh->echo);

	if (osh->exit == NULL)
		nsh->exit = estrdup("");
	else
		nsh->exit = estrdup(osh->exit);

	return (nsh);
}

/**
 * Free a shell structure and all associated strings.
 */
static void
JobFreeShell(struct Shell *sh)
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
 * Parse a shell specification and set up commandShell, shellPath appropriately.
 *
 * Results:
 *	FAILURE if the specification was incorrect.
 *
 * Side Effects:
 *	commandShell points to a Shell structure (either predefined or
 *	created from the shell spec), shellPath is the full path of the
 *	shell described by commandShell, while shellName is just the
 *	final component of shellPath.
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
ReturnStatus
Job_ParseShell(const char line[])
{
	ArgArray	aa;
	char		**argv;
	int		argc;
	char		*path;
	Boolean		fullSpec = FALSE;
	struct Shell	newShell;
	struct Shell	*sh;

	memset(&newShell, 0, sizeof(newShell));
	path = NULL;

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
			Parse_Error(PARSE_FATAL, "missing '=' in shell "
			    "specification keyword '%s'", *argv);
			ArgArray_Done(&aa);
			return (FAILURE);
		}
		*eq++ = '\0';

		if (strcmp(*argv, "path") == 0) {
			path = eq;
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
			Parse_Error(PARSE_FATAL, "unknown keyword in shell "
			    "specification '%s'", *argv);
			ArgArray_Done(&aa);
			return (FAILURE);
		}
	}

	/*
	 * Some checks (could be more)
	 */
	if (fullSpec) {
		if ((newShell.echoOn != NULL) ^ (newShell.echoOff != NULL))
			Parse_Error(PARSE_FATAL, "Shell must have either both "
			    "echoOff and echoOn or none of them");

		if (newShell.echoOn != NULL && newShell.echoOff)
			newShell.hasEchoCtl = TRUE;
	}

	if (path == NULL) {
		/*
		 * If no path was given, the user wants one of the pre-defined
		 * shells, yes? So we find the one s/he wants with the help of
		 * JobMatchShell and set things up the right way. shellPath
		 * will be set up by Job_Init.
		 */
		if (newShell.name == NULL) {
			Parse_Error(PARSE_FATAL,
			    "Neither path nor name specified");
			ArgArray_Done(&aa);
			return (FAILURE);
		}
		if ((sh = JobMatchShell(newShell.name)) == NULL) {
			Parse_Error(PARSE_FATAL, "%s: no matching shell",
			    newShell.name);
			ArgArray_Done(&aa);
			return (FAILURE);
		}

	} else {
		/*
		 * The user provided a path. If s/he gave nothing else
		 * (fullSpec is FALSE), try and find a matching shell in the
		 * ones we know of. Else we just take the specification at its
		 * word and copy it to a new location. In either case, we need
		 * to record the path the user gave for the shell.
		 */
		path = estrdup(path);
		if (newShell.name == NULL) {
			/* get the base name as the name */
			if ((newShell.name = strrchr(path, '/')) == NULL) {
				newShell.name = path;
			} else {
				newShell.name += 1;
			}
		}

		if (!fullSpec) {
			if ((sh = JobMatchShell(newShell.name)) == NULL) {
				Parse_Error(PARSE_FATAL,
				    "%s: no matching shell", newShell.name);
				free(path);
				ArgArray_Done(&aa);
				return (FAILURE);
			}
		} else {
			sh = JobCopyShell(&newShell);
		}
		free(shellPath);
		shellPath = path;
	}

	/* set the new shell */
	JobFreeShell(commandShell);
	commandShell = sh;

	shellName = commandShell->name;

	ArgArray_Done(&aa);
	return (SUCCESS);
}

void
Shell_Init(void)
{
	commandShell = JobMatchShell(DEFSHELLNAME);

	/*
	 * Both the absolute path and the last component
	 * must be set. The last component is taken from the 'name'
	 * field of the default shell description pointed-to by
	 * commandShell. All default shells are located in
	 * PATH_DEFSHELLDIR.
	 */
 	shellName = commandShell->name;
	shellPath = str_concat(
			PATH_DEFSHELLDIR,
			commandShell->name,
			STR_ADDSLASH);
}


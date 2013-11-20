/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/boot/common/interp.c,v 1.29 2003/08/25 23:30:41 obrien Exp $
 */

/*
 * Simple commandline interpreter, toplevel and misc.
 */

#include <stand.h>
#include <string.h>
#include "bootstrap.h"
#include "dloader.h"

static void	prompt(void);
static int	iseol(char c);
static void	skipeol(int fd);

/*
 * Perform the command
 */
int
perform(int argc, char *argv[])
{
    int				result;
    struct bootblk_command	**cmdp;
    bootblk_cmd_t		*cmd;
    const char *av0;

    if (argc < 1)
	return(CMD_OK);

    av0 = argv[0];
    if (strchr(av0, '=') != NULL)
	av0 = "local";

    /* set return defaults; a successful command will override these */
    command_errmsg = command_errbuf;
    strcpy(command_errbuf, "no error message");
    cmd = NULL;
    result = CMD_ERROR;

    /* search the command set for the command */
    SET_FOREACH(cmdp, Xcommand_set) {
	if (((*cmdp)->c_name != NULL) && !strcmp(av0, (*cmdp)->c_name)) {
	    cmd = (*cmdp)->c_fn;
	    break;
	}
    }
    if (cmd != NULL) {
	if ((*cmdp)->c_cond || CurrentCondition != 0)
		result = (cmd)(argc, argv);
	else
		result = CMD_OK;
    } else {
	command_errmsg = "unknown command";
    }
    return(result);
}

/*
 * Interactive mode
 */
void
interact(void)
{
    char	input[256];			/* big enough? */
    int		argc;
    char	**argv;

    /*
     * We may be booting from the boot partition, or we may be booting
     * from the root partition with a /boot sub-directory.  If the latter
     * chdir into /boot.  Ignore any error.  Only rel_open() uses the chdir
     * info.
     */
    chdir("/boot");
    setenv("base", DirBase, 1);

    /*
     * Read our default configuration
     */
    if (include("dloader.rc") != CMD_OK)
	include("boot.conf");
    printf("\n");
    /*
     * Before interacting, we might want to autoboot.
     */
    autoboot_maybe();

    dloader_init_cmds();

    /*
     * Not autobooting, go manual
     */
    printf("\nType '?' for a list of commands, 'help' for more detailed help.\n");
    if (getenv("prompt") == NULL)
	setenv("prompt", "OK", 1);

    for (;;) {
	input[0] = '\0';
	prompt();
	ngets(input, sizeof(input));
	if (!parse(&argc, &argv, input)) {
	    if (perform(argc, argv))
		printf("%s: %s\n", argv[0], command_errmsg);
	    free(argv);
	} else {
	    printf("parse error\n");
	}
    }
}

/*
 * Read commands from a file, then execute them.
 *
 * We store the commands in memory and close the source file so that the media
 * holding it can safely go away while we are executing.
 *
 * Commands may be prefixed with '@' (so they aren't displayed) or '-' (so
 * that the script won't stop if they fail).
 */
COMMAND_SET(include, "include", "run commands from file", command_include);

static int
command_include(int argc, char *argv[])
{
    int		i;
    int		res;
    char	**argvbuf;

    /*
     * Since argv is static, we need to save it here.
     */
    argvbuf = (char**) calloc((u_int)argc, sizeof(char*));
    for (i = 0; i < argc; i++)
	argvbuf[i] = strdup(argv[i]);

    res=CMD_OK;
    for (i = 1; (i < argc) && (res == CMD_OK); i++)
	res = include(argvbuf[i]);

    for (i = 0; i < argc; i++)
	free(argvbuf[i]);
    free(argvbuf);

    return(res);
}

COMMAND_SET(optinclude, "optinclude",
	    "run commands from file; ignore exit status",
	    command_optinclude);

static int
command_optinclude(int argc, char *argv[])
{
    int		i;
    char	**argvbuf;

    /*
     * Since argv is static, we need to save it here.
     */
    argvbuf = (char**) calloc((u_int)argc, sizeof(char*));
    for (i = 0; i < argc; i++)
	argvbuf[i] = strdup(argv[i]);

    for (i = 1; (i < argc); i++)
	include(argvbuf[i]);

    for (i = 0; i < argc; i++)
	free(argvbuf[i]);
    free(argvbuf);

    return(CMD_OK);
}

struct includeline
{
    char		*text;
    int			flags;
    int			line;
#define SL_QUIET	(1<<0)
#define SL_IGNOREERR	(1<<1)
    struct includeline	*next;
};

int
include(const char *filename)
{
    struct includeline	*script, *se, *sp;
    char		input[256];			/* big enough? */
    int			argc,res;
    char		**argv, *cp;
    int			fd, flags, line;

    if (((fd = rel_open(filename, NULL, O_RDONLY)) == -1)) {
	command_errmsg = command_errbuf;
	snprintf(command_errbuf, 256, "cannot find \"%s\"", filename);
	return(CMD_ERROR);
    }

    /*
     * Read the script into memory.
     */
    script = se = NULL;
    line = 0;

    while (fgets(input, sizeof(input), fd) != NULL) {
	line++;
	flags = 0;
	if(strlen(input) == sizeof(input) - 1 &&
	    !iseol(input[sizeof(input) - 2])) {
	    printf("WARNING: %s: %s: Line too long: truncating; have:\n",
		__func__, filename);
	    printf("%s\n", input);
	    skipeol(fd);
	}
	/* Discard comments */
	if (strncmp(input+strspn(input, " "), "\\ ", 2) == 0)
	    continue;
	cp = input;
	/* Echo? */
	if (input[0] == '@') {
	    cp++;
	    flags |= SL_QUIET;
	}
	/* Error OK? */
	if (input[0] == '-') {
	    cp++;
	    flags |= SL_IGNOREERR;
	}
	/* Allocate script line structure and copy line, flags */
	sp = malloc(sizeof(struct includeline) + strlen(cp) + 1);
	sp->text = (char *)sp + sizeof(struct includeline);
	strcpy(sp->text, cp);
	sp->flags = flags;
	sp->line = line;
	sp->next = NULL;

	if (script == NULL) {
	    script = sp;
	} else {
	    se->next = sp;
	}
	se = sp;
    }
    close(fd);

    /*
     * Execute the script
     */
    argv = NULL;
    res = CMD_OK;
    for (sp = script; sp != NULL; sp = sp->next) {

#if 0
	/* print if not being quiet */
	if (!(sp->flags & SL_QUIET)) {
	    prompt();
	    printf("%s\n", sp->text);
	}
#endif

	/* Parse the command */
	if (!parse(&argc, &argv, sp->text)) {
	    if ((argc > 0) && (perform(argc, argv) != 0)) {
		/* normal command */
		printf("%s: %s\n", argv[0], command_errmsg);
		if (!(sp->flags & SL_IGNOREERR)) {
		    res=CMD_ERROR;
		    break;
		}
	    }
	    free(argv);
	    argv = NULL;
	} else {
	    printf("%s line %d: parse error\n", filename, sp->line);
	    res=CMD_ERROR;
	    break;
	}
    }
    if (argv != NULL)
	free(argv);
    while(script != NULL) {
	se = script;
	script = script->next;
	free(se);
    }
    return(res);
}

/*
 * Emit the current prompt; use the same syntax as the parser
 * for embedding environment variables.
 */
static void
prompt(void)
{
    char	*pr, *p, *cp, *ev;

    if ((cp = getenv("prompt")) == NULL)
	cp = ">";
    pr = p = strdup(cp);

    while (*p != 0) {
	if ((*p == '$') && (*(p+1) == '{')) {
	    for (cp = p + 2; (*cp != 0) && (*cp != '}'); cp++)
		;
	    *cp = 0;
	    ev = getenv(p + 2);

	    if (ev != NULL)
		printf("%s", ev);
	    p = cp + 1;
	    continue;
	}
	putchar(*p++);
    }
    putchar(' ');
    free(pr);
}

static int
iseol(char c)
{
    return(c == '\n' || c == '\r');
}

static void
skipeol(int fd)
{
    char c;

    while (read(fd, &c, 1) == 1) {
	if (iseol(c))
	    break;
    }
}

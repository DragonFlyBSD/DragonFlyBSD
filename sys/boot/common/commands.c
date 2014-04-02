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
 * $FreeBSD: src/sys/boot/common/commands.c,v 1.19 2003/08/25 23:30:41 obrien Exp $
 */

#include <stand.h>
#include <string.h>

#include "bootstrap.h"

char		*command_errmsg;
char		command_errbuf[256];
int		CurrentCondition = 1;

static int page_file(char *filename);

/*
 * Help is read from a formatted text file.
 *
 * Entries in the file are formatted as 

# Ttopic [Ssubtopic] Ddescription
help
text
here
#

 *
 * Note that for code simplicity's sake, the above format must be followed
 * exactly.
 *
 * Subtopic entries must immediately follow the topic (this is used to
 * produce the listing of subtopics).
 *
 * If no argument(s) are supplied by the user, the help for 'help' is displayed.
 */
COMMAND_SET(help, "help", "detailed help", command_help);

static int
help_getnext(int fd, char **topic, char **subtopic, char **desc) 
{
    char	line[81], *cp, *ep;
    
    for (;;) {
	if (fgetstr(line, 80, fd) < 0)
	    return(0);
	
	if ((strlen(line) < 3) || (line[0] != '#') || (line[1] != ' '))
	    continue;

	*topic = *subtopic = *desc = NULL;
	cp = line + 2;
	while ((cp != NULL) && (*cp != 0)) {
	    ep = strchr(cp, ' ');
	    if ((*cp == 'T') && (*topic == NULL)) {
		if (ep != NULL)
		    *ep++ = 0;
		*topic = strdup(cp + 1);
	    } else if ((*cp == 'S') && (*subtopic == NULL)) {
		if (ep != NULL)
		    *ep++ = 0;
		*subtopic = strdup(cp + 1);
	    } else if (*cp == 'D') {
		*desc = strdup(cp + 1);
		ep = NULL;
	    }
	    cp = ep;
	}
	if (*topic == NULL) {
	    if (*subtopic != NULL)
		free(*subtopic);
	    if (*desc != NULL)
		free(*desc);
	    continue;
	}
	return(1);
    }
}

static void
help_emitsummary(char *topic, char *subtopic, char *desc)
{
    int		i;
    
    pager_output("    ");
    pager_output(topic);
    i = strlen(topic);
    if (subtopic != NULL) {
	pager_output(" ");
	pager_output(subtopic);
	i += strlen(subtopic) + 1;
    }
    if (desc != NULL) {
	do {
	    pager_output(" ");
	} while (i++ < 30);
	pager_output(desc);
    }
    pager_output("\n");
}

	    
static int
command_help(int argc, char *argv[]) 
{
    char	buf[81];	/* XXX buffer size? */
    int		hfd, matched, doindex;
    char	*topic, *subtopic, *t, *s, *d;

    /* page the help text from our load path */
    /* sprintf(buf, "%s/boot/loader.help", getenv("loaddev")); */
    /* page the help text from our base path */
    snprintf(buf, sizeof(buf), "%sloader.help", getenv("base"));
    if ((hfd = open(buf, O_RDONLY)) < 0) {
	if ((hfd = rel_open("loader.help", NULL, O_RDONLY)) < 0) {
	    printf("Verbose help not available, use '?' to list commands\n");
	    return(CMD_OK);
	}
    }

    /* pick up request from arguments */
    topic = subtopic = NULL;
    switch(argc) {
    case 3:
	subtopic = strdup(argv[2]);
    case 2:
	topic = strdup(argv[1]);
	break;
    case 1:
	topic = strdup("help");
	break;
    default:
	command_errmsg = "usage is 'help <topic> [<subtopic>]";
	return(CMD_ERROR);
    }

    /* magic "index" keyword */
    doindex = !strcmp(topic, "index");
    matched = doindex;
    
    /* Scan the helpfile looking for help matching the request */
    pager_open();
    while (help_getnext(hfd, &t, &s, &d)) {

	if (doindex) {		/* dink around formatting */
	    help_emitsummary(t, s, d);

	} else if (strcmp(topic, t)) {
	    /* topic mismatch */
	    if (matched)	/* nothing more on this topic, stop scanning */
		break;

	} else {
	    /* topic matched */
	    matched = 1;
	    if (((subtopic == NULL) && (s == NULL)) ||
		((subtopic != NULL) && (s != NULL) && !strcmp(subtopic, s))) {
		/* exact match, print text */
		while ((fgetstr(buf, 80, hfd) >= 0) && (buf[0] != '#')) {
		    if (pager_output(buf))
			break;
		    if (pager_output("\n"))
			break;
		}
	    } else if ((subtopic == NULL) && (s != NULL)) {
		/* topic match, list subtopics */
		help_emitsummary(t, s, d);
	    }
	}
	free(t);
	free(s);
	free(d);
    }
    pager_close();
    close(hfd);
    if (!matched) {
	sprintf(command_errbuf, "no help available for '%s'", topic);
	free(topic);
	if (subtopic)
	    free(subtopic);
	return(CMD_ERROR);
    }
    free(topic);
    if (subtopic)
	free(subtopic);
    return(CMD_OK);
}


COMMAND_SET(commandlist, "?", "list commands", command_commandlist);

static int
command_commandlist(int argc, char *argv[])
{
    struct bootblk_command	**cmdp;
    char str[81];
    
    pager_open();
    printf("Available commands:\n");
    SET_FOREACH(cmdp, Xcommand_set) {
	if (((*cmdp)->c_name != NULL) && ((*cmdp)->c_desc != NULL)) {
	    snprintf(str, sizeof(str), "  %-15s  %s\n",
		(*cmdp)->c_name, (*cmdp)->c_desc);
	    pager_output(str);
	}
    }
    pager_close();
    return(CMD_OK);
}

/*
 * XXX set/show should become set/echo if we have variable
 * substitution happening.
 */

COMMAND_SET(show, "show", "show kenv variable(s)", command_show);

static int
command_show(int argc, char *argv[])
{
    struct env_var	*ev;
    char		*cp;

    if (argc < 2) {
	/* 
	 * With no arguments, print everything.
	 */
	pager_open();
	for (ev = environ; ev != NULL; ev = ev->ev_next) {
	    pager_output(ev->ev_name);
	    cp = getenv(ev->ev_name);
	    if (cp != NULL) {
		pager_output("=");
		pager_output(cp);
	    }
	    if (pager_output("\n"))
		break;
	}
	pager_close();
    } else {
	if ((cp = getenv(argv[1])) != NULL) {
	    printf("%s\n", cp);
	} else {
	    sprintf(command_errbuf, "variable '%s' not found", argv[1]);
	    return(CMD_ERROR);
	}
    }
    return(CMD_OK);
}

COMMAND_SET(set, "set", "set a kenv variable", command_set);

static int
command_set(int argc, char *argv[])
{
    int		err;
    
    if (argc != 2) {
	command_errmsg = "wrong number of arguments";
	return(CMD_ERROR);
    } else {
	if ((err = putenv(argv[1])) != 0) {
	    command_errmsg = strerror(err);
	    return(CMD_ERROR);
	}
    }
    return(CMD_OK);
}

COMMAND_SET(unset, "unset", "unset a kenv variable", command_unset);

static int
command_unset(int argc, char *argv[]) 
{
    if (argc != 2) {
	command_errmsg = "wrong number of arguments";
	return(CMD_ERROR);
    } else {
	/* ignore any errors */
	unsetenv(argv[1]);
    }
    return(CMD_OK);
}

COMMAND_SET(echo, "echo", "print text ($VAR is kenv variable)", command_echo);

static int
command_echo(int argc, char *argv[])
{
    char	*s;
    int		nl, ch;
    
    nl = 0;
    optind = 1;
    optreset = 1;
    while ((ch = getopt(argc, argv, "n")) != -1) {
	switch(ch) {
	case 'n':
	    nl = 1;
	    break;
	case '?':
	default:
	    /* getopt has already reported an error */
	    return(CMD_OK);
	}
    }
    argv += (optind);
    argc -= (optind);

    s = unargv(argc, argv);
    if (s != NULL) {
	printf("%s", s);
	free(s);
    }
    if (!nl)
	printf("\n");
    return(CMD_OK);
}

/*
 * A passable emulation of the sh(1) command of the same name.
 */

COMMAND_SET(read, "read", "read to kenv variable", command_read);

static int
command_read(int argc, char *argv[])
{
    char	*prompt;
    int		timeout;
    time_t	when;
    char	*cp;
    char	*name;
    char	buf[256];		/* XXX size? */
    int		c;
    
    timeout = -1;
    prompt = NULL;
    optind = 1;
    optreset = 1;
    while ((c = getopt(argc, argv, "p:t:")) != -1) {
	switch(c) {
	    
	case 'p':
	    prompt = optarg;
	    break;
	case 't':
	    timeout = strtol(optarg, &cp, 0);
	    if (cp == optarg) {
		sprintf(command_errbuf, "bad timeout '%s'", optarg);
		return(CMD_ERROR);
	    }
	    break;
	default:
	    return(CMD_OK);
	}
    }

    argv += (optind);
    argc -= (optind);
    if (argc > 1) {
	command_errmsg = "wrong number of arguments";
	return(CMD_ERROR);
    }
    name = (argc > 0) ? argv[0]: NULL;
	
    if (prompt != NULL)
	printf("%s", prompt);
    if (timeout >= 0) {
	when = time(NULL) + timeout;
	while (!ischar())
	    if (time(NULL) >= when)
		return(CMD_OK);		/* is timeout an error? */
    }

    ngets(buf, sizeof(buf));

    if (name != NULL)
	setenv(name, buf, 1);
    return(CMD_OK);
}

/*
 * File pager
 */
COMMAND_SET(more, "more", "show contents of a file", command_more);

static int
command_more(int argc, char *argv[])
{
    int         i;
    int         res;
    char	line[80];

    res=0;
    pager_open();
    for (i = 1; (i < argc) && (res == 0); i++) {
	sprintf(line, "*** FILE %s BEGIN ***\n", argv[i]);
	if (pager_output(line))
		break;
        res = page_file(argv[i]);
	if (!res) {
	    sprintf(line, "*** FILE %s END ***\n", argv[i]);
	    res = pager_output(line);
	}
    }
    pager_close();

    if (res == 0)
	return CMD_OK;
    else
	return CMD_ERROR;
}

static int
page_file(char *filename)
{
    int result;
    int fd;
    char *fullpath;

    if ((fd = rel_open(filename, &fullpath, O_RDONLY)) != -1) {
	close(fd);
	result = pager_file(fullpath);
	free(fullpath);
    } else {
	result = -1;
    }
    if (result == -1)
	sprintf(command_errbuf, "error showing %s", filename);

    return result;
}   

/*
 * List all disk-like devices
 */
COMMAND_SET(lsdev, "lsdev", "list all devices", command_lsdev);

static int
command_lsdev(int argc, char *argv[])
{
    int		verbose, ch, i;
    char	line[80];
    
    verbose = 0;
    optind = 1;
    optreset = 1;
    while ((ch = getopt(argc, argv, "v")) != -1) {
	switch(ch) {
	case 'v':
	    verbose = 1;
	    break;
	case '?':
	default:
	    /* getopt has already reported an error */
	    return(CMD_OK);
	}
    }
    argv += (optind);
    argc -= (optind);

    pager_open();
    for (i = 0; devsw[i] != NULL; i++) {
	if (devsw[i]->dv_print != NULL){
	    sprintf(line, "%s devices:\n", devsw[i]->dv_name);
	    if (pager_output(line))
		    break;
	    devsw[i]->dv_print(verbose);
	} else {
	    sprintf(line, "%s: (unknown)\n", devsw[i]->dv_name);
	    if (pager_output(line))
		    break;
	}
    }
    pager_close();
    return(CMD_OK);
}

/*
 * CONDITIONALS
 */
COMMAND_SET_COND(ifexists, "ifexists", "conditional file/dir present",
		 command_ifexists);

struct cond {
    char    inherit;
    char    current;
};

static struct cond CondStack[32];
static int CondIndex;

static int
command_ifexists(int argc, char *argv[])
{
	if (CondIndex + 1 == sizeof(CondStack)/sizeof(CondStack[0])) {
		sprintf(command_errbuf, "if stack too deep");
		return(-1);
	} else if (argc != 2) {
		sprintf(command_errbuf, "ifexists requires one argument");
		return(-1);
	} else {
		struct stat sb;
		struct cond *cond = &CondStack[CondIndex++];

		cond->inherit = CurrentCondition;

		if (rel_stat(argv[1], &sb)) {
			cond->current = 0;
		} else {
			cond->current = 1;
		}
		CurrentCondition = (cond->inherit && cond->current);
		return(CMD_OK);
	}
}

COMMAND_SET_COND(ifset, "ifset", "conditional kenv variable present", command_ifset);

static int
command_ifset(int argc, char *argv[])
{
	if (CondIndex + 1 == sizeof(CondStack)/sizeof(CondStack[0])) {
		sprintf(command_errbuf, "if stack too deep");
		return(-1);
	} else if (argc != 2) {
		sprintf(command_errbuf, "ifset requires one argument");
		return(-1);
	} else {
		struct cond *cond = &CondStack[CondIndex++];

		cond->inherit = CurrentCondition;

		if (getenv(argv[1])) {
			cond->current = 1;
		} else {
			cond->current = 0;
		}
		CurrentCondition = (cond->inherit && cond->current);
		return(CMD_OK);
	}
}

COMMAND_SET_COND(elseifexists, "elseifexists", "conditional file/dir present",
		 command_elseifexists);

static int
command_elseifexists(int argc, char *argv[])
{
	if (CondIndex == 0) {
		sprintf(command_errbuf, "elseifexists without if");
		return(-1);
	} else if (argc != 2) {
		sprintf(command_errbuf, "elseifexists requires one argument");
		return(-1);
	} else {
		struct stat sb;
		struct cond *cond = &CondStack[CondIndex - 1];

		if (cond->inherit == 0) {
			CurrentCondition = 0;	/* already ran / can't run */
		} else if (cond->current) {
			cond->inherit = 0;	/* can't run any more */
			cond->current = 0;
			CurrentCondition = 0;
		} else {
			if (rel_stat(argv[1], &sb)) {
				cond->current = 0;
			} else {
				cond->current = 1;
			}
			CurrentCondition = (cond->inherit && cond->current);
		}
		return(CMD_OK);
	}
}

COMMAND_SET_COND(else, "else", "conditional if/else/endif", command_else);

static int
command_else(int argc, char *argv[])
{
	struct cond *cond;

	if (CondIndex) {
		cond = &CondStack[CondIndex - 1];
		cond->current = !cond->current;
		CurrentCondition = (cond->inherit && cond->current);
		return(CMD_OK);
	} else {
		sprintf(command_errbuf, "else without if");
		return(-1);
	}
}

COMMAND_SET_COND(endif, "endif", "conditional if/else/endif", command_endif);

static int
command_endif(int argc, char *argv[])
{
	struct cond *cond;

	if (CondIndex) {
		--CondIndex;
		if (CondIndex) {
			cond = &CondStack[CondIndex - 1];
			CurrentCondition = (cond->inherit && cond->current);
		} else {
			CurrentCondition = 1;
		}
		return(CMD_OK);
	} else {
		sprintf(command_errbuf, "endif without if");
		return(-1);
	}
}

COMMAND_SET(rmem, "rmem", "read memory", command_rmem);

static int
command_rmem(int argc, char *argv[])
{
    int addr;

    if (argc > 1) {
	addr = strtol(argv[1], NULL, 0);
	printf("%08x: %08x\n", addr, *(int *)(intptr_t)addr);
    }
    return(CMD_OK);
}

COMMAND_SET(wmem, "wmem", "write memory", command_wmem);

static int
command_wmem(int argc, char *argv[])
{
    int addr;
    int data;

    if (argc > 2) {
	addr = strtol(argv[1], NULL, 0);
	data = strtol(argv[2], NULL, 0);
	*(int *)(intptr_t)addr = data;
	printf("%08x: %08x\n", addr, *(int *)(intptr_t)addr);
    }
    return(CMD_OK);
}

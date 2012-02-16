/*
 * rconfig - Remote configurator
 *
 * 	rconfig [-W workingdir] [server_ip[:tag]]
 *	rconfig [-f configfile] -s
 * 
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include "defs.h"

const char *WorkDir = "/tmp";
const char *ConfigFiles = "/etc/defaults/rconfig.conf:/etc/rconfig.conf";
const char *TagDir = "/usr/local/etc/rconfig";
tag_t AddrBase;
tag_t VarBase;
int VerboseOpt;

static void usage(int code);
static void addTag(tag_t *basep, const char *tag, int flags);

int
main(int ac, char **av)
{
    int ch;
    int i;
    int serverMode = 0;
    
    while ((ch = getopt(ac, av, "aW:T:C:sv")) != -1) {
	switch(ch) {
	case 'a':	/* auto tag / standard broadcast */
	    addTag(&AddrBase, NULL, 0);
	    break;
	case 'W':	/* specify working directory */
	    WorkDir = optarg;
	    break;
	case 'T':
	    TagDir = optarg;
	    break;
	case 'C':	/* specify server config file(s) (colon delimited) */
	    ConfigFiles = optarg;
	    break;
	case 's':	/* run as server using config file */
	    serverMode = 1;
	    break;
	case 'v':
	    VerboseOpt = 1;
	    break;
	default:
	    usage(1);
	    /* not reached */
	}
    }
    for (i = optind; i < ac; ++i) {
	if (strchr(av[i], '='))
	    addTag(&VarBase, av[i], 0);
	else
	    addTag(&AddrBase, av[i], 0);
    }
    if (AddrBase == NULL)
	usage(1);
    if (AddrBase && AddrBase->name == NULL && AddrBase->next) {
	fprintf(stderr,
		"You cannot specify both -a AND a list of hosts.  If you want\n"
		"to use auto-broadcast mode with a tag other than 'auto',\n"
		"just specify the tag without a host, e.g. ':<tag>'\n");
	exit(1);
    }
    if (serverMode)
	doServer();
    else
	doClient();
    return(0);
}

static
void
addTag(tag_t *basep, const char *name, int flags)
{
    tag_t tag = calloc(sizeof(struct tag), 1);

    while ((*basep) != NULL)
	basep = &(*basep)->next;

    tag->name = name;
    tag->flags = flags;
    *basep = tag;
}

static void
usage(int code)
{
    fprintf(stderr, "rconfig [-W workdir] [-f servconfig] "
		    "[-s] [var=data]* [server_ip[:tag]]* \n");
    exit(code);
}


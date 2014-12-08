/*
 * Copyright (c)2004 The DragonFly Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 *
 *   Neither the name of the DragonFly Project nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * main.c
 * Main program for installer backend.
 * $Id: main.c,v 1.18 2005/03/11 19:57:27 cpressey Exp $
 */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef ENABLE_NLS
#include <libintl.h>
#define _(String) gettext (String)
#else
#define _(String) (String)
#endif

#include "libaura/mem.h"

#include "libdfui/dfui.h"
#include "libdfui/dump.h"
#include "libdfui/system.h"

#include "flow.h"
#include "pathnames.h"

static void usage(char **);

int
main(int argc, char **argv)
{
	char os_root[256];
	char *rendezvous = NULL;
	int do_reboot = 0;
	int opt;
	int transport = 0;
	int flags = 0;

#ifdef ENABLE_NLS
	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);
#endif

	/*
	 * XXX TODO: set transport and rendezvous from
	 * corresponding environment variables, if set.
	 */

	strlcpy(os_root, DEFAULT_OS_ROOT, sizeof(os_root));

#ifdef DEBUG
	dfui_debug_file = fopen("/tmp/dfuibe_installer_debug.log", "w");
#endif

	/*
	 * Get command-line arguments.
	 */
	while ((opt = getopt(argc, argv, "o:r:t:")) != -1) {
		switch (opt) {
		case 'o':
			strlcpy(os_root, optarg, sizeof(os_root));
			break;
		case 'r':
			rendezvous = aura_strdup(optarg);
			break;
		case 't':
			transport = user_get_transport(optarg);
			break;
		case '?':
		default:
			usage(argv);
		}
	}
	argc -= optind;
	argv += optind;

	if (transport == 0)
		transport = user_get_transport("tcp");

	if (rendezvous == NULL) {
		if (transport == DFUI_TRANSPORT_TCP)
			rendezvous = aura_strdup("9999");
		else
			rendezvous = aura_strdup("test");
	}

	do_reboot = flow(transport, rendezvous, os_root,
	    flags);
	free(rendezvous);

	if (do_reboot)
		exit(5);
	else
		exit(0);
}

static void
usage(char **argv)
{
	fprintf(stderr, _("Usage: %s [-o rootdir] [-r rendezvous] "
			  "[-t npipe|tcp]\n"), argv[0]);
	exit(1);
}

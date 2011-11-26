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
 * package.c
 * Manage installation etc of packages.
 * $Id: package.c,v 1.24 2005/02/06 21:05:18 cpressey Exp $
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libaura/dict.h"
#include "libaura/fspred.h"
#include "libaura/popen.h"

#include "libdfui/dfui.h"

#include "commands.h"
#include "diskutil.h"
#include "functions.h"
#include "uiutil.h"
#include "package.h"

/*
 * Determine whether a package is installed on a DragonFly HDD.
 */
int
pkg_exists(struct i_fn_args *a, const char *pkg)
{
	return(is_dir("%smnt/var/db/pkg/%s", a->os_root, pkg));
}

int
pkg_clean(struct i_fn_args *a, struct commands *cmds)
{
	command_add(cmds, "%s%s %smnt/ /%s '*'",
	    a->os_root, cmd_name(a, "CHROOT"),
	    a->os_root, cmd_name(a, "PKG_DELETE"));
	return(1);
}

/*
 * Copy a package from an installation CD onto a DragonFly HDD.
 */
int
pkg_copy(struct i_fn_args *a, struct commands *cmds, const char *pkg_name,
	 struct aura_dict *seen)
{
	FILE *pipe;
	char *rpkg_name;
	char line[256];
	char pkg_suffix[256];

	/*
	 * Get all the packages that this package depends on, and
	 * recursively pkg_copy them first, if they're not already there.
	 *
	 * XXX We should be sending this command through a command chain
	 * right now so that we can get an accurate idea of what is being
	 * run and so that it will be logged.  But unfortunately that's
	 * not feasible, since this function is building another command
	 * chain for later use.  So we use a pipe.
	 */
	if ((pipe = aura_popen("%s%s -r %s", "r",
	    a->os_root, cmd_name(a, "PKG_INFO"), pkg_name)) == NULL)
		return(0);

	while (fgets(line, 255, pipe) != NULL) {
		/*
		 * Only look at lines that begin with 'Dependency:'.
		 */
		if (strncmp(line, "Dependency:", 11) != 0)
			continue;
		rpkg_name = &line[12];

		/*
		 * Strip any trailing whitespace.
		 */
		while (strlen(rpkg_name) > 0 &&
		       isspace(rpkg_name[strlen(rpkg_name) - 1])) {
			rpkg_name[strlen(rpkg_name) - 1] = '\0';
		}

		if (!pkg_exists(a, rpkg_name)) {
			if (!pkg_copy(a, cmds, rpkg_name, seen)) {
				aura_pclose(pipe);
				return(0);
			}
		}
	}
	aura_pclose(pipe);
	snprintf(pkg_suffix, 256, "tgz");
	if (!pkg_exists(a, pkg_name) &&
	    !aura_dict_exists(seen, pkg_name, strlen(pkg_name))) {
		aura_dict_store(seen,
		    pkg_name, strlen(pkg_name), "", 0);
		command_add(cmds, "%s%s -b %s %smnt/tmp/%s.%s",
		    a->os_root, cmd_name(a, "PKG_CREATE"),
		    pkg_name, a->os_root, pkg_name, pkg_suffix);
		command_add(cmds, "%s%s %smnt/ /%s /tmp/%s.%s",
		    a->os_root, cmd_name(a, "CHROOT"),
		    a->os_root, cmd_name(a, "PKG_ADD"),
		    pkg_name, pkg_suffix);
		command_add(cmds, "%s%s %smnt/tmp/%s.%s",
		    a->os_root, cmd_name(a, "RM"),
		    a->os_root, pkg_name, pkg_suffix);
	}

	return(1);
}

/*
 * Remove a package from a DragonFly HDD.
 */
int
pkg_remove(struct i_fn_args *a, struct commands *cmds, const char *pkg_name,
	   struct aura_dict *seen)
{
	FILE *pipe;
	char *command, *rpkg_name;
	char line[256];
	int seen_required_by = 0;

	/*
	 * Get all the packages that this package depends on, and
	 * recursively pkg_copy them first, if they're not already there.
	 */
	asprintf(&command,
	    "%s%s %smnt/ /%s -R %s",
	    a->os_root, cmd_name(a, "CHROOT"),
	    a->os_root, cmd_name(a, "PKG_INFO"),
	    pkg_name);
	pipe = popen(command, "r");
	free(command);
	if (pipe == NULL)
		return(0);

	while (fgets(line, 255, pipe) != NULL) {
		/*
		 * Only look at lines that follow the "Required by:" line.
		 */
		if (seen_required_by) {
			rpkg_name = line;
			/*
			 * Strip any trailing whitespace.
			 */
			while (strlen(rpkg_name) > 0 &&
			       isspace(rpkg_name[strlen(rpkg_name) - 1])) {
				rpkg_name[strlen(rpkg_name) - 1] = '\0';
			}

			if (strlen(rpkg_name) > 0 && pkg_exists(a, rpkg_name)) {
				if (!pkg_remove(a, cmds, rpkg_name, seen)) {
					pclose(pipe);
					return(0);
				}
			}
		} else {
			if (strncmp(line, "Required by:", 12) != 0) {
				seen_required_by = 1;
			}
		}
	}
	pclose(pipe);

	if (pkg_exists(a, pkg_name) &&
	    !aura_dict_exists(seen, pkg_name, strlen(pkg_name))) {
		aura_dict_store(seen,
		    pkg_name, strlen(pkg_name), "", 0);
		command_add(cmds, "%s%s %smnt/ /%s %s",
		    a->os_root, cmd_name(a, "CHROOT"),
		    a->os_root, cmd_name(a, "PKG_DELETE"),
		    pkg_name);
	}

	return(1);
}

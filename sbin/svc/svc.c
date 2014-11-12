/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
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
 */
/*
 * SERVICE MANAGER
 *
 * This program builds an environment to run a service in and provides
 * numerous options for naming, tracking, and management.  It uses
 * reapctl(2) to corral the processes under management.
 */

#include "svc.h"

static int execute_remote(command_t *cmd, int (*func)(command_t *cmd));
static int process_jailspec(command_t *cmd, const char *spec);

int
main(int ac, char **av)
{
	command_t cmd;
	int rc;

	signal(SIGPIPE, SIG_IGN);

	rc = process_cmd(&cmd, stdout, ac, av);
	cmd.cmdline = 1;	/* commanded from front-end */
	cmd.commanded = 1;	/* commanded action (vs automatic) */
	if (rc == 0)
		rc = execute_cmd(&cmd);
	free_cmd(&cmd);

	return rc;
}

int
process_cmd(command_t *cmd, FILE *fp, int ac, char **av)
{
	const char *optstr = "dfhp:r:R:xst:u:g:G:l:c:C:j:J:k:T:F:";
	struct group *grent;
	struct passwd *pwent;
	char *sub;
	char *cpy;
	int rc = 1;
	int ch;
	int i;

	bzero(cmd, sizeof(*cmd));
	cmd->fp = fp;				/* error and output reporting */
	cmd->logfd = -1;
	sreplace(&cmd->piddir, "/var/run");	/* must not be NULL */
	cmd->termkill_timo = -1;		/* will use default value */
	cmd->orig_ac = ac;
	cmd->orig_av = av;
	cmd->empty_label = 1;

	optind = 1;
	opterr = 1;
	optreset = 1;

	while ((ch = getopt(ac, av, optstr)) != -1) {
		switch(ch) {
		case 'd':
			cmd->debug = 1;
			cmd->foreground = 1;
			break;
		case 'f':
			cmd->foreground = 1;
			break;
		case 'h':
			execute_help(cmd);
			exit(0);
			break;
		case 'p':
			sreplace(&cmd->piddir, optarg);
			break;
		case 'r':
			cmd->restart_some = 1;
			cmd->restart_all = 0;
			cmd->restart_timo = strtol(optarg, NULL, 0);
			break;
		case 'R':
			cmd->restart_some = 0;
			cmd->restart_all = 1;
			cmd->restart_timo = strtol(optarg, NULL, 0);
			break;
		case 'x':
			cmd->exit_mode = 1;
			break;
		case 's':
			cmd->sync_mode = 1;
			break;
		case 't':
			cmd->termkill_timo = strtoul(optarg, NULL, 0);
			break;
		case 'u':
			if (isdigit(optarg[0])) {
				pwent = getpwnam(optarg);
			} else {
				pwent = getpwuid(strtol(optarg, NULL, 0));
			}
			if (pwent == NULL) {
				fprintf(fp, "Cannot find user %s: %s\n",
					optarg,
					strerror(errno));
				goto failed;
			}
			sfree(&cmd->pwent.pw_name);
			sfree(&cmd->pwent.pw_passwd);
			sfree(&cmd->pwent.pw_class);
			sfree(&cmd->pwent.pw_gecos);
			sfree(&cmd->pwent.pw_dir);
			sfree(&cmd->pwent.pw_shell);
			cmd->pwent = *pwent;
			sdup(&cmd->pwent.pw_name);
			sdup(&cmd->pwent.pw_passwd);
			sdup(&cmd->pwent.pw_class);
			sdup(&cmd->pwent.pw_gecos);
			sdup(&cmd->pwent.pw_dir);
			sdup(&cmd->pwent.pw_shell);
			break;
		case 'g':
			setgroupent(1);
			if (isdigit(optarg[0])) {
				grent = getgrnam(optarg);
			} else {
				grent = getgrgid(strtol(optarg, NULL, 0));
			}
			if (grent == NULL) {
				fprintf(fp, "Cannot find group %s: %s\n",
					optarg,
					strerror(errno));
				goto failed;
			}
			sfree(&cmd->grent.gr_name);
			sfree(&cmd->grent.gr_passwd);
			afree(&cmd->grent.gr_mem);
			cmd->grent = *grent;
			sdup(&cmd->grent.gr_name);
			sdup(&cmd->grent.gr_passwd);
			adup(&cmd->grent.gr_mem);
			break;
		case 'G':
			setgroupent(1);
			cpy = strdup(optarg);
			sub = strtok(cpy, ",");
			i = 0;
			while (sub) {
				if (isdigit(optarg[0])) {
					grent = getgrnam(optarg);
				} else {
					grent = getgrgid(strtol(optarg,
								NULL, 0));
				}
				if (grent == NULL) {
					fprintf(fp,
						"Cannot find group %s: %s\n",
						optarg,
						strerror(errno));
					i = -1;
				}
				if (i == NGROUPS) {
					fprintf(fp,
						"Too many groups specified, "
						"max %d\n", NGROUPS);
					i = -1;
				}
				if (i >= 0)
					cmd->groups[i++] = grent->gr_gid;
				sub = strtok(NULL, ",");
			}
			free(cpy);
			if (i < 0)
				goto failed;
			cmd->ngroups = i;
			break;
		case 'l':
			sreplace(&cmd->logfile, optarg);
			break;
		case 'C':
			cmd->mountdev = 1;
			/* fall through */
		case 'c':
			sreplace(&cmd->rootdir, optarg);
			break;
		case 'J':
			cmd->mountdev = 1;
			/* fall through */
		case 'j':
			sreplace(&cmd->jaildir, optarg);
			break;
		case 'k':
			rc = process_jailspec(cmd, optarg);
			if (rc)
				goto failed;
			break;
		case 'T':
			sreplace(&cmd->proctitle, optarg);
			break;
		case 'F':
			cmd->restart_per = 60;
			if (sscanf(optarg, "%d:%d",
				   &cmd->restart_count,
				   &cmd->restart_per) < 1) {
				fprintf(fp, "bad restart specification: %s\n",
					optarg);
				goto failed;
			}
			break;
		default:
			fprintf(fp, "Unknown option %c\n", ch);
			goto failed;
		}
	}

	/*
	 * directive [label] [...additional args]
	 *
	 * If 'all' is specified the label field is left NULL (ensure that
	 * it is NULL), and empty_label is still cleared so safety code works.
	 */
	i = optind;
	if (av[i]) {
		cmd->directive = strdup(av[i]);
		++i;
		if (av[i]) {
			cmd->empty_label = 0;
			if (strcmp(av[i], "all") == 0)
				sfree(&cmd->label);
			else
				cmd->label = strdup(av[i]);
			++i;
			cmd->ext_av = av + i;
			cmd->ext_ac = ac - i;
			adup(&cmd->ext_av);
		}
	} else {
		fprintf(fp, "No directive specified\n");
		goto failed;
	}
	rc = 0;
failed:
	endgrent();
	endpwent();

	return rc;
}

int
execute_cmd(command_t *cmd)
{
	const char *directive;
	int rc;

	directive = cmd->directive;

	/*
	 * Safely, require a label for directives that do not match
	 * this list, or 'all'.  Do not default to all if no label
	 * is specified.  e.g. things like 'kill' or 'exit' could
	 * blow up the system.
	 */
	if (cmd->empty_label) {
		if (strcmp(directive, "status") != 0 &&
		    strcmp(directive, "list") != 0 &&
		    strcmp(directive, "log") != 0 &&
		    strcmp(directive, "logf") != 0 &&
		    strcmp(directive, "help") != 0 &&
		    strcmp(directive, "tailf") != 0)  {
			fprintf(cmd->fp,
				"Directive requires a label or 'all': %s\n",
				directive);
			rc = 1;
			return rc;
		}
	}

	/*
	 * Process directives.  If we are on the remote already the
	 * execute_remote() function will simply chain to the passed-in
	 * function.
	 */
	if (strcmp(directive, "init") == 0) {
		rc = execute_init(cmd);
	} else if (strcmp(directive, "help") == 0) {
		rc = execute_help(cmd);
	} else if (strcmp(directive, "start") == 0) {
		rc = execute_remote(cmd, execute_start);
	} else if (strcmp(directive, "stop") == 0) {
		rc = execute_remote(cmd, execute_stop);
	} else if (strcmp(directive, "stopall") == 0) {
		cmd->restart_some = 0;
		cmd->restart_all = 1;
		rc = execute_remote(cmd, execute_stop);
	} else if (strcmp(directive, "restart") == 0) {
		rc = execute_remote(cmd, execute_restart);
	} else if (strcmp(directive, "exit") == 0) {
		cmd->restart_some = 0;
		cmd->restart_all = 1;		/* stop everything */
		cmd->force_remove_files = 1;
		rc = execute_remote(cmd, execute_exit);
	} else if (strcmp(directive, "kill") == 0) {
		cmd->restart_some = 0;
		cmd->restart_all = 1;		/* stop everything */
		cmd->termkill_timo = 0;		/* force immediate SIGKILL */
		cmd->force_remove_files = 1;
		rc = execute_remote(cmd, execute_exit);
	} else if (strcmp(directive, "list") == 0) {
		rc = execute_remote(cmd, execute_list);
	} else if (strcmp(directive, "status") == 0) {
		rc = execute_remote(cmd, execute_status);
	} else if (strcmp(directive, "log") == 0) {
		rc = execute_remote(cmd, execute_log);
	} else if (strcmp(directive, "logf") == 0) {
		cmd->tail_mode = 1;
		rc = execute_remote(cmd, execute_log);
	} else if (strcmp(directive, "tailf") == 0) {
		cmd->tail_mode = 2;
		rc = execute_remote(cmd, execute_log);
	} else if (strcmp(directive, "logfile") == 0) {
		rc = execute_remote(cmd, execute_logfile);
	} else {
		fprintf(cmd->fp, "Uknown directive: %s\n", directive);
		rc = 1;
	}
	return rc;
}

static
int
execute_remote(command_t *cmd, int (*func)(command_t *cmd))
{
	DIR *dir;
	struct dirent *den;
	const char *p1;
	const char *p2;
	char *plab;
	size_t cmdlen;
	size_t len;
	int rc;

	/*
	 * If already on the remote service just execute the operation
	 * as requested.
	 */
	if (cmd->cmdline == 0) {
		return (func(cmd));
	}

	/*
	 * Look for label(s).  If no exact match or label is NULL, scan
	 * piddir for matches.
	 */
	if ((dir = opendir(cmd->piddir)) == NULL) {
		fprintf(cmd->fp, "Unable to scan \"%s\"\n", cmd->piddir);
		return 1;
	}

	rc = 0;
	cmdlen = (cmd->label ? strlen(cmd->label) : 0);

	while ((den = readdir(dir)) != NULL) {
		/*
		 * service. prefix.
		 */
		if (strncmp(den->d_name, "service.", 8) != 0)
			continue;

		/*
		 * .sk suffix
		 */
		p1 = den->d_name + 8;
		p2 = strrchr(p1, '.');
		if (p2 == NULL || p2 < p1 || strcmp(p2, ".sk") != 0)
			continue;

		/*
		 * Extract the label from the service.<label>.sk name.
		 */
		len = p2 - p1;
		plab = strdup(p1);
		*strrchr(plab, '.') = 0;

		/*
		 * Start remote execution (in parallel) for all matching
		 * labels.  This will generally create some asynchronous
		 * threads.
		 */
		if (cmdlen == 0 ||
		    (cmdlen <= len && strncmp(cmd->label, plab, cmdlen) == 0)) {
			remote_execute(cmd, plab);
		}
		free(plab);
	}
	closedir(dir);

	/*
	 * Wait for completion of remote commands and dump output.
	 */
	rc = remote_wait();

	return rc;
}

void
free_cmd(command_t *cmd)
{
	sfree(&cmd->piddir);

	sfree(&cmd->pwent.pw_name);
	sfree(&cmd->pwent.pw_passwd);
	sfree(&cmd->pwent.pw_class);
	sfree(&cmd->pwent.pw_gecos);
	sfree(&cmd->pwent.pw_dir);
	sfree(&cmd->pwent.pw_shell);

	sfree(&cmd->grent.gr_name);
	sfree(&cmd->grent.gr_passwd);
	afree(&cmd->grent.gr_mem);

	sfree(&cmd->logfile);
	sfree(&cmd->rootdir);
	sfree(&cmd->jaildir);
	sfree(&cmd->proctitle);
	sfree(&cmd->directive);
	sfree(&cmd->label);
	afree(&cmd->ext_av);

	if (cmd->logfd >= 0) {
		close(cmd->logfd);
		cmd->logfd = -1;
	}

	bzero(cmd, sizeof(*cmd));
}

static
int
process_jailspec(command_t *cmd, const char *spec)
{
	char *cpy = strdup(spec);
	char *ptr;
	int rc = 0;

	ptr = strtok(cpy, ",");
	while (ptr) {
		if (strcmp(ptr, "clean") == 0) {
			cmd->jail_clean = 1;
		} else if (strncmp(ptr, "ip=", 3) == 0) {
			assert(0); /* XXX TODO */
		} else {
			fprintf(cmd->fp, "jail-spec '%s' not understood\n",
				ptr);
			rc = 1;
		}
		ptr = strtok(NULL, ",");
	}
	free(cpy);

	return rc;
}

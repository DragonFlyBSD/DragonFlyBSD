/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
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

#include <sys/types.h>
#include <sys/syslimits.h>
#include <sys/ioctl.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/devfs_rules.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <libgen.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#include "devfsctl.h"

struct verb {
	const char *verb;
	rule_parser_t *parser;
	int	min_args;
};

struct devtype {
	const char *name;
	int	value;
};



static int parser_include(char **);
static int parser_jail(char **);
static int parser_hide(char **);
static int parser_show(char **);
static int parser_link(char **);
static int parser_group(char **);
static int parser_perm(char **);
static int dump_config_entry(struct rule *, struct groupdevid *);
static int rule_id_iterate(struct groupdevid *, struct rule *,
		rule_iterate_callback_t *);
static int rule_ioctl(unsigned long, struct devfs_rule_ioctl *);
static void rule_fill(struct devfs_rule_ioctl *, struct rule *,
		struct groupdevid *);
static int rule_send(struct rule *, struct groupdevid *);
static int rule_check_num_args(char **, int);
static int process_line(FILE*, int);
static int rule_parser(char **tokens);
#if 0
static int ruletab_parser(char **tokens);
#endif
static void usage(void);

static int dev_fd;

const char *config_name = NULL, *mountp = NULL;
static int dflag = 0;
static int aflag = 0, cflag = 0, rflag = 0, tflag = 0;
static int line_stack[RULE_MAX_STACK];
static char *file_stack[RULE_MAX_STACK];
static char *cwd_stack[RULE_MAX_STACK];
static int line_stack_depth = 0;
static int jail = 0;

static TAILQ_HEAD(, rule) rule_list =
		TAILQ_HEAD_INITIALIZER(rule_list);
static TAILQ_HEAD(, rule_tab) rule_tab_list =
		TAILQ_HEAD_INITIALIZER(rule_tab_list);
static TAILQ_HEAD(, groupdevid) group_list =
		TAILQ_HEAD_INITIALIZER(group_list);


static const struct verb parsers[] = {
	{ "include", parser_include, 1 },
	{ "jail", parser_jail, 1 },
	{ "group", parser_group, 2 },
	{ "perm", parser_perm, 2 },
	{ "link", parser_link, 2 },
	{ "hide", parser_hide, 2 },
	{ "show", parser_show, 2 },
	{ NULL, NULL, 0 }
};

static const struct devtype devtypes[] = {
	{ "D_TAPE", D_TAPE },
	{ "D_DISK", D_DISK },
	{ "D_TTY", D_TTY },
	{ "D_MEM", D_MEM },
	{ NULL, 0 }
};

int
syntax_error(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	errx(1, "%s: syntax error on line %d: %s\n",file_stack[line_stack_depth],
			line_stack[line_stack_depth], buf);
}

static int
parser_include(char **tokens)
{
	struct stat	sb;
	int error;

	error = stat(tokens[1], &sb);

	if (error)
		syntax_error("could not stat %s on include, error: %s",
		    tokens[1], strerror(errno));

	chdir(dirname(tokens[1]));
	read_config(basename(tokens[1]), RULES_FILE);

	return 0;
}

static int
parser_jail(char **tokens)
{
	if (tokens[1][0] == 'y') {
		jail = 1;
	} else if (tokens[1][0] == 'n') {
		jail = 0;
	} else {
		syntax_error("incorrect argument to 'jail'. Must be either y[es] or n[o]");
	}

	return 0;
}

static int
parser_hide(char **tokens)
{
	struct groupdevid *id;
	struct rule *rule;

	id = get_id(tokens[1]);
	rule = new_rule(rHIDE, id);
	add_rule(rule);

	return 0;
}

static int
parser_show(char **tokens)
{
	struct groupdevid *id;
	struct rule *rule;

	id = get_id(tokens[1]);
	rule = new_rule(rSHOW, id);
	add_rule(rule);

	return 0;
}

static int
parser_link(char **tokens)
{
	struct groupdevid *id;
	struct rule *rule;

	id = get_id(tokens[1]);
	rule = new_rule(rLINK, id);
	rule->dest = strdup(tokens[2]);
	add_rule(rule);

	return 0;
}

static int
parser_group(char **tokens)
{
	struct groupdevid *gid, *id;
	int i;
	size_t k;

	gid = get_group(tokens[1], 1);
	for (k = 0; gid->list[k] != NULL; k++)
		/* Do nothing */;
	for (i = 2; tokens[i] != NULL; i++) {
		id = get_id(tokens[i]);
		if (id == gid) {
			syntax_error("recursive group definition for group %s", gid->name);
		} else {
			if (k >= gid->listsize-1 ) {
				gid->list = realloc(gid->list,
						2*gid->listsize*sizeof(struct groupdevid *));
				gid->listsize *= 2;
			}

			gid->list[k++] = id;
		}
	}
	gid->list[k] = NULL;

	return 0;
}

static int
parser_perm(char **tokens)
{
	struct passwd *pwd;
	struct group *grp;
	struct groupdevid *id;
	struct rule *rule;
	char *uname;
	char *grname;

	id = get_id(tokens[1]);
	rule = new_rule(rPERM, id);

	rule->mode = strtol(tokens[3], NULL, 8);
	uname = tokens[2];
	grname = strchr(tokens[2], ':');
	if (grname == NULL)
		syntax_error("invalid format for user/group (%s)", tokens[2]);

	*grname = '\0';
	++grname;
	if ((pwd = getpwnam(uname)))
		rule->uid = pwd->pw_uid;
	else
		syntax_error("invalid user name %s", uname);

	if ((grp = getgrnam(grname)))
		rule->gid = grp->gr_gid;
	else
		syntax_error("invalid group name %s", grname);

	add_rule(rule);
	return 0;
}

struct groupdevid *
new_id(const char *name, int type_in)
{
	struct groupdevid *id;
	int type = (type_in != 0)?(type_in):(isNAME), i;

	id = calloc(1, sizeof(*id));
	if (id == NULL)
		err(1, NULL);

	if (type_in == 0) {
		for (i = 0; devtypes[i].name != NULL; i++) {
			if (!strcmp(devtypes[i].name, name)) {
				type = isTYPE;
				id->devtype = devtypes[i].value;
				break;
			}
		}
	}
	id->type = type;

	if ((type == isNAME) || (type == isGROUP)) {
		id->name = strdup(name);
	}

	if (type == isGROUP) {
		id->list = calloc(4, sizeof(struct groupdevid *));
		memset(id->list, 0, 4 * sizeof(struct groupdevid *));
		id->listsize = 4;
	}

	return (id);
}

struct groupdevid *
get_id(const char *name)
{
	struct groupdevid *id;

	if ((name[0] == '@') && (name[1] != '\0')) {
		id = get_group(name+1, 0);
		if (id == NULL)
			syntax_error("unknown group name '%s', you "
					"have to use the 'group' verb first.", name+1);
	}
	else
		id = new_id(name, 0);

	return id;
}

struct groupdevid *
get_group(const char *name, int expect)
{
	struct groupdevid *g;

	TAILQ_FOREACH(g, &group_list, link) {
		if (strcmp(g->name, name) == 0)
			return (g);
	}

	/* Caller doesn't expect to get a group no matter what */
	if (!expect)
		return NULL;

	g = new_id(name, isGROUP);
	TAILQ_INSERT_TAIL(&group_list, g, link);
	return (g);
}

struct rule *
new_rule(int type, struct groupdevid *id)
{
	struct rule *rule;

	rule = calloc(1, sizeof(*rule));
	if (rule == NULL)
		err(1, NULL);

	rule->type = type;
	rule->id = id;
	rule->jail = jail;
	return (rule);
}

void
add_rule(struct rule *rule)
{
	TAILQ_INSERT_TAIL(&rule_list, rule, link);
}

static int
dump_config_entry(struct rule *rule, struct groupdevid *id)
{
	struct passwd *pwd;
	struct group *grp;
	int i;

	switch (rule->type) {
	case rPERM: printf("perm "); break;
	case rLINK: printf("link "); break;
	case rHIDE: printf("hide "); break;
	case rSHOW: printf("show "); break;
	default: errx(1, "invalid rule type");
	}

	switch (id->type) {
	case isGROUP: printf("@"); /* FALLTHROUGH */
	case isNAME: printf("%s", id->name); break;
	case isTYPE:
		for (i = 0; devtypes[i].name != NULL; i++) {
			if (devtypes[i].value == id->devtype) {
				printf("%s", devtypes[i].name);
				break;
			}
		}
		break;
	default: errx(1, "invalid id type %d", id->type);
	}

	switch (rule->type) {
	case rPERM:
		pwd = getpwuid(rule->uid);
		grp = getgrgid(rule->gid);
		if (pwd && grp) {
			printf(" %s:%s 0%.03o",
				   pwd->pw_name,
				   grp->gr_name,
				   rule->mode);
		} else {
			printf(" %d:%d 0%.03o",
				   rule->uid,
				   rule->gid,
				   rule->mode);
		}
		break;
	case rLINK:
		printf(" %s", rule->dest);
		break;
	default: /* NOTHING */;
	}

	if (rule->jail)
		printf("\t(only affects jails)");

	printf("\n");

	return 0;
}

static int
rule_id_iterate(struct groupdevid *id, struct rule *rule,
		rule_iterate_callback_t *callback)
{
	int error = 0;
	int i;

	if (id->type == isGROUP) {
		for (i = 0; id->list[i] != NULL; i++) {
			if ((error = rule_id_iterate(id->list[i], rule, callback)))
				return error;
		}
	} else {
		error = callback(rule, id);
	}

	return error;
}

void
dump_config(void)
{
	struct rule *rule;

	TAILQ_FOREACH(rule, &rule_list, link) {
		rule_id_iterate(rule->id, rule, dump_config_entry);
	}
}

static int
rule_ioctl(unsigned long cmd, struct devfs_rule_ioctl *rule)
{
	if (ioctl(dev_fd, cmd, rule) == -1)
		err(1, "ioctl");

	return 0;
}

static void
rule_fill(struct devfs_rule_ioctl *dr, struct rule *r, struct groupdevid *id)
{
	dr->rule_type = 0;
	dr->rule_cmd = 0;

	switch (id->type) {
	default:
		errx(1, "invalid id type");
	case isGROUP:
		errx(1, "internal error: can not fill group rule");
		/* NOTREACHED */
	case isNAME:
		dr->rule_type |= DEVFS_RULE_NAME;
		strncpy(dr->name, id->name, PATH_MAX-1);
		break;
	case isTYPE:
		dr->rule_type |= DEVFS_RULE_TYPE;
		dr->dev_type = id->devtype;
		break;
	}

	switch (r->type) {
	case rPERM:
		dr->rule_cmd |= DEVFS_RULE_PERM;
		dr->uid = r->uid;
		dr->gid = r->gid;
		dr->mode = r->mode;
		break;
	case rLINK:
		dr->rule_cmd |= DEVFS_RULE_LINK;
		strncpy(dr->linkname, r->dest, PATH_MAX-1);
		break;
	case rHIDE:
		dr->rule_cmd |= DEVFS_RULE_HIDE;
		break;
	case rSHOW:
		dr->rule_cmd |= DEVFS_RULE_SHOW;
		break;
	}

	if (r->jail)
		dr->rule_type |= DEVFS_RULE_JAIL;
}

static int
rule_send(struct rule *rule, struct groupdevid *id)
{
	struct devfs_rule_ioctl dr;
	int r = 0;

	strncpy(dr.mntpoint, mountp, PATH_MAX-1);

	rule_fill(&dr, rule, id);
	r = rule_ioctl(DEVFS_RULE_ADD, &dr);

	return r;
}

int
rule_apply(void)
{
	struct devfs_rule_ioctl dr;
	struct rule *rule;
	int r = 0;

	strncpy(dr.mntpoint, mountp, PATH_MAX-1);

	TAILQ_FOREACH(rule, &rule_list, link) {
		r = rule_id_iterate(rule->id, rule, rule_send);
		if (r != 0)
			return (-1);
	}

	return (rule_ioctl(DEVFS_RULE_APPLY, &dr));
}

static int
rule_check_num_args(char **tokens, int num)
{
	int i;

	for (i = 0; tokens[i] != NULL; i++)
		;

	if (i < num) {
		syntax_error("at least %d tokens were expected but only %d were found", num, i);
		return 1;
	}
	return 0;
}

int
read_config(const char *name, int ftype)
{
	FILE *fd;
	struct stat sb;

	if ((fd = fopen(name, "r")) == NULL) {
		printf("Error opening config file %s\n", name);
		perror("fopen");
		return 1;
	}

	if (fstat(fileno(fd), &sb) != 0) {
		errx(1, "file %s could not be fstat'ed, aborting", name);
	}

	if (sb.st_uid != 0)
		errx(1, "file %s does not belong to root, aborting!", name);

	if (++line_stack_depth >= RULE_MAX_STACK) {
		--line_stack_depth;
		syntax_error("Maximum include depth (%d) exceeded, "
				"check for recursion.", RULE_MAX_STACK);
	}

	line_stack[line_stack_depth] = 1;
	file_stack[line_stack_depth] = strdup(name);
	cwd_stack[line_stack_depth] = getwd(NULL);

	while (process_line(fd, ftype) == 0)
		line_stack[line_stack_depth]++;

	fclose(fd);

	free(file_stack[line_stack_depth]);
	free(cwd_stack[line_stack_depth]);
	--line_stack_depth;
	chdir(cwd_stack[line_stack_depth]);

	return 0;
}

static int
process_line(FILE* fd, int ftype)
{
	char buffer[4096];
	char *tokens[256];
	int c, n, i = 0;
	int quote = 0;
	int ret = 0;

	while (((c = fgetc(fd)) != EOF) && (c != '\n')) {
		buffer[i++] = (char)c;
		if (i == (sizeof(buffer) -1))
			break;
	}
	buffer[i] = '\0';

	if (feof(fd) || ferror(fd))
		ret = 1;
	c = 0;
	while (((buffer[c] == ' ') || (buffer[c] == '\t')) && (c < i)) c++;
	/*
	 * If this line effectively (after indentation) begins with the comment
	 * character #, we ignore the rest of the line.
	 */
	if (buffer[c] == '#')
		return 0;

	tokens[0] = &buffer[c];
	for (n = 1; c < i; c++) {
		if (buffer[c] == '"') {
			quote = !quote;
			if (quote) {
				if ((c >= 1) && (&buffer[c] != tokens[n-1])) {
					syntax_error("stray opening quote not at beginning of token");
					/* NOTREACHED */
				}
				tokens[n-1] = &buffer[c+1];
			} else {
				if ((c < i-1) && (!iswhitespace(buffer[c+1]))) {
					syntax_error("stray closing quote not at end of token");
					/* NOTREACHED */
				}
				buffer[c] = '\0';
			}
		}

		if (quote) {
			continue;
		}

		if ((buffer[c] == ' ') || (buffer[c] == '\t')) {
			buffer[c++] = '\0';
			while ((iswhitespace(buffer[c])) && (c < i)) c++;
			tokens[n++] = &buffer[c--];
		}
	}
	tokens[n] = NULL;

	/*
	 * If there are not enough arguments for any function or it is
	 * a line full of whitespaces, we just return here. Or if a
	 * quote wasn't closed.
	 */
	if ((quote) || (n < 2) || (tokens[0][0] == '\0'))
		return ret;

	switch (ftype) {
	case RULES_FILE:
		ret = rule_parser(tokens);
		break;
#if 0
	case RULETAB_FILE:
		ret = ruletab_parser(tokens);
		break;
#endif
	default:
		ret = 1;
	}

	return ret;
}

static int
rule_parser(char **tokens)
{
	int i;
	int parsed = 0;

	/* Convert the command/verb to lowercase */
	for (i = 0; tokens[0][i] != '\0'; i++)
		tokens[0][i] = tolower(tokens[0][i]);

	for (i = 0; parsers[i].verb; i++) {
		if (rule_check_num_args(tokens, parsers[i].min_args) != 0)
			continue;

		if (!strcmp(tokens[0], parsers[i].verb)) {
			parsers[i].parser(tokens);
			parsed = 1;
			break;
		}
	}
	if (parsed == 0) {
		syntax_error("unknown verb/command %s", tokens[0]);
	}

	return 0;
}

#if 0
static int
ruletab_parser(char **tokens)
{
	struct rule_tab *rt;
	struct stat	sb;
	int i;
	int error;

	if (rule_check_num_args(tokens, 2) != 0)
		return 0;

	error = stat(tokens[0], &sb);
	if (error) {
		printf("ruletab warning: could not stat %s: %s\n",
		    tokens[0], strerror(errno));
	}

	if (tokens[0][0] != '/') {
		errx(1, "ruletab error: entry %s does not seem to be an absolute path",
		    tokens[0]);
	}

	for (i = 1; tokens[i] != NULL; i++) {
		rt = calloc(1, sizeof(struct rule_tab));
		rt->mntpoint = strdup(tokens[0]);
		rt->rule_file = strdup(tokens[i]);
		TAILQ_INSERT_TAIL(&rule_tab_list, rt, link);
	}

	return 0;
}

void
rule_tab(void)
{
	struct rule_tab *rt;
	int error;
	int mode;

	chdir("/etc/devfs");
	error = read_config("ruletab", RULETAB_FILE);

	if (error)
		errx(1, "could not read/process ruletab file (/etc/devfs/ruletab)");

	if (!strcmp(mountp, "*")) {
		mode = RULETAB_ALL;
	} else if (!strcmp(mountp, "boot")) {
		mode = RULETAB_ONLY_BOOT;
	} else if (mountp) {
		mode = RULETAB_SPECIFIC;
	} else {
		errx(1, "-t needs -m");
	}

	dev_fd = open("/dev/devfs", O_RDWR);
	if (dev_fd == -1)
		err(1, "open(/dev/devfs)");

	TAILQ_FOREACH(rt, &rule_tab_list, link) {
		switch(mode) {
		case RULETAB_ONLY_BOOT:
			if ((strcmp(rt->mntpoint, "*") != 0) &&
			    (strcmp(rt->mntpoint, "/dev") != 0)) {
				continue;
			}
			break;
		case RULETAB_SPECIFIC:
			if (strcmp(rt->mntpoint, mountp) != 0)
				continue;
			break;
		}
		delete_rules();
		read_config(rt->rule_file, RULES_FILE);
		mountp = rt->mntpoint;
		rule_apply();
	}

	close(dev_fd);

	return;
}

void
delete_rules(void)
{
	struct rule *rp;
	struct groupdevid *gdp;

	TAILQ_FOREACH(rp, &rule_list, link) {
		TAILQ_REMOVE(&rule_list, rp, link);
	}

	TAILQ_FOREACH(gdp, &group_list, link) {
		TAILQ_REMOVE(&group_list, gdp, link);
	}
}
#endif

static void
usage(void)
{
	fprintf(stderr,
		"Usage: devfsctl <commands> [options]\n"
		"Valid commands are:\n"
		" -a\n"
		"\t Loads all read rules into the kernel and applies them\n"
		" -c\n"
		"\t Clears all rules stored in the kernel but does not reset the nodes\n"
		" -d\n"
		"\t Dumps the rules that have been loaded to the screen to verify syntax\n"
		" -r\n"
		"\t Resets all devfs_nodes but does not clear the rules stored\n"
		"\n"
		"Valid options and its arguments are:\n"
		" -f <config_file>\n"
		"\t Specifies the configuration file to be used\n"
		" -m <mount_point>\n"
		"\t Specifies a mount point to which the command will apply. Defaults to *\n"
		);

	exit(1);
}

int main(int argc, char *argv[])
{
	struct devfs_rule_ioctl dummy_rule;
	struct stat sb;
	int ch, error;

	while ((ch = getopt(argc, argv, "acdf:hm:r")) != -1) {
		switch (ch) {
		case 'f':
			config_name = optarg;
			break;
		case 'm':
			mountp = optarg;
			break;
		case 'a':
			aflag = 1;
			break;
		case 'c':
			cflag = 1;
			break;
		case 'r':
			rflag = 1;
			break;
		case 'd':
			dflag = 1;
			break;

		case 'h':
		case '?':
		default:
			usage();
			/* NOT REACHED */
		}
	}

	argc -= optind;
	argv += optind;

	/*
	 * Check arguments:
	 * - need to use at least one mode
	 * - can not use -d with any other mode
	 * - can not use -t with any other mode or -f
	 */
	if (!(aflag || rflag || cflag || dflag) ||
	    (dflag && (aflag || rflag || cflag || tflag))) {
		usage();
		/* NOT REACHED */
	}

	if (mountp == NULL)
		mountp = "*";
	else if (mountp[0] != '/') {
		errx(1, "-m needs to be given an absolute path");
	}

	strncpy(dummy_rule.mntpoint, mountp, PATH_MAX-1);

	if (config_name != NULL) {
		error = stat(config_name, &sb);

		if (error) {
			chdir("/etc/devfs");
			error = stat(config_name, &sb);
		}

		if (error)
			err(1, "could not stat specified configuration file %s", config_name);

		if (config_name[0] == '/')
			chdir(dirname(config_name));

		read_config(config_name, RULES_FILE);
	}

	if (dflag) {
		dump_config();
		exit(0);
	}

	dev_fd = open("/dev/devfs", O_RDWR);
	if (dev_fd == -1)
		err(1, "open(/dev/devfs)");

	if (cflag)
		rule_ioctl(DEVFS_RULE_CLEAR, &dummy_rule);

	if (rflag)
		rule_ioctl(DEVFS_RULE_RESET, &dummy_rule);

	if (aflag)
		rule_apply();

	close(dev_fd);

	return 0;
}

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
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <err.h>
#include <pwd.h>
#include <grp.h>
#define SPECNAMELEN 15
#include <sys/vfs/devfs/devfs_rules.h>
#include <sys/device.h>

void dump_config(void);
void rule_init(struct devfs_rule *rule, char *name);
int rule_check_num_args(char **tokens, int num);
void rule_group(char **tokens);
void rule_parse_groups(void);
void rule_clear_groups(void);
void rule_clear_rules(void);
void rule_perm(char **tokens);
void rule_link(char **tokens);
void rule_hide(char **tokens);
void rule_show(char **tokens);
struct devfs_rule *get_group(char *name);
struct devfs_rule *get_rule(char *name);
void put_rule(struct devfs_rule *rule);
int rule_open(void);
int rule_close(void);
int rule_ioctl(unsigned long cmd, struct devfs_rule *rule);
int read_config(char *name);
int process_line(FILE* fd);
void usage(void);

static int dev_fd;
static int jail = 0;
static struct devfs_rule dummy_rule;
static int verbose = 0;


TAILQ_HEAD(devfs_rule_head, devfs_rule);

static struct devfs_rule_head devfs_rule_list = TAILQ_HEAD_INITIALIZER(devfs_rule_list);
static struct devfs_rule_head devfs_group_list = TAILQ_HEAD_INITIALIZER(devfs_group_list);


void
dump_config(void)
{
	struct devfs_rule *rule;
	struct passwd *pwd;
	struct group *grp;

	TAILQ_FOREACH(rule, &devfs_rule_list, link) {
		printf("-----------------------------------------\n");
		printf("Affects:\t");
		if (rule->dev_type) {
			printf("device type ");
			switch (rule->dev_type) {
			case D_TAPE:
				puts("D_TAPE");
				break;
			case D_DISK:
				puts("D_DISK");
				break;
			case D_TTY:
				puts("D_TTY");
				break;
			case D_MEM:
				puts("D_MEM");
				break;
			default:
				puts("*unknown");
			}
		} else {
			printf("%s\n", rule->name);
		}

		printf("only jails?\t%s\n", (rule->rule_type & DEVFS_RULE_JAIL)?"yes":"no");

		printf("Action:\t\t");
		if (rule->rule_type & DEVFS_RULE_LINK) {
			printf("link to %s\n", rule->linkname);
		} else if (rule->rule_type & DEVFS_RULE_HIDE) {
			printf("hide\n");
		} else if (rule->rule_type & DEVFS_RULE_SHOW) {
			printf("show\n");
		} else {
			pwd = getpwuid(rule->uid);
			grp = getgrgid(rule->gid);
			printf("set mode: %o\n\t\tset owner: %s\n\t\tset group: %s\n",
					rule->mode, pwd->pw_name, grp->gr_name);
		}
	}
	printf("-----------------------------------------\n");
}

void
rule_init(struct devfs_rule *rule, char *name)
{
	if (!strcmp(name, "D_TAPE")) {
		rule->rule_type = DEVFS_RULE_TYPE;
		rule->dev_type = D_TAPE;
	} else if (!strcmp(name, "D_DISK")) {
		rule->rule_type = DEVFS_RULE_TYPE;
		rule->dev_type = D_DISK;
	} else if (!strcmp(name, "D_TTY")) {
		rule->rule_type = DEVFS_RULE_TYPE;
		rule->dev_type = D_TTY;
	} else if (!strcmp(name, "D_MEM")) {
		rule->rule_type = DEVFS_RULE_TYPE;
		rule->dev_type = D_MEM;
	} else {
		strlcpy(rule->name, name, DEVFS_MAX_POLICY_LENGTH);
		rule->rule_type = DEVFS_RULE_NAME;
	}

	if (jail)
		rule->rule_type |= DEVFS_RULE_JAIL;
}

int
rule_check_num_args(char **tokens, int num)
{
	int i = 0;
	for (i = 0; tokens[i] != NULL; i++);

	if (i != num) {
		printf("This line in the configuration file is incorrect."
				" It has %d words, but %d were expected\n",
				i, num);

		for (i = 0; *(tokens[i]); i++) {
			puts(tokens[i]);
			putchar('\t');
		}
		putchar('\n');

		return 1;
	}
	return 0;
}

void
rule_group(char **tokens)
{
	struct devfs_rule *group, *rule;

	rule = get_rule(NULL);
	rule_init(rule, tokens[0]);

	if (!(group = get_group(tokens[2]))) {
		group = get_rule(NULL);
		strlcpy(group->name, tokens[2], DEVFS_MAX_POLICY_LENGTH);
		TAILQ_INSERT_TAIL(&devfs_group_list, group, link);
	}

	rule->group = group;
	TAILQ_INSERT_TAIL(&devfs_rule_list, rule, link);
}

void
rule_parse_groups(void)
{
	struct devfs_rule *rule, *group;

	TAILQ_FOREACH(rule, &devfs_rule_list, link) {
		if ((group = rule->group)) {
			rule->group = 0;
			rule->rule_type |= group->rule_type;
			rule->mode = group->mode;
			rule->uid = group->uid;
			rule->gid = group->gid;

			if (group->linkname[0] != '\0')
				strlcpy(rule->linkname, group->linkname, SPECNAMELEN+1);
		}
	}
}

void
rule_clear_groups(void)
{
	struct devfs_rule *rule, *rule1;
	TAILQ_FOREACH_MUTABLE(rule, &devfs_group_list, link, rule1) {
		TAILQ_REMOVE(&devfs_group_list, rule, link);
		put_rule(rule);
	}
}

void
rule_clear_rules(void)
{
	struct devfs_rule *rule, *rule1;
	TAILQ_FOREACH_MUTABLE(rule, &devfs_rule_list, link, rule1) {
		TAILQ_REMOVE(&devfs_rule_list, rule, link);
		put_rule(rule);
	}
}

void
rule_perm(char **tokens)
{
	struct devfs_rule *rule;
	struct passwd *pwd;
	struct group *grp;
	rule = get_rule(tokens[0]);
	rule_init(rule, tokens[0]);
	rule->mode = strtol(tokens[2], NULL, 8);
	if ((pwd = getpwnam(tokens[3])))
		rule->uid = pwd->pw_uid;
	if ((grp = getgrnam(tokens[4])))
		rule->gid = grp->gr_gid;
	TAILQ_INSERT_TAIL(&devfs_rule_list, rule, link);
}

void
rule_link(char **tokens)
{
	struct devfs_rule *rule;

	rule = get_rule(tokens[0]);
	rule_init(rule, tokens[0]);

	strlcpy(rule->linkname, tokens[2], SPECNAMELEN+1);
	rule->rule_type |= DEVFS_RULE_LINK;

	TAILQ_INSERT_TAIL(&devfs_rule_list, rule, link);
}

void
rule_hide(char **tokens)
{
	struct devfs_rule *rule;

	rule = get_rule(tokens[0]);
	rule_init(rule, tokens[0]);

	rule->rule_type |= DEVFS_RULE_HIDE;

	TAILQ_INSERT_TAIL(&devfs_rule_list, rule, link);
}

void
rule_show(char **tokens)
{
	struct devfs_rule *rule;

	rule = get_rule(tokens[0]);
	rule_init(rule, tokens[0]);

	rule->rule_type |= DEVFS_RULE_SHOW;

	TAILQ_INSERT_TAIL(&devfs_rule_list, rule, link);
}

struct devfs_rule *
get_group(char *name)
{
	struct devfs_rule *rule, *found = NULL;

	TAILQ_FOREACH(rule, &devfs_group_list, link) {
		if (!strcmp(rule->name, name)) {
			found = rule;
			break;
		}
	}

	return found;
}

struct devfs_rule *
get_rule(char *name)
{
	struct devfs_rule *rule;

	if ((name == NULL) || (!(rule = get_group(name)))) {
		rule = (struct devfs_rule *)malloc(sizeof(struct devfs_rule));
		memset(rule, 0, sizeof(struct devfs_rule));
	}

	return rule;
}

void
put_rule(struct devfs_rule *rule)
{
	free(rule);
}

int
rule_open(void)
{
    if ((dev_fd = open("/dev/devfs", O_RDWR)) == -1) {
        perror("open /dev/devfs error\n");
        return 1;
    }

	return 0;
}

int
rule_close(void)
{
	close(dev_fd);
	return 0;
}

int
rule_ioctl(unsigned long cmd, struct devfs_rule *rule)
{
	if (ioctl(dev_fd, cmd, rule) == -1) {
		perror("ioctl error");
		return 1;
	}

	return 0;
}

int
read_config(char *name)
{
	FILE *fd;
	if ((fd = fopen(name, "r")) == NULL) {
		printf("Error opening config file %s\n", name);
		perror("fopen");
		return 1;
	}

	while (process_line(fd) == 0);
	rule_parse_groups();

	fclose(fd);

	return 0;
}

int
process_line(FILE* fd)
{
	char buffer[4096];
	char *tokens[256];
	int c, n, i = 0;
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
	tokens[0] = &buffer[c];
	for (n = 1, c = 0; c < i; c++) {
		if ((buffer[c] == ' ') || (buffer[c] == '\t')) {
			buffer[c++] = '\0';
			while (((buffer[c] == ' ') || (buffer[c] == '\t')) && (c < i)) c++;
			tokens[n++] = &buffer[c];
		}
	}
	tokens[n] = NULL;

	if (n < 2)
		return ret;

	if (verbose)
		printf("Currently processing verb/command: %s\n", (tokens[0][0] == '#')?(tokens[0]):(tokens[1]));

	if (!strcmp(tokens[0], "#include")) {
		/* This is an include instruction */
		if (!rule_check_num_args(tokens, 2))
			read_config(tokens[1]);
	} else if (!strcmp(tokens[0], "#jail")) {
		/* This is a jail instruction */
		if (!rule_check_num_args(tokens, 2))
			jail = (tokens[1][0] == 'y')?1:0;
	} else if (!strcmp(tokens[1], "group")) {
		/* This is a group rule */
		if (!rule_check_num_args(tokens, 3))
			rule_group(tokens);
	} else if (!strcmp(tokens[1], "perm")) {
		/* This is a perm rule */
		if (!rule_check_num_args(tokens, 5))
			rule_perm(tokens);
	} else if (!strcmp(tokens[1], "link")) {
		/* This is a link rule */
		if (!rule_check_num_args(tokens, 3))
			rule_link(tokens);
	} else if (!strcmp(tokens[1], "hide")) {
		/* This is a hide rule */
		if (!rule_check_num_args(tokens, 2))
			rule_hide(tokens);
	} else if (!strcmp(tokens[1], "show")) {
		/* This is a show rule */
		if (!rule_check_num_args(tokens, 2))
			rule_show(tokens);
	} else {
		printf("Unknown verb %s\n", tokens[1]);
	}
	return ret;
}


void
usage(void)
{
    printf("Usage: devfsctl [options] [command]\n");
    exit(1);
}


int main(int argc, char *argv[])
{
	struct devfs_rule *rule;
	int ch;
	char farg[1024], darg[1024], marg[1024];
	int fflag = 0, dflag = 0, mflag = 0;
	int aflag = 0, cflag = 0, rflag = 0;

    while ((ch = getopt(argc, argv, "acdrvf:m:")) != -1) {
        switch (ch) {
        case 'v':
            verbose = 1;
            break;
		case 'f':
			strlcpy(farg, optarg, 1024);
			fflag = 1;
			break;
		case 'm':
			strlcpy(marg, optarg, 1024);
			mflag = 1;
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
        case '?':
        default:
            usage();
        }
    }

    argc -= optind;
    argv += optind;

	if (!mflag)
		strcpy(marg, "*");

	if (cflag) {
		strlcpy(dummy_rule.mntpoint, marg, 255 + 1);
		dummy_rule.mntpointlen = strlen(rule->mntpoint);

		rule_open();
		rule_ioctl(DEVFS_RULE_CLEAR, &dummy_rule);
		rule_close();
	}

	if (rflag) {
		strlcpy(dummy_rule.mntpoint, marg, 255 + 1);
		dummy_rule.mntpointlen = strlen(rule->mntpoint);
		rule_open();
		rule_ioctl(DEVFS_RULE_RESET, &dummy_rule);
		rule_close();
	}

	if (fflag) {
		jail = 0;
		read_config(farg);
	}

	if (dflag) {
		dump_config();
	}

	if (aflag) {
		jail = 0;
		read_config(farg);

		rule_open();

		TAILQ_FOREACH(rule, &devfs_rule_list, link) {
			strlcpy(rule->mntpoint, marg, 255 + 1);
			rule->mntpointlen = strlen(rule->mntpoint);
			rule_ioctl(DEVFS_RULE_ADD, rule);
		}

		strlcpy(dummy_rule.mntpoint, marg, 255 + 1);
		dummy_rule.mntpointlen = strlen(rule->mntpoint);
		rule_ioctl(DEVFS_RULE_APPLY, &dummy_rule);

		rule_close();
		rule_clear_groups();
		rule_clear_rules();
	}


	return 0;
}

/*
 * Copyright (c) 2008 Canonical Ltd.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "includes.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#include <openssl/evp.h>

#include "xmalloc.h"
#include "ssh.h"
#include "log.h"
#include "key.h"
#include "authfile.h"
#include "pathnames.h"
#include "misc.h"

extern char *__progname;

/* Default files to check */
static char *default_host_files[] = {
	_PATH_HOST_RSA_KEY_FILE,
	_PATH_HOST_DSA_KEY_FILE,
	_PATH_HOST_KEY_FILE,
	NULL
};
static char *default_files[] = {
	_PATH_SSH_CLIENT_ID_RSA,
	_PATH_SSH_CLIENT_ID_DSA,
	_PATH_SSH_CLIENT_IDENTITY,
	_PATH_SSH_USER_PERMITTED_KEYS,
	_PATH_SSH_USER_PERMITTED_KEYS2,
	NULL
};

static int quiet = 0;

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-aq] [file ...]\n", __progname);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -a          Check keys of all users.\n");
	fprintf(stderr, "  -q          Quiet mode.\n");
	exit(1);
}

void
describe_key(const char *msg, Key *key, const char *comment)
{
	char *fp;

	fp = key_fingerprint(key, SSH_FP_MD5, SSH_FP_HEX);
	if (!quiet)
		printf("%s: %u %s %s\n", msg, key_size(key), fp, comment);
	xfree(fp);
}

int
do_key(Key *key, const char *comment)
{
	char *blacklist_file;
	struct stat st;
	int ret = 1;

	blacklist_file = blacklist_filename(key);
	if (stat(blacklist_file, &st) < 0)
		describe_key("Unknown (no blacklist information)",
		    key, comment);
	else if (blacklisted_key(key)) {
		describe_key("COMPROMISED", key, comment);
		ret = 0;
	} else
		describe_key("Not blacklisted", key, comment);
	xfree(blacklist_file);

	return ret;
}

int
do_filename(const char *filename, int quiet_open)
{
	FILE *f;
	char line[SSH_MAX_PUBKEY_BYTES];
	char *cp;
	u_long linenum = 0;
	Key *key;
	char *comment = NULL;
	int found = 0, ret = 1;

	/* Copy much of key_load_public's logic here so that we can read
	 * several keys from a single file (e.g. authorized_keys).
	 */

	if (strcmp(filename, "-") != 0) {
		f = fopen(filename, "r");
		if (!f) {
			char pubfile[MAXPATHLEN];
			if (strlcpy(pubfile, filename, sizeof pubfile) <
			    sizeof(pubfile) &&
			    strlcat(pubfile, ".pub", sizeof pubfile) <
			    sizeof(pubfile))
				f = fopen(pubfile, "r");
		}
		if (!f) {
			if (!quiet_open)
				perror(filename);
			return -1;
		}
	} else
		f = stdin;
	while (read_keyfile_line(f, filename, line, sizeof(line),
		    &linenum) != -1) {
		int i;
		char *space;
		int type;

		/* Chop trailing newline. */
		i = strlen(line) - 1;
		if (line[i] == '\n')
			line[i] = '\0';

		/* Skip leading whitespace, empty and comment lines. */
		for (cp = line; *cp == ' ' || *cp == '\t'; cp++)
			;
		if (!*cp || *cp == '\n' || *cp == '#')
			continue;

		/* Cope with ssh-keyscan output and options in
		 * authorized_keys files.
		 */
		space = strchr(cp, ' ');
		if (!space)
			continue;
		*space = '\0';
		type = key_type_from_name(cp);
		*space = ' ';
		/* Leading number (RSA1) or valid type (RSA/DSA) indicates
		 * that we have no host name or options to skip.
		 */
		if (atoi(cp) == 0 && type == KEY_UNSPEC) {
			int quoted = 0;

			for (; *cp && (quoted || (*cp != ' ' && *cp != '\t')); cp++) {
				if (*cp == '\\' && cp[1] == '"')
					cp++;	/* Skip both */
				else if (*cp == '"')
					quoted = !quoted;
			}
			/* Skip remaining whitespace. */
			for (; *cp == ' ' || *cp == '\t'; cp++)
				;
			if (!*cp)
				continue;
		}

		/* Read and process the key itself. */
		key = key_new(KEY_RSA1);
		if (key_read(key, &cp) == 1) {
			while (*cp == ' ' || *cp == '\t')
				cp++;
			if (!do_key(key, *cp ? cp : filename))
				ret = 0;
			found = 1;
		} else {
			key_free(key);
			key = key_new(KEY_UNSPEC);
			if (key_read(key, &cp) == 1) {
				while (*cp == ' ' || *cp == '\t')
					cp++;
				if (!do_key(key, *cp ? cp : filename))
					ret = 0;
				found = 1;
			}
		}
		key_free(key);
	}
	if (f != stdin)
		fclose(f);

	if (!found && filename) {
		key = key_load_public(filename, &comment);
		if (key) {
			if (!do_key(key, comment))
				ret = 0;
			found = 1;
		}
		if (comment)
			xfree(comment);
	}

	return ret;
}

int
do_host(void)
{
	int i;
	struct stat st;
	int ret = 1;

	for (i = 0; default_host_files[i]; i++) {
		if (stat(default_host_files[i], &st) < 0)
			continue;
		if (!do_filename(default_host_files[i], 1))
			ret = 0;
	}

	return ret;
}

int
do_user(const char *dir)
{
	int i;
	char buf[MAXPATHLEN];
	struct stat st;
	int ret = 1;

	for (i = 0; default_files[i]; i++) {
		snprintf(buf, sizeof(buf), "%s/%s", dir, default_files[i]);
		if (stat(buf, &st) < 0)
			continue;
		if (!do_filename(buf, 0))
			ret = 0;
	}

	return ret;
}

int
main(int argc, char **argv)
{
	int opt, all_users = 0;
	int ret = 1;
	extern int optind;

	/* Ensure that fds 0, 1 and 2 are open or directed to /dev/null */
	sanitise_stdfd();

	__progname = ssh_get_progname(argv[0]);

	SSLeay_add_all_algorithms();
	log_init(argv[0], SYSLOG_LEVEL_INFO, SYSLOG_FACILITY_USER, 1);

	/* We don't need the RNG ourselves, but symbol references here allow
	 * ld to link us properly.
	 */
	seed_rng();

	while ((opt = getopt(argc, argv, "ahq")) != -1) {
		switch (opt) {
		case 'a':
			all_users = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'h':
		default:
			usage();
		}
	}

	if (all_users) {
		struct passwd *pw;

		if (!do_host())
			ret = 0;

		while ((pw = getpwent()) != NULL) {
			if (pw->pw_dir) {
				if (!do_user(pw->pw_dir))
					ret = 0;
			}
		}
	} else if (optind == argc) {
		struct passwd *pw;

		if (!do_host())
			ret = 0;

		if ((pw = getpwuid(getuid())) == NULL)
			fprintf(stderr, "No user found with uid %u\n",
			    (u_int)getuid());
		else {
			if (!do_user(pw->pw_dir))
				ret = 0;
		}
	} else {
		while (optind < argc)
			if (!do_filename(argv[optind++], 0))
				ret = 0;
	}

	return ret;
}

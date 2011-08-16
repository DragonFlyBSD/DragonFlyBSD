/*
 * Copyright (c) 2011 François Tigeot <ftigeot@wolfpond.org>
 * All rights reserved.
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

#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/vfs_quota.h>

#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <string.h>
#include <fts.h>
#include <libprop/proplib.h>
#include <unistd.h>
#include <inttypes.h>

static bool flag_debug = 0;

static void usage(int);
static int get_dirsize(char *);
static int get_fslist(void);

static void
usage(int retcode)
{
	fprintf(stderr, "usage: vquota [-D] check directory\n");
	fprintf(stderr, "       vquota [-D] lsfs\n");
	fprintf(stderr, "       vquota [-D] show mount_point\n");
	exit(retcode);
}

static int
get_dirsize(char* dirname)
{
	FTS		*fts;
	FTSENT		*p;
	char*		fts_args[2];
	uint64_t	size_of_files = 0;
	int		retval = 0;

	/* TODO: check directory name sanity */
	fts_args[0] = dirname;
	fts_args[1] = NULL;

	if ((fts = fts_open(fts_args, FTS_PHYSICAL, NULL)) == NULL)
		err(1, "fts_open() failed");

	while ((p = fts_read(fts)) != NULL) {
		switch (p->fts_info) {
		/* directories, ignore them */
		case FTS_D:
		case FTS_DC:
		case FTS_DP:
			break;
		/* read errors, warn, continue and flag */
		case FTS_DNR:
		case FTS_ERR:
		case FTS_NS:
			warnx("%s: %s", p->fts_path, strerror(p->fts_errno));
			retval = 1;
			break;
		default:
			size_of_files += p->fts_statp->st_size;
		}
	}
	fts_close(fts);

	printf("%"PRIu64"\n", size_of_files);
	return retval;
}

/* print a list of filesystems with accounting enabled */
static int get_fslist(void) {
	struct statfs *mntbufp;
	int nloc, i;

	/* read mount table from kernel */
	nloc = getmntinfo(&mntbufp, MNT_NOWAIT|MNT_LOCAL);
	if (nloc <= 0) {
		perror("getmntinfo");
		exit(1);
	}

	/* iterate mounted filesystems */
	for (i=0; i<nloc; i++) {
	    /* vfs accounting enabled on this one ? */
	    if (mntbufp[i].f_flags & MNT_ACCOUNTING)
		printf("%s on %s\n", mntbufp[i].f_mntfromname,
						mntbufp[i].f_mntonname);
	}

	return 0;
}

static bool
send_command(const char *path, const char *cmd,
		prop_dictionary_t args, prop_dictionary_t *res) {
	prop_dictionary_t dict;
	struct plistref pref;

	bool rv;
	int error;

	dict = prop_dictionary_create();

	if (dict == NULL) {
		printf("send_command(): couldn't create dictionary\n");
		return false;
	}

	rv = prop_dictionary_set_cstring(dict, "command", cmd);
	if (rv== false) {
		printf("send_command(): couldn't initialize dictionary\n");
		return false;
	}

	rv = prop_dictionary_set(dict, "arguments", args);
	if (rv == false) {
		printf("prop_dictionary_set() failed\n");
		return false;
	}

	error = prop_dictionary_send_syscall(dict, &pref);
	if (error != 0) {
		printf("prop_dictionary_send_syscall() failed\n");
		prop_object_release(dict);
		return false;
	}

	if (flag_debug)
		printf("message to kernel:\n%s\n", prop_dictionary_externalize(dict));

	error = vquotactl(path, &pref);
	if (error != 0) {
		printf("send_command: vquotactl = %d\n", error);
		return false;
	}

	error = prop_dictionary_recv_syscall(&pref, res);
	if (error != 0) {
		printf("prop_dictionary_recv_syscall() failed\n");
	}

	if (flag_debug)
		printf("Message from kernel:\n%s\n", prop_dictionary_externalize(*res));

	return true;
}

/* show collected statistics on mount point */
static int show_mp(char *path) {
	prop_dictionary_t args, res;
	prop_array_t reslist;
	bool rv;
	prop_object_iterator_t	iter;
	prop_dictionary_t item;
	uint32_t id;
	uint64_t space;

	args = prop_dictionary_create();
	res  = prop_dictionary_create();
	if (args == NULL)
		printf("couldn't create args dictionary\n");

	rv = send_command(path, "get usage all", args, &res);
	if (rv == false) {
		printf("show-mp(): failed to send message to kernel\n");
		goto end;
	}

	reslist = prop_dictionary_get(res, "get usage all");
	if (reslist == NULL) {
		printf("show_mp(): failed to get array of results");
		rv = false;
		goto end;
	}

	iter = prop_array_iterator(reslist);
	if (iter == NULL) {
		printf("show_mp(): failed to create iterator\n");
		rv = false;
		goto end;
	}

	while ((item = prop_object_iterator_next(iter)) != NULL) {
		rv = prop_dictionary_get_uint64(item, "space used", &space);
		if (prop_dictionary_get_uint32(item, "uid", &id))
			printf("uid %u:", id);
		else if (prop_dictionary_get_uint32(item, "gid", &id))
			printf("gid %u:", id);
		else
			printf("total space used");
		printf(" %" PRIu64 "\n", space);
	}
	prop_object_iterator_release(iter);

end:
	prop_object_release(args);
	prop_object_release(res);
	return (rv == true);
}

int
main(int argc, char **argv) {
	int ch;

	while ((ch = getopt(argc, argv, "D")) != -1) {
		switch(ch) {
		case 'D':
			flag_debug = 1;
			break;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc < 1)
		usage(1);
	
	if (strcmp(argv[0], "check") == 0) {
		if (argc != 2)
			usage(1);
		return get_dirsize(argv[1]);
	}
	if (strcmp(argv[0], "lsfs") == 0) {
		return get_fslist();
	}
	if (strcmp(argv[0], "show") == 0) {
		if (argc != 2)
			usage(1);
		return show_mp(argv[1]);
	}

	usage(0);
}

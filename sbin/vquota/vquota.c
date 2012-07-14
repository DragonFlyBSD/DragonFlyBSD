/*
 * Copyright (c) 2011, 2012 François Tigeot <ftigeot@wolfpond.org>
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
#include <sys/tree.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/types.h>
#include <libutil.h>
#include <pwd.h>
#include <grp.h>

static bool flag_debug = 0;
static bool flag_humanize = 0;
static bool flag_resolve_ids = 1;

static void usage(int);
static int get_dirsize(char *);
static int get_fslist(void);

static void
usage(int retcode)
{
	fprintf(stderr, "usage: vquota [-Dhn] check directory\n");
	fprintf(stderr, "       vquota [-Dhn] lsfs\n");
	fprintf(stderr, "       vquota [-Dhn] limit mount_point size\n");
	fprintf(stderr, "       vquota [-Dhn] ulim  mount_point user  size\n");
	fprintf(stderr, "       vquota [-Dhn] glim  mount_point group size\n");
	fprintf(stderr, "       vquota [-Dhn] show mount_point\n");
	fprintf(stderr, "       vquota [-Dhn] sync mount_point\n");
	exit(retcode);
}

/*
 * Inode numbers with more than one hard link often come in groups;
 * use linear arrays of 1024 ones as the basic unit of allocation.
 * We only need to check if the inodes have been previously processed,
 * bit arrays are perfect for that purpose.
 */
#define HL_CHUNK_BITS		10
#define HL_CHUNK_ENTRIES	(1<<HL_CHUNK_BITS)
#define HL_CHUNK_MASK		(HL_CHUNK_ENTRIES - 1)
#define BA_UINT64_BITS		6
#define BA_UINT64_ENTRIES	(1<<BA_UINT64_BITS)
#define BA_UINT64_MASK		(BA_UINT64_ENTRIES - 1)

struct hl_node {
	RB_ENTRY(hl_node)	rb_entry;
	ino_t			ino_left_bits;
	uint64_t		hl_chunk[HL_CHUNK_ENTRIES/64];
};

RB_HEAD(hl_tree,hl_node)	hl_root;

RB_PROTOTYPE(hl_tree, hl_node, rb_entry, rb_hl_node_cmp);

static int
rb_hl_node_cmp(struct hl_node *a, struct hl_node *b);

RB_GENERATE(hl_tree, hl_node, rb_entry, rb_hl_node_cmp);

struct hl_node* hl_node_insert(ino_t);


static int
rb_hl_node_cmp(struct hl_node *a, struct hl_node *b)
{
	if (a->ino_left_bits < b->ino_left_bits)
		return(-1);
	else if (a->ino_left_bits > b->ino_left_bits)
		return(1);
	return(0);
}

struct hl_node* hl_node_insert(ino_t inode)
{
	struct hl_node *hlp, *res;

	hlp = malloc(sizeof(struct hl_node));
	if (hlp == NULL) {
		/* shouldn't happen */
		printf("hl_node_insert(): malloc failed\n");
		exit(ENOMEM);
	}
	bzero(hlp, sizeof(struct hl_node));

	hlp->ino_left_bits = (inode >> HL_CHUNK_BITS);
	res = RB_INSERT(hl_tree, &hl_root, hlp);

	if (res != NULL)	/* shouldn't happen */
		printf("hl_node_insert(): RB_INSERT didn't return NULL\n");

	return hlp;
}

/*
 * hl_register: register an inode number in a rb-tree of bit arrays
 * returns:
 * - true if the inode was already processed
 * - false otherwise
 */
static bool
hl_register(ino_t inode)
{
	struct hl_node hl_find, *hlp;
	uint64_t ino_right_bits, ba_index, ba_offset;
	uint64_t bitmask, bitval;
	bool retval = false;

	/* calculate the different addresses of the wanted bit */
	hl_find.ino_left_bits = (inode >> HL_CHUNK_BITS);

	ino_right_bits = inode & HL_CHUNK_MASK;
	ba_index  = ino_right_bits >> BA_UINT64_BITS;
	ba_offset = ino_right_bits & BA_UINT64_MASK;

	/* no existing node? create and initialize it */
	if ((hlp = RB_FIND(hl_tree, &hl_root, &hl_find)) == NULL) {
		hlp = hl_node_insert(inode);
	}

	/* node was found, check the bit value */
	bitmask = 1 << ba_offset;
	bitval = hlp->hl_chunk[ba_index] & bitmask;
	if (bitval != 0) {
		retval = true;
	}

	/* set the bit */
	hlp->hl_chunk[ba_index] |= bitmask;

	return retval;
}

/* global variable used by get_dir_size() */
uint64_t global_size;

/* storage for collected id numbers */
/* FIXME: same data structures used in kernel, should find a way to
 * deduplicate this code */

static int
rb_ac_unode_cmp(struct ac_unode*, struct ac_unode*);
static int
rb_ac_gnode_cmp(struct ac_gnode*, struct ac_gnode*);

RB_HEAD(ac_utree,ac_unode) ac_uroot;
RB_HEAD(ac_gtree,ac_gnode) ac_groot;
RB_PROTOTYPE(ac_utree, ac_unode, rb_entry, rb_ac_unode_cmp);
RB_PROTOTYPE(ac_gtree, ac_gnode, rb_entry, rb_ac_gnode_cmp);
RB_GENERATE(ac_utree, ac_unode, rb_entry, rb_ac_unode_cmp);
RB_GENERATE(ac_gtree, ac_gnode, rb_entry, rb_ac_gnode_cmp);

static int
rb_ac_unode_cmp(struct ac_unode *a, struct ac_unode *b)
{
	if (a->left_bits < b->left_bits)
		return(-1);
	else if (a->left_bits > b->left_bits)
		return(1);
	return(0);
}

static int
rb_ac_gnode_cmp(struct ac_gnode *a, struct ac_gnode *b)
{
	if (a->left_bits < b->left_bits)
		return(-1);
	else if (a->left_bits > b->left_bits)
		return(1);
	return(0);
}

static struct ac_unode*
unode_insert(uid_t uid)
{
	struct ac_unode *unp, *res;

	unp = malloc(sizeof(struct ac_unode));
	if (unp == NULL) {
		printf("unode_insert(): malloc failed\n");
		exit(ENOMEM);
	}
	bzero(unp, sizeof(struct ac_unode));

	unp->left_bits = (uid >> ACCT_CHUNK_BITS);
	res = RB_INSERT(ac_utree, &ac_uroot, unp);

	if (res != NULL)	/* shouldn't happen */
		printf("unode_insert(): RB_INSERT didn't return NULL\n");

	return unp;
}

static struct ac_gnode*
gnode_insert(gid_t gid)
{
	struct ac_gnode *gnp, *res;

	gnp = malloc(sizeof(struct ac_gnode));
	if (gnp == NULL) {
		printf("gnode_insert(): malloc failed\n");
		exit(ENOMEM);
	}
	bzero(gnp, sizeof(struct ac_gnode));

	gnp->left_bits = (gid >> ACCT_CHUNK_BITS);
	res = RB_INSERT(ac_gtree, &ac_groot, gnp);

	if (res != NULL)	/* shouldn't happen */
		printf("gnode_insert(): RB_INSERT didn't return NULL\n");

	return gnp;
}

/*
 * get_dirsize(): walks a directory tree in the same filesystem
 * output:
 * - global rb-trees ac_uroot and ac_groot
 * - global variable global_size
 */
static int
get_dirsize(char* dirname)
{
	FTS		*fts;
	FTSENT		*p;
	char*		fts_args[2];
	int		retval = 0;

	/* what we need */
	ino_t		file_inode;
	off_t		file_size;
	uid_t		file_uid;
	gid_t		file_gid;

	struct ac_unode *unp, ufind;
	struct ac_gnode *gnp, gfind;

	/* TODO: check directory name sanity */
	fts_args[0] = dirname;
	fts_args[1] = NULL;

	if ((fts = fts_open(fts_args, FTS_PHYSICAL|FTS_XDEV, NULL)) == NULL)
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
			file_inode = p->fts_statp->st_ino;
			file_size = p->fts_statp->st_size;
			file_uid = p->fts_statp->st_uid;
			file_gid = p->fts_statp->st_gid;

			/* files with more than one hard link: */
			/* process them only once */
			if (p->fts_statp->st_nlink > 1)
				if (hl_register(file_inode))
					break;

			global_size += file_size;
			ufind.left_bits = (file_uid >> ACCT_CHUNK_BITS);
			gfind.left_bits = (file_gid >> ACCT_CHUNK_BITS);
			if ((unp = RB_FIND(ac_utree, &ac_uroot, &ufind)) == NULL)
				unp = unode_insert(file_uid);
			if ((gnp = RB_FIND(ac_gtree, &ac_groot, &gfind)) == NULL)
				gnp = gnode_insert(file_gid);
			unp->uid_chunk[(file_uid & ACCT_CHUNK_MASK)].space += file_size;
			gnp->gid_chunk[(file_gid & ACCT_CHUNK_MASK)].space += file_size;
		}
	}
	fts_close(fts);

	return retval;
}

static void
print_user(uid_t uid)
{
	struct passwd *pw;

	if (flag_resolve_ids && ((pw = getpwuid(uid)) != NULL)) {
		printf("user %s:", pw->pw_name);
	} else {
		printf("uid %u:", uid);
	}
}

static void
print_group(gid_t gid)
{
	struct group *gr;

	if (flag_resolve_ids && ((gr = getgrgid(gid)) != NULL)) {
		printf("group %s:", gr->gr_name);
	} else {
		printf("gid %u:", gid);
	}
}

static int
cmd_check(char* dirname)
{
	int32_t uid, gid;
	char	hbuf[5];
	struct  ac_unode *unp;
	struct  ac_gnode *gnp;
	int	rv, i;

	rv = get_dirsize(dirname);

	if (flag_humanize) {
		humanize_number(hbuf, sizeof(hbuf), global_size, "",
		    HN_AUTOSCALE, HN_NOSPACE);
		printf("total: %s\n", hbuf);
	} else {
		printf("total: %"PRIu64"\n", global_size);
	}
	RB_FOREACH(unp, ac_utree, &ac_uroot) {
	    for (i=0; i<ACCT_CHUNK_NIDS; i++) {
		if (unp->uid_chunk[i].space != 0) {
		    uid = (unp->left_bits << ACCT_CHUNK_BITS) + i;
		    print_user(uid);
		    if (flag_humanize) {
			humanize_number(hbuf, sizeof(hbuf),
			unp->uid_chunk[i].space, "", HN_AUTOSCALE, HN_NOSPACE);
			printf(" %s\n", hbuf);
		    } else {
			printf(" %" PRIu64 "\n", unp->uid_chunk[i].space);
		    }
		}
	    }
	}
	RB_FOREACH(gnp, ac_gtree, &ac_groot) {
	    for (i=0; i<ACCT_CHUNK_NIDS; i++) {
		if (gnp->gid_chunk[i].space != 0) {
		    gid = (gnp->left_bits << ACCT_CHUNK_BITS) + i;
		    print_group(gid);
		    if (flag_humanize) {
			humanize_number(hbuf, sizeof(hbuf),
			gnp->gid_chunk[i].space, "", HN_AUTOSCALE, HN_NOSPACE);
			printf(" %s\n", hbuf);
		    } else {
			printf(" %" PRIu64 "\n", gnp->gid_chunk[i].space);
		    }
		}
	    }
	}

	return rv;
}

/* print a list of filesystems with accounting enabled */
static int
get_fslist(void)
{
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
	    /* vfs quota enabled on this one ? */
	    if (mntbufp[i].f_flags & MNT_QUOTA)
		printf("%s on %s\n", mntbufp[i].f_mntfromname,
		    mntbufp[i].f_mntonname);
	}

	return 0;
}

static bool
send_command(const char *path, const char *cmd,
		prop_object_t args, prop_dictionary_t *res)
{
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
		printf("Message to kernel:\n%s\n",
		    prop_dictionary_externalize(dict));

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
		printf("Message from kernel:\n%s\n",
		    prop_dictionary_externalize(*res));

	return true;
}

/* show collected statistics on mount point */
static int
show_mp(char *path)
{
	prop_dictionary_t args, res;
	prop_array_t reslist;
	bool rv;
	prop_object_iterator_t	iter;
	prop_dictionary_t item;
	uint32_t id;
	uint64_t space, limit=0;
	char hbuf[5];

	args = prop_dictionary_create();
	res  = prop_dictionary_create();
	if (args == NULL)
		printf("show_mp(): couldn't create args dictionary\n");
	res  = prop_dictionary_create();
	if (res == NULL)
		printf("show_mp(): couldn't create res dictionary\n");

	rv = send_command(path, "get usage all", args, &res);
	if (rv == false) {
		printf("show-mp(): failed to send message to kernel\n");
		goto end;
	}

	reslist = prop_dictionary_get(res, "returned data");
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
		rv = prop_dictionary_get_uint64(item, "limit", &limit);
		if (prop_dictionary_get_uint32(item, "uid", &id))
			print_user(id);
		else if (prop_dictionary_get_uint32(item, "gid", &id))
			print_group(id);
		else
			printf("total:");
		if (flag_humanize) {
			humanize_number(hbuf, sizeof(hbuf), space, "", HN_AUTOSCALE, HN_NOSPACE);
			printf(" %s", hbuf);
		} else {
			printf(" %"PRIu64, space);
		}
		if (limit == 0) {
			printf("\n");
			continue;
		}
		if (flag_humanize) {
			humanize_number(hbuf, sizeof(hbuf), limit, "", HN_AUTOSCALE, HN_NOSPACE);
			printf(", limit = %s\n", hbuf);
		} else {
			printf(", limit = %"PRIu64"\n", limit);
		}
	}
	prop_object_iterator_release(iter);

end:
	prop_object_release(args);
	prop_object_release(res);
	return (rv == true);
}

/* sync the in-kernel counters to the actual file system usage */
static int cmd_sync(char *dirname)
{
	prop_dictionary_t res, item;
	prop_array_t args;
	struct ac_unode *unp;
	struct ac_gnode *gnp;
	int rv = 0, i;

	args = prop_array_create();
	if (args == NULL)
		printf("cmd_sync(): couldn't create args dictionary\n");
	res  = prop_dictionary_create();
	if (res == NULL)
		printf("cmd_sync(): couldn't create res dictionary\n");

	rv = get_dirsize(dirname);

	item = prop_dictionary_create();
	if (item == NULL)
		printf("cmd_sync(): couldn't create item dictionary\n");
	(void) prop_dictionary_set_uint64(item, "space used", global_size);
	prop_array_add_and_rel(args, item);

	RB_FOREACH(unp, ac_utree, &ac_uroot) {
	    for (i=0; i<ACCT_CHUNK_NIDS; i++) {
		if (unp->uid_chunk[i].space != 0) {
		    item = prop_dictionary_create();
		    (void) prop_dictionary_set_uint32(item, "uid",
				(unp->left_bits << ACCT_CHUNK_BITS) + i);
		    (void) prop_dictionary_set_uint64(item, "space used",
				unp->uid_chunk[i].space);
		    prop_array_add_and_rel(args, item);
		}
	    }
	}
	RB_FOREACH(gnp, ac_gtree, &ac_groot) {
	    for (i=0; i<ACCT_CHUNK_NIDS; i++) {
		if (gnp->gid_chunk[i].space != 0) {
		    item = prop_dictionary_create();
		    (void) prop_dictionary_set_uint32(item, "gid",
				(gnp->left_bits << ACCT_CHUNK_BITS) + i);
		    (void) prop_dictionary_set_uint64(item, "space used",
				gnp->gid_chunk[i].space);
		    prop_array_add_and_rel(args, item);
		}
	    }
	}

	if (send_command(dirname, "set usage all", args, &res) == false) {
		printf("Failed to send message to kernel\n");
		rv = 1;
	}

	prop_object_release(args);
	prop_object_release(res);

	return rv;
}

static int
cmd_limit(char *dirname, uint64_t limit)
{
	prop_dictionary_t res, args;
	int rv = 0;

	args = prop_dictionary_create();
	if (args == NULL)
		printf("cmd_limit(): couldn't create args dictionary\n");
	res  = prop_dictionary_create();
	if (res == NULL)
		printf("cmd_limit(): couldn't create res dictionary\n");

	(void) prop_dictionary_set_uint64(args, "limit", limit);

	if (send_command(dirname, "set limit", args, &res) == false) {
		printf("Failed to send message to kernel\n");
		rv = 1;
	}

	prop_object_release(args);
	prop_object_release(res);

	return rv;
}

static int
cmd_limit_uid(char *dirname, uid_t uid, uint64_t limit)
{
	prop_dictionary_t res, args;
	int rv = 0;

	args = prop_dictionary_create();
	if (args == NULL)
		printf("cmd_limit_uid(): couldn't create args dictionary\n");
	res  = prop_dictionary_create();
	if (res == NULL)
		printf("cmd_limit_uid(): couldn't create res dictionary\n");

	(void) prop_dictionary_set_uint32(args, "uid", uid);
	(void) prop_dictionary_set_uint64(args, "limit", limit);

	if (send_command(dirname, "set limit uid", args, &res) == false) {
		printf("Failed to send message to kernel\n");
		rv = 1;
	}

	prop_object_release(args);
	prop_object_release(res);

	return rv;
}

static int
cmd_limit_gid(char *dirname, gid_t gid, uint64_t limit)
{
	prop_dictionary_t res, args;
	int rv = 0;

	args = prop_dictionary_create();
	if (args == NULL)
		printf("cmd_limit_gid(): couldn't create args dictionary\n");
	res  = prop_dictionary_create();
	if (res == NULL)
		printf("cmd_limit_gid(): couldn't create res dictionary\n");

	(void) prop_dictionary_set_uint32(args, "gid", gid);
	(void) prop_dictionary_set_uint64(args, "limit", limit);

	if (send_command(dirname, "set limit gid", args, &res) == false) {
		printf("Failed to send message to kernel\n");
		rv = 1;
	}

	prop_object_release(args);
	prop_object_release(res);

	return rv;
}

int
main(int argc, char **argv)
{
	int ch;
	uint64_t limit;

	while ((ch = getopt(argc, argv, "Dhn")) != -1) {
		switch(ch) {
		case 'D':
			flag_debug = 1;
			break;
		case 'h':
			flag_humanize = 1;
			break;
		case 'n':
			flag_resolve_ids = 0;
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
		return cmd_check(argv[1]);
	}
	if (strcmp(argv[0], "lsfs") == 0) {
		return get_fslist();
	}
	if (strcmp(argv[0], "limit") == 0) {
		if (argc != 3)
			usage(1);
		if (dehumanize_number(argv[2], &limit) < 0)
			err(1, "bad number for option: %s", argv[2]);

		return cmd_limit(argv[1], limit);
	}
	if (strcmp(argv[0], "show") == 0) {
		if (argc != 2)
			usage(1);
		return show_mp(argv[1]);
	}
	if (strcmp(argv[0], "sync") == 0) {
		if (argc != 2)
			usage(1);
		return cmd_sync(argv[1]);
	}
	if (strcmp(argv[0], "ulim") == 0) {
		struct passwd *pwd;
		if (argc != 4)
			usage(1);
		if ((pwd = getpwnam(argv[2])) == NULL)
			errx(1, "%s: no such user", argv[2]);
		if (dehumanize_number(argv[3], &limit) < 0)
			err(1, "bad number for option: %s", argv[2]);

		return cmd_limit_uid(argv[1], pwd->pw_uid, limit);
	}
	if (strcmp(argv[0], "glim") == 0) {
		struct group *grp;
		if (argc != 4)
			usage(1);
		if ((grp = getgrnam(argv[2])) == NULL)
			errx(1, "%s: no such group", argv[2]);
		if (dehumanize_number(argv[3], &limit) < 0)
			err(1, "bad number for option: %s", argv[2]);

		return cmd_limit_gid(argv[1], grp->gr_gid, limit);
	}

	usage(0);
}

/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sbin/hammer/cmd_pseudofs.c,v 1.12 2008/10/08 21:01:54 thomas Exp $
 */

#include <libgen.h>

#include "hammer.h"

static int scanpfsid(struct hammer_ioc_pseudofs_rw *pfs, const char *path);
static void parse_pfsd_options(char **av, int ac, hammer_pseudofs_data_t pfsd);
static void init_pfsd(hammer_pseudofs_data_t pfsd, int is_slave);
static void pseudofs_usage(int code);
static int timetosecs(char *str);

void
clrpfs(struct hammer_ioc_pseudofs_rw *pfs, hammer_pseudofs_data_t pfsd,
	int pfs_id)
{
	bzero(pfs, sizeof(*pfs));

	if (pfsd)
		pfs->ondisk = pfsd;
	else
		pfs->ondisk = malloc(sizeof(*pfs->ondisk));
	bzero(pfs->ondisk, sizeof(*pfs->ondisk));

	pfs->pfs_id = pfs_id;
	pfs->bytes = sizeof(*pfs->ondisk);
	pfs->version = HAMMER_IOC_PSEUDOFS_VERSION;
}

/*
 * If path is a symlink, return strdup'd path.
 * If it's a directory via symlink, strip trailing /
 * from strdup'd path and return the symlink.
 */
static char*
getlink(const char *path)
{
	int i;
	char *linkpath;
	struct stat st;

	if (lstat(path, &st))
		return(NULL);
	linkpath = strdup(path);

	if (S_ISDIR(st.st_mode)) {
		i = strlen(linkpath) - 1;
		while (i > 0 && linkpath[i] == '/')
			linkpath[i--] = 0;
		lstat(linkpath, &st);
	}
	if (S_ISLNK(st.st_mode))
		return(linkpath);

	free(linkpath);
	return(NULL);
}

/*
 * Calculate the PFS id given a path to a file/directory or
 * a @@%llx:%d softlink.
 */
int
getpfs(struct hammer_ioc_pseudofs_rw *pfs, const char *path)
{
	int fd;

	clrpfs(pfs, NULL, -1);

	/*
	 * Extract the PFS id.
	 * dirname(path) is supposed to be a directory in root PFS.
	 */
	if (scanpfsid(pfs, path) == 0)
		path = dirname(path); /* strips trailing / first if any */

	/*
	 * Open the path regardless of scanpfsid() result, since some
	 * commands can take a regular file/directory (e.g. pfs-status).
	 */
	fd = open(path, O_RDONLY);
	if (fd < 0)
		err(1, "Failed to open %s", path);

	/*
	 * If pfs.pfs_id has been set to non -1, the file descriptor fd
	 * could be any fd of HAMMER inodes since HAMMERIOC_GET_PSEUDOFS
	 * doesn't depend on inode attributes if it's set to a valid id.
	 */
	if (ioctl(fd, HAMMERIOC_GET_PSEUDOFS, pfs) < 0)
		err(1, "Cannot access %s", path);

	return(fd);
}

/*
 * Extract the PFS id from path.
 */
static int
scanpfsid(struct hammer_ioc_pseudofs_rw *pfs, const char *path)
{
	char *linkpath;
	char buf[64];
	uintmax_t dummy_tid;
	struct stat st;

	if (stat(path, &st))
		; /* possibly slave PFS */
	else if (S_ISDIR(st.st_mode))
		; /* possibly master or slave PFS */
	else
		return(-1);  /* neither */

	linkpath = getlink(path);
	if (linkpath) {
		/*
		 * Read the symlink assuming it's a link to PFS.
		 */
		bzero(buf, sizeof(buf));
		if (readlink(linkpath, buf, sizeof(buf) - 1) < 0) {
			free(linkpath);
			return(-1);
		}
		free(linkpath);
		path = buf;
	}

	/*
	 * The symlink created by pfs-master|slave is just a symlink.
	 * One could happen to remove a symlink and relink PFS as
	 * # ln -s ./@@-1:00001 ./link
	 * which results PFS having something extra before @@.
	 * One could also directly use the PFS and results the same.
	 * Get rid of it before we extract the PFS id.
	 */
	if (strchr(path, '/')) {
		path = basename(path); /* strips trailing / first if any */
		if (path == NULL)
			err(1, "basename");
	}

	/*
	 * Test and extract the PFS id from the link.
	 * "@@%jx:%d" covers both "@@-1:%05d" format for master PFS
	 * and "@@0x%016jx:%05d" format for slave PFS.
	 */
	if (sscanf(path, "@@%jx:%d", &dummy_tid, &pfs->pfs_id) == 2) {
		assert(pfs->pfs_id > 0);
		return(0);
	}

	return(-1);
}

void
relpfs(int fd, struct hammer_ioc_pseudofs_rw *pfs)
{
	if (fd >= 0)
		close(fd);
	if (pfs->ondisk) {
		free(pfs->ondisk);
		pfs->ondisk = NULL;
	}
}

static void
print_pfs_status(char *path)
{
	struct hammer_ioc_pseudofs_rw pfs;
	int fd;

	fd = getpfs(&pfs, path);
	printf("%s\t", path);
	if (fd < 0 || ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) < 0) {
		printf("Invalid PFS path %s\n", path);
	} else {
		printf("PFS#%d {\n", pfs.pfs_id);
		dump_pfsd(pfs.ondisk, fd);
		printf("}\n");
	}
	if (fd >= 0)
		close(fd);
	if (pfs.ondisk)
		free(pfs.ondisk);
	relpfs(fd, &pfs);
}

void
hammer_cmd_pseudofs_status(char **av, int ac)
{
	int i;

	if (ac == 0) {
		char buf[2] = "."; /* can't be readonly string */
		print_pfs_status(buf);
		return;
	}

	for (i = 0; i < ac; ++i)
		print_pfs_status(av[i]);
}

void
hammer_cmd_pseudofs_create(char **av, int ac, int is_slave)
{
	struct hammer_ioc_pseudofs_rw pfs;
	struct hammer_pseudofs_data pfsd;
	struct stat st;
	const char *path;
	char *dirpath;
	char *linkpath;
	int pfs_id;
	int fd;

	if (ac == 0)
		pseudofs_usage(1);
	path = av[0];
	if (lstat(path, &st) == 0)
		errx(1, "Cannot create %s, file exists!", path);
	else if (path[strlen(path) - 1] == '/')
		errx(1, "Invalid PFS path %s with trailing /", path);

	/*
	 * Figure out the directory prefix, taking care of degenerate
	 * cases.
	 */
	dirpath = dirname(path);
	fd = open(dirpath, O_RDONLY);
	if (fd < 0)
		err(1, "Cannot open directory %s", dirpath);

	/*
	 * Avoid foot-shooting.  Don't let the user create a PFS
	 * softlink via a PFS.  PFS softlinks may only be accessed
	 * via the master filesystem.  Checking it here ensures
	 * other PFS commands access PFS under the master filesystem.
	 */
	clrpfs(&pfs, &pfsd, -1);

	ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs);
	if (pfs.pfs_id != HAMMER_ROOT_PFSID) {
		fprintf(stderr,
			"You are attempting to access a PFS softlink "
			"from a PFS.  It may not represent the PFS\n"
			"on the main filesystem mount that you "
			"expect!  You may only access PFS softlinks\n"
			"via the main filesystem mount!\n");
		exit(1);
	}

	for (pfs_id = 0; pfs_id < HAMMER_MAX_PFS; ++pfs_id) {
		clrpfs(&pfs, &pfsd, pfs_id);
		if (ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) < 0) {
			if (errno != ENOENT)
				err(1, "Cannot create %s", path);
			break;
		}
	}
	if (pfs_id == HAMMER_MAX_PFS)
		errx(1, "Cannot create %s, all PFSs in use", path);
	else if (pfs_id == HAMMER_ROOT_PFSID)
		errx(1, "Fatal error: PFS#%d must exist", HAMMER_ROOT_PFSID);

	/*
	 * Create the new PFS
	 */
	printf("Creating PFS#%d\t", pfs_id);
	clrpfs(&pfs, &pfsd, pfs_id);
	init_pfsd(&pfsd, is_slave);

	if (ioctl(fd, HAMMERIOC_SET_PSEUDOFS, &pfs) < 0) {
		printf("failed: %s\n", strerror(errno));
	} else {
		/* special symlink, must be exactly 10 characters */
		asprintf(&linkpath, "@@PFS%05d", pfs_id);
		if (symlink(linkpath, path) < 0) {
			printf("failed: cannot create symlink: %s\n",
				strerror(errno));
		} else {
			printf("succeeded!\n");
			hammer_cmd_pseudofs_update(av, ac);
		}
	}
	free(dirpath);
	close(fd);
}

void
hammer_cmd_pseudofs_destroy(char **av, int ac)
{
	struct hammer_ioc_pseudofs_rw pfs;
	char *linkpath;
	int fd;
	int i;

	if (ac == 0)
		pseudofs_usage(1);
	fd = getpfs(&pfs, av[0]);

	if (pfs.pfs_id == HAMMER_ROOT_PFSID)
		errx(1, "You cannot destroy PFS#%d", HAMMER_ROOT_PFSID);

	printf("You have requested that PFS#%d (%s) be destroyed\n",
		pfs.pfs_id, pfs.ondisk->label);
	printf("This will irrevocably destroy all data on this PFS!!!!!\n");
	printf("Do you really want to do this? [y/n] ");
	fflush(stdout);
	if (getyn() == 0)
		errx(1, "No action taken on PFS#%d", pfs.pfs_id);

	if (hammer_is_pfs_master(pfs.ondisk)) {
		printf("This PFS is currently setup as a MASTER!\n");
		printf("Are you absolutely sure you want to destroy it? [y/n] ");
		fflush(stdout);
		if (getyn() == 0)
			errx(1, "No action taken on PFS#%d", pfs.pfs_id);
	}

	printf("Destroying PFS#%d (%s)", pfs.pfs_id, pfs.ondisk->label);
	if (DebugOpt) {
		printf("\n");
	} else {
		printf(" in");
		for (i = 5; i; --i) {
			printf(" %d", i);
			fflush(stdout);
			sleep(1);
		}
		printf(".. starting destruction pass\n");
	}

	/*
	 * Remove the softlink on success.
	 */
	if (ioctl(fd, HAMMERIOC_RMR_PSEUDOFS, &pfs) == 0) {
		printf("pfs-destroy of PFS#%d succeeded!\n", pfs.pfs_id);
		linkpath = getlink(av[0]);
		if (linkpath) {
			if (remove(linkpath) < 0)
				err(1, "Unable to remove softlink %s", linkpath);
			free(linkpath);
		}
	} else {
		printf("pfs-destroy of PFS#%d failed: %s\n",
			pfs.pfs_id, strerror(errno));
	}
	relpfs(fd, &pfs);
}

void
hammer_cmd_pseudofs_upgrade(char **av, int ac)
{
	struct hammer_ioc_pseudofs_rw pfs;
	int fd;

	if (ac == 0)
		pseudofs_usage(1);
	fd = getpfs(&pfs, av[0]);

	if (pfs.pfs_id == HAMMER_ROOT_PFSID) {
		errx(1, "You cannot upgrade PFS#%d"
			" (It should already be a master)",
			HAMMER_ROOT_PFSID);
	} else if (hammer_is_pfs_master(pfs.ondisk)) {
		errx(1, "It is already a master");
	}

	if (ioctl(fd, HAMMERIOC_UPG_PSEUDOFS, &pfs) == 0) {
		printf("pfs-upgrade of PFS#%d (%s) succeeded\n",
			pfs.pfs_id, pfs.ondisk->label);
	} else {
		err(1, "pfs-upgrade of PFS#%d (%s) failed",
			pfs.pfs_id, pfs.ondisk->label);
	}
	relpfs(fd, &pfs);
}

void
hammer_cmd_pseudofs_downgrade(char **av, int ac)
{
	struct hammer_ioc_pseudofs_rw pfs;
	int fd;

	if (ac == 0)
		pseudofs_usage(1);
	fd = getpfs(&pfs, av[0]);

	if (pfs.pfs_id == HAMMER_ROOT_PFSID)
		errx(1, "You cannot downgrade PFS#%d", HAMMER_ROOT_PFSID);
	else if (hammer_is_pfs_slave(pfs.ondisk))
		errx(1, "It is already a slave");

	if (ioctl(fd, HAMMERIOC_DGD_PSEUDOFS, &pfs) == 0) {
		printf("pfs-downgrade of PFS#%d (%s) succeeded\n",
			pfs.pfs_id, pfs.ondisk->label);
	} else {
		err(1, "pfs-downgrade of PFS#%d (%s) failed",
			pfs.pfs_id, pfs.ondisk->label);
	}
	relpfs(fd, &pfs);
}

void
hammer_cmd_pseudofs_update(char **av, int ac)
{
	struct hammer_ioc_pseudofs_rw pfs;
	int fd;

	if (ac == 0)
		pseudofs_usage(1);
	fd = getpfs(&pfs, av[0]);

	printf("%s\n", av[0]);
	fflush(stdout);

	if (ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) == 0) {
		parse_pfsd_options(av + 1, ac - 1, pfs.ondisk);
		if (hammer_is_pfs_slave(pfs.ondisk) &&
		    pfs.pfs_id == HAMMER_ROOT_PFSID) {
			errx(1, "The real mount point cannot be made a PFS "
			       "slave, only PFS sub-directories can be made "
			       "slaves");
		}
		pfs.bytes = sizeof(*pfs.ondisk);
		if (ioctl(fd, HAMMERIOC_SET_PSEUDOFS, &pfs) == 0) {
			if (ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) == 0) {
				dump_pfsd(pfs.ondisk, fd);
			} else {
				err(1, "Unable to retrieve PFS configuration "
					"after successful update");
			}
		} else {
			err(1, "Unable to adjust PFS configuration");
		}
	}
	relpfs(fd, &pfs);
}

static void
init_pfsd(hammer_pseudofs_data_t pfsd, int is_slave)
{
	uint32_t status;

	bzero(pfsd, sizeof(*pfsd));
	pfsd->sync_beg_tid = 1;
	pfsd->sync_end_tid = 1;
	pfsd->sync_beg_ts = 0;
	pfsd->sync_end_ts = 0;
	uuid_create(&pfsd->shared_uuid, &status);
	uuid_create(&pfsd->unique_uuid, &status);
	if (is_slave)
		pfsd->mirror_flags |= HAMMER_PFSD_SLAVE;
}

void
dump_pfsd(hammer_pseudofs_data_t pfsd, int fd)
{
	struct hammer_ioc_version	version;
	uint32_t status;
	char *str = NULL;

	printf("    sync-beg-tid=0x%016jx\n", (uintmax_t)pfsd->sync_beg_tid);
	printf("    sync-end-tid=0x%016jx\n", (uintmax_t)pfsd->sync_end_tid);
	uuid_to_string(&pfsd->shared_uuid, &str, &status);
	printf("    shared-uuid=%s\n", str);
	free(str);
	uuid_to_string(&pfsd->unique_uuid, &str, &status);
	printf("    unique-uuid=%s\n", str);
	free(str);
	printf("    label=\"%s\"\n", pfsd->label);
	if (pfsd->snapshots[0])
		printf("    snapshots=\"%s\"\n", pfsd->snapshots);
	if (pfsd->prune_min < (60 * 60 * 24)) {
		printf("    prune-min=%02d:%02d:%02d\n",
			pfsd->prune_min / 60 / 60 % 24,
			pfsd->prune_min / 60 % 60,
			pfsd->prune_min % 60);
	} else if (pfsd->prune_min % (60 * 60 * 24)) {
		printf("    prune-min=%dd/%02d:%02d:%02d\n",
			pfsd->prune_min / 60 / 60 / 24,
			pfsd->prune_min / 60 / 60 % 24,
			pfsd->prune_min / 60 % 60,
			pfsd->prune_min % 60);
	} else {
		printf("    prune-min=%dd\n", pfsd->prune_min / 60 / 60 / 24);
	}

	if (hammer_is_pfs_slave(pfsd))
		printf("    operating as a SLAVE\n");
	else
		printf("    operating as a MASTER\n");

	/*
	 * Snapshots directory cannot be shown when there is no fd since
	 * hammer version can't be retrieved. mirror-dump passes -1 because
	 * its input came from mirror-read output thus no path is available
	 * to open(2).
	 */
	if (fd >= 0 && pfsd->snapshots[0] == 0) {
		bzero(&version, sizeof(version));
		if (ioctl(fd, HAMMERIOC_GET_VERSION, &version) < 0)
			return;
		HammerVersion = version.cur_version;
		if (version.cur_version < 3) {
			if (hammer_is_pfs_slave(pfsd)) {
				printf("    snapshots directory not set for "
				       "slave\n");
			} else {
				printf("    snapshots directory for master "
				       "defaults to <pfs>/snapshots\n");
			}
		} else {
			printf("    snapshots directory defaults to "
			       "/var/hammer/<pfs>\n");
		}
	}
}

static void
parse_pfsd_options(char **av, int ac, hammer_pseudofs_data_t pfsd)
{
	char *cmd;
	char *ptr;
	int len;
	uint32_t status;

	while (ac) {
		cmd = *av;
		if ((ptr = strchr(cmd, '=')) != NULL)
			*ptr++ = 0;

		/*
		 * Basic assignment value test
		 */
		if (ptr == NULL)
			errx(1, "option %s requires an assignment", cmd);

		status = uuid_s_ok;
		if (strcmp(cmd, "sync-beg-tid") == 0) {
			pfsd->sync_beg_tid = strtoull(ptr, NULL, 16);
		} else if (strcmp(cmd, "sync-end-tid") == 0) {
			pfsd->sync_end_tid = strtoull(ptr, NULL, 16);
		} else if (strcmp(cmd, "shared-uuid") == 0) {
			uuid_from_string(ptr, &pfsd->shared_uuid, &status);
		} else if (strcmp(cmd, "unique-uuid") == 0) {
			uuid_from_string(ptr, &pfsd->unique_uuid, &status);
		} else if (strcmp(cmd, "label") == 0) {
			len = strlen(ptr);
			if (ptr[0] == '"' && ptr[len-1] == '"') {
				ptr[len-1] = 0;
				++ptr;
			} else if (ptr[0] == '"') {
				errx(1, "option %s: malformed string", cmd);
			}
			snprintf(pfsd->label, sizeof(pfsd->label), "%s", ptr);
		} else if (strcmp(cmd, "snapshots") == 0) {
			len = strlen(ptr);
			if (ptr[0] != '/') {
				fprintf(stderr,
					"option %s: '%s' must be an "
					"absolute path\n", cmd, ptr);
				if (ptr[0] == 0) {
					fprintf(stderr,
						"use 'snapshots-clear' "
						"to unset snapshots dir\n");
				}
				exit(1);
			}
			if (len >= (int)sizeof(pfsd->snapshots)) {
				errx(1, "option %s: path too long, %d "
					"character limit", cmd, len);
			}
			snprintf(pfsd->snapshots, sizeof(pfsd->snapshots),
				 "%s", ptr);
		} else if (strcmp(cmd, "snapshots-clear") == 0) {
			pfsd->snapshots[0] = 0;
		} else if (strcmp(cmd, "prune-min") == 0) {
			pfsd->prune_min = timetosecs(ptr);
			if (pfsd->prune_min < 0) {
				errx(1, "option %s: illegal time spec, "
					"use Nd or [Nd/]hh[:mm[:ss]]", ptr);
			}
		} else {
			errx(1, "invalid option: %s", cmd);
		}
		if (status != uuid_s_ok)
			errx(1, "option %s: error parsing uuid %s", cmd, ptr);
		--ac;
		++av;
	}
}

static
void
pseudofs_usage(int code)
{
	fprintf(stderr,
		"hammer pfs-status <dirpath> ...\n"
		"hammer pfs-master <dirpath> [options]\n"
		"hammer pfs-slave <dirpath> [options]\n"
		"hammer pfs-update <dirpath> [options]\n"
		"hammer pfs-upgrade <dirpath>\n"
		"hammer pfs-downgrade <dirpath>\n"
		"hammer pfs-destroy <dirpath>\n"
		"\n"
		"options:\n"
		"    sync-beg-tid=0x16llx\n"
		"    sync-end-tid=0x16llx\n"
		"    shared-uuid=0x16llx\n"
		"    unique-uuid=0x16llx\n"
		"    label=\"string\"\n"
		"    snapshots=\"/path\"\n"
		"    snapshots-clear\n"
		"    prune-min=Nd\n"
		"    prune-min=[Nd/]hh[:mm[:ss]]\n"
	);
	exit(code);
}

/*
 * Convert time in the form [Nd/]hh[:mm[:ss]] to seconds.
 *
 * Return -1 if a parse error occurs.
 * Return 0x7FFFFFFF if the time exceeds the maximum allowed.
 */
static
int
timetosecs(char *str)
{
	int days = 0;
	int hrs = 0;
	int mins = 0;
	int secs = 0;
	int n;
	long long v;
	char *ptr;

	n = strtol(str, &ptr, 10);
	if (n < 0)
		return(-1);
	if (*ptr == 'd') {
		days = n;
		++ptr;
		if (*ptr == '/')
		    n = strtol(ptr + 1, &ptr, 10);
		else
		    n = 0;
	}
	if (n < 0)
		return(-1);
	hrs = n;
	if (*ptr == ':') {
		n = strtol(ptr + 1, &ptr, 10);
		if (n < 0)
			return(-1);
		mins = n;
		if (*ptr == ':') {
			n = strtol(ptr + 1, &ptr, 10);
			if (n < 0)
				return(-1);
			secs = n;
		}
	}
	if (*ptr)
		return(-1);
	v = days * 24 * 60 * 60 + hrs *  60 * 60 + mins * 60 + secs;
	if (v > 0x7FFFFFFF)
		v = 0x7FFFFFFF;
	return((int)v);
}

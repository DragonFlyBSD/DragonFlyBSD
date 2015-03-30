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

#include "hammer.h"

static void parse_pfsd_options(char **av, int ac, hammer_pseudofs_data_t pfsd);
static void init_pfsd(hammer_pseudofs_data_t pfsd, int is_slave);
static void pseudofs_usage(int code);
static char *strtrl(char **path, int len);
static int getyn(void);
static int timetosecs(char *str);

/*
 * Return a directory that contains path.
 * If '/' is not found in the path then '.' is returned.
 * A caller need to free the returned pointer.
 */
static char*
getdir(const char *path)
{
	char *dirpath;

	dirpath = strdup(path);
	if (strrchr(dirpath, '/')) {
		*strrchr(dirpath, '/') = 0;
		if (strlen(dirpath) == 0) {
			free(dirpath);
			dirpath = strdup("/");
		}
	} else {
		free(dirpath);
		dirpath = strdup(".");
	}

	return(dirpath);
}

/*
 * Calculate the pfs_id given a path to a directory or a @@PFS or @@%llx:%d
 * softlink.
 */
int
getpfs(struct hammer_ioc_pseudofs_rw *pfs, char *path)
{
	uintmax_t dummy_tid;
	struct stat st;
	char *dirpath = NULL;
	char buf[64];
	size_t len;
	int fd;
	int n;

	bzero(pfs, sizeof(*pfs));
	pfs->ondisk = malloc(sizeof(*pfs->ondisk));
	bzero(pfs->ondisk, sizeof(*pfs->ondisk));
	pfs->bytes = sizeof(*pfs->ondisk);

	/*
	 * Trailing '/' must be removed so that upon pfs-destroy
	 * the symlink can be deleted without problems.
	 * Root directory (/) must be excluded from this.
	 */
	len = strnlen(path, MAXPATHLEN);
	if (len > 1) {
		if (strtrl(&path, len) == NULL)
			errx(1, "Unexpected NULL path");
	}

	if (lstat(path, &st) == 0 && S_ISLNK(st.st_mode)) {
		/*
		 * Extract the PFS from the link.  HAMMER will automatically
		 * convert @@PFS%05d links so if actually see one in that
		 * form the target PFS may not exist or may be corrupt.  But
		 * we can extract the PFS id anyway.
		 */
		dirpath = getdir(path);
		n = readlink(path, buf, sizeof(buf) - 1);
		if (n < 0)
			n = 0;
		buf[n] = 0;
		if (sscanf(buf, "@@PFS%d", &pfs->pfs_id) == 1) {
			fd = open(dirpath, O_RDONLY);
			goto done;
		}
		if (sscanf(buf, "@@%jx:%d", &dummy_tid, &pfs->pfs_id) == 2) {
			fd = open(dirpath, O_RDONLY);
			goto done;
		}
	}

	/*
	 * Try to open the path and request the pfs_id that way.
	 */
	fd = open(path, O_RDONLY);
	if (fd >= 0) {
		pfs->pfs_id = -1;
		ioctl(fd, HAMMERIOC_GET_PSEUDOFS, pfs);
		if (pfs->pfs_id == -1) {
			close(fd);
			fd = -1;
		}
	}

	/*
	 * Cleanup
	 */
done:
	if (fd < 0) {
		fprintf(stderr, "Cannot access PFS %s: %s\n",
			path, strerror(errno));
		exit(1);
	}
	if (ioctl(fd, HAMMERIOC_GET_PSEUDOFS, pfs) < 0) {
		fprintf(stderr, "Cannot access PFS %s: %s\n",
			path, strerror(errno));
		exit(1);
	}
	free(dirpath);
	return(fd);
}

void
relpfs(int fd, struct hammer_ioc_pseudofs_rw *pfs)
{
	close(fd);
	if (pfs->ondisk) {
		free(pfs->ondisk);
		pfs->ondisk = NULL;
	}
}

void
hammer_cmd_pseudofs_status(char **av, int ac)
{
	struct hammer_ioc_pseudofs_rw pfs;
	int i;
	int fd;

	if (ac == 0)
		pseudofs_usage(1);

	for (i = 0; i < ac; ++i) {
		fd = getpfs(&pfs, av[i]);
		printf("%s\t", av[i]);
		if (fd < 0 || ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) < 0) {
			printf("Invalid PFS path %s\n", av[i]);
		} else {
			printf("PFS #%d {\n", pfs.pfs_id);
			dump_pfsd(pfs.ondisk, fd);
			printf("}\n");
		}
		if (fd >= 0)
			close(fd);
		if (pfs.ondisk)
			free(pfs.ondisk);
	}
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
	int error;

	if (ac == 0)
		pseudofs_usage(1);
	path = av[0];
	if (lstat(path, &st) == 0) {
		fprintf(stderr, "Cannot create %s, file exists!\n", path);
		exit(1);
	}

	/*
	 * Figure out the directory prefix, taking care of degenerate
	 * cases.
	 */
	dirpath = getdir(path);
	fd = open(dirpath, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Cannot open directory %s\n", dirpath);
		exit(1);
	}

	/*
	 * Avoid foot-shooting.  Don't let the user create a PFS
	 * softlink via a PFS.  PFS softlinks may only be accessed
	 * via the master filesystem.  Checking it here ensures
	 * other PFS commands access PFS under the master filesystem.
	 */
	bzero(&pfs, sizeof(pfs));
	bzero(&pfsd, sizeof(pfsd));
	pfs.pfs_id = -1;
	pfs.ondisk = &pfsd;
	pfs.bytes = sizeof(pfsd);

	ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs);
	if (pfs.pfs_id != 0) {
		fprintf(stderr,
			"You are attempting to access a PFS softlink "
			"from a PFS.  It may not represent the PFS\n"
			"on the main filesystem mount that you "
			"expect!  You may only access PFS softlinks\n"
			"via the main filesystem mount!\n");
		exit(1);
	}

	error = 0;
	for (pfs_id = 0; pfs_id < HAMMER_MAX_PFS; ++pfs_id) {
		bzero(&pfs, sizeof(pfs));
		bzero(&pfsd, sizeof(pfsd));
		pfs.pfs_id = pfs_id;
		pfs.ondisk = &pfsd;
		pfs.bytes = sizeof(pfsd);
		pfs.version = HAMMER_IOC_PSEUDOFS_VERSION;
		if (ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) < 0) {
			error = errno;
			break;
		}
	}
	if (pfs_id == HAMMER_MAX_PFS) {
		fprintf(stderr, "Cannot create %s, all PFSs in use\n", path);
		exit(1);
	}
	if (error != ENOENT) {
		fprintf(stderr, "Cannot create %s, got %s during scan\n",
			path, strerror(error));
		exit(1);
	}

	/*
	 * Create the new PFS
	 */
	printf("Creating PFS #%d\t", pfs_id);
	bzero(&pfsd, sizeof(pfsd));
	init_pfsd(&pfsd, is_slave);
	pfs.pfs_id = pfs_id;
	pfs.ondisk = &pfsd;
	pfs.bytes = sizeof(pfsd);
	pfs.version = HAMMER_IOC_PSEUDOFS_VERSION;

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
	struct stat st;
	int fd;
	int i;

	if (ac == 0)
		pseudofs_usage(1);
	bzero(&pfs, sizeof(pfs));
	fd = getpfs(&pfs, av[0]);

	if (pfs.pfs_id == 0) {
		fprintf(stderr, "You cannot destroy PFS#0\n");
		exit(1);
	}
	printf("You have requested that PFS#%d (%s) be destroyed\n",
		pfs.pfs_id, pfs.ondisk->label);
	printf("This will irrevocably destroy all data on this PFS!!!!!\n");
	printf("Do you really want to do this? ");
	fflush(stdout);
	if (getyn() == 0) {
		fprintf(stderr, "No action taken on PFS#%d\n", pfs.pfs_id);
		exit(1);
	}

	if ((pfs.ondisk->mirror_flags & HAMMER_PFSD_SLAVE) == 0) {
		printf("This PFS is currently setup as a MASTER!\n");
		printf("Are you absolutely sure you want to destroy it? ");
		fflush(stdout);
		if (getyn() == 0) {
			fprintf(stderr, "No action taken on PFS#%d\n",
				pfs.pfs_id);
			exit(1);
		}
	}

	printf("Destroying PFS #%d (%s) in ", pfs.pfs_id, pfs.ondisk->label);
	for (i = 5; i; --i) {
		printf(" %d", i);
		fflush(stdout);
		sleep(1);
	}
	printf(".. starting destruction pass\n");
	fflush(stdout);

	/*
	 * Set the sync_beg_tid and sync_end_tid's to 1, once we start the
	 * RMR the PFS is basically destroyed even if someone ^C's it.
	 */
	pfs.ondisk->mirror_flags |= HAMMER_PFSD_SLAVE;
	pfs.ondisk->reserved01 = -1;
	pfs.ondisk->sync_beg_tid = 1;
	pfs.ondisk->sync_end_tid = 1;

	if (ioctl(fd, HAMMERIOC_SET_PSEUDOFS, &pfs) < 0) {
		fprintf(stderr, "Unable to update the PFS configuration: %s\n",
			strerror(errno));
		exit(1);
	}

	/*
	 * Ok, do it.  Remove the softlink on success.
	 */
	if (ioctl(fd, HAMMERIOC_RMR_PSEUDOFS, &pfs) == 0) {
		printf("pfs-destroy of PFS#%d succeeded!\n", pfs.pfs_id);
		if (lstat(av[0], &st) == 0 && S_ISLNK(st.st_mode)) {
			if (remove(av[0]) < 0) {
				fprintf(stderr, "Unable to remove softlink: %s "
					"(but the PFS has been destroyed)\n",
					av[0]);
				/* exit status 0 anyway */
			}
		}
	} else {
		printf("pfs-destroy of PFS#%d failed: %s\n",
			pfs.pfs_id, strerror(errno));
	}
}

void
hammer_cmd_pseudofs_upgrade(char **av, int ac)
{
	struct hammer_ioc_pseudofs_rw pfs;
	int fd;

	if (ac == 0)
		pseudofs_usage(1);
	bzero(&pfs, sizeof(pfs));
	fd = getpfs(&pfs, av[0]);

	if (pfs.pfs_id == 0) {
		fprintf(stderr, "You cannot upgrade PFS#0"
				" (It should already be a master)\n");
		exit(1);
	}
	if (ioctl(fd, HAMMERIOC_UPG_PSEUDOFS, &pfs) == 0) {
		printf("pfs-upgrade of PFS#%d (%s) succeeded\n",
			pfs.pfs_id, pfs.ondisk->label);
	} else {
		fprintf(stderr, "pfs-upgrade of PFS#%d (%s) failed: %s\n",
			pfs.pfs_id, pfs.ondisk->label, strerror(errno));
	}
}

void
hammer_cmd_pseudofs_downgrade(char **av, int ac)
{
	struct hammer_ioc_pseudofs_rw pfs;
	int fd;

	if (ac == 0)
		pseudofs_usage(1);
	bzero(&pfs, sizeof(pfs));
	fd = getpfs(&pfs, av[0]);

	if (pfs.pfs_id == 0) {
		fprintf(stderr, "You cannot downgrade PFS#0\n");
		exit(1);
	}

	if (ioctl(fd, HAMMERIOC_DGD_PSEUDOFS, &pfs) == 0) {
		printf("pfs-downgrade of PFS#%d (%s) succeeded\n",
			pfs.pfs_id, pfs.ondisk->label);
	} else {
		fprintf(stderr, "pfs-downgrade of PFS#%d (%s) failed: %s\n",
			pfs.pfs_id, pfs.ondisk->label, strerror(errno));
	}
}

void
hammer_cmd_pseudofs_update(char **av, int ac)
{
	struct hammer_ioc_pseudofs_rw pfs;
	int fd;

	if (ac == 0)
		pseudofs_usage(1);
	bzero(&pfs, sizeof(pfs));
	fd = getpfs(&pfs, av[0]);

	printf("%s\n", av[0]);
	fflush(stdout);

	if (ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) == 0) {
		parse_pfsd_options(av + 1, ac - 1, pfs.ondisk);
		if ((pfs.ondisk->mirror_flags & HAMMER_PFSD_SLAVE) &&
		    pfs.pfs_id == 0) {
			printf("The real mount point cannot be made a PFS "
			       "slave, only PFS sub-directories can be made "
			       "slaves\n");
			exit(1);
		}
		pfs.bytes = sizeof(*pfs.ondisk);
		if (ioctl(fd, HAMMERIOC_SET_PSEUDOFS, &pfs) == 0) {
			if (ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) == 0) {
				dump_pfsd(pfs.ondisk, fd);
			} else {
				printf("Unable to retrieve pfs configuration "
					"after successful update: %s\n",
					strerror(errno));
				exit(1);
			}
		} else {
			printf("Unable to adjust pfs configuration: %s\n",
				strerror(errno));
			exit(1);
		}
	}
}

static void
init_pfsd(hammer_pseudofs_data_t pfsd, int is_slave)
{
	uint32_t status;

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
	u_int32_t status;
	char *str = NULL;

	printf("    sync-beg-tid=0x%016jx\n", (uintmax_t)pfsd->sync_beg_tid);
	printf("    sync-end-tid=0x%016jx\n", (uintmax_t)pfsd->sync_end_tid);
	uuid_to_string(&pfsd->shared_uuid, &str, &status);
	printf("    shared-uuid=%s\n", str);
	free(str);
	uuid_to_string(&pfsd->unique_uuid, &str, &status);
	printf("    unique-uuid=%s\n", str);
	free(str);
	if (pfsd->mirror_flags & HAMMER_PFSD_SLAVE) {
		printf("    slave\n");
	}
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

	if (pfsd->mirror_flags & HAMMER_PFSD_SLAVE) {
		printf("    operating as a SLAVE\n");
	} else {
		printf("    operating as a MASTER\n");
	}

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
		if (version.cur_version < 3) {
			if (pfsd->mirror_flags & HAMMER_PFSD_SLAVE) {
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
		if (ptr == NULL) {
			fprintf(stderr,
				"option %s requires an assignment\n",
				cmd);
			exit(1);
		}

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
				fprintf(stderr,
					"option %s: malformed string\n",
					cmd);
				exit(1);
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
				fprintf(stderr,
					"option %s: path too long, %d "
					"character limit\n", cmd, len);
				exit(1);
			}
			snprintf(pfsd->snapshots, sizeof(pfsd->snapshots),
				 "%s", ptr);
		} else if (strcmp(cmd, "snapshots-clear") == 0) {
			pfsd->snapshots[0] = 0;
		} else if (strcmp(cmd, "prune-min") == 0) {
			pfsd->prune_min = timetosecs(ptr);
			if (pfsd->prune_min < 0) {
				fprintf(stderr,
					"option %s: illegal time spec, "
					"use Nd or [Nd/]hh[:mm[:ss]]\n", ptr);
				exit(1);
			}
		} else {
			fprintf(stderr, "invalid option: %s\n", cmd);
			exit(1);
		}
		if (status != uuid_s_ok) {
			fprintf(stderr, "option %s: error parsing uuid %s\n",
				cmd, ptr);
			exit(1);
		}
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

static
int
getyn(void)
{
	char buf[256];
	int len;

	if (fgets(buf, sizeof(buf), stdin) == NULL)
		return(0);
	len = strlen(buf);
	while (len && (buf[len-1] == '\n' || buf[len-1] == '\r'))
		--len;
	buf[len] = 0;
	if (strcmp(buf, "y") == 0 ||
	    strcmp(buf, "yes") == 0 ||
	    strcmp(buf, "Y") == 0 ||
	    strcmp(buf, "YES") == 0) {
		return(1);
	}
	return(0);
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

static
char *
strtrl(char **path, int len)
{
	char *s, *p;

	s = *path;
	if (s == NULL)
		return NULL;

	p = s + len;
	/* Attempt to remove all trailing slashes */
	while (p-- > s && *p == '/')
		*p = '\0';

	return p;
}

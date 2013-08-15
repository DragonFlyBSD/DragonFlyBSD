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
 * $DragonFly: src/sbin/hammer/cmd_cleanup.c,v 1.6 2008/10/07 22:28:41 thomas Exp $
 */
/*
 * Clean up specific HAMMER filesystems or all HAMMER filesystems.
 *
 * If no filesystems are specified any HAMMER- or null-mounted hammer PFS's
 * are cleaned.
 *
 * Each HAMMER filesystem may contain a configuration file.  If no
 * configuration file is present one will be created with the following
 * defaults:
 *
 *	snapshots 1d 60d	(0d 0d for /tmp, /var/tmp, /usr/obj)
 *	prune     1d 5m
 *	rebalance 1d 5m
 *	#dedup	  1d 5m		(not enabled by default)
 *	reblock   1d 5m
 *	recopy    30d 10m
 *
 * All hammer commands create and maintain cycle files in the snapshots
 * directory.
 *
 * For HAMMER version 2- the configuration file is a named 'config' in
 * the snapshots directory, which defaults to <pfs>/snapshots.
 * For HAMMER version 3+ the configuration file is saved in filesystem
 * meta-data. The snapshots directory defaults to /var/hammer/<pfs>
 * (/var/hammer/root for root mount).
 */

#include "hammer.h"

struct didpfs {
	struct didpfs *next;
	uuid_t		uuid;
};

static void do_cleanup(const char *path);
static void config_init(const char *path, struct hammer_ioc_config *config);
static void migrate_config(FILE *fp, struct hammer_ioc_config *config);
static void migrate_snapshots(int fd, const char *snapshots_path);
static void migrate_one_snapshot(int fd, const char *fpath,
			struct hammer_ioc_snapshot *snapshot);
static int strtosecs(char *ptr);
static const char *dividing_slash(const char *path);
static int check_period(const char *snapshots_path, const char *cmd, int arg1,
			time_t *savep);
static void save_period(const char *snapshots_path, const char *cmd,
			time_t savet);
static int check_softlinks(int fd, int new_config, const char *snapshots_path);
static void cleanup_softlinks(int fd, int new_config,
			const char *snapshots_path, int arg2, char *arg3);
static void delete_snapshots(int fd, struct hammer_ioc_snapshot *dsnapshot);
static int check_expired(const char *fpath, int arg2);

static int create_snapshot(const char *path, const char *snapshots_path);
static int cleanup_rebalance(const char *path, const char *snapshots_path,
			int arg1, int arg2);
static int cleanup_prune(const char *path, const char *snapshots_path,
			int arg1, int arg2, int snapshots_disabled);
static int cleanup_reblock(const char *path, const char *snapshots_path,
			int arg1, int arg2);
static int cleanup_recopy(const char *path, const char *snapshots_path,
			int arg1, int arg2);
static int cleanup_dedup(const char *path, const char *snapshots_path,
			int arg1, int arg2);

static void runcmd(int *resp, const char *ctl, ...) __printflike(2, 3);

/*
 * WARNING: Do not make the SNAPSHOTS_BASE "/var/snapshots" because
 * it will interfere with the older HAMMER VERS < 3 snapshots directory
 * for the /var PFS.
 */
#define SNAPSHOTS_BASE	"/var/hammer"	/* HAMMER VERS >= 3 */
#define WS	" \t\r\n"

struct didpfs *FirstPFS;

void
hammer_cmd_cleanup(char **av, int ac)
{
	char *fstype, *fs, *path;
	struct statfs *stfsbuf;
	int mntsize, i;

	tzset();
	if (ac == 0) {
		mntsize = getmntinfo(&stfsbuf, MNT_NOWAIT);
		if (mntsize > 0) {
			for (i=0; i < mntsize; i++) {
				/*
				 * We will cleanup in the case fstype is hammer.
				 * If we have null-mounted PFS, we check the
				 * mount source. If it looks like a PFS, we
				 * proceed to cleanup also.
				 */
				fstype = stfsbuf[i].f_fstypename;
				fs = stfsbuf[i].f_mntfromname;
				if ((strcmp(fstype, "hammer") == 0) ||
				    ((strcmp(fstype, "null") == 0) &&
				     (strstr(fs, "/@@0x") != NULL ||
				      strstr(fs, "/@@-1") != NULL))) {
					path = stfsbuf[i].f_mntonname;
					do_cleanup(path);
				}
			}
		}

	} else {
		while (ac) {
			do_cleanup(*av);
			--ac;
			++av;
		}
	}
}

static
void
do_cleanup(const char *path)
{
	struct hammer_ioc_pseudofs_rw pfs;
	struct hammer_ioc_config config;
	struct hammer_ioc_version version;
	union hammer_ioc_mrecord_any mrec_tmp;
	char *snapshots_path = NULL;
	char *config_path;
	struct stat st;
	char *cmd;
	char *ptr;
	int arg1;
	int arg2;
	char *arg3;
	time_t savet;
	char buf[256];
	char *cbase;
	char *cptr;
	FILE *fp = NULL;
	struct didpfs *didpfs;
	int snapshots_disabled = 0;
	int prune_warning = 0;
	int new_config = 0;
	int snapshots_from_pfs = 0;
	int fd;
	int r;
	int found_rebal = 0;

	bzero(&pfs, sizeof(pfs));
	bzero(&mrec_tmp, sizeof(mrec_tmp));
	pfs.ondisk = &mrec_tmp.pfs.pfsd;
	pfs.bytes = sizeof(mrec_tmp.pfs.pfsd);
	pfs.pfs_id = -1;

	printf("cleanup %-20s -", path);
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		printf(" unable to access directory: %s\n", strerror(errno));
		return;
	}
	if (ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) < 0) {
		printf(" not a HAMMER filesystem: %s\n", strerror(errno));
		close(fd);
		return;
	}
	if (pfs.version != HAMMER_IOC_PSEUDOFS_VERSION) {
		printf(" unrecognized HAMMER version\n");
		close(fd);
		return;
	}
	bzero(&version, sizeof(version));
	if (ioctl(fd, HAMMERIOC_GET_VERSION, &version) < 0) {
		printf(" HAMMER filesystem but couldn't retrieve version!\n");
		close(fd);
		return;
	}

	bzero(&config, sizeof(config));
	if (version.cur_version >= 3) {
		if (ioctl(fd, HAMMERIOC_GET_CONFIG, &config) == 0 &&
		    config.head.error == 0) {
			new_config = 1;
		}
	}

	/*
	 * Make sure we have not already handled this PFS.  Several nullfs
	 * mounts might alias the same PFS.
	 */
	for (didpfs = FirstPFS; didpfs; didpfs = didpfs->next) {
		if (bcmp(&didpfs->uuid, &mrec_tmp.pfs.pfsd.unique_uuid, sizeof(uuid_t)) == 0) {
			printf(" PFS #%d already handled\n", pfs.pfs_id);
			close(fd);
			return;
		}
	}
	didpfs = malloc(sizeof(*didpfs));
	didpfs->next = FirstPFS;
	FirstPFS = didpfs;
	didpfs->uuid = mrec_tmp.pfs.pfsd.unique_uuid;

	/*
	 * Calculate the old snapshots directory for HAMMER VERSION < 3
	 *
	 * If the directory is explicitly specified in the PFS config
	 * we flag it and will not migrate it later.
	 */
	if (mrec_tmp.pfs.pfsd.snapshots[0] == '/') {
		asprintf(&snapshots_path, "%s", mrec_tmp.pfs.pfsd.snapshots);
		snapshots_from_pfs = 1;
	} else if (mrec_tmp.pfs.pfsd.snapshots[0]) {
		printf(" WARNING: pfs-slave's snapshots dir is not absolute\n");
		close(fd);
		return;
	} else if (mrec_tmp.pfs.pfsd.mirror_flags & HAMMER_PFSD_SLAVE) {
		if (version.cur_version < 3) {
			printf(" WARNING: must configure snapshot dir for PFS slave\n");
			printf("\tWe suggest <fs>/var/slaves/<name> where "
			       "<fs> is the base HAMMER fs\n");
			printf("\tcontaining the slave\n");
			close(fd);
			return;
		}
	} else {
		asprintf(&snapshots_path,
			 "%s%ssnapshots", path, dividing_slash(path));
	}

	/*
	 * Check for old-style config file
	 */
	if (snapshots_path) {
		asprintf(&config_path, "%s/config", snapshots_path);
		fp = fopen(config_path, "r");
	}

	/*
	 * Handle upgrades to hammer version 3, move the config
	 * file into meta-data.
	 *
	 * For the old config read the file into the config structure,
	 * we will parse it out of the config structure regardless.
	 */
	if (version.cur_version >= 3) {
		if (fp) {
			printf("(migrating) ");
			fflush(stdout);
			migrate_config(fp, &config);
			migrate_snapshots(fd, snapshots_path);
			fclose(fp);
			if (ioctl(fd, HAMMERIOC_SET_CONFIG, &config) < 0) {
				printf(" cannot init meta-data config!\n");
				close(fd);
				return;
			}
			remove(config_path);
		} else if (new_config == 0) {
			config_init(path, &config);
			if (ioctl(fd, HAMMERIOC_SET_CONFIG, &config) < 0) {
				printf(" cannot init meta-data config!\n");
				close(fd);
				return;
			}
		}
		new_config = 1;
	} else {
		/*
		 * Create missing snapshots directory for HAMMER VERSION < 3
		 */
		if (stat(snapshots_path, &st) < 0) {
			if (mkdir(snapshots_path, 0755) != 0) {
				free(snapshots_path);
				printf(" unable to create snapshot dir \"%s\": %s\n",
					snapshots_path, strerror(errno));
				close(fd);
				return;
			}
		}

		/*
		 *  Create missing config file for HAMMER VERSION < 3
		 */
		if (fp == NULL) {
			config_init(path, &config);
			fp = fopen(config_path, "w");
			if (fp) {
				fwrite(config.config.text, 1,
					strlen(config.config.text), fp);
				fclose(fp);
			}
		} else {
			migrate_config(fp, &config);
			fclose(fp);
		}
	}

	/*
	 * If snapshots_from_pfs is not set we calculate the new snapshots
	 * directory default (in /var) for HAMMER VERSION >= 3 and migrate
	 * the old snapshots directory over.
	 *
	 * People who have set an explicit snapshots directory will have
	 * to migrate the data manually into /var/hammer, or not bother at
	 * all.  People running slaves may wish to migrate it and then
	 * clear the snapshots specification in the PFS config for the
	 * slave.
	 */
	if (new_config && snapshots_from_pfs == 0) {
		char *npath;

		assert(path[0] == '/');
		if (strcmp(path, "/") == 0)
			asprintf(&npath, "%s/root", SNAPSHOTS_BASE);
		else
			asprintf(&npath, "%s/%s", SNAPSHOTS_BASE, path + 1);
		if (snapshots_path) {
			if (stat(npath, &st) < 0 && errno == ENOENT) {
				if (stat(snapshots_path, &st) < 0 && errno == ENOENT) {
					printf(" HAMMER UPGRADE: Creating snapshots\n"
					       "\tCreating snapshots in %s\n",
					       npath);
					runcmd(&r, "mkdir -p %s", npath);
				} else {
					printf(" HAMMER UPGRADE: Moving snapshots\n"
					       "\tMoving snapshots from %s to %s\n",
					       snapshots_path, npath);
					runcmd(&r, "mkdir -p %s", npath);
					runcmd(&r, "cpdup %s %s", snapshots_path, npath);
					if (r != 0) {
				    printf("Unable to move snapshots directory!\n");
				    printf("Please fix this critical error.\n");
				    printf("Aborting cleanup of %s\n", path);
						close(fd);
						return;
					}
					runcmd(&r, "rm -rf %s", snapshots_path);
				}
			}
			free(snapshots_path);
		} else if (stat(npath, &st) < 0 && errno == ENOENT) {
			runcmd(&r, "mkdir -p %s", npath);
		}
		snapshots_path = npath;
	}

	/*
	 * Lock the PFS.  fd is the base directory of the mounted PFS.
	 */
	if (flock(fd, LOCK_EX|LOCK_NB) == -1) {
		if (errno == EWOULDBLOCK)
			printf(" PFS #%d locked by other process\n", pfs.pfs_id);
		else
			printf(" can not lock %s: %s\n", config_path, strerror(errno));
		close(fd);
		return;
	}

	printf(" handle PFS #%d using %s\n", pfs.pfs_id, snapshots_path);

	/*
	 * Process the config file
	 */
	cbase = config.config.text;

	while ((cptr = strchr(cbase, '\n')) != NULL) {
		bcopy(cbase, buf, cptr - cbase);
		buf[cptr - cbase] = 0;
		cbase = cptr + 1;

		cmd = strtok(buf, WS);
		if (cmd == NULL || cmd[0] == '#')
			continue;

		arg1 = 0;
		arg2 = 0;
		arg3 = NULL;
		if ((ptr = strtok(NULL, WS)) != NULL) {
			arg1 = strtosecs(ptr);
			if ((ptr = strtok(NULL, WS)) != NULL) {
				arg2 = strtosecs(ptr);
				arg3 = strtok(NULL, WS);
			}
		}

		printf("%20s - ", cmd);
		fflush(stdout);

		r = 1;
		if (strcmp(cmd, "snapshots") == 0) {
			if (arg1 == 0) {
				if (arg2 &&
				    check_softlinks(fd, new_config,
						    snapshots_path)) {
					printf("only removing old snapshots\n");
					prune_warning = 1;
					cleanup_softlinks(fd, new_config,
							  snapshots_path,
							  arg2, arg3);
				} else {
					printf("disabled\n");
					snapshots_disabled = 1;
				}
			} else
			if (check_period(snapshots_path, cmd, arg1, &savet)) {
				printf("run\n");
				cleanup_softlinks(fd, new_config,
						  snapshots_path,
						  arg2, arg3);
				r = create_snapshot(path, snapshots_path);
			} else {
				printf("skip\n");
			}
		} else if (arg1 == 0) {
			/*
			 * The commands following this check can't handle
			 * a period of 0, so call the feature disabled and
			 * ignore the directive.
			 */
			printf("disabled\n");
		} else if (strcmp(cmd, "prune") == 0) {
			if (check_period(snapshots_path, cmd, arg1, &savet)) {
				if (prune_warning) {
					printf("run - WARNING snapshot "
					       "softlinks present "
					       "but snapshots disabled\n");
				} else {
					printf("run\n");
				}
				r = cleanup_prune(path, snapshots_path,
					      arg1, arg2, snapshots_disabled);
			} else {
				printf("skip\n");
			}
		} else if (strcmp(cmd, "rebalance") == 0) {
			found_rebal = 1;
			if (check_period(snapshots_path, cmd, arg1, &savet)) {
				printf("run");
				fflush(stdout);
				if (VerboseOpt)
					printf("\n");
				r = cleanup_rebalance(path, snapshots_path,
						arg1, arg2);
			} else {
				printf("skip\n");
			}
		} else if (strcmp(cmd, "reblock") == 0) {
			if (check_period(snapshots_path, cmd, arg1, &savet)) {
				printf("run");
				fflush(stdout);
				if (VerboseOpt)
					printf("\n");
				r = cleanup_reblock(path, snapshots_path,
						arg1, arg2);
			} else {
				printf("skip\n");
			}
		} else if (strcmp(cmd, "recopy") == 0) {
			if (check_period(snapshots_path, cmd, arg1, &savet)) {
				printf("run");
				fflush(stdout);
				if (VerboseOpt)
					printf("\n");
				r = cleanup_recopy(path, snapshots_path,
					       arg1, arg2);
			} else {
				printf("skip\n");
			}
		} else if (strcmp(cmd, "dedup") == 0) {
			if (check_period(snapshots_path, cmd, arg1, &savet)) {
				printf("run");
				fflush(stdout);
				if (VerboseOpt)
					printf("\n");
				r = cleanup_dedup(path, snapshots_path,
						arg1, arg2);
			} else {
				printf("skip\n");
			}
		} else {
			printf("unknown directive\n");
			r = 1;
		}
		if (r == 0)
			save_period(snapshots_path, cmd, savet);
	}

	/*
	 * Add new rebalance feature if the config doesn't have it.
	 * (old style config only).
	 */
	if (new_config == 0 && found_rebal == 0) {
		if ((fp = fopen(config_path, "r+")) != NULL) {
			fseek(fp, 0L, 2);
			fprintf(fp, "rebalance 1d 5m\n");
			fclose(fp);
		}
	}

	/*
	 * Cleanup, and delay a little
	 */
	close(fd);
	usleep(1000);
}

/*
 * Initialize new config data (new or old style)
 */
static void
config_init(const char *path, struct hammer_ioc_config *config)
{
	const char *snapshots;

	if (strcmp(path, "/tmp") == 0 ||
	    strcmp(path, "/var/tmp") == 0 ||
	    strcmp(path, "/usr/obj") == 0) {
		snapshots = "snapshots 0d 0d\n";
	} else {
		snapshots = "snapshots 1d 60d\n";
	}
	bzero(config->config.text, sizeof(config->config.text));
	snprintf(config->config.text, sizeof(config->config.text) - 1, "%s%s",
		snapshots,
		"prune     1d 5m\n"
		"rebalance 1d 5m\n"
		"#dedup	   1d 5m\n"
		"reblock   1d 5m\n"
		"recopy    30d 10m\n");
}

/*
 * Migrate configuration data from the old snapshots/config
 * file to the new meta-data format.
 */
static void
migrate_config(FILE *fp, struct hammer_ioc_config *config)
{
	int n;

	n = fread(config->config.text, 1, sizeof(config->config.text) - 1, fp);
	if (n >= 0)
		bzero(config->config.text + n, sizeof(config->config.text) - n);
}

/*
 * Migrate snapshot softlinks in the snapshots directory to the
 * new meta-data format.  The softlinks are left intact, but
 * this way the pruning code won't lose track of them if you
 * happen to blow away the snapshots directory.
 */
static void
migrate_snapshots(int fd, const char *snapshots_path)
{
	struct hammer_ioc_snapshot snapshot;
	struct dirent *den;
	struct stat st;
	DIR *dir;
	char *fpath;

	bzero(&snapshot, sizeof(snapshot));

	if ((dir = opendir(snapshots_path)) != NULL) {
		while ((den = readdir(dir)) != NULL) {
			if (den->d_name[0] == '.')
				continue;
			asprintf(&fpath, "%s/%s", snapshots_path, den->d_name);
			if (lstat(fpath, &st) == 0 && S_ISLNK(st.st_mode)) {
				migrate_one_snapshot(fd, fpath, &snapshot);
			}
			free(fpath);
		}
		closedir(dir);
	}
	migrate_one_snapshot(fd, NULL, &snapshot);

}

/*
 * Migrate a single snapshot.  If fpath is NULL the ioctl is flushed,
 * otherwise it is flushed when it fills up.
 */
static void
migrate_one_snapshot(int fd, const char *fpath,
		     struct hammer_ioc_snapshot *snapshot)
{
	if (fpath) {
		struct hammer_snapshot_data *snap;
		struct tm tm;
		time_t t;
		int year;
		int month;
		int day = 0;
		int hour = 0;
		int minute = 0;
		int r;
		char linkbuf[1024];
		const char *ptr;
		hammer_tid_t tid;

		t = (time_t)-1;
		tid = (hammer_tid_t)(int64_t)-1;

		/* fpath may contain directory components */
		if ((ptr = strrchr(fpath, '/')) != NULL)
			++ptr;
		else
			ptr = fpath;
		while (*ptr && *ptr != '-' && *ptr != '.')
			++ptr;
		if (*ptr)
			++ptr;
		r = sscanf(ptr, "%4d%2d%2d-%2d%2d",
			   &year, &month, &day, &hour, &minute);

		if (r >= 3) {
			bzero(&tm, sizeof(tm));
			tm.tm_isdst = -1;
			tm.tm_min = minute;
			tm.tm_hour = hour;
			tm.tm_mday = day;
			tm.tm_mon = month - 1;
			tm.tm_year = year - 1900;
			t = mktime(&tm);
		}
		bzero(linkbuf, sizeof(linkbuf));
		if (readlink(fpath, linkbuf, sizeof(linkbuf) - 1) > 0 &&
		    (ptr = strrchr(linkbuf, '@')) != NULL &&
		    ptr > linkbuf && ptr[-1] == '@') {
			tid = strtoull(ptr + 1, NULL, 16);
		}
		if (t != (time_t)-1 && tid != (hammer_tid_t)(int64_t)-1) {
			snap = &snapshot->snaps[snapshot->count];
			bzero(snap, sizeof(*snap));
			snap->tid = tid;
			snap->ts = (u_int64_t)t * 1000000ULL;
			snprintf(snap->label, sizeof(snap->label),
				 "migrated");
			++snapshot->count;
		} else {
			printf("    non-canonical snapshot softlink: %s->%s\n",
			       fpath, linkbuf);
		}
	}

	if ((fpath == NULL && snapshot->count) ||
	    snapshot->count == HAMMER_SNAPS_PER_IOCTL) {
		printf(" (%d snapshots)", snapshot->count);
again:
		if (ioctl(fd, HAMMERIOC_ADD_SNAPSHOT, snapshot) < 0) {
			printf("    Ioctl to migrate snapshots failed: %s\n",
			       strerror(errno));
		} else if (snapshot->head.error == EALREADY) {
			++snapshot->index;
			goto again;
		} else if (snapshot->head.error) {
			printf("    Ioctl to migrate snapshots failed: %s\n",
			       strerror(snapshot->head.error));
		}
		printf("index %d\n", snapshot->index);
		snapshot->index = 0;
		snapshot->count = 0;
		snapshot->head.error = 0;
	}
}

static
int
strtosecs(char *ptr)
{
	int val;

	val = strtol(ptr, &ptr, 0);
	switch(*ptr) {
	case 'd':
		val *= 24;
		/* fall through */
	case 'h':
		val *= 60;
		/* fall through */
	case 'm':
		val *= 60;
		/* fall through */
	case 's':
		break;
	default:
		errx(1, "illegal suffix converting %s\n", ptr);
		break;
	}
	return(val);
}

static const char *
dividing_slash(const char *path)
{
	int len = strlen(path);
	if (len && path[len-1] == '/')
		return("");
	else
		return("/");
}

/*
 * Check whether the desired period has elapsed since the last successful
 * run.  The run may take a while and cross a boundary so we remember the
 * current time_t so we can save it later on.
 *
 * Periods in minutes, hours, or days are assumed to have been crossed
 * if the local time crosses a minute, hour, or day boundary regardless
 * of how close the last operation actually was.
 *
 * If ForceOpt is set always return true.
 */
static int
check_period(const char *snapshots_path, const char *cmd, int arg1,
	time_t *savep)
{
	char *check_path;
	struct tm tp1;
	struct tm tp2;
	FILE *fp;
	time_t baset, lastt;
	char buf[256];

	time(savep);
	localtime_r(savep, &tp1);

	/*
	 * Force run if -F
	 */
	if (ForceOpt)
		return(1);

	/*
	 * Retrieve the start time of the last successful operation.
	 */
	asprintf(&check_path, "%s/.%s.period", snapshots_path, cmd);
	fp = fopen(check_path, "r");
	free(check_path);
	if (fp == NULL)
		return(1);
	if (fgets(buf, sizeof(buf), fp) == NULL) {
		fclose(fp);
		return(1);
	}
	fclose(fp);

	lastt = strtol(buf, NULL, 0);
	localtime_r(&lastt, &tp2);

	/*
	 * Normalize the times.  e.g. if asked to do something on a 1-day
	 * interval the operation will be performed as soon as the day
	 * turns over relative to the previous operation, even if the previous
	 * operation ran a few seconds ago just before midnight.
	 */
	if (arg1 % 60 == 0) {
		tp1.tm_sec = 0;
		tp2.tm_sec = 0;
	}
	if (arg1 % (60 * 60) == 0) {
		tp1.tm_min = 0;
		tp2.tm_min = 0;
	}
	if (arg1 % (24 * 60 * 60) == 0) {
		tp1.tm_hour = 0;
		tp2.tm_hour = 0;
	}

	baset = mktime(&tp1);
	lastt = mktime(&tp2);

#if 0
	printf("%lld vs %lld\n", (long long)(baset - lastt), (long long)arg1);
#endif

	if ((int)(baset - lastt) >= arg1)
		return(1);
	return(0);
}

/*
 * Store the start time of the last successful operation.
 */
static void
save_period(const char *snapshots_path, const char *cmd,
			time_t savet)
{
	char *ocheck_path;
	char *ncheck_path;
	FILE *fp;

	asprintf(&ocheck_path, "%s/.%s.period", snapshots_path, cmd);
	asprintf(&ncheck_path, "%s/.%s.period.new", snapshots_path, cmd);
	fp = fopen(ncheck_path, "w");
	if (fp) {
		fprintf(fp, "0x%08llx\n", (long long)savet);
		if (fclose(fp) == 0)
			rename(ncheck_path, ocheck_path);
		remove(ncheck_path);
	} else {
		fprintf(stderr, "hammer: Unable to create period-file %s: %s\n",
			ncheck_path, strerror(errno));
	}
}

/*
 * Simply count the number of softlinks in the snapshots dir
 */
static int
check_softlinks(int fd, int new_config, const char *snapshots_path)
{
	struct dirent *den;
	struct stat st;
	DIR *dir;
	char *fpath;
	int res = 0;

	/*
	 * Old-style softlink-based snapshots
	 */
	if ((dir = opendir(snapshots_path)) != NULL) {
		while ((den = readdir(dir)) != NULL) {
			if (den->d_name[0] == '.')
				continue;
			asprintf(&fpath, "%s/%s", snapshots_path, den->d_name);
			if (lstat(fpath, &st) == 0 && S_ISLNK(st.st_mode))
				++res;
			free(fpath);
		}
		closedir(dir);
	}

	/*
	 * New-style snapshots are stored as filesystem meta-data,
	 * count those too.
	 */
	if (new_config) {
		struct hammer_ioc_snapshot snapshot;

		bzero(&snapshot, sizeof(snapshot));
		do {
			if (ioctl(fd, HAMMERIOC_GET_SNAPSHOT, &snapshot) < 0) {
				err(2, "hammer cleanup: check_softlink "
					"snapshot error");
				/* not reached */
			}
			res += snapshot.count;
		} while (snapshot.head.error == 0 && snapshot.count);
	}
	return (res);
}

/*
 * Clean up expired softlinks in the snapshots dir
 */
static void
cleanup_softlinks(int fd, int new_config,
		  const char *snapshots_path, int arg2, char *arg3)
{
	struct dirent *den;
	struct stat st;
	DIR *dir;
	char *fpath;
	int anylink = 0;

	if (arg3 != NULL && strstr(arg3, "any") != NULL)
		anylink = 1;

	if ((dir = opendir(snapshots_path)) != NULL) {
		while ((den = readdir(dir)) != NULL) {
			if (den->d_name[0] == '.')
				continue;
			asprintf(&fpath, "%s/%s", snapshots_path, den->d_name);
			if (lstat(fpath, &st) == 0 && S_ISLNK(st.st_mode) &&
			    (anylink || strncmp(den->d_name, "snap-", 5) == 0)
			) {
				if (check_expired(den->d_name, arg2)) {
					if (VerboseOpt) {
						printf("    expire %s\n",
							fpath);
					}
					remove(fpath);
				}
			}
			free(fpath);
		}
		closedir(dir);
	}

	/*
	 * New-style snapshots are stored as filesystem meta-data,
	 * count those too.
	 */
	if (new_config) {
		struct hammer_ioc_snapshot snapshot;
		struct hammer_ioc_snapshot dsnapshot;
		struct hammer_snapshot_data *snap;
		struct tm *tp;
		time_t t;
		time_t dt;
		char snapts[32];
		u_int32_t i;

		bzero(&snapshot, sizeof(snapshot));
		bzero(&dsnapshot, sizeof(dsnapshot));
		do {
			if (ioctl(fd, HAMMERIOC_GET_SNAPSHOT, &snapshot) < 0) {
				err(2, "hammer cleanup: check_softlink "
					"snapshot error");
				/* not reached */
			}
			for (i = 0; i < snapshot.count; ++i) {
				snap = &snapshot.snaps[i];
				t = snap->ts / 1000000ULL;
				dt = time(NULL) - t;
				if ((int)dt > arg2 || snap->tid == 0) {
					dsnapshot.snaps[dsnapshot.count++] =
						*snap;
				}
				if ((int)dt > arg2 && VerboseOpt) {
					tp = localtime(&t);
					strftime(snapts, sizeof(snapts),
						 "%Y-%m-%d %H:%M:%S %Z", tp);
					printf("    expire 0x%016jx %s %s\n",
					       (uintmax_t)snap->tid,
					       snapts,
					       snap->label);
				}
				if (dsnapshot.count == HAMMER_SNAPS_PER_IOCTL)
					delete_snapshots(fd, &dsnapshot);
			}
		} while (snapshot.head.error == 0 && snapshot.count);

		if (dsnapshot.count)
			delete_snapshots(fd, &dsnapshot);
	}
}

static void
delete_snapshots(int fd, struct hammer_ioc_snapshot *dsnapshot)
{
	for (;;) {
		if (ioctl(fd, HAMMERIOC_DEL_SNAPSHOT, dsnapshot) < 0) {
			printf("    Ioctl to delete snapshots failed: %s\n",
			       strerror(errno));
			break;
		}
		if (dsnapshot->head.error) {
			printf("    Ioctl to delete snapshots failed at "
			       "snap=%016jx: %s\n",
			       dsnapshot->snaps[dsnapshot->index].tid,
			       strerror(dsnapshot->head.error));
			if (++dsnapshot->index < dsnapshot->count)
				continue;
		}
		break;
	}
	dsnapshot->index = 0;
	dsnapshot->count = 0;
	dsnapshot->head.error = 0;
}

/*
 * Take a softlink path in the form snap-yyyymmdd-hhmm and the
 * expiration in seconds (arg2) and return non-zero if the softlink
 * has expired.
 */
static int
check_expired(const char *fpath, int arg2)
{
	struct tm tm;
	time_t t;
	int year;
	int month;
	int day = 0;
	int hour = 0;
	int minute = 0;
	int r;

	while (*fpath && *fpath != '-' && *fpath != '.')
		++fpath;
	if (*fpath)
		++fpath;

	r = sscanf(fpath, "%4d%2d%2d-%2d%2d",
		   &year, &month, &day, &hour, &minute);

	if (r >= 3) {
		bzero(&tm, sizeof(tm));
		tm.tm_isdst = -1;
		tm.tm_min = minute;
		tm.tm_hour = hour;
		tm.tm_mday = day;
		tm.tm_mon = month - 1;
		tm.tm_year = year - 1900;
		t = mktime(&tm);
		if (t == (time_t)-1)
			return(0);
		t = time(NULL) - t;
		if ((int)t > arg2)
			return(1);
	}
	return(0);
}

/*
 * Issue a snapshot.
 */
static int
create_snapshot(const char *path, const char *snapshots_path)
{
	int r;

	runcmd(&r, "hammer snapshot %s %s", path, snapshots_path);
	return(r);
}

static int
cleanup_prune(const char *path, const char *snapshots_path,
		  int arg1 __unused, int arg2, int snapshots_disabled)
{
	const char *path_or_snapshots_path;
	struct softprune *base = NULL;
	struct hammer_ioc_prune dummy_template;

	bzero(&dummy_template, sizeof(dummy_template));
	hammer_softprune_scandir(&base, &dummy_template, snapshots_path);

	/*
	 * If the snapshots_path (e.g. /var/hammer/...) has no snapshots
	 * in it then prune will get confused and prune the filesystem
	 * containing the snapshots_path instead of the requested
	 * filesystem.  De-confuse prune.  We need a better way.
	 */
	path_or_snapshots_path = base ? snapshots_path : path;

	/*
	 * If snapshots have been disabled run prune-everything instead
	 * of prune.
	 */
	if (snapshots_disabled && arg2) {
		runcmd(NULL,
		       "hammer -c %s/.prune.cycle -t %d prune-everything %s",
		       snapshots_path, arg2, path);
	} else if (snapshots_disabled) {
		runcmd(NULL, "hammer prune-everything %s", path);
	} else if (arg2) {
		runcmd(NULL, "hammer -c %s/.prune.cycle -t %d prune %s",
			snapshots_path, arg2, path_or_snapshots_path);
	} else {
		runcmd(NULL, "hammer prune %s", path_or_snapshots_path);
	}
	return(0);
}

static int
cleanup_rebalance(const char *path, const char *snapshots_path,
		  int arg1 __unused, int arg2)
{
	if (VerboseOpt == 0) {
		printf(".");
		fflush(stdout);
	}

	runcmd(NULL,
	       "hammer -c %s/.rebalance.cycle -t %d rebalance %s",
	       snapshots_path, arg2, path);
	if (VerboseOpt == 0) {
		printf(".");
		fflush(stdout);
	}
	if (VerboseOpt == 0)
		printf("\n");
	return(0);
}

static int
cleanup_reblock(const char *path, const char *snapshots_path,
		  int arg1 __unused, int arg2)
{
	if (VerboseOpt == 0) {
		printf(".");
		fflush(stdout);
	}

	/*
	 * When reblocking the B-Tree always reblock everything in normal
	 * mode.
	 */
	runcmd(NULL,
	       "hammer -c %s/.reblock-1.cycle -t %d reblock-btree %s",
	       snapshots_path, arg2, path);
	if (VerboseOpt == 0) {
		printf(".");
		fflush(stdout);
	}

	/*
	 * When reblocking the inodes always reblock everything in normal
	 * mode.
	 */
	runcmd(NULL,
	       "hammer -c %s/.reblock-2.cycle -t %d reblock-inodes %s",
	       snapshots_path, arg2, path);
	if (VerboseOpt == 0) {
		printf(".");
		fflush(stdout);
	}

	/*
	 * When reblocking the directories always reblock everything in normal
	 * mode.
	 */
	runcmd(NULL,
	       "hammer -c %s/.reblock-4.cycle -t %d reblock-dirs %s",
	       snapshots_path, arg2, path);
	if (VerboseOpt == 0) {
		printf(".");
		fflush(stdout);
	}

	/*
	 * Do not reblock all the data in normal mode.
	 */
	runcmd(NULL,
	       "hammer -c %s/.reblock-3.cycle -t %d reblock-data %s 95",
	       snapshots_path, arg2, path);
	if (VerboseOpt == 0)
		printf("\n");
	return(0);
}

static int
cleanup_recopy(const char *path, const char *snapshots_path,
		  int arg1 __unused, int arg2)
{
	if (VerboseOpt == 0) {
		printf(".");
		fflush(stdout);
	}
	runcmd(NULL,
	       "hammer -c %s/.recopy-1.cycle -t %d reblock-btree %s",
	       snapshots_path, arg2, path);
	if (VerboseOpt == 0) {
		printf(".");
		fflush(stdout);
	}
	runcmd(NULL,
	       "hammer -c %s/.recopy-2.cycle -t %d reblock-inodes %s",
	       snapshots_path, arg2, path);
	if (VerboseOpt == 0) {
		printf(".");
		fflush(stdout);
	}
	runcmd(NULL,
	       "hammer -c %s/.recopy-4.cycle -t %d reblock-dirs %s",
	       snapshots_path, arg2, path);
	if (VerboseOpt == 0) {
		printf(".");
		fflush(stdout);
	}
	runcmd(NULL,
	       "hammer -c %s/.recopy-3.cycle -t %d reblock-data %s",
	       snapshots_path, arg2, path);
	if (VerboseOpt == 0)
		printf("\n");
	return(0);
}

static int
cleanup_dedup(const char *path, const char *snapshots_path __unused,
		  int arg1 __unused, int arg2)
{
	if (VerboseOpt == 0) {
		printf(".");
		fflush(stdout);
	}

	runcmd(NULL, "hammer -t %d dedup %s", arg2, path);
	if (VerboseOpt == 0) {
		printf(".");
		fflush(stdout);
	}
	if (VerboseOpt == 0)
		printf("\n");
	return(0);
}

static
void
runcmd(int *resp, const char *ctl, ...)
{
	va_list va;
	char *cmd;
	char *arg;
	char **av;
	int n;
	int nmax;
	int res;
	pid_t pid;

	/*
	 * Generate the command
	 */
	va_start(va, ctl);
	vasprintf(&cmd, ctl, va);
	va_end(va);
	if (VerboseOpt)
		printf("    %s\n", cmd);

	/*
	 * Break us down into arguments.  We do not just use system() here
	 * because it blocks SIGINT and friends.
	 */
	n = 0;
	nmax = 16;
	av = malloc(sizeof(char *) * nmax);

	for (arg = strtok(cmd, WS); arg; arg = strtok(NULL, WS)) {
		if (n == nmax - 1) {
			nmax += 16;
			av = realloc(av, sizeof(char *) * nmax);
		}
		av[n++] = arg;
	}
	av[n++] = NULL;

	/*
	 * Run the command.
	 */
	RunningIoctl = 1;
	if ((pid = fork()) == 0) {
		if (VerboseOpt < 2) {
			int fd = open("/dev/null", O_RDWR);
			dup2(fd, 1);
			close(fd);
		}
		execvp(av[0], av);
		_exit(127);
	} else if (pid < 0) {
		res = 127;
	} else {
		int status;

		while (waitpid(pid, &status, 0) != pid)
			;
		res = WEXITSTATUS(status);
	}
	RunningIoctl = 0;
	if (DidInterrupt)
		_exit(1);

	free(cmd);
	free(av);
	if (resp)
		*resp = res;
}

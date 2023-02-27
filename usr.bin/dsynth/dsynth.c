/*
 * Copyright (c) 2019 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * This code uses concepts and configuration based on 'synth', by
 * John R. Marino <draco@marino.st>, which was written in ada.
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

#include "dsynth.h"

static int CheckAddReExec(int lkfd);
static void DoAddReExec(int lkfd, int ac, char **oldav);
static void DoInit(void);
static void usage(int ecode) __dead2;

int OverridePkgDeleteOpt;
int FetchOnlyOpt;
int YesOpt;
int DebugOpt;
int MaskProbeAbort;
int ColorOpt = 1;
int NullStdinOpt = 1;
int SlowStartOpt = -1;
long PkgDepMemoryTarget;
long PkgDepScaleTarget = 100;	/* 1.00 */
char *DSynthExecPath;
char *ProfileOverrideOpt;
int NiceOpt = 10;

int
main(int ac, char **av)
{
	char *lkpath;
	pkg_t *pkgs;
	int lkfd;
	int isworker;
	int c;
	int sopt;
	int doadds;

	/*
	 * Get our exec path so we can self-exec clean WORKER
	 * processes.
	 */
	{
		size_t len;
		const int name[] = {
			CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1,
		};
		if (sysctl(name, 4, NULL, &len, NULL, 0) < 0)
			dfatal_errno("Cannot get binary path");
		DSynthExecPath = malloc(len + 1);
		if (sysctl(name, 4, DSynthExecPath, &len, NULL, 0) < 0)
			dfatal_errno("Cannot get binary path");
		DSynthExecPath[len] = 0;
	}

	/*
	 * Override profile in dsynth.ini (can be further overridden
	 * with the -p profile option).
	 */
	ProfileOverrideOpt = getenv("DSYNTH_PROFILE");

	/*
	 * Process options and make sure the directive is present
	 */
	sopt = 0;
	while ((c = getopt(ac, av, "dhm:p:vxys:DPM:NS")) != -1) {
		switch(c) {
		case 'x':
			++OverridePkgDeleteOpt;
			break;
		case 'y':
			++YesOpt;
			break;
		case 'D':
			WorkerProcFlags |= WORKER_PROC_DEVELOPER;
			break;
		case 'P':
			WorkerProcFlags |= WORKER_PROC_CHECK_PLIST;
			break;
		case 'S':
			UseNCurses = 0;
			if (++sopt == 2)
				ColorOpt = 0;
			break;
		case 'N':
			NiceOpt = 0;
			break;
		case 'd':
			++DebugOpt;
			if (DebugOpt >= 2)
				UseNCurses = 0;
			break;
		case 'h':
			usage(0);
			/* NOT REACHED */
			exit(0);
		case 'v':
			printf("dsynth %s\n", DSYNTH_VERSION);
			exit(0);
		case 's':
			/*
			 * Start with N jobs, increasing to the configured
			 * maximum slowly.  0 to disable (starts with the
			 * full count).
			 */
			SlowStartOpt = strtol(optarg, NULL, 0);
			break;
		case 'm':
			PkgDepMemoryTarget = strtoul(optarg, NULL, 0);
			PkgDepMemoryTarget *= ONEGB;
			break;
		case 'M':
			PkgDepScaleTarget = strtod(optarg, NULL) * 100;
			if (PkgDepScaleTarget < 1)
				PkgDepScaleTarget = 1;
			if (PkgDepScaleTarget > 9900)
				PkgDepScaleTarget = 9900;
			break;
		case 'p':
			ProfileOverrideOpt = optarg;
			break;
		default:
			fprintf(stderr, "Unknown option: %c\n", c);
			usage(2);
			/* NOT REACHED */
			break;
		}
	}
	ac -= optind;
	av += optind;
	pkgs = NULL;
	if (ac < 1) {
		fprintf(stderr, "Missing directive\n");
		usage(2);
		/* NOT REACHED */
	}

	/*
	 * Directives which do not require a working configuration
	 */
	if (strcmp(av[0], "init") == 0) {
		DoInit();
		exit(0);
		/* NOT REACHED */
	}
	if (strcmp(av[0], "help") == 0) {
		usage(0);
		exit(0);
		/* NOT REACHED */
	}
	if (strcmp(av[0], "version") == 0) {
		printf("dsynth %s\n", DSYNTH_VERSION);
		exit(0);
		/* NOT REACHED */
	}

	/*
	 * Preconfiguration.
	 */
	if (strcmp(av[0], "WORKER") == 0) {
		isworker = 1;
	} else {
		isworker = 0;
	}

	signal(SIGPIPE, SIG_IGN);
	ParseConfiguration(isworker);

	/*
	 * Lock file path (also contains any 'add' directives thrown in
	 * during a build).
	 */
	asprintf(&lkpath, "%s/.lock", BuildBase);

	/*
	 * Setup some environment for bulk operations (pkglist scan).
	 * These are not used by the builder (the builder will replicate
	 * all of these).
	 *
	 * NOTE: PKG_SUFX - pkg versions older than 1.17
	 *	 PKG_COMPRESSION_FORMAT - pkg versions >= 1.17
	 */
	addbuildenv("PORTSDIR", DPortsPath,
		    BENV_ENVIRONMENT | BENV_PKGLIST);
	addbuildenv("BATCH", "yes",
		    BENV_ENVIRONMENT | BENV_PKGLIST);
	addbuildenv("PKG_COMPRESSION_FORMAT", UsePkgSufx,
		    BENV_ENVIRONMENT | BENV_PKGLIST);
	addbuildenv("PKG_SUFX", UsePkgSufx,
		    BENV_ENVIRONMENT | BENV_PKGLIST);
	addbuildenv("PACKAGE_BUILDING", "yes",
		    BENV_ENVIRONMENT | BENV_PKGLIST);
	addbuildenv("ARCH", ArchitectureName,
		    BENV_ENVIRONMENT | BENV_PKGLIST);

#if 0
	/*
	 *
	 */
	addbuildenv("OSTYPE", OperatingSystemName,
		    BENV_ENVIRONMENT | BENV_PKGLIST);
	addbuildenv("MACHTYPE", MachineName,
		    BENV_ENVIRONMENT | BENV_PKGLIST);
#endif
	/*
	 * SlowStart auto adjust.  We nominally start with 1 job and increase
	 * it to the maximum every 5 seconds to give various dynamic management
	 * parameters time to stabilize.
	 *
	 * This can take a while on a many-core box with a high jobs setting,
	 * so increase the initial jobs in such cases.
	 */
	if (SlowStartOpt > MaxWorkers)
		SlowStartOpt = MaxWorkers;
	if (SlowStartOpt < 0) {
		if (MaxWorkers < 16)
			SlowStartOpt = 1;
		else
			SlowStartOpt = MaxWorkers / 4;
	}

	/*
	 * Special directive for when dsynth execs itself to manage
	 * a worker chroot.
	 */
	if (isworker) {
		WorkerProcess(ac, av);
		exit(0);
	}

	/*
	 * Build initialization and directive handling
	 */
	DoInitBuild(-1);

	/*
	 * Directives that use the configuration but are not interlocked
	 * against a running dsynth.
	 */
	if (strcmp(av[0], "monitor") == 0) {
		char *spath;
		char *lpath;

		if (ac == 1) {
			asprintf(&spath, "%s/%s", StatsBase, STATS_FILE);
			asprintf(&lpath, "%s/%s", StatsBase, STATS_LOCKFILE);
			MonitorDirective(spath, lpath);
			free(spath);
			free(lpath);
		} else {
			MonitorDirective(av[1], NULL);
		}
		exit(0);
		/* NOT REACHED */
	} else if (strcmp(av[0], "add") == 0) {
		char *buf;
		int fd;
		int i;

		/*
		 * The lock check is a bit racey XXX
		 */
		fd = open(lkpath, O_RDWR | O_CREAT | O_APPEND, 0644);
		if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
			dfatal("No dsynth running to add ports to");
			flock(fd, LOCK_UN);
		}
		for (i = 1; i < ac; ++i) {
			asprintf(&buf, "%s\n", av[i]);
			write(fd, buf, strlen(buf));
			printf("added to run: %s\n", av[i]);
		}
		close(fd);
		exit(0);
	}

	/*
	 * Front-end exec (not a WORKER exec), normal startup.  We have
	 * the configuration so the first thing we need to do is check
	 * the lock file.
	 */
	lkfd = open(lkpath, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
	if (lkfd < 0)
		dfatal_errno("Unable to create %s", lkpath);
	if (flock(lkfd, LOCK_EX | LOCK_NB) < 0) {
		dfatal("Another dsynth is using %s, exiting",
		       BuildBase);
	}

	/*
	 * Starting a new run cleans out any prior add directives
	 * that may have been pending.
	 */
	ftruncate(lkfd, 0);
	/* leave descriptor open */

	doadds = 0;

	if (strcmp(av[0], "debug") == 0) {
		DoCleanBuild(1);
		OptimizeEnv();
		pkgs = ParsePackageList(ac - 1, av + 1, 1);
		RemovePackages(pkgs);
		DoBuild(pkgs);
		doadds = 1;
	} else if (strcmp(av[0], "status") == 0) {
		OptimizeEnv();
		if (ac - 1)
			pkgs = ParsePackageList(ac - 1, av + 1, 0);
		else
			pkgs = GetLocalPackageList();
		DoStatus(pkgs);
	} else if (strcmp(av[0], "cleanup") == 0) {
		DoCleanBuild(0);
	} else if (strcmp(av[0], "configure") == 0) {
		DoCleanBuild(0);
		DoConfigure();
	} else if (strcmp(av[0], "fetch-only") == 0) {
		if (SlowStartOpt == -1)
			SlowStartOpt = 999;
		if (PkgDepScaleTarget == 100)
			PkgDepScaleTarget = 999;
		++FetchOnlyOpt;
		++YesOpt;
		WorkerProcFlags |= WORKER_PROC_FETCHONLY;
		DoCleanBuild(1);
		OptimizeEnv();
		if (ac == 2 && strcmp(av[1], "everything") == 0) {
			MaskProbeAbort = 1;
			pkgs = GetFullPackageList();
		} else {
			pkgs = ParsePackageList(ac - 1, av + 1, 0);
		}
		DoBuild(pkgs);
		doadds = 1;
	} else if (strcmp(av[0], "list-system") == 0) {
		FILE *fp;

		DoCleanBuild(1);
		OptimizeEnv();
		pkgs = GetLocalPackageList();
		if ((fp = fopen("build.txt", "w")) != NULL) {
			while (pkgs) {
				fprintf(fp, "%s\n", pkgs->portdir);
				pkgs = pkgs->bnext;
			}
			fclose(fp);
			printf("list written to build.txt\n");
		} else {
			fprintf(stderr, "Cannot create 'build.txt'\n");
			exit(1);
		}
	} else if (strcmp(av[0], "upgrade-system") == 0) {
		DoCleanBuild(1);
		OptimizeEnv();
		pkgs = GetLocalPackageList();
		DoBuild(pkgs);
		DoRebuildRepo(0);
		DoUpgradePkgs(pkgs, 0);
		dfatal("NOTE: you have to pkg upgrade manually");
	} else if (strcmp(av[0], "prepare-system") == 0) {
		DeleteObsoletePkgs = 1;
		DoCleanBuild(1);
		OptimizeEnv();
		pkgs = GetLocalPackageList();
		DoBuild(pkgs);
		DoRebuildRepo(0);
	} else if (strcmp(av[0], "rebuild-repository") == 0) {
		OptimizeEnv();
		DoRebuildRepo(0);
	} else if (strcmp(av[0], "purge-distfiles") == 0) {
		OptimizeEnv();
		pkgs = GetFullPackageList();
		PurgeDistfiles(pkgs);
	} else if (strcmp(av[0], "reset-db") == 0) {
		char *dbmpath;

		asprintf(&dbmpath, "%s/ports_crc.db", BuildBase);
		remove(dbmpath);
		printf("%s reset, will be regenerated on next build\n",
		       dbmpath);
		free(dbmpath);
	} else if (strcmp(av[0], "status-everything") == 0) {
		OptimizeEnv();
		pkgs = GetFullPackageList();
		DoStatus(pkgs);
	} else if (strcmp(av[0], "everything") == 0) {
		if (WorkerProcFlags & WORKER_PROC_DEVELOPER)
			WorkerProcFlags |= WORKER_PROC_CHECK_PLIST;
		MaskProbeAbort = 1;
		DeleteObsoletePkgs = 1;
		DoCleanBuild(1);
		OptimizeEnv();
		pkgs = GetFullPackageList();
		DoBuild(pkgs);
		DoRebuildRepo(!CheckAddReExec(lkfd));
	} else if (strcmp(av[0], "build") == 0) {
		DoCleanBuild(1);
		OptimizeEnv();
		pkgs = ParsePackageList(ac - 1, av + 1, 0);
		DoBuild(pkgs);
		DoRebuildRepo(!CheckAddReExec(lkfd));
		DoUpgradePkgs(pkgs, 1);
		doadds = 1;
	} else if (strcmp(av[0], "just-build") == 0) {
		DoCleanBuild(1);
		OptimizeEnv();
		pkgs = ParsePackageList(ac - 1, av + 1, 0);
		DoBuild(pkgs);
		doadds = 1;
	} else if (strcmp(av[0], "install") == 0) {
		DoCleanBuild(1);
		OptimizeEnv();
		pkgs = ParsePackageList(ac - 1, av + 1, 0);
		DoBuild(pkgs);
		DoRebuildRepo(0);
		DoUpgradePkgs(pkgs, 0);
		doadds = 1;
	} else if (strcmp(av[0], "force") == 0) {
		DoCleanBuild(1);
		OptimizeEnv();
		pkgs = ParsePackageList(ac - 1, av + 1, 0);
		RemovePackages(pkgs);
		DoBuild(pkgs);
		DoRebuildRepo(!CheckAddReExec(lkfd));
		DoUpgradePkgs(pkgs, 1);
		doadds = 1;
	} else if (strcmp(av[0], "test") == 0) {
		WorkerProcFlags |= WORKER_PROC_CHECK_PLIST |
				   WORKER_PROC_INSTALL |
				   WORKER_PROC_DEINSTALL;
		DoCleanBuild(1);
		OptimizeEnv();
		pkgs = ParsePackageList(ac - 1, av + 1, 0);
		RemovePackages(pkgs);
		WorkerProcFlags |= WORKER_PROC_DEVELOPER;
		DoBuild(pkgs);
		doadds = 1;
	} else {
		fprintf(stderr, "Unknown directive '%s'\n", av[0]);
		usage(2);
	}

	/*
	 * For directives that support the 'add' directive, check for
	 * additions and re-exec.
	 *
	 * Note that the lockfile is O_CLOEXEC and will be remade on exec.
	 *
	 * XXX a bit racey vs adds done just as we are finishing
	 */
	if (doadds && CheckAddReExec(lkfd))
		DoAddReExec(lkfd, optind + 1, av - optind);

	return 0;
}

/*
 * If the 'add' directive was issued while a dsynth build was in
 * progress, we re-exec dsynth with its original options and
 * directive along with the added ports.
 */
static int
CheckAddReExec(int lkfd)
{
	struct stat st;

	if (fstat(lkfd, &st) < 0 || st.st_size == 0)
		return 0;
	return 1;
}

static void
DoAddReExec(int lkfd, int ac, char **oldav)
{
	struct stat st;
	char *buf;
	char **av;
	size_t bi;
	size_t i;
	int nadd;
	int n;

	if (fstat(lkfd, &st) < 0 || st.st_size == 0)
		return;
	buf = malloc(st.st_size + 1);
	if (read(lkfd, buf, st.st_size) != st.st_size) {
		free(buf);
		return;
	}
	buf[st.st_size] = 0;

	nadd = 0;
	for (i = 0; i < (size_t)st.st_size; ++i) {
		if (buf[i] == '\n' || buf[i] == 0) {
			buf[i] = 0;
			++nadd;
		}
	}

	av = calloc(ac + nadd + 1, sizeof(char *));

	for (n = 0; n < ac; ++n)
		av[n] = oldav[n];

	nadd = 0;
	bi = 0;
	for (i = 0; i < (size_t)st.st_size; ++i) {
		if (buf[i] == 0) {
			av[ac + nadd] = buf + bi;
			bi = i + 1;
			++nadd;
		}
	}

	printf("dsynth re-exec'ing additionally added packages\n");
	for (n = 0; n < ac + nadd; ++n)
		printf(" %s", av[n]);
	printf("\n");
	fflush(stdout);
	sleep(2);
	execv(DSynthExecPath, av);
}

static void
DoInit(void)
{
	struct stat st;
	char *path;
	FILE *fp;

	if (stat(ConfigBase1, &st) == 0) {
		dfatal("init will not overwrite %s", ConfigBase1);
	}
	if (stat(ConfigBase2, &st) == 0) {
		dfatal("init will not create %s if %s exists",
		       ConfigBase2, ConfigBase1);
	}
	if (mkdir(ConfigBase1, 0755) < 0)
		dfatal_errno("Unable to mkdir %s", ConfigBase1);

	asprintf(&path, "%s/dsynth.ini", ConfigBase1);
	fp = fopen(path, "w");
	dassert_errno(fp, "Unable to create %s", path);
	fprintf(fp, "%s",
	    "; This Synth configuration file is automatically generated\n"
	    "; Take care when hand editing!\n"
	    "\n"
	    "[Global Configuration]\n"
	    "profile_selected= LiveSystem\n"
	    "\n"
	    "[LiveSystem]\n"
	    "Operating_system= DragonFly\n"
	    "Directory_packages= /build/synth/live_packages\n"
	    "Directory_repository= /build/synth/live_packages/All\n"
	    "Directory_portsdir= /build/synth/dports\n"
	    "Directory_options= /build/synth/options\n"
	    "Directory_distfiles= /build/synth/distfiles\n"
	    "Directory_buildbase= /build/synth/build\n"
	    "Directory_logs= /build/synth/logs\n"
	    "Directory_ccache= disabled\n"
	    "Directory_system= /\n"
	    "Package_suffix= .txz\n"
	    "Number_of_builders= 0\n"
	    "Max_jobs_per_builder= 0\n"
	    "Tmpfs_workdir= true\n"
	    "Tmpfs_localbase= true\n"
	    "Display_with_ncurses= true\n"
	    "leverage_prebuilt= false\n"
	    "; Meta_version= 2\n"
	    "; Check_plist= false\n"
	    "; Numa_setsize= 2\n"
	    "\n");
	if (fclose(fp))
		dfatal_errno("Unable to write to %s\n", ConfigBase1);
	free(path);

	asprintf(&path, "%s/LiveSystem-make.conf", ConfigBase1);
	fp = fopen(path, "w");
	dassert_errno(fp, "Unable to create %s", path);
	fprintf(fp, "%s",
	    "#\n"
	    "# Various dports options that might be of interest\n"
	    "#\n"
	    "#LICENSES_ACCEPTED=      NONE\n"
	    "#DISABLE_LICENSES=       yes\n"
	    "#DEFAULT_VERSIONS=       ssl=openssl\n"
	    "#FORCE_PACKAGE=          yes\n"
	    "#DPORTS_BUILDER=         yes\n"
	    "#\n"
	    "# Turn these on to generate debug binaries.  However, these\n"
	    "# options will seriously bloat memory use and storage use,\n"
	    "# do not use lightly\n"
	    "#\n"
	    "#STRIP=\n"
	    "#WITH_DEBUG=yes\n"
	);
	if (fclose(fp))
		dfatal_errno("Unable to write to %s\n", ConfigBase1);
	free(path);
}

__dead2 static void
usage(int ecode)
{
	if (ecode == 2) {
		fprintf(stderr, "Run 'dsynth help' for usage\n");
		exit(1);
	}

	fprintf(stderr,
    "dsynth [options] directive\n"
    "    -d                   - Debug verbosity (-dd disables ncurses)\n"
    "    -h                   - Display this screen and exit\n"
    "    -m gb                - Load management based on pkgdep memory\n"
    "    -p profile           - Override profile selected in dsynth.ini\n"
    "    -s n                 - Set initial DynamicMaxWorkers\n"
    "    -v                   - Print version info and exit\n"
    "    -x                   - Do not rebuild packages with dependencies\n"
    "                           which require rebuilding\n"
    "    -xx                  - Do not rebuild packages whos dports trees\n"
    "                           change\n"
    "    -y                   - Automatically answer yes to dsynth questions\n"
    "    -D                   - Enable DEVELOPER mode\n"
    "    -P                   - Include the check-plist stage\n"
    "    -S                   - Disable ncurses\n"
    "    -N                   - Do not nice-up sub-processes (else nice +10)\n"
    "\n"
    "    init                 - Initialize /etc/dsynth\n"
    "    status               - Dry-run of 'upgrade-system'\n"
    "    cleanup              - Clean-up mounts\n"
    "    configure            - Bring up configuration menu\n"
    "    list-system          - Just generate the build list to build.txt\n"
    "    upgrade-system       - Incremental build and upgrade using pkg list\n"
    "                           from local system, then upgrade the local\n"
    "                           system.\n"
    "    prepare-system       - 'upgrade-system' but stops after building\n"
    "    rebuild-repository   - Rebuild database files for current repository\n"
    "    purge-distfiles      - Delete obsolete source distribution files\n"
    "    reset-db             - Delete ports_crc.db, regenerate next build\n"
    "    status-everything    - Dry-run of 'everything'\n"
    "    everything           - Build entire dports tree and repo database\n"
    "				(-D everything infers -P)\n"
    "    version              - Print version info and exit\n"
    "    help                 - Display this screen and exit\n"
    "    status     [ports]   - Dry-run of 'build' with given list\n"
    "    build      [ports]   - Incrementally build dports based on the given\n"
    "                           list, but asks before updating the repo\n"
    "                           database and system\n"
    "    just-build [ports]   - 'build' but skips post-build steps\n"
    "    install    [ports]   - 'build' but upgrades system without asking\n"
    "    force      [ports]   - 'build' but deletes existing packages first\n"
    "    test       [ports]   - 'build' w/DEVELOPER=yes and pre-deletes pkgs\n"
    "				(also infers -P)\n"
    "    debug      [ports]   - like 'test' but leaves mounts intact\n"
    "    fetch-only [ports]   - Fetch src dists only ('everything' ok)\n"
    "    monitor    [datfile] - Monitor a running dsynth\n"
    "\n"
    "    [ports] is a space-delimited list of origins, e.g. editors/joe.  It\n"
    "            may also be a path to a file containing one origin per line.\n"
	);

	exit(ecode);
}

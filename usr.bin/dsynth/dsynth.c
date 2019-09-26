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

static void DoInit(void);
static void usage(int ecode) __dead2;

int YesOpt;
int DebugOpt;
int ColorOpt = 1;
int NullStdinOpt = 1;
int SlowStartOpt = 1;
long PkgDepMemoryTarget;
char *DSynthExecPath;

int
main(int ac, char **av)
{
	pkg_t *pkgs;
	int isworker;
	int c;
	int sopt;

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
	 * Process options and make sure the directive is present
	 */
	sopt = 0;
	while ((c = getopt(ac, av, "dhm:vys:DS")) != -1) {
		switch(c) {
		case 'y':
			++YesOpt;
			break;
		case 'D':
			WorkerProcFlags |= WORKER_PROC_DEVELOPER;
			break;
		case 'S':
			UseNCurses = 0;
			if (++sopt == 2)
				ColorOpt = 0;
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
			SlowStartOpt = strtol(optarg, NULL, 0);
			break;
		case 'm':
			PkgDepMemoryTarget = strtoul(optarg, NULL, 0);
			PkgDepMemoryTarget *= ONEGB;
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

	if (strcmp(av[0], "init") == 0) {
		DoInit();
		exit(0);
	}

	if (strcmp(av[0], "WORKER") == 0) {
		isworker = 1;
	} else {
		isworker = 0;
	}

	/*
	 * Preconfiguration.
	 */
	signal(SIGPIPE, SIG_IGN);
	ParseConfiguration(isworker);

	/*
	 * Setup some environment for bulk operations (pkglist scan).
	 * These are not used by the builder (the builder will replicate
	 * all of these).
	 */
	addbuildenv("PORTSDIR", DPortsPath,
		    BENV_ENVIRONMENT | BENV_PKGLIST);
	addbuildenv("BATCH", "yes",
		    BENV_ENVIRONMENT | BENV_PKGLIST);
	addbuildenv("PKG_SUFX", USE_PKG_SUFX,
		    BENV_ENVIRONMENT | BENV_PKGLIST);
	addbuildenv("PACKAGE_BUILDING", "yes",
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
	 * Special directive for when dsynth execs itself to manage
	 * a worker chroot.
	 */
	if (isworker) {
		WorkerProcess(ac, av);
		exit(0);
	}

	DoInitBuild(-1);

	if (strcmp(av[0], "debug") == 0) {
#if 0
		DoCleanBuild(1);
		pkgs = ParsePackageList(ac - 1, av + 1);
		RemovePackages(pkgs);
		addbuildenv("DEVELOPER", "yes", BENV_ENVIRONMENT);
		DoBuild(pkgs);
#endif
		WorkerProcFlags |= WORKER_PROC_DEBUGSTOP;
		DoCleanBuild(1);
		OptimizeEnv();
		pkgs = ParsePackageList(ac - 1, av + 1);
		DoBuild(pkgs);
	} else if (strcmp(av[0], "status") == 0) {
		OptimizeEnv();
		if (ac - 1)
			pkgs = ParsePackageList(ac - 1, av + 1);
		else
			pkgs = GetLocalPackageList();
		DoStatus(pkgs);
	} else if (strcmp(av[0], "monitor") == 0) {
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
	} else if (strcmp(av[0], "cleanup") == 0) {
		DoCleanBuild(0);
	} else if (strcmp(av[0], "configure") == 0) {
		DoCleanBuild(0);
		DoConfigure();
	} else if (strcmp(av[0], "upgrade-system") == 0) {
		DoCleanBuild(1);
		OptimizeEnv();
		pkgs = GetLocalPackageList();
		DoBuild(pkgs);
		DoRebuildRepo(0);
		DoUpgradePkgs(pkgs, 0);
	} else if (strcmp(av[0], "prepare-system") == 0) {
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
	} else if (strcmp(av[0], "status-everything") == 0) {
		OptimizeEnv();
		pkgs = GetFullPackageList();
		DoStatus(pkgs);
	} else if (strcmp(av[0], "everything") == 0) {
		DoCleanBuild(1);
		OptimizeEnv();
		pkgs = GetFullPackageList();
		DoBuild(pkgs);
		DoRebuildRepo(1);
	} else if (strcmp(av[0], "version") == 0) {
		printf("dsynth %s\n", DSYNTH_VERSION);
		exit(0);
	} else if (strcmp(av[0], "help") == 0) {
		usage(0);
		/* NOT REACHED */
		exit(0);
	} else if (strcmp(av[0], "build") == 0) {
		DoCleanBuild(1);
		OptimizeEnv();
		pkgs = ParsePackageList(ac - 1, av + 1);
		DoBuild(pkgs);
		DoRebuildRepo(1);
		DoUpgradePkgs(pkgs, 1);
	} else if (strcmp(av[0], "just-build") == 0) {
		DoCleanBuild(1);
		OptimizeEnv();
		pkgs = ParsePackageList(ac - 1, av + 1);
		DoBuild(pkgs);
	} else if (strcmp(av[0], "install") == 0) {
		DoCleanBuild(1);
		OptimizeEnv();
		pkgs = ParsePackageList(ac - 1, av + 1);
		DoBuild(pkgs);
		DoRebuildRepo(0);
		DoUpgradePkgs(pkgs, 0);
	} else if (strcmp(av[0], "force") == 0) {
		DoCleanBuild(1);
		OptimizeEnv();
		pkgs = ParsePackageList(ac - 1, av + 1);
		RemovePackages(pkgs);
		DoBuild(pkgs);
		DoRebuildRepo(1);
		DoUpgradePkgs(pkgs, 1);
	} else if (strcmp(av[0], "test") == 0) {
		DoCleanBuild(1);
		OptimizeEnv();
		pkgs = ParsePackageList(ac - 1, av + 1);
		RemovePackages(pkgs);
		WorkerProcFlags |= WORKER_PROC_DEVELOPER;
		DoBuild(pkgs);
	} else {
		fprintf(stderr, "Unknown directive '%s'\n", av[0]);
		usage(2);
	}

	return 0;
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
	    "Number_of_builders= 0\n"
	    "Max_jobs_per_builder= 0\n"
	    "Tmpfs_workdir= true\n"
	    "Tmpfs_localbase= true\n"
	    "Display_with_ncurses= true\n"
	    "leverage_prebuilt= false\n"
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
    "    -v                   - Print version info and exit\n"
    "    -y                   - Automatically answer yes to dsynth questions\n"
    "    -s n                 - Set initial DynamicMaxWorkers\n"
    "    -D                   - Enable DEVELOPER mode\n"
    "    -S                   - Disable ncurses\n"
    "\n"
    "    init                 - Initialize /etc/dsynth\n"
    "    status               - Dry-run of 'upgrade-system'\n"
    "    cleanup              - Clean-up mounts\n"
    "    configure            - Bring up configuration menu\n"
    "    upgrade-system       - Incremental build and upgrade using pkg list\n"
    "                           from local system, then upgrade the local\n"
    "                           system.\n"
    "    prepare-system       - 'upgrade-system' but stops after building\n"
    "    rebuild-repository   - Rebuild database files for current repository\n"
    "    purge-distfiles      - Delete obsolete source distribution files\n"
    "    status-everything    - Dry-run of 'everything'\n"
    "    everything           - Build entire dports tree and repo database\n"
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
    "    monitor    [datfile] - Monitor a running dsynth\n"
    "\n"
    "    [ports] is a space-delimited list of origins, e.g. editors/joe.  It\n"
    "            may also be a path to a file containing one origin per line.\n"
	);

	exit(ecode);
}

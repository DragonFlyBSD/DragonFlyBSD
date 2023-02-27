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

int UseCCache;
int UseUsrSrc;
int UseTmpfs;
int NumCores = 1;
int MaxBulk = 8;
int MaxWorkers = 8;
int MaxJobs = 8;
int UseTmpfsWork = 1;
int UseTmpfsBase = 1;
int UseNCurses = -1;		/* indicates default operation (enabled) */
int LeveragePrebuilt = 0;
int WorkerProcFlags = 0;
int DeleteObsoletePkgs;
long PhysMem;
const char *OperatingSystemName = "Unknown";	/* e.g. "DragonFly" */
const char *ArchitectureName = "unknown";	/* e.g. "x86_64" */
const char *MachineName = "unknown";		/* e.g. "x86_64" */
const char *VersionName = "unknown";		/* e.g. "DragonFly 5.7-SYNTH" */
const char *VersionOnlyName = "unknown";	/* e.g. "5.7-SYNTH" */
const char *VersionFromParamHeader = "unknown";	/* e.g. "500704" */
const char *VersionFromSysctl = "unknown";	/* e.g. "500704" */
const char *ReleaseName = "unknown";		/* e.g. "5.7" */
const char *DPortsPath = "/usr/dports";
const char *CCachePath = DISABLED_STR;
const char *PackagesPath = "/build/synth/live_packages";
const char *RepositoryPath = "/build/synth/live_packages/All";
const char *OptionsPath = "/build/synth/options";
const char *DistFilesPath = "/build/synth/distfiles";
const char *BuildBase = "/build/synth/build";
const char *LogsPath = "/build/synth/logs";
const char *SystemPath = "/";
const char *UsePkgSufx = USE_PKG_SUFX;
char *StatsBase;
char *StatsFilePath;
char *StatsLockPath;
static const char *ProfileLabel = "[LiveSystem]"; /* with the brackets */
const char *Profile = "LiveSystem";		/* without the brackets */
int MetaVersion = 2;

/*
 * Hooks are scripts in ConfigBase
 */
int UsingHooks;
const char *HookRunStart;
const char *HookRunEnd;
const char *HookPkgSuccess;
const char *HookPkgFailure;
const char *HookPkgIgnored;
const char *HookPkgSkipped;

const char *ConfigBase;				/* The config base we found */
const char *ConfigBase1 = "/etc/dsynth";
const char *ConfigBase2 = "/usr/local/etc/dsynth";

static void parseConfigFile(const char *path);
static void parseProfile(const char *cpath, const char *path);
static char *stripwhite(char *str);
static int truefalse(const char *str);
static char *dokernsysctl(int m1, int m2);
static void getElfInfo(const char *path);
static char *checkhook(const char *scriptname);

void
ParseConfiguration(int isworker)
{
	struct stat st;
	size_t len;
	int reln;
	char *synth_config;
	char *buf;
	char *osreldate;

	/*
	 * Get the default OperatingSystemName, ArchitectureName, and
	 * ReleaseName.
	 */
	OperatingSystemName = dokernsysctl(CTL_KERN, KERN_OSTYPE);
	ArchitectureName = dokernsysctl(CTL_HW, HW_MACHINE_ARCH);
	MachineName = dokernsysctl(CTL_HW, HW_MACHINE);
	ReleaseName = dokernsysctl(CTL_KERN, KERN_OSRELEASE);
	asprintf(&osreldate, "%d", getosreldate());
	VersionFromSysctl = osreldate;

	/*
	 * Retrieve resource information from the system.  Note that
	 * NumCores and PhysMem will also be used for dynamic load
	 * management.
	 */
	NumCores = 1;
	len = sizeof(NumCores);
	if (sysctlbyname("hw.ncpu", &NumCores, &len, NULL, 0) < 0)
		dfatal_errno("Cannot get hw.ncpu");

	len = sizeof(PhysMem);
	if (sysctlbyname("hw.physmem", &PhysMem, &len, NULL, 0) < 0)
		dfatal_errno("Cannot get hw.physmem");

	/*
	 * Calculate nominal defaults.
	 */
	MaxBulk = NumCores;
	MaxWorkers = MaxBulk / 2;
	if (MaxWorkers > (int)((PhysMem + (ONEGB/2)) / ONEGB))
		MaxWorkers = (PhysMem + (ONEGB/2)) / ONEGB;

	if (MaxBulk < 1)
		MaxBulk = 1;
	if (MaxWorkers < 1)
		MaxWorkers = 1;
	if (MaxJobs < 1)
		MaxJobs = 1;

	/*
	 * Configuration file must exist.  Look for it in
	 * "/etc/dsynth" and "/usr/local/etc/dsynth".
	 */
	ConfigBase = ConfigBase1;
	asprintf(&synth_config, "%s/dsynth.ini", ConfigBase1);
	if (stat(synth_config, &st) < 0) {
		ConfigBase = ConfigBase2;
		asprintf(&synth_config, "%s/dsynth.ini", ConfigBase2);
	}

	if (stat(synth_config, &st) < 0) {
		dfatal("Configuration file missing, "
		       "could not find %s/dsynth.ini or %s/dsynth.ini\n",
		       ConfigBase1,
		       ConfigBase2);
	}

	/*
	 * Check to see what hooks we have
	 */
	HookRunStart = checkhook("hook_run_start");
	HookRunEnd = checkhook("hook_run_end");
	HookPkgSuccess = checkhook("hook_pkg_success");
	HookPkgFailure = checkhook("hook_pkg_failure");
	HookPkgIgnored = checkhook("hook_pkg_ignored");
	HookPkgSkipped = checkhook("hook_pkg_skipped");

	/*
	 * Parse the configuration file(s).  This may override some of
	 * the above defaults.
	 */
	parseConfigFile(synth_config);
	parseProfile(synth_config, ProfileLabel);

	/*
	 * Figure out whether CCache is configured.  Also set UseUsrSrc
	 * if it exists under the system path.
	 *
	 * Not supported for the moment
	 */
	if (strcmp(CCachePath, "disabled") != 0) {
		/* dfatal("Directory_ccache is not supported, please\n"
		       " set to 'disabled'\n"); */
		UseCCache = 1;
	}
	asprintf(&buf, "%s/usr/src/sys/Makefile", SystemPath);
	if (stat(buf, &st) == 0)
		UseUsrSrc = 1;
	free(buf);

	/*
	 * Default pkg dependency memory target.  This is a heuristical
	 * calculation for how much memory we are willing to put towards
	 * pkg install dependencies.  The builder count is reduced as needed.
	 *
	 * Reduce the target even further when CCache is enabled due to
	 * its added overhead (even though it doesn't use tmpfs).
	 * (NOT CURRENTLY IMPLEMENTED, LEAVE THE SAME)
	 */
	if (PkgDepMemoryTarget == 0) {
		if (UseCCache)
			PkgDepMemoryTarget = PhysMem / 3;
		else
			PkgDepMemoryTarget = PhysMem / 3;
	}

	/*
	 * If this is a dsynth WORKER exec it handles a single slot,
	 * just set MaxWorkers to 1.
	 */
	if (isworker)
		MaxWorkers = 1;

	/*
	 * Final check
	 */
	if (stat(DPortsPath, &st) < 0)
		dfatal("Directory missing: %s", DPortsPath);
	if (stat(PackagesPath, &st) < 0)
		dfatal("Directory missing: %s", PackagesPath);
	if (stat(OptionsPath, &st) < 0)
		dfatal("Directory missing: %s", OptionsPath);
	if (stat(DistFilesPath, &st) < 0)
		dfatal("Directory missing: %s", DistFilesPath);
	if (stat(BuildBase, &st) < 0)
		dfatal("Directory missing: %s", BuildBase);
	if (stat(LogsPath, &st) < 0)
		dfatal("Directory missing: %s", LogsPath);
	if (stat(SystemPath, &st) < 0)
		dfatal("Directory missing: %s", SystemPath);
	if (UseCCache && stat(CCachePath, &st) < 0)
		dfatal("Directory missing: %s", CCachePath);

	/*
	 * Now use the SystemPath to retrieve file information from /bin/sh,
	 * and use this to set OperatingSystemName, ArchitectureName,
	 * MachineName, and ReleaseName.
	 *
	 * Since this method is used to build for specific releases, require
	 * that it succeed.
	 */
	asprintf(&buf, "%s/bin/sh", SystemPath);
	getElfInfo(buf);
	free(buf);

	/*
	 * Calculate VersionName from OperatingSystemName and ReleaseName.
	 */
	if (strchr(ReleaseName, '-')) {
		reln = strchr(ReleaseName, '-') - ReleaseName;
		asprintf(&buf, "%s %*.*s-SYNTH",
			 OperatingSystemName,
			 reln, reln, ReleaseName);
		VersionName = buf;
		asprintf(&buf, "%*.*s-SYNTH", reln, reln, ReleaseName);
		VersionOnlyName = buf;
	} else {
		asprintf(&buf, "%s %s-SYNTH",
			 OperatingSystemName,
			 ReleaseName);
		asprintf(&buf, "%s-SYNTH", ReleaseName);
		VersionOnlyName = buf;
	}

	/*
	 * Get __DragonFly_version from the system header via SystemPath
	 */
	{
		char *ptr;
		FILE *fp;

		asprintf(&buf, "%s/usr/include/sys/param.h", SystemPath);
		fp = fopen(buf, "r");
		if (fp == NULL)
			dpanic_errno("Cannot open %s", buf);
		while ((ptr = fgetln(fp, &len)) != NULL) {
			if (len == 0 || ptr[len-1] != '\n')
				continue;
			ptr[len-1] = 0;
			if (strncmp(ptr, "#define __DragonFly_version", 27))
				continue;
			ptr += 27;
			ptr = strtok(ptr, " \t\r\n");
			VersionFromParamHeader = strdup(ptr);
			break;
		}
		fclose(fp);

		/* Warn the user that World/kernel are out of sync */
		if (strcmp(VersionFromSysctl, VersionFromParamHeader)) {
			dlog(DLOG_ALL, "Kernel (%s) out of sync with world (%s) on %s\n",
			    VersionFromSysctl, VersionFromParamHeader, SystemPath);
		}
	}

	/*
	 * If RepositoryPath is under PackagesPath, make sure it
	 * is created.
	 */
	if (strncmp(RepositoryPath, PackagesPath, strlen(PackagesPath)) == 0) {
		if (stat(RepositoryPath, &st) < 0) {
			if (mkdir(RepositoryPath, 0755) < 0)
				dfatal_errno("Cannot mkdir '%s'",
					     RepositoryPath);
		}
	}

	if (stat(RepositoryPath, &st) < 0)
		dfatal("Directory missing: %s", RepositoryPath);

	/*
	 * StatsBase, StatsFilePath, StatsLockPath
	 */
	asprintf(&StatsBase, "%s/stats", LogsPath);
	asprintf(&StatsFilePath, "%s/monitor.dat", StatsBase);
	asprintf(&StatsLockPath, "%s/monitor.lk", StatsBase);
}

void
DoConfigure(void)
{
	dfatal("Not Implemented");
}

static void
parseConfigFile(const char *path)
{
	char buf[1024];
	char copy[1024];
	FILE *fp;
	char *l1;
	char *l2;
	size_t len;
	int mode = -1;
	int lineno = 0;

	fp = fopen(path, "r");
	if (fp == NULL) {
		ddprintf(0, "Warning: Config file %s does not exist\n", path);
		return;
	}
	if (DebugOpt >= 2)
		ddprintf(0, "ParseConfig %s\n", path);

	if (ProfileOverrideOpt) {
		Profile = strdup(ProfileOverrideOpt);
		asprintf(&l2, "[%s]", Profile);
		ProfileLabel = l2;
	}

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		++lineno;
		len = strlen(buf);
		if (len == 0 || buf[len-1] != '\n')
			continue;
		buf[--len] = 0;

		/*
		 * Remove any trailing whitespace, ignore empty lines.
		 */
		while (len > 0 && isspace(buf[len-1]))
			--len;
		if (len == 0)
			continue;
		buf[len] = 0;

		/*
		 * ignore comments
		 */
		if (buf[0] == ';' || buf[0] == '#')
			continue;
		if (buf[0] == '[') {
			if (strcmp(buf, "[Global Configuration]") == 0)
				mode = 0;	/* parse global config */
			else if (strcmp(buf, ProfileLabel) == 0)
				mode = 1;	/* use profile */
			else
				mode = -1;	/* ignore profile */
			continue;
		}

		bcopy(buf, copy, len + 1);

		l1 = strtok(copy, "=");
		if (l1 == NULL) {
			dfatal("Syntax error in config line %d: %s\n",
			       lineno, buf);
		}
		l2 = strtok(NULL, " \t\n");
		if (l2 == NULL) {
			dfatal("Syntax error in config line %d: %s\n",
			       lineno, buf);
		}
		l1 = stripwhite(l1);
		l2 = stripwhite(l2);

		switch(mode) {
		case 0:
			/*
			 * Global Configuration
			 */
			if (strcmp(l1, "profile_selected") == 0) {
				if (ProfileOverrideOpt == NULL) {
					Profile = strdup(l2);
					asprintf(&l2, "[%s]", l2);
					ProfileLabel = l2;
				}
			} else {
				dfatal("Unknown directive in config "
				       "line %d: %s\n", lineno, buf);
			}
			break;
		case 1:
			/*
			 * Selected Profile
			 */
			l2 = strdup(l2);
			if (strcmp(l1, "Operating_system") == 0) {
				OperatingSystemName = l2;
			} else if (strcmp(l1, "Directory_packages") == 0) {
				PackagesPath = l2;
			} else if (strcmp(l1, "Directory_repository") == 0) {
				RepositoryPath = l2;
			} else if (strcmp(l1, "Directory_portsdir") == 0) {
				DPortsPath = l2;
			} else if (strcmp(l1, "Directory_options") == 0) {
				OptionsPath = l2;
			} else if (strcmp(l1, "Directory_distfiles") == 0) {
				DistFilesPath = l2;
			} else if (strcmp(l1, "Directory_buildbase") == 0) {
				BuildBase = l2;
			} else if (strcmp(l1, "Directory_logs") == 0) {
				LogsPath = l2;
			} else if (strcmp(l1, "Directory_ccache") == 0) {
				CCachePath = l2;
			} else if (strcmp(l1, "Directory_system") == 0) {
				SystemPath = l2;
			} else if (strcmp(l1, "Package_suffix") == 0) {
				UsePkgSufx = l2;
				dassert(strcmp(l2, ".tgz") == 0 ||
					strcmp(l2, ".tar") == 0 ||
					strcmp(l2, ".txz") == 0 ||
					strcmp(l2, ".tzst") == 0 ||
					strcmp(l2, ".tbz") == 0,
					"Config: Unknown Package_suffix,"
					"specify .tgz .tar .txz .tbz or .tzst");
			} else if (strcmp(l1, "Meta_version") == 0) {
				MetaVersion = strtol(l2, NULL, 0);
			} else if (strcmp(l1, "Number_of_builders") == 0) {
				MaxWorkers = strtol(l2, NULL, 0);
				if (MaxWorkers == 0)
					MaxWorkers = NumCores / 2 + 1;
				else
				if (MaxWorkers < 0 || MaxWorkers > MAXWORKERS) {
					dfatal("Config: Number_of_builders "
					       "must range %d..%d",
					       1, MAXWORKERS);
				}
				free(l2);
			} else if (strcmp(l1, "Max_jobs_per_builder") == 0) {
				MaxJobs = strtol(l2, NULL, 0);
				if (MaxJobs == 0) {
					MaxJobs = NumCores;
				} else
				if (MaxJobs < 0 || MaxJobs > MAXJOBS) {
					dfatal("Config: Max_jobs_per_builder "
					       "must range %d..%d",
					       1, MAXJOBS);
				}
				free(l2);
			} else if (strcmp(l1, "Tmpfs_workdir") == 0) {
				UseTmpfsWork = truefalse(l2);
				dassert(UseTmpfsWork == 1,
					"Config: Tmpfs_workdir must be "
					"set to true, 'false' not supported");
			} else if (strcmp(l1, "Tmpfs_localbase") == 0) {
				UseTmpfsBase = truefalse(l2);
				dassert(UseTmpfsBase == 1,
					"Config: Tmpfs_localbase must be "
					"set to true, 'false' not supported");
			} else if (strcmp(l1, "Display_with_ncurses") == 0) {
				if (UseNCurses == -1)
					UseNCurses = truefalse(l2);
			} else if (strcmp(l1, "leverage_prebuilt") == 0) {
				LeveragePrebuilt = truefalse(l2);
				dassert(LeveragePrebuilt == 0,
					"Config: leverage_prebuilt not "
					"supported and must be set to false");
			} else if (strcmp(l1, "Check_plist") == 0) {
				if (truefalse(l2)) {
					WorkerProcFlags |=
						WORKER_PROC_CHECK_PLIST;
				}
			} else if (strcmp(l1, "Numa_setsize") == 0) {
				NumaSetSize = strtol(l2, NULL, 0);
				free(l2);
			} else {
				dfatal("Unknown directive in profile section "
				       "line %d: %s\n", lineno, buf);
			}
			break;
		default:
			/*
			 * Ignore unselected profile
			 */
			break;
		}
	}
	fclose(fp);
}

/*
 * NOTE: profile has brackets, e.g. "[LiveSystem]".
 */
static void
parseProfile(const char *cpath, const char *profile)
{
	char buf[1024];
	char copy[1024];
	char *ppath;
	FILE *fp;
	char *l1;
	char *l2;
	int len;
	int plen;
	int lineno = 0;

	len = strlen(cpath);
	while (len && cpath[len-1] != '/')
		--len;
	if (len == 0)
		++len;
	plen = strlen(profile);
	ddassert(plen > 2 && profile[0] == '[' && profile[plen-1] == ']');

	asprintf(&ppath, "%*.*s%*.*s-make.conf",
		 len, len, cpath, plen - 2, plen - 2, profile + 1);
	fp = fopen(ppath, "r");
	if (fp == NULL) {
		ddprintf(0, "Warning: Profile %s does not exist\n", ppath);
		return;
	}
	if (DebugOpt >= 2)
		ddprintf(0, "ParseProfile %s\n", ppath);
	free(ppath);

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		++lineno;
		len = strlen(buf);
		if (len == 0 || buf[len-1] != '\n')
			continue;
		buf[--len] = 0;

		/*
		 * Remove any trailing whitespace, ignore empty lines.
		 */
		while (len > 0 && isspace(buf[len-1]))
			--len;
		buf[len] = 0;
		stripwhite(buf);

		/*
		 * Allow empty lines, ignore comments.
		 */
		len = strlen(buf);
		if (len == 0)
			continue;
		if (buf[0] == ';' || buf[0] == '#')
			continue;

		/*
		 * Require env variable name
		 */
		bcopy(buf, copy, len + 1);
		l1 = strtok(copy, "=");
		if (l1 == NULL) {
			dfatal("Syntax error in profile line %d: %s\n",
			       lineno, buf);
		}

		/*
		 * Allow empty assignment
		 */
		l2 = strtok(NULL, " \t\n");
		if (l2 == NULL)
			l2 = l1 + strlen(l1);

		l1 = stripwhite(l1);
		l2 = stripwhite(l2);

		/*
		 * Add to builder environment
		 */
		addbuildenv(l1, l2, BENV_MAKECONF);
		if (DebugOpt >= 2)
			ddprintf(4, "%s=%s\n", l1, l2);
	}
	fclose(fp);
	if (DebugOpt >= 2)
		ddprintf(0, "ParseProfile finished\n");
}

static char *
stripwhite(char *str)
{
	size_t len;

	len = strlen(str);
	while (len > 0 && isspace(str[len-1]))
		--len;
	str[len] =0;

	while (*str && isspace(*str))
		++str;
	return str;
}

static int
truefalse(const char *str)
{
	if (strcmp(str, "0") == 0)
		return 0;
	if (strcmp(str, "1") == 0)
		return 1;
	if (strcasecmp(str, "false") == 0)
		return 0;
	if (strcasecmp(str, "true") == 0)
		return 1;
	dfatal("syntax error for boolean '%s': "
	       "must be '0', '1', 'false', or 'true'", str);
	return 0;
}

static char *
dokernsysctl(int m1, int m2)
{
	int mib[] = { m1, m2 };
	char buf[1024];
	size_t len;

	len = sizeof(buf) - 1;
	if (sysctl(mib, 2, buf, &len, NULL, 0) < 0)
		dfatal_errno("sysctl for system/architecture");
	buf[len] = 0;
	return(strdup(buf));
}

struct NoteTag {
	Elf_Note note;
	char osname1[12];
	int version;		/* e.g. 500702 -> 5.7 */
	int x1;
	int x2;
	int x3;
	char osname2[12];
	int zero;
};

static void
getElfInfo(const char *path)
{
	struct NoteTag note;
	char *cmd;
	char *base;
	FILE *fp;
	size_t size;
	size_t n;
	int r;
	uint32_t addr;
	uint32_t v[4];

	asprintf(&cmd, "readelf -x .note.tag %s", path);
	fp = popen(cmd, "r");
	dassert_errno(fp, "Cannot run: %s", cmd);
	n = 0;

	while (n != sizeof(note) &&
	       (base = fgetln(fp, &size)) != NULL && size) {
		base[--size] = 0;
		if (strncmp(base, "  0x", 3) != 0)
			continue;
		r = sscanf(base, "%x %x %x %x %x",
			   &addr, &v[0], &v[1], &v[2], &v[3]);
		v[0] = ntohl(v[0]);
		v[1] = ntohl(v[1]);
		v[2] = ntohl(v[2]);
		v[3] = ntohl(v[3]);
		if (r < 2)
			continue;
		r = (r - 1) * sizeof(v[0]);
		if (n + r > sizeof(note))
			r = sizeof(note) - n;
		bcopy((char *)v, (char *)&note + n, r);
		n += r;
	}
	pclose(fp);

	if (n != sizeof(note))
		dfatal("Unable to parse output from: %s", cmd);
	if (strncmp(OperatingSystemName, note.osname1, sizeof(note.osname1))) {
		dfatal("%s ELF, mismatch OS name %.*s vs %s",
		       path, (int)sizeof(note.osname1),
		       note.osname1, OperatingSystemName);
	}
	free(cmd);
	if (note.version) {
		asprintf(&cmd, "%d.%d",
			note.version / 100000,
			(note.version % 100000) / 100);
	} else if (note.zero) {
		asprintf(&cmd, "%d.%d",
			note.zero / 100000,
			(note.zero % 100000) / 100);
	} else {
		dfatal("%s ELF, cannot extract version info", path);
	}
	ReleaseName = cmd;
}

static char *
checkhook(const char *scriptname)
{
	struct stat st;
	char *path;

	asprintf(&path, "%s/%s", ConfigBase, scriptname);
	if (stat(path, &st) < 0 || (st.st_mode & 0111) == 0) {
		free(path);
		return NULL;
	}
	UsingHooks = 1;

	return path;
}

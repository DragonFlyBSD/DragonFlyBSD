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
int UseTmpfs;
int NumCores = 1;
int MaxBulk = 8;
int MaxWorkers = 8;
int MaxJobs = 8;
int UseTmpfsWork = 1;
int UseTmpfsBase = 1;
int UseNCurses = 1;
int LeveragePrebuilt = 0;
size_t PhysMem;
const char *OperatingSystemName = "Unknown";
const char *ArchitectureName = "unknown";
const char *MachineName = "unknown";
const char *VersionName = "unknown";
const char *ReleaseName = "unknown";
const char *DPortsPath = "/usr/dports";
const char *CCachePath = "/build/ccache";
const char *PackagesPath = "/build/synth/live_packages";
const char *RepositoryPath = "/build/synth/live_packages/All";
const char *OptionsPath = "/build/synth/options";
const char *DistFilesPath = "/build/synth/distfiles";
const char *BuildBase = "/build/synth/build";
const char *LogsPath = "/build/synth/logs";
const char *SystemPath = "/";
const char *ProfileLabel = "[LiveSystem]";

const char *ConfigBase = "/etc/dsynth";
const char *AltConfigBase = "/usr/local/etc/dsynth";

static void parseConfigFile(const char *path);
static void parseProfile(const char *cpath, const char *path);
static char *stripwhite(char *str);
static int truefalse(const char *str);
static char *dokernsysctl(int m1, int m2);

void
ParseConfiguration(int isworker)
{
	struct stat st;
	size_t len;
	char *synth_config;

	/*
	 *
	 */
	asprintf(&synth_config, "%s/dsynth.ini", ConfigBase);
	if (stat(synth_config, &st) < 0)
		asprintf(&synth_config, "%s/dsynth.ini", AltConfigBase);

	/*
	 * OperatingSystemName, ArchitectureName, ReleaseName
	 */
	OperatingSystemName = dokernsysctl(CTL_KERN, KERN_OSTYPE);
	ArchitectureName = dokernsysctl(CTL_HW, HW_MACHINE_ARCH);
	MachineName = dokernsysctl(CTL_HW, HW_MACHINE);
	ReleaseName = dokernsysctl(CTL_KERN, KERN_OSRELEASE);
	VersionName = dokernsysctl(CTL_KERN, KERN_VERSION);

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
	 * Calculate nominal defaults
	 */
	MaxBulk = NumCores;
	MaxWorkers = MaxBulk / 2;
	if (MaxWorkers > (int)(PhysMem / 1000000000))
		MaxWorkers = PhysMem / 1000000000;

	if (MaxBulk < 1)
		MaxBulk = 1;
	if (MaxWorkers < 1)
		MaxWorkers = 1;
	if (MaxJobs < 1)
		MaxJobs = 1;

	/*
	 * Configuration file must exist
	 */
	if (stat(synth_config, &st) < 0) {
		dfatal("Configuration file missing, "
		       "could not find %s/dsynth.ini or %s/dsynth.ini\n",
		       ConfigBase,
		       AltConfigBase);
	}

	/*
	 * Parse the configuration file(s)
	 */
	parseConfigFile(synth_config);
	parseProfile(synth_config, ProfileLabel);

	/*
	 * If this is a dsynth WORKER exec it handles a single slot,
	 * just set MaxWorkers to 1.
	 */
	if (isworker)
		MaxWorkers = 1;

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
		l1 = stripwhite(l1);
		l2 = stripwhite(l2);

		switch(mode) {
		case 0:
			/*
			 * Global Configuration
			 */
			if (strcmp(l1, "profile_selected") == 0) {
				asprintf(&l2, "[%s]", l2);
				ProfileLabel = l2;
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
				UseNCurses = truefalse(l2);
			} else if (strcmp(l1, "leverage_prebuilt") == 0) {
				LeveragePrebuilt = truefalse(l2);
				dassert(LeveragePrebuilt == 0,
					"Config: leverage_prebuilt not "
					"supported and must be set to false");
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
		len = strlen(buf);
		if (len == 0)
			continue;

		/*
		 * Ignore comments.
		 */
		if (buf[0] == ';' || buf[0] == '#')
			continue;

		bcopy(buf, copy, len + 1);
		l1 = strtok(copy, "=");
		if (l1 == NULL) {
			dfatal("Syntax error in profile line %d: %s\n",
			       lineno, buf);
		}
		l2 = strtok(NULL, " \t\n");
		if (l2 == NULL) {
			dfatal("Syntax error in profile line %d: %s\n",
			       lineno, buf);
		}
		l1 = stripwhite(l1);
		l2 = stripwhite(l2);

		/*
		 * Add to builder environment
		 */
		addbuildenv(l1, l2);
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

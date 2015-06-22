/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
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
 */

#include "hammer.h"
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

static void config_get(const char *dirpath, struct hammer_ioc_config *config);
static void config_set(const char *dirpath, struct hammer_ioc_config *config);
static void config_remove_path(void);

char *ConfigPath;

/*
 * hammer config [<fs> [configfile]]
 *
 * Prints out the hammer cleanup configuration for the specified HAMMER
 * filesystem(s) or the current filesystem.
 */
void
hammer_cmd_config(char **av, int ac)
{
	struct hammer_ioc_config config;
	char *dirpath;
	ssize_t n;
	int fd;

	bzero(&config, sizeof(config));
	if (ac == 0) {
		config_get(".", &config);
		if (config.head.error == 0) {
			printf("%s", config.config.text);
		} else {
			errx(2, "hammer config: no configuration found");
			/* not reached */
		}
		return;
	}
	dirpath = av[0];
	if (ac == 1) {
		config_get(dirpath, &config);
		if (config.head.error == 0) {
			printf("%s", config.config.text);
		} else {
			errx(2, "hammer config: no configuration found");
			/* not reached */
		}
		return;
	}
	config_get(dirpath, &config);	/* ignore errors */
	config.head.error = 0;

	fd = open(av[1], O_RDONLY);
	if (fd < 0) {
		err(2, "hammer config: %s", av[1]);
		/* not reached */
	}
	n = read(fd, config.config.text, sizeof(config.config.text) - 1);
	if (n == sizeof(config.config.text) - 1) {
		err(2, "hammer config: config file too big, limit %zu bytes",
		    sizeof(config.config.text) - 1);
		/* not reached */
	}
	bzero(config.config.text + n, sizeof(config.config.text) - n);
	config_set(dirpath, &config);
	close(fd);
}

/*
 * hammer viconfig [<fs>]
 */
void
hammer_cmd_viconfig(char **av, int ac)
{
	struct hammer_ioc_config config;
	struct timeval times[2];
	const char *dirpath;
	struct stat st;
	char *runcmd, *editor, *tmp;
	char path[32];
	ssize_t n;
	int fd;

	if (ac > 1) {
		errx(1, "hammer viconfig: 0 or 1 argument (<fs>) only");
		/* not reached */
	}
	if (ac == 0)
		dirpath = ".";
	else
		dirpath = av[0];
	config_get(dirpath, &config);
	if (config.head.error == ENOENT) {
		snprintf(config.config.text, sizeof(config.config.text),
			"%s",
			"# No configuration present, here are some defaults\n"
			"# you can uncomment.  Also remove these instructions\n"
			"#\n"
                        "#snapshots 1d 60d\n"
                        "#prune     1d 5m\n"
                        "#rebalance 1d 5m\n"
                        "#dedup     1d 5m\n"
                        "#reblock   1d 5m\n"
                        "#recopy    30d 10m\n");
		config.head.error = 0;
	}
	if (config.head.error) {
		errx(2, "hammer viconfig: read config failed error: %s",
			strerror(config.head.error));
		/* not reached */
	}

	/*
	 * Edit a temporary file and write back if it was modified.
	 * Adjust the mtime back one second so a quick edit is not
	 * improperly detected as not having been modified.
	 */
	snprintf(path, sizeof(path), "/tmp/configXXXXXXXXXX");
	mkstemp(path);
	ConfigPath = path;
	atexit(config_remove_path);

	fd = open(path, O_RDWR|O_CREAT|O_TRUNC, 0600);
	if (fd < 0)
		err(2, "hammer viconfig: creating temporary file %s", path);
	write(fd, config.config.text, strlen(config.config.text));
	if (fstat(fd, &st) < 0)
		err(2, "hammer viconfig");
	times[0].tv_sec = st.st_mtime - 1;
	times[0].tv_usec = 0;
	times[1] = times[0];
	close(fd);
	utimes(path, times);

	if ((tmp = getenv("EDITOR")) != NULL ||
	    (tmp = getenv("VISUAL")) != NULL)
		editor = strdup(tmp);
	else
		editor = strdup("vi");

	asprintf(&runcmd, "%s %s", editor, path);
	system(runcmd);

	if (stat(path, &st) < 0)
		err(2, "hammer viconfig: unable to stat file after vi");
	if (times[0].tv_sec == st.st_mtime) {
		printf("hammer viconfig: no changes were made\n");
		remove(path);
		return;
	}
	fd = open(path, O_RDONLY);
	if (fd < 0)
		err(2, "hammer viconfig: unable to read %s", path);
	remove(path);
	n = read(fd, config.config.text, sizeof(config.config.text) - 1);
	if (n < 0)
		err(2, "hammer viconfig: unable to read %s", path);
	if (n == sizeof(config.config.text) - 1) {
		err(2, "hammer config: config file too big, limit %zu bytes",
		    sizeof(config.config.text) - 1);
		/* not reached */
	}
	bzero(config.config.text + n, sizeof(config.config.text) - n);
	config_set(dirpath, &config);
	free(editor);
	free(runcmd);
}

static void
config_get(const char *dirpath, struct hammer_ioc_config *config)
{
	struct hammer_ioc_version version;
	int fd;

	bzero(&version, sizeof(version));
	if ((fd = open(dirpath, O_RDONLY)) < 0)
		err(2, "hammer config: unable to open directory %s", dirpath);
	if (ioctl(fd, HAMMERIOC_GET_VERSION, &version) < 0)
		errx(2, "hammer config: not a HAMMER filesystem!");

	if (ioctl(fd, HAMMERIOC_GET_CONFIG, config) < 0)
		errx(2, "hammer config: config_get");
	close(fd);
}

static void
config_set(const char *dirpath, struct hammer_ioc_config *config)
{
	struct hammer_ioc_version version;
	int fd;

	bzero(&version, sizeof(version));
	if ((fd = open(dirpath, O_RDONLY)) < 0)
		errx(2, "hammer config: unable to open directory %s", dirpath);
	if (ioctl(fd, HAMMERIOC_GET_VERSION, &version) < 0)
		errx(2, "hammer config: not a HAMMER filesystem!");
	if (ioctl(fd, HAMMERIOC_SET_CONFIG, config) < 0)
		err(2, "hammer config");
	close(fd);
}

static void
config_remove_path(void)
{
	remove(ConfigPath);
}

/*
 * Copyright (c) 2013 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
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

#include "hammer2.h"

static int cmd_setcheck_core(uint8_t check_algo, const char *path_str,
			    struct stat *st);

int
cmd_setcheck(const char *check_str, char **paths)
{
	static const char *checks[] = HAMMER2_CHECK_STRINGS;
	struct stat st;
	int check_algo;
	int ecode;
	int res;

	ecode = 0;

	if (isdigit(check_str[0])) {
		check_algo = strtol(check_str, NULL, 0);
	} else {
		check_algo = HAMMER2_CHECK_STRINGS_COUNT;
		while (--check_algo >= 0) {
			if (strcasecmp(check_str, checks[check_algo]) == 0)
				break;
		}
		if (check_algo < 0 && strcasecmp(check_str, "default") == 0) {
			check_algo = HAMMER2_CHECK_ISCSI32;
			check_str = "crc32";
		}
		if (check_algo < 0 && strcasecmp(check_str, "disabled") == 0) {
			check_algo = HAMMER2_CHECK_DISABLED;
			check_str = "disabled";
		}
		if (check_algo < 0) {
			fprintf(stderr,
				"Unknown check code type: %s\n",
				check_str);
			ecode = 3;
		}
	}

	if (ecode == 0) {
		while (*paths) {
			if (lstat(*paths, &st) == 0) {
				res = cmd_setcheck_core(
					HAMMER2_ENC_ALGO(check_algo),
				        *paths,
					&st);
				if (res)
					ecode = res;
			} else {
				printf("%s: %s\n", *paths, strerror(errno));
				ecode = 3;
			}
			++paths;
		}
	}

	return ecode;
}

static int
cmd_setcheck_core(uint8_t check_algo, const char *path_str, struct stat *st)
{
	hammer2_ioc_inode_t inode;
	int fd;
	int res;

	fd = hammer2_ioctl_handle(path_str);
	if (fd < 0) {
		res = 3;
		goto failed;
	}
	res = ioctl(fd, HAMMER2IOC_INODE_GET, &inode);
	if (res < 0) {
		fprintf(stderr,
			"%s: HAMMER2IOC_INODE_GET: error %s\n",
			path_str, strerror(errno));
		res = 3;
		goto failed;
	}
	printf("%s\tcheck_algo=0x%02x\n", path_str, check_algo);
	inode.ip_data.meta.check_algo = check_algo;
	res = ioctl(fd, HAMMER2IOC_INODE_SET, &inode);
	if (res < 0) {
		fprintf(stderr,
			"%s: HAMMER2IOC_INODE_SET: error %s\n",
			path_str, strerror(errno));
		res = 3;
		goto failed;
	}
	res = 0;

	if (RecurseOpt && S_ISDIR(st->st_mode)) {
		DIR *dir;
		char *path;
		struct dirent *den;

		if ((dir = fdopendir(fd)) != NULL) {
			while ((den = readdir(dir)) != NULL) {
				if (strcmp(den->d_name, ".") == 0 ||
				    strcmp(den->d_name, "..") == 0) {
					continue;
				}
				asprintf(&path, "%s/%s", path_str, den->d_name);
				if (lstat(path, st) == 0)
					cmd_setcheck_core(check_algo, path, st);
				free(path);
			}
			closedir(dir);
		}
	}
failed:
	close(fd);
	return res;
}

/*
 * Copyright (c) 2026 The DragonFly Project.  All rights reserved.
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

#include <sys/param.h>
#include <stand.h>
#include <string.h>
#include "bootstrap.h"
#include "dloader.h"

/*
 * The first line of nextboot.conf is overwritten in place with this.
 * Both markers are 21 bytes; the trailing space pads the shorter one.
 * A longer first line is padded with spaces, so only that line is
 * touched.
 */
static char nextboot_marker[] = "nextboot_enable=\"NO\" ";

#define NEXTBOOT_BUFSZ	1024

/*
 * Set by command_menu() when the boot is unattended: the countdown ran
 * out or Enter was pressed.  Any other key is a manual choice, which
 * consumes the entry without applying it.
 */
int nextboot_autoboot;

/*
 * Parse one "var=value" line.  The value may be quoted.  Returns 0 and
 * fills in name and value, or -1 if the line is not an assignment.
 */
static int
nextboot_parse_line(char *line, char **namep, char **valuep)
{
	char *end, *eq, *name, *p, *value;

	p = line;
	while (*p == ' ' || *p == '\t')
		++p;
	if (*p == '\0' || *p == '\n' || *p == '#')
		return (-1);

	eq = strchr(p, '=');
	if (eq == NULL)
		return (-1);
	name = p;
	*eq = '\0';
	value = eq + 1;

	end = name + strlen(name);
	while (end > name && (end[-1] == ' ' || end[-1] == '\t'))
		*--end = '\0';
	if (*name == '\0')
		return (-1);

	while (*value == ' ' || *value == '\t')
		++value;
	end = value + strlen(value);
	while (end > value && (end[-1] == '\n' || end[-1] == '\r' ||
	    end[-1] == ' ' || end[-1] == '\t'))
		*--end = '\0';
	if (*value == '"') {
		if (end - value < 2 || end[-1] != '"')
			return (-1);
		end[-1] = '\0';
		++value;
	}

	*namep = name;
	*valuep = value;
	return (0);
}

/*
 * A name has to survive the trip to the kernel environment, so only
 * accept the characters a loader.conf variable can have.
 */
static int
nextboot_name_ok(const char *name)
{
	for (; *name != '\0'; ++name) {
		if ((*name >= 'A' && *name <= 'Z') ||
		    (*name >= 'a' && *name <= 'z') ||
		    (*name >= '0' && *name <= '9') ||
		    *name == '_' || *name == '.')
			continue;
		return (0);
	}
	return (1);
}

static void
nextboot_file_path(const char *base, char *path, size_t pathsz)
{
	snprintf(path, pathsz, "%snextboot.conf", base);
}

/*
 * Read the entry from ${base}nextboot.conf.
 */
static int
nextboot_file_read(const char *base, char *buf, size_t bufsz, size_t *lenp,
    int *truncp, const char **srcp)
{
	static char path[MAXPATHLEN];
	size_t len;
	int c, error, fd, n;

	nextboot_file_path(base, path, sizeof(path));
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		error = errno;
		if (error != ENOENT)
			printf("nextboot: cannot open %s: %s\n", path,
			    strerror(error));
		return (error);
	}

	len = 0;
	while (len < bufsz) {
		n = read(fd, buf + len, bufsz - len);
		if (n < 0) {
			error = errno;
			printf("nextboot: cannot read %s: %s\n", path,
			    strerror(error));
			close(fd);
			return (error);
		}
		if (n == 0)
			break;
		len += n;
	}
	c = 0;
	*truncp = 0;
	if (len == bufsz && read(fd, &c, 1) == 1)
		*truncp = 1;	/* the file continues past the buffer */
	close(fd);
	buf[len] = '\0';
	*lenp = len;
	*srcp = path;
	return (0);
}

static int
nextboot_file_consume(const char *base, size_t len_first)
{
	static char path[MAXPATHLEN];
	static char first[NEXTBOOT_BUFSZ + 1];
	int error, fd;

	nextboot_file_path(base, path, sizeof(path));
	if (len_first < sizeof(nextboot_marker) - 1) {
		printf("nextboot: %s: first line must be "
		    "nextboot_enable=\"YES\", ignoring\n", path);
		return (EINVAL);
	}
	memcpy(first, nextboot_marker, sizeof(nextboot_marker) - 1);
	memset(first + sizeof(nextboot_marker) - 1, ' ',
	    len_first - (sizeof(nextboot_marker) - 1));

	fd = open(path, O_RDWR);
	if (fd < 0) {
		printf("nextboot: cannot open %s for writing: %s, ignoring\n",
		    path, strerror(errno));
		return (errno);
	}
	if (write(fd, first, len_first) < 0) {
		error = errno;
		printf("nextboot: cannot consume %s: %s, ignoring\n", path,
		    strerror(error));
		close(fd);
		return (error);
	}
	close(fd);
	return (0);
}

int
nextboot_file_armed(const char *base, char *path, size_t pathsz)
{
	static char buf[NEXTBOOT_BUFSZ + 1];
	char *name, *nl, *value;
	int fd, n;

	nextboot_file_path(base, path, pathsz);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return (0);
	n = read(fd, buf, NEXTBOOT_BUFSZ);
	close(fd);
	if (n <= 0)
		return (0);
	buf[n] = '\0';
	nl = strchr(buf, '\n');
	if (nl != NULL)
		*nl = '\0';
	return (nextboot_parse_line(buf, &name, &value) == 0 &&
	    strcmp(name, "nextboot_enable") == 0 &&
	    (strcmp(value, "YES") == 0 || strcmp(value, "yes") == 0));
}

void
nextboot_check(void)
{
	static char assign[NEXTBOOT_BUFSZ + 1];
	static char base[MAXPATHLEN];
	static char buf[NEXTBOOT_BUFSZ + 1];
	static char kpath[MAXPATHLEN];
	char *kernel, *name, *nl, *p, *value;
	const char *src;
	size_t len, len_first;
	int error, truncated;

	if (getenv("nextboot_enable") != NULL &&
	    strcmp(getenv("nextboot_enable"), "NO") == 0)
		return;

	p = getenv("base");
	if (p == NULL)
		p = "/";
	snprintf(base, sizeof(base), "%s", p);

	src = NULL;
	truncated = 0;
	if (archsw.arch_nextboot_read != NULL)
		error = archsw.arch_nextboot_read(base, buf, NEXTBOOT_BUFSZ,
		    &len, &truncated, &src);
	else
		error = nextboot_file_read(base, buf, NEXTBOOT_BUFSZ, &len,
		    &truncated, &src);
	if (error != 0)
		return;

	/*
	 * Only the first line decides whether nextboot is enabled.  It is
	 * cut off here and skipped by the assignment loop below.
	 */
	nl = strchr(buf, '\n');
	if (nl != NULL) {
		*nl = '\0';
		p = nl + 1;
		len_first = nl - buf;
	} else {
		p = buf + strlen(buf);
		len_first = strlen(buf);
	}
	/*
	 * This cuts the first line out of buf; the parse writes into it
	 * as well, so len_first is the only record of its length.
	 */
	if (nextboot_parse_line(buf, &name, &value) != 0 ||
	    strcmp(name, "nextboot_enable") != 0 ||
	    (strcmp(value, "YES") != 0 && strcmp(value, "yes") != 0))
		return;

	/*
	 * The loader applies only what fits into the buffer: drop the
	 * line the read stopped in.
	 */
	if (truncated) {
		printf("nextboot: %s: longer than %d bytes, "
		    "ignoring the rest\n", src, NEXTBOOT_BUFSZ);
		nl = strrchr(p, '\n');
		if (nl != NULL)
			*nl = '\0';
		else
			*p = '\0';
	}

	/*
	 * Consume the entry first.  If that fails, boot normally: applying
	 * it would repeat the same boot on every reset.
	 */
	if (archsw.arch_nextboot_consume != NULL)
		error = archsw.arch_nextboot_consume(len_first);
	else
		error = nextboot_file_consume(base, len_first);
	if (error != 0)
		return;
	setenv("nextboot_enable", "NO", 1);

	if (!nextboot_autoboot) {
		printf("nextboot: manual choice, entry discarded\n");
		return;
	}

	/*
	 * Apply the var="value" lines.  Nothing else from the entry is
	 * executed.
	 */
	kernel = NULL;
	while (*p != '\0') {
		nl = strchr(p, '\n');
		if (nl != NULL)
			*nl = '\0';
		if (nextboot_parse_line(p, &name, &value) == 0) {
			if (!nextboot_name_ok(name)) {
				printf("nextboot: ignoring line with bad name\n");
			} else if (strcmp(name, "nextboot_enable") == 0) {
				/* Used above. */
			} else if (strcmp(name, "kernel") == 0) {
				kernel = value;
			} else {
				snprintf(assign, sizeof(assign), "%s=%s",
				    name, value);
				dloader_set_var(assign);
			}
		} else if (*p != '\0' && *p != ' ' && *p != '\t' &&
		    *p != '#') {
			printf("nextboot: ignoring line without assignment\n");
		}
		if (nl == NULL)
			break;
		p = nl + 1;
	}

	if (kernel != NULL && *kernel != '\0') {
		snprintf(kpath, sizeof(kpath), "%s%s", base, kernel);
		if (chdir(kpath) != CMD_OK) {
			printf("nextboot: cannot change to %s: %s\n", kpath,
			    command_errmsg);
			snprintf(kpath, sizeof(kpath), "%s", DirBase);
		}
	} else {
		snprintf(kpath, sizeof(kpath), "%s", DirBase);
	}
	printf("nextboot: one-shot boot of %s (entry consumed)\n", kpath);
}

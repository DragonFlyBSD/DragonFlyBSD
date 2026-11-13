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

/*
 * Apply /boot/nextboot.conf for one boot.  The file is marked as used
 * before anything from it takes effect, so a kernel that hangs or
 * panics cannot make the entry stick.  If the file cannot be marked,
 * nothing is applied and the boot is a normal one.
 */
void
nextboot_check(void)
{
	static char assign[NEXTBOOT_BUFSZ + 1];
	static char base[MAXPATHLEN];
	static char buf[NEXTBOOT_BUFSZ + 1];
	static char first[NEXTBOOT_BUFSZ + 1];
	static char path[MAXPATHLEN];
	char *kernel, *name, *nl, *p, *value;
	size_t len, len_first;
	int c, fd, n, truncated;

	/* Only one shot: never apply the same file twice in one session. */
	if (getenv("nextboot_enable") != NULL &&
	    strcmp(getenv("nextboot_enable"), "NO") == 0)
		return;

	p = getenv("base");
	if (p == NULL)
		p = "/";
	snprintf(base, sizeof(base), "%s", p);
	snprintf(path, sizeof(path), "%snextboot.conf", base);

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		if (errno != ENOENT)
			printf("nextboot: cannot open %s: %s\n", path,
			    strerror(errno));
		return;
	}

	len = 0;
	while (len < NEXTBOOT_BUFSZ) {
		n = read(fd, buf + len, NEXTBOOT_BUFSZ - len);
		if (n < 0) {
			printf("nextboot: cannot read %s: %s\n", path,
			    strerror(errno));
			close(fd);
			return;
		}
		if (n == 0)
			break;
		len += n;
	}
	c = 0;
	truncated = 0;
	if (len == NEXTBOOT_BUFSZ && read(fd, &c, 1) == 1)
		truncated = 1;	/* the file continues past the buffer */
	close(fd);
	buf[len] = '\0';

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
		    "ignoring the rest\n", path, NEXTBOOT_BUFSZ);
		nl = strrchr(p, '\n');
		if (nl != NULL)
			*nl = '\0';
		else
			*p = '\0';
	}

	/*
	 * Consume the entry first: overwrite the first line, and only
	 * the first line.  If that fails, boot normally: applying it
	 * would repeat the same boot on every reset.
	 */
	if (len_first < sizeof(nextboot_marker) - 1) {
		printf("nextboot: %s: first line must be "
		    "nextboot_enable=\"YES\", ignoring\n", path);
		return;
	}
	memcpy(first, nextboot_marker, sizeof(nextboot_marker) - 1);
	memset(first + sizeof(nextboot_marker) - 1, ' ',
	    len_first - (sizeof(nextboot_marker) - 1));

	fd = open(path, O_RDWR);
	if (fd < 0) {
		printf("nextboot: cannot open %s for writing: %s, ignoring\n",
		    path, strerror(errno));
		return;
	}
	if (write(fd, first, len_first) < 0) {
		printf("nextboot: cannot consume %s: %s, ignoring\n", path,
		    strerror(errno));
		close(fd);
		return;
	}
	close(fd);
	setenv("nextboot_enable", "NO", 1);

	if (!nextboot_autoboot) {
		printf("nextboot: manual choice, entry discarded\n");
		return;
	}

	/*
	 * Apply the var="value" lines.  Nothing else from the file is
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
		snprintf(path, sizeof(path), "%s%s", base, kernel);
		if (chdir(path) != CMD_OK) {
			printf("nextboot: cannot change to %s: %s\n", path,
			    command_errmsg);
			snprintf(path, sizeof(path), "%s", DirBase);
		}
	} else {
		snprintf(path, sizeof(path), "%s", DirBase);
	}
	printf("nextboot: one-shot boot of %s (entry consumed)\n", path);
}

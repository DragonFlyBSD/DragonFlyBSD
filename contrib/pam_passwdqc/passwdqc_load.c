/*
 * Copyright (c) 2008,2009 by Dmitry V. Levin
 * Copyright (c) 2021 by Solar Designer
 * See LICENSE
 */

#ifdef _MSC_VER
#define _CRT_NONSTDC_NO_WARNINGS /* we use POSIX function names */
#define _CRT_SECURE_NO_WARNINGS /* we use fopen(), strerror(), sprintf() */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>

#include "passwdqc.h"
#include "concat.h"

static char *mkreason(const char *what, const char *pathname,
    unsigned int lineno, const char *why)
{
	char buf[sizeof(unsigned int) * 3 + 1];
	const char *at_line = (lineno ? " at line " : "");
	const char *at_num = (lineno ? buf : "");

	if (lineno)
		sprintf(buf, "%u", lineno);
	return concat(what, " \"", pathname, "\"", at_line, at_num, ": ",
	    (why ? why : strerror(errno)), NULL);
}

static int
parse_file(FILE *fp, passwdqc_params_t *params, char **reason,
    const char *pathname)
{
	unsigned int lineno;
	char buf[8192];

	for (lineno = 1; fgets(buf, sizeof(buf), fp); ++lineno) {
		char *str, *end, *rt;
		const char *cstr;
		int rc;

		if (strlen(buf) >= sizeof(buf) - 1) {
			*reason = mkreason("Error reading", pathname,
			    lineno, "Line too long");
			return -1;
		}

		str = buf + strspn(buf, " \t\r\n");
		if (!*str || *str == '#')
			continue;

		if ((end = strpbrk(str, "\r\n")))
			*end = '\0';
		else
			end = str + strlen(str);

		while (end > str && (*--end == ' ' || *end == '\t'))
			*end = '\0';

		cstr = str;
		if ((rc = passwdqc_params_parse(params, &rt, 1, &cstr))) {
			*reason = mkreason("Error loading", pathname,
			    lineno, (rt ? rt : "Out of memory"));
			free(rt);
			return rc;
		}
	}

	if (!feof(fp) || ferror(fp)) {
		*reason = mkreason("Error reading", pathname, 0, NULL);
		return -1;
	}

	return 0;
}

struct dev_ino_t;
struct dev_ino_t {
	struct dev_ino_t *next;
	dev_t dev;
	ino_t ino;
};

static struct dev_ino_t *dev_ino_head;

int
passwdqc_params_load(passwdqc_params_t *params, char **reason,
    const char *pathname)
{
	int rc;
	FILE *fp;
	struct dev_ino_t di, *di_p;
	struct stat st;

	if (!(fp = fopen(pathname, "r"))) {
		*reason = mkreason("Error opening", pathname, 0, NULL);
		return -1;
	}

	if (fstat(fileno(fp), &st)) {
		*reason = mkreason("Error stat", pathname, 0, NULL);
		fclose(fp);
		return -1;
	}

	di.dev = st.st_dev;
	di.ino = st.st_ino;
	for (di_p = dev_ino_head; di_p; di_p = di_p->next)
		if (di_p->dev == di.dev && di_p->ino == di.ino)
			break;
	if (di_p) {
		*reason = mkreason("Error opening", pathname, 0,
		    "Loop detected");
		fclose(fp);
		return -1;
	}

	di.next = dev_ino_head;
	dev_ino_head = &di;

	rc = parse_file(fp, params, reason, pathname);
	fclose(fp);

	dev_ino_head = dev_ino_head->next;
	return rc;
}

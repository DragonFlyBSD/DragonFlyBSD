/*
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

static void usage(void);

/*
 * Process paths on stdin and generate a directory path if it looks
 * like a pkgsrc directory.
 *
 * av[1]
 */
int
main(int ac, char **av)
{
	char buf[1024];
	char *path;
	char *bpath;
	struct stat st;
	size_t len;
	size_t blen;
	int count;
	int ch;
	int stripOpt = 0;

	while ((ch = getopt(ac, av, "s")) != -1) {
		switch(ch) {
		case 's':
			stripOpt = 1;
			break;
		default:
			usage();
			/* NOT REACHED */
		}
	}
	ac -= optind;
	av += optind;
	if (ac != 1) {
		fprintf(stderr, "requires base directory as first argument\n");
		exit(1);
	}

	/*
	 * Base dir
	 */
	bpath = strdup(av[0]);
	blen = strlen(bpath);
	while (blen && bpath[blen-1] == '/')
		--blen;
	bpath[blen] = 0;

	/*
	 * Process lines
	 */
	while (fgets(buf, sizeof(buf) - 32, stdin) != NULL) {
		path = strtok(buf, " \t\r\n");
		if (path == NULL || *path == 0)
			continue;
		len = strlen(path);
		if (len < blen || bcmp(path, bpath, blen) != 0)
			continue;
		if (stat(path, &st) != 0)
			continue;
		len = strlen(path);
		if (!S_ISDIR(st.st_mode)) {
			while (len && path[len-1] != '/')
				--len;
		}
		while (len && path[len-1] == '/')
			--len;
		strcpy(path + len, "/Makefile");
		if (stat(path, &st) != 0)
			continue;
		strcpy(path + len, "/DESCR");
		if (stat(path, &st) != 0)
			continue;
		path[len] = 0;

		/*
		 * Must be at least one sub-directory
		 */
		count = 0;
		for (len = blen; path[len]; ++len) {
			if (path[len] == '/')
				++count;
		}
		if (count < 2)
			continue;
		if (stripOpt)
			printf("%s\n", path + blen + 1);
		else
			printf("%s\n", path);
	}
	return(0);
}

static void
usage(void)
{
	fprintf(stderr, "getpkgsrcdir: unsupported option\n");
	exit(1);
}

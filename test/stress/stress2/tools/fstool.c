/*-
 * Copyright (c) 2008 Peter Holm <pho@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/types.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <string.h>
#include <err.h>


static int files = 5;
static int fs = 1024;
static char *buffer;
static int max;

void
error(char *op, char* arg, char* file, int line) {
	fprintf(stderr,"%s. %s. %s (%s:%d)\n",
		op, arg, sys_errlist[errno], file, line);
}

void
mkDir(char *path, int level) {
	int fd, j;
	char newPath[MAXPATHLEN + 1];
	char file[128];

	if (mkdir(path, 0770) == -1) {
		error("mkdir", path, __FILE__, __LINE__);
		fprintf(stderr, "length(path) = %d\n", strlen(path));
		fprintf(stderr, ") level = %d\n", level);
		exit(2);
	}
	chdir(path);

	for (j = 0; j <  files; j++) {
		sprintf(file,"f%05d", j);
		if ((fd = creat(file, 0660)) == -1) {
			if (errno != EINTR) {
				err(1, "%d: creat(%s)", level, file);
				break;
			}
		}
		if (write(fd, buffer, fs) != fs)
			err(1, "%d: write(%s), %s:%d", level, file, __FILE__, __LINE__);

		if (fd != -1 && close(fd) == -1)
			err(2, "%d: close(%d)", level, j);

	}

	if (level < max) {
		sprintf(newPath,"d%d", level+1);
		mkDir(newPath, level+1);
	}
}

static void
rmFile(void)
{
	int j;
	char file[128];

	for (j = 0; j <  files; j++) {
		sprintf(file,"f%05d", j);
		(void) unlink(file);
	}
}

void
rmDir(char *path, int level) {
	char newPath[10];


	if (level < max) {
		sprintf(newPath,"d%d", level+1);
		rmDir(newPath, level+1);
	}
	rmFile();
	chdir ("..");
	if (rmdir(path) == -1) {
		error("rmdir", path, __FILE__, __LINE__);
		exit(2);
	}
}

void
rmDir2(char *path, int level) {
	char newPath[10];
	char help[80];

	rmFile();
	chdir(path);
	sprintf(newPath,"d%d", level+1);
	if (access(newPath, R_OK) == 0)
		rmDir2(newPath, level+1);
	chdir ("..");
	if (rmdir(path) == -1) {
		error("rmdir", path, __FILE__, __LINE__);
		sprintf(help, "rm -rf ./%s", path);
		system(help);
	}
}

int
main(int argc, char **argv)
{
	int c, levels = 1, leave = 0;
	char path[128], rpath[128] = "";
	char ch = 0;
	extern char *optarg;
	pid_t pid;

	while ((c = getopt(argc, argv, "ln:r:f:s:")) != -1)
		switch (c) {
			case 'l':
				leave = 1;
				break;
			case 'r':
				strcpy(rpath, optarg);
				break;
			case 'n':
				levels = atoi(optarg);
				break;
			case 'f':
				files = atoi(optarg);
				break;
			case 's':
				sscanf(optarg, "%d%c", &fs, &ch);
				if (ch == 'k' || ch == 'K')
					fs = fs * 1024;
				if (ch == 'm' || ch == 'M')
					fs = fs * 1024 * 1024;
				break;
			default:
				fprintf(stderr,
					"Usage: %s {-l} | {-n <num>} | -r <dir> | -s <file size> "
					"-f <num>\n",
					argv[0]);
				printf("   -l: Leave the files.\n");
				printf("   -r: Remove an old tree.\n");
				printf("   -n: Tree depth.\n");
				printf("   -f: Number of files.\n");
				printf("   -s: Size of each file.\n");
				exit(1);
		}


	max = levels;
	pid = getpid();
	if ((buffer = calloc(1, fs)) == NULL)
		err(1, "calloc(%d)", fs);

	if (strlen(rpath) > 0) {
		rmDir2(rpath,1);
	} else {
		umask(0);
		sprintf(path,"p%05d.d%d", pid, 1);
		mkDir(path, 1);
		if (leave == 0) rmDir(path, 1);
	}
	return 0;
}

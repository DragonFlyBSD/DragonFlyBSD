/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
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
 * $DragonFly: src/sbin/newfs/fscopy.c,v 1.1 2003/12/01 04:35:39 dillon Exp $
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#ifdef DIRBLKSIZ
#undef DIRBLKSIZ
#endif
#include "defs.h"

struct FSNode {
    struct FSNode	*fs_Next;
    struct FSNode	*fs_HNext;
    struct FSNode	*fs_Base;
    struct FSNode	*fs_Parent;
    struct stat		fs_St;
    char		*fs_Data;
    int			fs_Bytes;
    int			fs_Marker;
    char		fs_Name[4];
};

static
fsnode_t
fsmknode(const char *path)
{
    int pathlen = strlen(path);
    fsnode_t node = malloc(offsetof(struct FSNode, fs_Name[pathlen+1]));

    if (node == NULL) {
	fprintf(stderr, "ran out of memory copying filesystem\n");
	return(NULL);
    }
    bzero(node, sizeof(*node));
    bcopy(path, node->fs_Name, pathlen + 1);
    if (lstat(path, &node->fs_St) < 0) {
	fprintf(stderr, "Unable to lstat(\"%s\")\n", path);
	free(node);
	return(NULL);
    }
    return(node);
}

fsnode_t
fsgethlink(fsnode_t hlinks, fsnode_t node)
{
    fsnode_t scan;

    for (scan = hlinks; scan; scan = scan->fs_HNext) {
	if (scan->fs_St.st_dev == node->fs_St.st_dev &&
	    scan->fs_St.st_ino == node->fs_St.st_ino
	) {
	    return(scan);
	}
    }
    return(NULL);
}

char *
fshardpath(fsnode_t hlink, fsnode_t node)
{
    fsnode_t scan;
    char *path;
    char *tmp;

    for (scan = hlink; scan; scan = scan->fs_Parent)
	scan->fs_Marker = 1;
    for (scan = node; scan; scan = scan->fs_Parent) {
	if (scan->fs_Marker == 1) {
	    scan->fs_Marker = 2;
	    break;
	}
    }
    if (scan == NULL)
	return(NULL);

    /*
     * Build the path backwards
     */
    asprintf(&path, "%s", hlink->fs_Name);
    for (scan = hlink->fs_Parent; scan->fs_Marker == 1; scan = scan->fs_Parent) {
	tmp = path;
	asprintf(&path, "%s/%s", scan->fs_Name, tmp);
	free(tmp);
    }
    for (scan = node->fs_Parent; scan; scan = scan->fs_Parent) {
	if (scan->fs_Marker == 2)
	    break;
	tmp = path;
	asprintf(&path, "../%s", tmp);
	free(tmp);
    }

    for (scan = hlink; scan; scan = scan->fs_Parent)
	scan->fs_Marker = 0;
    for (scan = node; scan; scan = scan->fs_Parent)
	scan->fs_Marker = 0;
    return(path);
}

fsnode_t
FSCopy(fsnode_t *phlinks, const char *path)
{
    int n;
    DIR *dir;
    fsnode_t node;
    char buf[1024];

    node = fsmknode(path);
    if (node) {
	switch(node->fs_St.st_mode & S_IFMT) {
	case S_IFIFO:
	    break;
	case S_IFCHR:
	    break;
	case S_IFDIR:
	    if ((dir = opendir(path)) != NULL) {
		struct dirent *den;
		fsnode_t scan;
		fsnode_t *pscan;

		if (chdir(path) < 0) {
		    fprintf(stderr, "Unable to chdir into %s\n", path);
		    break;
		}
		pscan = &node->fs_Base;
		while ((den = readdir(dir)) != NULL) {
		    if (den->d_namlen == 1 && den->d_name[0] == '.')
			continue;
		    if (den->d_namlen == 2 && den->d_name[0] == '.' &&
			den->d_name[1] == '.'
		    ) {
			continue;
		    }
		    scan = FSCopy(phlinks, den->d_name);
		    if (scan) {
			*pscan = scan;
			scan->fs_Parent = node;
			pscan = &scan->fs_Next;
		    }
		}
		if (chdir("..") < 0) {
		    fprintf(stderr, "Unable to chdir .. after scanning %s\n", path);
		    exit(1);
		}
		closedir(dir);
	    }
	    break;
	case S_IFBLK:
	    break;
	case S_IFREG:
	    if (node->fs_St.st_nlink > 1 && fsgethlink(*phlinks, node)) {
		node->fs_Bytes = -1;	/* hardlink indicator */
	    } else if (node->fs_St.st_size >= 0x80000000LL) {
		fprintf(stderr, "File %s too large to copy\n", path);
		free(node);
		node = NULL;
	    } else if ((node->fs_Data = malloc(node->fs_St.st_size)) == NULL) {
		fprintf(stderr, "Ran out of memory copying %s\n", path);
		free(node);
		node = NULL;
	    } else if ((n = open(path, O_RDONLY)) < 0) {
		fprintf(stderr, "Unable to open %s for reading %s\n", path);
		free(node->fs_Data);
		free(node);
		node = NULL;
	    } else if (read(n, node->fs_Data, node->fs_St.st_size) != node->fs_St.st_size) {
		fprintf(stderr, "Unable to read %s\n", path);
		free(node->fs_Data);
		free(node);
		node = NULL;
		
	    } else {
		node->fs_Bytes = node->fs_St.st_size;
		if (node->fs_St.st_nlink > 1) {
		    node->fs_HNext = *phlinks;
		    *phlinks = node;
		}
	    }
	    break;
	case S_IFLNK:
	    if ((n = readlink(path, buf, sizeof(buf))) > 0) {
		if ((node->fs_Data = malloc(n + 1)) == NULL) {
		    fprintf(stderr, "Ran out of memory\n");
		    free(node);
		    node = NULL;
		} else {
		    node->fs_Bytes = n;
		    bcopy(buf, node->fs_Data, n);
		    node->fs_Data[n] = 0;
		}
	    } else if (n == 0) {
		node->fs_Data = "";
		node->fs_Bytes = 0;
	    } else {
		fprintf(stderr, "Unable to read link: %s\n", path);
		free(node);
		node = NULL;
	    }
	    break;
	case S_IFSOCK:
	    break;
	case S_IFWHT:
	    break;
	default:
	    break;
	}
    }
    return(node);
}

void
FSPaste(const char *path, fsnode_t node, fsnode_t hlinks)
{
    struct timeval times[2];
    fsnode_t scan;
    int fd;
    int ok = 0;

    switch(node->fs_St.st_mode & S_IFMT) {
    case S_IFIFO:
	break;
    case S_IFCHR:
    case S_IFBLK:
	if (mknod(path, node->fs_St.st_mode, node->fs_St.st_rdev) < 0) {
	    fprintf(stderr, "Paste: mknod failed on %s\n", path);
	    break;
	}
	ok = 1;
	break;
    case S_IFDIR:
	fd = open(".", O_RDONLY);
	if (fd < 0) {
	    fprintf(stderr, "Paste: cannot open current directory\n");
	    exit(1);
	}
	if (mkdir(path, 0700) < 0 && errno != EEXIST) {
	    printf("Paste: unable to create directory %s\n", path);
	    close(fd);
	    break;
	}
	if (chdir(path) < 0) {
	    printf("Paste: unable to chdir into %s\n", path);
	    exit(1);
	}
	for (scan = node->fs_Base; scan; scan = scan->fs_Next) {
	    FSPaste(scan->fs_Name, scan, hlinks);
	}
	if (fchdir(fd) < 0) {
	    fprintf(stderr, "Paste: cannot fchdir current dir\n");
	    close(fd);
	    exit(1);
	}
	close(fd);
	ok = 1;
	break;
    case S_IFREG:
	if (node->fs_St.st_nlink > 1 && node->fs_Bytes < 0) {
	    if ((scan = fsgethlink(hlinks, node)) == NULL) {
		fprintf(stderr, "Cannot find hardlink for %s\n", path);
	    } else {
		char *hpath = fshardpath(scan, node);
		if (hpath == NULL || link(hpath, path) < 0) {
		    fprintf(stderr, "Cannot create hardlink: %s->%s\n", path, hpath ? hpath : "?");
		    if (hpath)
			free(hpath);
		    break;
		}
		ok = 1;
		free(hpath);
	    }
	    break;
	}
	if ((fd = open(path, O_RDWR|O_CREAT|O_TRUNC, 0600)) < 0) {
	    fprintf(stderr, "Cannot create file: %s\n", path);
	    break;
	}
	if (write(fd, node->fs_Data, node->fs_Bytes) != node->fs_Bytes) {
	    fprintf(stderr, "Cannot write file: %s\n", path);
	    remove(path);
	    close(fd);
	    break;
	}
	close(fd);
	ok = 1;
	break;
    case S_IFLNK:
	if (symlink(node->fs_Data, path) < 0) {
	    fprintf(stderr, "Unable to create symbolic link: %s\n", path);
	    break;
	}
	ok = 1;
	break;
    case S_IFSOCK:
	break;
    case S_IFWHT:
	break;
    default:
	break;
    }

    /* 
     * Set perms
     */
    if (ok) {
	struct stat *st = &node->fs_St;

	times[0].tv_sec = st->st_atime;
	times[0].tv_usec = 0;
	times[1].tv_sec = st->st_mtime;
	times[1].tv_usec = 0;

	if (lchown(path, st->st_uid, st->st_gid) < 0)
	    fprintf(stderr, "lchown failed on %s\n", path);
	if (lutimes(path, times) < 0)
	    fprintf(stderr, "lutimes failed on %s\n", path);
	if (lchmod(path, st->st_mode & ALLPERMS) < 0)
	    fprintf(stderr, "lchmod failed on %s\n", path);
	if (chflags(path, st->st_flags) < 0)
	    fprintf(stderr, "lchflags failed on %s\n", path);
    }
}


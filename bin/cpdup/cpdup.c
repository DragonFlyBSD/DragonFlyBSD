/*-
 * CPDUP.C
 *
 * CPDUP <options> source destination
 *
 * (c) Copyright 1997-1999 by Matthew Dillon and Dima Ruban.  Permission to
 *     use and distribute based on the FreeBSD copyright.  Supplied as-is,
 *     USE WITH EXTREME CAUTION.
 *
 * This program attempts to duplicate the source onto the destination as 
 * exactly as possible, retaining modify times, flags, perms, uid, and gid.
 * It can duplicate devices, files (including hardlinks), softlinks, 
 * directories, and so forth.  It is recursive by default!  The duplication
 * is inclusive of removal of files/directories on the destination that do
 * not exist on the source.  This program supports a per-directory exception
 * file called .cpignore, or a user-specified exception file.
 *
 * Safety features:
 *
 *	- does not cross partition boundries on source
 *	- asks for confirmation on deletions unless -i0 is specified
 *	- refuses to replace a destination directory with a source file
 *	  unless -s0 is specified.
 *	- terminates on error
 *
 * Copying features:
 *
 *	- does not copy file if mtime, flags, perms, and size match unless
 *	  forced
 *
 *	- copies to temporary and renames-over the original, allowing
 *	  you to update live systems
 *
 *	- copies uid, gid, mtime, perms, flags, softlinks, devices, hardlinks,
 *	  and recurses through directories.
 *
 *	- accesses a per-directory exclusion file, .cpignore, containing 
 *	  standard wildcarded ( ? / * style, NOT regex) exclusions.
 *
 *	- tries to play permissions and flags smart in regards to overwriting 
 *	  schg files and doing related stuff.
 *
 *	- Can do MD5 consistancy checks
 *
 * $DragonFly: src/bin/cpdup/cpdup.c,v 1.6 2004/07/22 13:09:02 asmodai Exp $
 */

/*-
 * Example: cc -O cpdup.c -o cpdup -lmd
 *
 * ".MD5.CHECKSUMS" contains md5 checksumms for the current directory.
 * This file is stored on the source.
 */

#include "cpdup.h"

#define HSIZE	16384
#define HMASK	(HSIZE-1)

const char *MD5CacheFile;

typedef struct Node {
    struct Node *no_Next;
    struct Node *no_HNext;
    int  no_Value;
    char no_Name[4];
} Node;

typedef struct List {
    Node	li_Node;
    Node	*li_Hash[HSIZE];
} List;

struct hlink {
    ino_t ino;
    ino_t dino;
    char name[2048];
    struct hlink *next;
    struct hlink *prev;
    int nlinked;
};

typedef struct MD5Node {
    struct MD5Node *md_Next;
    char *md_Name;
    char *md_Code;
    int md_Accessed;
} MD5Node;


void RemoveRecur(const char *dpath, dev_t devNo);
void InitList(List *list);
void ResetList(List *list);
int AddList(List *list, const char *name, int n);
static struct hlink *hltlookup(struct stat *);
static struct hlink *hltadd(struct stat *, const char *);
static int shash(const char *s);
static void hltdelete(struct hlink *);
int YesNo(const char *path);
static int xrename(const char *src, const char *dst, u_long flags);
static int xlink(const char *src, const char *dst, u_long flags);
int WildCmp(const char *s1, const char *s2);
static MD5Node *md5_lookup(const char *sfile);
static int md5_check(const char *spath, const char *dpath);
static void md5_flush(void);
static void md5_cache(const char *spath, int sdirlen);
static char *fextract(FILE *fi, int n, int *pc, int skip);
int DoCopy(const char *spath, const char *dpath, dev_t sdevNo, dev_t ddevNo);
char *doMD5File(const char *filename, char *buf);

int AskConfirmation = 1;
int SafetyOpt = 1;
int ForceOpt = 0;
int VerboseOpt = 0;
int QuietOpt = 0;
int NoRemoveOpt = 0;
int UseMD5Opt = 0;
int SummaryOpt = 0;
const char *UseCpFile;

int64_t CountSourceBytes = 0;
int64_t CountSourceItems = 0;
int64_t CountCopiedItems = 0;
int64_t CountReadBytes = 0;
int64_t CountWriteBytes = 0;
int64_t CountRemovedItems = 0;

char *MD5SCache;		/* cache source directory name */
MD5Node *MD5Base;
int MD5SCacheDirLen;
int MD5SCacheDirty;


int
main(int ac, char **av)
{
    int i;
    char *src = NULL;
    char *dst = NULL;
    struct timeval start;

    gettimeofday(&start, NULL);
    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];
	int v = 1;

	if (*ptr != '-') { 
	    if (src == NULL) {
		src = ptr;
	    } else if (dst == NULL) {
		dst = ptr;
	    } else {
		fatal("too many arguments");
		/* not reached */
	    }
	    continue;
	}
	ptr += 2;

	if (*ptr)
	    v = strtol(ptr, NULL, 0);

	switch(ptr[-1]) {
	case 'v':
	    VerboseOpt = 1;
	    while (*ptr == 'v') {
		++VerboseOpt;
		++ptr;
	    }
	    if (*ptr >= '0' && *ptr <= '9')
		VerboseOpt = strtol(ptr, NULL, 0);
	    break;
	case 'I':
	    SummaryOpt = v;
	    break;
	case 'o':
	    NoRemoveOpt = v;
	    break;
	case 'x':
	    UseCpFile = ".cpignore";
	    break;
	case 'X':
	    UseCpFile = (*ptr) ? ptr : av[++i];
	    break;
	case 'f':
	    ForceOpt = v;
	    break;
	case 'i':
	    AskConfirmation = v;
	    break;
	case 's':
	    SafetyOpt = v;
	    break;
	case 'q':
	    QuietOpt = v;
	    break;
	case 'M':
	    UseMD5Opt = v;
	    MD5CacheFile = av[++i];
	    break;
	case 'm':
	    UseMD5Opt = v;
	    MD5CacheFile = ".MD5.CHECKSUMS";
	    break;
	case 'u':
	    setvbuf(stdout, NULL, _IOLBF, 0);
	    break;
	default:
	    fatal("illegal option: %s\n", ptr - 2);
	    /* not reached */
	    break;
	}
    }

    /*
     * dst may be NULL only if -m option is specified,
     * which forces an update of the MD5 checksums
     */

    if (dst == NULL && UseMD5Opt == 0) {
	fatal(NULL);
	/* not reached */
    }
    if (dst) {
	i = DoCopy(src, dst, (dev_t)-1, (dev_t)-1);
    } else {
	i = DoCopy(src, NULL, (dev_t)-1, (dev_t)-1);
    }
    md5_flush();

    if (SummaryOpt && i == 0) {
	long duration;
	struct timeval end;

	gettimeofday(&end, NULL);
	CountSourceBytes += sizeof(struct stat) * CountSourceItems;
	CountReadBytes += sizeof(struct stat) * CountSourceItems;
	CountWriteBytes +=  sizeof(struct stat) * CountCopiedItems;
	CountWriteBytes +=  sizeof(struct stat) * CountRemovedItems;

	duration = end.tv_sec - start.tv_sec;
	duration *= 1000000;
	duration += end.tv_usec - start.tv_usec;
	if (duration == 0) duration = 1;
	logstd("cpdup completed sucessfully\n");
	logstd("%lld bytes source %lld bytes read %lld bytes written (%.1fX speedup)\n",
	    (long long)CountSourceBytes,
	    (long long)CountReadBytes,
	    (long long)CountWriteBytes,
	    ((double)CountSourceBytes * 2.0) / ((double)(CountReadBytes + CountWriteBytes)));
 	logstd("%lld source items %lld items copied %lld things deleted\n",
	    (long long)CountSourceItems,
	    (long long)CountCopiedItems,
	    (long long)CountRemovedItems);
	logstd("%.1f seconds %5d Kbytes/sec synced %5d Kbytes/sec scanned\n",
	    (float)duration / (float)1000000,
	    (long)((long)1000000 * (CountReadBytes + CountWriteBytes) / duration  / 1024.0),
	    (long)((long)1000000 * CountSourceBytes / duration / 1024.0));
    }
    exit((i == 0) ? 0 : 1);
}

#define HASHF 16

struct hlink *hltable[HASHF];

static struct hlink *
hltlookup(struct stat *stp)
{
    struct hlink *hl;
    int n;

    n = stp->st_ino % HASHF;

    for (hl = hltable[n]; hl; hl = hl->next)
        if (hl->ino == stp->st_ino)
              return hl;

    return NULL;
}

static struct hlink *
hltadd(struct stat *stp, const char *path)
{
    struct hlink *new;
    int n;

    if (!(new = (struct hlink *)malloc(sizeof (struct hlink)))) {
        fprintf(stderr, "out of memory\n");
        exit(10);
    }

    /* initialize and link the new element into the table */
    new->ino = stp->st_ino;
    new->dino = 0;
    strncpy(new->name, path, 2048);
    new->nlinked = 1;
    new->prev = NULL;
    n = stp->st_ino % HASHF;
    new->next = hltable[n];
    if (hltable[n])
        hltable[n]->prev = new;
    hltable[n] = new;

    return new;
}

static void
hltdelete(struct hlink *hl)
{
    if (hl->prev) {
        if (hl->next)
            hl->next->prev = hl->prev;
        hl->prev->next = hl->next;
    } else {
        if (hl->next)
            hl->next->prev = NULL;

        hltable[hl->ino % HASHF] = hl->next;
    }

    free(hl);
}

int
DoCopy(const char *spath, const char *dpath, dev_t sdevNo, dev_t ddevNo)
{
    struct stat st1;
    struct stat st2;
    int r = 0;
    int mres = 0;
    int st2Valid = 0;
    struct hlink *hln = NULL;
    List list;
    u_int64_t size = 0;

    InitList(&list);

    if (lstat(spath, &st1) != 0)
	return(0);
    st2.st_mode = 0;	/* in case lstat fails */
    st2.st_flags = 0;	/* in case lstat fails */
    if (dpath && lstat(dpath, &st2) == 0)
	st2Valid = 1;

    if (S_ISREG(st1.st_mode)) {
	size = st1.st_blocks * 512;
	if (st1.st_size % 512) 
	    size += st1.st_size % 512 - 512;
    }

    /*
     * Handle hardlinks
     */

    if (S_ISREG(st1.st_mode) && st1.st_nlink > 1 && dpath) {
        if ((hln = hltlookup(&st1)) != NULL) {
            hln->nlinked++;

            if (st2Valid) {
                if (st2.st_ino == hln->dino) {
		    /*
		     * hard link is already correct, nothing to do
		     */
		    if (VerboseOpt >= 3)
			logstd("%-32s nochange\n", (dpath) ? dpath : spath);
                    if (hln->nlinked == st1.st_nlink)
                        hltdelete(hln);
		    CountSourceItems++;
                    return 0;
                } else {
		    /*
		     * hard link is not correct, attempt to unlink it
		     */
                    if (unlink(dpath) < 0) {
			logerr("%-32s hardlink: unable to unlink: %s\n", 
			    ((dpath) ? dpath : spath), strerror(errno));
                        hltdelete(hln);
			return (r + 1);
		    }
                }
            }

            if (xlink(hln->name, dpath, st1.st_flags) < 0) {
		logerr("%-32s hardlink: unable to link to %s: %s\n",
		    (dpath ? dpath : spath), hln->name, strerror(errno)
		);
                hltdelete(hln);
                hln = NULL;
		++r;
            } else {
                if (hln->nlinked == st1.st_nlink) {
                    hltdelete(hln);
		    hln = NULL;
		}
                if (r == 0) {
		    if (VerboseOpt) {
			logstd("%-32s hardlink: %s\n", 
			    (dpath ? dpath : spath),
			    (st2Valid ? "relinked" : "linked")
			);
		    }
		    CountSourceItems++;
		    CountCopiedItems++;
                    return 0;
		}
            }
        } else {
	    /*
	     * first instance of hardlink must be copied normally
	     */
            hln = hltadd(&st1, dpath);
	}
    }

    /*
     * Do we need to copy the file/dir/link/whatever?  Early termination
     * if we do not.  Always traverse directories.  Always redo links.
     *
     * NOTE: st2Valid is true only if dpath != NULL *and* dpath stats good.
     */

    if (
	st2Valid &&
	st1.st_mode == st2.st_mode &&
	st1.st_flags == st2.st_flags
    ) {
	if (S_ISLNK(st1.st_mode) || S_ISDIR(st1.st_mode)) {
	    ;
	} else {
	    if (ForceOpt == 0 &&
		st1.st_size == st2.st_size &&
		st1.st_uid == st2.st_uid &&
		st1.st_gid == st2.st_gid &&
		st1.st_mtime == st2.st_mtime
		&& (UseMD5Opt == 0 || (mres = md5_check(spath, dpath)) == 0)
	    ) {
                if (hln)
                    hln->dino = st2.st_ino;
		if (VerboseOpt >= 3) {
		    if (UseMD5Opt)
			logstd("%-32s md5-nochange\n", (dpath ? dpath : spath));
		    else
			logstd("%-32s nochange\n", (dpath ? dpath : spath));
		}
		CountSourceBytes += size;
		CountSourceItems++;

		return(0);
	    }
	}
    }
    if (st2Valid && !S_ISDIR(st1.st_mode) && S_ISDIR(st2.st_mode)) {
	if (SafetyOpt) {
	    logerr("%-32s SAFETY - refusing to copy file over directory\n",
		(dpath ? dpath : spath)
	    );
	    ++r;		/* XXX */
	    return(0);	/* continue with the cpdup anyway */
	}
	if (QuietOpt == 0 || AskConfirmation) {
	    logstd("%-32s WARNING: non-directory source will blow away\n"
		   "%-32s preexisting dest directory, continuing anyway!\n",
		   ((dpath) ? dpath : spath), "");
	}
	if (dpath)
	    RemoveRecur(dpath, ddevNo);
    }

    if (S_ISDIR(st1.st_mode)) {
	DIR *dir;

	if ((dir = opendir(spath)) != NULL) {
	    struct dirent *den;
	    int noLoop = 0;

	    if (dpath) {
		if (S_ISDIR(st2.st_mode) == 0) {
		    remove(dpath);
		    if (mkdir(dpath, st1.st_mode | 0700) != 0) {
			logerr("%s: mkdir failed: %s\n", 
			    (dpath ? dpath : spath), strerror(errno));
			r = 1;
			noLoop = 1;
		    }
		    /*
		     * Matt: why don't you check error codes here?
		     */
		    lstat(dpath, &st2);
		    chown(dpath, st1.st_uid, st1.st_gid);
		    CountCopiedItems++;
		} else {
		    /*
		     * Directory must be scanable by root for cpdup to
		     * work.  We'll fix it later if the directory isn't
		     * supposed to be readable ( which is why we fixup
		     * st2.st_mode to match what we did ).
		     */
		    if ((st2.st_mode & 0700) != 0700) {
			chmod(dpath, st2.st_mode | 0700);
			st2.st_mode |= 0700;
		    }
		    if (VerboseOpt >= 2)
			logstd("%s\n", dpath ? dpath : spath);
		}
	    }

	    if ((int)sdevNo >= 0 && st1.st_dev != sdevNo) {
		noLoop = 1;
	    } else {
		sdevNo = st1.st_dev;
	    }

	    if ((int)ddevNo >= 0 && st2.st_dev != ddevNo) {
		noLoop = 1;
	    } else {
		ddevNo = st2.st_dev;
	    }

	    /*
	     * scan .cpignore file for files/directories 
	     * to ignore.
	     */

	    if (UseCpFile) {
		FILE *fi;
		char buf[8192];
		char *fpath;

		if (UseCpFile[0] == '/') {
		    fpath = mprintf("%s", UseCpFile);
		} else {
		    fpath = mprintf("%s/%s", spath, UseCpFile);
		}
		AddList(&list, strrchr(fpath, '/') + 1, 1);
		if ((fi = fopen(fpath, "r")) != NULL) {
		    while (fgets(buf, sizeof(buf), fi) != NULL) {
			int l = strlen(buf);
			CountReadBytes += l;
			if (l && buf[l-1] == '\n')
			    buf[--l] = 0;
			if (buf[0] && buf[0] != '#')
			    AddList(&list, buf, 1);
		    }
		    fclose(fi);
		}
		free(fpath);
	    }

	    /*
	     * Automatically exclude MD5CacheFile that we create on the
	     * source from the copy to the destination.
	     */
	    if (UseMD5Opt)
		AddList(&list, MD5CacheFile, 1);

	    while (noLoop == 0 && (den = readdir(dir)) != NULL) {
		/*
		 * ignore . and ..
		 */
		char *nspath;
		char *ndpath = NULL;

		if (strcmp(den->d_name, ".") == 0 ||
		    strcmp(den->d_name, "..") == 0
		) {
		    continue;
		}
		/*
		 * ignore if on .cpignore list
		 */
		if (AddList(&list, den->d_name, 0) == 1) {
		    continue;
		}
		nspath = mprintf("%s/%s", spath, den->d_name);
		if (dpath)
		    ndpath = mprintf("%s/%s", dpath, den->d_name);
		r += DoCopy(
		    nspath,
		    ndpath,
		    sdevNo,
		    ddevNo
		);
		free(nspath);
		if (ndpath)
		    free(ndpath);
	    }

	    closedir(dir);

	    /*
	     * Remove files/directories from destination that do not appear
	     * in the source.
	     */
	    if (dpath && (dir = opendir(dpath)) != NULL) {
		while (noLoop == 0 && (den = readdir(dir)) != NULL) {
		    /*
		     * ignore . or ..
		     */
		    if (strcmp(den->d_name, ".") == 0 ||
			strcmp(den->d_name, "..") == 0
		    ) {
			continue;
		    }
		    /*
		     * If object does not exist in source or .cpignore
		     * then recursively remove it.
		     */
		    if (AddList(&list, den->d_name, 3) == 3) {
			char *ndpath;

			ndpath = mprintf("%s/%s", dpath, den->d_name);
			RemoveRecur(ndpath, ddevNo);
			free(ndpath);
		    }
		}
		closedir(dir);
	    }

	    if (dpath) {
		if (ForceOpt ||
		    st2Valid == 0 || 
		    st1.st_uid != st2.st_uid ||
		    st1.st_gid != st2.st_gid
		) {
		    chown(dpath, st1.st_uid, st1.st_gid);
		}
		if (st2Valid == 0 || st1.st_mode != st2.st_mode) {
		    chmod(dpath, st1.st_mode);
		}
		if (st2Valid == 0 || st1.st_flags != st2.st_flags) {
		    chflags(dpath, st1.st_flags);
		}
	    }
	}
    } else if (dpath == NULL) {
	/*
	 * If dpath is NULL, we are just updating the MD5
	 */
	if (UseMD5Opt && S_ISREG(st1.st_mode)) {
	    mres = md5_check(spath, NULL);

	    if (VerboseOpt > 1) {
		if (mres < 0)
		    logstd("%-32s md5-update\n", (dpath) ? dpath : spath);
		else
		    logstd("%-32s md5-ok\n", (dpath) ? dpath : spath);
	    } else if (!QuietOpt && mres < 0) {
		logstd("%-32s md5-update\n", (dpath) ? dpath : spath);
	    }
	}
    } else if (S_ISREG(st1.st_mode)) {
	char *path;
	int fd1;
	int fd2;

	path = mprintf("%s.tmp", dpath);

	/*
	 * Handle check failure message.
	 */
	if (mres < 0)
	    logerr("%-32s md5-CHECK-FAILED\n", (dpath) ? dpath : spath);

	if ((fd1 = open(spath, O_RDONLY)) >= 0) {
	    if ((fd2 = open(path, O_WRONLY|O_CREAT|O_EXCL, 0600)) < 0) {
		/*
		 * There could be a .tmp file from a previously interrupted
		 * run, delete and retry.  Fail if we still can't get at it.
		 */
		chflags(path, 0);
		remove(path);
		fd2 = open(path, O_WRONLY|O_CREAT|O_EXCL|O_TRUNC, 0600);
	    }
	    if (fd2 >= 0) {
		/*
		 * Matt: I think 64k would be faster here
		 */
		char buf[32768];
		const char *op;
		int n;

		/*
		 * Matt: What about holes?
		 */
		op = "read";
		while ((n = read(fd1, buf, sizeof(buf))) > 0) {
		    op = "write";
		    if (write(fd2, buf, n) != n)
			break;
		    op = "read";
		}
		close(fd2);
		if (n == 0) {
		    struct timeval tv[2];

		    bzero(tv, sizeof(tv));
		    tv[0].tv_sec = st1.st_mtime;
		    tv[1].tv_sec = st1.st_mtime;

		    utimes(path, tv);
		    chown(path, st1.st_uid, st1.st_gid);
		    chmod(path, st1.st_mode);
		    if (xrename(path, dpath, st2.st_flags) != 0) {
			logerr("%-32s rename-after-copy failed: %s\n",
			    (dpath ? dpath : spath), strerror(errno)
			);
			++r;
		    } else {
			if (VerboseOpt)
			    logstd("%-32s copy-ok\n", (dpath ? dpath : spath));
			if (st1.st_flags)
			    chflags(dpath, st1.st_flags);
		    }
		    CountReadBytes += size;
		    CountWriteBytes += size;
		    CountSourceBytes += size;
		    CountSourceItems++;
		    CountCopiedItems++;
		} else {
		    logerr("%-32s %s failed: %s\n",
			(dpath ? dpath : spath), op, strerror(errno)
		    );
		    remove(path);
		    ++r;
		}
	    } else {
		logerr("%-32s create (uid %d, euid %d) failed: %s\n",
		    (dpath ? dpath : spath), getuid(), geteuid(),
		    strerror(errno)
		);
		++r;
	    }
	    close(fd1);
	} else {
	    logerr("%-32s copy: open failed: %s\n",
		(dpath ? dpath : spath),
		strerror(errno)
	    );
	    ++r;
	}
	free(path);

        if (hln) {
            if (!r && stat(dpath, &st2) == 0)
                hln->dino = st2.st_ino;
            else
                hltdelete(hln);
        }
    } else if (S_ISLNK(st1.st_mode)) {
	char link1[1024];
	char link2[1024];
	char path[2048];
	int n1;
	int n2;

	snprintf(path, sizeof(path), "%s.tmp", dpath);
	n1 = readlink(spath, link1, sizeof(link1) - 1);
	n2 = readlink(dpath, link2, sizeof(link2) - 1);
	if (n1 >= 0) {
	    if (ForceOpt || n1 != n2 || bcmp(link1, link2, n1) != 0) {
		umask(~st1.st_mode);
		remove(path);
		link1[n1] = 0;
		if (symlink(link1, path) < 0) {
                      logerr("%-32s symlink (%s->%s) failed: %s\n",
			  (dpath ? dpath : spath), link1, path,
			  strerror(errno)
		      );
		      ++r;
		} else {
		    lchown(path, st1.st_uid, st1.st_gid);
		    /*
		     * there is no lchmod() or lchflags(), we 
		     * cannot chmod or chflags a softlink.
		     */
		    if (xrename(path, dpath, st2.st_flags) != 0) {
			logerr("%-32s rename softlink (%s->%s) failed: %s\n",
			    (dpath ? dpath : spath),
			    path, dpath, strerror(errno));
		    } else if (VerboseOpt) {
			logstd("%-32s softlink-ok\n", (dpath ? dpath : spath));
		    }
		    umask(000);
		    CountWriteBytes += n1;
		    CountCopiedItems++;
	  	}
	    } else {
		if (VerboseOpt >= 3)
		    logstd("%-32s nochange\n", (dpath ? dpath : spath));
	    }
	    CountSourceBytes += n1;
	    CountReadBytes += n1;
	    if (n2 > 0) CountReadBytes += n2;
	    CountSourceItems++;
	} else {
	    r = 1;
	    logerr("%-32s softlink-failed\n", (dpath ? dpath : spath));
	}
    } else if (S_ISCHR(st1.st_mode) || S_ISBLK(st1.st_mode)) {
	char path[2048];

	if (ForceOpt ||
	    st2Valid == 0 || 
	    st1.st_mode != st2.st_mode || 
	    st1.st_rdev != st2.st_rdev ||
	    st1.st_uid != st2.st_uid ||
	    st1.st_gid != st2.st_gid
	) {
	    snprintf(path, sizeof(path), "%s.tmp", dpath);

	    remove(path);
	    if (mknod(path, st1.st_mode, st1.st_rdev) == 0) {
		chmod(path, st1.st_mode);
		chown(path, st1.st_uid, st1.st_gid);
		remove(dpath);
		if (xrename(path, dpath, st2.st_flags) != 0) {
		    logerr("%-32s dev-rename-after-create failed: %s\n",
			(dpath ? dpath : spath),
			strerror(errno)
		    );
		} else if (VerboseOpt) {
		    logstd("%-32s dev-ok\n", (dpath ? dpath : spath));
		}
		CountCopiedItems++;
	    } else {
		r = 1;
		logerr("%-32s dev failed: %s\n", 
		    (dpath ? dpath : spath), strerror(errno)
		);
	    }
	} else {
	    if (VerboseOpt >= 3)
		logstd("%-32s nochange\n", (dpath ? dpath : spath));
	}
	CountSourceItems++;
    }
    ResetList(&list);
    return(r);
}

/*
 * RemoveRecur()
 */

void
RemoveRecur(const char *dpath, dev_t devNo)
{
    struct stat st;

    if (lstat(dpath, &st) == 0) {
	if ((int)devNo < 0)
	    devNo = st.st_dev;
	if (st.st_dev == devNo) {
	    if (S_ISDIR(st.st_mode)) {
		DIR *dir;

		if ((dir = opendir(dpath)) != NULL) {
		    struct dirent *den;
		    while ((den = readdir(dir)) != NULL) {
			char *ndpath;

			if (strcmp(den->d_name, ".") == 0)
			    continue;
			if (strcmp(den->d_name, "..") == 0)
			    continue;
			ndpath = mprintf("%s/%s", dpath, den->d_name);
			RemoveRecur(ndpath, devNo);
			free(ndpath);
		    }
		    closedir(dir);
		}
		if (AskConfirmation && NoRemoveOpt == 0) {
		    if (YesNo(dpath)) {
			if (rmdir(dpath) < 0) {
			    logerr("%-32s rmdir failed: %s\n",
				dpath, strerror(errno)
			    );
			}
			CountRemovedItems++;
		    }
		} else {
		    if (NoRemoveOpt) {
			if (VerboseOpt)
			    logstd("%-32s not-removed\n", dpath);
		    } else if (rmdir(dpath) == 0) {
			if (VerboseOpt)
			    logstd("%-32s rmdir-ok\n", dpath);
			CountRemovedItems++;
		    } else {
			logerr("%-32s rmdir failed: %s\n",
			    dpath, strerror(errno)
			);
		    }
		}
	    } else {
		if (AskConfirmation && NoRemoveOpt == 0) {
		    if (YesNo(dpath)) {
			if (remove(dpath) < 0) {
			    logerr("%-32s remove failed: %s\n",
				dpath, strerror(errno)
			    );
			}
			CountRemovedItems++;
		    }
		} else {
		    if (NoRemoveOpt) {
			if (VerboseOpt)
			    logstd("%-32s not-removed\n", dpath);
		    } else if (remove(dpath) == 0) {
			if (VerboseOpt)
			    logstd("%-32s remove-ok\n", dpath);
			CountRemovedItems++;
		    } else {
			logerr("%-32s remove failed: %s\n",
			    dpath, strerror(errno)
			);
		    }
		}
	    }
	}
    }
}

void
InitList(List *list)
{
    bzero(list, sizeof(List));
    list->li_Node.no_Next = &list->li_Node;
}

void 
ResetList(List *list)
{
    Node *node;

    while ((node = list->li_Node.no_Next) != &list->li_Node) {
	list->li_Node.no_Next = node->no_Next;
	free(node);
    }
    InitList(list);
}

int
AddList(List *list, const char *name, int n)
{
    Node *node;
    int hv = shash(name);

    /*
     * Scan against wildcards.  Only a node value of 1 can be a wildcard
     * ( usually scanned from .cpignore )
     */

    for (node = list->li_Hash[0]; node; node = node->no_HNext) {
	if (strcmp(name, node->no_Name) == 0 ||
	    (n != 1 && node->no_Value == 1 && WildCmp(node->no_Name, name) == 0)
	) {
	    return(node->no_Value);
	}
    }

    /*
     * Look for exact match
     */

    for (node = list->li_Hash[hv]; node; node = node->no_HNext) {
	if (strcmp(name, node->no_Name) == 0) {
	    return(node->no_Value);
	}
    }
    node = malloc(sizeof(Node) + strlen(name) + 1);

    node->no_Next = list->li_Node.no_Next;
    list->li_Node.no_Next = node;

    node->no_HNext = list->li_Hash[hv];
    list->li_Hash[hv] = node;

    strcpy(node->no_Name, name);
    node->no_Value = n;

    return(n);
}

static int
shash(const char *s)
{
    int hv = 0xA4FB3255;

    while (*s) {
	if (*s == '*' || *s == '?' || 
	    *s == '{' || *s == '}' || 
	    *s == '[' || *s == ']' ||
	    *s == '|'
	) {
	    return(0);
	}
	hv = (hv << 5) ^ *s ^ (hv >> 23);
	++s;
    }
    return(((hv >> 16) ^ hv) & HMASK);
}

/*
 * WildCmp() - compare wild string to sane string
 *
 *	Return 0 on success, -1 on failure.
 */

int
WildCmp(const char *w, const char *s)
{
    /*
     * skip fixed portion
     */
  
    for (;;) {
	switch(*w) {
	case '*':
	    if (w[1] == 0)	/* optimize wild* case */
		return(0);
	    {
		int i;
		int l = strlen(s);

		for (i = 0; i <= l; ++i) {
		    if (WildCmp(w + 1, s + i) == 0)
			return(0);
		}
	    }
	    return(-1);
	case '?':
	    if (*s == 0)
		return(-1);
	    ++w;
	    ++s;
	    break;
	default:
	    if (*w != *s)
		return(-1);
	    if (*w == 0)	/* terminator */
		return(0);
	    ++w;
	    ++s;
	    break;
	}
    }
    /* not reached */
    return(-1);
}

int
YesNo(const char *path)
{
    int ch, first;

    (void)fprintf(stderr, "remove %s (Yes/No) [No]? ", path);
    (void)fflush(stderr);

    first = ch = getchar();
    while (ch != '\n' && ch != EOF)
	ch = getchar();
    return ((first == 'y' || first == 'Y'));
}

static void 
md5_flush(void)
{
    if (MD5SCacheDirty && MD5SCache) {
	FILE *fo;

	if ((fo = fopen(MD5SCache, "w")) != NULL) {
	    MD5Node *node;

	    for (node = MD5Base; node; node = node->md_Next) {
		if (node->md_Accessed && node->md_Code) {
		    fprintf(fo, "%s %d %s\n", 
			node->md_Code, 
			strlen(node->md_Name),
			node->md_Name
		    );
		}
	    }
	    fclose(fo);
	}
    }

    MD5SCacheDirty = 0;

    if (MD5SCache) {
	MD5Node *node;

	while ((node = MD5Base) != NULL) {
	    MD5Base = node->md_Next;

	    if (node->md_Code)
		free(node->md_Code);
	    if (node->md_Name)
		free(node->md_Name);
	    free(node);
	}
	free(MD5SCache);
	MD5SCache = NULL;
    }
}

static void
md5_cache(const char *spath, int sdirlen)
{
    FILE *fi;

    /*
     * Already cached
     */

    if (
	MD5SCache &&
	sdirlen == MD5SCacheDirLen &&
	strncmp(spath, MD5SCache, sdirlen) == 0
    ) {
	return;
    }

    /*
     * Different cache, flush old cache
     */

    if (MD5SCache != NULL)
	md5_flush();

    /*
     * Create new cache
     */

    MD5SCacheDirLen = sdirlen;
    MD5SCache = mprintf("%*.*s%s", sdirlen, sdirlen, spath, MD5CacheFile);

    if ((fi = fopen(MD5SCache, "r")) != NULL) {
	MD5Node **pnode = &MD5Base;
	int c;

	c = fgetc(fi);
	while (c != EOF) {
	    MD5Node *node = *pnode = malloc(sizeof(MD5Node));
	    char *s;
	    int nlen = 0;

	    bzero(node, sizeof(MD5Node));
	    node->md_Code = fextract(fi, -1, &c, ' ');
	    node->md_Accessed = 1;
	    if ((s = fextract(fi, -1, &c, ' ')) != NULL) {
		nlen = strtol(s, NULL, 0);
		free(s);
	    }
	    /*
	     * extracting md_Name - name may contain embedded control 
	     * characters.
	     */
	    CountReadBytes += nlen+1;
	    node->md_Name = fextract(fi, nlen, &c, EOF);
	    if (c != '\n') {
		fprintf(stderr, "Error parsing MD5 Cache: %s (%c)\n", MD5SCache, c);
		while (c != EOF && c != '\n')
		    c = fgetc(fi);
	    }
	    if (c != EOF)
		c = fgetc(fi);
	    pnode = &node->md_Next;
	}
	fclose(fi);
    }
}

/*
 * md5_lookup:	lookup/create md5 entry
 */

static MD5Node *
md5_lookup(const char *sfile)
{
    MD5Node **pnode;
    MD5Node *node;

    for (pnode = &MD5Base; (node = *pnode) != NULL; pnode = &node->md_Next) {
	if (strcmp(sfile, node->md_Name) == 0) {
	    break;
	}
    }
    if (node == NULL) {
	node = *pnode = malloc(sizeof(MD5Node));
	bzero(node, sizeof(MD5Node));
	node->md_Name = strdup(sfile);
    }
    node->md_Accessed = 1;
    return(node);
}

/*
 * md5_check:  check MD5 against file
 *
 *	Return -1 if check failed
 *	Return 0  if check succeeded
 *
 * dpath can be NULL, in which case we are force-updating
 * the source MD5.
 */

static int
md5_check(const char *spath, const char *dpath)
{
    const char *sfile;
    char *dcode;
    int sdirlen;
    int r = -1;
    MD5Node *node;

    if ((sfile = strrchr(spath, '/')) != NULL)
	++sfile;
    else
	sfile = spath;
    sdirlen = sfile - spath;

    md5_cache(spath, sdirlen);

    node = md5_lookup(sfile);

    /*
     * If dpath == NULL, we are force-updating the source .MD5* files
     */

    if (dpath == NULL) {
	char *scode = doMD5File(spath, NULL);

	r = 0;
	if (node->md_Code == NULL) {
	    r = -1;
	    node->md_Code = scode;
	    MD5SCacheDirty = 1;
	} else if (strcmp(scode, node->md_Code) != 0) {
	    r = -1;
	    free(node->md_Code);
	    node->md_Code = scode;
	    MD5SCacheDirty = 1;
	} else {
	    free(scode);
	}
	return(r);
    }

    /*
     * Otherwise the .MD5* file is used as a cache.
     */

    if (node->md_Code == NULL) {
	node->md_Code = doMD5File(spath, NULL);
	MD5SCacheDirty = 1;
    }

    dcode = doMD5File(dpath, NULL);
    if (dcode) {
	if (strcmp(node->md_Code, dcode) == 0) {
	    r = 0;
	} else {
	    char *scode = doMD5File(spath, NULL);

	    if (strcmp(node->md_Code, scode) == 0) {
		    free(scode);
	    } else {
		    free(node->md_Code);
		    node->md_Code = scode;
		    MD5SCacheDirty = 1;
		    if (strcmp(node->md_Code, dcode) == 0)
			r = 0;
	    }
	}
	free(dcode);
    }
    return(r);
}

/*
 * xrename() - rename with override
 *
 *	If the rename fails, attempt to override st_flags on the 
 *	destination and rename again.  If that fails too, try to
 *	set the flags back the way they were and give up.
 */

static int
xrename(const char *src, const char *dst, u_long flags)
{
    int r = 0;

    if ((r = rename(src, dst)) < 0) {
	chflags(dst, 0);
	if ((r = rename(src, dst)) < 0)
		chflags(dst, flags);
    }
    return(r);
}

static int
xlink(const char *src, const char *dst, u_long flags)
{
    int r = 0;
    int e;

    if ((r = link(src, dst)) < 0) {
	chflags(src, 0);
	r = link(src, dst);
	e = errno;
	chflags(src, flags);
	errno = e;
    }
    return(r);
}

static char *
fextract(FILE *fi, int n, int *pc, int skip)
{
    int i = 0;
    int imax = (n < 0) ? 64 : n + 1;
    char *s = malloc(imax);
    int c = *pc;

    while (c != EOF) {
	if (n == 0 || (n < 0 && (c == ' ' || c == '\n')))
	    break;

	s[i++] = c;
	if (i == imax) {
	    imax += 64;
	    s = realloc(s, imax);
	}
	if (n > 0)
	    --n;
	c = getc(fi);
    }
    if (c == skip && skip != EOF)
	c = getc(fi);
    *pc = c;
    s[i] = 0;
    return(s);
}

char *
doMD5File(const char *filename, char *buf)
{
    if (SummaryOpt) {
	struct stat st;
	if (stat(filename, &st) == 0) {
	    u_int64_t size = st.st_blocks * 512;
	    if (st.st_size % 512) 
		size += st.st_size % 512 - 512;
	    CountReadBytes += size;
	}
    }
    return MD5File(filename, buf);
}


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
 *	- Is able to do incremental mirroring/backups via hardlinks from
 *	  the 'previous' version (supplied with -H path).
 *
 * $DragonFly: src/bin/cpdup/cpdup.c,v 1.32 2008/11/11 04:36:00 dillon Exp $
 */

/*-
 * Example: cc -O cpdup.c -o cpdup -lmd
 *
 * ".MD5.CHECKSUMS" contains md5 checksumms for the current directory.
 * This file is stored on the source.
 */

#include "cpdup.h"
#include "hclink.h"
#include "hcproto.h"

#define HSIZE	8192
#define HMASK	(HSIZE-1)
#define HLSIZE	8192
#define HLMASK	(HLSIZE - 1)

#define MAXDEPTH	32	/* max copy depth for thread */
#define GETBUFSIZE	8192
#define GETPATHSIZE	2048
#define GETLINKSIZE	1024
#define GETIOSIZE	65536

#ifndef _ST_FLAGS_PRESENT_
#define st_flags	st_mode
#endif

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
    int	refs;
    struct hlink *next;
    struct hlink *prev;
    nlink_t nlinked;
    char name[0];
};

typedef struct copy_info {
	char *spath;
	char *dpath;
	dev_t sdevNo;
	dev_t ddevNo;
#ifdef USE_PTHREADS
	struct copy_info *parent;
	pthread_cond_t cond;
	int children;
	int r;
#endif
} *copy_info_t;

struct hlink *hltable[HLSIZE];

void RemoveRecur(const char *dpath, dev_t devNo);
void InitList(List *list);
void ResetList(List *list);
int AddList(List *list, const char *name, int n);
static struct hlink *hltlookup(struct stat *);
static struct hlink *hltadd(struct stat *, const char *);
static char *checkHLPath(struct stat *st, const char *spath, const char *dpath);
static int validate_check(const char *spath, const char *dpath);
static int shash(const char *s);
static void hltdelete(struct hlink *);
static void hltsetdino(struct hlink *, ino_t);
int YesNo(const char *path);
static int xrename(const char *src, const char *dst, u_long flags);
static int xlink(const char *src, const char *dst, u_long flags);
static int xremove(struct HostConf *host, const char *path);
int WildCmp(const char *s1, const char *s2);
static int DoCopy(copy_info_t info, int depth);

int AskConfirmation = 1;
int SafetyOpt = 1;
int ForceOpt;
int DeviceOpt = 1;
int VerboseOpt;
int QuietOpt;
int NoRemoveOpt;
int UseMD5Opt;
int UseFSMIDOpt;
int SummaryOpt;
int CompressOpt;
int SlaveOpt;
int EnableDirectoryRetries;
int DstBaseLen;
int ValidateOpt;
int CurParallel;
int MaxParallel = -1;
int HardLinkCount;
int ssh_argc;
const char *ssh_argv[16];
int RunningAsUser;
int RunningAsRoot;
const char *UseCpFile;
const char *UseHLPath;
const char *MD5CacheFile;
const char *FSMIDCacheFile;

int64_t CountSourceBytes;
int64_t CountSourceItems;
int64_t CountCopiedItems;
int64_t CountSourceReadBytes;
int64_t CountTargetReadBytes;
int64_t CountWriteBytes;
int64_t CountRemovedItems;
int64_t CountLinkedItems;

struct HostConf SrcHost;
struct HostConf DstHost;

#if USE_PTHREADS
pthread_mutex_t MasterMutex;
#endif

int
main(int ac, char **av)
{
    int i;
    char *src = NULL;
    char *dst = NULL;
    char *ptr;
    struct timeval start;
    struct copy_info info;

    signal(SIGPIPE, SIG_IGN);

    RunningAsUser = (geteuid() != 0);
    RunningAsRoot = !RunningAsUser;

#if USE_PTHREADS
    for (i = 0; i < HCTHASH_SIZE; ++i) {
	pthread_mutex_init(&SrcHost.hct_mutex[i], NULL);
	pthread_mutex_init(&DstHost.hct_mutex[i], NULL);
    }
    pthread_mutex_init(&MasterMutex, NULL);
    pthread_mutex_lock(&MasterMutex);
#endif

    gettimeofday(&start, NULL);
    for (i = 1; i < ac; ++i) {
	int v = 1;

	ptr = av[i];
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
	case 'C':
	    CompressOpt = 1;
	    break;
	case 'v':
	    VerboseOpt = 1;
	    while (*ptr == 'v') {
		++VerboseOpt;
		++ptr;
	    }
	    if (*ptr >= '0' && *ptr <= '9')
		VerboseOpt = strtol(ptr, NULL, 0);
	    break;
	case 'l':
	    setlinebuf(stdout);
	    setlinebuf(stderr);
	    break;
	case 'V':
	    ValidateOpt = v;
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
	case 'H':
	    UseHLPath = (*ptr) ? ptr : av[++i];
	    break;
	case 'F':
	    if (ssh_argc >= 16)
		fatal("too many -F options");
	    ssh_argv[ssh_argc++] = (*ptr) ? ptr : av[++i];
	    break;
	case 'S':
	    SlaveOpt = v;
	    break;
	case 'f':
	    ForceOpt = v;
	    break;
	case 'i':
	    AskConfirmation = v;
	    break;
	case 'j':
	    DeviceOpt = v;
	    break;
	case 'p':
	    MaxParallel = v;
	    break;
	case 's':
	    SafetyOpt = v;
	    break;
	case 'q':
	    QuietOpt = v;
	    break;
	case 'k':
	    UseFSMIDOpt = v;
	    FSMIDCacheFile = ".FSMID.CHECK";
	    break;
	case 'K':
	    UseFSMIDOpt = v;
	    FSMIDCacheFile = av[++i];
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
     * If we are told to go into slave mode, run the HC protocol
     */
    if (SlaveOpt) {
	hc_slave(0, 1);
	exit(0);
    }

    /*
     * Extract the source and/or/neither target [user@]host and
     * make any required connections.
     */
    if (src && (ptr = strchr(src, ':')) != NULL) {
	asprintf(&SrcHost.host, "%*.*s", (int)(ptr - src), (int)(ptr - src), src);
	src = ptr + 1;
	if (UseCpFile) {
	    fprintf(stderr, "The cpignore options are not currently supported for remote sources\n");
	    exit(1);
	}
	if (UseMD5Opt) {
	    fprintf(stderr, "The MD5 options are not currently supported for remote sources\n");
	    exit(1);
	}
	if (hc_connect(&SrcHost) < 0)
	    exit(1);
    }
    if (dst && (ptr = strchr(dst, ':')) != NULL) {
	asprintf(&DstHost.host, "%*.*s", (int)(ptr - dst), (int)(ptr - dst), dst);
	dst = ptr + 1;
	if (UseFSMIDOpt) {
	    fprintf(stderr, "The FSMID options are not currently supported for remote targets\n");
	    exit(1);
	}
	if (hc_connect(&DstHost) < 0)
	    exit(1);
    }

    /*
     * dst may be NULL only if -m option is specified,
     * which forces an update of the MD5 checksums
     */
    if (dst == NULL && UseMD5Opt == 0) {
	fatal(NULL);
	/* not reached */
    }
    bzero(&info, sizeof(info));
#if USE_PTHREADS
    info.r = 0;
    info.children = 0;
    pthread_cond_init(&info.cond, NULL);
#endif
    if (dst) {
	DstBaseLen = strlen(dst);
	info.spath = src;
	info.dpath = dst;
	info.sdevNo = (dev_t)-1;
	info.ddevNo = (dev_t)-1;
	i = DoCopy(&info, -1);
    } else {
	info.spath = src;
	info.dpath = NULL;
	info.sdevNo = (dev_t)-1;
	info.ddevNo = (dev_t)-1;
	i = DoCopy(&info, -1);
    }
#if USE_PTHREADS
    pthread_cond_destroy(&info.cond);
#endif
#ifndef NOMD5
    md5_flush();
#endif
    fsmid_flush();

    if (SummaryOpt && i == 0) {
	double duration;
	struct timeval end;

	gettimeofday(&end, NULL);
#if 0
	/* don't count stat's in our byte statistics */
	CountSourceBytes += sizeof(struct stat) * CountSourceItems;
	CountSourceReadBytes += sizeof(struct stat) * CountSourceItems;
	CountWriteBytes +=  sizeof(struct stat) * CountCopiedItems;
	CountWriteBytes +=  sizeof(struct stat) * CountRemovedItems;
#endif

	duration = (end.tv_sec - start.tv_sec);
	duration += (double)(end.tv_usec - start.tv_usec) / 1000000.0;
	if (duration == 0.0)
		duration = 1.0;
	logstd("cpdup completed successfully\n");
	logstd("%lld bytes source, %lld src bytes read, %lld tgt bytes read\n"
	       "%lld bytes written (%.1fX speedup)\n",
	    (long long)CountSourceBytes,
	    (long long)CountSourceReadBytes,
	    (long long)CountTargetReadBytes,
	    (long long)CountWriteBytes,
	    ((double)CountSourceBytes * 2.0) / ((double)(CountSourceReadBytes + CountTargetReadBytes + CountWriteBytes)));
 	logstd("%lld source items, %lld items copied, %lld items linked, "
	       "%lld things deleted\n",
	    (long long)CountSourceItems,
	    (long long)CountCopiedItems,
	    (long long)CountLinkedItems,
	    (long long)CountRemovedItems);
	logstd("%.1f seconds %5d Kbytes/sec synced %5d Kbytes/sec scanned\n",
	    duration,
	    (int)((CountSourceReadBytes + CountTargetReadBytes + CountWriteBytes) / duration  / 1024.0),
	    (int)(CountSourceBytes / duration / 1024.0));
    }
    exit((i == 0) ? 0 : 1);
}

static struct hlink *
hltlookup(struct stat *stp)
{
#if USE_PTHREADS
    struct timespec ts = { 0, 100000 };
#endif
    struct hlink *hl;
    int n;

    n = stp->st_ino & HLMASK;

#if USE_PTHREADS
again:
#endif
    for (hl = hltable[n]; hl; hl = hl->next) {
        if (hl->ino == stp->st_ino) {
#if USE_PTHREADS
	    /*
	     * If the hl entry is still in the process of being created
	     * by another thread we have to wait until it has either been
	     * deleted or completed.
	     */
	    if (hl->refs) {
		pthread_mutex_unlock(&MasterMutex);
		nanosleep(&ts, NULL);
		pthread_mutex_lock(&MasterMutex);
		goto again;
	    }
#endif
	    ++hl->refs;
	    return hl;
	}
    }

    return NULL;
}

static struct hlink *
hltadd(struct stat *stp, const char *path)
{
    struct hlink *new;
    int plen = strlen(path);
    int n;

    new = malloc(offsetof(struct hlink, name[plen + 1]));
    if (new == NULL) {
        fprintf(stderr, "out of memory\n");
        exit(EXIT_FAILURE);
    }
    ++HardLinkCount;

    /* initialize and link the new element into the table */
    new->ino = stp->st_ino;
    new->dino = (ino_t)-1;
    new->refs = 1;
    bcopy(path, new->name, plen + 1);
    new->nlinked = 1;
    new->prev = NULL;
    n = stp->st_ino & HLMASK;
    new->next = hltable[n];
    if (hltable[n])
        hltable[n]->prev = new;
    hltable[n] = new;

    return new;
}

static void
hltsetdino(struct hlink *hl, ino_t inum)
{
    hl->dino = inum;
}

static void
hltdelete(struct hlink *hl)
{
    assert(hl->refs == 1);
    --hl->refs;
    if (hl->prev) {
        if (hl->next)
            hl->next->prev = hl->prev;
        hl->prev->next = hl->next;
    } else {
        if (hl->next)
            hl->next->prev = NULL;

        hltable[hl->ino & HLMASK] = hl->next;
    }
    --HardLinkCount;
    free(hl);
}

static void
hltrels(struct hlink *hl)
{
    assert(hl->refs == 1);
    --hl->refs;
}

/*
 * If UseHLPath is defined check to see if the file in question is
 * the same as the source file, and if it is return a pointer to the
 * -H path based file for hardlinking.  Else return NULL.
 */
static char *
checkHLPath(struct stat *st1, const char *spath, const char *dpath)
{
    struct stat sthl;
    char *hpath;
    int error;

    asprintf(&hpath, "%s%s", UseHLPath, dpath + DstBaseLen);

    /*
     * stat info matches ?
     */
    if (hc_stat(&DstHost, hpath, &sthl) < 0 ||
	st1->st_size != sthl.st_size ||
	st1->st_mtime != sthl.st_mtime ||
	(RunningAsRoot && (st1->st_uid != sthl.st_uid ||
			   st1->st_gid != sthl.st_gid))
    ) {
	free(hpath);
	return(NULL);
    }

    /*
     * If ForceOpt or ValidateOpt is set we have to compare the files
     */
    if (ForceOpt || ValidateOpt) {
	error = validate_check(spath, hpath);
	if (error) {
	    free(hpath);
	    hpath = NULL;
	}
    }
    return(hpath);
}

/*
 * Return 0 if the contents of the file <spath> matches the contents of
 * the file <dpath>.
 */
static int
validate_check(const char *spath, const char *dpath)
{
    int error;
    int fd1;
    int fd2;

    fd1 = hc_open(&SrcHost, spath, O_RDONLY, 0);
    fd2 = hc_open(&DstHost, dpath, O_RDONLY, 0);
    error = -1;

    if (fd1 >= 0 && fd2 >= 0) {
	int n;
	int x;
	char *iobuf1 = malloc(GETIOSIZE);
	char *iobuf2 = malloc(GETIOSIZE);

	while ((n = hc_read(&SrcHost, fd1, iobuf1, GETIOSIZE)) > 0) {
	    CountSourceReadBytes += n;
	    x = hc_read(&DstHost, fd2, iobuf2, GETIOSIZE);
	    if (x > 0)
		    CountTargetReadBytes += x;
	    if (x != n)
		break;
	    if (bcmp(iobuf1, iobuf2, n) != 0)
		break;
	}
	free(iobuf1);
	free(iobuf2);
	if (n == 0)
	    error = 0;
    }
    if (fd1 >= 0)
	hc_close(&SrcHost, fd1);
    if (fd2 >= 0)
	hc_close(&DstHost, fd2);
    return (error);
}
#if USE_PTHREADS

static void *
DoCopyThread(void *arg)
{
    copy_info_t cinfo = arg;
    char *spath = cinfo->spath;
    char *dpath = cinfo->dpath;
    int r;
 
    r = pthread_detach(pthread_self());
    assert(r == 0);
    pthread_cond_init(&cinfo->cond, NULL);
    pthread_mutex_lock(&MasterMutex);
    cinfo->r += DoCopy(cinfo, 0);
    /* cinfo arguments invalid on return */
    --cinfo->parent->children;
    --CurParallel;
    pthread_cond_signal(&cinfo->parent->cond);
    free(spath);
    if (dpath)
	free(dpath);
    pthread_cond_destroy(&cinfo->cond);
    free(cinfo);
    hcc_free_trans(&SrcHost);
    hcc_free_trans(&DstHost);
    pthread_mutex_unlock(&MasterMutex);
    return(NULL);
}

#endif

int
DoCopy(copy_info_t info, int depth)
{
    const char *spath = info->spath;
    const char *dpath = info->dpath;
    dev_t sdevNo = info->sdevNo;
    dev_t ddevNo = info->ddevNo;
    struct stat st1;
    struct stat st2;
    unsigned long st2_flags;
    int r, mres, fres, st2Valid;
    struct hlink *hln;
    List *list = malloc(sizeof(List));
    u_int64_t size;

    InitList(list);
    r = mres = fres = st2Valid = 0;
    st2_flags = 0;
    size = 0;
    hln = NULL;

    if (hc_lstat(&SrcHost, spath, &st1) != 0) {
	r = 0;
	goto done;
    }
#ifdef SF_SNAPSHOT
    /* skip snapshot files because they're sparse and _huge_ */
    if (st1.st_flags & SF_SNAPSHOT)
       return(0);
#endif
    st2.st_mode = 0;	/* in case lstat fails */
    st2.st_flags = 0;	/* in case lstat fails */
    if (dpath && hc_lstat(&DstHost, dpath, &st2) == 0) {
	st2Valid = 1;
#ifdef _ST_FLAGS_PRESENT_
	st2_flags = st2.st_flags;
#endif
    }

    if (S_ISREG(st1.st_mode)) {
	size = st1.st_size;
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
                    if (hln->nlinked == st1.st_nlink) {
                        hltdelete(hln);
			hln = NULL;
		    }
		    CountSourceItems++;
		    r = 0;
		    goto done;
                } else {
		    /*
		     * hard link is not correct, attempt to unlink it
		     */
                    if (xremove(&DstHost, dpath) < 0) {
			logerr("%-32s hardlink: unable to unlink: %s\n", 
			    ((dpath) ? dpath : spath), strerror(errno));
                        hltdelete(hln);
			hln = NULL;
			++r;
			goto done;
		    }
                }
            }

            if (xlink(hln->name, dpath, st1.st_flags) < 0) {
		int tryrelink = (errno == EMLINK);
		logerr("%-32s hardlink: unable to link to %s: %s\n",
		    (dpath ? dpath : spath), hln->name, strerror(errno)
		);
                hltdelete(hln);
                hln = NULL;
		if (tryrelink) {
		    logerr("%-20s hardlink: will attempt to copy normally\n");
		    goto relink;
		}
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
		    r = 0;
		    goto done;
		}
            }
        } else {
	    /*
	     * first instance of hardlink must be copied normally
	     */
relink:
            hln = hltadd(&st1, dpath);
	}
    }

    /*
     * Do we need to copy the file/dir/link/whatever?  Early termination
     * if we do not.  Always redo links.  Directories are always traversed
     * except when the FSMID options are used.
     *
     * NOTE: st2Valid is true only if dpath != NULL *and* dpath stats good.
     */

    if (
	st2Valid
	&& st1.st_mode == st2.st_mode
#ifdef _ST_FLAGS_PRESENT_
	&& (RunningAsUser || st1.st_flags == st2.st_flags)
#endif
    ) {
	if (S_ISLNK(st1.st_mode) || S_ISDIR(st1.st_mode)) {
	    /*
	     * If FSMID tracking is turned on we can avoid recursing through
	     * an entire directory subtree if the FSMID matches.
	     */
#ifdef _ST_FSMID_PRESENT_
	    if (ForceOpt == 0 &&
		(UseFSMIDOpt && (fres = fsmid_check(st1.st_fsmid, dpath)) == 0)
	    ) {
		if (VerboseOpt >= 3) {
		    if (UseFSMIDOpt)
			logstd("%-32s fsmid-nochange\n", (dpath ? dpath : spath));
		    else
			logstd("%-32s nochange\n", (dpath ? dpath : spath));
		}
		r = 0;
		goto done;
	    }
#endif
	} else {
	    if (ForceOpt == 0 &&
		st1.st_size == st2.st_size &&
		st1.st_mtime == st2.st_mtime &&
		(RunningAsUser || (st1.st_uid == st2.st_uid &&
				   st1.st_gid == st2.st_gid))
#ifndef NOMD5
		&& (UseMD5Opt == 0 || !S_ISREG(st1.st_mode) ||
		    (mres = md5_check(spath, dpath)) == 0)
#endif
#ifdef _ST_FSMID_PRESENT_
		&& (UseFSMIDOpt == 0 ||
		    (fres = fsmid_check(st1.st_fsmid, dpath)) == 0)
#endif
		&& (ValidateOpt == 0 || !S_ISREG(st1.st_mode) ||
		    validate_check(spath, dpath) == 0)
	    ) {
		/*
		 * The files are identical, but if we are not running as
		 * root we might need to adjust ownership/group/flags.
		 */
		int changedown = 0;
		int changedflags = 0;
                if (hln)
		    hltsetdino(hln, st2.st_ino);

		if (RunningAsUser && (st1.st_uid != st2.st_uid ||
				      st1.st_gid != st2.st_gid)) {
			hc_chown(&DstHost, dpath, st1.st_uid, st1.st_gid);
			changedown = 1;
		}
#ifdef _ST_FLAGS_PRESENT_
		if (RunningAsUser && st1.st_flags != st2.st_flags) {
			hc_chflags(&DstHost, dpath, st1.st_flags);
			changedflags = 1;
		}
#endif
		if (VerboseOpt >= 3) {
#ifndef NOMD5
		    if (UseMD5Opt) {
			logstd("%-32s md5-nochange",
				(dpath ? dpath : spath));
		    } else
#endif
		    if (UseFSMIDOpt) {
			logstd("%-32s fsmid-nochange",
				(dpath ? dpath : spath));
		    } else if (ValidateOpt) {
			logstd("%-32s nochange (contents validated)",
				(dpath ? dpath : spath));
		    } else {
			logstd("%-32s nochange", (dpath ? dpath : spath));
		    }
		    if (changedown)
			logstd(" (uid/gid differ)");
		    if (changedflags)
			logstd(" (flags differ)");
		    logstd("\n");
		}
		CountSourceBytes += size;
		CountSourceItems++;
		r = 0;
		goto done;
	    }
	}
    }
    if (st2Valid && !S_ISDIR(st1.st_mode) && S_ISDIR(st2.st_mode)) {
	if (SafetyOpt) {
	    logerr("%-32s SAFETY - refusing to copy file over directory\n",
		(dpath ? dpath : spath)
	    );
	    ++r;		/* XXX */
	    r = 0;
	    goto done; 		/* continue with the cpdup anyway */
	}
	if (QuietOpt == 0 || AskConfirmation) {
	    logstd("%-32s WARNING: non-directory source will blow away\n"
		   "%-32s preexisting dest directory, continuing anyway!\n",
		   ((dpath) ? dpath : spath), "");
	}
	if (dpath)
	    RemoveRecur(dpath, ddevNo);
	st2Valid = 0;
    }

    /*
     * The various comparisons failed, copy it.
     */
    if (S_ISDIR(st1.st_mode)) {
	DIR *dir;

	if (fres < 0)
	    logerr("%-32s/ fsmid-CHECK-FAILED\n", (dpath) ? dpath : spath);
	if ((dir = hc_opendir(&SrcHost, spath)) != NULL) {
	    struct dirent *den;
	    int noLoop = 0;

	    if (dpath) {
		if (S_ISDIR(st2.st_mode) == 0) {
		    xremove(&DstHost, dpath);
		    if (hc_mkdir(&DstHost, dpath, st1.st_mode | 0700) != 0) {
			logerr("%s: mkdir failed: %s\n", 
			    (dpath ? dpath : spath), strerror(errno));
			r = 1;
			noLoop = 1;
		    }

		    /*
		     * Matt: why don't you check error codes here?
		     * (Because I'm an idiot... checks added!)
		     */
		    if (hc_lstat(&DstHost, dpath, &st2) != 0) {
			logerr("%s: lstat of newly made dir failed: %s\n",
			    (dpath ? dpath : spath), strerror(errno));
			r = 1;
			noLoop = 1;
		    }
		    if (hc_chown(&DstHost, dpath, st1.st_uid, st1.st_gid) != 0){
			logerr("%s: chown of newly made dir failed: %s\n",
			    (dpath ? dpath : spath), strerror(errno));
			r = 1;
			noLoop = 1;
		    }
		    CountCopiedItems++;
		} else {
		    /*
		     * Directory must be scanable by root for cpdup to
		     * work.  We'll fix it later if the directory isn't
		     * supposed to be readable ( which is why we fixup
		     * st2.st_mode to match what we did ).
		     */
		    if ((st2.st_mode & 0700) != 0700) {
			hc_chmod(&DstHost, dpath, st2.st_mode | 0700);
			st2.st_mode |= 0700;
		    }
		    if (VerboseOpt >= 2)
			logstd("%s\n", dpath ? dpath : spath);
		}
	    }

	    /*
	     * When copying a directory, stop if the source crosses a mount
	     * point.
	     */
	    if (sdevNo != (dev_t)-1 && st1.st_dev != sdevNo) {
		noLoop = 1;
	    } else {
		sdevNo = st1.st_dev;
	    }

	    /*
	     * When copying a directory, stop if the destination crosses
	     * a mount point.
	     *
	     * The target directory will have been created and stat'd
	     * for st2 if it did not previously exist.   st2Valid is left
	     * as a flag.  If the stat failed st2 will still only have its
	     * default initialization.
	     *
	     * So we simply assume here that the directory is within the
	     * current target mount if we had to create it (aka st2Valid is 0)
	     * and we leave ddevNo alone.
	     */
	    if (st2Valid) {
		    if (ddevNo != (dev_t)-1 && st2.st_dev != ddevNo) {
			noLoop = 1;
		    } else {
			ddevNo = st2.st_dev;
		    }
	    }

	    /*
	     * scan .cpignore file for files/directories 
	     * to ignore.
	     */

	    if (UseCpFile) {
		FILE *fi;
		char *buf = malloc(GETBUFSIZE);
		char *fpath;

		if (UseCpFile[0] == '/') {
		    fpath = mprintf("%s", UseCpFile);
		} else {
		    fpath = mprintf("%s/%s", spath, UseCpFile);
		}
		AddList(list, strrchr(fpath, '/') + 1, 1);
		if ((fi = fopen(fpath, "r")) != NULL) {
		    while (fgets(buf, GETBUFSIZE, fi) != NULL) {
			int l = strlen(buf);
			CountSourceReadBytes += l;
			if (l && buf[l-1] == '\n')
			    buf[--l] = 0;
			if (buf[0])
			    AddList(list, buf, 1);
		    }
		    fclose(fi);
		}
		free(fpath);
		free(buf);
	    }

	    /*
	     * Automatically exclude MD5CacheFile that we create on the
	     * source from the copy to the destination.
	     *
	     * Automatically exclude a FSMIDCacheFile on the source that
	     * would otherwise overwrite the one we maintain on the target.
	     */
	    if (UseMD5Opt)
		AddList(list, MD5CacheFile, 1);
	    if (UseFSMIDOpt)
		AddList(list, FSMIDCacheFile, 1);

	    while (noLoop == 0 && (den = hc_readdir(&SrcHost, dir)) != NULL) {
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
		if (AddList(list, den->d_name, 0) == 1) {
		    continue;
		}
		nspath = mprintf("%s/%s", spath, den->d_name);
		if (dpath)
		    ndpath = mprintf("%s/%s", dpath, den->d_name);

#if USE_PTHREADS
		if (CurParallel < MaxParallel || depth > MAXDEPTH) {
		    copy_info_t cinfo = malloc(sizeof(*cinfo));
		    pthread_t dummy_thr;

		    bzero(cinfo, sizeof(*cinfo));
		    cinfo->spath = nspath;
		    cinfo->dpath = ndpath;
		    cinfo->sdevNo = sdevNo;
		    cinfo->ddevNo = ddevNo;
		    cinfo->parent = info;
		    ++CurParallel;
		    ++info->children;
		    pthread_create(&dummy_thr, NULL, DoCopyThread, cinfo);
		} else
#endif
		{
		    info->spath = nspath;
		    info->dpath = ndpath;
		    info->sdevNo = sdevNo;
		    info->ddevNo = ddevNo;
		    if (depth < 0)
			r += DoCopy(info, depth);
		    else
			r += DoCopy(info, depth + 1);
		    free(nspath);
		    if (ndpath)
			free(ndpath);
		    info->spath = NULL;
		    info->dpath = NULL;
		}
	    }

	    hc_closedir(&SrcHost, dir);

#if USE_PTHREADS
	    /*
	     * Wait for our children to finish
	     */
	    while (info->children) {
		pthread_cond_wait(&info->cond, &MasterMutex);
	    }
	    r += info->r;
	    info->r = 0;
#endif

	    /*
	     * Remove files/directories from destination that do not appear
	     * in the source.
	     */
	    if (dpath && (dir = hc_opendir(&DstHost, dpath)) != NULL) {
		while (noLoop == 0 && (den = hc_readdir(&DstHost, dir)) != NULL) {
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
		    if (AddList(list, den->d_name, 3) == 3) {
			char *ndpath;

			ndpath = mprintf("%s/%s", dpath, den->d_name);
			RemoveRecur(ndpath, ddevNo);
			free(ndpath);
		    }
		}
		hc_closedir(&DstHost, dir);
	    }

	    if (dpath) {
		struct timeval tv[2];

		if (ForceOpt ||
		    st2Valid == 0 || 
		    st1.st_uid != st2.st_uid ||
		    st1.st_gid != st2.st_gid
		) {
		    hc_chown(&DstHost, dpath, st1.st_uid, st1.st_gid);
		}
		if (st2Valid == 0 || st1.st_mode != st2.st_mode) {
		    hc_chmod(&DstHost, dpath, st1.st_mode);
		}
#ifdef _ST_FLAGS_PRESENT_
		if (st2Valid == 0 || st1.st_flags != st2.st_flags) {
		    hc_chflags(&DstHost, dpath, st1.st_flags);
		}
#endif
		if (ForceOpt ||
		    st2Valid == 0 ||
		    st1.st_mtime != st2.st_mtime
		) {
		    bzero(tv, sizeof(tv));
		    tv[0].tv_sec = st1.st_mtime;
		    tv[1].tv_sec = st1.st_mtime;
		    hc_utimes(&DstHost, dpath, tv);
		}
	    }
	}
    } else if (dpath == NULL) {
	/*
	 * If dpath is NULL, we are just updating the MD5
	 */
#ifndef NOMD5
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
#endif
    } else if (S_ISREG(st1.st_mode)) {
	char *path;
	char *hpath;
	int fd1;
	int fd2;

	if (st2Valid)
		path = mprintf("%s.tmp%d", dpath, (int)getpid());
	else
		path = mprintf("%s", dpath);

	/*
	 * Handle check failure message.
	 */
#ifndef NOMD5
	if (mres < 0)
	    logerr("%-32s md5-CHECK-FAILED\n", (dpath) ? dpath : spath);
	else 
#endif
	if (fres < 0)
	    logerr("%-32s fsmid-CHECK-FAILED\n", (dpath) ? dpath : spath);

	/*
	 * Not quite ready to do the copy yet.  If UseHLPath is defined,
	 * see if we can hardlink instead.
	 *
	 * If we can hardlink, and the target exists, we have to remove it
	 * first or the hardlink will fail.  This can occur in a number of
	 * situations but must typically when the '-f -H' combination is 
	 * used.
	 */
	if (UseHLPath && (hpath = checkHLPath(&st1, spath, dpath)) != NULL) {
		if (st2Valid)
			xremove(&DstHost, dpath);
		if (hc_link(&DstHost, hpath, dpath) == 0) {
			++CountLinkedItems;
			if (VerboseOpt) {
			    logstd("%-32s hardlinked(-H)\n",
				   (dpath ? dpath : spath));
			}
			free(hpath);
			goto skip_copy;
		}
		/*
		 * Shucks, we may have hit a filesystem hard linking limit,
		 * we have to copy instead.
		 */
		free(hpath);
	}

	if ((fd1 = hc_open(&SrcHost, spath, O_RDONLY, 0)) >= 0) {
	    if ((fd2 = hc_open(&DstHost, path, O_WRONLY|O_CREAT|O_EXCL, 0600)) < 0) {
		/*
		 * There could be a .tmp file from a previously interrupted
		 * run, delete and retry.  Fail if we still can't get at it.
		 */
#ifdef _ST_FLAGS_PRESENT_
		hc_chflags(&DstHost, path, 0);
#endif
		hc_remove(&DstHost, path);
		fd2 = hc_open(&DstHost, path, O_WRONLY|O_CREAT|O_EXCL|O_TRUNC, 0600);
	    }
	    if (fd2 >= 0) {
		const char *op;
		char *iobuf1 = malloc(GETIOSIZE);
		int n;

		/*
		 * Matt: What about holes?
		 */
		op = "read";
		while ((n = hc_read(&SrcHost, fd1, iobuf1, GETIOSIZE)) > 0) {
		    op = "write";
		    if (hc_write(&DstHost, fd2, iobuf1, n) != n)
			break;
		    op = "read";
		}
		hc_close(&DstHost, fd2);
		if (n == 0) {
		    struct timeval tv[2];

		    bzero(tv, sizeof(tv));
		    tv[0].tv_sec = st1.st_mtime;
		    tv[1].tv_sec = st1.st_mtime;

		    hc_chown(&DstHost, path, st1.st_uid, st1.st_gid);
		    hc_chmod(&DstHost, path, st1.st_mode);
#ifdef _ST_FLAGS_PRESENT_
		    if (st1.st_flags & (UF_IMMUTABLE|SF_IMMUTABLE))
			hc_utimes(&DstHost, path, tv);
#else
		    hc_utimes(&DstHost, path, tv);
#endif
		    if (st2Valid && xrename(path, dpath, st2_flags) != 0) {
			logerr("%-32s rename-after-copy failed: %s\n",
			    (dpath ? dpath : spath), strerror(errno)
			);
			++r;
		    } else {
			if (VerboseOpt)
			    logstd("%-32s copy-ok\n", (dpath ? dpath : spath));
#ifdef _ST_FLAGS_PRESENT_
			if (st1.st_flags)
			    hc_chflags(&DstHost, dpath, st1.st_flags);
#endif
		    }
#ifdef _ST_FLAGS_PRESENT_
		    if ((st1.st_flags & (UF_IMMUTABLE|SF_IMMUTABLE)) == 0)
			hc_utimes(&DstHost, dpath, tv);
#endif
		    CountSourceReadBytes += size;
		    CountWriteBytes += size;
		    CountSourceBytes += size;
		    CountSourceItems++;
		    CountCopiedItems++;
		} else {
		    logerr("%-32s %s failed: %s\n",
			(dpath ? dpath : spath), op, strerror(errno)
		    );
		    hc_remove(&DstHost, path);
		    ++r;
		}
		free(iobuf1);
	    } else {
		logerr("%-32s create (uid %d, euid %d) failed: %s\n",
		    (dpath ? dpath : spath), getuid(), geteuid(),
		    strerror(errno)
		);
		++r;
	    }
	    hc_close(&SrcHost, fd1);
	} else {
	    logerr("%-32s copy: open failed: %s\n",
		(dpath ? dpath : spath),
		strerror(errno)
	    );
	    ++r;
	}
skip_copy:
	free(path);

        if (hln) {
            if (!r && hc_stat(&DstHost, dpath, &st2) == 0) {
		hltsetdino(hln, st2.st_ino);
	    } else {
                hltdelete(hln);
		hln = NULL;
	    }
        }
    } else if (S_ISLNK(st1.st_mode)) {
	char *link1 = malloc(GETLINKSIZE);
	char *link2 = malloc(GETLINKSIZE);
	char *path;
	int n1;
	int n2;

	n1 = hc_readlink(&SrcHost, spath, link1, GETLINKSIZE - 1);
	if (st2Valid) {
		path = mprintf("%s.tmp%d", dpath, (int)getpid());
		n2 = hc_readlink(&DstHost, dpath, link2, GETLINKSIZE - 1);
	} else {
		path = mprintf("%s", dpath);
		n2 = -1;
	}
	if (n1 >= 0) {
	    if (ForceOpt || n1 != n2 || bcmp(link1, link2, n1) != 0) {
		hc_umask(&DstHost, ~st1.st_mode);
		xremove(&DstHost, path);
		link1[n1] = 0;
		if (hc_symlink(&DstHost, link1, path) < 0) {
                      logerr("%-32s symlink (%s->%s) failed: %s\n",
			  (dpath ? dpath : spath), link1, path,
			  strerror(errno)
		      );
		      ++r;
		} else {
		    hc_lchown(&DstHost, path, st1.st_uid, st1.st_gid);
		    /*
		     * there is no lchmod() or lchflags(), we 
		     * cannot chmod or chflags a softlink.
		     */
		    if (st2Valid && xrename(path, dpath, st2_flags) != 0) {
			logerr("%-32s rename softlink (%s->%s) failed: %s\n",
			    (dpath ? dpath : spath),
			    path, dpath, strerror(errno));
		    } else if (VerboseOpt) {
			logstd("%-32s softlink-ok\n", (dpath ? dpath : spath));
		    }
		    hc_umask(&DstHost, 000);
		    CountWriteBytes += n1;
		    CountCopiedItems++;
	  	}
	    } else {
		if (VerboseOpt >= 3)
		    logstd("%-32s nochange\n", (dpath ? dpath : spath));
	    }
	    CountSourceBytes += n1;
	    CountSourceReadBytes += n1;
	    if (n2 > 0) 
		CountTargetReadBytes += n2;
	    CountSourceItems++;
	} else {
	    r = 1;
	    logerr("%-32s softlink-failed\n", (dpath ? dpath : spath));
	}
	free(link1);
	free(link2);
	free(path);
    } else if ((S_ISCHR(st1.st_mode) || S_ISBLK(st1.st_mode)) && DeviceOpt) {
	char *path = NULL;

	if (ForceOpt ||
	    st2Valid == 0 || 
	    st1.st_mode != st2.st_mode || 
	    st1.st_rdev != st2.st_rdev ||
	    st1.st_uid != st2.st_uid ||
	    st1.st_gid != st2.st_gid
	) {
	    if (st2Valid) {
		path = mprintf("%s.tmp%d", dpath, (int)getpid());
		xremove(&DstHost, path);
	    } else {
		path = mprintf("%s", dpath);
	    }

	    if (hc_mknod(&DstHost, path, st1.st_mode, st1.st_rdev) == 0) {
		hc_chmod(&DstHost, path, st1.st_mode);
		hc_chown(&DstHost, path, st1.st_uid, st1.st_gid);
		if (st2Valid)
			xremove(&DstHost, dpath);
		if (st2Valid && xrename(path, dpath, st2_flags) != 0) {
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
	if (path)
		free(path);
	CountSourceItems++;
    }
done:
    if (hln) {
	if (hln->dino == (ino_t)-1) {
	    hltdelete(hln);
	    /*hln = NULL; unneeded */
	} else {
	    hltrels(hln);
	}
    }
    ResetList(list);
    free(list);
    return (r);
}

/*
 * RemoveRecur()
 */

void
RemoveRecur(const char *dpath, dev_t devNo)
{
    struct stat st;

    if (hc_lstat(&DstHost, dpath, &st) == 0) {
	if (devNo == (dev_t)-1)
	    devNo = st.st_dev;
	if (st.st_dev == devNo) {
	    if (S_ISDIR(st.st_mode)) {
		DIR *dir;

		if ((dir = hc_opendir(&DstHost, dpath)) != NULL) {
		    struct dirent *den;
		    while ((den = hc_readdir(&DstHost, dir)) != NULL) {
			char *ndpath;

			if (strcmp(den->d_name, ".") == 0)
			    continue;
			if (strcmp(den->d_name, "..") == 0)
			    continue;
			ndpath = mprintf("%s/%s", dpath, den->d_name);
			RemoveRecur(ndpath, devNo);
			free(ndpath);
		    }
		    hc_closedir(&DstHost, dir);
		}
		if (AskConfirmation && NoRemoveOpt == 0) {
		    if (YesNo(dpath)) {
			if (hc_rmdir(&DstHost, dpath) < 0) {
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
		    } else if (hc_rmdir(&DstHost, dpath) == 0) {
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
			if (xremove(&DstHost, dpath) < 0) {
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
		    } else if (xremove(&DstHost, dpath) == 0) {
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
    int hv;

    hv = shash(name);

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
    if (node == NULL) {
        fprintf(stderr, "out of memory\n");
        exit(EXIT_FAILURE);
    }

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
    int hv;

    hv = 0xA4FB3255;

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

    fprintf(stderr, "remove %s (Yes/No) [No]? ", path);
    fflush(stderr);

    first = ch = getchar();
    while (ch != '\n' && ch != EOF)
	ch = getchar();
    return ((first == 'y' || first == 'Y'));
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
    int r;

    if ((r = hc_rename(&DstHost, src, dst)) < 0) {
#ifdef _ST_FLAGS_PRESENT_
	hc_chflags(&DstHost, dst, 0);
	if ((r = hc_rename(&DstHost, src, dst)) < 0)
		hc_chflags(&DstHost, dst, flags);
#endif
    }
    return(r);
}

static int
xlink(const char *src, const char *dst, u_long flags)
{
    int r;
#ifdef _ST_FLAGS_PRESENT_
    int e;
#endif

    if ((r = hc_link(&DstHost, src, dst)) < 0) {
#ifdef _ST_FLAGS_PRESENT_
	hc_chflags(&DstHost, src, 0);
	r = hc_link(&DstHost, src, dst);
	e = errno;
	hc_chflags(&DstHost, src, flags);
	errno = e;
#endif
    }
    if (r == 0)
	    ++CountLinkedItems;
    return(r);
}

static int
xremove(struct HostConf *host, const char *path)
{
    int res;

    res = hc_remove(host, path);
#ifdef _ST_FLAGS_PRESENT_
    if (res == -EPERM) {
	hc_chflags(host, path, 0);
	res = hc_remove(host, path);
    }
#endif
    return(res);
}


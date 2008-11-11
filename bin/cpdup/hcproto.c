/*
 * HCPROTO.C
 *
 * This module implements a simple remote control protocol
 *
 * $DragonFly: src/bin/cpdup/hcproto.c,v 1.8 2008/11/11 04:36:00 dillon Exp $
 */

#include "cpdup.h"
#include "hclink.h"
#include "hcproto.h"

static int hc_decode_stat(struct stat *, struct HCHead *);
static int rc_encode_stat(hctransaction_t trans, struct stat *);

static int rc_hello(hctransaction_t trans, struct HCHead *);
static int rc_stat(hctransaction_t trans, struct HCHead *);
static int rc_lstat(hctransaction_t trans, struct HCHead *);
static int rc_opendir(hctransaction_t trans, struct HCHead *);
static int rc_readdir(hctransaction_t trans, struct HCHead *);
static int rc_closedir(hctransaction_t trans, struct HCHead *);
static int rc_open(hctransaction_t trans, struct HCHead *);
static int rc_close(hctransaction_t trans, struct HCHead *);
static int rc_read(hctransaction_t trans, struct HCHead *);
static int rc_write(hctransaction_t trans, struct HCHead *);
static int rc_remove(hctransaction_t trans, struct HCHead *);
static int rc_mkdir(hctransaction_t trans, struct HCHead *);
static int rc_rmdir(hctransaction_t trans, struct HCHead *);
static int rc_chown(hctransaction_t trans, struct HCHead *);
static int rc_lchown(hctransaction_t trans, struct HCHead *);
static int rc_chmod(hctransaction_t trans, struct HCHead *);
static int rc_mknod(hctransaction_t trans, struct HCHead *);
static int rc_link(hctransaction_t trans, struct HCHead *);
#ifdef _ST_FLAGS_PRESENT_
static int rc_chflags(hctransaction_t trans, struct HCHead *);
#endif
static int rc_readlink(hctransaction_t trans, struct HCHead *);
static int rc_umask(hctransaction_t trans, struct HCHead *);
static int rc_symlink(hctransaction_t trans, struct HCHead *);
static int rc_rename(hctransaction_t trans, struct HCHead *);
static int rc_utimes(hctransaction_t trans, struct HCHead *);

struct HCDesc HCDispatchTable[] = {
    { HC_HELLO,		rc_hello },
    { HC_STAT,		rc_stat },
    { HC_LSTAT,		rc_lstat },
    { HC_OPENDIR,	rc_opendir },
    { HC_READDIR,	rc_readdir },
    { HC_CLOSEDIR,	rc_closedir },
    { HC_OPEN,		rc_open },
    { HC_CLOSE,		rc_close },
    { HC_READ,		rc_read },
    { HC_WRITE,		rc_write },
    { HC_REMOVE,	rc_remove },
    { HC_MKDIR,		rc_mkdir },
    { HC_RMDIR,		rc_rmdir },
    { HC_CHOWN,		rc_chown },
    { HC_LCHOWN,	rc_lchown },
    { HC_CHMOD,		rc_chmod },
    { HC_MKNOD,		rc_mknod },
    { HC_LINK,		rc_link },
#ifdef _ST_FLAGS_PRESENT_
    { HC_CHFLAGS,	rc_chflags },
#endif
    { HC_READLINK,	rc_readlink },
    { HC_UMASK,		rc_umask },
    { HC_SYMLINK,	rc_symlink },
    { HC_RENAME,	rc_rename },
    { HC_UTIMES,	rc_utimes },
};

int
hc_connect(struct HostConf *hc)
{
    if (hcc_connect(hc) < 0) {
	fprintf(stderr, "Unable to connect to %s\n", hc->host);
	return(-1);
    }
    return(hc_hello(hc));
}

void
hc_slave(int fdin, int fdout)
{
    hcc_slave(fdin, fdout, HCDispatchTable,
	      sizeof(HCDispatchTable) / sizeof(HCDispatchTable[0]));
    
}

/*
 * A HELLO RPC is sent on the initial connect.
 */
int
hc_hello(struct HostConf *hc)
{
    struct HCHead *head;
    struct HCLeaf *item;
    hctransaction_t trans;
    char hostbuf[256];
    int error;

    bzero(hostbuf, sizeof(hostbuf));
    if (gethostname(hostbuf, sizeof(hostbuf) - 1) < 0)
        return(-1);
    if (hostbuf[0] == 0)
	hostbuf[0] = '?';

    trans = hcc_start_command(hc, HC_HELLO);
    hcc_leaf_string(trans, LC_HELLOSTR, hostbuf);
    hcc_leaf_int32(trans, LC_VERSION, HCPROTO_VERSION);
    if ((head = hcc_finish_command(trans)) == NULL) {
	fprintf(stderr, "Connected to %s but remote failed to complete hello\n",
		hc->host);
	return(-1);
    }

    if (head->error) {
	fprintf(stderr, "Connected to %s but remote returned error %d\n",
		hc->host, head->error);
	return(-1);
    }

    error = -1;
    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_HELLOSTR:
	    fprintf(stderr, "Handshaked with %s\n", HCC_STRING(item));
	    error = 0;
	    break;
	case LC_VERSION:
	    hc->version = HCC_INT32(item);
	    break;
	}
    }
    if (hc->version < HCPROTO_VERSION_COMPAT) {
	fprintf(stderr, "Remote cpdup at %s has an incompatible version\n",
		hc->host);
	error = -1;
    }
    if (error < 0)
	fprintf(stderr, "Handshake failed with %s\n", hc->host);
    return (error);
}

static int
rc_hello(hctransaction_t trans, struct HCHead *head __unused)
{
    char hostbuf[256];

    bzero(hostbuf, sizeof(hostbuf));
    if (gethostname(hostbuf, sizeof(hostbuf) - 1) < 0)
        return(-1);
    if (hostbuf[0] == 0)
	hostbuf[0] = '?';

    hcc_leaf_string(trans, LC_HELLOSTR, hostbuf);
    hcc_leaf_int32(trans, LC_VERSION, HCPROTO_VERSION);
    return(0);
}

/*
 * STAT, LSTAT
 */
int
hc_stat(struct HostConf *hc, const char *path, struct stat *st)
{
    struct HCHead *head;
    hctransaction_t trans;

    if (hc == NULL || hc->host == NULL)
	return(stat(path, st));

    trans = hcc_start_command(hc, HC_STAT);
    hcc_leaf_string(trans, LC_PATH1, path);
    if ((head = hcc_finish_command(trans)) == NULL)
	return(-1);
    if (head->error)
	return(-1);
    return(hc_decode_stat(st, head));
}

int
hc_lstat(struct HostConf *hc, const char *path, struct stat *st)
{
    struct HCHead *head;
    hctransaction_t trans;

    if (hc == NULL || hc->host == NULL)
	return(lstat(path, st));

    trans = hcc_start_command(hc, HC_LSTAT);
    hcc_leaf_string(trans, LC_PATH1, path);
    if ((head = hcc_finish_command(trans)) == NULL)
	return(-1);
    if (head->error)
	return(-1);
    return(hc_decode_stat(st, head));
}

static int
hc_decode_stat(struct stat *st, struct HCHead *head)
{
    struct HCLeaf *item;

    bzero(st, sizeof(*st));
    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_DEV:
		st->st_dev = HCC_INT32(item);
		break;
	case LC_INO:
		st->st_ino = HCC_INT64(item);
		break;
	case LC_MODE:
		st->st_mode = HCC_INT32(item);
		break;
	case LC_NLINK:
		st->st_nlink = HCC_INT32(item);
		break;
	case LC_UID:
		st->st_uid = HCC_INT32(item);
		break;
	case LC_GID:
		st->st_gid = HCC_INT32(item);
		break;
	case LC_RDEV:
		st->st_rdev = HCC_INT32(item);
		break;
	case LC_ATIME:
		st->st_atime = (time_t)HCC_INT64(item);
		break;
	case LC_MTIME:
		st->st_mtime = (time_t)HCC_INT64(item);
		break;
	case LC_CTIME:
		st->st_ctime = (time_t)HCC_INT64(item);
		break;
	case LC_FILESIZE:
		st->st_size = HCC_INT64(item);
		break;
	case LC_FILEBLKS:
		st->st_blocks = HCC_INT64(item);
		break;
	case LC_BLKSIZE:
		st->st_blksize = HCC_INT32(item);
		break;
#ifdef _ST_FSMID_PRESENT_
	case LC_FSMID:
		st->st_fsmid = HCC_INT64(item);
		break;
#endif
#ifdef _ST_FLAGS_PRESENT_
	case LC_FILEFLAGS:
		st->st_flags = (u_int32_t)HCC_INT64(item);
		break;
#endif
	}
    }
    return(0);
}

static int
rc_stat(hctransaction_t trans, struct HCHead *head)
{
    struct HCLeaf *item;
    struct stat st;
    const char *path = NULL;

    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_PATH1:
	    path = HCC_STRING(item);
	    break;
	}
    }
    if (path == NULL)
	return(-2);
    if (stat(path, &st) < 0)
	return(-1);
    return (rc_encode_stat(trans, &st));
}

static int
rc_lstat(hctransaction_t trans, struct HCHead *head)
{
    struct HCLeaf *item;
    struct stat st;
    const char *path = NULL;

    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_PATH1:
	    path = HCC_STRING(item);
	    break;
	}
    }
    if (path == NULL)
	return(-2);
    if (lstat(path, &st) < 0)
	return(-1);
    return (rc_encode_stat(trans, &st));
}

static int
rc_encode_stat(hctransaction_t trans, struct stat *st)
{
    hcc_leaf_int32(trans, LC_DEV, st->st_dev);
    hcc_leaf_int64(trans, LC_INO, st->st_ino);
    hcc_leaf_int32(trans, LC_MODE, st->st_mode);
    hcc_leaf_int32(trans, LC_NLINK, st->st_nlink);
    hcc_leaf_int32(trans, LC_UID, st->st_uid);
    hcc_leaf_int32(trans, LC_GID, st->st_gid);
    hcc_leaf_int32(trans, LC_RDEV, st->st_rdev);
    hcc_leaf_int64(trans, LC_ATIME, st->st_atime);
    hcc_leaf_int64(trans, LC_MTIME, st->st_mtime);
    hcc_leaf_int64(trans, LC_CTIME, st->st_ctime);
    hcc_leaf_int64(trans, LC_FILESIZE, st->st_size);
    hcc_leaf_int64(trans, LC_FILEBLKS, st->st_blocks);
    hcc_leaf_int32(trans, LC_BLKSIZE, st->st_blksize);
#ifdef _ST_FSMID_PRESENT_
    hcc_leaf_int64(trans, LC_FSMID, st->st_fsmid);
#endif
#ifdef _ST_FLAGS_PRESENT_
    hcc_leaf_int64(trans, LC_FILEFLAGS, st->st_flags);
#endif
    return(0);
}

/*
 * OPENDIR
 */
DIR *
hc_opendir(struct HostConf *hc, const char *path)
{
    hctransaction_t trans;
    struct HCHead *head;
    struct HCLeaf *item;
    struct dirent *den;
    intptr_t desc = 0;

    if (hc == NULL || hc->host == NULL)
	return(opendir(path));

    trans = hcc_start_command(hc, HC_OPENDIR);
    hcc_leaf_string(trans, LC_PATH1, path);
    if ((head = hcc_finish_command(trans)) == NULL)
	return(NULL);
    if (head->error)
	return(NULL);
    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_DESCRIPTOR:
	    desc = HCC_INT32(item);
	    break;
	}
    }
    if (hcc_get_descriptor(hc, desc, HC_DESC_DIR)) {
	    fprintf(stderr, "hc_opendir: remote reused active descriptor %jd\n",
		(intmax_t)desc);
	return(NULL);
    }
    den = malloc(sizeof(*den));
    bzero(den, sizeof(*den));
    hcc_set_descriptor(hc, desc, den, HC_DESC_DIR);
    return((void *)desc);
}

static int
rc_opendir(hctransaction_t trans, struct HCHead *head)
{
    struct HCLeaf *item;
    const char *path = NULL;
    DIR *dir;
    int desc;

    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_PATH1:
	    path = HCC_STRING(item);
	    break;
	}
    }
    if (path == NULL)
	return(-2);
    if ((dir = opendir(path)) == NULL) {
	head->error = errno;
    } else {
	desc = hcc_alloc_descriptor(trans->hc, dir, HC_DESC_DIR);
	hcc_leaf_int32(trans, LC_DESCRIPTOR, desc);
    }
    return(0);
}

/*
 * READDIR
 */
struct dirent *
hc_readdir(struct HostConf *hc, DIR *dir)
{
    hctransaction_t trans;
    struct HCHead *head;
    struct HCLeaf *item;
    struct dirent *den;

    if (hc == NULL || hc->host == NULL)
	return(readdir(dir));

    trans = hcc_start_command(hc, HC_READDIR);
    hcc_leaf_int32(trans, LC_DESCRIPTOR, (intptr_t)dir);
    if ((head = hcc_finish_command(trans)) == NULL)
	return(NULL);
    if (head->error)
	return(NULL);	/* XXX errno */
    den = hcc_get_descriptor(hc, (intptr_t)dir, HC_DESC_DIR);
    if (den == NULL)
	return(NULL);	/* XXX errno */
    if (den->d_name)
	den->d_name[0] = 0;
    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_PATH1:
	    snprintf(den->d_name, sizeof(den->d_name), "%s", HCC_STRING(item));
	    break;
	case LC_INO:
	    den->d_fileno = HCC_INT64(item);
	    break;
	case LC_TYPE:
	    den->d_type = HCC_INT32(item);
	    break;
	}
    }
    if (den->d_name[0]) {
#ifdef _DIRENT_HAVE_D_NAMLEN
	den->d_namlen = strlen(den->d_name);
#endif
	return(den);
    }
    return(NULL);	/* XXX errno */
}

static int
rc_readdir(hctransaction_t trans, struct HCHead *head)
{
    struct HCLeaf *item;
    struct dirent *den;
    DIR *dir = NULL;

    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_DESCRIPTOR:
	    dir = hcc_get_descriptor(trans->hc, HCC_INT32(item), HC_DESC_DIR);
	    break;
	}
    }
    if (dir == NULL)
	return(-2);
    if ((den = readdir(dir)) != NULL) {
	hcc_leaf_string(trans, LC_PATH1, den->d_name);
	hcc_leaf_int64(trans, LC_INO, den->d_fileno);
	hcc_leaf_int32(trans, LC_TYPE, den->d_type);
    }
    return(0);
}

/*
 * CLOSEDIR
 *
 * XXX cpdup needs to check error code to avoid truncated dirs?
 */
int
hc_closedir(struct HostConf *hc, DIR *dir)
{
    hctransaction_t trans;
    struct HCHead *head;
    struct dirent *den;

    if (hc == NULL || hc->host == NULL)
	return(closedir(dir));
    den = hcc_get_descriptor(hc, (intptr_t)dir, HC_DESC_DIR);
    if (den) {
	free(den);
	hcc_set_descriptor(hc, (intptr_t)dir, NULL, HC_DESC_DIR);

	trans = hcc_start_command(hc, HC_CLOSEDIR);
	hcc_leaf_int32(trans, LC_DESCRIPTOR, (intptr_t)dir);
	if ((head = hcc_finish_command(trans)) == NULL)
	    return(-1);
	if (head->error)
	    return(-1);		/* XXX errno */
	return(0);
    } else {
	/* errno */
	return(-1);
    }
}

static int
rc_closedir(hctransaction_t trans, struct HCHead *head)
{
    struct HCLeaf *item;
    DIR *dir = NULL;

    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_DESCRIPTOR:
	    dir = hcc_get_descriptor(trans->hc, HCC_INT32(item), HC_DESC_DIR);
	    if (dir != NULL)
		    hcc_set_descriptor(trans->hc, HCC_INT32(item), NULL, HC_DESC_DIR);
	    break;
	}
    }
    if (dir == NULL)
	return(-2);
    return(closedir(dir));
}

/*
 * OPEN
 */
int
hc_open(struct HostConf *hc, const char *path, int flags, mode_t mode)
{
    hctransaction_t trans;
    struct HCHead *head;
    struct HCLeaf *item;
    int *fdp;
    int desc = 0;
    int nflags;

    if (hc == NULL || hc->host == NULL) {
#ifdef O_LARGEFILE
	flags |= O_LARGEFILE;
#endif
	return(open(path, flags, mode));
    }

    nflags = flags & XO_NATIVEMASK;
    if (flags & O_CREAT)
	nflags |= XO_CREAT;
    if (flags & O_EXCL)
	nflags |= XO_EXCL;
    if (flags & O_TRUNC)
	nflags |= XO_TRUNC;

    trans = hcc_start_command(hc, HC_OPEN);
    hcc_leaf_string(trans, LC_PATH1, path);
    hcc_leaf_int32(trans, LC_OFLAGS, nflags);
    hcc_leaf_int32(trans, LC_MODE, mode);

    if ((head = hcc_finish_command(trans)) == NULL)
	return(-1);
    if (head->error)
	return(-1);
    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_DESCRIPTOR:
	    desc = HCC_INT32(item);
	    break;
	}
    }
    if (hcc_get_descriptor(hc, desc, HC_DESC_FD)) {
	fprintf(stderr, "hc_opendir: remote reused active descriptor %d\n",
		desc);
	return(-1);
    }
    fdp = malloc(sizeof(int));
    *fdp = desc;	/* really just a dummy */
    hcc_set_descriptor(hc, desc, fdp, HC_DESC_FD);
    return(desc);
}

static int
rc_open(hctransaction_t trans, struct HCHead *head)
{
    struct HCLeaf *item;
    const char *path = NULL;
    int nflags = 0;
    int flags;
    mode_t mode = 0666;
    int desc;
    int *fdp;
    int fd;

    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_PATH1:
	    path = HCC_STRING(item);
	    break;
	case LC_OFLAGS:
	    nflags = HCC_INT32(item);
	    break;
	case LC_MODE:
	    mode = HCC_INT32(item);
	    break;
	}
    }
    if (path == NULL)
	return(-2);

    flags = nflags & XO_NATIVEMASK;
    if (nflags & XO_CREAT)
	flags |= O_CREAT;
    if (nflags & XO_EXCL)
	flags |= O_EXCL;
    if (nflags & XO_TRUNC)
	flags |= O_TRUNC;

#ifdef O_LARGEFILE
    flags |= O_LARGEFILE;
#endif
    if ((fd = open(path, flags, mode)) < 0) {
	head->error = errno;
	return(0);
    }
    fdp = malloc(sizeof(int));
    *fdp = fd;
    desc = hcc_alloc_descriptor(trans->hc, fdp, HC_DESC_FD);
    hcc_leaf_int32(trans, LC_DESCRIPTOR, desc);
    return(0);
}

/*
 * CLOSE
 */
int
hc_close(struct HostConf *hc, int fd)
{
    hctransaction_t trans;
    struct HCHead *head;
    int *fdp;

    if (hc == NULL || hc->host == NULL)
	return(close(fd));

    fdp = hcc_get_descriptor(hc, fd, HC_DESC_FD);
    if (fdp) {
	free(fdp);
	hcc_set_descriptor(hc, fd, NULL, HC_DESC_FD);

	trans = hcc_start_command(hc, HC_CLOSE);
	hcc_leaf_int32(trans, LC_DESCRIPTOR, fd);
	if ((head = hcc_finish_command(trans)) == NULL)
	    return(-1);
	if (head->error)
	    return(-1);
	return(0);
    } else {
	return(-1);
    }
}

static int
rc_close(hctransaction_t trans, struct HCHead *head)
{
    struct HCLeaf *item;
    int *fdp = NULL;
    int fd;
    int desc = -1;

    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_DESCRIPTOR:
	    desc = HCC_INT32(item);
	    break;
	}
    }
    if (desc < 0)
	return(-2);
    if ((fdp = hcc_get_descriptor(trans->hc, desc, HC_DESC_FD)) == NULL)
	return(-2);
    fd = *fdp;
    free(fdp);
    hcc_set_descriptor(trans->hc, desc, NULL, HC_DESC_FD);
    return(close(fd));
}

static int
getiolimit(void)
{
#if USE_PTHREADS
    if (CurParallel < 2)
	return(32768);
    if (CurParallel < 4)
	return(16384);
    if (CurParallel < 8)
	return(8192);
    return(4096);
#else
    return(32768);
#endif
}

/*
 * READ
 */
ssize_t
hc_read(struct HostConf *hc, int fd, void *buf, size_t bytes)
{
    hctransaction_t trans;
    struct HCHead *head;
    struct HCLeaf *item;
    int *fdp;
    int r;

    if (hc == NULL || hc->host == NULL)
	return(read(fd, buf, bytes));

    fdp = hcc_get_descriptor(hc, fd, HC_DESC_FD);
    if (fdp) {
	r = 0;
	while (bytes) {
	    size_t limit = getiolimit();
	    int n = (bytes > limit) ? limit : bytes;
	    int x = 0;

	    trans = hcc_start_command(hc, HC_READ);
	    hcc_leaf_int32(trans, LC_DESCRIPTOR, fd);
	    hcc_leaf_int32(trans, LC_BYTES, n);
	    if ((head = hcc_finish_command(trans)) == NULL)
		return(-1);
	    if (head->error)
		return(-1);
	    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
		switch(item->leafid) {
		case LC_DATA:
		    x = item->bytes - sizeof(*item);
		    if (x > (int)bytes)
			x = (int)bytes;
		    bcopy(HCC_BINARYDATA(item), buf, x);
		    buf = (char *)buf + x;
		    bytes -= (size_t)x;
		    r += x;
		    break;
		}
	    }
	    if (x < n)
		break;
	}
	return(r);
    } else {
	return(-1);
    }
}

static int
rc_read(hctransaction_t trans, struct HCHead *head)
{
    struct HCLeaf *item;
    int *fdp = NULL;
    char buf[32768];
    int bytes = -1;
    int n;

    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_DESCRIPTOR:
	    fdp = hcc_get_descriptor(trans->hc, HCC_INT32(item), HC_DESC_FD);
	    break;
	case LC_BYTES:
	    bytes = HCC_INT32(item);
	    break;
	}
    }
    if (fdp == NULL)
	return(-2);
    if (bytes < 0 || bytes > 32768)
	return(-2);
    n = read(*fdp, buf, bytes);
    if (n < 0) {
	head->error = errno;
	return(0);
    }
    hcc_leaf_data(trans, LC_DATA, buf, n);
    return(0);
}

/*
 * WRITE
 */
ssize_t
hc_write(struct HostConf *hc, int fd, const void *buf, size_t bytes)
{
    hctransaction_t trans;
    struct HCHead *head;
    struct HCLeaf *item;
    int *fdp;
    int r;

    if (hc == NULL || hc->host == NULL)
	return(write(fd, buf, bytes));

    fdp = hcc_get_descriptor(hc, fd, HC_DESC_FD);
    if (fdp) {
	r = 0;
	while (bytes) {
	    size_t limit = getiolimit();
	    int n = (bytes > limit) ? limit : bytes;
	    int x = 0;

	    trans = hcc_start_command(hc, HC_WRITE);
	    hcc_leaf_int32(trans, LC_DESCRIPTOR, fd);
	    hcc_leaf_data(trans, LC_DATA, buf, n);
	    if ((head = hcc_finish_command(trans)) == NULL)
		return(-1);
	    if (head->error)
		return(-1);
	    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
		switch(item->leafid) {
		case LC_BYTES:
		    x = HCC_INT32(item);
		    break;
		}
	    }
	    if (x < 0 || x > n)
		return(-1);
	    r += x;
	    buf = (const char *)buf + x;
	    bytes -= x;
	    if (x < n)
		break;
	}
	return(r);
    } else {
	return(-1);
    }
}

static int
rc_write(hctransaction_t trans, struct HCHead *head)
{
    struct HCLeaf *item;
    int *fdp = NULL;
    void *buf = NULL;
    int n = -1;

    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_DESCRIPTOR:
	    fdp = hcc_get_descriptor(trans->hc, HCC_INT32(item), HC_DESC_FD);
	    break;
	case LC_DATA:
	    buf = HCC_BINARYDATA(item);
	    n = item->bytes - sizeof(*item);
	    break;
	}
    }
    if (fdp == NULL)
	return(-2);
    if (n < 0 || n > 32768)
	return(-2);
    n = write(*fdp, buf, n);
    if (n < 0) {
	head->error = errno;
    } else {
	hcc_leaf_int32(trans, LC_BYTES, n);
    }
    return(0);
}

/*
 * REMOVE
 *
 * NOTE: This function returns -errno if an error occured.
 */
int
hc_remove(struct HostConf *hc, const char *path)
{
    hctransaction_t trans;
    struct HCHead *head;
    int res;

    if (hc == NULL || hc->host == NULL) {
	res = remove(path);
	if (res < 0)
		res = -errno;
	return(res);
    }

    trans = hcc_start_command(hc, HC_REMOVE);
    hcc_leaf_string(trans, LC_PATH1, path);
    if ((head = hcc_finish_command(trans)) == NULL)
	return(-EIO);
    if (head->error)
	return(-(int)head->error);
    return(0);
}

static int
rc_remove(hctransaction_t trans __unused, struct HCHead *head)
{
    struct HCLeaf *item;
    const char *path = NULL;

    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_PATH1:
	    path = HCC_STRING(item);
	    break;
	}
    }
    if (path == NULL)
	return(-2);
    return(remove(path));
}

/*
 * MKDIR
 */
int
hc_mkdir(struct HostConf *hc __unused, const char *path, mode_t mode)
{
    hctransaction_t trans;
    struct HCHead *head;

    if (hc == NULL || hc->host == NULL)
	return(mkdir(path, mode));

    trans = hcc_start_command(hc, HC_MKDIR);
    hcc_leaf_string(trans, LC_PATH1, path);
    hcc_leaf_int32(trans, LC_MODE, mode);
    if ((head = hcc_finish_command(trans)) == NULL)
	return(-1);
    if (head->error)
	return(-1);
    return(0);
}

static int
rc_mkdir(hctransaction_t trans __unused, struct HCHead *head)
{
    struct HCLeaf *item;
    const char *path = NULL;
    mode_t mode = 0777;

    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_PATH1:
	    path = HCC_STRING(item);
	    break;
	case LC_MODE:
	    mode = HCC_INT32(item);
	    break;
	}
    }
    if (path == NULL)
	return(-1);
    return(mkdir(path, mode));
}

/*
 * RMDIR
 */
int
hc_rmdir(struct HostConf *hc, const char *path)
{
    hctransaction_t trans;
    struct HCHead *head;

    if (hc == NULL || hc->host == NULL)
	return(rmdir(path));

    trans = hcc_start_command(hc, HC_RMDIR);
    hcc_leaf_string(trans, LC_PATH1, path);
    if ((head = hcc_finish_command(trans)) == NULL)
	return(-1);
    if (head->error)
	return(-1);
    return(0);
}

static int
rc_rmdir(hctransaction_t trans __unused, struct HCHead *head)
{
    struct HCLeaf *item;
    const char *path = NULL;

    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_PATH1:
	    path = HCC_STRING(item);
	    break;
	}
    }
    if (path == NULL)
	return(-1);
    return(rmdir(path));
}

/*
 * CHOWN
 */
int
hc_chown(struct HostConf *hc, const char *path, uid_t owner, gid_t group)
{
    hctransaction_t trans;
    struct HCHead *head;

    if (hc == NULL || hc->host == NULL)
	return(chown(path, owner, group));

    trans = hcc_start_command(hc, HC_CHOWN);
    hcc_leaf_string(trans, LC_PATH1, path);
    hcc_leaf_int32(trans, LC_UID, owner);
    hcc_leaf_int32(trans, LC_GID, group);
    if ((head = hcc_finish_command(trans)) == NULL)
	return(-1);
    if (head->error)
	return(-1);
    return(0);
}

static int
rc_chown(hctransaction_t trans __unused, struct HCHead *head)
{
    struct HCLeaf *item;
    const char *path = NULL;
    uid_t uid = (uid_t)-1;
    gid_t gid = (gid_t)-1;

    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_PATH1:
	    path = HCC_STRING(item);
	    break;
	case LC_UID:
	    uid = HCC_INT32(item);
	    break;
	case LC_GID:
	    gid = HCC_INT32(item);
	    break;
	}
    }
    if (path == NULL)
	return(-1);
    return(chown(path, uid, gid));
}

/*
 * LCHOWN
 */
int
hc_lchown(struct HostConf *hc, const char *path, uid_t owner, gid_t group)
{
    hctransaction_t trans;
    struct HCHead *head;

    if (hc == NULL || hc->host == NULL)
	return(lchown(path, owner, group));

    trans = hcc_start_command(hc, HC_LCHOWN);
    hcc_leaf_string(trans, LC_PATH1, path);
    hcc_leaf_int32(trans, LC_UID, owner);
    hcc_leaf_int32(trans, LC_GID, group);
    if ((head = hcc_finish_command(trans)) == NULL)
	return(-1);
    if (head->error)
	return(-1);
    return(0);
}

static int
rc_lchown(hctransaction_t trans __unused, struct HCHead *head)
{
    struct HCLeaf *item;
    const char *path = NULL;
    uid_t uid = (uid_t)-1;
    gid_t gid = (gid_t)-1;

    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_PATH1:
	    path = HCC_STRING(item);
	    break;
	case LC_UID:
	    uid = HCC_INT32(item);
	    break;
	case LC_GID:
	    gid = HCC_INT32(item);
	    break;
	}
    }
    if (path == NULL)
	return(-1);
    return(lchown(path, uid, gid));
}

/*
 * CHMOD
 */
int
hc_chmod(struct HostConf *hc, const char *path, mode_t mode)
{
    hctransaction_t trans;
    struct HCHead *head;

    if (hc == NULL || hc->host == NULL)
	return(chmod(path, mode));

    trans = hcc_start_command(hc, HC_CHMOD);
    hcc_leaf_string(trans, LC_PATH1, path);
    hcc_leaf_int32(trans, LC_MODE, mode);
    if ((head = hcc_finish_command(trans)) == NULL)
	return(-1);
    if (head->error)
	return(-1);
    return(0);
}

static int
rc_chmod(hctransaction_t trans __unused, struct HCHead *head)
{
    struct HCLeaf *item;
    const char *path = NULL;
    mode_t mode = 0666;

    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_PATH1:
	    path = HCC_STRING(item);
	    break;
	case LC_MODE:
	    mode = HCC_INT32(item);
	    break;
	}
    }
    if (path == NULL)
	return(-1);
    return(chmod(path, mode));
}

/*
 * MKNOD
 */
int
hc_mknod(struct HostConf *hc, const char *path, mode_t mode, dev_t rdev)
{
    hctransaction_t trans;
    struct HCHead *head;

    if (hc == NULL || hc->host == NULL)
	return(mknod(path, mode, rdev));

    trans = hcc_start_command(hc, HC_MKNOD);
    hcc_leaf_string(trans, LC_PATH1, path);
    hcc_leaf_int32(trans, LC_MODE, mode);
    hcc_leaf_int32(trans, LC_RDEV, rdev);
    if ((head = hcc_finish_command(trans)) == NULL)
	return(-1);
    if (head->error)
	return(-1);
    return(0);
}

static int
rc_mknod(hctransaction_t trans __unused, struct HCHead *head)
{
    struct HCLeaf *item;
    const char *path = NULL;
    mode_t mode = 0666;
    dev_t rdev = 0;

    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_PATH1:
	    path = HCC_STRING(item);
	    break;
	case LC_MODE:
	    mode = HCC_INT32(item);
	    break;
	case LC_RDEV:
	    rdev = HCC_INT32(item);
	    break;
	}
    }
    if (path == NULL)
	return(-1);
    return(mknod(path, mode, rdev));
}

/*
 * LINK
 */
int
hc_link(struct HostConf *hc, const char *name1, const char *name2)
{
    hctransaction_t trans;
    struct HCHead *head;

    if (hc == NULL || hc->host == NULL)
	return(link(name1, name2));

    trans = hcc_start_command(hc, HC_LINK);
    hcc_leaf_string(trans, LC_PATH1, name1);
    hcc_leaf_string(trans, LC_PATH2, name2);
    if ((head = hcc_finish_command(trans)) == NULL)
	return(-1);
    if (head->error)
	return(-1);
    return(0);
}

static int
rc_link(hctransaction_t trans __unused, struct HCHead *head)
{
    struct HCLeaf *item;
    const char *name1 = NULL;
    const char *name2 = NULL;

    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_PATH1:
	    name1 = HCC_STRING(item);
	    break;
	case LC_PATH2:
	    name2 = HCC_STRING(item);
	    break;
	}
    }
    if (name1 == NULL || name2 == NULL)
	return(-2);
    return(link(name1, name2));
}

#ifdef _ST_FLAGS_PRESENT_
/*
 * CHFLAGS
 */
int
hc_chflags(struct HostConf *hc, const char *path, u_long flags)
{
    hctransaction_t trans;
    struct HCHead *head;

    if (hc == NULL || hc->host == NULL)
	return(chflags(path, flags));

    trans = hcc_start_command(hc, HC_CHFLAGS);
    hcc_leaf_string(trans, LC_PATH1, path);
    hcc_leaf_int64(trans, LC_FILEFLAGS, flags);
    if ((head = hcc_finish_command(trans)) == NULL)
	return(-1);
    if (head->error)
	return(-1);
    return(0);
}

static int
rc_chflags(hctransaction_t trans __unused, struct HCHead *head)
{
    struct HCLeaf *item;
    const char *path = NULL;
    u_long flags = 0;

    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_PATH1:
	    path = HCC_STRING(item);
	    break;
	case LC_FILEFLAGS:
	    flags = (u_long)HCC_INT64(item);
	    break;
	}
    }
    if (path == NULL)
	return(-2);
    return(chflags(path, flags));
}

#endif

/*
 * READLINK
 */
int
hc_readlink(struct HostConf *hc, const char *path, char *buf, int bufsiz)
{
    hctransaction_t trans;
    struct HCHead *head;
    struct HCLeaf *item;
    int r;

    if (hc == NULL || hc->host == NULL)
	return(readlink(path, buf, bufsiz));

    trans = hcc_start_command(hc, HC_READLINK);
    hcc_leaf_string(trans, LC_PATH1, path);
    if ((head = hcc_finish_command(trans)) == NULL)
	return(-1);
    if (head->error)
	return(-1);

    r = 0;
    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_DATA:
	    r = item->bytes - sizeof(*item);
	    if (r < 0)
		r = 0;
	    if (r > bufsiz)
		r = bufsiz;
	    bcopy(HCC_BINARYDATA(item), buf, r);
	    break;
	}
    }
    return(r);
}

static int
rc_readlink(hctransaction_t trans, struct HCHead *head)
{
    struct HCLeaf *item;
    const char *path = NULL;
    char buf[1024];
    int r;

    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_PATH1:
	    path = HCC_STRING(item);
	    break;
	}
    }
    if (path == NULL)
	return(-2);
    r = readlink(path, buf, sizeof(buf));
    if (r < 0)
	return(-1);
    hcc_leaf_data(trans, LC_DATA, buf, r);
    return(0);
}

/*
 * UMASK
 */
mode_t
hc_umask(struct HostConf *hc, mode_t numask)
{
    hctransaction_t trans;
    struct HCHead *head;
    struct HCLeaf *item;

    if (hc == NULL || hc->host == NULL)
	return(umask(numask));

    trans = hcc_start_command(hc, HC_UMASK);
    hcc_leaf_int32(trans, LC_MODE, numask);
    if ((head = hcc_finish_command(trans)) == NULL)
	return((mode_t)-1);
    if (head->error)
	return((mode_t)-1);

    numask = ~0666;
    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_MODE:
	    numask = HCC_INT32(item);
	    break;
	}
    }
    return(numask);
}

static int
rc_umask(hctransaction_t trans, struct HCHead *head)
{
    struct HCLeaf *item;
    mode_t numask = ~0666;

    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_MODE:
	    numask = HCC_INT32(item);
	    break;
	}
    }
    numask = umask(numask);
    hcc_leaf_int32(trans, LC_MODE, numask);
    return(0);
}

/*
 * SYMLINK
 */
int
hc_symlink(struct HostConf *hc, const char *name1, const char *name2)
{
    hctransaction_t trans;
    struct HCHead *head;

    if (hc == NULL || hc->host == NULL)
	return(symlink(name1, name2));

    trans = hcc_start_command(hc, HC_SYMLINK);
    hcc_leaf_string(trans, LC_PATH1, name1);
    hcc_leaf_string(trans, LC_PATH2, name2);
    if ((head = hcc_finish_command(trans)) == NULL)
	return(-1);
    if (head->error)
	return(-1);
    return(0);
}

static int
rc_symlink(hctransaction_t trans __unused, struct HCHead *head)
{
    struct HCLeaf *item;
    const char *name1 = NULL;
    const char *name2 = NULL;

    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_PATH1:
	    name1 = HCC_STRING(item);
	    break;
	case LC_PATH2:
	    name2 = HCC_STRING(item);
	    break;
	}
    }
    if (name1 == NULL || name2 == NULL)
	return(-2);
    return(symlink(name1, name2));
}

/*
 * RENAME
 */
int
hc_rename(struct HostConf *hc, const char *name1, const char *name2)
{
    hctransaction_t trans;
    struct HCHead *head;
  
    if (hc == NULL || hc->host == NULL)
	return(rename(name1, name2));

    trans = hcc_start_command(hc, HC_RENAME);
    hcc_leaf_string(trans, LC_PATH1, name1);
    hcc_leaf_string(trans, LC_PATH2, name2);
    if ((head = hcc_finish_command(trans)) == NULL)
	return(-1);
    if (head->error)
	return(-1);
    return(0);
}

static int
rc_rename(hctransaction_t trans __unused, struct HCHead *head)
{
    struct HCLeaf *item;
    const char *name1 = NULL;
    const char *name2 = NULL;

    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_PATH1:
	    name1 = HCC_STRING(item);
	    break;
	case LC_PATH2:
	    name2 = HCC_STRING(item);
	    break;
	}
    }
    if (name1 == NULL || name2 == NULL)
	return(-2);
    return(rename(name1, name2));
}

/*
 * UTIMES
 */
int
hc_utimes(struct HostConf *hc, const char *path, const struct timeval *times)
{
    hctransaction_t trans;
    struct HCHead *head;

    if (hc == NULL || hc->host == NULL)
	return(utimes(path, times));

    trans = hcc_start_command(hc, HC_UTIMES);
    hcc_leaf_string(trans, LC_PATH1, path);
    hcc_leaf_int64(trans, LC_ATIME, times[0].tv_sec);
    hcc_leaf_int64(trans, LC_MTIME, times[1].tv_sec);
    if ((head = hcc_finish_command(trans)) == NULL)
	return(-1);
    if (head->error)
	return(-1);
    return(0);
}

static int
rc_utimes(hctransaction_t trans __unused, struct HCHead *head)
{
    struct HCLeaf *item;
    struct timeval times[2];
    const char *path;

    bzero(times, sizeof(times));
    path = NULL;

    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	switch(item->leafid) {
	case LC_PATH1:
	    path = HCC_STRING(item);
	    break;
	case LC_ATIME:
	    times[0].tv_sec = HCC_INT64(item);
	    break;
	case LC_MTIME:
	    times[1].tv_sec = HCC_INT64(item);
	    break;
	}
    }
    if (path == NULL)
	return(-2);
    return(utimes(path, times));
}

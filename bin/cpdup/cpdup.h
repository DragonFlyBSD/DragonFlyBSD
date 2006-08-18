/*
 * CPDUP.H
 *
 * $DragonFly: src/bin/cpdup/cpdup.h,v 1.5 2006/08/18 01:13:51 dillon Exp $
 */

#include <sys/param.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/file.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <utime.h>
#include <dirent.h>
#include <signal.h>
#include <pwd.h>
#include <assert.h>
#ifndef NOMD5
#include <md5.h>
#endif

void logstd(const char *ctl, ...);
void logerr(const char *ctl, ...);
char *mprintf(const char *ctl, ...);
void fatal(const char *ctl, ...);
char *fextract(FILE *fi, int n, int *pc, int skip);

int fsmid_check(int64_t fsmid, const char *dpath);
void fsmid_flush(void);
#ifndef NOMD5
int md5_check(const char *spath, const char *dpath);
void md5_flush(void);
#endif

extern const char *MD5CacheFile;
extern const char *FSMIDCacheFile;

extern int SummaryOpt;

extern int64_t CountSourceBytes;
extern int64_t CountSourceItems;
extern int64_t CountCopiedItems;
extern int64_t CountReadBytes;
extern int64_t CountWriteBytes;
extern int64_t CountRemovedItems;


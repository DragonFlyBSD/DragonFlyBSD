/*
 * CPDUP.H
 *
 * $DragonFly: src/bin/cpdup/cpdup.h,v 1.1 2003/12/01 02:20:14 dillon Exp $
 */

#include <sys/param.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <utime.h>
#include <dirent.h>
#include <signal.h>
#include <pwd.h>
#include <assert.h>
#include <md5.h>

extern void logstd(const char *ctl, ...);
extern void logerr(const char *ctl, ...);
extern char *mprintf(const char *ctl, ...);
extern void fatal(const char *ctl, ...);


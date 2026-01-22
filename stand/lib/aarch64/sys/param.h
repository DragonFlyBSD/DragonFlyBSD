#ifndef _SYS_PARAM_H_
#define _SYS_PARAM_H_

#include <sys/stdint.h>
#include <sys/types.h>

#define DEV_BSIZE 512
#define DEV_BSHIFT 9
#define PAGE_SIZE 4096
#define PAGE_SHIFT 12
#define PATH_MAX 1024
#define MAXPATHLEN PATH_MAX

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define howmany(x, y) (((x) + ((y) - 1)) / (y))
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#define rounddown(x, y) (((x) / (y)) * (y))

#define btodb(bytes) ((daddr_t)((bytes) >> DEV_BSHIFT))
#define dbtob(db) ((off_t)(db) << DEV_BSHIFT)

#endif

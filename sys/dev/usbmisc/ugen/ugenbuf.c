/*
 * $DragonFly: src/sys/dev/usbmisc/ugen/ugenbuf.c,v 1.1 2004/07/08 03:53:54 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include "ugenbuf.h"

static MALLOC_DEFINE(M_UGENBUF, "ugenbufs", "Temporary buffer space");
static void *ugencache_buf;
static int ugencache_size;

/*
 * getugenbuf()
 *
 *	Allocate a temporary buffer for UGEN.  This routine is called from
 *	mainline code only and the BGL is held.
 */
void *
getugenbuf(int reqsize, int *bsize)
{
    void *buf;

    if (reqsize < 256)
	reqsize = 256;
    if (reqsize > 262144)
	reqsize = 262144;
    *bsize = reqsize;

    buf = ugencache_buf;
    if (buf == NULL) {
	buf = malloc(reqsize, M_UGENBUF, M_WAITOK);
    } else if (ugencache_size != reqsize) {
	ugencache_buf = NULL;
	free(buf, M_UGENBUF);
	buf = malloc(reqsize, M_UGENBUF, M_WAITOK);
    } else {
	buf = ugencache_buf;
	ugencache_buf = NULL;
    }
    return(buf);
}

/*
 * relugenbuf()
 *
 *	Release a temporary buffer for UGEN.  This routine is called from
 *	mainline code only and the BGL is held.
 */
void
relugenbuf(void *buf, int bsize)
{
    if (ugencache_buf == NULL) {
	ugencache_buf = buf;
	ugencache_size = bsize;
    } else {
	free(buf, M_UGENBUF);
    }
}


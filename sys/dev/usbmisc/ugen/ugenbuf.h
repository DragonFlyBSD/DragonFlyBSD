/*
 * $DragonFly: src/sys/dev/usbmisc/ugen/ugenbuf.h,v 1.1 2004/07/08 03:53:54 dillon Exp $
 *
 */

extern void *getugenbuf(int reqsize, int *bsize);
extern void relugenbuf(void *buf, int bsize);

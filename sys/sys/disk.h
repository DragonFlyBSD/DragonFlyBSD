/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD: src/sys/sys/disk.h,v 1.16.2.3 2001/06/20 16:11:01 scottl Exp $
 * $DragonFly: src/sys/sys/disk.h,v 1.3 2003/07/22 17:03:34 dillon Exp $
 *
 */

#ifndef _SYS_DISK_H_
#define	_SYS_DISK_H_

#ifndef _SYS_DISKSLICE_H_
#include <sys/diskslice.h>
#endif

#ifndef _SYS_DISKLABEL
#include <sys/disklabel.h>
#endif

#ifndef _SYS_DISKLABEL
#include <sys/msgport.h>
#endif

struct disk {
	struct lwkt_port	d_port;		/* interception port */
	struct lwkt_port	*d_fwdport;	/* forward to real port */
	u_int			d_flags;
	u_int			d_dsflags;
	dev_t			d_dev;
	struct diskslices	*d_slice;
	struct disklabel	d_label;
	LIST_ENTRY(disk)	d_list;
};

#define DISKFLAG_LOCK		0x1
#define DISKFLAG_WANTED		0x2

dev_t disk_create __P((int unit, struct disk *disk, int flags, struct cdevsw *cdevsw));
void disk_destroy __P((struct disk *disk));
int disk_dumpcheck __P((dev_t dev, u_int *count, u_int *blkno, u_int *secsize));
struct disk *disk_enumerate __P((struct disk *disk));
void disk_invalidate __P((struct disk *disk));

#endif /* _SYS_DISK_H_ */

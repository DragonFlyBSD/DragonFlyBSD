/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD: src/sys/kern/subr_disk.c,v 1.20.2.6 2001/10/05 07:14:57 peter Exp $
 * $DragonFly: src/sys/kern/subr_disk.c,v 1.6 2003/11/20 06:05:30 dillon Exp $
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <machine/md_var.h>
#include <sys/ctype.h>
#include <sys/msgport.h>
#include <sys/msgport2.h>

static MALLOC_DEFINE(M_DISK, "disk", "disk data");

static d_strategy_t diskstrategy;
static d_open_t diskopen;
static d_close_t diskclose; 
static d_ioctl_t diskioctl;
static d_psize_t diskpsize;
static int disk_putport(lwkt_port_t port, lwkt_msg_t msg);

static LIST_HEAD(, disk) disklist = LIST_HEAD_INITIALIZER(&disklist);

static void
inherit_raw(dev_t pdev, dev_t dev)
{
	dev->si_disk = pdev->si_disk;
	dev->si_drv1 = pdev->si_drv1;
	dev->si_drv2 = pdev->si_drv2;
	dev->si_iosize_max = pdev->si_iosize_max;
	dev->si_bsize_phys = pdev->si_bsize_phys;
	dev->si_bsize_best = pdev->si_bsize_best;
}

/*
 * Create a slice and unit managed disk.  The underlying raw disk device
 * is specified by cdevsw.  We create the device as a managed device by
 * first creating it normally then overriding the message port with our
 * own frontend (which will be responsible for assigning pblkno).
 */
dev_t
disk_create(int unit, struct disk *dp, int flags, struct cdevsw *cdevsw)
{
	dev_t dev;

	bzero(dp, sizeof(*dp));
	lwkt_init_port(&dp->d_port, NULL);	/* intercept port */
	dp->d_port.mp_putport = disk_putport;

	dev = makedev(cdevsw->d_maj, 0);	/* base device */
	dev->si_disk = dp;
						/* forwarding port */
	dp->d_fwdport = cdevsw_add_override(cdevsw, &dp->d_port);

	if (bootverbose)
		printf("Creating DISK %s%d\n", cdevsw->d_name, unit);

  	/*
	 * The whole disk placemarker holds the disk structure.
	 */
	dev = make_dev(cdevsw, dkmakeminor(unit, WHOLE_DISK_SLICE, RAW_PART),
	    UID_ROOT, GID_OPERATOR, 0640, "%s%d", cdevsw->d_name, unit);
	dev->si_disk = dp;
	dp->d_dev = dev;
	dp->d_dsflags = flags;
	LIST_INSERT_HEAD(&disklist, dp, d_list);
	return (dev);
}

void
disk_destroy(struct disk *disk)
{
	dev_t dev = disk->d_dev;

	LIST_REMOVE(disk, d_list);
	bzero(disk, sizeof(*disk));
	dev->si_disk = NULL;
	destroy_dev(dev);
	/* YYY remove cdevsw entries? */
	return;
}

int
disk_dumpcheck(dev_t dev, u_int *count, u_int *blkno, u_int *secsize)
{
	struct disk *dp;
	struct disklabel *dl;
	u_int boff;

	dp = dev->si_disk;
	if (!dp)
		return (ENXIO);
	if (!dp->d_slice)
		return (ENXIO);
	dl = dsgetlabel(dev, dp->d_slice);
	if (!dl)
		return (ENXIO);
	*count = Maxmem * (PAGE_SIZE / dl->d_secsize);
	if (dumplo <= LABELSECTOR || 
	    (dumplo + *count > dl->d_partitions[dkpart(dev)].p_size))
		return (EINVAL);
	boff = dl->d_partitions[dkpart(dev)].p_offset +
	    dp->d_slice->dss_slices[dkslice(dev)].ds_offset;
	*blkno = boff + dumplo;
	*secsize = dl->d_secsize;
	return (0);
	
}

void 
disk_invalidate (struct disk *disk)
{
	if (disk->d_slice)
		dsgone(&disk->d_slice);
}

struct disk *
disk_enumerate(struct disk *disk)
{
	if (!disk)
		return (LIST_FIRST(&disklist));
	else
		return (LIST_NEXT(disk, d_list));
}

static int
sysctl_disks(SYSCTL_HANDLER_ARGS)
{
	struct disk *disk;
	int error, first;

	disk = NULL;
	first = 1;

	while ((disk = disk_enumerate(disk))) {
		if (!first) {
			error = SYSCTL_OUT(req, " ", 1);
			if (error)
				return error;
		} else {
			first = 0;
		}
		error = SYSCTL_OUT(req, disk->d_dev->si_name, strlen(disk->d_dev->si_name));
		if (error)
			return error;
	}
	error = SYSCTL_OUT(req, "", 1);
	return error;
}
 
SYSCTL_PROC(_kern, OID_AUTO, disks, CTLTYPE_STRING | CTLFLAG_RD, 0, NULL, 
    sysctl_disks, "A", "names of available disks");

/*
 * The port intercept functions
 */
static
int
disk_putport(lwkt_port_t port, lwkt_msg_t lmsg)
{
	struct disk *disk = (struct disk *)port;
	cdevallmsg_t msg = (cdevallmsg_t)lmsg;
	int error;

	switch(msg->am_lmsg.ms_cmd) {
	case CDEV_CMD_OPEN:
		error = diskopen(
			    msg->am_open.msg.dev,
			    msg->am_open.oflags,
			    msg->am_open.devtype,
			    msg->am_open.td);
		break;
	case CDEV_CMD_CLOSE:
		error = diskclose(
			    msg->am_close.msg.dev,
			    msg->am_close.fflag,
			    msg->am_close.devtype,
			    msg->am_close.td);
		break;
	case CDEV_CMD_IOCTL:
		error = diskioctl(
			    msg->am_ioctl.msg.dev,
			    msg->am_ioctl.cmd,
			    msg->am_ioctl.data,
			    msg->am_ioctl.fflag,
			    msg->am_ioctl.td);
		break;
	case CDEV_CMD_STRATEGY:
		diskstrategy(msg->am_strategy.bp);
		error = 0;
		break;
	case CDEV_CMD_PSIZE:
		msg->am_psize.result = diskpsize(msg->am_psize.msg.dev);
		error = 0;      /* XXX */
		break;
	default:
		error = lwkt_forwardmsg(disk->d_fwdport, &msg->am_lmsg);
		break;
	}
	return(error);
}

static int
diskopen(dev_t dev, int oflags, int devtype, struct thread *td)
{
	dev_t pdev;
	struct disk *dp;
	int error;

	error = 0;
	pdev = dkmodpart(dkmodslice(dev, WHOLE_DISK_SLICE), RAW_PART);

	dp = pdev->si_disk;
	if (dp == NULL)
		return (ENXIO);

	while (dp->d_flags & DISKFLAG_LOCK) {
		dp->d_flags |= DISKFLAG_WANTED;
		error = tsleep(dp, PCATCH, "diskopen", hz);
		if (error)
			return (error);
	}
	dp->d_flags |= DISKFLAG_LOCK;

	if (!dsisopen(dp->d_slice)) {
		if (!pdev->si_iosize_max)
			pdev->si_iosize_max = dev->si_iosize_max;
		error = dev_port_dopen(dp->d_fwdport, pdev, oflags, devtype, td);
	}

	/* Inherit properties from the whole/raw dev_t */
	inherit_raw(pdev, dev);

	if (error)
		goto out;
	
	error = dsopen(dev, devtype, dp->d_dsflags, &dp->d_slice, &dp->d_label);

	if (!dsisopen(dp->d_slice)) 
		dev_port_dclose(dp->d_fwdport, pdev, oflags, devtype, td);
out:	
	dp->d_flags &= ~DISKFLAG_LOCK;
	if (dp->d_flags & DISKFLAG_WANTED) {
		dp->d_flags &= ~DISKFLAG_WANTED;
		wakeup(dp);
	}
	
	return(error);
}

static int
diskclose(dev_t dev, int fflag, int devtype, struct thread *td)
{
	struct disk *dp;
	int error;
	dev_t pdev;

	error = 0;
	pdev = dkmodpart(dkmodslice(dev, WHOLE_DISK_SLICE), RAW_PART);
	dp = pdev->si_disk;
	if (!dp)
		return (ENXIO);
	dsclose(dev, devtype, dp->d_slice);
	if (!dsisopen(dp->d_slice))
		error = dev_port_dclose(dp->d_fwdport, pdev, fflag, devtype, td);
	return (error);
}

static void
diskstrategy(struct buf *bp)
{
	dev_t pdev;
	struct disk *dp;

	pdev = dkmodpart(dkmodslice(bp->b_dev, WHOLE_DISK_SLICE), RAW_PART);
	dp = pdev->si_disk;
	if (dp != bp->b_dev->si_disk)
		inherit_raw(pdev, bp->b_dev);

	if (!dp) {
		bp->b_error = ENXIO;
		bp->b_flags |= B_ERROR;
		biodone(bp);
		return;
	}

	if (dscheck(bp, dp->d_slice) <= 0) {
		biodone(bp);
		return;
	}
	dev_port_dstrategy(dp->d_fwdport, dp->d_dev, bp);
}

/*
 * note: when forwarding the ioctl we use the original device rather then
 * the whole disk slice.
 */
static int
diskioctl(dev_t dev, u_long cmd, caddr_t data, int fflag, struct thread *td)
{
	struct disk *dp;
	int error;
	dev_t pdev;

	pdev = dkmodpart(dkmodslice(dev, WHOLE_DISK_SLICE), RAW_PART);
	dp = pdev->si_disk;
	if (!dp)
		return (ENXIO);
	error = dsioctl(dev, cmd, data, fflag, &dp->d_slice);
	if (error == ENOIOCTL)
		error = dev_port_dioctl(dp->d_fwdport, dev, cmd, data, fflag, td);
	return (error);
}

static int
diskpsize(dev_t dev)
{
	struct disk *dp;
	dev_t pdev;

	pdev = dkmodpart(dkmodslice(dev, WHOLE_DISK_SLICE), RAW_PART);
	dp = pdev->si_disk;
	if (!dp)
		return (-1);
	if (dp != dev->si_disk) {
		dev->si_drv1 = pdev->si_drv1;
		dev->si_drv2 = pdev->si_drv2;
		/* XXX: don't set bp->b_dev->si_disk (?) */
	}
	return (dssize(dev, &dp->d_slice));
}

SYSCTL_DECL(_debug_sizeof);

SYSCTL_INT(_debug_sizeof, OID_AUTO, disklabel, CTLFLAG_RD, 
    0, sizeof(struct disklabel), "sizeof(struct disklabel)");

SYSCTL_INT(_debug_sizeof, OID_AUTO, diskslices, CTLFLAG_RD, 
    0, sizeof(struct diskslices), "sizeof(struct diskslices)");

SYSCTL_INT(_debug_sizeof, OID_AUTO, disk, CTLFLAG_RD, 
    0, sizeof(struct disk), "sizeof(struct disk)");

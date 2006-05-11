/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD: src/sys/dev/md/md.c,v 1.8.2.2 2002/08/19 17:43:34 jdp Exp $
 * $DragonFly: src/sys/dev/disk/md/md.c,v 1.12 2006/05/11 08:23:20 swildner Exp $
 *
 */

#include "opt_md.h"		/* We have adopted some tasks from MFS */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/devicestat.h>
#include <sys/disk.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/linker.h>
#include <sys/proc.h>
#include <sys/buf2.h>
#include <sys/thread2.h>

#ifndef MD_NSECT
#define MD_NSECT (10000 * 2)
#endif

MALLOC_DEFINE(M_MD, "MD disk", "Memory Disk");
MALLOC_DEFINE(M_MDSECT, "MD sectors", "Memory Disk Sectors");

static int md_debug;
SYSCTL_INT(_debug, OID_AUTO, mddebug, CTLFLAG_RW, &md_debug, 0, "");

#if defined(MD_ROOT) && defined(MD_ROOT_SIZE)
/* Image gets put here: */
static u_char mfs_root[MD_ROOT_SIZE*1024] = "MFS Filesystem goes here";
static u_char end_mfs_root[] __unused = "MFS Filesystem had better STOP here";
#endif

static int mdrootready;

static void mdcreate_malloc(void);

#define CDEV_MAJOR	95

static d_strategy_t mdstrategy;
static d_strategy_t mdstrategy_preload;
static d_strategy_t mdstrategy_malloc;
static d_open_t mdopen;
static d_ioctl_t mdioctl;

static struct cdevsw md_cdevsw = {
        /* name */      "md",
        /* maj */       CDEV_MAJOR,
        /* flags */     D_DISK | D_CANFREE | D_MEMDISK,
	/* port */	NULL,
	/* clone */	NULL,

        /* open */      mdopen,
        /* close */     nullclose,
        /* read */      physread,
        /* write */     physwrite,
        /* ioctl */     mdioctl,
        /* poll */      nopoll,
        /* mmap */      nommap,
        /* strategy */  mdstrategy,
        /* dump */      nodump,
        /* psize */     nopsize,
};

struct md_s {
	int unit;
	struct devstat stats;
	struct bio_queue_head bio_queue;
	struct disk disk;
	dev_t dev;
	int busy;
	enum {MD_MALLOC, MD_PRELOAD} type;
	unsigned nsect;

	/* MD_MALLOC related fields */
	unsigned nsecp;
	u_char **secp;

	/* MD_PRELOAD related fields */
	u_char *pl_ptr;
	unsigned pl_len;
};

static int mdunits;

static int
mdopen(dev_t dev, int flag, int fmt, struct thread *td)
{
	struct md_s *sc;
	struct disklabel *dl;

	if (md_debug)
		printf("mdopen(%s %x %x %p)\n",
			devtoname(dev), flag, fmt, td);

	sc = dev->si_drv1;
	if (sc->unit + 1 == mdunits)
		mdcreate_malloc();

	dl = &sc->disk.d_label;
	bzero(dl, sizeof(*dl));
	dl->d_secsize = DEV_BSIZE;
	dl->d_nsectors = 1024;
	dl->d_ntracks = 1;
	dl->d_secpercyl = dl->d_nsectors * dl->d_ntracks;
	dl->d_secperunit = sc->nsect;
	dl->d_ncylinders = dl->d_secperunit / dl->d_secpercyl;
	return (0);
}

static int
mdioctl(dev_t dev, u_long cmd, caddr_t addr, int flags, struct thread *td)
{

	if (md_debug)
		printf("mdioctl(%s %lx %p %x %p)\n",
			devtoname(dev), cmd, addr, flags, td);

	return (ENOIOCTL);
}

static void
mdstrategy(dev_t dev, struct bio *bio)
{
	struct buf *bp = bio->bio_buf;
	struct md_s *sc;

	if (md_debug > 1) {
		printf("mdstrategy(%p) %s %08x, %lld, %d, %p)\n",
		    bp, devtoname(dev), bp->b_flags, bio->bio_offset, 
		    bp->b_bcount, bp->b_data);
	}
	bio->bio_driver_info = dev;
	sc = dev->si_drv1;
	if (sc->type == MD_MALLOC) {
		mdstrategy_malloc(dev, bio);
	} else {
		mdstrategy_preload(dev, bio);
	}
}


static void
mdstrategy_malloc(dev_t dev, struct bio *bio)
{
	struct buf *bp = bio->bio_buf;
	unsigned secno, nsec, secval, uc;
	u_char *secp, **secpp, *dst;
	devstat_trans_flags dop;
	struct md_s *sc;
	int i;

	if (md_debug > 1)
		printf("mdstrategy_malloc(%p) %s %08xx, %lld, %d, %p)\n",
		    bp, devtoname(dev), bp->b_flags, bio->bio_offset, 
		    bp->b_bcount, bp->b_data);

	sc = dev->si_drv1;

	crit_enter();

	bioqdisksort(&sc->bio_queue, bio);

	if (sc->busy) {
		crit_exit();
		return;
	}

	sc->busy++;
	
	while (1) {
		bio = bioq_first(&sc->bio_queue);
		if (bp)
			bioq_remove(&sc->bio_queue, bio);
		crit_exit();
		if (bio == NULL)
			break;

		devstat_start_transaction(&sc->stats);

		switch(bp->b_cmd) {
		case BUF_CMD_FREEBLKS:
			dop = DEVSTAT_NO_DATA;
			break;
		case BUF_CMD_READ:
			dop = DEVSTAT_READ;
			break;
		case BUF_CMD_WRITE:
			dop = DEVSTAT_WRITE;
			break;
		default:
			panic("md: bad b_cmd %d", bp->b_cmd);
		}

		nsec = bp->b_bcount >> DEV_BSHIFT;
		secno = (unsigned)(bio->bio_offset >> DEV_BSHIFT);
		dst = bp->b_data;
		while (nsec--) {
			if (secno < sc->nsecp) {
				secpp = &sc->secp[secno];
				if ((u_int)*secpp > 255) {
					secp = *secpp;
					secval = 0;
				} else {
					secp = 0;
					secval = (u_int) *secpp;
				}
			} else {
				secpp = 0;
				secp = 0;
				secval = 0;
			}
			if (md_debug > 2)
				printf("%08x %p %p %d\n", bp->b_flags, secpp, secp, secval);

			switch(bp->b_cmd) {
			case BUF_CMD_FREEBLKS:
				if (secpp) {
					if (secp)
						FREE(secp, M_MDSECT);
					*secpp = 0;
				}
				break;
			case BUF_CMD_READ:
				if (secp) {
					bcopy(secp, dst, DEV_BSIZE);
				} else if (secval) {
					for (i = 0; i < DEV_BSIZE; i++)
						dst[i] = secval;
				} else {
					bzero(dst, DEV_BSIZE);
				}
				break;
			case BUF_CMD_WRITE:
				uc = dst[0];
				for (i = 1; i < DEV_BSIZE; i++) 
					if (dst[i] != uc)
						break;
				if (i == DEV_BSIZE && !uc) {
					if (secp)
						FREE(secp, M_MDSECT);
					if (secpp)
						*secpp = (u_char *)uc;
				} else {
					if (!secpp) {
						MALLOC(secpp, u_char **, (secno + nsec + 1) * sizeof(u_char *), M_MD, M_WAITOK);
						bzero(secpp, (secno + nsec + 1) * sizeof(u_char *));
						bcopy(sc->secp, secpp, sc->nsecp * sizeof(u_char *));
						FREE(sc->secp, M_MD);
						sc->secp = secpp;
						sc->nsecp = secno + nsec + 1;
						secpp = &sc->secp[secno];
					}
					if (i == DEV_BSIZE) {
						if (secp)
							FREE(secp, M_MDSECT);
						*secpp = (u_char *)uc;
					} else {
						if (!secp) 
							MALLOC(secp, u_char *, DEV_BSIZE, M_MDSECT, M_WAITOK);
						bcopy(dst, secp, DEV_BSIZE);

						*secpp = secp;
					}
				}
				break;
			default:
				panic("md: bad b_cmd %d", bp->b_cmd);

			}
			secno++;
			dst += DEV_BSIZE;
		}
		bp->b_resid = 0;
		devstat_end_transaction_buf(&sc->stats, bp);
		biodone(bio);
		crit_enter();
	}
	sc->busy = 0;
}


static void
mdstrategy_preload(dev_t dev, struct bio *bio)
{
	struct buf *bp = bio->bio_buf;
	devstat_trans_flags dop;
	struct md_s *sc;

	if (md_debug > 1)
		printf("mdstrategy_preload(%p) %s %08x, %lld, %d, %p)\n",
		    bp, devtoname(dev), bp->b_flags, bio->bio_offset, 
		    bp->b_bcount, bp->b_data);

	sc = dev->si_drv1;

	crit_enter();

	bioqdisksort(&sc->bio_queue, bio);

	if (sc->busy) {
		crit_exit();
		return;
	}

	sc->busy++;
	
	while (1) {
		bio = bioq_first(&sc->bio_queue);
		if (bio)
			bioq_remove(&sc->bio_queue, bio);
		crit_exit();
		if (bio == NULL)
			break;

		devstat_start_transaction(&sc->stats);

		switch(bp->b_cmd) {
		case BUF_CMD_FREEBLKS:
			dop = DEVSTAT_NO_DATA;
			break;
		case BUF_CMD_READ:
			dop = DEVSTAT_READ;
			bcopy(sc->pl_ptr + bio->bio_offset, 
			       bp->b_data, bp->b_bcount);
			break;
		case BUF_CMD_WRITE:
			dop = DEVSTAT_WRITE;
			bcopy(bp->b_data, sc->pl_ptr + bio->bio_offset,
			      bp->b_bcount);
			break;
		default:
			panic("md: bad cmd %d\n", bp->b_cmd);
		}
		bp->b_resid = 0;
		devstat_end_transaction_buf(&sc->stats, bp);
		biodone(bio);
		crit_enter();
	}
	sc->busy = 0;
}

static struct md_s *
mdcreate(void)
{
	struct md_s *sc;

	MALLOC(sc, struct md_s *,sizeof(*sc), M_MD, M_WAITOK);
	bzero(sc, sizeof(*sc));
	sc->unit = mdunits++;
	bioq_init(&sc->bio_queue);
	devstat_add_entry(&sc->stats, "md", sc->unit, DEV_BSIZE,
		DEVSTAT_NO_ORDERED_TAGS, 
		DEVSTAT_TYPE_DIRECT | DEVSTAT_TYPE_IF_OTHER,
		DEVSTAT_PRIORITY_OTHER);
	sc->dev = disk_create(sc->unit, &sc->disk, 0, &md_cdevsw);
	sc->dev->si_drv1 = sc;
	return (sc);
}

static void
mdcreate_preload(u_char *image, unsigned length)
{
	struct md_s *sc;

	sc = mdcreate();
	sc->type = MD_PRELOAD;
	sc->nsect = length / DEV_BSIZE;
	sc->pl_ptr = image;
	sc->pl_len = length;

	if (sc->unit == 0) 
		mdrootready = 1;
}

static void
mdcreate_malloc(void)
{
	struct md_s *sc;

	sc = mdcreate();
	sc->type = MD_MALLOC;

	sc->nsect = MD_NSECT;	/* for now */
	MALLOC(sc->secp, u_char **, sizeof(u_char *), M_MD, M_WAITOK);
	bzero(sc->secp, sizeof(u_char *));
	sc->nsecp = 1;
	printf("md%d: Malloc disk\n", sc->unit);
}

static void
md_drvinit(void *unused)
{

	caddr_t mod;
	caddr_t c;
	u_char *ptr, *name, *type;
	unsigned len;

#ifdef MD_ROOT_SIZE
	mdcreate_preload(mfs_root, MD_ROOT_SIZE*1024);
#endif
	mod = NULL;
	while ((mod = preload_search_next_name(mod)) != NULL) {
		name = (char *)preload_search_info(mod, MODINFO_NAME);
		type = (char *)preload_search_info(mod, MODINFO_TYPE);
		if (name == NULL)
			continue;
		if (type == NULL)
			continue;
		if (strcmp(type, "md_image") && strcmp(type, "mfs_root"))
			continue;
		c = preload_search_info(mod, MODINFO_ADDR);
		ptr = *(u_char **)c;
		c = preload_search_info(mod, MODINFO_SIZE);
		len = *(unsigned *)c;
		printf("md%d: Preloaded image <%s> %d bytes at %p\n",
		   mdunits, name, len, ptr);
		mdcreate_preload(ptr, len);
	} 
	mdcreate_malloc();
}

SYSINIT(mddev,SI_SUB_DRIVERS,SI_ORDER_MIDDLE+CDEV_MAJOR, md_drvinit,NULL)

#ifdef MD_ROOT
static void
md_takeroot(void *junk)
{
	if (mdrootready)
		rootdevnames[0] = "ufs:/dev/md0c";
}

SYSINIT(md_root, SI_SUB_MOUNT_ROOT, SI_ORDER_FIRST, md_takeroot, NULL);
#endif

/*-
 * Copyright (c) 1994 Bruce D. Evans.
 * All rights reserved.
 *
 * Copyright (c) 1982, 1986, 1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)ufs_disksubr.c	7.16 (Berkeley) 5/4/91
 *	from: ufs_disksubr.c,v 1.8 1994/06/07 01:21:39 phk Exp $
 * $FreeBSD: src/sys/kern/subr_diskmbr.c,v 1.45 2000/01/28 10:22:07 bde Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/diskslice.h>
#include <sys/diskmbr.h>
#include <sys/disk.h>
#include <sys/malloc.h>
#include <sys/syslog.h>
#include <sys/device.h>

#define TRACE(str)	do { if (dsi_debug) kprintf str; } while (0)

static volatile u_char dsi_debug;

/*
 * This is what we have embedded in every boot1 for supporting the bogus
 * "Dangerously Dedicated" mode. However, the old table is broken because
 * it has an illegal geometry in it - it specifies 256 heads (heads = end
 * head + 1) which causes nasty stuff when that wraps to zero in bios code.
 * eg: divide by zero etc. This caused the dead-thinkpad problem, numerous
 * SCSI bios crashes, EFI to crash, etc.
 *
 * We still have to recognize the old table though, even though we stopped
 * inflicting it upon the world.
 */
static struct dos_partition historical_bogus_partition_table[NDOSPART] = {
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	{ 0x80, 0, 1, 0, DOSPTYP_DFLYBSD, 255, 255, 255, 0, 50000, },
};
static struct dos_partition historical_bogus_partition_table_fixed[NDOSPART] = {
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
	{ 0x80, 0, 1, 0, DOSPTYP_DFLYBSD, 254, 255, 255, 0, 50000, },
};

static int check_part (char *sname, struct dos_partition *dp,
			   u_int64_t offset, int nsectors, int ntracks,
			   u_int64_t mbr_offset);
static void mbr_extended (cdev_t dev, struct disk_info *info,
			      struct diskslices *ssp, u_int64_t ext_offset,
			      u_int64_t ext_size, u_int64_t base_ext_offset,
			      int nsectors, int ntracks, u_int64_t mbr_offset,
			      int level);
static int mbr_setslice (char *sname, struct disk_info *info,
			     struct diskslice *sp, struct dos_partition *dp,
			     u_int64_t br_offset);


int
mbrinit(cdev_t dev, struct disk_info *info, struct diskslices **sspp)
{
	struct buf *bp;
	u_char	*cp;
	int	dospart;
	struct dos_partition *dp;
	struct dos_partition *dp0;
	struct dos_partition dpcopy[NDOSPART];
	int	error;
	int	max_ncyls;
	int	max_nsectors;
	int	max_ntracks;
	u_int64_t mbr_offset;
	char	partname[2];
	u_long	secpercyl;
	char	*sname = "tempname";
	struct diskslice *sp;
	struct diskslices *ssp;
	cdev_t wdev;

	mbr_offset = DOSBBSECTOR;
reread_mbr:
	/*
	 * Don't bother if the block size is weird or the
	 * media size is 0 (probably means no media present).
	 */
	if (info->d_media_blksize & DEV_BMASK)
		return (EIO);
	if (info->d_media_size == 0)
		return (EIO);

	/*
	 * Read master boot record.
	 */
	wdev = dev;
	bp = getpbuf_mem(NULL);
	KKASSERT((int)info->d_media_blksize <= bp->b_bufsize);
	bp->b_bio1.bio_offset = (off_t)mbr_offset * info->d_media_blksize;
	bp->b_bio1.bio_done = biodone_sync;
	bp->b_bio1.bio_flags |= BIO_SYNC;
	bp->b_bcount = info->d_media_blksize;
	bp->b_cmd = BUF_CMD_READ;
	bp->b_flags |= B_FAILONDIS;
	dev_dstrategy(wdev, &bp->b_bio1);
	if (biowait(&bp->b_bio1, "mbrrd") != 0) {
		if ((info->d_dsflags & DSO_MBRQUIET) == 0) {
			diskerr(&bp->b_bio1, wdev,
				"reading primary partition table: error",
				LOG_PRINTF, 0);
			kprintf("\n");
		}
		error = EIO;
		goto done;
	}

	/* Weakly verify it. */
	cp = bp->b_data;
	sname = dsname(dev, 0, 0, 0, NULL);
	if (cp[0x1FE] != 0x55 || cp[0x1FF] != 0xAA) {
		if (bootverbose)
			kprintf("%s: invalid primary partition table: no magic\n",
			       sname);
		error = EINVAL;
		goto done;
	}

	/* Make a copy of the partition table to avoid alignment problems. */
	memcpy(&dpcopy[0], cp + DOSPARTOFF, sizeof(dpcopy));

	dp0 = &dpcopy[0];

	/*
	 * Check for "Ontrack Diskmanager" or GPT.  If a GPT is found in
	 * the first dos partition, ignore the rest of the MBR and go
	 * to GPT processing.
	 */
	for (dospart = 0, dp = dp0; dospart < NDOSPART; dospart++, dp++) {
		if (dospart == 0 && dp->dp_typ == DOSPTYP_PMBR) {
			if (bootverbose)
				kprintf(
	    "%s: Found GPT in slice #%d\n", sname, dospart + 1);
			error = gptinit(dev, info, sspp);
			goto done;
		}

		if (dp->dp_typ == DOSPTYP_ONTRACK) {
			if (bootverbose)
				kprintf(
	    "%s: Found \"Ontrack Disk Manager\" on this disk.\n", sname);
			bp->b_flags |= B_INVAL | B_AGE;
			brelse(bp);
			mbr_offset = 63;
			goto reread_mbr;
		}
	}

	if (bcmp(dp0, historical_bogus_partition_table,
		 sizeof historical_bogus_partition_table) == 0 ||
	    bcmp(dp0, historical_bogus_partition_table_fixed,
		 sizeof historical_bogus_partition_table_fixed) == 0) {
#if 0
		TRACE(("%s: invalid primary partition table: historical\n",
		       sname));
#endif /* 0 */
		if (bootverbose)
			kprintf(
     "%s: invalid primary partition table: Dangerously Dedicated (ignored)\n",
			       sname);
		error = EINVAL;
		goto done;
	}

	/* Guess the geometry. */
	/*
	 * TODO:
	 * Perhaps skip entries with 0 size.
	 * Perhaps only look at entries of type DOSPTYP_386BSD or
	 * DOSPTYP_DFLYBSD
	 */
	max_ncyls = 0;
	max_nsectors = 0;
	max_ntracks = 0;
	for (dospart = 0, dp = dp0; dospart < NDOSPART; dospart++, dp++) {
		int	ncyls;
		int	nsectors;
		int	ntracks;

		ncyls = DPCYL(dp->dp_ecyl, dp->dp_esect) + 1;
		if (max_ncyls < ncyls)
			max_ncyls = ncyls;
		nsectors = DPSECT(dp->dp_esect);
		if (max_nsectors < nsectors)
			max_nsectors = nsectors;
		ntracks = dp->dp_ehd + 1;
		if (max_ntracks < ntracks)
			max_ntracks = ntracks;
	}

	/*
	 * Check that we have guessed the geometry right by checking the
	 * partition entries.
	 */
	/*
	 * TODO:
	 * As above.
	 * Check for overlaps.
	 * Check against d_secperunit if the latter is reliable.
	 */
	error = 0;
	for (dospart = 0, dp = dp0; dospart < NDOSPART; dospart++, dp++) {
		if (dp->dp_scyl == 0 && dp->dp_shd == 0 && dp->dp_ssect == 0
		    && dp->dp_start == 0 && dp->dp_size == 0)
			continue;
		//sname = dsname(dev, dkunit(dev), BASE_SLICE + dospart,
		//	       WHOLE_SLICE_PART, partname);

		/*
		 * Temporarily ignore errors from this check.  We could
		 * simplify things by accepting the table eariler if we
		 * always ignore errors here.  Perhaps we should always
		 * accept the table if the magic is right but not let
		 * bad entries affect the geometry.
		 */
		check_part(sname, dp, mbr_offset, max_nsectors, max_ntracks,
			   mbr_offset);
	}
	if (error != 0)
		goto done;

	/*
	 * Accept the DOS partition table.
	 *
	 * Adjust the disk information structure with updated CHS
	 * conversion parameters, but only use values extracted from
	 * the primary partition table.
	 *
	 * NOTE!  Regardless of our having to deal with this old cruft,
	 * we do not screw around with the info->d_media* parameters.
	 */
	secpercyl = (u_long)max_nsectors * max_ntracks;
	if (secpercyl != 0 && mbr_offset == DOSBBSECTOR) {
		info->d_secpertrack = max_nsectors;
		info->d_nheads = max_ntracks;
		info->d_secpercyl = secpercyl;
		info->d_ncylinders = info->d_media_blocks / secpercyl;
	}

	/*
	 * We are passed a pointer to a suitably initialized minimal
	 * slices "struct" with no dangling pointers in it.  Replace it
	 * by a maximal one.  This usually oversizes the "struct", but
	 * enlarging it while searching for logical drives would be
	 * inconvenient.
	 */
	kfree(*sspp, M_DEVBUF);
	ssp = dsmakeslicestruct(MAX_SLICES, info);
	*sspp = ssp;

	/* Initialize normal slices. */
	sp = &ssp->dss_slices[BASE_SLICE];
	for (dospart = 0, dp = dp0; dospart < NDOSPART; dospart++, dp++, sp++) {
		sname = dsname(dev, dkunit(dev), BASE_SLICE + dospart,
			       WHOLE_SLICE_PART, partname);
		(void)mbr_setslice(sname, info, sp, dp, mbr_offset);
	}
	ssp->dss_nslices = BASE_SLICE + NDOSPART;

	/* Handle extended partitions. */
	sp -= NDOSPART;
	for (dospart = 0; dospart < NDOSPART; dospart++, sp++) {
		if (sp->ds_type == DOSPTYP_EXT ||
		    sp->ds_type == DOSPTYP_EXTLBA) {
			mbr_extended(wdev, info, ssp,
				     sp->ds_offset, sp->ds_size, sp->ds_offset,
				     max_nsectors, max_ntracks, mbr_offset, 1);
		}
	}

	/*
	 * mbr_extended() abuses ssp->dss_nslices for the number of slices
	 * that would be found if there were no limit on the number of slices
	 * in *ssp.  Cut it back now.
	 */
	if (ssp->dss_nslices > MAX_SLICES)
		ssp->dss_nslices = MAX_SLICES;

done:
	bp->b_flags |= B_INVAL | B_AGE;
	relpbuf(bp, NULL);
	if (error == EINVAL)
		error = 0;
	return (error);
}

static int
check_part(char *sname, struct dos_partition *dp, u_int64_t offset,
	    int nsectors, int ntracks, u_int64_t mbr_offset)
{
	int	chs_ecyl;
	int	chs_esect;
	int	chs_scyl;
	int	chs_ssect;
	int	error;
	u_long	secpercyl;
	u_int64_t esector;
	u_int64_t esector1;
	u_int64_t ssector;
	u_int64_t ssector1;

	secpercyl = (u_long)nsectors * ntracks;
	chs_scyl = DPCYL(dp->dp_scyl, dp->dp_ssect);
	chs_ssect = DPSECT(dp->dp_ssect);
	ssector = chs_ssect - 1 + dp->dp_shd * nsectors + chs_scyl * secpercyl
		  + mbr_offset;
	ssector1 = offset + dp->dp_start;

	/*
	 * If ssector1 is on a cylinder >= 1024, then ssector can't be right.
	 * Allow the C/H/S for it to be 1023/ntracks-1/nsectors, or correct
	 * apart from the cylinder being reduced modulo 1024.  Always allow
	 * 1023/255/63, because this is the official way to represent
	 * pure-LBA for the starting position.
	 */
	if ((ssector < ssector1
	     && ((chs_ssect == nsectors && dp->dp_shd == ntracks - 1
		  && chs_scyl == 1023)
		 || (secpercyl != 0
		     && (ssector1 - ssector) % (1024 * secpercyl) == 0)))
	    || (dp->dp_scyl == 255 && dp->dp_shd == 255
		&& dp->dp_ssect == 255)) {
		TRACE(("%s: C/H/S start %d/%d/%d, start %llu: allow\n",
		       sname, chs_scyl, dp->dp_shd, chs_ssect,
		       (long long)ssector1));
		ssector = ssector1;
	}

	chs_ecyl = DPCYL(dp->dp_ecyl, dp->dp_esect);
	chs_esect = DPSECT(dp->dp_esect);
	esector = chs_esect - 1 + dp->dp_ehd * nsectors + chs_ecyl * secpercyl
		  + mbr_offset;
	esector1 = ssector1 + dp->dp_size - 1;

	/*
	 * Allow certain bogus C/H/S values for esector, as above. However,
	 * heads == 255 isn't really legal and causes some BIOS crashes. The
	 * correct value to indicate a pure-LBA end is 1023/heads-1/sectors -
	 * usually 1023/254/63. "heads" is base 0, "sectors" is base 1.
	 */
	if ((esector < esector1
	     && ((chs_esect == nsectors && dp->dp_ehd == ntracks - 1
		  && chs_ecyl == 1023)
		 || (secpercyl != 0
		     && (esector1 - esector) % (1024 * secpercyl) == 0)))
	    || (dp->dp_ecyl == 255 && dp->dp_ehd == 255
		&& dp->dp_esect == 255)) {
		TRACE(("%s: C/H/S end %d/%d/%d, end %llu: allow\n",
		       sname, chs_ecyl, dp->dp_ehd, chs_esect,
		       (long long)esector1));
		esector = esector1;
	}

	error = (ssector == ssector1 && esector == esector1) ? 0 : EINVAL;
	if (bootverbose)
		kprintf("%s: type 0x%x, start %llu, end = %llu, size %u %s\n",
		       sname, dp->dp_typ,
		       (long long)ssector1, (long long)esector1,
		       dp->dp_size, (error ? "" : ": OK"));
	if (ssector != ssector1 && bootverbose)
		kprintf("%s: C/H/S start %d/%d/%d (%llu) != start %llu: invalid\n",
		       sname, chs_scyl, dp->dp_shd, chs_ssect,
		       (long long)ssector, (long long)ssector1);
	if (esector != esector1 && bootverbose)
		kprintf("%s: C/H/S end %d/%d/%d (%llu) != end %llu: invalid\n",
		       sname, chs_ecyl, dp->dp_ehd, chs_esect,
		       (long long)esector, (long long)esector1);
	return (error);
}

static
void
mbr_extended(cdev_t dev, struct disk_info *info, struct diskslices *ssp,
	    u_int64_t ext_offset, u_int64_t ext_size, u_int64_t base_ext_offset,
	    int nsectors, int ntracks, u_int64_t mbr_offset, int level)
{
	struct buf *bp;
	u_char	*cp;
	int	dospart;
	struct dos_partition *dp;
	struct dos_partition dpcopy[NDOSPART];
	u_int64_t ext_offsets[NDOSPART];
	u_int64_t ext_sizes[NDOSPART];
	char	partname[2];
	int	slice;
	char	*sname;
	struct diskslice *sp;

	if (level >= 16) {
		kprintf(
	"%s: excessive recursion in search for slices; aborting search\n",
		       devtoname(dev));
		return;
	}

	/* Read extended boot record. */
	bp = getpbuf_mem(NULL);
	KKASSERT((int)info->d_media_blksize <= bp->b_bufsize);
	bp->b_bio1.bio_offset = (off_t)ext_offset * info->d_media_blksize;
	bp->b_bio1.bio_done = biodone_sync;
	bp->b_bio1.bio_flags |= BIO_SYNC;
	bp->b_bcount = info->d_media_blksize;
	bp->b_cmd = BUF_CMD_READ;
	bp->b_flags |= B_FAILONDIS;
	dev_dstrategy(dev, &bp->b_bio1);
	if (biowait(&bp->b_bio1, "mbrrd") != 0) {
		diskerr(&bp->b_bio1, dev,
			"reading extended partition table: error",
			LOG_PRINTF, 0);
		kprintf("\n");
		goto done;
	}

	/* Weakly verify it. */
	cp = bp->b_data;
	if (cp[0x1FE] != 0x55 || cp[0x1FF] != 0xAA) {
		sname = dsname(dev, dkunit(dev), WHOLE_DISK_SLICE, WHOLE_SLICE_PART,
			       partname);
		if (bootverbose)
			kprintf("%s: invalid extended partition table: no magic\n",
			       sname);
		goto done;
	}

	/* Make a copy of the partition table to avoid alignment problems. */
	memcpy(&dpcopy[0], cp + DOSPARTOFF, sizeof(dpcopy));

	slice = ssp->dss_nslices;
	for (dospart = 0, dp = &dpcopy[0]; dospart < NDOSPART;
	    dospart++, dp++) {
		ext_sizes[dospart] = 0;
		if (dp->dp_scyl == 0 && dp->dp_shd == 0 && dp->dp_ssect == 0
		    && dp->dp_start == 0 && dp->dp_size == 0)
			continue;
		if (dp->dp_typ == DOSPTYP_EXT ||
		    dp->dp_typ == DOSPTYP_EXTLBA) {
			static char buf[32];

			sname = dsname(dev, dkunit(dev), WHOLE_DISK_SLICE,
				       WHOLE_SLICE_PART, partname);
			ksnprintf(buf, sizeof(buf), "%s", sname);
			if (strlen(buf) < sizeof buf - 11)
				strcat(buf, "<extended>");
			check_part(buf, dp, base_ext_offset, nsectors,
				   ntracks, mbr_offset);
			ext_offsets[dospart] = base_ext_offset + dp->dp_start;
			ext_sizes[dospart] = dp->dp_size;
		} else {
			sname = dsname(dev, dkunit(dev), slice, WHOLE_SLICE_PART,
				       partname);
			check_part(sname, dp, ext_offset, nsectors, ntracks,
				   mbr_offset);
			if (slice >= MAX_SLICES) {
				kprintf("%s: too many slices\n", sname);
				slice++;
				continue;
			}
			sp = &ssp->dss_slices[slice];
			if (mbr_setslice(sname, info, sp, dp, ext_offset) != 0)
				continue;
			slice++;
		}
	}
	ssp->dss_nslices = slice;

	/* If we found any more slices, recursively find all the subslices. */
	for (dospart = 0; dospart < NDOSPART; dospart++) {
		if (ext_sizes[dospart] != 0) {
			mbr_extended(dev, info, ssp, ext_offsets[dospart],
				     ext_sizes[dospart], base_ext_offset,
				     nsectors, ntracks, mbr_offset, ++level);
		}
	}

done:
	bp->b_flags |= B_INVAL | B_AGE;
	relpbuf(bp, NULL);
}

static int
mbr_setslice(char *sname, struct disk_info *info, struct diskslice *sp,
	    struct dos_partition *dp, u_int64_t br_offset)
{
	u_int64_t	offset;
	u_int64_t	size;

	offset = br_offset + dp->dp_start;
	if (offset > info->d_media_blocks || offset < br_offset) {
		kprintf(
		"%s: slice starts beyond end of the disk: rejecting it\n",
		       sname);
		return (1);
	}
	size = info->d_media_blocks - offset;
	if (size >= dp->dp_size) {
		if (dp->dp_size == 0xFFFFFFFFU) {
			kprintf("%s: slice >2TB, using media size instead "
				"of slice table size\n", sname);
		} else {
			size = dp->dp_size;
		}
	} else {
		kprintf("%s: slice extends beyond end of disk: "
			"truncating from %u to %llu sectors\n",
		        sname, dp->dp_size, (unsigned long long)size);
	}
	sp->ds_offset = offset;
	sp->ds_size = size;
	sp->ds_type = dp->dp_typ;
	bzero(&sp->ds_type_uuid, sizeof(sp->ds_type_uuid));
	bzero(&sp->ds_stor_uuid, sizeof(sp->ds_type_uuid));

	/*
	 * Slices do not overlap with the parent (if any).
	 */
	sp->ds_reserved = 0;
	return (0);
}

/*
 * SYS/BIOTRACK.H
 *
 * $DragonFly: src/sys/sys/biotrack.h,v 1.1 2006/02/17 19:18:07 dillon Exp $
 */

#ifndef _SYS_BIOTRACK_H_
#define _SYS_BIOTRACK_H_

/*
 * BIO tracking structure - tracks in-progress BIOs
 */
struct bio_track {
	int	bk_active;      /* I/O's currently in progress */
};

#define bio_track_active(track)	((track)->bk_active)
#define bio_track_ref(track)	atomic_add_int(&(track)->bk_active, 1)

#ifdef _KERNEL

int	bio_track_wait(struct bio_track *track, int slp_flags, int slp_timo);

#endif

#endif

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
	int	bk_waitflag;
};

#endif

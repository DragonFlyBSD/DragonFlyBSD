/*
 * Copyright (c) 2011 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Antonio Huete <tuxillo@quantumachine.net>
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifndef _LIBHAMMER_H_
#define _LIBHAMMER_H_

#include <sys/queue.h>
#include <sys/param.h>

#include <vfs/hammer/hammer_disk.h>
#include <vfs/hammer/hammer_ioctl.h>

#define TXTLEN	64

#define HAMMER_BUFLISTS		64
#define HAMMER_BUFLISTMASK	(HAMMER_BUFLISTS - 1)
#define COLLECT_HSIZE	1024
#define COLLECT_HMASK	(COLLECT_HSIZE - 1)
#define HAMMER_BUFINFO_READAHEAD	0x0001
/*
 * WARNING: Do not make the SNAPSHOTS_BASE "/var/snapshots" because
 * it will interfere with the older HAMMER VERS < 3 snapshots directory
 * for the /var PFS.
 */
#define SNAPSHOTS_BASE	"/var/hammer"	/* HAMMER VERS >= 3 */
#define WS	" \t\r\n"

#define SERIALBUF_SIZE	(512 * 1024)
#define RD_HSIZE	32768
#define RD_HMASK	(RD_HSIZE - 1)

#define DICTF_MADEDIR	0x01
#define DICTF_MADEFILE	0x02
#define DICTF_PARENT	0x04	/* parent attached for real */
#define DICTF_TRAVERSED	0x80
#define FLAG_TOOFARLEFT		0x0001
#define FLAG_TOOFARRIGHT	0x0002
#define FLAG_BADTYPE		0x0004
#define FLAG_BADCHILDPARENT	0x0008
#define FLAG_BADMIRRORTID	0x0010

/*
 * Hammer information system structures
 */
struct libhammer_head {
	int32_t error;
	int32_t flags;
	int32_t rsv[2];
};

typedef struct libhammer_snapinfo {
	struct libhammer_head head; /* Additional error and flags */

	TAILQ_ENTRY(libhammer_snapinfo) entries;
	hammer_tid_t    tid;
	u_int64_t       ts;
	char            label[64];
	u_int64_t       rsv;
} *libhammer_snapinfo_t;

typedef struct libhammer_pfsinfo {
	struct libhammer_head head; /* Additional error and flags */

	uint32_t  snapcount;	    /* Snapshot count */
	u_int32_t version;          /* HAMMER version */
	char      *mountedon;       /* Mount path of the PFS */
	int       ismaster;         /* Is a PFS master */
	int       pfs_id;           /* PFS ID number */
	int       mirror_flags;     /* Misc flags */
	char      snapshots[64];    /* softlink dir for pruning */
	uuid_t    unique_uuid;      /* unique uuid of this master/slave */
	TAILQ_ENTRY(libhammer_pfsinfo) entries;
	TAILQ_HEAD(snaplist, libhammer_snapinfo) list_snap;
	hammer_tid_t beg_tid;       /* Earliest TID with full history */
	hammer_tid_t end_tid;       /* Current synchronisation TID */

	uint64_t rsv[4];
} *libhammer_pfsinfo_t;

typedef struct libhammer_fsinfo {
	struct libhammer_head head; /* Additional error and flags */

	char     vol_name[TXTLEN];  /* Volume name */
	uuid_t   vol_fsid;          /* Filesystem UUID */
	int      version;           /* HAMMER version */
	int      nvolumes;          /* Number of volumes */
	int64_t  inodes;            /* no. of inodes */
	int64_t  bigblocks;         /* Total big blocks */
	int64_t  freebigblocks;     /* Free big blocks */
	int64_t  rsvbigblocks;      /* Reserved big blocks */
	int32_t  rsv[8];
	TAILQ_HEAD(pfslist, libhammer_pfsinfo) list_pseudo;
} *libhammer_fsinfo_t;

struct libhammer_btree_stats {
	int64_t elements;
	int64_t iterations;
	int64_t splits;
	int64_t inserts;
	int64_t deletes;
	int64_t lookups;
	int64_t searches;
};

struct libhammer_io_stats {
	int64_t undo;
	int64_t dev_writes;
	int64_t dev_reads;
	int64_t file_writes;
	int64_t file_reads;
	int64_t file_iop_writes;
	int64_t file_iop_reads;
	int64_t inode_flushes;
	int64_t commits;
};

/*
 * Function prototypes
 */
__BEGIN_DECLS
libhammer_fsinfo_t libhammer_get_fsinfo(const char *);
void libhammer_free_fsinfo(libhammer_fsinfo_t);

int libhammer_stats_redo(int64_t *);
int libhammer_stats_undo(int64_t *);
int libhammer_stats_commits(int64_t *);
int libhammer_stats_inode_flushes(int64_t *);
int libhammer_stats_disk_write(int64_t *);
int libhammer_stats_disk_read(int64_t *);
int libhammer_stats_file_iopsw(int64_t *);
int libhammer_stats_file_iopsr(int64_t *);
int libhammer_stats_file_write(int64_t *);
int libhammer_stats_file_read(int64_t *);
int libhammer_stats_record_iterations(int64_t *);
int libhammer_stats_root_iterations(int64_t *);
int libhammer_stats_btree_iterations(int64_t *);
int libhammer_stats_btree_splits(int64_t *);
int libhammer_stats_btree_elements(int64_t *);
int libhammer_stats_btree_deletes(int64_t *);
int libhammer_stats_btree_inserts(int64_t *);
int libhammer_stats_btree_lookups(int64_t *);
int libhammer_stats_btree_searches(int64_t *);
int libhammer_btree_stats(struct libhammer_btree_stats *);
int libhammer_io_stats(struct libhammer_io_stats *);

int libhammer_pfs_get_snapshots(libhammer_fsinfo_t, libhammer_pfsinfo_t);
void libhammer_pfs_free_snapshots(libhammer_pfsinfo_t);

char *libhammer_find_pfs_mount(uuid_t *);
void libhammer_pfs_canonical_path(char *, libhammer_pfsinfo_t, char **);
void *_libhammer_malloc(size_t);
void libhammer_compat_old_snapcount(libhammer_pfsinfo_t);
__END_DECLS

/*
 * Wrappers to operate PFS TAILQ for each HAMMER filesystem
 */
static __inline libhammer_pfsinfo_t
libhammer_get_next_pfs(libhammer_pfsinfo_t pfsinfo)
{
	return TAILQ_NEXT(pfsinfo, entries);
}

static __inline libhammer_pfsinfo_t
libhammer_get_prev_pfs(libhammer_pfsinfo_t pfsinfo)
{
	return TAILQ_PREV(pfsinfo, pfslist, entries);
}

static __inline libhammer_pfsinfo_t
libhammer_get_first_pfs(libhammer_fsinfo_t fip)
{
	return TAILQ_FIRST(&fip->list_pseudo);
}

static __inline libhammer_pfsinfo_t
libhammer_get_last_pfs(libhammer_fsinfo_t fip)
{
	return TAILQ_LAST(&fip->list_pseudo, pfslist);
}

/*
 * Wrappers to operate snapshot TAILQ for each PFS
 */
static __inline libhammer_snapinfo_t
libhammer_get_next_snap(libhammer_snapinfo_t sip)
{
	return TAILQ_NEXT(sip, entries);
}

static __inline libhammer_snapinfo_t
libhammer_get_prev_snap(libhammer_snapinfo_t sip)
{
	return TAILQ_PREV(sip, snaplist, entries);
}

static __inline libhammer_snapinfo_t
libhammer_get_first_snap(libhammer_pfsinfo_t pip)
{
	return TAILQ_FIRST(&pip->list_snap);
}

static __inline libhammer_snapinfo_t
libhammer_get_last_snap(libhammer_pfsinfo_t pip)
{
	return TAILQ_LAST(&pip->list_snap, snaplist);
}

#endif

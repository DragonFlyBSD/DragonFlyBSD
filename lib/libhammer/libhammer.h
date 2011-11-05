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
} *libhammer_pfsinfo_t;

typedef struct libhammer_volinfo {
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
} *libhammer_volinfo_t;

/*
 * INFO directive prototypes
 */
__BEGIN_DECLS
libhammer_volinfo_t libhammer_get_volinfo(const char *);
void libhammer_free_volinfo(libhammer_volinfo_t);
__END_DECLS

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
libhammer_get_first_pfs(libhammer_volinfo_t volinfo)
{
	return TAILQ_FIRST(&volinfo->list_pseudo);
}

static __inline libhammer_pfsinfo_t
libhammer_get_last_pfs(libhammer_volinfo_t volinfo)
{
	return TAILQ_LAST(&volinfo->list_pseudo, pfslist);
}

/*
 * MISC directive prototypes
 */
__BEGIN_DECLS
char *libhammer_find_pfs_mount(int, uuid_t, int);
void *_libhammer_malloc(size_t);
__END_DECLS

#endif

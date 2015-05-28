# DragonflyBSD Makefile

LIB=	hammer
SHLIBDIR?= /lib
SRCS=	crc32.c icrc32.c info.c misc.c stats.c snapshot.c compat.c
INCS=	libhammer.h

MAN+=	 libhammer.3
MAN+=	 libhammer_fsinfo.3
MAN+=	 libhammer_snapshot.3
MAN+=	 libhammer_stats.3

MLINKS+= libhammer_fsinfo.3 libhammer_get_fsinfo.3
MLINKS+= libhammer_fsinfo.3 libhammer_get_next_pfs.3
MLINKS+= libhammer_fsinfo.3 libhammer_get_prev_pfs.3
MLINKS+= libhammer_fsinfo.3 libhammer_get_first_pfs.3
MLINKS+= libhammer_fsinfo.3 libhammer_get_last_pfs.3
MLINKS+= libhammer_fsinfo.3 libhammer_free_fsinfo.3

MLINKS+= libhammer_snapshot.3 libhammer_pfs_get_snapshots.3
MLINKS+= libhammer_snapshot.3 libhammer_get_next_snap.3
MLINKS+= libhammer_snapshot.3 libhammer_get_prev_snap.3
MLINKS+= libhammer_snapshot.3 libhammer_get_first_snap.3
MLINKS+= libhammer_snapshot.3 libhammer_get_last_snap.3
MLINKS+= libhammer_snapshot.3 libhammer_pfs_free_snapshots.3

MLINKS+= libhammer_stats.3 libhammer_stats_redo.3
MLINKS+= libhammer_stats.3 libhammer_stats_undo.3
MLINKS+= libhammer_stats.3 libhammer_stats_commits.3
MLINKS+= libhammer_stats.3 libhammer_stats_inode_flushes.3
MLINKS+= libhammer_stats.3 libhammer_stats_disk_write.3
MLINKS+= libhammer_stats.3 libhammer_stats_disk_read.3
MLINKS+= libhammer_stats.3 libhammer_stats_file_iopsw.3
MLINKS+= libhammer_stats.3 libhammer_stats_file_iopsr.3
MLINKS+= libhammer_stats.3 libhammer_stats_file_write.3
MLINKS+= libhammer_stats.3 libhammer_stats_file_read.3
MLINKS+= libhammer_stats.3 libhammer_stats_record_iterations.3
MLINKS+= libhammer_stats.3 libhammer_stats_root_iterations.3
MLINKS+= libhammer_stats.3 libhammer_stats_btree_iterations.3
MLINKS+= libhammer_stats.3 libhammer_stats_btree_splits.3
MLINKS+= libhammer_stats.3 libhammer_stats_btree_elements.3
MLINKS+= libhammer_stats.3 libhammer_stats_btree_deletes.3
MLINKS+= libhammer_stats.3 libhammer_stats_btree_inserts.3
MLINKS+= libhammer_stats.3 libhammer_stats_btree_lookups.3
MLINKS+= libhammer_stats.3 libhammer_stats_btree_searches.3
MLINKS+= libhammer_stats.3 libhammer_btree_stats.3
MLINKS+= libhammer_stats.3 libhammer_io_stats.3

.PATH: ${.CURDIR}/../../sys/libkern
SHLIB_MAJOR=	0

.include <bsd.lib.mk>

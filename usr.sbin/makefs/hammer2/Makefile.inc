.PATH:	${.CURDIR}/hammer2

CFLAGS+=-I${.CURDIR}/../../sys
CFLAGS+=-I${.CURDIR}/../../sbin/hammer2
CFLAGS+=-I${.CURDIR}/../../sbin/newfs_hammer2

#CFLAGS+=-DHAMMER2_IO_DEBUG
#CFLAGS+=-DHAMMER2_DEBUG_SYNC

SRCS+=	hammer2_admin.c hammer2_bulkfree.c hammer2_chain.c hammer2_cluster.c hammer2_flush.c hammer2_freemap.c hammer2_inode.c hammer2_io.c hammer2_ioctl.c hammer2_lz4.c hammer2_ondisk.c hammer2_strategy.c hammer2_subr.c hammer2_vfsops.c hammer2_vnops.c hammer2_xops.c
SRCS+=	hammer2_buf.c

.PATH:	${.CURDIR}/hammer2/zlib
SRCS+=	hammer2_zlib_adler32.c hammer2_zlib_deflate.c hammer2_zlib_inffast.c hammer2_zlib_inflate.c hammer2_zlib_inftrees.c hammer2_zlib_trees.c hammer2_zlib_zutil.c
.PATH:	${.CURDIR}/../../sbin/hammer2
SRCS+=	subs.c ondisk.c
.PATH:	${.CURDIR}/../../sbin/newfs_hammer2
SRCS+=	mkfs_hammer2.c
.PATH:	${.CURDIR}/../../sys/libkern
SRCS+=	icrc32.c
.PATH:	${.CURDIR}/../../sys/vfs/hammer2/xxhash
SRCS+=	xxhash.c

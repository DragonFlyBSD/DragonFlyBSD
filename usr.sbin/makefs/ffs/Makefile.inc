#	$FreeBSD: head/usr.sbin/makefs/ffs/Makefile.inc 332844 2018-04-20 22:23:38Z emaste $
#

.PATH:	${.CURDIR}/ffs ${.CURDIR}/../../sys/vfs/ufs
SRCS+=	ffs_alloc.c ffs_balloc.c ffs_bswap.c ffs_subr.c ufs_bmap.c
SRCS+=	buf.c mkfs.c

# Reach-over source from sys/ufs/ffs
SRCS+=	ffs_tables.c

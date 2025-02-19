#	$FreeBSD: head/usr.sbin/makefs/Makefile 332844 2018-04-20 22:23:38Z emaste $

PROG=	makefs

SRCS=	cd9660.c \
	ffs.c \
	hammer2.c \
	makefs.c \
	msdos.c \
	mtree.c \
	walk.c
MAN=	makefs.8

WARNS?=	2

CFLAGS+=-I${.CURDIR} # -g

.include "${.CURDIR}/cd9660/Makefile.inc"
.include "${.CURDIR}/ffs/Makefile.inc"
.include "${.CURDIR}/msdos/Makefile.inc"
.include "${.CURDIR}/hammer2/Makefile.inc"

CFLAGS+=-DHAVE_NBTOOL_CONFIG_H=0
CFLAGS+=-DHAVE_NETDB_H=1
CFLAGS+=-DHAVE_STRUCT_STAT_BIRTHTIME=0
CFLAGS+=-DHAVE_STRUCT_STAT_ST_FLAGS=1
CFLAGS+=-DHAVE_STRUCT_STAT_ST_MTIMENSEC=0
CFLAGS+=-DHAVE_STRUCT_STATVFS_F_IOSIZE=0

.PATH: ${.CURDIR}/../mtree
CFLAGS+=-I${.CURDIR}/../mtree
SRCS+=	getid.c misc.c pack_dev.c spec.c stat_flags.c

DPADD=	${LIBUTIL} ${LIBSBUF}
LDADD=	-lutil -lsbuf

#.if ${MK_TESTS} != "no"
#SUBDIR+=	tests
#.endif

.include <bsd.prog.mk>

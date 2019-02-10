#	$FreeBSD: head/usr.sbin/makefs/Makefile 332844 2018-04-20 22:23:38Z emaste $

PROG=	makefs

CFLAGS+=-I${.CURDIR}

SRCS=	cd9660.c ffs.c \
	makefs.c \
	mtree.c \
	walk.c
MAN=	makefs.8

WARNS?=	2

.include "${.CURDIR}/cd9660/Makefile.inc"
.include "${.CURDIR}/ffs/Makefile.inc"

CFLAGS+=-DHAVE_STRUCT_STAT_ST_FLAGS=1

.PATH: ${.CURDIR}/../mtree
CFLAGS+=-I${.CURDIR}/../mtree
SRCS+=	getid.c misc.c pack_dev.c spec.c stat_flags.c

DPADD=	${LIBUTIL} ${LIBSBUF}
LDADD=	-lutil -lsbuf

#.if ${MK_TESTS} != "no"
#SUBDIR+=	tests
#.endif

.include <bsd.prog.mk>

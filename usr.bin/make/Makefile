#	@(#)Makefile	5.2 (Berkeley) 12/28/90
# $FreeBSD: src/usr.bin/make/Makefile,v 1.13.2.1 2001/05/25 08:33:40 sobomax Exp $
# $DragonFly: src/usr.bin/make/Makefile,v 1.12 2004/12/09 19:16:03 okumoto Exp $

PROG=	make
CFLAGS+=-I${.CURDIR}
SRCS=	arch.c buf.c compat.c cond.c dir.c for.c hash.c job.c main.c \
	make.c parse.c str.c suff.c targ.c util.c var.c var_modify.c
SRCS+=	lstAppend.c lstClose.c lstConcat.c lstDeQueue.c lstDestroy.c \
	lstDupl.c lstFindFrom.c lstForEachFrom.c lstInit.c lstInsert.c \
	lstIsAtEnd.c lstMember.c lstNext.c lstOpen.c lstRemove.c
.PATH:	${.CURDIR}/lst.lib

WARNS?=	3
NOSHARED?=	YES

CFLAGS+=-DMAKE_VERSION=\"5200408120\"

# XXX: kernel currently broken
# CFLAGS+=-DUSE_KQUEUE

main.o: ${MAKEFILE}

.include <bsd.prog.mk>

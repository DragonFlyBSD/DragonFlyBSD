#	@(#)Makefile	5.2 (Berkeley) 12/28/90
# $FreeBSD: src/usr.bin/make/Makefile,v 1.13.2.1 2001/05/25 08:33:40 sobomax Exp $
# $DragonFly: src/usr.bin/make/Makefile,v 1.5 2004/11/18 02:01:39 dillon Exp $

PROG=	make
CFLAGS+=-I${.CURDIR}
SRCS=	arch.c buf.c compat.c cond.c dir.c for.c hash.c job.c main.c \
	make.c parse.c str.c suff.c targ.c util.c var.c var_modify.c
SRCS+=	lstAppend.c lstAtEnd.c lstAtFront.c lstClose.c lstConcat.c \
	lstDatum.c lstDeQueue.c lstDestroy.c lstDupl.c lstEnQueue.c \
	lstFind.c lstFindFrom.c lstFirst.c lstForEach.c lstForEachFrom.c \
	lstInit.c lstInsert.c lstIsAtEnd.c lstIsEmpty.c lstLast.c \
	lstMember.c lstNext.c lstOpen.c lstRemove.c lstReplace.c lstSucc.c
.PATH:	${.CURDIR}/lst.lib

NOSHARED?=	YES

CFLAGS+=-DMAKE_VERSION=\"5200205220\"

main.o: ${MAKEFILE}

.include <bsd.prog.mk>

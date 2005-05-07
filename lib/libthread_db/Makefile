# $DragonFly: src/lib/libthread_db/Makefile,v 1.1 2005/05/07 09:25:44 davidxu Exp $

.PATH:	${.CURDIR}/arch/${MACHINE_ARCH}

LIB=	thread_db
SHLIB_MAJOR= 1
SRCS=	thread_db.c
SRCS+=	libthread_xu.c
INCS=	thread_db.h
WARNS?= 1

CFLAGS+=-I. -I${.CURDIR} -I${.CURDIR}/../../libexec/rtld-elf \
	-I${.CURDIR}/../../libexec/rtld-elf/${MACHINE_ARCH}

.include <bsd.lib.mk>

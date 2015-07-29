# $FreeBSD: head/usr.sbin/crunch/crunchide/Makefile 232911 2012-03-13 05:21:14Z jmallett $

PROG=	crunchide
SRCS=	crunchide.c

.if ${MACHINE_ARCH} == "x86_64"
CFLAGS+=-DNLIST_ELF64
SRCS+=	exec_elf64.c
.endif

.include <bsd.prog.mk>

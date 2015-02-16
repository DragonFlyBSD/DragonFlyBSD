# $FreeBSD: head/usr.sbin/crunch/crunchide/Makefile 232911 2012-03-13 05:21:14Z jmallett $

PROG=   crunchide
SRCS=	crunchide.c

TARGET_ARCH?=	${MACHINE_ARCH}

.if ${TARGET_ARCH} == x86_64
CFLAGS+=-DNLIST_ELF64
SRCS+=	exec_elf64.c
exec_elf64.o: exec_elf32.c
.else
CFLAGS+=-DNLIST_ELF32
SRCS+=	exec_elf32.c
.endif

.include <bsd.prog.mk>

#	from @(#)Makefile.inc	8.1 (Berkeley) 6/4/93
# $FreeBSD: src/lib/libc/gmon/Makefile.inc,v 1.7.2.1 2001/04/25 10:04:05 ru Exp $
# $DragonFly: src/lib/libcr/gmon/Attic/Makefile.inc,v 1.3 2003/07/24 21:41:48 dillon Exp $

# gmon sources
.PATH: ${.CURDIR}/../libcr/gmon

SRCS+=	gmon.c mcount.c

.if ${LIB} == "c"
MAN+=	moncontrol.3

MLINKS+=moncontrol.3 monstartup.3
.endif

# mcount cannot be compiled with profiling
mcount.po: mcount.o
	cp mcount.o mcount.po

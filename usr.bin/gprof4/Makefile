# This was cloned from the Makefile for gprof by changing PROG from gprof
# to gprof4, adding NOMAN and PATH, adding -DGPROF4 to CFLAGS and deleting
# beforeinstall.

#	@(#)Makefile	5.17 (Berkeley) 5/11/90
# $DragonFly: src/usr.bin/gprof4/Makefile,v 1.2 2007/08/27 16:50:54 pavalos Exp $

PROG=	gprof4
NOMAN=	noman
SRCS=	gprof.c aout.c arcs.c dfn.c elf.c lookup.c ${MACHINE_ARCH}.c hertz.c \
	printgprof.c printlist.c
WARNS?=	0
CFLAGS+=-DGPROF4
.PATH:	${.CURDIR}/../../usr.bin/gprof

.include <bsd.prog.mk>

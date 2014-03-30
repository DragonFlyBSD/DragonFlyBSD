# $FreeBSD: head/usr.sbin/crunch/crunchgen/Makefile 80029 2001-07-20 06:20:32Z obrien $

PROG=	crunchgen
SRCS=	crunchgen.c crunched_skel.c
CLEANFILES+= crunched_skel.c

crunched_skel.c: crunched_main.c
	sh -e ${.CURDIR}/mkskel.sh ${.CURDIR}/crunched_main.c >crunched_skel.c

.include <bsd.prog.mk>

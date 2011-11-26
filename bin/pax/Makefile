#       @(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/bin/pax/Makefile,v 1.5.2.1 2001/08/01 05:03:11 obrien Exp $
# $DragonFly: src/bin/pax/Makefile,v 1.5 2006/09/27 19:18:00 pavalos Exp $

PROG=   pax
SRCS=	ar_io.c ar_subs.c buf_subs.c cache.c cpio.c file_subs.c ftree.c \
	gen_subs.c getoldopt.c options.c pat_rep.c pax.c sel_subs.c \
	tables.c tar.c tty_subs.c

WARNS?=	2
#MAN=	pax.1 tar.1 cpio.1
#LINKS=	${BINDIR}/pax ${BINDIR}/tar ${BINDIR}/pax ${BINDIR}/cpio

.include <bsd.prog.mk>

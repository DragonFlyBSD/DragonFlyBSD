#	@(#)Makefile	8.1 (Berkeley) 6/9/93
# $DragonFly: src/usr.bin/tset/Makefile,v 1.2 2007/08/27 16:51:00 pavalos Exp $

PROG=	tset
SRCS=	map.c misc.c set.c term.c tset.c wrterm.c

DPADD=	${LIBTERMCAP}
LDADD=	-ltermcap
LINKS=	${BINDIR}/tset ${BINDIR}/reset
MLINKS=	tset.1 reset.1

.include <bsd.prog.mk>

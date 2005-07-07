#	@(#)Makefile	8.1 (Berkeley) 6/8/93
# $FreeBSD: src/share/termcap/Makefile,v 1.12.2.5 2002/07/17 15:10:20 ru Exp $
# $DragonFly: src/share/termcap/Makefile,v 1.3 2005/07/07 11:49:56 corecode Exp $

# reorder gives an editor command for most common terminals
# (in reverse order from n'th to 1'st most commonly used)
# to move them to the front of termcap
#
MAN=	termcap.5

FILES=		map3270 termcap termcap.db
FILESDIR=	${BINDIR}/misc
CLEANFILES+=	termcap termcap.db

termcap: reorder termcap.src
	TERM=dumb TERMCAP=dumb: ex - ${.CURDIR}/termcap.src < ${.CURDIR}/reorder

termcap.db: termcap
	cap_mkdb termcap

etc-termcap:
	${LN} -fs ${BINDIR}/misc/termcap ${DESTDIR}/etc/termcap

.include <bsd.prog.mk>

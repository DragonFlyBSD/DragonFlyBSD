#	@(#)Makefile	8.1 (Berkeley) 6/8/93
# $FreeBSD: src/share/termcap/Makefile,v 1.12.2.5 2002/07/17 15:10:20 ru Exp $
# $DragonFly: src/share/termcap/Makefile,v 1.4 2006/12/29 22:43:32 corecode Exp $

MAN=	termcap.5

FILES=		map3270 termcap.src termcap.db
FILENAME_termcap.src=	termcap
FILESDIR=	${BINDIR}/misc
CLEANFILES+=	termcap.db

termcap.db: termcap.src
	cap_mkdb -f termcap ${.ALLSRC}

etc-termcap:
	${LN} -fs ${BINDIR}/misc/termcap ${DESTDIR}/etc/termcap

.include <bsd.prog.mk>

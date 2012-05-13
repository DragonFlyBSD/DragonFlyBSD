#	@(#)Makefile	8.1 (Berkeley) 6/8/93
# $FreeBSD: src/share/termcap/Makefile,v 1.12.2.5 2002/07/17 15:10:20 ru Exp $

MAN=	termcap.5

FILES=		termcap.src termcap.db
FILESNAME_termcap.src=	termcap
FILESDIR=	${BINDIR}/misc
CLEANFILES+=	termcap.db

termcap.db: termcap.src
	cap_mkdb -f termcap ${.ALLSRC}

etc-termcap:
	${LN} -fs ${BINDIR}/misc/termcap ${DESTDIR}/etc/termcap

.include <bsd.prog.mk>

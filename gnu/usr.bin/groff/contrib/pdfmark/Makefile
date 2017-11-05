# $DragonFly: src/gnu/usr.bin/groff/contrib/pdfmark/Makefile,v 1.2 2008/03/12 18:25:12 swildner Exp $

SCRIPTS=	pdfroff
MAN=		pdfroff.1

.if !defined(NO_SHARE)
TMACDIR?=	${SHAREDIR}/tmac

FILES=		pdfmark.tmac spdf.tmac
FILESDIR=	${TMACDIR}
.endif

CLEANFILES+=	${SCRIPTS} ${MAN}

.include <bsd.prog.mk>

# $FreeBSD: src/share/syscons/scrnmaps/Makefile,v 1.14.2.3 2002/08/07 16:31:52 ru Exp $

.include "./Makefile.scrnmaps"

NOMAN=	YES

FILES=		${SCRMAPS}
FILESDIR=	${SHAREDIR}/syscons/scrnmaps

CLEANFILES+=	${SCRMAPS}

${SCRMAPS}: ${.TARGET:R} mkscrfil.c
	${.OBJDIR}/../mapsmk/${.TARGET:R}.nx ${.TARGET:R}.tmp
	uuencode ${.TARGET:R}.tmp ${.TARGET:R} > ${.TARGET}
	rm -f ${.TARGET:R}.tmp

.include <bsd.prog.mk>

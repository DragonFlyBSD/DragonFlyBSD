# From:	@(#)Makefile	8.3 (Berkeley) 4/2/94
# $FreeBSD: src/usr.bin/passwd/Makefile,v 1.46 2004/12/29 08:16:39 ache Exp $

PROG=	passwd
BINOWN=	root
BINMODE= 4555
DPADD=	${LIBPAM}
LDADD=	${MINUSLPAM}
LDFLAGS+= ${LDFLAGSPAM}
.if !defined(NO_NIS)
LINKS=	${BINDIR}/passwd ${BINDIR}/yppasswd
MLINKS=	passwd.1 yppasswd.1
.endif

beforeinstall:
.for i in passwd yppasswd
	[ ! -e ${DESTDIR}${BINDIR}/$i ] || \
		chflags noschg ${DESTDIR}${BINDIR}/$i || true
.endfor

.if !defined(NOFSCHG)
afterinstall:
	-chflags schg ${DESTDIR}${BINDIR}/passwd
.endif

.include <bsd.prog.mk>

#	@(#)Makefile	0.1 (RWGrimes) 3/24/93
# $FreeBSD: src/usr.sbin/tcpdump/tcpslice/Makefile,v 1.10.6.1 2001/04/25 12:11:03 ru Exp $
# $DragonFly: src/usr.sbin/tcpdump/tcpslice/Makefile,v 1.4 2006/12/25 00:42:01 pavalos Exp $

PROG=	tcpslice
CFLAGS+= -I.
SRCS=	gwtm2secs.c search.c tcpslice.c util.c version.c version.h
CLEANFILES+=	version.c version.h
DPADD+=	${LIBPCAP}
LDADD+= -lpcap

TCPDUMP_DISTDIR?= ${.CURDIR}/../../../contrib/tcpdump-3.9

.ORDER: version.c version.h
version.c version.h: ${TCPDUMP_DISTDIR}/VERSION
	rm -f version.c ; \
	sed 's/.*/char version[] = "&";/' ${TCPDUMP_DISTDIR}/VERSION > version.c
	set `sed 's/\([0-9]*\)\.\([0-9]*\).*/\1 \2/' ${TCPDUMP_DISTDIR}/VERSION` ; \
		{ echo '#define VERSION_MAJOR' $$1 ; \
		  echo '#define VERSION_MINOR' $$2 ; } > version.h

.include <bsd.prog.mk>

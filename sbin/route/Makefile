#	@(#)Makefile	8.1 (Berkeley) 6/5/93
# $FreeBSD: src/sbin/route/Makefile,v 1.11.2.3 2002/12/04 14:05:42 ru Exp $

PROG=	route
MAN=	route.8
SRCS=	route.c show.c keywords.h
CFLAGS+=-I.
CFLAGS+=-DINET6
CLEANFILES+=keywords.h
NO_STRICT_ALIASING=

keywords.h: keywords
	LC_ALL=C awk '!/^#|^$$/ { \
		printf "#define\tK_%s\t%d\n\t{\"%s\", K_%s},\n", \
		    toupper($$1), ++L, $$1, toupper($$1); \
	}' < ${.CURDIR}/keywords > ${.TARGET} || (rm -f ${.TARGET}; false)

.include <bsd.prog.mk>

# $FreeBSD: src/kerberos5/lib/libvers/Makefile,v 1.8 2004/02/05 18:51:50 ru Exp $
# $DragonFly: src/kerberos5/lib/libvers/Makefile,v 1.4 2005/01/16 14:25:46 eirikn Exp $

LIB=	vers
INTERNALLIB=
SRCS=	print_version.c print_version.h
CFLAGS+=-I.

CLEANFILES= print_version.h

print_version.h:
	${MAKEPRINTVERSION} ${.TARGET}

.include <bsd.lib.mk>

.PATH: ${KRB5DIR}/lib/vers

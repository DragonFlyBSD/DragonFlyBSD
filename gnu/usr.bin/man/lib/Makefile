# $FreeBSD: src/gnu/usr.bin/man/lib/Makefile,v 1.11.6.1 2002/07/19 18:46:24 ru Exp $

LIB=	man
INTERNALLIB= true

SRCS=	gripes.c util.c

# Kludge to create config.h for other modules.  The library itself doesn't
# depend on config.h.
SRCS+=	config.h

.include <bsd.lib.mk>

# $FreeBSD: head/sys/boot/libstand32/Makefile 293341 2016-01-07 19:19:23Z bdrewery $

LIBSTAND_SRC= ${.CURDIR}/../../../lib/libstand
LIBSTAND_ARCH= i386
INTERNALLIB=
INCS=
LIB=	stand32
MAN=
.PATH:	${LIBSTAND_SRC}

WORLD_CCOPTLEVEL=	1	# Force -O1, -O2 implodes

.include "${LIBSTAND_SRC}/Makefile"

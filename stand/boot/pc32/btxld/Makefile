# $FreeBSD: src/usr.sbin/btxld/Makefile,v 1.3.2.1 2001/04/25 12:09:21 ru Exp $

PROG=	btxld
SRCS=	btxld.c elfh.c

WARNS?=	3

build-tools: btxld.nx

# this btxld is used in place, it is not installed anywhere
install:

# hint for pc32/Makefile.inc
__no_pc32_defines=

.include <bsd.hostprog.mk>

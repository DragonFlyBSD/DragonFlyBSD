# $FreeBSD: src/sbin/ipfw/Makefile,v 1.6.6.3 2002/07/24 03:21:23 luigi Exp $

PROG =	ipfw3
MAN =	ipfw3.8
SRCS =	ipfw3.c
SRCS += ipfw3basic.c ipfw3basic.h
SRCS += ipfw3set.c ipfw3set.h
SRCS += ipfw3log.c ipfw3log.h
SRCS += ipfw3sync.c ipfw3sync.h
SRCS += ipfw3state.c ipfw3state.h
SRCS += ipfw3table.c ipfw3table.h
SRCS += ipfw3dummynet.c ipfw3dummynet.h
SRCS += ipfw3nat.c ipfw3nat.h
WARNS?=	2

# DO NOT USE -static, dlopen() for ipfw3 modules
.undef NOSHARED

.include <bsd.prog.mk>

# $FreeBSD: src/sys/modules/streams/Makefile,v 1.4.2.1 2001/07/24 09:49:43 dd Exp $ 
# $DragonFly: src/sys/dev/misc/streams/Attic/Makefile,v 1.3 2003/08/15 08:32:29 dillon Exp $ 

MAINTAINER  =  newton@atdot.dotat.org
.if defined(DEBUG)
CFLAGS+= -DDEBUG_SVR4
.endif
CFLAGS+= -I${.CURDIR}/../../../emulation/svr4 -I${.OBJDIR}/../../../emulation/svr4

.PATH:  ${.CURDIR}/../../dev/streams
KMOD=   streams
SRCS=   streams.c opt_streams.h

NOMAN=

CFLAGS+=	-O

.include <bsd.kmod.mk>

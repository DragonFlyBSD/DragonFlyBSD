# $DragonFly: src/sbin/dhclient/includes/Attic/Makefile,v 1.1 2005/01/04 19:58:54 joerg Exp $

.include "${.CURDIR}/../Makefile.common"

CURSUBDIR=	includes/

SRCS=	dhcpd.h.patch osdep.h.patch

OBJS=	${SRCS:S/.patch//}

all: ${OBJS}

.include <bsd.prog.mk>

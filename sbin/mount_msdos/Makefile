#
# $FreeBSD: src/sbin/mount_msdos/Makefile,v 1.14.2.6 2002/08/07 16:31:48 ru Exp $
# $DragonFly: src/sbin/mount_msdos/Makefile,v 1.4 2006/10/17 00:55:42 pavalos Exp $
#

PROG=	mount_msdos
SRCS=	mount_msdos.c getmntopts.c
MAN=	mount_msdos.8
DPADD=	${LIBUTIL}
LDADD=	-lutil -lkiconv

MOUNT= ${.CURDIR}/../mount
CFLAGS= -I${MOUNT}
.PATH:	${MOUNT}

.include <bsd.prog.mk>

# $FreeBSD: src/usr.sbin/pccard/Makefile.inc,v 1.4.2.1 2001/04/25 11:47:22 ru Exp $
# $DragonFly: src/usr.sbin/pccard/Attic/Makefile.inc,v 1.2 2003/06/17 04:29:59 dillon Exp $

CFLAGS+=	-Wall
NOSHARED?=	YES

.include "../Makefile.inc"

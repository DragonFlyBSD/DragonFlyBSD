#	@(#)Makefile.inc	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/bin/Makefile.inc,v 1.11.2.1 2001/12/13 09:58:12 ru Exp $
# $DragonFly: src/bin/Makefile.inc,v 1.2 2003/06/17 04:22:38 dillon Exp $

BINDIR?=	/bin
CFLAGS+=	-Wall -Wformat
NOSHARED?=	YES

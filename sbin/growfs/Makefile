#	@(#)Makefile	8.8 (Berkeley) 6/21/2000
#
# $TSHeader: src/sbin/growfs/Makefile,v 1.4 2000/12/05 19:45:24 tomsoft Exp $
# $FreeBSD: src/sbin/growfs/Makefile,v 1.3.2.1 2001/07/16 15:02:13 tomsoft Exp $
# $DragonFly: src/sbin/growfs/Makefile,v 1.2 2003/06/17 04:27:33 dillon Exp $
#

MAINTAINER=	tomsoft@FreeBSD.ORG, chm@FreeBSD.ORG

#GFSDBG=YES  
#CFLAGS+=${BDECFLAGS}

PROG=   growfs
SRCS=   growfs.c
MAN=	growfs.8

.if defined(GFSDBG)
SRCS+=  debug.c
CFLAGS+=-DFS_DEBUG
.endif  

.include <bsd.prog.mk>      

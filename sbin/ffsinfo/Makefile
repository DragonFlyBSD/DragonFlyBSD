# @(#)Makefile    8.8 (Berkeley) 6/21/2000
#
# $TSHeader: src/sbin/ffsinfo/Makefile,v 1.3 2000/12/05 19:45:10 tomsoft Exp $
# $FreeBSD: src/sbin/ffsinfo/Makefile,v 1.3.2.1 2001/07/16 15:01:56 tomsoft Exp $
# $DragonFly: src/sbin/ffsinfo/Makefile,v 1.5 2006/10/17 00:55:40 pavalos Exp $
#

#CFLAGS+=${BDECFLAGS}
 
PROG=   ffsinfo
SRCS=   ffsinfo.c debug.c
MAN=   ffsinfo.8
NO_STRICT_ALIASING=
 
GROWFS= ${.CURDIR}/../growfs
CFLAGS+=-DFS_DEBUG -I${GROWFS}
.PATH:  ${GROWFS}
 
.include <bsd.prog.mk>               

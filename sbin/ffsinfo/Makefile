# @(#)Makefile    8.8 (Berkeley) 6/21/2000
#
# $TSHeader: src/sbin/ffsinfo/Makefile,v 1.3 2000/12/05 19:45:10 tomsoft Exp $
# $FreeBSD: src/sbin/ffsinfo/Makefile,v 1.3.2.1 2001/07/16 15:01:56 tomsoft Exp $
# $DragonFly: src/sbin/ffsinfo/Makefile,v 1.4 2005/01/14 06:02:45 cpressey Exp $
#

#CFLAGS+=${BDECFLAGS}
 
PROG=   ffsinfo
WARNS?=	6
SRCS=   ffsinfo.c debug.c
MAN=   ffsinfo.8
 
GROWFS= ${.CURDIR}/../growfs
CFLAGS+=-DFS_DEBUG -I${GROWFS}
.PATH:  ${GROWFS}
 
.include <bsd.prog.mk>               

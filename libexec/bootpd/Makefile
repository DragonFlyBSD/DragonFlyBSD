# bootpd/Makefile
# $FreeBSD: src/libexec/bootpd/Makefile,v 1.8.2.1 2001/04/25 10:40:49 ru Exp $

PROG=	bootpd
SRCS=	bootpd.c dovend.c readfile.c hash.c dumptab.c \
	lookup.c getif.c hwaddr.c report.c tzone.c rtmsg.c
MAN=	bootptab.5 bootpd.8
MLINKS=	bootpd.8 bootpgw.8
WARNS?=	2

CFLAGS+= -DETC_ETHERS
CFLAGS+= -DSYSLOG -DDEBUG -DVEND_CMU

SUBDIR= bootpgw tools

.include <bsd.prog.mk>

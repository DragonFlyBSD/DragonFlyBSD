# $FreeBSD: src/usr.bin/ftp/Makefile,v 1.11.2.1 2000/05/16 04:25:19 cpiazza Exp $
#	$NetBSD: Makefile,v 1.15 1997/10/18 15:31:20 lukem Exp $
#	from: @(#)Makefile	8.2 (Berkeley) 4/3/94

# Uncomment the following to provide defaults for gate-ftp operation
#
#CFLAGS+=-DGATE_SERVER=\"ftp-gw.host\" # -DGATE_PORT=21

CONTRIBDIR= ${.CURDIR}/../../contrib/tnftp
.PATH:	${CONTRIBDIR}/src

PROG=	ftp
SRCS=	cmds.c cmdtab.c complete.c domacro.c fetch.c ftp.c main.c \
	progressbar.c ruserpass.c util.c

CFLAGS+=-I${.CURDIR} -I${.OBJDIR} -I${CONTRIBDIR} \
	-I${.CURDIR}/../../lib/libc/stdtime \
	-D__dead=__dead2 -DUSE_GLOB_H
LDADD=	-lprivate_edit -lprivate_ncurses -lutil
DPADD=	${LIBEDIT} ${LIBNCURSES} ${LIBUTIL}
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.if !defined(NO_CRYPT)
SRCS+=		ssl.c
CFLAGS+=	-DWITH_SSL -I${.CURDIR}/../../crypto/libressl/include
LDADD+=		-lprivate_ssl -lprivate_crypto
DPADD+=		${LIBRESSL} ${LIBRECRYPTO}
.endif

LINKS=	${BINDIR}/ftp ${BINDIR}/pftp \
	${BINDIR}/ftp ${BINDIR}/gate-ftp
MLINKS=	ftp.1 pftp.1 \
	ftp.1 gate-ftp.1

WARNS?=	3

.include <bsd.prog.mk>

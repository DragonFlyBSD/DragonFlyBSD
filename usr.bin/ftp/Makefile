# $FreeBSD: src/usr.bin/ftp/Makefile,v 1.11.2.1 2000/05/16 04:25:19 cpiazza Exp $
# $DragonFly: src/usr.bin/ftp/Makefile,v 1.5 2008/05/17 22:01:12 pavalos Exp $
#	$NetBSD: Makefile,v 1.15 1997/10/18 15:31:20 lukem Exp $
#	from: @(#)Makefile	8.2 (Berkeley) 4/3/94

# Uncomment the following to provide defaults for gate-ftp operation
#
#CFLAGS+=-DGATE_SERVER=\"ftp-gw.host\" # -DGATE_PORT=21

CONTRIBDIR= ${.CURDIR}/../../contrib/tnftp
.PATH:	${CONTRIBDIR}

PROG=	ftp
SRCS=	cmds.c cmdtab.c complete.c domacro.c fetch.c ftp.c main.c \
	progressbar.c ruserpass.c util.c

CFLAGS+=-DINET6 -I${.OBJDIR} -I${CONTRIBDIR} -I${.CURDIR}/../../lib/libc/stdtime
LDADD+=	-ledit -ltermcap -lutil
DPADD+=	${LIBEDIT} ${LIBTERMCAP} ${LIBUTIL}

LINKS=	${BINDIR}/ftp ${BINDIR}/pftp \
	${BINDIR}/ftp ${BINDIR}/gate-ftp
MLINKS=	ftp.1 pftp.1 \
	ftp.1 gate-ftp.1

WARNS?=	3

.include <bsd.prog.mk>

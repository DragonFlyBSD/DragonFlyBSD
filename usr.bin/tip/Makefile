#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/tip/tip/Makefile,v 1.10.6.3 2002/07/17 13:25:19 ru Exp $
#
# Files are:
#	/etc/remote		remote host description file
#	/etc/phones		phone number file, owned by ${OWNER} and
#				mode 6??
#	/var/log/aculog		ACU accounting file, owned by ${OWNER} and
#				mode 6?? {if ACULOG defined}

PROG=	tip
DPADD=	${LIBUTIL}
LDADD=	-lutil
LINKS=  ${BINDIR}/tip ${BINDIR}/cu
MLINKS= tip.1 cu.1
MAN=	tip.1 modems.5
SRCS=	acu.c acucommon.c cmds.c cmdtab.c cu.c hunt.c log.c partab.c \
	remote.c tip.c tipout.c tod.c unidialer.c value.c vars.c

WARNS?=	2

BINDIR?=	/usr/bin
BINOWN=		uucp
BINGRP=		dialer

# XXX: there is some concern that `tip' in its current state shouldn't run
#      SUID.  If it believed it should, the mode above may still no be proper.
#BINMODE?=	4510

.include <bsd.prog.mk>

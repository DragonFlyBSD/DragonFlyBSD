# $FreeBSD: src/tools/tools/ath/athprom/Makefile,v 1.1 2008/12/07 19:17:33 sam Exp $

PROG=	athprom

.include <../Makefile.inc>

TEMPLATEDIR=	/usr/local/libdata/athprom
TEXTMODE?=	444

CFLAGS+=-DDIR_TEMPLATE='"${TEMPLATEDIR}"'

beforeinstall:
	mkdir -p ${DESTDIR}${TEMPLATEDIR}
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m ${TEXTMODE} \
	    ${.CURDIR}/eeprom-* ${DESTDIR}${TEMPLATEDIR}/

.include <bsd.prog.mk>

# $FreeBSD: src/share/zoneinfo/Makefile,v 1.16.2.3 2002/08/07 16:31:52 ru Exp $

CLEANFILES+=	yearistype

.if defined(LEAPSECONDS)
LEAPFILE=	-L leapseconds
.else
LEAPFILE=
.endif

TZFILES=	africa antarctica asia australasia etcetera europe \
		northamerica southamerica systemv
TZFILES+=	dragonfly
POSIXRULES=	America/New_York

.if defined(OLDTIMEZONES)
TZFILES+=	backward
.endif

.if exists(${.OBJDIR}/yearistype)
YEARISTYPE=	${.OBJDIR}/yearistype
.else
YEARISTYPE=	${.CURDIR}/yearistype
.endif

FILES=			iso3166.tab zone.tab
FILESDIR_iso3166.tab=	${SHAREDIR}/misc
FILESNAME_iso3166.tab=	iso3166
FILESDIR_zone.tab=	${SHAREDIR}/zoneinfo

all: yearistype

yearistype: yearistype.sh
	cp ${.ALLSRC} ${.TARGET}
	chmod +x ${.TARGET}

beforeinstall:
	umask 022; cd ${.CURDIR}; \
	zic -D -d ${DESTDIR}/usr/share/zoneinfo -p ${POSIXRULES} \
	    -u ${BINOWN} -g ${BINGRP} \
	    ${LEAPFILE} -y ${YEARISTYPE} ${TZFILES}

.include <bsd.prog.mk>

# $FreeBSD: src/share/zoneinfo/Makefile,v 1.16.2.3 2002/08/07 16:31:52 ru Exp $

CLEANFILES+=	yearistype

.if defined(LEAPSECONDS)
LEAPFILE=	-L leapseconds
.else
LEAPFILE=
.endif

ZIC_UG_FLAGS=	-u ${BINOWN} -g ${BINGRP}

TZFILES=	africa antarctica asia australasia etcetera europe \
		northamerica southamerica
TZFILES+=	backward
POSIXRULES=	America/New_York

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
	    ${ZIC_UG_FLAGS} ${LEAPFILE} -y ${YEARISTYPE} ${TZFILES}

afterinstall:
#
# If the file /var/db/zoneinfo exists, and it is owned by root:wheel,
# and the contents of it exists in /usr/share/zoneinfo, then reinstall
# it.
#
	@-if [ -f ${DESTDIR}/var/db/zoneinfo -a -O ${DESTDIR}/var/db/zoneinfo \
	    -a -G ${DESTDIR}/var/db/zoneinfo ]; then \
		zf=$$(cat ${DESTDIR}/var/db/zoneinfo); \
		if [ -f ${DESTDIR}/usr/share/zoneinfo/$${zf} ]; then \
			if [ ! -z "${DESTDIR}" ]; then \
				optC="-C ${DESTDIR}"; \
			fi; \
			echo "Updating /etc/localtime"; \
			tzsetup $${optC} -r; \
		fi; \
	else \
		echo "Run tzsetup(8) manually to update /etc/localtime."; \
	fi

.include <bsd.prog.mk>

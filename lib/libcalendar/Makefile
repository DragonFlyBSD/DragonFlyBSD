# $FreeBSD: src/lib/libcalendar/Makefile,v 1.4.2.1 2001/04/25 10:04:20 ru Exp $
# $DragonFly: src/lib/libcalendar/Makefile,v 1.3 2008/10/28 16:54:09 swildner Exp $

LIB=	calendar

SRCS=	calendar.c easter.c
INCS=	calendar.h

WARNS?=	6

MAN=	calendar.3

MLINKS=	calendar.3 easterg.3 calendar.3 easterog.3 calendar.3 easteroj.3 \
	calendar.3 gdate.3 calendar.3 jdate.3 \
	calendar.3 ndaysg.3 calendar.3 ndaysj.3 \
	calendar.3 week.3 calendar.3 weekday.3

CFLAGS+=-I. -I${.CURDIR}

.include <bsd.lib.mk>

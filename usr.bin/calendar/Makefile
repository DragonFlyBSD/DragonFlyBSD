#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: head/usr.bin/calendar/Makefile 334102 2018-05-23 16:28:31Z brd $

PROG=		calendar
MAN=		calendar.1
SRCS=		basics.c calendar.c chinese.c dates.c days.c \
		ecclesiastical.c gregorian.c io.c julian.c \
		moon.c nnames.c parsedata.c sun.c utils.c

DPADD=		${LIBM}
LDADD=		-lm

CALENDAR_ETCDIR=/etc/calendar
CALENDAR_DIR=	/usr/share/calendar
CFLAGS+=	-DCALENDAR_ETCDIR='"${CALENDAR_ETCDIR}"' \
		-DCALENDAR_DIR='"${CALENDAR_DIR}"'

SED_EXPR=	-e 's|@@CALENDAR_ETCDIR@@|${CALENDAR_ETCDIR}|' \
		-e 's|@@CALENDAR_DIR@@|${CALENDAR_DIR}|'

CLEANFILES+=${MAN}
${MAN}: ${MAN}.in
	sed ${SED_EXPR} ${.ALLSRC} > ${.TARGET}

.if !defined(NO_SHARE)
INTER=		de_AT.UTF-8 \
		de_DE.UTF-8 \
		fr_FR.UTF-8 \
		hr_HR.UTF-8 \
		hu_HU.UTF-8 \
		pt_BR.UTF-8 \
		ru_RU.UTF-8 \
		uk_UA.UTF-8 \
		zh_Hans_CN.UTF-8 \
		zh_Hant_TW.UTF-8

FILESGROUPS+=	CALS
CALS=		calendars/calendar.all \
		calendars/calendar.australia \
		calendars/calendar.birthday \
		calendars/calendar.brazilian \
		calendars/calendar.canada \
		calendars/calendar.chinese \
		calendars/calendar.christian \
		calendars/calendar.computer \
		calendars/calendar.croatian \
		calendars/calendar.discord \
		calendars/calendar.dragonfly \
		calendars/calendar.dutch \
		calendars/calendar.fictional \
		calendars/calendar.french \
		calendars/calendar.german \
		calendars/calendar.history \
		calendars/calendar.holiday \
		calendars/calendar.hungarian \
		calendars/calendar.judaic \
		calendars/calendar.misc \
		calendars/calendar.music \
		calendars/calendar.newzealand \
		calendars/calendar.orthodox \
		calendars/calendar.russian \
		calendars/calendar.southafrica \
		calendars/calendar.space \
		calendars/calendar.uk \
		calendars/calendar.ukrainian \
		calendars/calendar.ushistory \
		calendars/calendar.usholiday \
		calendars/calendar.world
CALSDIR=	${SHAREDIR}/calendar

CAL_de_AT.UTF-8=calendar.feiertag

CAL_de_DE.UTF-8=calendar.all \
		calendar.feiertag \
		calendar.geschichte \
		calendar.kirche \
		calendar.literatur \
		calendar.musik \
		calendar.wissenschaft

CAL_fr_FR.UTF-8=calendar.all \
		calendar.fetes \
		calendar.jferies \
		calendar.proverbes

CAL_hr_HR.UTF-8=calendar.all \
		calendar.praznici

CAL_hu_HU.UTF-8=calendar.all \
		calendar.nevnapok \
		calendar.unnepek

CAL_pt_BR.UTF-8=calendar.all \
		calendar.commemorative \
		calendar.holidays \
		calendar.mcommemorative

CAL_ru_RU.UTF-8=calendar.all \
		calendar.common \
		calendar.history \
		calendar.orthodox \
		calendar.pagan \
		calendar.primety

CAL_uk_UA.UTF-8=calendar.all \
		calendar.holiday \
		calendar.misc \
		calendar.orthodox

CAL_zh_Hans_CN.UTF-8=\
		calendar.all \
		calendar.chinese \
		calendar.gregorian

CAL_zh_Hant_TW.UTF-8=\
		calendar.all \
		calendar.chinese \
		calendar.gregorian

.for lang in ${INTER}
FILESGROUPS+=	CALS_${lang}
CALS_${lang}DIR=${SHAREDIR}/calendar/${lang}
.  for file in ${CAL_${lang}}
CALS_${lang}+=	${file:S@^@calendars/${lang}/@}
.  endfor
.endfor

.endif  # !defined(NO_SHARE)

.include <bsd.prog.mk>

#	from @(#)Makefile.inc	8.1 (Berkeley) 6/4/93
# $FreeBSD: src/lib/libc/locale/Makefile.inc,v 1.16.2.5 2002/10/24 11:00:52 tjr Exp $
# $DragonFly: src/lib/libc/locale/Makefile.inc,v 1.6 2005/03/13 19:18:29 joerg Exp $

# locale sources
.PATH: ${.CURDIR}/../libc/${MACHINE_ARCH}/locale ${.CURDIR}/../libc/locale

SRCS+=	ansi.c big5.c collate.c collcmp.c euc.c fix_grouping.c frune.c \
	gbk.c gb18030.c isctype.c \
	ldpart.c lmessages.c lmonetary.c lnumeric.c localeconv.c mbrune.c \
        mskanji.c nl_langinfo.c nomacros.c none.c rune.c \
	runetype.c setinvalidrune.c setlocale.c setrunelocale.c table.c \
	tolower.c toupper.c utf2.c utf8.c

.if ${LIB} == "c"
MAN+=	mbrune.3 multibyte.3 nl_langinfo.3 rune.3 setlocale.3

MLINKS+=mbrune.3 mbmb.3 mbrune.3 mbrrune.3
MLINKS+=multibyte.3 mblen.3 multibyte.3 mbstowcs.3 multibyte.3 mbtowc.3 \
	multibyte.3 wcstombs.3 multibyte.3 wctomb.3
MLINKS+=rune.3 fgetrune.3 rune.3 fputrune.3 rune.3 fungetrune.3 \
	rune.3 setinvalidrune.3 rune.3 setrunelocale.3 rune.3 sgetrune.3 \
	rune.3 sputrune.3
MLINKS+=setlocale.3 localeconv.3
.endif

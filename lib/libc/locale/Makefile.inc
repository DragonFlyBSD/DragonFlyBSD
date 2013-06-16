# $DragonFly: src/lib/libc/locale/Makefile.inc,v 1.13 2008/05/31 04:51:55 swildner Exp $

.PATH: ${.CURDIR}/../libc/locale

CMAPS+=	${.CURDIR}/locale/Symbol.map

SRCS+=	collate.c collcmp.c
SRCS+=	_def_messages.c _def_monetary.c _def_numeric.c _def_time.c
SRCS+=	localeconv.c nl_langinfo.c setlocale.c __mb_cur_max.c
SRCS+=	wcsftime.c wcstod.c wcstof.c wcstol.c wcstold.c
SRCS+=	wcstoll.c wcstoimax.c wcstoul.c
SRCS+=	wcstoull.c wcstoumax.c aliasname.c
SRCS+=	___runetype_mb.c _wctrans.c iswctype.c multibyte_c90.c
SRCS+=	multibyte_amd1.c rune.c runeglue.c runetable.c setrunelocale.c

CFLAGS+=	-I${.CURDIR}/../libc/locale

.if ${LIB} == "c"
MAN+=	nl_langinfo.3 setlocale.3
MAN+=	btowc.3 iswalnum.3 iswctype.3 towlower.3 towctrans.3 mblen.3 mbrlen.3
MAN+=	mbrtowc.3 mbsinit.3 mbsrtowcs.3 mbstowcs.3 mbtowc.3 wcrtomb.3
MAN+=	wcsftime.3 wcstod.3 wcstol.3 wcsrtombs.3 wcstombs.3
MAN+=	wctob.3 wctomb.3 wctrans.3 wctype.3 wcwidth.3

MLINKS+=iswalnum.3 iswalpha.3 iswalnum.3 iswblank.3
MLINKS+=iswalnum.3 iswcntrl.3 iswalnum.3 iswdigit.3
MLINKS+=iswalnum.3 iswgraph.3 iswalnum.3 iswlower.3
MLINKS+=iswalnum.3 iswprint.3 iswalnum.3 iswpunct.3
MLINKS+=iswalnum.3 iswspace.3 iswalnum.3 iswupper.3
MLINKS+=iswalnum.3 iswxdigit.3 towlower.3 towupper.3
MLINKS+=setlocale.3 localeconv.3
MLINKS+=wcstod.3 wcstof.3 wcstod.3 wcstold.3
MLINKS+=wcstol.3 wcstoul.3 wcstol.3 wcstoll.3 wcstol.3 wcstoull.3
MLINKS+=wcstol.3 wcstoimax.3 wcstol.3 wcstoumax.3
.endif

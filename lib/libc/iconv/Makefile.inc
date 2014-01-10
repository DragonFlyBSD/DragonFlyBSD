.PATH: ${.CURDIR}/../libc/iconv

CMAPS+=	${.CURDIR}/iconv/Symbol.map

SRCS+=	iconv.c

.if ${LIB} == "c"
MAN+=	iconv.3 iconvctl.3 iconv_canonicalize.3 iconvlist.3 __iconv_get_list.3
MLINKS+=iconv.3 iconvctl.3
MLINKS+=iconv.3 iconv_open.3
MLINKS+=iconv.3 iconv_open_into.3
MLINKS+=iconv.3 iconv_close.3
MLINKS+=iconv.3 __iconv.3
MLINKS+=__iconv_get_list.3 __iconv_free_list.3
.endif

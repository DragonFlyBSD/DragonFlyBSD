LIB=evtr
SHLIB_MAJOR=1

SRCS=evtr.c ktrfmt.yy.c ktrfmt.tab.c
INCS=evtr.h
WARNS?=	2
NO_STRICT_ALIASING=

#ktrfmt.yy.c: ktrfmt.l ktrfmt.tab.h
#	flex ktrfmt.l

#ktrfmt.tab.c: ktrfmt.y
#	bison -d ktrfmt.y

.include <bsd.lib.mk>

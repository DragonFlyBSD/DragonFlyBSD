LIB=evtr
SHLIB_MAJOR=1

SRCS=evtr.c ktrfmt.yy.c ktrfmt.tab.c
INCS=evtr.h

CFLAGS+= -W -Wall

#ktrfmt.yy.c: ktrfmt.l ktrfmt.tab.h
#	flex ktrfmt.l

#ktrfmt.tab.c: ktrfmt.y
#	bison -d ktrfmt.y

.include <bsd.lib.mk>

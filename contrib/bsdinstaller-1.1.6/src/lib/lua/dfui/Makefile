# Makefile for luadfui.
# $Id: Makefile,v 1.6 2005/04/05 20:53:46 cpressey Exp $

OSTYPE!=sysctl -n kern.ostype

COMPAT?=/usr/local/lib/lua/compat-5.1r2

LIB=	ldfui.so
OBJS=	common.o dfui.o progress.o compat-5.1.o

WARNS=	-W -Werror -Wall -Wstrict-prototypes -Wmissing-prototypes \
	-Wpointer-arith	-Wno-uninitialized -Wreturn-type -Wcast-qual \
	-Wwrite-strings -Wswitch -Wshadow -Wcast-align -Wchar-subscripts \
	-Winline -Wnested-externs -Wredundant-decls

CFLAGS+=${WARNS}
.ifdef DEBUG
CFLAGS+= -DDEBUG -g 
.endif
CFLAGS+=-I/usr/local/include -I${COMPAT}

.if ${OSTYPE} == "NetBSD"
CFLAGS+=-I/usr/pkg/include
LDADD+= -L/usr/pkg/lib
.endif

LDADD+=	-L/usr/local/lib/ -ldfui -laura -lm -llua50 -llualib50

all: ${LIB}

# XXX build and link to dependencies here
in_cvs: all

${LIB}: ${OBJS}
	${CC} ${CFLAGS} ${OBJS} -static ${LDADD} -shared -o ${LIB}

compat-5.1.o: ${COMPAT}/compat-5.1.c
	${CC} ${CFLAGS} -c ${COMPAT}/compat-5.1.c

clean:
	rm -f *.o *.so

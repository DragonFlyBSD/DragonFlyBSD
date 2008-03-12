# Makefile for libaura.
# $Id: Makefile,v 1.11 2005/04/05 19:40:47 cpressey Exp $

OSTYPE!=sysctl -n kern.ostype

LIB=	aura
LIB_VER=3
ANAME=	lib${LIB}.a
SONAME=	lib${LIB}.so.${LIB_VER}
SOLINK=	lib${LIB}.so

OBJS=	mem.o popen.o fspred.o \
	dict.o buffer.o

WARNS=	-W -Werror -Wall -Wstrict-prototypes -Wmissing-prototypes \
	-Wpointer-arith	-Wno-uninitialized -Wreturn-type -Wcast-qual \
	-Wwrite-strings -Wswitch -Wshadow -Wcast-align -Wchar-subscripts \
	-Winline -Wnested-externs -Wredundant-decls

CFLAGS+=${WARNS} -I/usr/local/include
.ifdef DEBUG
CFLAGS+=-DDEBUG -g
.endif

LDADD=	-L/usr/local/lib/

.if ${OSTYPE} == "NetBSD"
CFLAGS+= -I/usr/pkg/include
LDADD+= -L/usr/pkg/lib
.endif

all: ${SONAME} ${ANAME}

in_cvs: all

${SONAME}: ${OBJS}
	${CC} ${CFLAGS} ${OBJS} -static ${LDADD} -shared -o ${SONAME}
	ln -sf ${SONAME} ${SOLINK}

${ANAME}: ${OBJS}
	ar rc ${ANAME} ${OBJS}
	ranlib ${ANAME}

clean:
	rm -rf *.o *.so *.so.? *.So *.a

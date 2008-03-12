# $Id: Makefile,v 1.4 2005/04/05 20:53:47 cpressey Exp $

OSTYPE!=sysctl -n kern.ostype

CC?=	cc
WARNS=	-W -Werror -Wall -Wstrict-prototypes -Wmissing-prototypes \
	-Wpointer-arith	-Wno-uninitialized -Wreturn-type -Wcast-qual \
	-Wwrite-strings -Wswitch -Wshadow -Wcast-align -Wchar-subscripts \
	-Winline -Wnested-externs -Wredundant-decls
LOCALBASE?=/usr/local

CFLAGS+=${WARNS}
.ifdef DEBUG
CFLAGS+= -DDEBUG -g 
.endif
CFLAGS+=-I/usr/local/include

.if ${OSTYPE} == "NetBSD"
CFLAGS+=-I/usr/pkg/include
LDADD+= -L/usr/pkg/lib
.endif

LDADD+=	-L/usr/local/lib/ -lutil -lm -llua50 -llualib50

all: lpty.so

in_cvs: all

lpty.so: pty.o
	${CC} ${CFLAGS} pty.o -static ${LDADD} -shared -o lpty.so

pty.o: pty.c
	${CC} ${CFLAGS} -c pty.c -o pty.o

clean:
	rm -rf *.so *.o

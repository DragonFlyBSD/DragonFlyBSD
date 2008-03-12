# Makefile for dfuife_test
# $Id: Makefile,v 1.8 2005/04/05 20:53:46 cpressey Exp $

OSTYPE!=sysctl -n kern.ostype

PROG=	dfuife_test
OBJS=	fe_test.o

WARNS=	-W -Werror -Wall -Wstrict-prototypes -Wmissing-prototypes \
	-Wpointer-arith	-Wno-uninitialized -Wreturn-type -Wcast-qual \
	-Wwrite-strings -Wswitch -Wshadow -Wcast-align -Wchar-subscripts \
	-Winline -Wnested-externs -Wredundant-decls

CFLAGS+=${WARNS} -I/usr/local/include
LDADD=	-L/usr/local/lib/ -ldfui -laura

.if ${OSTYPE} == "NetBSD"
CFLAGS+= -I/usr/pkg/include
LDADD+= -L/usr/pkg/lib
.endif

all: ${PROG}

# XXX build and set up symlinks to dependencies here
in_cvs: all

${PROG}: ${OBJS}
	${CC} ${CFLAGS} ${OBJS} ${LDADD} -o ${PROG}

clean:
	rm -rf ${PROG} *.o

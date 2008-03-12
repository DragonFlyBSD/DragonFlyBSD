# Makefile for libdfui.
# $Id: Makefile,v 1.18 2005/04/05 19:40:47 cpressey Exp $

OSTYPE!=sysctl -n kern.ostype

LIB=	dfui
LIB_VER=4
ANAME=	lib${LIB}.a
SONAME=	lib${LIB}.so.${LIB_VER}
SOLINK=	lib${LIB}.so

OBJS=	encode.o decode.o conn_tcp.o \
	connection.o conn_caps.o conn_npipe.o \
	form.o \
	dump.o system.o lang.o

WARNS=	-W -Werror -Wall -Wstrict-prototypes -Wmissing-prototypes \
	-Wpointer-arith	-Wno-uninitialized -Wreturn-type -Wcast-qual \
	-Wwrite-strings -Wswitch -Wshadow -Wcast-align -Wchar-subscripts \
	-Winline -Wnested-externs -Wredundant-decls

CFLAGS+=${WARNS} -I/usr/local/include
CFLAGS+=-I/usr/src/lib -I/usr/src/lib/libcaps/${MACHINE_ARCH} -DSMP
.ifdef DEBUG
CFLAGS+=-DDEBUG -g
.endif

LDADD=	-L/usr/local/lib/ -laura

.if ${OSTYPE} == "NetBSD"
CFLAGS+= -I/usr/pkg/include
LDADD+= -L/usr/pkg/lib
.endif

# Default target:

all: ${SONAME} ${ANAME}

# To build this straight from a checkout of the CVS tree, *without* first
# installing the support libraries, do a 'make in_cvs'.  This causes
# search paths and symbolic links to be set up so that the build process
# can find the needed header files and libraries.

in_cvs:
	cd ../libaura && ${MAKE} in_cvs
	ln -sf ../libaura aura
	${MAKE} -DIN_CVS all

.if defined(IN_CVS)
CFLAGS+=-I.
LDADD+=	-L../libaura
.endif

# Main targets, shared and static:

${SONAME}: ${OBJS}
	${CC} ${CFLAGS} ${OBJS} -static ${LDADD} -shared -o ${SONAME}
	ln -sf ${SONAME} ${SOLINK}

${ANAME}: ${OBJS}
	ar rc ${ANAME} ${OBJS}
	ranlib ${ANAME}

clean:
	rm -f aura
	rm -rf *.o *.so *.so.? *.So *.a

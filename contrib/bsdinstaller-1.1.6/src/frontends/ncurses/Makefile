# Makefile for dfuife_curses.
# $Id: Makefile,v 1.14 2005/04/05 19:40:47 cpressey Exp $

OSTYPE!=sysctl -n kern.ostype

PROG=	dfuife_curses
OBJS=	curses_util.o \
	curses_form.o curses_widget.o curses_bar.o \
	curses_xlat.o \
	main.o

WARNS=	-W -Werror -Wall -Wstrict-prototypes -Wmissing-prototypes \
	-Wpointer-arith	-Wno-uninitialized -Wreturn-type -Wcast-qual \
	-Wwrite-strings -Wswitch -Wshadow -Wcast-align -Wchar-subscripts \
	-Winline -Wnested-externs -Wredundant-decls

CFLAGS+=${WARNS} -I/usr/local/include
.ifdef DEBUG
CFLAGS+=-g -DDEBUG
.endif
LDADD+=	-L/usr/local/lib/ -ldfui -laura -lpanel -lcurses

.if ${OSTYPE} == "NetBSD"
CFLAGS+= -I/usr/pkg/include
LDADD+= -L/usr/pkg/lib
.endif

.if defined(WITH_NLS)
# Localization (gettext)
LOCALEDIR=	/usr/local/share/locale
PACKAGE=	${PROG}
CFLAGS+=	-DLOCALEDIR=\"${LOCALEDIR}\"
CFLAGS+=	-DPACKAGE=\"${PACKAGE}\"
CFLAGS+=	-DENABLE_NLS
LDADD+=		-lintl
LANGUAGES=	ru
.endif

# Default target.

all: ${PROG} mo

# Build directlty in CVS tree.

in_cvs:
	cd ../../lib/libaura && ${MAKE} in_cvs
	ln -sf ../../lib/libaura aura
	cd ../../lib/libdfui && ${MAKE} in_cvs
	ln -sf ../../lib/libdfui dfui
	${MAKE} -DIN_CVS all

.if defined(IN_CVS)
CFLAGS+=-I.
LDADD+=	-L../../lib/libaura -L../../lib/libdfui
.endif

# Main target.

${PROG}: ${OBJS}
	${CC} ${CFLAGS} ${OBJS} ${LDADD} -o ${PROG}

mo:
	for _language in ${LANGUAGES}; do \
		msgfmt -o po/$${_language}.mo po/$${_language}; \
	done

pot:
	xgettext -s --no-location --keyword=_ -o po/${PROG}.pot *.c

clean:
	rm -f aura dfui
	rm -rf ${PROG} *.o po/*.mo

# In-CVS-tree testing.

test: in_cvs
	env LD_LIBRARY_PATH="../../lib/libaura:../../lib/libdfui" ./${PROG}

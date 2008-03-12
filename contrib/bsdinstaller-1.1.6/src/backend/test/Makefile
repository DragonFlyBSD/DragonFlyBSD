# Makefile for dfuibe_test.
# $Id: Makefile,v 1.15 2005/04/05 19:40:46 cpressey Exp $

OSTYPE!=sysctl -n kern.ostype

PROG=	dfuibe_test
OBJS=	be_test.o

WARNS=	-W -Werror -Wall -Wstrict-prototypes -Wmissing-prototypes \
	-Wpointer-arith	-Wno-uninitialized -Wreturn-type -Wcast-qual \
	-Wwrite-strings -Wswitch -Wshadow -Wcast-align -Wchar-subscripts \
	-Winline -Wnested-externs -Wredundant-decls

CFLAGS+=${WARNS} -I/usr/local/include
.ifdef DEBUG
CFLAGS+=-g -DDEBUG
.endif
LDADD+=	-L/usr/local/lib/ -ldfui -laura

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

# Build directly from CVS checkout.

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

# XXX This is here only because there is no port for this.
install: mo
	for _language in ${LANGUAGES}; do \
		${INSTALL} -c -o root -g wheel -m 444 po/$${_language}.mo \
		    ${LOCALEDIR}/$${_language}/LC_MESSAGES/${PACKAGE}.mo; \
	done

clean:
	rm -f aura dfui
	rm -rf ${PROG} *.o po/*.mo

# In-CVS-tree testing

test: in_cvs
	env LD_LIBRARY_PATH="../../lib/libaura:../../lib/libdfui" ./${PROG}

# $Id: Makefile,v 1.26 2005/04/05 19:40:46 cpressey Exp $

OSTYPE!=sysctl -n kern.ostype

PROG=	dfuibe_installer
OBJS=	fn_disk.o fn_configure.o fn_diagnostic.o fn_subpart.o fn_install.o \
	flow.o main.o

WARNS=	-W -Werror -Wall -Wstrict-prototypes -Wmissing-prototypes \
	-Wpointer-arith	-Wno-uninitialized -Wreturn-type -Wcast-qual \
	-Wwrite-strings -Wswitch -Wshadow -Wcast-align -Wchar-subscripts \
	-Winline -Wnested-externs -Wredundant-decls

CFLAGS+=${WARNS} -I/usr/local/include
.ifdef DEBUG
CFLAGS+=-g -DDEBUG
.endif
LDADD=	-L/usr/local/lib/ -laura -ldfui -linstaller

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

# Default target:

all: ${PROG} mo

# To build this straight from a checkout of the CVS tree, *without* first
# installing the support libraries, do a 'make in_cvs'.  This causes
# search paths and symbolic links to be set up so that the build process
# can find the needed header files and libraries.

in_cvs:
	cd ../../lib/libaura && ${MAKE} in_cvs
	ln -sf ../../lib/libaura aura
	cd ../../lib/libdfui && ${MAKE} in_cvs
	ln -sf ../../lib/libdfui dfui
	cd ../../lib/libinstaller && ${MAKE} in_cvs
	ln -sf ../../lib/libinstaller installer
	${MAKE} -DIN_CVS all

.if defined(IN_CVS)
CFLAGS+=-I.
LDADD+=	-L../../lib/libaura -L../../lib/libdfui -L../../lib/libinstaller
.endif

# Main target:

${PROG}: ${OBJS}
	${CC} ${CFLAGS} ${OBJS} ${LDADD} -o ${PROG}

# NLS (translated text) compilations:

mo:
	for _language in ${LANGUAGES}; do \
		msgfmt -o po/$${_language}.mo po/$${_language}; \
	done

# Translator templates:

pot:
	xgettext -s --no-location --keyword=_ -o po/${PROG}.pot *.c

# Cleaning up after ourselves:

clean:
	rm -f aura dfui installer
	rm -rf ${PROG} *.o po/*.mo

# Testing from within the CVS checkout.

test: in_cvs
	env LD_LIBRARY_PATH="../../lib/libaura:../../lib/libdfui:../../lib/libinstaller" ./${PROG}

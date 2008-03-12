# BSD Makefile for dfuibe_lua.
# $Id: Makefile,v 1.42 2005/04/05 20:53:46 cpressey Exp $

PACKAGE=	dfuibe_lua

.if defined(WITH_NLS)
LOCALEDIR=	/usr/local/share/locale
LANGUAGES=	ru
.endif

all:
	@echo "No building required."

# XXX this will probably be a little nightmarish to do right...
in_cvs: all

clean:
	rm -rf so/*.so po/*.mo

pot:
	cat `find . -name '*.lua' ! -regex '\./scripts/test/main.lua'` | \
	    sed 's|\-\-|//|' | sed 's/\.\.//' | \
	    xgettext -s -C --no-location --keyword=_ -o po/${PACKAGE}.pot -

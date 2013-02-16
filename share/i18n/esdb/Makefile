# $NetBSD: Makefile,v 1.15 2011/04/27 20:05:56 mbalmer Exp $

NOMAN=

.include <bsd.own.mk>

BINDIR?=	/usr/share/i18n/esdb

.SUFFIXES: .src .esdb

.src.esdb:
	mkesdb -o $@ $>

subdir=	APPLE AST BIG5 CBM CP DEC EUC EBCDIC GB GEORGIAN ISO-2022 \
	ISO-8859 ISO646 KAZAKH KOI MISC TCVN UTF

.for i in ${subdir}
.include "${.CURDIR}/$i/Makefile.inc"
.endfor

# ----------------------------------------------------------------------
# esdb.{dir,alias}
#

esdb.dir: ${SRC_esdb.dir}
	cat $> > $@
esdb.dir.db: esdb.dir
	mkesdb -m -o $@ $>

esdb.alias: ${SRC_esdb.alias}
	cat $> > $@
esdb.alias.db: esdb.alias
	mkesdb -m -o $@ $>

FILES+=		esdb.dir esdb.dir.db esdb.alias esdb.alias.db
CLEANFILES+=	esdb.dir esdb.dir.db esdb.alias esdb.alias.db
FILESDIR:=	${BINDIR}

all: ${FILES}
realall: ${FILES}

.include <bsd.prog.mk>

# $NetBSD: src/share/i18n/csmapper/Makefile,v 1.5 2003/10/21 10:01:21 lukem Exp $
# $DragonFly: src/share/i18n/csmapper/Makefile,v 1.1 2005/03/10 16:19:17 joerg Exp $

NOMAN=

.include <bsd.own.mk>

.SUFFIXES: .src .646 .mps

BINDIR?= /usr/share/i18n/csmapper
SRCS_mapper.dir+= mapper.dir.src
SRCS_charset.pivot+= charset.pivot.src

subdir= CP EBCDIC GB ISO646 ISO-8859 JIS KS MISC
.for i in ${subdir}
.if exists(${.CURDIR}/$i/Makefile.inc)
.include "${.CURDIR}/$i/Makefile.inc"
.endif
.endfor

# ----------------------------------------------------------------------

mapper.dir: ${SRCS_mapper.dir}
	cat $> > $@
mapper.dir.db: mapper.dir
	mkcsmapper -m -o $@ $>
FILES+= mapper.dir mapper.dir.db
CLEANFILES+= mapper.dir mapper.dir.db

charset.pivot: ${SRCS_charset.pivot}
	cat $> > $@
charset.pivot.pvdb: charset.pivot
	mkcsmapper -p -o $@ $>
FILES+= charset.pivot charset.pivot.pvdb
CLEANFILES+= charset.pivot charset.pivot.pvdb

.src.mps:
	mkcsmapper -o $@ $>

all: ${FILES}
realall: ${FILES}

.include <bsd.prog.mk>

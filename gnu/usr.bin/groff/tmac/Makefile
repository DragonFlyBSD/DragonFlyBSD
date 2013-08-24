# $FreeBSD: src/gnu/usr.bin/groff/tmac/Makefile,v 1.42 2003/09/08 17:46:33 ru Exp $

.include "../Makefile.inc"

TMACOWN?=	${BINOWN}
TMACGRP?=	${BINGRP}
TMACMODE?=	${NOBINMODE}
TMACDIR?=	${SHAREDIR}/tmac
MDOCDIR?=	${TMACDIR}/mdoc

MAN=		groff_ms.7 groff_man.7 groff_me.7 groff_mdoc.7
MAN+=		groff_trace.7 groff_www.7
MLINKS=		groff_ms.7 ms.7
MLINKS+=	groff_man.7 man.7
MLINKS+=	groff_me.7 me.7
MLINKS+=	groff_mdoc.7 mdoc.7 groff_mdoc.7 mdoc.samples.7

CONTRIBDIR=     ${DIST_DIR}

SRCS=		${PATCHES}
CLEANFILES=	${MAN} ${MDOCFILES:S/$/-s/} ${STRIPFILES:S/$/-s/} ${SPECIALFILES:S/$/-s/} ${NORMALFILES:S/$/-s/}

NORMALFILES= man.tmac mandoc.tmac andoc.tmac an-old.tmac an-ext.tmac \
	ms.tmac \
	me.tmac \
	mdoc.tmac \
	pic.tmac \
	a4.tmac papersize.tmac \
	62bit.tmac \
	ec.tmac \
	safer.tmac \
	trace.tmac \
	ps.tmac psold.tmac pspic.tmac psatk.tmac \
	dvi.tmac \
	tty.tmac tty-char.tmac \
	latin1.tmac latin2.tmac latin5.tmac latin9.tmac cp1047.tmac \
	unicode.tmac \
	X.tmac Xps.tmac \
	lj4.tmac \
	lbp.tmac \
	html.tmac html-end.tmac \
	devtag.tmac \
	europs.tmac \
	composite.tmac \
	eqnrc \
	troffrc troffrc-end \
	trans.tmac \
	hyphen.us hyphenex.us \
	fr.tmac hyphen.fr \
	sv.tmac hyphen.sv \
	de.tmac den.tmac hyphen.det hyphen.den hyphenex.det \
	cs.tmac hyphen.cs hyphenex.cs

SPECIALFILES=	an.tmac s.tmac www.tmac
STRIPFILES=	e.tmac doc.tmac doc-old.tmac mdoc.local
MDOCFILES=	doc-common doc-ditroff doc-nroff doc-syms \
		fr.ISO8859-1 ru.KOI8-R

all: ${MDOCFILES:S/$/-s/} ${STRIPFILES:S/$/-s/} ${SPECIALFILES:S/$/-s/} ${NORMALFILES:S/$/-s/}

.for f in ${MDOCFILES} ${STRIPFILES}
$f-s: $f
	sed -f ${DIST_DIR}/strip.sed ${.ALLSRC} > ${.TARGET}
.endfor

.for f in ${SPECIALFILES}
$f-s: $f
	sed -e "s;@TMAC_AN_PREFIX@;${tmac_an_prefix};g" \
	    -e "s;@TMAC_S_PREFIX@;${tmac_s_prefix};g" \
	    -e "s;@PNMTOPS_NOSETPAGE@;pnmtops;g" \
		${.ALLSRC} > ${.TARGET}
.endfor

.for f in ${NORMALFILES}
$f-s: $f
	cp ${.ALLSRC} ${.TARGET}
.endfor

beforeinstall:
	cd ${.CURDIR}; \
	    ${INSTALL} -o ${TMACOWN} -g ${TMACGRP} -m ${TMACMODE} \
	    koi8-r.tmac hyphen.ru ${DESTDIR}${TMACDIR}
.for f in ${STRIPFILES} ${SPECIALFILES} ${NORMALFILES}
	cd ${.OBJDIR}; \
	${INSTALL} -o ${TMACOWN} -g ${TMACGRP} -m ${TMACMODE} \
	    $f-s ${DESTDIR}${TMACDIR}/$f
.endfor
.for f in ${MDOCFILES}
	cd ${.OBJDIR}; \
	${INSTALL} -o ${TMACOWN} -g ${TMACGRP} -m ${TMACMODE} \
	    $f-s ${DESTDIR}${MDOCDIR}/$f
.endfor
.if !exists(${DESTDIR}${TMACDIR}/man.local)
	cd ${.OBJDIR}; \
	${INSTALL} -o ${TMACOWN} -g ${TMACGRP} -m ${TMACMODE} \
	    ${DIST_DIR}/man.local ${DESTDIR}${TMACDIR}
.endif

.include <bsd.prog.mk>

# $FreeBSD: src/gnu/usr.bin/binutils/doc/Makefile,v 1.4.2.5 2002/09/01 23:39:14 obrien Exp $
# $DragonFly: src/gnu/usr.bin/binutils/doc/Attic/Makefile,v 1.4 2005/01/04 22:25:26 joerg Exp $

.include "../Makefile.inc0"

CONTRIBDIR= ${.CURDIR}/../../../../contrib

.PATH: ${SRCDIR}/gas/doc ${SRCDIR}/ld ${SRCDIR}/bfd/doc

INFO = as ld gasp binutils
INFOSECTION=	"Programming & development tools."
INFOENTRY_as=	"* As: (as).            The GNU assembler."
INFOENTRY_gasp=	"* Gasp: (gasp).        The GNU Assembler Macro Preprocessor."
INFOENTRY_ld=	"* Ld: (ld).		The GNU linker."
INFOENTRY_binutils= "* Binutils: (binutils).		The GNU Binary Utilities."

MAKEINFOFLAGS+= --no-validate
MAKEINFOFLAGS+= -I ${SRCDIR}/gas/doc -I ${SRCDIR}/ld -I ${SRCDIR}/bfd/doc
MAKEINFOFLAGS+= -I ${SRCDIR}/binutils
MAKEINFOFLAGS+= -I ${CONTRIBDIR}/libreadline/doc

CLEANFILES=	inc-hist.texi inc-hist.texi.orig \
 		gasver.texi ldver.texi

as.info:	as.texinfo asconfig.texi c-i386.texi gasver.texi
ld.info:	ld.texinfo bfdsumm.texi ldver.texi configdoc.texi

configdoc.texi: gen-doc.texi
	ln -sf ${.ALLSRC} ${.TARGET}

.PATH: ${SRCDIR}/binutils/doc
binutils.info: binutils.texi config.texi

config.texi gasver.texi ldver.texi:
	echo "@set VERSION ${VERSION}" > ${.TARGET}

.PATH: ${CONTRIBDIR}/libreadline/doc
inc-hist.texi: hsuser.texinfo inc-hist.diff
	cp ${.ALLSRC:M*.texinfo} ${.TARGET}
	patch --suffix=.orig < ${.ALLSRC:M*.diff}

.include <bsd.info.mk>

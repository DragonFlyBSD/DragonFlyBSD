# $DragonFly: src/gnu/usr.bin/cc34/doc/Makefile,v 1.2 2008/05/14 15:02:48 hasso Exp $

.include "../Makefile.inc"

PATCHES!=	echo ${.CURDIR}/patches/*.patch
SRCDIR=		${GCCDIR}/gcc/doc
CONTRIBDIR=	${SRCDIR}

.PATH:	${SRCDIR}

INFO=	cpp cppinternals gcc gccint

INFOSECTION=		"Programming & development tools."
INFOENTRY_cpp=		"* CPP.					The GNU C preprocessor."
INFOENTRY_cppinternals=	"* CPP-internals.			The GNU C preprocessor's internals."
INFOENTRY_gcc=		"* GCC.					The GNU Compiler Collection."
INFOENTRY_gcint=	"* GCC-internals.			The GNU Compiler Collection's internals."

cpp.info: cpp.texi fdl.texi cppenv.texi cppopts.texi

cppinternals.info: cppinternals.texi

gcc.info: gcc.texi gcc-common.texi frontends.texi standards.texi invoke.texi \
	  extend.texi md.texi objc.texi gcov.texi trouble.texi bugreport.texi \
	  service.texi contribute.texi compat.texi funding.texi gnu.texi \
	  gpl.texi fdl.texi contrib.texi cppenv.texi cppopts.texi

gccint.info: gccint.texi gcc-common.texi contribute.texi makefile.texi \
	     configterms.texi portability.texi interface.texi passes.texi \
	     c-tree.texi rtl.texi md.texi tm.texi hostconfig.texi \
	     fragments.texi configfiles.texi collect2.texi headerdirs.texi \
	     funding.texi gnu.texi gpl.texi fdl.texi contrib.texi \
	     languages.texi sourcebuild.texi gty.texi libgcc.texi

.for inctexi in fdl.texi funding.texi gcc-common.texi gpl.texi texinfo.texi
${inctexi}: ${CONTRIBDIR}/include/${inctexi}
	cp ${CONTRIBDIR}/include/${inctexi} ${.TARGET}
CLEANFILES+=	${inctexi}
.endfor

.include <bsd.info.mk>

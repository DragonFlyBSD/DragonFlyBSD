# $DragonFly: src/gnu/usr.bin/binutils215/ld/Attic/Makefile,v 1.2 2005/01/10 05:00:37 y0netan1 Exp $

.include "../Makefile.inc0"

.PATH: ${SRCDIR}/ld

PROG=	ld
LIBEXEC_BINUTILS= /usr/libexec/binutils215
SCRIPTDIR= ${LIBEXEC_BINUTILS}/ldscripts
SRCS+=	ldcref.c ldctor.c ldemul.c ldemul-list.h ldexp.c ldfile.c \
	ldgram.y ldlang.c ldlex.l ldmain.c ldmisc.c \
	ldver.c ldwrite.c lexsup.c mri.c

CONTRIBDIR=	${SRCDIR}/ld
CLEANFILES+=	emultempl/elf32.em genscripts.sh

emultempl/elf32.em: ${CONTRIBDIR}/emultempl/elf32.em patches/elf32.em.patch
	mkdir -p emultempl
	patch -o ${.TARGET} -i ${.ALLSRC:M*.patch} ${CONTRIBDIR}/${.TARGET}

genscripts.sh: ${CONTRIBDIR}/genscripts.sh patches/genscripts.sh.patch
	patch -o ${.TARGET} -i ${.ALLSRC:M*.patch} ${CONTRIBDIR}/${.TARGET}

depend: emultempl/elf32.em genscripts.sh

.if defined(BOOTSTRAPPING) && !exists(/usr/include/elf-hints.h)
SRCS+=	elf-hints.h
CLEANFILES+=	elf-hints.h

elf-hints.h:
	ln -sf ${.CURDIR}/../../../../include/${.TARGET} .
.endif

WARNS=	1
CFLAGS+= -DSCRIPTDIR=\"${TOOLS_PREFIX}${LIBEXEC_BINUTILS}\"
CFLAGS+= -DBFD_VERSION_STRING=\"${VERSION}\"
CFLAGS+= -I${SRCDIR}/ld -I${SRCDIR}/bfd -I${.OBJDIR}/../libbfd
CFLAGS+= -DBINDIR=\"${TOOLS_PREFIX}${LIBEXEC_BINUTILS}\"
CFLAGS+= -DTOOLBINDIR=\"${TOOLS_PREFIX}${LIBEXEC_BINUTILS}\"
NOSHARED?= yes
DPADD=	${RELTOP}/libbfd/libbfd.a
DPADD+=	${RELTOP}/libiberty/libiberty.a
LDADD=	${DPADD}
CLEANDIRS+=	ldscripts
CLEANFILES+=	ldemul-list.h stringify.sed

# If set, BINUTILSDISTDIR is the path to a directory containing the full GNU
# binutils release. FreeBSD only distributes the bits that are required to
# build native architectures. BINUTILSDISTDIR is needed to build cross tools.
.if defined(BINUTILSDISTDIR) && exists(${BINUTILSDISTDIR})
.PATH: ${BINUTILSDISTDIR}/ld
CFLAGS+= -I${BINUTILSDISTDIR}/ld
.endif

EMXFR=
EMLST=
.for _e in ${EMS}
EMXFR+=	extern ld_emulation_xfer_type ld_${_e}_emulation;
EMLST+=	&ld_${_e}_emulation,
.endfor

ldemul-list.h:
	echo "${EMXFR}" > ${.TARGET}
	echo "#define EMULATION_LIST ${EMLST} 0" >> ${.TARGET}

stringify.sed:
	ln -sf ${SRCDIR}/ld/emultempl/astring.sed ${.TARGET}

afterinstall:
	${INSTALL} -o ${LIBOWN} -g ${LIBGRP} -m ${LIBMODE} \
	    ${LDSCRIPTS:S|^|ldscripts/|} ${DESTDIR}${SCRIPTDIR}

.include <bsd.prog.mk>

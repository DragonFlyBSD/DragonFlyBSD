ELF_SCR_EXT=	x xbn xn xr xs xu xc xsc

.include "../Makefile.inc0"

.PATH: ${SRCDIR}/ld

PROG=	ld
MAN=	ld217.1
MLINKS+=ld217.1 ld.1
LIBEXEC_BINUTILS= /usr/libexec/binutils217
SCRIPTDIR= ${LIBEXEC_BINUTILS}/ldscripts
SRCS+=	ldcref.c ldctor.c ldemul.c ldemul-list.h ldexp.c ldfile.c \
	ldgram.c ldlang.c ldlex.c ldmain.c ldmisc.c \
	ldver.c ldwrite.c lexsup.c mri.c

.if defined(BOOTSTRAPPING) && !exists(/usr/include/elf-hints.h)
SRCS+=	elf-hints.h
CLEANFILES+=	elf-hints.h

elf-hints.h:
	${LN} -sf ${.CURDIR}/../../../../include/${.TARGET} .
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
CLEANDIRS+=	ldscripts tmpdir
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
	${LN} -sf ${SRCDIR}/ld/emultempl/astring.sed ${.TARGET}

ld217.1: ${SRCDIR}/ld/ld.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	ld217.1

afterinstall:
	${INSTALL} -o ${LIBOWN} -g ${LIBGRP} -m ${LIBMODE} \
	    ${LDSCRIPTS:S|^|ldscripts/|} ${DESTDIR}${SCRIPTDIR}

.include <bsd.prog.mk>

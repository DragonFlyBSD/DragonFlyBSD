ELF_SCR_EXT=	x xbn xc xd xdc xdw xn xr xs xsc xsw xu xw

.include "../Makefile.inc0"
.include "../Makefile.inc1"

.PATH: ${SRCDIR}/ld

PROG=	ld.bfd
MFILE=	ld${MANPAGEVER}.1
MAN=	${MFILE}
LIBEXEC_BINUTILS= ${BINDIR}/..
SCRIPTDIR= ${LIBEXEC_BINUTILS}/ldscripts
SRCS+=	ldgram.y	\
	ldlex.l		\
	lexsup.c	\
	ldlang.c	\
	mri.c		\
	ldctor.c	\
	ldmain.c	\
	ldwrite.c	\
	ldexp.c		\
	ldemul.c	\
	ldver.c		\
	ldmisc.c	\
	ldfile.c	\
	ldcref.c

SRCS+=	ldemul-list.h

WARNS=	1
CFLAGS+= -DSCRIPTDIR=\"${TOOLS_PREFIX}${LIBEXEC_BINUTILS}\"
CFLAGS+= -I${SRCDIR}/ld -I${SRCDIR}/bfd -I${.OBJDIR}/../libbfd
CFLAGS+= -DBINDIR=\"${TOOLS_PREFIX}${LIBEXEC_BINUTILS}\"
CFLAGS+= -DTOOLBINDIR=\"${TOOLS_PREFIX}${LIBEXEC_BINUTILS}\"
CFLAGS+= -DTARGET_SYSTEM_ROOT=\"${USRDATA_PREFIX}\"
NOSHARED?= yes

CLEANDIRS+=	ldscripts tmpdir
CLEANFILES+=	ldemul-list.h stringify.sed

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

${MFILE}: ${SRCDIR}/ld/ld.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} ld.1
.endif

afterinstall: install-ldscripts

install-ldscripts: ${LDSCRIPTS:S|^|ldscripts/|}
	${INSTALL} -o ${LIBOWN} -g ${LIBGRP} -m ${LIBMODE} \
	     ${.ALLSRC} ${DESTDIR}${SCRIPTDIR}

.include <bsd.prog.mk>

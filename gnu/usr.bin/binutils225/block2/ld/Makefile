.include "../../Makefile.inc1"
.include "../Makefile.block2"

.PATH: ${SRCDIR}/ld

PROG=	ld.bfd
MFILE=	ld${MANPAGEVER}.1
MAN=	${MFILE}
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
	ldcref.c	\
	plugin.c	\
	ldbuildid.c
LDADD=	${LDADD_BFD} ${LDADD_IBERTY} -lz

LIBEXEC_BINUTILS=	${BINDIR}/..
SCRIPTDIR=		${LIBEXEC_BINUTILS}/ldscripts
ELF_SCR_EXT=		x xbn xc xd xdc xdw xn xr xs xsc xsw xu xw

.include "Makefile.x86_64"

# We need to overwrite CFLAGS, it's picking up bulibs's config.h first
CFLAGS=		-I${SRCDIR}/include -I${SRCDIR}/bfd
CFLAGS+=	-I${SRCDIR}/ld -I${.OBJDIR} -I${.CURDIR}
CFLAGS+=	-I${OBJTOP}/block1/bulibs -I${BINTOP}/block1/bulibs

CFLAGS+=	-DSCRIPTDIR=\"${TOOLS_PREFIX}${LIBEXEC_BINUTILS}\"
CFLAGS+=	-DBINDIR=\"${TOOLS_PREFIX}${LIBEXEC_BINUTILS}\"
CFLAGS+=	-DTOOLBINDIR=\"${TOOLS_PREFIX}${LIBEXEC_BINUTILS}\"
CFLAGS+=	-DENABLE_PLUGINS -DHAVE_CONFIG_H
CFLAGS+=	-DTARGET_SYSTEM_ROOT=\"${USRDATA_PREFIX}\"
# link-time optimization requires dlopen() which means ld cannot be built
# statically and also support -plugin, not even for cross-tools.
.undef NOSHARED

ldemul.c: ldemul-list.h

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

install-ldscripts: ${LDSCRIPTS:S|^|ldscripts/|}
	${INSTALL} -o ${LIBOWN} -g ${LIBGRP} -m ${LIBMODE} \
	     ${.ALLSRC} ${DESTDIR}${SCRIPTDIR}

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} ld.1
.endif

afterinstall: install-ldscripts

CLEANDIRS+=	ldscripts tmpdir
CLEANFILES+=	${MFILE} ldemul-list.h stringify.sed

.include <bsd.prog.mk>

# $DragonFly: src/gnu/usr.bin/cc34/cc_prep/Makefile,v 1.2 2004/06/19 11:13:29 joerg Exp $

.include "../Makefile.inc"

PATCHES!=	echo ${.CURDIR}/patches/*.patch
CONTRIBDIR=	${GCCDIR}/gcc

version_local.c: version.c Makefile
	sed -e 's/\(const char version.*".*\) (/\1 [DragonFly] (/' < ${.ALLSRC:M*c} > ${.TARGET}

configargs.h: Makefile
	echo '/* Generated automatically. */' > configargs.h
	echo 'static const char configuration_arguments[] = "./configure --prefix=/usr --host=${target}";' >> configargs.h
	echo 'static const char thread_model[] = "posix";' >> configargs.h
	echo >> configargs.h
	echo 'static const struct {' >> configargs.h
	echo '  const char *name, *value;' >> configargs.h
	echo '} configure_default_options[] = { { NULL, NULL} };' >> configargs.h

CLEANFILES+=	version_local.c configargs.h

.include <bsd.prog.mk>

# keep this order!
depend: version_local.c configargs.h

# we don't use SRCS, so we can't use the normal bsd.dep.mk code
.for _PSRC in ${PATCHES:M*.patch}
.for _PC in ${_PSRC:T:S/.patch$//:S|,|/|g}

${_PC}: ${CONTRIBDIR}/${_PC} ${_PSRC}
	mkdir -p ${.TARGET:H}
	patch -o ${.TARGET} -i ${.ALLSRC:M*.patch} ${CONTRIBDIR}/${.TARGET}

CLEANFILES:=	${CLEANFILES} ${_PC}

depend: ${_PC}
.endfor
.endfor

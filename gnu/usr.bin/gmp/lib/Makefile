.include "../Makefile.inc"
.include "Makefile.src"

# NOTE: Place 'gmp.h' at the upper directory to avoid the 'config.h'
# possibly polluting the GMP users (e.g., MPFR, MPC).

LIB=		gmp
INTERNALLIB=	YES

CFLAGS+=	-I${.CURDIR} -I${.CURDIR}/..
CFLAGS+=	-I${.OBJDIR}
CFLAGS+=	-I${SRCDIR}
CFLAGS+=	-D__GMP_WITHIN_GMP

# Preserve the subdirs in the targets in .depend.
MKDEPSRCDIR=	${SRCDIR}

SRCS=		${libgmp_la_SOURCES} ${libgmp_la_LIBADD:.lo=.c}

# Required mpn multiple-function sources
# see also: GMP_MULFUNC_CHOICES in configure.ac
MPN_MFUNCS=	logops_n.c,and_n \
		logops_n.c,andn_n \
		logops_n.c,ior_n \
		logops_n.c,iorn_n \
		logops_n.c,nand_n \
		logops_n.c,nior_n \
		logops_n.c,xnor_n \
		logops_n.c,xor_n \
		popham.c,hamdist \
		popham.c,popcount \
		sec_aors_1.c,sec_add_1 \
		sec_aors_1.c,sec_sub_1 \
		sec_div.c,sec_div_qr \
		sec_div.c,sec_div_r \
		sec_pi1_div.c,sec_pi1_div_qr \
		sec_pi1_div.c,sec_pi1_div_r

# Populate mpn sources.
.for src in ${mpn_objs_in_libgmp:.lo=.c}
sfile:=	${src:T}
. if ${MPN_MFUNCS:M*,${sfile:R}} != ""
sfile:=	${MPN_MFUNCS:M*,${sfile:R}:S/,${sfile:R}//}
. endif
${src}: ${SRCDIR}/mpn/generic/${sfile}
	cp ${.ALLSRC} ${.TARGET}
.endfor

FLAGS_GROUPS=		operation
operation_FLAGS_FILES=	${mpn_objs_in_libgmp:.lo=.c}
operation_FLAGS=	-DOPERATION_${.TARGET:T:R}

GENFILES+=	gmp-mparam.h
gmp-mparam.h: mpn/generic/gmp-mparam.h
	cp ${.ALLSRC} ${.TARGET}

TOOLSDIR=	${.OBJDIR}/../tools
GMP_LIMB_BITS!=	awk '/^\#define[[:space:]]+GMP_LIMB_BITS/ { print $$3 }' \
		${.CURDIR}/../gmp.h
GMP_NAIL_BITS!=	awk '/^\#define[[:space:]]+GMP_NAIL_BITS/ { print $$3 }' \
		${.CURDIR}/../gmp.h

GENFILES+=	fac_table.h sieve_table.h \
		fib_table.h mpn/fib_table.c \
		mp_bases.h mpn/mp_bases.c \
		trialdivtab.h \
		mpn/jacobitab.h mpn/perfsqr.h
fac_table.h: ${TOOLSDIR}/gen-fac.nx
	${TOOLSDIR}/gen-fac.nx ${GMP_LIMB_BITS} ${GMP_NAIL_BITS} > ${.TARGET}
sieve_table.h: ${TOOLSDIR}/gen-sieve.nx
	${TOOLSDIR}/gen-sieve.nx ${GMP_LIMB_BITS} > ${.TARGET}
fib_table.h: ${TOOLSDIR}/gen-fib.nx
	${TOOLSDIR}/gen-fib.nx header ${GMP_LIMB_BITS} ${GMP_NAIL_BITS} > ${.TARGET}
mpn/fib_table.c: ${TOOLSDIR}/gen-fib.nx
	${TOOLSDIR}/gen-fib.nx table ${GMP_LIMB_BITS} ${GMP_NAIL_BITS} > ${.TARGET}
mp_bases.h: ${TOOLSDIR}/gen-bases.nx
	${TOOLSDIR}/gen-bases.nx header ${GMP_LIMB_BITS} ${GMP_NAIL_BITS} > ${.TARGET}
mpn/mp_bases.c: ${TOOLSDIR}/gen-bases.nx
	${TOOLSDIR}/gen-bases.nx table ${GMP_LIMB_BITS} ${GMP_NAIL_BITS} > ${.TARGET}
trialdivtab.h: ${TOOLSDIR}/gen-trialdivtab.nx
	${TOOLSDIR}/gen-trialdivtab.nx ${GMP_LIMB_BITS} 8000 > ${.TARGET}
mpn/jacobitab.h: ${TOOLSDIR}/gen-jacobitab.nx
	${TOOLSDIR}/gen-jacobitab.nx > ${.TARGET}
mpn/perfsqr.h: ${TOOLSDIR}/gen-psqr.nx
	${TOOLSDIR}/gen-psqr.nx ${GMP_LIMB_BITS} ${GMP_NAIL_BITS} > ${.TARGET}

GENDIRS=	mpf mpz mpq mpn mpn/generic printf scanf rand
mkdirs:
	mkdir -p ${GENDIRS}

genfiles: ${GENFILES}  # for parallelism
.ORDER: mkdirs genfiles

CLEANFILES+=	${GENFILES}
CLEANDIRS+=	${GENDIRS}

beforedepend: mkdirs genfiles

.include <bsd.lib.mk>

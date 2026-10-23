CFLAGS+=	-I${.CURDIR}
GCC_NO_INC=	yes
.include "../Makefile.inc"

# Prefer sources in config/x86 and config/posix.
# see also: ${search_path} in libatomic/Makefile.am
.PATH: ${GCCDIR}/libatomic/config/x86
.PATH: ${GCCDIR}/libatomic/config/posix
.PATH: ${GCCDIR}/libatomic

CFLAGS+=	-I${GCCDIR}/libatomic/config/x86
CFLAGS+=	-I${GCCDIR}/libatomic/config/posix
CFLAGS+=	-I${GCCDIR}/libatomic
VERSION_MAP=	${GCCDIR}/libatomic/libatomic.map

LIB=		atomic
SHLIB_MAJOR=	1

SRCS=	${libatomic_la_SOURCES}
OBJS=	${libatomic_la_LIBADD}

# From libatomic/Makefile.am
libatomic_la_SOURCES = gload.c gstore.c gcas.c gexch.c glfree.c lock.c init.c \
	fenv.c fence.c flag.c

# From gcc-build/{target}/libatomic/Makefile
SIZEOBJS = load store cas exch fadd fsub fand fior fxor fnand tas
SIZES = 1 2 4 8 16
## On a target-specific basis, include alternates to be selected by IFUNC.
## HAVE_IFUNC -> ARCH_X86_64
IFUNC_OPTIONS = -mcx16
IFUNC_ALTS_16 = 1 2

.for f in ${SIZEOBJS}
. for s in ${SIZES}
libatomic_la_LIBADD+=	${f}_${s}_.o
SRC.${f}_${s}_=		${GCCDIR}/libatomic/${f}_n.c
FLAGS.${f}_${s}_=	-DN=${s}

.  for alt in ${IFUNC_ALTS_${s}}
libatomic_la_LIBADD+=	${f}_${s}_${alt}_.o
SRC.${f}_${s}_${alt}_=	${GCCDIR}/libatomic/${f}_n.c
FLAGS.${f}_${s}_${alt}_= -DN=${s} -DIFUNC_ALT=${alt} ${IFUNC_OPTIONS}
.  endfor
. endfor
.endfor

.for f in ${libatomic_la_LIBADD:R}
${f}.o: ${SRC.${f}}
	${CC} ${STATIC_CFLAGS} ${CFLAGS} ${FLAGS.${f}} \
		-c ${.ALLSRC} -o ${.TARGET}

${f}.po: ${SRC.${f}}
	${CC} ${PO_FLAG} ${STATIC_CFLAGS} ${PO_CFLAGS} ${FLAGS.${f}} \
		-c ${.ALLSRC} -o ${.TARGET}

${f}.So: ${SRC.${f}}
	${CC} ${PICFLAG} -DPIC ${SHARED_CFLAGS} ${CFLAGS} ${FLAGS.${f}} \
		-c ${.ALLSRC} -o ${.TARGET}
.endfor

.include <bsd.lib.mk>

.include "../../Makefile.inc1"
.include "../Makefile.block2"
.include "x86_64-dragonfly/Makefile"

.PATH: ${SRCDIR}/gas ${SRCDIR}/gas/config

PROG=	as
MFILE=	as${MANPAGEVER}.1
MAN=	${MFILE}
SRCS+=	app.c \
	as.c \
	atof-generic.c \
	compress-debug.c \
	cond.c \
	depend.c \
	dwarf2dbg.c \
	dw2gencfi.c \
	ecoff.c \
	ehopt.c \
	expr.c \
	flonum-copy.c \
	flonum-konst.c \
	flonum-mult.c \
	frags.c \
	hash.c \
	input-file.c \
	input-scrub.c \
	listing.c \
	literal.c \
	macro.c \
	messages.c \
	output-file.c \
	read.c \
	remap.c \
	sb.c \
	stabs.c \
	subsegs.c \
	symbols.c \
	write.c
LDADD=	${STD_LDADD} ${LDADD_OPCODE} -lz

CFLAGS+=	-I${SRCDIR}/gas -I${SRCDIR}/gas/config -I${SRCDIR}
CFLAGS+=	-I${.CURDIR}/x86_64-dragonfly

CFLAGS+=	-DTARGET_ALIAS=\"${TARGET_ALIAS}\"
CFLAGS+=	-DTARGET_CANONICAL=\"${TARGET_CANONICAL}\"
CFLAGS+=	-DTARGET_CPU=\"${TARGET_CPU}\"
CFLAGS+=	-DDEFAULT_ARCH=\"${DEFAULT_ARCH}\"
CFLAGS+=	-DTARGET_OS=\"dragonfly\"
CFLAGS+=	"-DEMULATIONS=${EMULATIONS}"
CFLAGS+=	-DDEFAULT_EMULATION=\"${DEFAULT_EMULATION}\"
CFLAGS+=	-DTE_DragonFly -DHAVE_ERRNO_H

NOSHARED?=	yes

${MFILE}: ${SRCDIR}/gas/doc/as.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} as.1
.endif

.include <bsd.prog.mk>

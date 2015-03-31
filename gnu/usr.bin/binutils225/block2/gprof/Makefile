.include "../../Makefile.inc1"
.include "../Makefile.block2"

.PATH: ${SRCDIR}/gprof

PROG=	gprof
SRCS=   ${gprof_SOURCES}
MFILE=	gprof${MANPAGEVER}.1
MAN=	${MFILE}
LDADD=	${LDADD_BFD} ${LDADD_IBERTY} -lz

#verbatim from gprof/Makefile
sources = basic_blocks.c call_graph.c cg_arcs.c cg_dfn.c \
	cg_print.c corefile.c gmon_io.c gprof.c hertz.c hist.c source.c \
	search_list.c symtab.c sym_ids.c utils.c \
	i386.c alpha.c vax.c tahoe.c sparc.c mips.c aarch64.c
gprof_SOURCES = $(sources) flat_bl.c bsd_callg_bl.c fsf_callg_bl.c

CFLAGS+=	-DHAVE_CONFIG_H
CFLAGS+=	-I${.CURDIR} 

${MFILE}: ${.CURDIR}/gprof.1
	cp ${.ALLSRC} ${.TARGET}

flat_bl.c bsd_callg_bl.c fsf_callg_bl.c:
	/usr/bin/awk -f ${SRCDIR}/gprof/gen-c-prog.awk > ${.TARGET} \
	FUNCTION=`(echo ${.TARGET:R}|sed -e 's,.*/,,g' -e 's/_bl//')`_blurb \
	FILE=flat_bl.m ${SRCDIR}/gprof/${.TARGET:.c=.m}

CLEANFILES+=	${MFILE} flat_bl.c bsd_callg_bl.c fsf_callg_bl.c

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} gprof.1
.endif

.include <bsd.prog.mk>

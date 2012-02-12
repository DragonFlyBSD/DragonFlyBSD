.include "../Makefile.inc1"
.include "../Makefile.bu"

.PATH: ${SRCDIR}/gprof

PROG=	gprof
SRCS=   basic_blocks.c	\
	call_graph.c	\
	cg_arcs.c	\
	cg_dfn.c	\
	cg_print.c	\
	corefile.c	\
	gmon_io.c	\
	gprof.c		\
	hertz.c		\
	hist.c		\
	source.c	\
	search_list.c	\
	symtab.c	\
	sym_ids.c	\
	utils.c		\
	i386.c		\
	alpha.c		\
	vax.c		\
	tahoe.c		\
	sparc.c		\
	mips.c		\
	flat_bl.c	\
	bsd_callg_bl.c	\
	fsf_callg_bl.c
MFILE=	gprof${MANPAGEVER}.1
MAN=	${MFILE}

CFLAGS=	-DHAVE_CONFIG_H
CFLAGS+=-I${.CURDIR} -I${.CURDIR}/../libbfd -I${.OBJDIR}/../libbfd
CFLAGS+=-I${SRCDIR}/include -I${SRCDIR}/bfd

${MFILE}: ${.CURDIR}/gprof.1
	cp ${.ALLSRC} ${.TARGET}

flat_bl.c:
	/usr/bin/awk -f ${SRCDIR}/gprof/gen-c-prog.awk > ${.TARGET} \
	FUNCTION=`(echo flat_bl|sed -e 's,.*/,,g' -e 's/_bl//')`_blurb \
	FILE=flat_bl.m ${SRCDIR}/gprof/flat_bl.m
	
bsd_callg_bl.c:	
	/usr/bin/awk -f ${SRCDIR}/gprof/gen-c-prog.awk > ${.TARGET} \
	FUNCTION=`(echo bsd_callg_bl|sed -e 's,.*/,,g' -e 's/_bl//')`_blurb \
	FILE=bsd_callg_bl.m ${SRCDIR}/gprof/bsd_callg_bl.m
	
fsf_callg_bl.c:
	/usr/bin/awk -f ${SRCDIR}/gprof/gen-c-prog.awk > ${.TARGET} \
	FUNCTION=`(echo fsf_callg_bl|sed -e 's,.*/,,g' -e 's/_bl//')`_blurb \
	FILE=fsf_callg_bl.m ${SRCDIR}/gprof/fsf_callg_bl.m

CLEANFILES+=	${MFILE}       \
		flat_bl.c      \
		bsd_callg_bl.c \
		fsf_callg_bl.c

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} gprof.1
.endif

.include <bsd.prog.mk>

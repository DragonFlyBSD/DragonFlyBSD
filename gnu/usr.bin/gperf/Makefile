# $FreeBSD: src/gnu/usr.bin/gperf/Makefile,v 1.4.2.2 2002/08/09 06:52:41 ru Exp $
# $DragonFly: src/gnu/usr.bin/gperf/Attic/Makefile,v 1.2 2003/06/17 04:25:45 dillon Exp $

GPROFDIR=	${.CURDIR}/../../../contrib/gperf

.PATH: ${GPROFDIR}/src ${GPROFDIR}/lib ${GPROFDIR}/doc

SUBDIR=	doc

PROG_CXX=	gperf
SRCS=	bool-array.cc gen-perf.cc hash-table.cc iterator.cc key-list.cc \
	list-node.cc main.cc new.cc options.cc read-line.cc trace.cc \
	vectors.cc version.cc
SRCS+=	hash.cc getopt.c getopt1.c

CXXFLAGS+=	-I${GPROFDIR}/lib -I${.CURDIR}

.include <bsd.prog.mk>

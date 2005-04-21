# $FreeBSD: src/gnu/usr.bin/gperf/Makefile,v 1.4.2.2 2002/08/09 06:52:41 ru Exp $
# $DragonFly: src/gnu/usr.bin/gperf/Attic/Makefile,v 1.3 2005/04/21 18:31:29 asmodai Exp $

GPROFDIR=	${.CURDIR}/../../../contrib/gperf-3.0.1

.PATH: ${GPROFDIR}/src ${GPROFDIR}/lib ${GPROFDIR}/doc

SUBDIR=	doc

PROG_CXX=	gperf
SRCS= bool-array.cc hash-table.cc input.cc keyword-list.cc keyword.cc main.cc \
      options.cc output.cc positions.cc search.cc version.cc \
      getline.cc hash.cc getopt.c getopt1.c

CXXFLAGS+=	-I${GPROFDIR}/lib -I${.CURDIR}

.include <bsd.prog.mk>

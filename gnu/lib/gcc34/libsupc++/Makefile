# $DragonFly: src/gnu/lib/gcc34/libsupc++/Makefile,v 1.1 2004/06/14 22:27:52 joerg Exp $

.include "../Makefile.inc"

.PATH: ${GCCDIR}/libstdc++-v3/libsupc++

CFLAGS+=	-DIN_GLIBCPP_V3 -DHAVE_CONFIG_H
CXXFLAGS+=	-fno-implicit-templates -ffunction-sections -fdata-sections
CXXFLAGS+=	-I${GCCDIR}/libstdc++-v3/libsupc++ 
CXXFLAGS+=	-I${GCCDIR}/libstdc++-v3/include/c_std
CXXFLAGS+=	-I${STOPDIR}/cc_prep/c++ -I.

INCS=		exception new typeinfo cxxabi.h exception_defines.h
INCSDIR=	${INCLUDEDIR}/c++/3.4

LIB=		supc++

SRCS=	del_op.cc del_opnt.cc del_opv.cc del_opvnt.cc
SRCS+=	eh_alloc.cc eh_aux_runtime.cc eh_catch.cc eh_exception.cc
SRCS+=	eh_globals.cc eh_personality.cc eh_term_handler.cc eh_terminate.cc
SRCS+=	eh_throw.cc eh_type.cc eh_unex_handler.cc guard.cc new_handler.cc
SRCS+=	new_op.cc new_opnt.cc new_opv.cc new_opvnt.cc pure.cc tinfo.cc
SRCS+=	tinfo2.cc vec.cc vterminate.cc
# from libiberty
SRCS+=	cp-demangle.c

.include <bsd.lib.mk>

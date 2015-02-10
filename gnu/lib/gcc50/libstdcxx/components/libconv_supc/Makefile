RELUP=	/../..
.include "../../../Makefile.inc"
SRCDIR=	${GCCDIR}/libstdc++-v3
.include "Makefile.src"

LIB=			supc++convenience
INTERNALLIB=		yes
NOPROFILE=		yes
INSTALL_PIC_ARCHIVE=	yes

.PATH:	${GCCDIR}/libiberty
.PATH:	${GCCDIR}/libgcc
.PATH:	${SRCDIR}/libsupc++

CFLAGS+=	-I${.OBJDIR}
CFLAGS+=	-I${GCCDIR}/libgcc
CXXFLAGS+=	-fno-implicit-templates
CXXFLAGS+=	-fdiagnostics-show-location=once
CXXFLAGS+=	-ffunction-sections -fdata-sections
SHARED_CXXFLAGS=	-D_GLIBCXX_SHARED

FLAGS_GROUPS=		gnu11 gnu14 mangle
gnu11_FLAGS=		-std=gnu++11
gnu11_FLAGS_FILES=	bad_array_length.cc bad_array_new.cc eh_ptr.cc \
			eh_aux_runtime.cc eh_terminate.cc eh_throw.cc \
			guard.cc atexit_thread.cc nested_exception.cc \
			new_handler.cc new_op.cc new_opnt.cc
gnu14_FLAGS=		-std=gnu++14 -Wno-sized-deallocation
gnu14_FLAGS_FILES=	del_ops.cc del_opvs.cc
mangle_FLAGS=		-DIN_GLIBCPP_V3
mangle_FLAGS_FILES=	cp-demangle.c

SRCS=	${libsupc__convenience_la_SOURCES}

.include <bsd.lib.mk>

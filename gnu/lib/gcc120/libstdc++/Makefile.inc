GCC_NO_INC=	yes
.include "../Makefile.inc"

# for now do not let the use of -flto (-Wodr...)
.if ${CFLAGS:M-flto}
CFLAGS+=	-fno-lto
LDFLAGS+=	-flto
.endif

CFLAGS+=	-I${DF.OBJTOP}/gnu/lib/${CCVER}/libstdc++/headers
CFLAGS+=	-I${SRCDIR}/include \
		-I${SRCDIR}/include/std \
		-I${SRCDIR}/include/c_global \
		-I${SRCDIR}/libsupc++

CXXFLAGS+=	-nostdinc++
CXXFLAGS+=	-fdiagnostics-show-location=once
CXXFLAGS+=	-ffunction-sections -fdata-sections

SHARED_CXXFLAGS= -D_GLIBCXX_SHARED

LDFLAGS+=	-Wl,-z,relro -Wl,--gc-sections

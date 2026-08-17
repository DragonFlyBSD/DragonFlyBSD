GCC_NO_INC=	yes
.include "../Makefile.inc"

# for now do not let the use of -flto (-Wodr...)
.if ${CFLAGS:M-flto}
CFLAGS+=	-fno-lto
LDFLAGS+=	-flto
.endif

.if exists(${.OBJDIR}/../headers)  # for product
CFLAGS+=	-I${.OBJDIR}/../headers
.elif exists(${.OBJDIR}/../../headers)  # for components/libconv_*
CFLAGS+=	-I${.OBJDIR}/../../headers
.elif exists(${.OBJDIR}/../libstdc++/headers)  # for libstdc++fs
CFLAGS+=	-I${.OBJDIR}/../libstdc++/headers
.endif

CFLAGS+=	-I${SRCDIR}/include \
		-I${SRCDIR}/include/std \
		-I${SRCDIR}/include/c_global \
		-I${SRCDIR}/libsupc++

CXXFLAGS+=	-nostdinc++
CXXFLAGS+=	-fdiagnostics-show-location=once
CXXFLAGS+=	-ffunction-sections -fdata-sections

SHARED_CXXFLAGS= -D_GLIBCXX_SHARED

LDFLAGS+=	-Wl,-z,relro -Wl,--gc-sections

INTERNALLIB=	yes
TOP_LLVM=	${.CURDIR}/../../../contrib/llvm
CFLAGS+=	-I${.OBJDIR} \
		-I${TOP_LLVM}/include \
		-I${TOP_LLVM}/include/${HDRDIR}

CFLAGS+=	-DGTEST_HAS_RTTI=0 \
		-D__STDC_CONSTANT_MACROS \
		-D__STDC_FORMAT_MACROS \
		-D__STDC_LIMIT_MACROS

CXXFLAGS+=	-std=c++11 -fno-exceptions -fno-rtti

${SRCS}:	llvm/Support/DataTypes.h \
		llvm/Config/llvm-config.h \
		llvm/Config/config.h

CLEANDIRS=	llvm

.PATH:		${TOP_LLVM}/${SRCDIR}

.include "${.CURDIR}/../Makefile.generated"

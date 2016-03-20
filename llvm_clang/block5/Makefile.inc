TOP_LLVM=	${.CURDIR}/../../../contrib/llvm
CFLAGS+=	-I${.OBJDIR} \
		-I${.OBJDIR}/../../block3 \
		-I${TOP_LLVM}/include \

CFLAGS+=	-DGTEST_HAS_RTTI=0 \
		-D__STDC_CONSTANT_MACROS \
		-D__STDC_FORMAT_MACROS \
		-D__STDC_LIMIT_MACROS

CXXFLAGS+=	-std=c++11 -fno-exceptions -fno-rtti

${SRCS}:	llvm/Support/DataTypes.h \
		llvm/Config/llvm-config.h \
		llvm/Config/config.h \
		llvm/Config/Targets.def \
		llvm/Config/AsmPrinters.def \
		llvm/Config/AsmParsers.def \
		llvm/Config/Disassemblers.def

CLEANDIRS=	llvm

.PATH:		${TOP_LLVM}/${SRCDIR}

.if defined(CLANG_INCLUDES)
.PATH:		${.OBJDIR}/../../block3/clang/Headers
.endif

.include "${.CURDIR}/../../block1/Makefile.generated"

INTERNALLIB=	yes
TOP_LLVM=	${.CURDIR}/../../../contrib/llvm
CFLAGS+=	-I${.OBJDIR} \
		-I${TOP_LLVM}/include \
		-I${TOP_LLVM}/include/${HDRDIR} \
		-I${.OBJDIR}/../../block3

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

CFLAGS_AMDGPU=	-I${TOP_LLVM}/lib/Target/AMDGPU \
		-I${.OBJDIR}/../../block3/lib/Target/AMDGPU
CFLAGS_ARM=	-I${TOP_LLVM}/lib/Target/ARM \
		-I${.OBJDIR}/../../block3/lib/Target/ARM
CFLAGS_ARM64=	-I${TOP_LLVM}/lib/Target/AArch64 \
		-I${.OBJDIR}/../../block3/lib/Target/AArch64
CFLAGS_MIPS=	-I${TOP_LLVM}/lib/Target/Mips \
		-I${.OBJDIR}/../../block3/lib/Target/Mips
CFLAGS_PPC=	-I${TOP_LLVM}/lib/Target/PowerPC \
		-I${.OBJDIR}/../../block3/lib/Target/PowerPC
CFLAGS_X86=	-I${TOP_LLVM}/lib/Target/X86 \
		-I${.OBJDIR}/../../block3/lib/Target/X86
CFLAGS_CLANG=	-I${TOP_LLVM}/tools/clang/include

CLEANDIRS=	llvm clang

.PATH:		${TOP_LLVM}/${SRCDIR}

.include "${.CURDIR}/../../block1/Makefile.generated"

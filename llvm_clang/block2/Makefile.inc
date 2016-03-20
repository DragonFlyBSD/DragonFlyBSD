TOP_LLVM=	${.CURDIR}/../../../contrib/llvm
CFLAGS+=	-I${.OBJDIR} \
		-I${TOP_LLVM}/include

CFLAGS+=	-DGTEST_HAS_RTTI=0 \
		-D__STDC_CONSTANT_MACROS \
		-D__STDC_FORMAT_MACROS \
		-D__STDC_LIMIT_MACROS

CXXFLAGS+=	-std=c++11 -fno-exceptions -fno-rtti

${SRCS}:	llvm/Support/DataTypes.h \
		llvm/Config/llvm-config.h \
		llvm/Config/config.h

CLEANDIRS=	llvm

LLVM_LIBS=	${.OBJDIR}/../../block1/LLVMSupport/libLLVMSupport.a \
		${.OBJDIR}/../../block1/LLVMTableGen/libLLVMTableGen.a \
		${.OBJDIR}/../../block1/LLVMSupport/libLLVMSupport.a

LDADD=		-lpthread -lz -lm ${LLVM_LIBS}

NOMAN=		yes

.PATH:		${TOP_LLVM}/${SRCDIR}

.include "${.CURDIR}/../../block1/Makefile.generated"

install:
	# don't install the table generators

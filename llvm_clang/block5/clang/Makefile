# clang driver

PROG_CXX=	clang

BINDIR?=	/usr/libexec/clang

SRCS=	driver.cpp \
	cc1_main.cpp \
	cc1as_main.cpp

LINKS=	${BINDIR}/clang ${BINDIR}/cc \
	${BINDIR}/clang ${BINDIR}/CC \
	${BINDIR}/clang ${BINDIR}/cpp \
	${BINDIR}/clang ${BINDIR}/c++ \
	${BINDIR}/clang ${BINDIR}/clang++

# clang-specific libraries (order critical, duplicates intentional)
STLIB=	clangBasic \
	clangCodeGen \
	clangDriver \
	clangFrontend \
	clangFrontendTool \
	clangCodeGen \
	clangRewriteFrontend \
	clangARCMigrate \
	clangStaticAnalyzerFrontend \
	clangFrontend \
	clangDriver \
	clangParse \
	clangSerialization \
	clangSema \
	clangEdit \
	clangStaticAnalyzerCheckers \
	clangStaticAnalyzerCore \
	clangAnalysis \
	clangAST \
	clangRewrite \
	clangLex \
	clangBasic

# X86 Target and CPP backend first in line (order critical)

STLIB+=	LLVMX86CodeGen \
	LLVMX86AsmParser \
	LLVMX86Desc \
	LLVMX86Disassembler \
	LLVMX86Info \
	LLVMX86AsmPrinter \
	LLVMX86Utils \
	LLVMCppBackendCodeGen \
	LLVMCppBackendInfo

# Extra architecture targets (order critical)
.if defined(CLANG_EXTRA_TARGETS)
.   if ${CLANG_EXTRA_TARGETS:MAArch64}
STLIB+=	LLVMAArch64CodeGen \
	LLVMAArch64AsmParser \
	LLVMAArch64Desc \
	LLVMAArch64Disassembler \
	LLVMAArch64Info \
	LLVMAArch64AsmPrinter \
	LLVMAArch64Utils
.   endif
.   if ${CLANG_EXTRA_TARGETS:MAMDGPU}
STLIB+=	LLVMAMDGPUCodeGen \
	LLVMAMDGPUAsmParser \
	LLVMAMDGPUDesc \
	LLVMAMDGPUInfo \
	LLVMAMDGPUAsmPrinter \
	LLVMAMDGPUUtils
.   endif
.   if ${CLANG_EXTRA_TARGETS:MARM}
STLIB+=	LLVMARMCodeGen \
	LLVMARMAsmParser \
	LLVMARMDesc \
	LLVMARMDisassembler \
	LLVMARMInfo \
	LLVMARMAsmPrinter
.   endif
.   if ${CLANG_EXTRA_TARGETS:MMips}
STLIB+=	LLVMMipsCodeGen \
	LLVMMipsAsmParser \
	LLVMMipsDesc \
	LLVMMipsDisassembler \
	LLVMMipsInfo \
	LLVMMipsAsmPrinter
.   endif
.   if ${CLANG_EXTRA_TARGETS:MPowerPC}
STLIB+=	LLVMPowerPCCodeGen \
	LLVMPowerPCAsmParser \
	LLVMPowerPCDesc \
	LLVMPowerPCDisassembler \
	LLVMPowerPCInfo \
	LLVMPowerPCAsmPrinter
.   endif
.endif

# LLVM libraries (order critical, duplicates intentional)

STLIB+=	LLVMAnalysis \
	LLVMCodeGen \
	LLVMCore \
	LLVMipo \
	LLVMInstCombine \
	LLVMInstrumentation \
	LLVMMC \
	LLVMMCParser \
	LLVMObjCARCOpts \
	LLVMOption \
	LLVMScalarOpts \
	LLVMTransformUtils \
	LLVMVectorize \
	LLVMAsmPrinter \
	LLVMSelectionDAG \
	LLVMCodeGen \
	LLVMMCDisassembler \
	LLVMipo \
	LLVMVectorize \
	LLVMInstrumentation \
	LLVMObjCARCOpts \
	LLVMScalarOpts \
	LLVMInstCombine \
	LLVMTarget \
	LLVMBitWriter \
	LLVMIRReader \
	LLVMAsmParser \
	LLVMLinker \
	LLVMTransformUtils \
	LLVMAnalysis \
	LLVMProfileData \
	LLVMObject \
	LLVMMCParser \
	LLVMBitReader \
	LLVMCore \
	LLVMMC

CFLAGS+=	-I${TOP_LLVM}/tools/clang/include

.for SL in ${STLIB}
LDADD+=	${.OBJDIR}/../../block4/${SL}/lib${SL}.a
DPADD+=	${.OBJDIR}/../../block4/${SL}/lib${SL}.a
.endfor

# block 1 inclusion
LDADD+=	${.OBJDIR}/../../block1/LLVMSupport/libLLVMSupport.a
DPADD+=	${.OBJDIR}/../../block1/LLVMSupport/libLLVMSupport.a

# host library linkage
LDADD+=	-lpthread -lz -lm

SRCDIR=	tools/clang/tools/driver

.include <bsd.prog.mk>

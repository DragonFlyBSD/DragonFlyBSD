# maximum parallelism
SUBDIR_ORDERED=

SUBDIR=	LLVMAnalysis \
	LLVMAsmParser \
	LLVMAsmPrinter \
	LLVMBitReader \
	LLVMBitWriter \
	LLVMCodeGen \
	LLVMCore \
	LLVMCppBackendCodeGen \
	LLVMCppBackendInfo \
	LLVMIRReader \
	LLVMInstCombine \
	LLVMInstrumentation \
	LLVMLinker \
	LLVMMC \
	LLVMMCDisassembler \
	LLVMMCParser \
	LLVMObjCARCOpts \
	LLVMObject \
	LLVMOption \
	LLVMProfileData \
	LLVMScalarOpts \
	LLVMSelectionDAG \
	LLVMTarget \
	LLVMTransformUtils \
	LLVMVectorize \
	LLVMX86AsmParser \
	LLVMX86AsmPrinter \
	LLVMX86CodeGen \
	LLVMX86Desc \
	LLVMX86Disassembler \
	LLVMX86Info \
	LLVMX86Utils \
	LLVMipo \
	clangARCMigrate \
	clangAST \
	clangAnalysis \
	clangBasic \
	clangCodeGen \
	clangDriver \
	clangEdit \
	clangFrontend \
	clangFrontendTool \
	clangLex \
	clangParse \
	clangRewrite \
	clangRewriteFrontend \
	clangSema \
	clangSerialization \
	clangStaticAnalyzerCheckers \
	clangStaticAnalyzerCore \
	clangStaticAnalyzerFrontend

.if defined(CLANG_EXTRA_TARGETS)
.   if ${CLANG_EXTRA_TARGETS:MAArch64}
SUBDIR+=	LLVMAArch64AsmParser \
		LLVMAArch64AsmPrinter \
		LLVMAArch64CodeGen \
		LLVMAArch64Desc \
		LLVMAArch64Disassembler \
		LLVMAArch64Info \
		LLVMAArch64Utils
.   endif
.   if ${CLANG_EXTRA_TARGETS:MAMDGPU}
SUBDIR+=	LLVMAMDGPUAsmParser \
		LLVMAMDGPUAsmPrinter \
		LLVMAMDGPUCodeGen \
		LLVMAMDGPUDesc \
		LLVMAMDGPUInfo \
		LLVMAMDGPUUtils
.   endif
.   if ${CLANG_EXTRA_TARGETS:MARM}
SUBDIR+=	LLVMARMAsmParser \
		LLVMARMAsmPrinter \
		LLVMARMCodeGen \
		LLVMARMDesc \
		LLVMARMDisassembler \
		LLVMARMInfo
.   endif
.   if ${CLANG_EXTRA_TARGETS:MMips}
SUBDIR+=	LLVMMipsAsmParser \
		LLVMMipsAsmPrinter \
		LLVMMipsCodeGen \
		LLVMMipsDesc \
		LLVMMipsDisassembler \
		LLVMMipsInfo
.   endif
.   if ${CLANG_EXTRA_TARGETS:MPowerPC}
SUBDIR+=	LLVMPowerPCAsmParser \
		LLVMPowerPCAsmPrinter \
		LLVMPowerPCCodeGen \
		LLVMPowerPCDesc \
		LLVMPowerPCDisassembler \
		LLVMPowerPCInfo
.   endif
.endif

.include <bsd.subdir.mk>

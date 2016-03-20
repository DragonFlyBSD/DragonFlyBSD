# See README on how to generate llvm/clang makefiles

LLVMVER?=	38
LLVMFULLVER?=	3.8.0
PORTOBJDIR?=	/usr/obj/dports
LLVMSRCDIR?=	${PORTOBJDIR}/devel/llvm${LLVMVER}/llvm-${LLVMFULLVER}.src
SRCDIR?=	/usr/src

.if !exists (${LLVMSRCDIR})
.error Run error: ${LLVMSRCDIR} does not exist
.endif

GROUP1=	LLVMSupport:lib/Support \
	LLVMTableGen:lib/TableGen

GROUP2=	llvm-tblgen:utils/TableGen \
	clang-tblgen:tools/clang/utils/TableGen

GROUP4=	LLVMAnalysis:lib/Analysis \
	LLVMAsmParser:lib/AsmParser \
	LLVMBitReader:lib/Bitcode/Reader \
	LLVMBitWriter:lib/Bitcode/Writer \
	LLVMCodeGen:lib/CodeGen \
	LLVMAsmPrinter:lib/CodeGen/AsmPrinter \
	LLVMSelectionDAG:lib/CodeGen/SelectionDAG \
	LLVMCore:lib/IR \
	LLVMIRReader:lib/IRReader \
	LLVMMC:lib/MC \
	LLVMMCParser:lib/MC/MCParser \
	LLVMMCDisassembler:lib/MC/MCDisassembler \
	LLVMLinker:lib/Linker \
	LLVMObject:lib/Object \
	LLVMOption:lib/Option \
	LLVMProfileData:lib/ProfileData \
	LLVMTarget:lib/Target \
	LLVMCppBackendCodeGen:lib/Target/CppBackend \
	LLVMCppBackendInfo:lib/Target/CppBackend/TargetInfo \
	LLVMipo:lib/Transforms/IPO \
	LLVMInstCombine:lib/Transforms/InstCombine \
	LLVMInstrumentation:lib/Transforms/Instrumentation \
	LLVMObjCARCOpts:lib/Transforms/ObjCARC \
	LLVMScalarOpts:lib/Transforms/Scalar \
	LLVMTransformUtils:lib/Transforms/Utils \
	LLVMVectorize:lib/Transforms/Vectorize \
	${X86} ${ARM} ${MIPS} ${AMDGPU} ${ARM64} ${PPC} ${CLANG}

X86=	LLVMX86CodeGen:lib/Target/X86 \
	LLVMX86Disassembler:lib/Target/X86/Disassembler \
	LLVMX86AsmParser:lib/Target/X86/AsmParser \
	LLVMX86AsmPrinter:lib/Target/X86/InstPrinter \
	LLVMX86Desc:lib/Target/X86/MCTargetDesc \
	LLVMX86Info:lib/Target/X86/TargetInfo \
	LLVMX86Utils:lib/Target/X86/Utils

ARM=	LLVMARMCodeGen:lib/Target/ARM \
	LLVMARMAsmParser:lib/Target/ARM/AsmParser \
	LLVMARMDisassembler:lib/Target/ARM/Disassembler \
	LLVMARMAsmPrinter:lib/Target/ARM/InstPrinter \
	LLVMARMDesc:lib/Target/ARM/MCTargetDesc \
	LLVMARMInfo:lib/Target/ARM/TargetInfo

MIPS=	LLVMMipsCodeGen:lib/Target/Mips \
	LLVMMipsAsmParser:lib/Target/Mips/AsmParser \
	LLVMMipsDisassembler:lib/Target/Mips/Disassembler \
	LLVMMipsAsmPrinter:lib/Target/Mips/InstPrinter \
	LLVMMipsDesc:lib/Target/Mips/MCTargetDesc \
	LLVMMipsInfo:lib/Target/Mips/TargetInfo

AMDGPU=	LLVMAMDGPUCodeGen:lib/Target/AMDGPU \
	LLVMAMDGPUAsmParser:lib/Target/AMDGPU/AsmParser \
	LLVMAMDGPUAsmPrinter:lib/Target/AMDGPU/InstPrinter \
	LLVMAMDGPUDesc:lib/Target/AMDGPU/MCTargetDesc \
	LLVMAMDGPUInfo:lib/Target/AMDGPU/TargetInfo \
	LLVMAMDGPUUtils:lib/Target/AMDGPU/Utils

ARM64=	LLVMAArch64CodeGen:lib/Target/AArch64 \
	LLVMAArch64AsmParser:lib/Target/AArch64/AsmParser \
	LLVMAArch64Disassembler:lib/Target/AArch64/Disassembler \
	LLVMAArch64AsmPrinter:lib/Target/AArch64/InstPrinter \
	LLVMAArch64Desc:lib/Target/AArch64/MCTargetDesc \
	LLVMAArch64Info:lib/Target/AArch64/TargetInfo \
	LLVMAArch64Utils:lib/Target/AArch64/Utils

PPC=	LLVMPowerPCCodeGen:lib/Target/PowerPC \
	LLVMPowerPCAsmParser:lib/Target/PowerPC/AsmParser \
	LLVMPowerPCDisassembler:lib/Target/PowerPC/Disassembler \
	LLVMPowerPCAsmPrinter:lib/Target/PowerPC/InstPrinter \
	LLVMPowerPCDesc:lib/Target/PowerPC/MCTargetDesc \
	LLVMPowerPCInfo:lib/Target/PowerPC/TargetInfo

CLANG=	clangARCMigrate:tools/clang/lib/ARCMigrate \
	clangAST:tools/clang/lib/AST \
	clangAnalysis:tools/clang/lib/Analysis \
	clangBasic:tools/clang/lib/Basic \
	clangCodeGen:tools/clang/lib/CodeGen \
	clangDriver:tools/clang/lib/Driver \
	clangEdit:tools/clang/lib/Edit \
	clangFrontend:tools/clang/lib/Frontend \
	clangFrontendTool:tools/clang/lib/FrontendTool \
	clangLex:tools/clang/lib/Lex \
	clangParse:tools/clang/lib/Parse \
	clangRewrite:tools/clang/lib/Rewrite \
	clangRewriteFrontend:tools/clang/lib/Frontend/Rewrite \
	clangSema:tools/clang/lib/Sema \
	clangSerialization:tools/clang/lib/Serialization \
	clangStaticAnalyzerFrontend:tools/clang/lib/StaticAnalyzer/Frontend \
	clangStaticAnalyzerCheckers:tools/clang/lib/StaticAnalyzer/Checkers \
	clangStaticAnalyzerCore:tools/clang/lib/StaticAnalyzer/Core

LIST=	${GROUP1} ${GROUP4}

OUTPUT=	generated
HDR=	includes/Makefile.sources

.PATH:	${LLVMSRCDIR}

all:	${LIST:C|:.*$|/Makefile.sources|} \
	${GROUP2:C|:.*$|/Makefile.sources|} \
	${HDR}

.for X in ${LIST}
${X:C|:.*$|/Makefile.sources|}: ${X:C/^.*://}/CMakeLists.txt
	mkdir -p ${OUTPUT}/${.TARGET:H}
	awk -v SRCDIR="${X:C/^.*://}" -f scrape_library.awk ${.ALLSRC} \
		> ${OUTPUT}/${.TARGET}
.endfor

.for X in ${GROUP2}
${X:C|:.*$|/Makefile.sources|}: ${X:C/^.*://}/CMakeLists.txt
	mkdir -p ${OUTPUT}/${.TARGET:H}
	awk -v SRCDIR="${X:C/^.*://}" -f scrape_tablegen.awk ${.ALLSRC} \
		> ${OUTPUT}/${.TARGET}
.endfor

${HDR}: tools/clang/lib/Headers/CMakeLists.txt
	mkdir -p ${OUTPUT}/${.TARGET:H}
	awk -v SRCDIR="tools/clang/lib/Headers" -f scrape_headers.awk ${.ALLSRC} \
		> ${OUTPUT}/${.TARGET}

clean:
	rm -rf ${.OBJDIR}/${OUTPUT}

install:
	mkdir -p ${SRCDIR}/llvm_clang/block5/include
	cp ${OUTPUT}/${HDR} ${SRCDIR}/llvm_clang/block5/include
.for X in ${GROUP1:C|:.*$||}
	@echo ${X}
	mkdir -p ${SRCDIR}/llvm_clang/block1/${X}
	cp ${OUTPUT}/${X}/Makefile.sources ${SRCDIR}/llvm_clang/block1/${X}
.endfor
.for X in ${GROUP2:C|:.*$||}
	@echo ${X}
	mkdir -p ${SRCDIR}/llvm_clang/block2/${X}
	cp ${OUTPUT}/${X}/Makefile.sources ${SRCDIR}/llvm_clang/block2/${X}
.endfor
.for X in ${GROUP4:C|:.*$||}
	@echo ${X}
	mkdir -p ${SRCDIR}/llvm_clang/block4/${X}
	cp ${OUTPUT}/${X}/Makefile.sources ${SRCDIR}/llvm_clang/block4/${X}
.endfor

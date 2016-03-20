# Static makefile
beforedepend: libLLVMSupport.a
.include "${.CURDIR}/Makefile.sources"

${SRCS}:	llvm/Config/Targets.def \
		llvm/Config/AsmPrinters.def \
		llvm/Config/AsmParsers.def \
		llvm/Config/Disassemblers.def

.include <bsd.lib.mk>

# Static makefile
.include "${.CURDIR}/Makefile.sources"
CFLAGS+= ${CFLAGS_CLANG}
CGEN=   ${.OBJDIR}/../../block2/clang-tblgen/clang-tblgen
CXAST=	-I${TOP_LLVM}/tools/clang/include \
        -I${TOP_LLVM}/tools/clang/lib/StaticAnalyzer/Checkers \
        -I${TOP_LLVM}/lib/Target \
        -I${TOP_LLVM}/include

AnalyzerStatsChecker.cpp: Checkers.inc

Checkers.inc: Checkers.td
	${CGEN} -gen-clang-sa-checkers ${CXAST} ${.ALLSRC} -o ${.TARGET}

CLEANFILES=	Checkers.inc

.include <bsd.lib.mk>

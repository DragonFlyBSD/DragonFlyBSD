# $FreeBSD: head/lib/libexecinfo/Makefile 275024 2014-11-25 11:07:26Z bapt $

LIB=		execinfo
SHLIB_MAJOR=	1

INCS=		execinfo.h
SRCS=		backtrace.c symtab.c unwind.c
MAN=		backtrace.3

MLINKS+=	backtrace.3 backtrace_symbols.3
MLINKS+=	backtrace.3 backtrace_symbols_fmt.3
MLINKS+=	backtrace.3 backtrace_symbols_fd.3
MLINKS+=	backtrace.3 backtrace_symbols_fd_fmt.3

CONLIBS=	${.OBJDIR}/../libelf/libelf.a
DPADD=		${CONLIBS:.a=_pic.a}
LDADD=		${CONLIBS:.a=_pic.a}
ARADD=		${CONLIBS}

TOOLCHAINDIR=	${.CURDIR}/../../contrib/elftoolchain
CFLAGS+=	-I${TOOLCHAINDIR}/libelf -I${TOOLCHAINDIR}/common

# Override bsd.lib.mk scripts
lib${LIB}.a: ${SRCS:.c=.o} script.ar
	@${ECHO} building static ${LIB} library via script
	rm -f ${.TARGET}
	${AR} -M < script.ar
	${RANLIB} ${.TARGET}

lib${LIB}_p.a: ${SRCS:.c=.po} script_p.ar
	@${ECHO} building profiled ${LIB} library via script
	rm -f ${.TARGET}
	${AR} -M < script_p.ar
	${RANLIB} ${.TARGET}

script.ar:
	@echo "CREATE lib${LIB}.a"	>  ${.TARGET}
.for arc in ${CONLIBS}
	@echo "ADDLIB ${arc}"		>> ${.TARGET}
.endfor
.for obj in ${SRCS:.c=.o}
	@echo "ADDMOD ${obj}"		>> ${.TARGET}
.endfor
	@echo "SAVE"			>> ${.TARGET}
	@echo "END"			>> ${.TARGET}

script_p.ar:
	@echo "CREATE lib${LIB}_p.a"	>  ${.TARGET}
.for arc in ${CONLIBS:.a=_p.a}
	@echo "ADDLIB ${arc}"		>> ${.TARGET}
.endfor
.for obj in ${SRCS:.c=.po}
	@echo "ADDMOD ${obj}"		>> ${.TARGET}
.endfor
	@echo "SAVE"			>> ${.TARGET}
	@echo "END"			>> ${.TARGET}

GENFILES=	script.ar script_p.ar
CLEANFILES=	${GENFILES}
beforedepend:	${GENFILES}

.include <bsd.lib.mk>

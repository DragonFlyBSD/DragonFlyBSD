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

DPADD=		${.OBJDIR}/../libelf/libelf_pic.a
LDADD=		-L${.OBJDIR}/../libelf -lelf_pic
ARADD=		${.OBJDIR}/../libelf/libelf.a

TOOLCHAINDIR=	${.CURDIR}/../../contrib/elftoolchain
CFLAGS+=	-I${TOOLCHAINDIR}/libelf -I${TOOLCHAINDIR}/common

.include <bsd.lib.mk>

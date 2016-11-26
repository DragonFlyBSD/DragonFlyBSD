#	@(#)Makefile.inc	8.1 (Berkeley) 6/2/93
# $FreeBSD: src/lib/libc/compat-43/Makefile.inc,v 1.9.2.1 2001/04/25 10:04:04 ru Exp $

# compat-43 sources
.PATH: ${.CURDIR}/../libc/compat-43

SRCS+=	sethostid.c setpgrp.c setrgid.c setruid.c sigcompat.c

.if ${LIB} == "c"
MAN+=	sigblock.3 sigpause.3 sigsetmask.3 sigvec.3 setruid.3

MLINKS+=setruid.3 setrgid.3
.endif

CMAPS+=	${.CURDIR}/compat-43/Symbol.map

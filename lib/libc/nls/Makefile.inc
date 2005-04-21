#	from $NetBSD: Makefile.inc,v 1.7 1995/02/27 13:06:20 cgd Exp $
# $FreeBSD: src/lib/libc/nls/Makefile.inc,v 1.5.2.2 2001/04/25 10:04:07 ru Exp $
# $DragonFly: src/lib/libc/nls/Makefile.inc,v 1.3 2005/04/21 16:36:34 joerg Exp $

.PATH: ${.CURDIR}/../libc/iconv
.PATH: ${.CURDIR}/../libc/nls

SRCS+=	catclose.c catgets.c catopen.c

.if ${LIB} == "c"
MAN+=	catclose.3 catgets.3 catopen.3
.endif

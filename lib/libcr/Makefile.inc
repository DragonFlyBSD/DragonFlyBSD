# $FreeBSD: src/lib/libc/Makefile.inc,v 1.5 1999/08/27 23:58:06 peter Exp $
# $DragonFly: src/lib/libcr/Attic/Makefile.inc,v 1.3 2003/07/24 21:41:43 dillon Exp $
#
# This file contains make rules that are shared by libc and libc_r.
#
# Define (empty) variables so that make doesn't give substitution
# errors if the included makefiles don't change these:
MDSRCS=
MISRCS=
MDASM=
MIASM=
NOASM=

#
# If there is a machine dependent makefile, use it:
#
.if exists(${.CURDIR}/../libcr/${MACHINE_ARCH}/Makefile.inc)
.include "${.CURDIR}/../libcr/${MACHINE_ARCH}/Makefile.inc"
.endif

.include "${.CURDIR}/../libcr/db/Makefile.inc"
.include "${.CURDIR}/../libcr/compat-43/Makefile.inc"
.include "${.CURDIR}/../libcr/gen/Makefile.inc"
.include "${.CURDIR}/../libcr/gmon/Makefile.inc"
.include "${.CURDIR}/../libcr/locale/Makefile.inc"
.include "${.CURDIR}/../libcr/net/Makefile.inc"
.include "${.CURDIR}/../libcr/nls/Makefile.inc"
.if !defined(NO_QUAD)
.include "${.CURDIR}/../libcr/quad/Makefile.inc"
.endif
.include "${.CURDIR}/../libcr/regex/Makefile.inc"
.include "${.CURDIR}/../libcr/stdio/Makefile.inc"
.include "${.CURDIR}/../libcr/stdlib/Makefile.inc"
.include "${.CURDIR}/../libcr/stdtime/Makefile.inc"
.include "${.CURDIR}/../libcr/string/Makefile.inc"
.include "${.CURDIR}/../libcr/sys/Makefile.inc"
.include "${.CURDIR}/../libcr/rpc/Makefile.inc"
.include "${.CURDIR}/../libcr/xdr/Makefile.inc"
.if !defined(NO_YP_LIBC)
CFLAGS+= -DYP
.include "${.CURDIR}/../libcr/yp/Makefile.inc"
.endif

# If there are no machine dependent sources, append all the
# machine-independent sources:
.if empty(MDSRCS)
SRCS+=	${MISRCS}
.else
# Append machine-dependent sources, then append machine-independent sources
# for which there is no machine-dependent variant.
SRCS+=	${MDSRCS}
.for _src in ${MISRCS}
.if ${MDSRCS:R:M${_src:R}} == ""
SRCS+=	${_src}
.endif
.endfor
.endif

# $FreeBSD: src/share/mk/bsd.init.mk,v 1.1.2.1 2002/07/17 19:08:23 ru Exp $
# $DragonFly: src/share/mk/bsd.init.mk,v 1.6 2004/03/22 20:58:15 dillon Exp $

# The include file <bsd.init.mk> includes ../Makefile.inc and
# <bsd.own.mk>; this is used at the top of all <bsd.*.mk> files
# that actually "build something".

.if !target(__<bsd.init.mk>__)
__<bsd.init.mk>__:
.if exists(${.CURDIR}/../Makefile.inc)
.include "${.CURDIR}/../Makefile.inc"
.endif
.if ${CCVER} != ${_CCVER}
.include <bsd.cpu.mk>
.endif
.include <bsd.own.mk>
.MAIN: all

.endif !target(__<bsd.init.mk>__)

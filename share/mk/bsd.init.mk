# $FreeBSD: src/share/mk/bsd.init.mk,v 1.1.2.1 2002/07/17 19:08:23 ru Exp $
# $DragonFly: src/share/mk/bsd.init.mk,v 1.3 2004/01/26 19:09:49 joerg Exp $

# The include file <bsd.init.mk> includes ../Makefile.inc and
# <bsd.own.mk>; this is used at the top of all <bsd.*.mk> files
# that actually "build something".

.if target(__<bsd.init.mk>__) && !target(__<bsd.init.mk>__2)
__<bsd.init.mk>__2:
.if exists(${.CURDIR}/../Makefile.sub)
.include "${.CURDIR}/../Makefile.sub"
.endif
.endif


.if !target(__<bsd.init.mk>__)
__<bsd.init.mk>__:
.if exists(${.CURDIR}/../Makefile.inc)
.include "${.CURDIR}/../Makefile.inc"
.endif
.include <bsd.own.mk>
.MAIN: all
.endif !target(__<bsd.init.mk>__)

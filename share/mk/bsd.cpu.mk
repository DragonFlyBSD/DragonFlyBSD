# $FreeBSD: src/share/mk/bsd.cpu.mk,v 1.2.2.5 2002/07/19 08:09:32 ru Exp $
# $DragonFly: src/share/mk/bsd.cpu.mk,v 1.3 2004/01/26 15:22:08 joerg Exp $

# include compiler-specific bsd.cpu.mk

.if !defined(CCVER) || ${CCVER} == "gcc2"
.  include <bsd.cpu.gcc2.mk>
.elif ${CCVER} == "gcc3"
.  include <bsd.cpu.gcc3.mk>
.elif defined(CCVER_BSD_CPU_MK)
.  if ${CCVER_BSD_CPU_MK} != ""
.    include "${CCVER_BSD_CPU_MK}"
.  endif

.else

.error "Either set CCVER to a known compiler or specify CCVER_BSD_CPU_MK"

.endif


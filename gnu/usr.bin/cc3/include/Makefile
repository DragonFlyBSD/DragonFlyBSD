# $FreeBSD: src/gnu/usr.bin/cc/include/Makefile,v 1.6 2003/12/09 16:31:30 kan Exp $
# $DragonFly: src/gnu/usr.bin/cc3/include/Attic/Makefile,v 1.1 2004/01/17 22:40:16 drhodus Exp $

.include "../Makefile.inc"

.PATH: ${GCCDIR}/config/${GCC_CPU}

.if ${TARGET_ARCH} == "i386" || ${TARGET_ARCH} == "amd64"
INCS=	emmintrin.h mmintrin.h pmmintrin.h xmmintrin.h
.elif ${TARGET_ARCH} == "ia64"
INCS=	ia64intrin.h
.endif

NOOBJ=

.include <bsd.init.mk>
.include <bsd.incs.mk>
.include <bsd.obj.mk>

# $FreeBSD: src/gnu/usr.bin/binutils/Makefile,v 1.11.2.5 2002/09/01 23:39:12 obrien Exp $
# $DragonFly: src/gnu/usr.bin/binutils/Attic/Makefile,v 1.3 2004/01/31 06:56:36 dillon Exp $

SUBDIR=		libiberty libbfd libopcodes libbinutils \
		addr2line ar as gasp ld nm objcopy objdump ranlib readelf \
		size strings strip doc

TARGET_ARCH?=	${MACHINE_ARCH}
.if (${TARGET_ARCH} == "alpha" || ${TARGET_ARCH} == "i386") \
    && !defined(NO_GDB)
SUBDIR+=	gdb gdbreplay
.endif

.include <bsd.subdir.mk>

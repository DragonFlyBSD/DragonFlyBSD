# $FreeBSD: src/gnu/usr.bin/binutils/Makefile,v 1.11.2.5 2002/09/01 23:39:12 obrien Exp $
# $DragonFly: src/gnu/usr.bin/binutils/Attic/Makefile,v 1.4 2004/03/22 20:57:38 dillon Exp $

SUBDIR=		libiberty libbfd libopcodes libbinutils \
		addr2line ar as gasp ld nm objcopy objdump ranlib readelf \
		size strings strip doc

TARGET_ARCH?=	${MACHINE_ARCH}
.if (${TARGET_ARCH} == "alpha" || ${TARGET_ARCH} == "i386") \
    && !defined(NO_GDB)
SUBDIR+=	gdb gdbreplay
.endif

# interdependant header files
.ORDER: ${SUBDIR}

.include <bsd.subdir.mk>

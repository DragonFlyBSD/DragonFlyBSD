# $FreeBSD: src/gnu/usr.bin/binutils/Makefile,v 1.11.2.5 2002/09/01 23:39:12 obrien Exp $
# $DragonFly: src/gnu/usr.bin/binutils/Attic/Makefile,v 1.5 2005/03/15 01:54:28 joerg Exp $

SUBDIR=		libiberty libbfd libopcodes libbinutils \
		addr2line ar as gasp ld nm objcopy objdump ranlib readelf \
		size strings strip doc

TARGET_ARCH?=	${MACHINE_ARCH}

# interdependant header files
.ORDER: ${SUBDIR}

.include <bsd.subdir.mk>

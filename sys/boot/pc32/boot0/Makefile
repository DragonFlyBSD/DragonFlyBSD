# $FreeBSD: src/sys/boot/i386/boot0/Makefile,v 1.17 2002/09/17 01:48:54 peter Exp $
# $DragonFly: src/sys/boot/pc32/boot0/Makefile,v 1.5 2004/07/19 23:30:31 dillon Exp $

PROG=		boot0
NOMAN=
STRIP=
BINDIR?=	/boot
BINMODE=	444

.PATH: ${.CURDIR}/..

# The default set of flags compiled into boot0.  This enables update (writing
# the modified boot0 back to disk after running so that the selection made is
# saved), packet mode (detect and use the BIOS EDD extensions if we try to
# boot past the 1024 cylinder liimt), and booting from all valid slices.
BOOT_BOOT0_FLAGS?=	0xf

# The number of timer ticks to wait for a keypress before assuming the default
# selection.  Since there are 18.2 ticks per second, the default value of
# 0xb6 (182d) corresponds to 10 seconds.
BOOT_BOOT0_TICKS?=	0xb6

# pull BOOT0_ORIGIN out of bootasm.h
#
#
boot0: boot0.o bootasmdef.nx
	@echo "boot0 origin set to `${.OBJDIR}/bootasmdef.nx BOOT0_ORIGIN`"
	${LD} -N -e start -Ttext `${.OBJDIR}/bootasmdef.nx BOOT0_ORIGIN` -o boot0.out boot0.o
	objcopy -S -O binary boot0.out ${.TARGET}

boot0.o: boot0.S
	${CC} -c -DFLAGS=${BOOT_BOOT0_FLAGS} -DTICKS=${BOOT_BOOT0_TICKS} \
		${AFLAGS} ${.IMPSRC} -o ${.TARGET}

CLEANFILES+= boot0.out boot0.o bootasmdef.nx

.include <bsd.prog.mk>

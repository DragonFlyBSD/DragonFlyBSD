# $FreeBSD: src/sys/boot/i386/boot0/Makefile,v 1.17 2002/09/17 01:48:54 peter Exp $
# $DragonFly: src/sys/boot/i386/bootn0/Attic/Makefile,v 1.1 2004/06/26 02:00:11 dillon Exp $

PROG=		boot0
NOMAN=
STRIP=
BINDIR?=	/boot
BINMODE=	444

# The default set of flags compiled into boot0.  This enables update (writing
# the modified boot0 back to disk after running so that the selection made is
# saved), packet mode (detect and use the BIOS EDD extensions if we try to
# boot past the 1024 cylinder liimt), and booting from all valid slices.
BOOT_BOOT0_FLAGS?=	0xf

# The number of timer ticks to wait for a keypress before assuming the default
# selection.  Since there are 18.2 ticks per second, the default value of
# 0xb6 (182d) corresponds to 10 seconds.
BOOT_BOOT0_TICKS?=	0xb6

# We relocate boot0 to this address, with the stack heading downward and 
# the contents of boot0 heading upward.
BOOT_BOOT0_ORG?=	0x91000

boot0: boot0.o
	${LD} -N -e start -Ttext ${BOOT_BOOT0_ORG} -o boot0.out boot0.o
	objcopy -S -O binary boot0.out ${.TARGET}

boot0.o: boot0.S
	${CC} -E ${.IMPSRC} | \
	${AS} ${AFLAGS} --defsym FLAGS=${BOOT_BOOT0_FLAGS} \
		--defsym TICKS=${BOOT_BOOT0_TICKS} -o ${.TARGET}

CLEANFILES+= boot0.out boot0.o

.include <bsd.prog.mk>

DEFAULT_ARCH=		i386
EMULATIONS=		&i386elf,
DEFAULT_EMULATION=	i386elf
TARGET_ALIAS=		i386-just-dragonfly
TARGET_CANONICAL=	i386-just-dragonfly
TARGET_CPU=		i386

.include "${.CURDIR}/../Makefile.inc0"

CFLAGS += -I${.OBJDIR}/../../libbfd
.if ${TARGET_ARCH} != "i386"
BINDIR=	/usr/libexec/cross/i386-dragonfly
.endif
SRCS+=	obj-elf.c tc-i386.c atof-ieee.c

.include <bsd.prog.mk>

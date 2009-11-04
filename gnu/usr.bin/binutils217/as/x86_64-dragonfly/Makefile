DEFAULT_ARCH=           x86_64 
EMULATIONS=             &i386elf,
DEFAULT_EMULATION=      i386elf 
TARGET_ALIAS=           x86_64-just-dragonfly1.9
TARGET_CANONICAL=       x86_64-just-dragonfly1.9
TARGET_CPU=             x86_64 

.include "${.CURDIR}/../Makefile.inc0"

CFLAGS += -I${.OBJDIR}/../../libbfd
.if ${TARGET_ARCH} != "x86_64"
BINDIR=	/usr/libexec/cross/x86_64-dragonfly
.endif
SRCS+=	obj-elf.c tc-i386-local.c
CLEANFILES=	tc-i386-local.c

.include <bsd.prog.mk>


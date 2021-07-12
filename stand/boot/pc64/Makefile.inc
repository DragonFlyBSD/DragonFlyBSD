.if !target(__pc64__)
__pc64__:

.MAKEFLAGS:  MACHINE_ARCH=i386 MACHINE=i386 REALLY_X86_64=true

# Disable stack protector
CFLAGS+=		-fno-stack-protector

CFLAGS+=		-m32
LDFLAGS+=		-m elf_i386
COMPILER_LDFLAGS+=	-Wl,-m,elf_i386
AFLAGS+=		--32

.include "../pc32/Makefile.inc"
.endif

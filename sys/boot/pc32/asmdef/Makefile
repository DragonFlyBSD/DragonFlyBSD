.PATH: ${.CURDIR}/..

PROG=	bootasmdef

CFLAGS+= -I${.CURDIR}/..

build-tools: bootasmdef.nx

# this bootasmdef is used in place, it is not installed anywhere
install:

# hint for pc32/Makefile.inc
__no_pc32_defines=

.include <bsd.hostprog.mk>

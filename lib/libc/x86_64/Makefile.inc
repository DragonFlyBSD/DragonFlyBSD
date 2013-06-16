# $FreeBSD: src/lib/libc/amd64/Makefile.inc,v 1.6 2007/12/03 07:17:32 das Exp $
#
# Machine dependent definitions for the x86_64 architecture.
#

CMAPS+=	${.CURDIR}/x86_64/Symbol.map

# Long double is 80 bits
GDTOASRCS+=strtorx.c
MDSRCS+=machdep_ldisx.c

# $FreeBSD: src/lib/libc/amd64/stdlib/gdtoa.mk,v 1.1 2003/03/12 20:29:59 das Exp $
# $DragonFly: src/lib/libc/amd64/stdlib/gdtoa.mk,v 1.1 2004/02/02 05:43:14 dillon Exp $

# Long double is 80 bits
GDTOASRCS+=strtopx.c
MDSRCS+=machdep_ldisx.c

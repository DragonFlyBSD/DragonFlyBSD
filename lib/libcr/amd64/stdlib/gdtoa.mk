# $FreeBSD: src/lib/libc/amd64/stdlib/gdtoa.mk,v 1.1 2003/03/12 20:29:59 das Exp $
# $DragonFly: src/lib/libcr/amd64/stdlib/Attic/gdtoa.mk,v 1.1 2004/03/13 19:46:55 eirikn Exp $

# Long double is 80 bits
GDTOASRCS+=strtopx.c
MDSRCS+=machdep_ldisx.c

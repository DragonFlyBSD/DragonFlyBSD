# $FreeBSD: src/lib/libc/alpha/Makefile.inc,v 1.5 1999/08/27 23:58:07 peter Exp $
# $DragonFly: src/lib/libcr/alpha/Attic/Makefile.inc,v 1.2 2003/06/17 04:26:41 dillon Exp $
#
# Machine dependent definitions for the alpha architecture.
#

#
# Alpha is 64-bit, so it doesn't need quad functions:
#
NO_QUAD=1

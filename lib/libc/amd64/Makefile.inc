# $FreeBSD: src/lib/libc/amd64/Makefile.inc,v 1.1 2003/07/22 06:34:57 peter Exp $
# $DragonFly: src/lib/libc/amd64/Makefile.inc,v 1.1 2004/02/02 05:43:14 dillon Exp $
#
# Machine dependent definitions for the amd64 architecture.
#

#
# AMD64 is 64-bit, so it doesn't need quad functions:
#
NO_QUAD=1

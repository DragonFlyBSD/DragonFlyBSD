# $FreeBSD$
# $DragonFly: src/sys/boot/ia64/Makefile.inc,v 1.1 2003/11/10 06:08:37 dillon Exp $

# Options used when building standalone components
CFLAGS+=	-ffreestanding -fshort-wchar -Wformat

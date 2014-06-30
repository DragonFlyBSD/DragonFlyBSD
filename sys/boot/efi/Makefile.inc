# $FreeBSD: src/sys/boot/efi/Makefile.inc,v 1.6 2002/12/10 04:20:15 marcel Exp $
# $DragonFly: src/sys/boot/efi/Makefile.inc,v 1.1 2003/11/10 06:08:32 dillon Exp $

# Options used when building app-specific efi components
CFLAGS+=	-ffreestanding -fshort-wchar -Wformat
CFLAGS+=        -mno-mmx -mno-3dnow -mno-sse -mno-sse2 -mno-sse3 -msoft-float

# $FreeBSD: head/etc/devd/Makefile 261894 2014-02-14 15:31:48Z avg $

FILES=	uath.conf usb.conf

.if ${MACHINE_ARCH} == "x86_64" || ${MACHINE_ARCH} == "i386"
FILES+=	asus.conf
.endif

NOOBJ=
FILESDIR=	/etc/devd
FILESMODE=	644

.include <bsd.prog.mk>

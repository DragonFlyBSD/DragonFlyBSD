# $FreeBSD: head/etc/devd/Makefile 261894 2014-02-14 15:31:48Z avg $

FILES=	uath.conf usb.conf

.if ${MACHINE} == "amd64" || ${MACHINE} == "i386"
FILES+=	asus.conf
.endif

NOOBJ=
FILESDIR=	/etc/devd
FILESMODE=	644

.include <bsd.prog.mk>

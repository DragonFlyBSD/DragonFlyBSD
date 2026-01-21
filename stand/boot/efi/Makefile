# $FreeBSD: head/sys/boot/efi/Makefile 296892 2016-03-15 05:17:31Z imp $

.if ${MACHINE_ARCH} == "x86_64"
SUBDIR+=	libefi loader boot1
.elif ${MACHINE_ARCH} == "aarch64"
SUBDIR+=	libefi loader
.endif

.include <bsd.subdir.mk>

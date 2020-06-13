# $FreeBSD: head/usr.sbin/efibootmgr/Makefile 326725 2017-12-09 06:52:20Z imp $
EFIBOOT=${.CURDIR}/../../sys/boot/efi
EFIINCL=${.CURDIR}/../../sys/boot/efi/include
EFIVAR=${.CURDIR}/../efivar
.PATH: ${EFIBOOT}/libefi ${EFIVAR}
CFLAGS+= -I${EFIVAR} -I${EFIINCL}
CFLAGS+= -I${.CURDIR}/../../lib/libefivar

PROG=	efibootmgr
MAN=	efibootmgr.8

LDADD=	-lefivar
DPADD=	${LIBEFIVAR}

.include <bsd.prog.mk>

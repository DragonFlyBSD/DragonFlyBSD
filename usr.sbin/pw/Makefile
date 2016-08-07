# $FreeBSD: src/usr.sbin/pw/Makefile,v 1.11.2.3 2001/06/21 02:16:32 dd Exp $

PROG=	pw
SRCS=	pw.c pw_conf.c pw_user.c pw_group.c pw_log.c pw_nis.c pw_vpw.c \
	grupd.c pwupd.c fileupd.c edgroup.c psdate.c \
	bitmap.c cpdir.c rm_r.c
WARNS?=	2

MAN=	pw.conf.5 pw.8

LDADD=	-lcrypt -lutil
DPADD=	${LIBCRYPT} ${LIBUTIL}
NO_STRICT_OVERFLOW=

.include <bsd.prog.mk>

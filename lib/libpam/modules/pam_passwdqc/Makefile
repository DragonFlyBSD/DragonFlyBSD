# $FreeBSD: head/lib/libpam/modules/pam_passwdqc/Makefile 312452 2017-01-20 03:27:47Z ngie $

CONTRIBDIR= ${.CURDIR}/../../../../contrib/pam_passwdqc
.PATH: ${CONTRIBDIR}

LIB=	pam_passwdqc
SRCS=	pam_passwdqc.c passwdqc_check.c passwdqc_filter.c passwdqc_load.c \
	passwdqc_memzero.c passwdqc_parse.c passwdqc_random.c wordset_4k.c
SRCS+=	concat.c md4.c
MAN=	passwdqc.conf.5 pam_passwdqc.8

CFLAGS+= -I${CONTRIBDIR}
WARNS?=	3

DPADD=	${LIBCRYPT}
LDADD=	-lcrypt

.include <bsd.lib.mk>

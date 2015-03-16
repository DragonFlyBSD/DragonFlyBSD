CONTRIBDIR= ${.CURDIR}/../../../contrib/pam_passwdqc
.PATH: ${CONTRIBDIR}

LIB=	pam_passwdqc
SRCS=	pam_passwdqc.c passwdqc_check.c passwdqc_random.c wordset_4k.c
MAN=	pam_passwdqc.8

CFLAGS+= -I${CONTRIBDIR}
WARNS?=	3

DPADD=	${LIBCRYPT}
LDADD=	-lcrypt

.include <bsd.lib.mk>

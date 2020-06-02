PROG=	automount
SRCS=	automount.c automountd.c autounmountd.c common.c \
	defined.c log.c popen.c token.l

CFLAGS+= -I${.CURDIR} -I${.CURDIR}/../../sys

MAN=	automount.8 automountd.8 autounmountd.8 auto_master.5

DPADD=	${LIBUTIL}
LDADD=	-lutil

LINKS=	${BINDIR}/automount ${BINDIR}/automountd
LINKS+=	${BINDIR}/automount ${BINDIR}/autounmountd

FILES=	auto_master
FILESDIR=	/etc
FILESMODE=	644

SUBDIR=	autofs

.include <bsd.prog.mk>

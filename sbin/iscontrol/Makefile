# $FreeBSD$

SRCS= iscontrol.c pdu.c fsm.c config.c login.c auth_subr.c misc.c
PROG= iscontrol
DPADD= ${LIBCAM} ${LIBMD} ${LIBSBUF}
LDADD= -lcam -lmd -lsbuf

CFLAGS += -I${.CURDIR}/../../sys/dev/disk/iscsi/initiator
#CFLAGS += -g -DDEBUG

MAN= iscsi.conf.5 iscontrol.8

WARNS?= 3
.include <bsd.prog.mk>

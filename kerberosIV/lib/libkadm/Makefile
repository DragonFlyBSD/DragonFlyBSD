# $FreeBSD: src/kerberosIV/lib/libkadm/Makefile,v 1.5.2.1 2002/07/22 14:21:48 ru Exp $
# $DragonFly: src/kerberosIV/lib/libkadm/Attic/Makefile,v 1.2 2003/06/17 04:26:25 dillon Exp $

LIB=		kadm
CFLAGS+=	-I${KRB4DIR}/include \
		-I${INCLUDEOBJDIR} \
		-I${KRB4DIR}/lib/krb \
		-I${KRB4DIR}/lib/kdb \
		-I${KRB4DIR}/lib/kadm \
		-I${KRB4DIR}/lib/roken \
		-I${KRBOBJDIR} \
		-I${KADMOBJDIR} \
		-I${.OBJDIR}
SRCS=		kadm_cli_wrap.c kadm_stream.c kadm_supp.c \
		${KADMOBJDIR}/kadm_err.c ${KADMOBJDIR}/kadm_err.h \
		${KRBOBJDIR}/krb_err.h check_password.c protos.h
INCS=		kadm.h ${KADMOBJDIR}/kadm_err.h

.include <bsd.lib.mk>

.PATH:		${KRB4DIR}/lib/kadm

CLEANFILES+=	protos.h

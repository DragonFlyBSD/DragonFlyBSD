# $FreeBSD: src/kerberos5/lib/libtelnet/Makefile,v 1.14.2.4 2003/04/24 19:13:59 nectar Exp $
# $DragonFly: src/kerberos5/lib/libtelnet/Attic/Makefile,v 1.2 2003/06/17 04:26:17 dillon Exp $

LIB=		telnet

INTERNALLIB=	yes

SRCS=		genget.c getent.c misc.c encrypt.c auth.c \
		enc_des.c sra.c pk.c
.if defined(MAKE_KERBEROS4)
SRCS+=		kerberos.c
CFLAGS+=	-DKRB4 -I${KRB4DIR}/lib/krb
.endif
SRCS+=		kerberos5.c

#WARNS?=		2

CFLAGS+=	-DENCRYPTION -DAUTHENTICATION -DSRA -I${TELNETDIR}
CFLAGS+=	-DKRB5 -I${KRB5DIR}/lib/krb5 -I${KRB5OBJDIR} -I${ASN1OBJDIR}
CFLAGS+=	-DFORWARD -Dnet_write=telnet_net_write

INCS=		${TELNETDIR}/arpa/telnet.h
INCSDIR=	${INCLUDEDIR}/arpa

.include <bsd.lib.mk>

.PATH:	${TELNETDIR}/libtelnet

#
# Remove obsolete shared libraries, if any.  We don't bother moving them
# to /usr/lib/compat, since they were only used by telnet, telnetd and
# tn3270.
#
beforeinstall: __remove-stale-libs
__remove-stale-libs: .PHONY
.if exists(${DESTDIR}${SHLIBDIR}/lib${LIB}.so.2.0)
	-chflags noschg ${DESTDIR}${SHLIBDIR}/lib${LIB}.so.2.0
	rm -f ${DESTDIR}${SHLIBDIR}/lib${LIB}.so.2.0
.endif

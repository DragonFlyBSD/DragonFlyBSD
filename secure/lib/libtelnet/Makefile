#	$FreeBSD: src/secure/lib/libtelnet/Makefile,v 1.17.2.7 2002/07/22 14:21:51 ru Exp $
#	$DragonFly: src/secure/lib/libtelnet/Makefile,v 1.2 2003/06/17 04:27:48 dillon Exp $

LIB=		telnet

INTERNALLIB=	yes

SRCS=		genget.c getent.c misc.c encrypt.c auth.c \
		enc_des.c sra.c pk.c

CFLAGS+=	-DENCRYPTION -DAUTHENTICATION -DSRA \
		-I${TELNETDIR} -I${TELNETDIR}/libtelnet

WARNS?=		2

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

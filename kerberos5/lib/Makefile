# $FreeBSD: src/kerberos5/lib/Makefile,v 1.2.2.4 2002/07/25 09:33:14 ru Exp $

SUBDIR=	libroken libvers libasn1 libhdb libkrb5 libkadm5clnt libkadm5srv libsl libgssapi
.if defined(MAKE_KERBEROS4)
SUBDIR+=libkafs5
.endif
SUBDIR+=libtelnet
.include <bsd.subdir.mk>

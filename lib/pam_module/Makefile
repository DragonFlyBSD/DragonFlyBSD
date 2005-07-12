# $DragonFly: src/lib/pam_module/Makefile,v 1.2 2005/07/12 23:30:24 joerg Exp $

SUBDIR=		pam_cleartext_pass_ok pam_deny pam_opie pam_opieaccess
SUBDIR+=	pam_permit pam_radius pam_ssh pam_tacplus pam_unix
.if defined(MAKE_KERBEROS5) && !defined(NOCRYPT) && !defined(NO_OPENSSL)
SUBDIR+=	pam_krb5
.endif

.include <bsd.subdir.mk>

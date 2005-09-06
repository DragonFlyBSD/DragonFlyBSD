# $DragonFly: src/lib/pam_module/Makefile,v 1.3 2005/09/06 18:55:22 dillon Exp $

SUBDIR=		pam_cleartext_pass_ok pam_deny pam_opie pam_opieaccess
SUBDIR+=	pam_permit pam_radius pam_ssh pam_tacplus pam_unix
.if defined(WANT_KERBEROS) && !defined(NO_CRYPT) && !defined(NO_OPENSSL)
SUBDIR+=	pam_krb5
.endif

.include <bsd.subdir.mk>

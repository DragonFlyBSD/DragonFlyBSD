# $DragonFly: src/lib/pam_module/Makefile,v 1.4 2008/01/02 17:41:30 matthias Exp $

SUBDIR=	pam_chroot \
	pam_cleartext_pass_ok \
	pam_deny \
	pam_echo \
	pam_exec \
	pam_ftpusers \
	pam_group \
	pam_guest \
	pam_lastlog \
	pam_login_access \
	pam_nologin \
	pam_opie \
	pam_opieaccess \
	pam_permit \
	pam_radius \
	pam_rhosts \
	pam_rootok \
	pam_securetty \
	pam_self \
	pam_ssh \
	pam_tacplus \
	pam_unix

.if defined(WANT_KERBEROS) && !defined(NO_CRYPT) && !defined(NO_OPENSSL)
SUBDIR+=pam_krb5 pam_ksu
.endif

.include <bsd.subdir.mk>

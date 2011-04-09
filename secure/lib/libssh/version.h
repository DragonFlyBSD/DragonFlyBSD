/* $OpenBSD: version.h,v 1.46 2006/02/01 11:27:22 markus Exp $ */
/* $DragonFly: src/secure/lib/libssh/version.h,v 1.10 2008/09/28 03:19:46 pavalos Exp $ */

#ifndef SSH_VERSION

#define	SSH_VERSION		(ssh_version_get())
#define	SSH_RELEASE		(ssh_version_get())
#define	SSH_VERSION_BASE	"OpenSSH_5.8p1-hpn13v11"
#define	SSH_VERSION_ADDENDUM	"DragonFly-20110408"

const char *ssh_version_get(void);
void ssh_version_set_addendum(const char *add);
#endif /* SSH_VERSION */

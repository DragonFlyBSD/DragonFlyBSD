/* $OpenBSD: version.h,v 1.46 2006/02/01 11:27:22 markus Exp $ */
/* $DragonFly: src/secure/lib/libssh/version.h,v 1.9 2008/04/07 01:20:18 pavalos Exp $ */

#ifndef SSH_VERSION

#define SSH_VERSION             (ssh_version_get())
#define SSH_RELEASE		(ssh_version_get())
#define SSH_VERSION_BASE        "OpenSSH_5.0p1"
#define SSH_VERSION_ADDENDUM    "DragonFly-20080406"

const char *ssh_version_get(void);
void ssh_version_set_addendum(const char *add);
#endif /* SSH_VERSION */

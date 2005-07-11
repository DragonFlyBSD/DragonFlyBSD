/* $OpenBSD: version.h,v 1.44 2005/03/16 21:17:39 markus Exp $ */
/* $DragonFly: src/secure/lib/libssh/version.h,v 1.3 2005/07/11 22:49:45 corecode Exp $ */

#ifndef SSH_VERSION

#define SSH_VERSION             (ssh_version_get())
#define SSH_RELEASE		(ssh_version_get())
#define SSH_VERSION_BASE        "OpenSSH_4.1p1"
#define SSH_VERSION_ADDENDUM    "DragonFly-20050712"

const char *ssh_version_get(void);
void ssh_version_set_addendum(const char *add);
#endif /* SSH_VERSION */

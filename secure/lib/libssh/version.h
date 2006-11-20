/* $OpenBSD: version.h,v 1.46 2006/02/01 11:27:22 markus Exp $ */
/* $DragonFly: src/secure/lib/libssh/version.h,v 1.6 2006/11/20 17:15:46 pavalos Exp $ */

#ifndef SSH_VERSION

#define SSH_VERSION             (ssh_version_get())
#define SSH_RELEASE		(ssh_version_get())
#define SSH_VERSION_BASE        "OpenSSH_4.5p1"
#define SSH_VERSION_ADDENDUM    "DragonFly-20061120"

const char *ssh_version_get(void);
void ssh_version_set_addendum(const char *add);
#endif /* SSH_VERSION */

/* $OpenBSD: version.h,v 1.41 2004/03/20 10:40:59 markus Exp $ */
/* $DragonFly: src/secure/lib/libssh/version.h,v 1.1 2004/07/31 20:05:00 geekgod Exp $ */

#ifndef SSH_VERSION

#define SSH_VERSION             (ssh_version_get())
#define SSH_VERSION_BASE        "OpenSSH_3.8.1p1"
#define SSH_VERSION_ADDENDUM    "DragonFly-20040710"

const char *ssh_version_get(void);
void ssh_version_set_addendum(const char *add);
#endif /* SSH_VERSION */

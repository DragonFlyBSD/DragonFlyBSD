/* $OpenBSD: version.h,v 1.35 2002/10/01 13:24:50 markus Exp $ */
/* $FreeBSD: src/crypto/openssh/version.h,v 1.1.1.1.2.10 2003/02/03 17:31:08 des Exp $ */
/* $DragonFly: src/crypto/openssh/Attic/version.h,v 1.3 2003/09/16 16:46:28 dillon Exp $ */

#ifndef SSH_VERSION

#define SSH_VERSION             (ssh_version_get())
#define SSH_VERSION_BASE        "OpenSSH_3.5p1"
#define SSH_VERSION_ADDENDUM    "FreeBSD-20030916"

const char *ssh_version_get(void);
void ssh_version_set_addendum(const char *add);
#endif /* SSH_VERSION */


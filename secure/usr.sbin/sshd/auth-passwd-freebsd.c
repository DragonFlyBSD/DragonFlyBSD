/*
 * $DragonFly: src/secure/usr.sbin/sshd/auth-passwd-freebsd.c,v 1.1 2004/07/31 20:05:00 geekgod Exp $
 */

#include <unistd.h>

#include "auth.h"

int
sys_auth_passwd(Authctxt *authctxt, const char *password)
{
	struct passwd *pw = authctxt->pw;
	char *encrypted_password;
	char *pw_password = pw->pw_passwd;

	/* Check for users with no password. */
	if (strcmp(pw_password, "") == 0 && strcmp(password, "") == 0)
		return (1);

	/* Encrypt the candidate password using the proper salt. */
	encrypted_password = crypt(password,
	    (pw_password[0] && pw_password[1]) ? pw_password : "xx");

	/*
	 * Authentication is accepted if the encrypted passwords
	 * are identical.
	 */
	return (strcmp(encrypted_password, pw_password) == 0);
}

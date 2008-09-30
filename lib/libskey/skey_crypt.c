/* Author: Wietse Venema, Eindhoven University of Technology. */
/* $DragonFly: src/lib/libskey/skey_crypt.c,v 1.2 2008/09/30 16:57:05 swildner Exp $ */

#include <string.h>
#include <stdio.h>
#include <pwd.h>
#include <unistd.h>

#include "skey.h"

/* skey_crypt - return encrypted UNIX passwd if s/key or regular password ok */

const char *
skey_crypt(char *pp, char *salt, struct passwd *pwd, int pwok)
{
    struct skey skey;
    char   *p;

    /* Try s/key authentication even when the UNIX password is permitted. */

    if (pwd != 0 && skeyinfo(&skey, pwd->pw_name, (char *) 0) == 0
	&& skeyverify(&skey, pp) == 0) {
	/* s/key authentication succeeded */
	return (pwd->pw_passwd);
    }

    /* When s/key authentication does not work, always invoke crypt(). */

    p = crypt(pp, salt);
    if (pwok && pwd != 0 && strcmp(p, pwd->pw_passwd) == 0)
	return (pwd->pw_passwd);

    /* The user does not exist or entered bad input. */

    return (":");
}

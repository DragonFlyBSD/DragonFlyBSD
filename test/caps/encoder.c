/*
 * ENCODER.C
 *
 * CAPS-Encode the passwd structure for the specified usernames, generate
 * to stdout.
 *
 * $DragonFly: src/test/caps/encoder.c,v 1.1 2004/03/07 23:36:45 dillon Exp $
 */
#include <sys/types.h>
#include <libcaps/caps_struct.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>

int
main(int ac, char **av)
{
    int i;
    int n;
    char buf[1024];

    for (i = 1; i < ac; ++i) {
	struct passwd *pw;

	if ((pw = getpwnam(av[i])) == NULL) {
	    printf("%s: lookup failed\n");
	    continue;
	}
	n = caps_encode(buf, sizeof(buf), pw, &caps_passwd_struct);
	if (n > sizeof(buf)) {
	    printf("buffer overflow during encoding\n");
	} else {
	    buf[n] = 0;
	    printf("%s\n", buf);
	}
    }
    return(0);
}


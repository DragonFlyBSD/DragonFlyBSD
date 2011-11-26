/*
 * DECODER.C
 *
 * Decode CAPS encoded buffers, one per line, into a struct passwd and
 * report the results.
 *
 * $DragonFly: src/test/caps/decoder.c,v 1.1 2004/03/07 23:36:45 dillon Exp $
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
    int error;
    int len;
    char buf[1024];
    struct passwd pw;

    while (fgets(buf, sizeof(buf), stdin) != NULL) {
	len = strlen(buf);
	bzero(&pw, sizeof(pw));
	n = caps_decode(buf, len, &pw, &caps_passwd_struct, &error);
	printf("decode %d bytes error %d\n", n, error);
	if (error) {
	    if (n > len)
		n = len;
	    if (n && buf[n] == '\n') /* don't highlight a 'newline' */
		--n;
	    printf("%*.*s", n, n, buf);
	    printf("\033[7m%c\033[m", buf[n]);
	    n = len - n - 1;
	    if (n > 0)
		printf("%*.*s", n, n, buf + len - n);
	} else {
	    printf("{\n");
	    printf("    pw_name = \"%s\"\n", pw.pw_name);
	    printf("    pw_passwd = \"%s\"\n", pw.pw_passwd);
	    printf("    pw_uid = %d\n", pw.pw_uid);
	    printf("    pw_gid = %d\n", pw.pw_gid);
	    printf("    pw_change = %08llx\n", (long long)pw.pw_change);
	    printf("    pw_class = \"%s\"\n", pw.pw_class);
	    printf("    pw_gecos = \"%s\"\n", pw.pw_gecos);
	    printf("    pw_dir = \"%s\"\n", pw.pw_dir);
	    printf("    pw_shell = \"%s\"\n", pw.pw_shell);
	    printf("    pw_expire = %08llx\n", (long long)pw.pw_expire);
	    printf("}\n");
	}
	caps_struct_free_pointers(&pw, &caps_passwd_struct);
    }
    return(0);
}


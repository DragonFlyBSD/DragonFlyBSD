/*
 * $DragonFly: src/test/caps/client.c,v 1.1 2004/01/18 12:39:56 dillon Exp $
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/caps.h>
#include <sys/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int ac, char **av)
{
    int cid;
    int n;
    int count = 0;
    char buf[256];
    struct caps_msgid msgid;
    off_t msgcid;

    cid = caps_sys_client("test", getuid(), getgid(), 0, CAPF_ANYCLIENT);
    printf("cid = %d %d\n", cid, errno);
    if (cid < 0)
	return(0);
    for (;;) {
	msgcid = caps_sys_put(cid, "xyz", 3);
#ifdef DEBUG
	printf("msgcid = %016llx %d\n", msgcid, errno);
#endif
	n = caps_sys_wait(cid, buf, sizeof(buf), &msgid, NULL);
#ifdef DEBUG
	printf("n = %d msgid=%016llx state=%d errno=%d\n", n, msgid.c_id, msgid.c_state, errno);
	if (n > 0)
	    printf("REPLY: %*.*s\n", n, n, buf);
#endif
	++count;
	if ((count & 65535) == 0)
	    printf("%d\n", count);
    }
    return(0);
}


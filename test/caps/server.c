/*
 * $DragonFly: src/test/caps/server.c,v 1.2 2004/01/20 06:03:15 asmodai Exp $
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
    char buf[256];
    struct caps_msgid msgid;

    cid = caps_sys_service("test", getuid(), getgid(), 0, CAPS_ANYCLIENT);
    printf("cid = %d\n", cid);
    if (cid < 0)
	return(0);
    bzero(&msgid, sizeof(msgid));
    for (;;) {
	n = caps_sys_wait(cid, buf, sizeof(buf), &msgid, NULL);
#ifdef DEBUG
	printf("n = %d msgid=%016llx state=%d errno=%d\n", n, msgid.c_id, msgid.c_state, errno);
	if (n > 0)
	    printf("BUFFER: %*.*s\n", n, n, buf);
#endif
	n = caps_sys_reply(cid, "good", 4, msgid.c_id);
#ifdef DEBUG
	printf("reply: n = %d\n", n);
#endif
    }
    return(0);
}


/*
 * $DragonFly: src/test/caps/server.c,v 1.4 2004/03/06 22:15:00 dillon Exp $
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
    int count;
    char buf[256];
    struct caps_msgid msgid;

    count = 0;

    cid = caps_sys_service("test", getuid(), getgid(), 0, CAPF_ANYCLIENT);
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
	if (msgid.c_state != CAPMS_DISPOSE)
	    n = caps_sys_reply(cid, "good", 4, msgid.c_id);
#ifdef DEBUG
	printf("reply: n = %d\n", n);
#endif
	if (++count % 1000000 == 0)
		caps_sys_setgen(cid, count);
    }
    return(0);
}


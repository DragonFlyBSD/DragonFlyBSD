/*
 * $DragonFly: src/test/caps/client.c,v 1.2 2004/03/06 22:15:00 dillon Exp $
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
    int lostit = 0;
    int didfork = 0;
    int which = 0;
    char buf[256];
    struct caps_msgid msgid;
    off_t msgcid;
    caps_gen_t gen = 0;
    caps_gen_t ngen;

    cid = caps_sys_client("test", getuid(), getgid(), 0, CAPF_ANYCLIENT);
    for (;;) {
	errno = 0;
	if (cid >= 0) {
	    msgcid = caps_sys_put(cid, "xyz", 3);
	    ngen = caps_sys_getgen(cid);
	}
	if (cid < 0 || (msgcid < 0 && errno == ENOTCONN)) {
	    if (lostit == 0) {
		if (didfork) {
		    didfork = 0;
		    printf("%d client forked, reconnecting to service\n", which);
		} else {
		    printf("%d client lost connection, reconnecting\n", which);
		}
	    }
	    lostit = 1;
	    caps_sys_close(cid);
	    cid = caps_sys_client("test", getuid(), getgid(), 0, 
			CAPF_ANYCLIENT | CAPF_WAITSVC);
	    continue;
	}
	if (lostit) {
	    printf("%d client resume on reconnect after lost connection\n", which);
	    lostit = 0;
	}
	if (ngen != gen) {
	    printf("%d client: note generation change %lld\n", which, ngen);
	    gen = ngen;
	}
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
	    printf("%d %d\n", which, count);
	if (count == 1000 && which == 0) {
	    if (fork() == 0) {
		usleep(100000);
		didfork = 1;
		which = 1;
	    } else {
		printf("forked pid %d\n", (int)getpid());
	    }
	}
    }
    return(0);
}


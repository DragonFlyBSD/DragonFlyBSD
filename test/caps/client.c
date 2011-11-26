/*
 * $DragonFly: src/test/caps/client.c,v 1.3 2004/03/31 20:27:34 dillon Exp $
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
    long long xcount = 0;
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
	    if (lostit == 0)
		printf("%d client forked or lost connection, reconnecting\n", which);
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
	++xcount;
	if ((count & 65535) == 0)
	    printf("%d %lld\n", which, xcount);
	if (count == 100000000)
	    count = 0;
	if (count == 1000 && didfork == 0 && which < 10) {
	    if (fork() == 0) {
		usleep(100000);
		++which;
	    } else {
		printf("forked pid %d client #%d\n", (int)getpid(), which + 1);
		didfork = 1;
	    }
	}
    }
    return(0);
}


/*
 * cputest.c
 *
 *	WARNING!  Always test on a 100% idle system.  Note that some tests
 *	may return 0 or negative results, due to pipeline effects.
 *
 * $DragonFly: src/test/cpuperf/cputest.c,v 1.1 2004/02/09 18:08:54 dillon Exp $
 */

#include "blib.h"

extern char test_str[];

int junk;

int
main(int ac, char **av)
{
    int i;
    int ttl;
    int us1;
    int us2;

    printf("CPUTEST %s\n", test_str);
    start_timing();
    for (i = 0; ; ++i) {
	test_load(&junk);
	if ((i & 65535) == 0 && get_timing() > 1000000)
	    break;
    }
    ttl = i * 4;
    start_timing();
    for (i = 0; i < ttl; ++i) {
	test_dummy(&junk);
    }
    us1 = get_timing();
    start_timing();
    for (i = 0; i < ttl; ++i) {
	test_load(&junk);
    }
    us2 = get_timing();
    stop_timing2(ttl, us2 - us1, "instruction overhead:");
    return(0);
}


/*
 * SENDSYS.H
 *
 * $DragonFly: src/test/sysmsg/Attic/sendsys.h,v 1.1 2003/08/12 02:29:41 dillon Exp $
 */

static __inline
int
sendsys(void *port, void *msg, int msgsize)
{
    int error;
    __asm __volatile("int $0x81" : "=a"(error), "=c"(msg), "=d"(msgsize) : "0"(port), "1"(msg), "2"(msgsize) : "memory");
    return(error);
}


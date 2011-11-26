/*
 * $DragonFly: src/lib/libcaps/i386/sendsys.h,v 1.1 2003/12/04 22:06:22 dillon Exp $
 */
#ifndef _SENDSYS_H_
#define _SENDSYS_H_

static __inline
int
sendsys(struct lwkt_port *port, void *msg, int msgsize)
{
    int error;
    __asm __volatile("int $0x81" : "=a"(error), "=c"(msg), "=d"(msgsize) : "0"(port), "1"(msg), "2"(msgsize) : "memory");
    return(error);
}

#endif /* _SENDSYS_H_ */

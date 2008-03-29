/*
 * $FreeBSD: src/sys/boot/ficl/unix.c,v 1.2 2007/03/23 22:26:01 jkim Exp $
 * $DragonFly: src/sys/boot/ficl/unix.c,v 1.2 2008/03/29 23:31:07 swildner Exp $
 */

#include <string.h>
#include <netinet/in.h>

#include "ficl.h"



unsigned long ficlNtohl(unsigned long number)
{
    return ntohl(number);
}




void ficlCompilePlatform(FICL_DICT *dp)
{
    return;
}



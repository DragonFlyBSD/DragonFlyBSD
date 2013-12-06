 /*
  * Replace %m by system error message.
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  */

#include <stdio.h>
#include <errno.h>
#include <string.h>

#ifndef SYS_ERRLIST_DEFINED
extern char *sys_errlist[];
extern int sys_nerr;
#endif

#include "mystdarg.h"

char   *percent_m(obuf, ibuf)
char   *obuf;
char   *ibuf;
{
    char   *bp = obuf;
    char   *cp = ibuf;

    while ((*bp = *cp) != 0)
	if (*cp == '%' && cp[1] == 'm') {
	    if (errno < sys_nerr && errno > 0) {
		strcpy(bp, sys_errlist[errno]);
	    } else {
		sprintf(bp, "Unknown error %d", errno);
	    }
	    bp += strlen(bp);
	    cp += 2;
	} else {
	    bp++, cp++;
	}
    return (obuf);
}

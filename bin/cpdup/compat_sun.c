/*
 * Implement some functions which are unknown to Solaris 10.
 */

#include "compat_sun.h"
#include <stdlib.h>

int
vasprintf(char **str, const char *format, va_list ap)
{
    char dummy[2];
    int result;

    if ((result = vsnprintf(dummy, 2, format, ap)) < 0) {
	*str = NULL;
	return (result);
    }
    if ((*str = malloc(result + 1)) == NULL)
	return (-1);
    if ((result = vsnprintf(*str, result + 1, format, ap)) < 0) {
	free(*str);
	*str = NULL;
    }
    return (result);
}

int
asprintf(char **str, const char *format, ...)
{
    va_list ap;
    int result;

    va_start(ap, format);
    result = vasprintf(str, format, ap);
    va_end(ap);
    return (result);
}

#ifndef NOMD5

#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>

char *
MD5File(const char *filename, char *buf)
{
    unsigned char dbuf[8192];
    MD5_CTX ctx;
    int fd;
    int count;

    if ((fd = open(filename, O_RDONLY)) < 0)
	return (NULL);
    MD5Init(&ctx);
    while ((count = read(fd, dbuf, sizeof(dbuf))) > 0)
	MD5Update(&ctx, dbuf, count);
    close(fd);
    if (count < 0)
	return (NULL);
    if (buf == NULL)
	if ((buf = malloc(33)) == NULL)
	    return NULL;
    MD5Final(dbuf, &ctx);
    for (count = 0; count < 16; count++)
	sprintf(buf + count * 2, "%02x", dbuf[count]);
    return (buf);
}

#endif	/* ifndef NOMD5 */

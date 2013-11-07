
/*
 * cc crc32.c /usr/src/sys/libkern/crc32.c -o /usr/local/bin/crc32
 */

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(int ac, char **av)
{
    char buf[256];
    int n;
    u_int32_t crc = crc32(NULL, 0);

    while ((n = read(0, buf, sizeof(buf))) > 0)
	crc = crc32_ext(buf, n, crc);
    printf("crc %08x\n", crc);
    return(0);
}

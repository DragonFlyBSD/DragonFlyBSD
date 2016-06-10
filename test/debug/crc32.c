
/*
 * cc crc32.c /usr/src/sys/libkern/{crc32.c,icrc32.c} -o ~/bin/crc32
 */

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

uint32_t iscsi_crc32(const void *buf, size_t size);
uint32_t iscsi_crc32_ext(const void *buf, size_t size, uint32_t ocrc);
uint32_t crc32(const void *buf, size_t size);
uint32_t crc32_ext(const void *buf, size_t size, uint32_t ocrc);

#define ISCSI

int
main(int ac, char **av)
{
    char buf[16384];
    int n;
#ifdef ISCSI
    u_int32_t crc1 = iscsi_crc32(NULL, 0);
#else
    u_int32_t crc2 = crc32(NULL, 0);
#endif

    while ((n = read(0, buf, sizeof(buf))) > 0) {
#ifdef ISCSI
	crc1 = iscsi_crc32_ext(buf, n, crc1);
#else
	crc2 = crc32_ext(buf, n, crc2);
#endif
    }
#ifdef ISCSI
    printf("iscsi_crc32 %08x\n", crc1);
#else
    printf("crc32 %08x\n", crc2);
#endif
    return(0);
}

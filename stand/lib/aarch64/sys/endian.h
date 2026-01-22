#ifndef _SYS_ENDIAN_H_
#define _SYS_ENDIAN_H_

#include <sys/types.h>

#define _LITTLE_ENDIAN 1234
#define _BIG_ENDIAN 4321

#define _BYTE_ORDER _LITTLE_ENDIAN

#define _QUAD_HIGHWORD 1
#define _QUAD_LOWWORD 0

/* Byte-swap functions */
static __inline __uint16_t
__bswap16(__uint16_t _x)
{
	return ((_x >> 8) | (_x << 8));
}

static __inline __uint32_t
__bswap32(__uint32_t _x)
{
	return ((_x >> 24) | ((_x >> 8) & 0xff00) |
	    ((_x << 8) & 0xff0000) | (_x << 24));
}

static __inline __uint64_t
__bswap64(__uint64_t _x)
{
	return ((_x >> 56) | ((_x >> 40) & 0xff00) |
	    ((_x >> 24) & 0xff0000) | ((_x >> 8) & 0xff000000) |
	    ((_x << 8) & ((__uint64_t)0xff << 32)) |
	    ((_x << 24) & ((__uint64_t)0xff << 40)) |
	    ((_x << 40) & ((__uint64_t)0xff << 48)) | (_x << 56));
}

/* Host to network / network to host byte order (little-endian host) */
#define	__htonl(x)	__bswap32(x)
#define	__htons(x)	__bswap16(x)
#define	__ntohl(x)	__bswap32(x)
#define	__ntohs(x)	__bswap16(x)

#define	htonl(x)	__htonl(x)
#define	htons(x)	__htons(x)
#define	ntohl(x)	__ntohl(x)
#define	ntohs(x)	__ntohs(x)

#endif

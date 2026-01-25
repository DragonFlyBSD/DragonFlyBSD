/*
 * Placeholder for arm64 bus DMA definitions.
 */

#ifndef _CPU_BUS_DMA_H_
#define _CPU_BUS_DMA_H_

#include <sys/types.h>

typedef u_int64_t bus_addr_t;
typedef u_int64_t bus_size_t;

typedef u_int64_t bus_space_tag_t;
typedef u_int64_t bus_space_handle_t;

#define BUS_SPACE_MAXSIZE_24BIT 0xFFFFFFUL
#define BUS_SPACE_MAXSIZE_32BIT	0xFFFFFFFFUL
#define BUS_SPACE_MAXSIZE	(64 * 1024)
#define BUS_SPACE_MAXADDR_24BIT	0xFFFFFFUL
#define BUS_SPACE_MAXADDR_32BIT	0xFFFFFFFFUL
#define BUS_SPACE_MAXADDR	0xFFFFFFFFFFFFFFFFUL

#define BUS_SPACE_UNRESTRICTED	(~0)

#endif /* !_CPU_BUS_DMA_H_ */

#ifndef _SYS_UUID_H_
#define _SYS_UUID_H_

#include <sys/cdefs.h>
#include <machine/stdint.h>

#define _UUID_NODE_LEN 6

struct uuid {
	__uint32_t time_low;
	__uint16_t time_mid;
	__uint16_t time_hi_and_version;
	__uint8_t clock_seq_hi_and_reserved;
	__uint8_t clock_seq_low;
	__uint8_t node[_UUID_NODE_LEN];
};

typedef struct uuid uuid_t;

#define UUID_NODE_LEN _UUID_NODE_LEN

#endif

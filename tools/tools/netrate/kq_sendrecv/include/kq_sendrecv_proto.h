#ifndef _KQ_SENDRECV_PROTO_H_
#define _KQ_SENDRECV_PROTO_H_

#include <sys/types.h>
#include <stdint.h>

#define RECV_PORT	11236

struct conn_ack {
	uint16_t	version;
	uint16_t	rsvd;		/* reserved 0 */
	uint32_t	rsvd1;		/* reserved 0 */
	uint64_t	dummy;
} __packed;

struct recv_info {
	uint16_t	version;
	uint16_t	ndport;
	uint32_t	rsvd;		/* reserved 0 */

	uint16_t	dport[];	/* network byte order */
} __packed;

#endif	/* !_KQ_SENDRECV_PROTO_H_ */

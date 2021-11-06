.PATH: ${.CURDIR}/../libc/sysvipc

CMAPS+=	${.CURDIR}/sysvipc/Symbol.map

SRCS+=	ipc.c lock.c lock_generic.c msg.c sem.c shm.c sockets.c
SRCS+=	sysvipc_hash.c utils.c

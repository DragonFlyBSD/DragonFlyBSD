.PATH: ${.CURDIR}/../libc/${MACHINE_ARCH}/sysvipc ${.CURDIR}/../libc/sysvipc

CMAPS+=	${.CURDIR}/sysvipc/Symbol.map
SRCS+=	lock.c lock_generic.c sysvipc_hash.c ipc.c shm.c msg.c sem.c sockets.c utils.c

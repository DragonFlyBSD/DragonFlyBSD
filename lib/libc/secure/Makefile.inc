# libc sources related to security

.PATH: ${.CURDIR}/../libc/secure

SRCS+=	stack_protector.c \
	stack_protector_compat.c

CMAPS+=	${.CURDIR}/secure/Symbol.map

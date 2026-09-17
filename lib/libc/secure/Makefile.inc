# libc sources related to security

.PATH: ${.CURDIR}/secure

SRCS+=	stack_protector.c \
	stack_protector_compat.c

CMAPS+=	${.CURDIR}/secure/Symbol.map

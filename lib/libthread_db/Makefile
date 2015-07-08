LIB=	thread_db
SHLIB_MAJOR= 1
SRCS=	thread_db.c
SRCS+=	libthread_xu.c
INCS=	thread_db.h
WARNS?= 1

CFLAGS+=-I. -I${.CURDIR} -I${.CURDIR}/../../libexec/rtld-elf \
	-I${.CURDIR}/../../libexec/rtld-elf/${MACHINE_ARCH}

.include <bsd.lib.mk>

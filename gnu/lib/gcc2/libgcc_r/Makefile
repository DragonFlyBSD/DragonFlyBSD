# $FreeBSD: src/gnu/lib/libgcc_r/Makefile,v 1.4.4.1 2000/11/01 01:20:37 jdp Exp $
# $DragonFly: src/gnu/lib/gcc2/libgcc_r/Attic/Makefile,v 1.1 2004/01/16 07:45:19 dillon Exp $

LIB=	gcc_r
CFLAGS+=-D_PTHREADS
CFLAGS+=-D'__GTHREAD_MUTEX_INIT_FUNCTION(m)=pthread_mutex_init(m, NULL)'

.include "../libgcc/Makefile"

LIB=	thread_xu
SHLIB_MAJOR= 2

# The threading implementation has been merged into libc (see
# lib/libc/thread/Makefile.inc).  This directory now builds only an
# empty compatibility shim: old binaries carry DT_NEEDED entries for
# libthread_xu.so.2 and expect the file to exist; the symbols they
# reference resolve from libc.so.8 by name.
SRCS=	dummy.c

PRECIOUSLIB=	yes
LIBDIR=	/usr/lib/thread
NOPROFILE=	yes

.include <bsd.lib.mk>

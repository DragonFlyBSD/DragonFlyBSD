LIB=		thread_xu
SHLIB_MAJOR=	2
LIBDIR=		/usr/lib/thread
NOPROFILE=	yes

# The threading implementation has been merged into libc.  This directory now
# builds only an empty compatibility shim: old binaries carry DT_NEEDED entries
# for libthread_xu.so.2 and expect the file to exist.
SRCS=	dummy.c

.include <bsd.lib.mk>

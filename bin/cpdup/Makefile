PROG=	cpdup
SRCS=	cpdup.c hcproto.c hclink.c misc.c fsmid.c

.if defined(.FreeBSD)
CFLAGS += -D_ST_FLAGS_PRESENT_=1
WARNS?=	6
.endif

.if !defined(NOMD5)
SRCS+=	md5.c
.endif

# XXX sys/md5.h shim errata for bootstrap REMOVE_OPENSSL_FILES
CFLAGS+= -I${_SHLIBDIRPREFIX}/usr/include/priv

LDADD+= -lmd
DPADD+= ${LIBMD}

.include <bsd.prog.mk>


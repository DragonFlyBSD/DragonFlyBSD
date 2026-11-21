PROG=	cpdup
SRCS=	cpdup.c hcproto.c hclink.c misc.c

.if defined(BOOTSTRAPPING) || defined(RESCUE)
# For boostrapping buildworld the checksum functionality is not needed
CFLAGS+=	-DNOCHECKSUM
.else
.if !defined(NOCHECKSUM)
SRCS+=		checksum.c
CFLAGS+=	${PRIVATELIB_CFLAGS}
DPADD=		${LIBCRYPTO}
LDADD=		-lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}
.endif
.endif

.include <bsd.prog.mk>


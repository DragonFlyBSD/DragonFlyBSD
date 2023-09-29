PROG=	hammer2
SRCS=	main.c subs.c ondisk.c
SRCS+=	cmd_remote.c cmd_snapshot.c cmd_pfs.c
SRCS+=	cmd_service.c cmd_leaf.c cmd_debug.c
SRCS+=	cmd_rsa.c cmd_stat.c cmd_setcomp.c cmd_setcheck.c
SRCS+=	cmd_bulkfree.c cmd_cleanup.c cmd_info.c cmd_destroy.c
SRCS+=	cmd_emergency.c cmd_growfs.c cmd_volume.c
SRCS+=  cmd_recover.c
SRCS+=	print_inode.c

MAN=	hammer2.8

#CFLAGS+= -DXXH_NAMESPACE=h2_
CFLAGS+= -I${.CURDIR}/../../sys
CFLAGS+= -pthread

DPADD=	${LIBDMSG} ${LIBM} ${LIBUTIL}
LDADD=	-ldmsg -lm -lutil

CFLAGS+=	${PRIVATELIB_CFLAGS}
DPADD+=		${LIBCRYPTO}
LDADD+=		-lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

DPADD+=		${LIBPTHREAD}
LDADD+=		-lpthread

.PATH: ${.CURDIR}/../../sys/libkern ${.CURDIR}/../../sys/vfs/hammer2/xxhash ${.CURDIR}/zlib ${.CURDIR}/lz4

# Hash functions
#
SRCS+= icrc32.c xxhash.c

# Decompressor
#
SRCS+=	hammer2_compression.c
SRCS+=	hammer2_lz4.c
SRCS+=  hammer2_zlib_adler32.c hammer2_zlib_deflate.c
SRCS+=  hammer2_zlib_inffast.c hammer2_zlib_inflate.c
SRCS+=  hammer2_zlib_inftrees.c hammer2_zlib_trees.c
SRCS+=  hammer2_zlib_zutil.c
CFLAGS+= -DZLIB_CONST


.include <bsd.prog.mk>

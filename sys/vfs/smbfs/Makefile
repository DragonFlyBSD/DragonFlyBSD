# $FreeBSD: src/sys/modules/smbfs/Makefile,v 1.1.2.4 2002/03/26 10:12:29 ume Exp $

.PATH:	${.CURDIR}/../../crypto/des \
	${.CURDIR}/../../crypto/des/arch/${MACHINE_ARCH} \
	${.CURDIR}/../../kern \
	${.CURDIR}/../../libkern \
	${.CURDIR}/../../netproto/smb

KMOD=	smbfs

SRCS=	opt_inet.h \
	opt_netsmb.h \
	iconv_converter_if.h \
	md4c.c \
	smb_conn.c smb_dev.c smb_trantcp.c smb_smb.c smb_subr.c smb_rq.c \
	smb_usr.c smb_crypt.c smb_iod.c \
	smbfs_vfsops.c smbfs_node.c smbfs_io.c smbfs_vnops.c \
	smbfs_subr.c smbfs_smb.c

.if exists(${.CURDIR}/../../crypto/des/des.h)
NETSMBCRYPTO=yes
.endif

.if defined(NETSMBCRYPTO)
SRCS+=	des_ecb.c des_setkey.c
.if ${MACHINE_ARCH} == "i386"
SRCS+=	des_enc.S
.else
SRCS+=	des_enc.c
.endif
.endif

# Build with INET support (1|0)
SMB_INET?=	1

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	touch ${.TARGET}
.if ${SMB_INET} > 0
	echo "#define INET 1" > ${.TARGET}
.endif

opt_netsmb.h:
	echo "#define NETSMB	1"  > ${.TARGET}
.if defined(NETSMBCRYPTO)
	echo "#define NETSMBCRYPTO 1" >> ${.TARGET}
.endif
.endif

.include <bsd.kmod.mk>

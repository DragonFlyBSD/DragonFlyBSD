LIB=	fsid
SHLIBDIR?= /lib
SRCS=	libfsid.c ufs.c hammer.c cd9660.c msdosfs.c ext2.c 
INCS=	libfsid.h
CFLAGS+=	-D_FSID_INTERNAL
MAN=	 libfsid.3
MLINKS+=	libfsid.3 fsid_probe.3
MLINKS+=	libfsid.3 fsid_probe_all.3
MLINKS+=	libfsid.3 fsid_volname.3
MLINKS+=	libfsid.3 fsid_volname_all.3
MLINKS+=	libfsid.3 fsid_fsname.3
MLINKS+=	libfsid.3 fsid_fs_count.3

.include <bsd.lib.mk>

# Makefile for vfs modules
#

SUBDIR=	fifofs msdosfs nfs procfs \
	hpfs ntfs smbfs isofs mfs udf \
	nullfs hammer tmpfs autofs ext2fs # fuse

SUBDIR+= hammer2

# empty to allow concurrent building
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>

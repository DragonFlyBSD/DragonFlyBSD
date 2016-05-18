# Makefile for vfs modules
#

SUBDIR=fifofs msdosfs portal nfs procfs puffs \
	hpfs ntfs smbfs isofs mfs udf \
	nullfs hammer tmpfs autofs

.if defined(WANT_HAMMER2)
SUBDIR+= hammer2
.endif

# empty to allow concurrent building
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>

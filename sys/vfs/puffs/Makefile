
KMOD=	puffs
SRCS=	puffs_msgif.c puffs_msgif.h puffs_node.c puffs_subr.c		\
	puffs_sys.h puffs_vfsops.c puffs_vnops.c puffs_io.c

DEBUG_FLAGS= -O0 -g -I${.CURDIR}/../.. -DINVARIANTS
#DEBUG_FLAGS+= -DPUFFSDEBUG

.include <bsd.kmod.mk>

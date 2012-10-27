# The libobjc header conflict between base compilers has been resolved
# Maximum parallelism can be restored by setting SUBDIR_ORDERED to null

SUBDIR= gcc44

.if !defined(NO_GCC47)
SUBDIR+= gcc47
.endif

SUBDIR+= libcryptsetup
SUBDIR+= libdevmapper
SUBDIR+= libdialog
SUBDIR+= libluks
SUBDIR+= liblvm

SUBDIR_ORDERED=

.include <bsd.subdir.mk>

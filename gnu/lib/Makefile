# The libobjc header conflict between base compilers has been resolved
# Maximum parallelism can be restored by setting SUBDIR_ORDERED to null

.if !defined(NO_GCC50)
SUBDIR= gcc50
.endif

SUBDIR+= gcc47

SUBDIR+= libcryptsetup
SUBDIR+= libdevmapper
SUBDIR+= libdialog
SUBDIR+= libluks
SUBDIR+= liblvm

SUBDIR_ORDERED=

.include <bsd.subdir.mk>

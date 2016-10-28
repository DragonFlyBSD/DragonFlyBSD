# The libobjc header conflict between base compilers has been resolved
# Maximum parallelism can be restored by setting SUBDIR_ORDERED to null

SUBDIR= gcc50

.if !defined(NO_ALTCOMPILER)
SUBDIR+= gcc47
.endif

.if !defined(NO_CRYPT)
SUBDIR+= libcryptsetup
SUBDIR+= libluks
.endif

SUBDIR+= libdevmapper
SUBDIR+= libdialog
SUBDIR+= liblvm

SUBDIR_ORDERED=

.include <bsd.subdir.mk>

# SUBDIR_ORDERED defaults to SUBDIR order which is just fine

SUBDIR= gcc44

.if !defined(NO_GCC47)
SUBDIR+= gcc47
.endif

SUBDIR+= libcryptsetup
SUBDIR+= libdevmapper
SUBDIR+= libdialog
SUBDIR+= libluks
SUBDIR+= liblvm

.include <bsd.subdir.mk>

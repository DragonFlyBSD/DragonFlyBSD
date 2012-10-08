SUBDIR= gcc44 libcryptsetup libdevmapper libdialog libluks liblvm

.if !defined(NO_GCC47)
SUBDIR+= gcc47

# FIXME: libobjc in gcc44 and gcc47 can collide installing header files
#
SUBDIR_ORDERED= gcc44 gcc47
.endif


.include <bsd.subdir.mk>

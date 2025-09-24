# gcc80 is now the default.  See sys.mk for WORLD_ALTCOMPILER default.
#
SUBDIR+= gcc80

.if !defined(NO_ALTCOMPILER) && defined(WORLD_ALTCOMPILER)
. if ${WORLD_ALTCOMPILER} == "all" || ${WORLD_ALTCOMPILER:Mgcc47}
SUBDIR+= gcc47
. endif
.endif

SUBDIR+= libcryptsetup libdevmapper libdialog libluks liblvm
SUBDIR_ORDERED=

.include <bsd.subdir.mk>

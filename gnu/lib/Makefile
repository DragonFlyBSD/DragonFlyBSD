
# gcc80 is now the default.  See sys.mk for WORLD_ALTCOMPILER default.
#
SUBDIR+= gcc80

.if !defined(NO_ALTCOMPILER)
. if defined(WORLD_ALTCOMPILER) && (${WORLD_ALTCOMPILER} == "all" || ${WORLD_ALTCOMPILER:Mgcc120})
SUBDIR+= gcc120
. endif
.endif

SUBDIR+= libcryptsetup libdevmapper libdialog libluks liblvm
SUBDIR_ORDERED=

.include <bsd.subdir.mk>

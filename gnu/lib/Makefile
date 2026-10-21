# gcc120 is now the default.  See sys.mk for WORLD_ALTCOMPILER default.
#
SUBDIR+= gcc120

.if !defined(NO_ALTCOMPILER) && defined(WORLD_ALTCOMPILER)
. if ${WORLD_ALTCOMPILER} == "all" || ${WORLD_ALTCOMPILER:Mgcc80}
SUBDIR+= gcc80
. endif
.endif

SUBDIR+= libcryptsetup libdevmapper libdialog libluks liblvm
SUBDIR_ORDERED=

.include <bsd.subdir.mk>

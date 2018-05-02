SUBDIR=	gcc50 libcryptsetup libdevmapper libdialog libluks liblvm

.if !defined(NO_ALTCOMPILER)
. if defined(WORLD_ALTCOMPILER) && (${WORLD_ALTCOMPILER} == "all" || ${WORLD_ALTCOMPILER:Mgcc47})
SUBDIR+= gcc47
. endif
. if defined(WORLD_ALTCOMPILER) && (${WORLD_ALTCOMPILER} == "all" || ${WORLD_ALTCOMPILER:Mgcc80})
SUBDIR+= gcc80
. endif
.endif

SUBDIR_ORDERED=

.include <bsd.subdir.mk>

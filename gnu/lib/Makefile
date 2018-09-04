
# gcc80 is now the default.  See sys.mk for WORLD_ALTCOMPILER default.
#
# (duplicate WORLD_CCVER for ALTCOMPILER == all is already built and
#  takes no extra time)
#
SUBDIR+= gcc80
SUBDIR+= ${WORLD_CCVER}

.if !defined(NO_ALTCOMPILER)
. if defined(WORLD_ALTCOMPILER) && (${WORLD_ALTCOMPILER} == "all" || ${WORLD_ALTCOMPILER:Mgcc47})
SUBDIR+= gcc47
. endif
. if defined(WORLD_ALTCOMPILER) && (${WORLD_ALTCOMPILER} == "all" || ${WORLD_ALTCOMPILER:Mgcc50})
SUBDIR+= gcc50
. endif
.endif

SUBDIR+= libcryptsetup libdevmapper libdialog libluks liblvm
SUBDIR_ORDERED=

.include <bsd.subdir.mk>

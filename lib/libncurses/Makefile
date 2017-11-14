SUBDIR=	libncurses \
	libpanel \
	libncursesw \
	libpanelw

# only needed to build share/terminfo
.if defined(NO_SHARE)
SUBDIR_ORDERED=
.else
SUBDIR+=	libtic tic_next
SUBDIR_ORDERED=	libtic tic_next
.endif

.include <bsd.subdir.mk>

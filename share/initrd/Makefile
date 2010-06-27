
#If you want to add files to other subdirectories, please use a similar
#approach as below, for example for var:
#
#add VAR_FILES=	foo
#add ${VAR_FILES} to FILES=
#add a for loop for ${VAR_FILES}

ETC_FILES=	rc
FILES=		${ETC_FILES}

.for file in ${ETC_FILES}
FILESDIR_${file}=${SHAREDIR}/initrd/etc
.endfor

.include <bsd.prog.mk>

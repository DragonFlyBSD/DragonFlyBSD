
#If you want to add files to other subdirectories, please use a similar
#approach as below, for example for var:
#
#add VAR_FILES=	foo
#add ${VAR_FILES} to FILES=
#add a for loop for ${VAR_FILES}

ETC_FILES=	rc rc.lvm2 rcmount_crypt rcmount_tcplay
FILES=		${ETC_FILES}

.for file in ${ETC_FILES}
FILESDIR_${file}=${SHAREDIR}/initrd/etc
FILESMODE_${file}=555
.endfor

.include <bsd.prog.mk>

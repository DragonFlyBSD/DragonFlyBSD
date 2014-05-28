#If you want to add files to other subdirectories, please use a similar
#approach as below, for example for var:
#
#add VAR_FILES=	foo
#add ${VAR_FILES} to FILES=
#add a for loop for ${VAR_FILES}

ETC_SCRIPTS=	rc rc.lvm2 rcmount_crypt rcmount_tcplay
FILES=		${ETC_SCRIPTS}

.for file in ${ETC_SCRIPTS}
FILESDIR_${file}=${SHAREDIR}/initrd/etc
FILESMODE_${file}=555
.endfor

# Files that should be installed read-only (444)
#
ETC_FILES=	${.CURDIR}/../../../etc/protocols	\
		${.CURDIR}/../../../etc/regdomain.xml	\
		${.CURDIR}/../../../etc/wpa_supplicant.conf	\
		${.CURDIR}/motd

PW_FILES=	/tmp/pwd.db /tmp/spwd.db

afterinstall:
	${INSTALL} -o root -g wheel -m 444 \
		${ETC_FILES} ${DESTDIR}${SHAREDIR}/initrd/etc
	cp ${.CURDIR}/../../../etc/master.passwd /tmp
	/usr/sbin/pwd_mkdb -d /tmp /tmp/master.passwd
	${INSTALL} -o root -g wheel -m 444 \
		${PW_FILES} ${DESTDIR}${SHAREDIR}/initrd/etc

.include <bsd.prog.mk>

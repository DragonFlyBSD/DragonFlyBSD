ETC_SCRIPTS=	rc rc.lvm2 rcmount_crypt rcmount_tcplay
ETC_FILES=	${.CURDIR}/motd \
		${.CURDIR}/../../etc/group \
		${.CURDIR}/../../etc/master.passwd \
		${.CURDIR}/../../etc/protocols \
		${.CURDIR}/../../etc/regdomain.xml \
		${.CURDIR}/../../etc/wpa_supplicant.conf

FILES=		${ETC_SCRIPTS} ${ETC_FILES}

.for file in ${ETC_SCRIPTS}
FILESDIR_${file}=	${BINDIR}
FILESMODE_${file}=	555
.endfor

.for file in ${ETC_FILES}
FILESDIR_${file}=	${BINDIR}
FILESMODE_${file}=	444
.endfor

afterinstall:
	pwd_mkdb -d ${DESTDIR}${BINDIR} ${DESTDIR}${BINDIR}/master.passwd

.include <bsd.prog.mk>

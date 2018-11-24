INITRD_MAKE=	LC_ALL=C \
		make -m ${.CURDIR}/../share/mk
INITRD_SUBDIRS=	etc
RESCUE_SUBDIRS=	oinit rescue rescue.libcrypto

# Temporary directory to stage the files for creating the initrd image.
#
# NOTE: Do not place this directory in the object tree, so that the entire
#       object tree can be exported read-only for upgrading other machines.
#
TMPDIR?=	/tmp
STAGEDIR:=	${TMPDIR}/initrd_staged.${.MAKE.PID}


all:
	@echo "--------------------------------------------------------------"
	@echo ">>> building rescue tools and initrd contents ..."
	@echo "--------------------------------------------------------------"
.for _dir in ${RESCUE_SUBDIRS} ${INITRD_SUBDIRS}
	(cd ${.CURDIR}/${_dir}; \
	    ${INITRD_MAKE} obj; \
	    ${INITRD_MAKE} depend all)
.endfor


install:
	# If called by 'installworld' (i.e., 'WORLDINSTALL' defined),
	# do not overwrite the existing ones.
.if !(defined(WORLDINSTALL) && exists(${DESTDIR}/rescue/rescue))
	@echo "--------------------------------------------------------------"
	@echo ">>> installing rescue tools to ${DESTDIR}/rescue ..."
	@echo "--------------------------------------------------------------"
.for _dir in ${RESCUE_SUBDIRS}
	(cd ${.CURDIR}/${_dir}; \
	    ${INITRD_MAKE} BINDIR=/rescue install)
.endfor
.endif

.if !(defined(WORLDINSTALL) && exists(${DESTDIR}/boot/kernel/initrd.img.gz))
	@echo "--------------------------------------------------------------"
	@echo ">>> setting up ${DESTDIR}/boot/kernel/initrd.img.gz ..."
	@echo "--------------------------------------------------------------"
	rm -rf ${STAGEDIR}
.for _dir in ${INITRD_SUBDIRS}
	(cd ${.CURDIR}/${_dir}; \
	    mkdir -p ${STAGEDIR}/${_dir}; \
	    ${INITRD_MAKE} BINDIR=${STAGEDIR}/${_dir} DESTDIR="" install)
.endfor
	sh ${.CURDIR}/mkinitrd.sh \
	    -b ${DESTDIR}/boot \
	    -r ${DESTDIR}/rescue \
	    -c ${STAGEDIR}
	rm -rf ${STAGEDIR}
.endif


clean:
.for _dir in ${RESCUE_SUBDIRS} ${INITRD_SUBDIRS}
	(cd ${.CURDIR}/${_dir} && ${INITRD_MAKE} clean cleandepend)
.endfor


.ORDER: all install
.MAIN: all

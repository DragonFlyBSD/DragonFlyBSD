# Target directory to install the rescue tools
RESCUE_DIR?=		/rescue

# Temporary directory to stage the initrd files (e.g., /etc)
# NOTE: Ignore ${DESTDIR} when staging files into this directory
INITRD_STAGEDIR!=	mktemp -d -t initrd-stage

.END:
	rm -rf ${INITRD_STAGEDIR}

rescue:
	@${ECHO} ">>> Building and installing rescue tools ..."
.for _dir in bin oinit sbin sbin.libcrypto sbin.lvm
	(cd ${.CURDIR}/${_dir}; \
	    ${ECHO} "=> ${_dir}"; \
	    make BINDIR=${RESCUE_DIR} clean cleandepend ; \
	    make BINDIR=${RESCUE_DIR} obj ; \
	    make BINDIR=${RESCUE_DIR} depend all install clean cleandepend)
.endfor

initrd: rescue
	@${ECHO} ">>> Preparing initrd contents ..."
	(cd ${.CURDIR}/etc; \
	    mkdir ${INITRD_STAGEDIR}/etc; \
	    make DESTDIR="" BINDIR=${INITRD_STAGEDIR}/etc \
		depend all install)
	@${ECHO} ">>> Creating initrd.img.gz ..."
	sh ${.CURDIR}/mkinitrd.sh \
	    -b ${DESTDIR}/boot \
	    -r ${DESTDIR}${RESCUE_DIR} \
	    -c ${INITRD_STAGEDIR}
	(cd ${.CURDIR}/etc; make clean cleandepend)

clean:
	@${ECHO} ">>> Cleaning ..."
.for _dir in bin oinit sbin sbin.libcrypto sbin.lvm
	(cd ${.CURDIR}/${_dir}; \
	    ${ECHO} "=> ${_dir}"; \
	    make BINDIR=${RESCUE_DIR} clean cleandepend)
.endfor

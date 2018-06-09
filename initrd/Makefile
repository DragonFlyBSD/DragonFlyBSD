INITRD_MAKE= make -m ${.CURDIR}/../share/mk

# Target directory to install the rescue tools
RESCUE_DIR?=		/rescue

# Temporary directory to stage the initrd files (e.g., /etc)
# NOTE: Ignore ${DESTDIR} when staging files into this directory
INITRD_STAGEDIR!=	mktemp -d -t initrd-stage

.END:
	@rm -rf ${INITRD_STAGEDIR}

rescue:
	@${ECHO} ">>> Building and installing rescue tools ..."
.for _dir in bin oinit sbin sbin.libcrypto sbin.lvm
	(cd ${.CURDIR}/${_dir}; \
	    ${ECHO} "=> ${_dir}"; \
	    ${INITRD_MAKE} BINDIR=${RESCUE_DIR} clean cleandepend; \
	    ${INITRD_MAKE} BINDIR=${RESCUE_DIR} obj; \
	    ${INITRD_MAKE} BINDIR=${RESCUE_DIR} \
		depend all install clean cleandepend)
.endfor

initrd: rescue
	@${ECHO} ">>> Preparing initrd contents ..."
	(cd ${.CURDIR}/etc; \
	    mkdir ${INITRD_STAGEDIR}/etc; \
	    ${INITRD_MAKE} BINDIR=${INITRD_STAGEDIR}/etc depend all; \
	    ${INITRD_MAKE} BINDIR=${INITRD_STAGEDIR}/etc DESTDIR="" install)
	@${ECHO} ">>> Creating initrd.img.gz ..."
	sh ${.CURDIR}/mkinitrd.sh \
	    -b ${DESTDIR}/boot \
	    -r ${DESTDIR}${RESCUE_DIR} \
	    -c ${INITRD_STAGEDIR}
	(cd ${.CURDIR}/etc; ${INITRD_MAKE} clean cleandepend)

clean:
	@${ECHO} ">>> Cleaning ..."
.for _dir in bin oinit sbin sbin.libcrypto sbin.lvm
	(cd ${.CURDIR}/${_dir}; \
	    ${ECHO} "=> ${_dir}"; \
	    ${INITRD_MAKE} BINDIR=${RESCUE_DIR} clean cleandepend)
.endfor

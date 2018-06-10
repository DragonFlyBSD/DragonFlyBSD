INITRD_MAKE= make -m ${.CURDIR}/../share/mk

# Target directory to install the rescue tools
RESCUE_DIR?=		/rescue

# Temporary directory to stage the initrd files (e.g., /etc)
# NOTE: Ignore ${DESTDIR} when staging files into this directory
INITRD_STAGEDIR!=	mktemp -d -t initrd-stage

.END:
	@rm -rf ${INITRD_STAGEDIR}

rescue: .PHONY
	@${ECHO} ">>> Building and installing rescue tools ..."
.for _dir in oinit rescue rescue.libcrypto
.if !defined(NO_CLEAN)
	(cd ${.CURDIR}/${_dir}; \
	    ${INITRD_MAKE} BINDIR=${RESCUE_DIR} clean cleandepend)
.endif
	(cd ${.CURDIR}/${_dir}; \
	    ${ECHO} "=> ${_dir}"; \
	    ${INITRD_MAKE} BINDIR=${RESCUE_DIR} obj; \
	    ${INITRD_MAKE} BINDIR=${RESCUE_DIR} depend all install)
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

clean:
	@${ECHO} ">>> Cleaning ..."
.for _dir in etc oinit rescue rescue.libcrypto
	(cd ${.CURDIR}/${_dir}; \
	    ${ECHO} "=> ${_dir}"; \
	    ${INITRD_MAKE} BINDIR=${RESCUE_DIR} clean cleandepend)
.endfor

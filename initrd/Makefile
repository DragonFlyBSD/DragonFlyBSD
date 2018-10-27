INITRD_MAKE=	LC_ALL=C \
		make -m ${.CURDIR}/../share/mk

# Target directory to install the rescue tools
RESCUE_DIR?=		/rescue

# Temporary directory to stage the initrd files (e.g., /etc)
# NOTE: Ignore ${DESTDIR} when staging files into this directory
INITRD_STAGEDIR!=	mktemp -d -t initrd-stage

.END:
	@rm -rf ${INITRD_STAGEDIR}

_build_rescue:
	@echo ">>> Building rescue tools ..."
.for _dir in oinit rescue rescue.libcrypto
	(cd ${.CURDIR}/${_dir} && \
	    ${INITRD_MAKE} obj && \
	    ${INITRD_MAKE} depend all)
.endfor

_install_rescue:
	@echo ">>> Installing rescue tools ..."
.for _dir in oinit rescue rescue.libcrypto
	(cd ${.CURDIR}/${_dir} && \
	    ${INITRD_MAKE} BINDIR=${RESCUE_DIR} install)
.endfor

_stage_initrd:
	@echo ">>> Staging initrd contents ..."
	(cd ${.CURDIR}/etc && \
	    mkdir ${INITRD_STAGEDIR}/etc && \
	    ${INITRD_MAKE} obj && \
	    ${INITRD_MAKE} depend all && \
	    ${INITRD_MAKE} BINDIR=${INITRD_STAGEDIR}/etc DESTDIR="" install)

_make_initrd:
	@echo ">>> Creating initrd.img.gz ..."
	sh ${.CURDIR}/mkinitrd.sh \
	    -b ${DESTDIR}/boot \
	    -r ${DESTDIR}${RESCUE_DIR} \
	    -c ${INITRD_STAGEDIR}

_clean_rescue:
	@echo ">>> Cleaning rescue ..."
.for _dir in oinit rescue rescue.libcrypto
	(cd ${.CURDIR}/${_dir} && ${INITRD_MAKE} clean cleandepend)
.endfor

_clean_initrd:
	@echo ">>> Cleaning initrd ..."
	(cd ${.CURDIR}/etc && ${INITRD_MAKE} clean cleandepend)

clean: _clean_rescue _clean_initrd

_quickrescue: _build_rescue _install_rescue
rescue: _clean_rescue _quickrescue .PHONY
.ORDER: _clean_rescue _build_rescue _install_rescue

_quickinitrd: _stage_initrd _make_initrd
_initrd: _clean_initrd _quickinitrd
.ORDER: _clean_initrd _stage_initrd _make_initrd

quickrescue: _quickrescue
quickinitrd: _quickrescue _quickinitrd
initrd: rescue _initrd
.ORDER: _quickrescue _quickinitrd
.ORDER: rescue _initrd

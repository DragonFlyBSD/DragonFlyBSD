SUBDIR= amdsbwd cmx cpuctl dcons ecc ichwd ipmi joy kbdmux lpbb \
	nmdm pcfclock putter snp syscons tbridge coremctl dimm aperf

.if ${MACHINE_ARCH} == "x86_64"
SUBDIR+=efirt
.endif

# Empty for concurrent build
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>

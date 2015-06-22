SUBDIR=	aesni cryptodev hifn padlock rdrand rndtest safe tpm ubsec

.if ${MACHINE_ARCH} == "i386"
SUBDIR+=glxsb
.endif

# Empty for concurrent build
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>

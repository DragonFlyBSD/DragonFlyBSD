SUBDIR= an age alc ale ath bce bfe bge bwn \
	dc em ep et \
	fwe fxp ic iwi iwn jme lge lnc \
	mii_layer my msk mxge ndis nfe nge oce pcn \
	ral re rl sbsh sf sis sk sln ste stge ti tl tx txp \
	vge vr vx wb wi wpi xe xl ig_hal emx ae igb ix bnx

.if ${MACHINE_ARCH} == "i386"
SUBDIR+=ar ed sbni sr
.endif

# Empty for concurrent build
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>

SUBDIR= an age alc ale ar ath bce bfe bge \
	dc ed em ep et \
	fwe fxp ic iwi iwn jme lge lnc \
	mii_layer my msk mxge ndis nfe nge oce pcn \
	ral re rl sbni sbsh sf sis sk sln sr ste stge ti tl tx txp \
	vge vr vx wb wi wpi xe xl ig_hal emx ae igb ixgbe bnx

# XXX need to be updated to the new net80211 stack
# SUBDIR+= acx bwi iwl rtw
# .endif
#
# ...and to the new USB stack
# .if !defined(WANT_USB4BSD)
# SUBDIR+= rum ural
# .endif

.if !defined(WANT_USB4BSD)
SUBDIR+=aue axe cue kue lgue rue
.endif

.include <bsd.subdir.mk>

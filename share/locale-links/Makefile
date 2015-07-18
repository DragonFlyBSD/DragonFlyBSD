# This creates short versions of locales as symlinks to full versions
# e.g. en_GB is a symlink to en_GB.UTF-8.
# The UTF-8 version will always be the target if available

UTF8=	af_ZA am_ET be_BY bg_BG ca_AD ca_ES ca_FR ca_IT \
	cs_CZ da_DK de_AT de_CH de_DE el_GR en_AU en_CA \
	en_GB en_IE en_NZ en_US es_ES et_EE eu_ES fi_FI \
	fr_BE fr_CA fr_CH fr_FR he_IL hr_HR hu_HU hy_AM \
	is_IS it_CH it_IT ja_JP kk_KZ ko_KR lt_LT lv_LV \
	mn_MN nb_NO nl_BE nl_NL nn_NO no_NO pl_PL pt_BR \
	pt_PT ro_RO ru_RU sk_SK sl_SI sr_YU sv_SE tr_TR \
	uk_UA zh_CN zh_HK zh_TW

LOCALEDIR=	/usr/share/locale

SYMLINKS=	hi_IN.ISCII-DEV ${LOCALEDIR}/hi_IN \
		la_LN.ISO8859-1 ${LOCALEDIR}/la_LN
.for symdir in ${UTF8}
SYMLINKS+=	${symdir}.UTF-8 ${LOCALEDIR}/${symdir}
.endfor

.include <bsd.prog.mk>

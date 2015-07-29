# This creates short versions of locales as symlinks to full versions
# e.g. en_GB is a symlink to en_GB.UTF-8.
# The UTF-8 version will always be the target if available

UTF8=	af_ZA am_ET be_BY bg_BG ca_AD ca_ES ca_FR ca_IT \
	cs_CZ da_DK de_AT de_CH de_DE el_GR en_AU en_CA \
	en_GB en_IE en_NZ en_US es_ES et_EE eu_ES fi_FI \
	fr_BE fr_CA fr_CH fr_FR he_IL hi_IN hr_HR hu_HU \
	hy_AM is_IS it_CH it_IT ja_JP kk_KZ ko_KR lt_LT \
	lv_LV nb_NO nl_BE nl_NL nn_NO pl_PL pt_BR pt_PT \
	ro_RO ru_RU sk_SK sl_SI sv_SE tr_TR uk_UA \
	mn_Cyrl_MN \
	sr_Cyrl_RS \
	sr_Latn_RS \
	zh_Hans_CN \
	zh_Hant_HK \
	zh_Hant_TW

EURO=	af_ZA ca_AD ca_ES ca_FR ca_IT da_DK de_AT de_CH \
	de_DE en_AU en_CA en_GB en_NZ en_US en_ZA es_ES \
	et_EE eu_ES fi_FI fr_BE fr_CA fr_CH fr_FR is_IS \
	it_CH it_IT la_LN nb_NO nl_BE nl_NL nn_NO pt_PT \
	sv_SE

STD1=	${EURO:Net_EE} pt_BR la_LN
STD2=	cs_CZ hr_HR hu_HU pl_PL ro_RO sk_SK sl_SI sr_Latn_RS
STD5=	be_BY ru_RU uk_UA sr_Cyrl_RS
MANUAL=	lt_LT el_GR tr_TR lv_LV

LOCALEDIR=	/usr/share/locale

SYMLINKS=	lt_LT.ISO8859-4 ${LOCALEDIR}/lt_LT \
		el_GR.ISO8859-7 ${LOCALEDIR}/el_GR \
		tr_TR.ISO8859-9 ${LOCALEDIR}/tr_TR \
		lv_LV.ISO8859-13 ${LOCALEDIR}/lv_LV
.for symdir in ${STD1}
SYMLINKS+=	${symdir}.ISO8859-1 ${LOCALEDIR}/${symdir}
.endfor
.for symdir in ${STD2}
SYMLINKS+=	${symdir}.ISO8859-2 ${LOCALEDIR}/${symdir}
.endfor
.for symdir in ${STD5}
SYMLINKS+=	${symdir}.ISO8859-5 ${LOCALEDIR}/${symdir}
.endfor
.for symdir in ${UTF8}
.  if ! (${STD1:M${symdir}} || ${STD2:M${symdir}} || \
         ${STD5:M${symdir}} || ${MANUAL:M${symdir}})
SYMLINKS+=	${symdir}.UTF-8 ${LOCALEDIR}/${symdir}
.  endif
SYMLINKS+=	${symdir}.UTF-8 ${LOCALEDIR}/${symdir}.UTF8
.endfor
.for symdir in ${EURO}
SYMLINKS+=	${symdir}.ISO8859-15 ${LOCALEDIR}/${symdir}@euro
SYMLINKS+=	${symdir}.ISO8859-15 ${LOCALEDIR}/${symdir}.ISO-8859-15@euro
.endfor

.include <bsd.prog.mk>

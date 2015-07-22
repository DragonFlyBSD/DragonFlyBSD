# $FreeBSD: head/share/numericdef/Makefile 245889 2013-01-24 17:00:11Z brooks $

LOCALES=	af_ZA.ISO8859-1 \
		af_ZA.UTF-8 \
		be_BY.CP1251 \
		bg_BG.CP1251 \
		ca_ES.ISO8859-1 \
		cs_CZ.ISO8859-2 \
		da_DK.ISO8859-1 \
		de_CH.ISO8859-1 \
		de_CH.UTF-8 \
		de_DE.ISO8859-1 \
		de_DE.UTF-8 \
		el_GR.ISO8859-7 \
		en_US.ISO8859-1 \
		en_US.UTF-8 \
		es_ES.ISO8859-1 \
		et_EE.ISO8859-15 \
		eu_ES.ISO8859-1 \
		fi_FI.ISO8859-1 \
		fr_BE.ISO8859-1 \
		fr_FR.ISO8859-1 \
		hi_IN.ISCII-DEV \
		hr_HR.ISO8859-2 \
		hu_HU.ISO8859-2 \
		hy_AM.ARMSCII-8 \
		hy_AM.UTF-8 \
		is_IS.ISO8859-1 \
		it_IT.ISO8859-1 \
		ja_JP.eucJP \
		kk_KZ.PT154 \
		ko_KR.eucKR \
		lt_LT.ISO8859-13 \
		nl_NL.ISO8859-1 \
		nb_NO.ISO8859-1 \
		pl_PL.ISO8859-2 \
		pt_BR.ISO8859-1 \
		pt_PT.ISO8859-1 \
		ro_RO.ISO8859-2 \
		ru_RU.KOI8-R \
		sk_SK.ISO8859-2 \
		sl_SI.ISO8859-2 \
		sr_YU.ISO8859-2 \
		sr_YU.ISO8859-5 \
		sv_SE.ISO8859-1 \
		tr_TR.ISO8859-9 \
		uk_UA.KOI8-U \
		zh_CN.eucCN

LOCALEDIR=	/usr/share/locale

.SUFFIXES: .src .out

.src.out:
	grep -v '^#' < ${.IMPSRC} > ${.TARGET}

FILES=		${LOCALES:S/$/.out/}
FILESNAME=	LC_NUMERIC

.for locale in ${LOCALES}
FILESDIR_${locale}.out= ${LOCALEDIR}/${locale}
.endfor

CLEANFILES=	${FILES}

ENCODINGS=	ARMSCII-8 Big5 Big5HKSCS \
		CP1131 CP1251 CP866 CP949 \
		eucCN eucJP eucKR GB18030 GB2312 GBK \
		ISO8859-1 ISO8859-2 ISO8859-4 ISO8859-5 ISO8859-7 ISO8859-9 \
		ISO8859-13 ISO8859-15 \
		KOI8-R KOI8-U PT154 SJIS US-ASCII UTF-8

# Only for ISO and other codesets (explicitly: not UTF-8)
BE_LINKS=	fr_BE:nl_BE
CA_LINKS=	ca_ES:ca_AD ca_ES:ca_FR ca_ES:ca_IT
DE_LINKS=	de_DE:de_AT
FR_LINKS=	fr_FR:fr_CA fr_FR:fr_CH
GB_LINKS=	en_GB:en_AU en_GB:en_NZ
IT_LINKS=	it_IT:it_CH
NO_LINKS=	nb_NO:nn_NO
US_LINKS=	en_US:en_CA en_US:en_GB

# Identical UTF-8 styles
# DOT_COMMA_3  copies en_US
# DOT_TICK_3   copies de_CH
# COMMA_DOT    copies hy_AM
# COMMA_DOT_3  copies de_DE
# COMMA_NBSP_3 copies af_ZA

DOT_COMMA_3=	en_US:am_ET en_US:en_AU en_US:en_CA en_US:en_GB \
		en_US:en_IE en_US:en_NZ en_US:he_IL en_US:ja_JP \
		en_US:ko_KR en_US:mn_MN en_US:sr_YU en_US:zh_CN \
		en_US:zh_HK en_US:zh_TW
DOT_TICK_3=	de_CH:fr_CH de_CH:it_CH
COMMA_DOT_3=	de_DE:ca_AD de_DE:ca_ES de_DE:ca_FR de_DE:ca_IT \
		de_DE:da_DK de_DE:de_AT de_DE:el_GR de_DE:es_ES \
		de_DE:eu_ES de_DE:fr_BE de_DE:hr_HR de_DE:is_IS \
		de_DE:it_IT de_DE:lt_LT de_DE:nl_BE de_DE:nl_NL \
		de_DE:pt_BR de_DE:ro_RO de_DE:sl_SI de_DE:tr_TR
COMMA_NBSP_3=	af_ZA:be_BY af_ZA:bg_BG af_ZA:cs_CZ af_ZA:et_EE \
		af_ZA:fi_FI af_ZA:fr_CA af_ZA:fr_FR af_ZA:hu_HU \
		af_ZA:kk_KZ af_ZA:lv_LV af_ZA:nb_NO af_ZA:nn_NO \
		af_ZA:pl_PL af_ZA:pt_PT af_ZA:ru_RU af_ZA:sk_SK \
		af_ZA:sv_SE af_ZA:uk_UA

CP1251_CP1131=	be_BY
CP1251_ISO8859-5= be_BY
eucCN_Big5=	zh_CN:zh_TW
eucCN_Big5HKSCS= zh_CN:zh_HK
eucCN_GB18030=	zh_CN
eucCN_GB2312=	zh_CN
eucCN_GBK=	zh_CN
eucJP_SJIS=	ja_JP
eucKR_CP949=	ko_KR
ISO8859-1_ISO8859-15= af_ZA ca_ES da_DK de_CH de_DE en_US es_ES eu_ES \
		fi_FI fr_BE fr_FR is_IS it_IT nl_NL pt_PT sv_SE \
		${BE_LINKS} ${CA_LINKS} ${DE_LINKS} ${FR_LINKS} ${GB_LINKS} \
		${IT_LINKS} ${NO_LINKS} ${US_LINKS}
ISO8859-1_ISO8859-1= ${BE_LINKS} ${CA_LINKS} ${DE_LINKS} ${FR_LINKS} \
		${GB_LINKS} ${IT_LINKS} ${NO_LINKS} ${US_LINKS}
ISO8859-1_US-ASCII= en_US ${GB_LINKS} ${US_LINKS}
ISO8859-5_CP1251= uk_UA
ISO8859-13_ISO8859-4= lt_LT
ISO8859-13_ISO8859-13= lt_LT:lv_LV
KOI8-R_CP1251=	ru_RU
KOI8-R_CP866=	ru_RU
KOI8-R_ISO8859-5= ru_RU
KOI8-U_ISO8859-5= uk_UA
UTF-8_UTF-8=	${DOT_COMMA_3} ${COMMA_NBSP_3} ${COMMA_DOT_3} ${DOT_TICK_3}

SYMLINKS=
.for enc1 in ${ENCODINGS}
.for enc2 in ${ENCODINGS}
.for lang_terr in ${${enc1}_${enc2}}
SYMLINKS+=	../${lang_terr:C/:.*$//}.${enc1}/${FILESNAME} \
		${LOCALEDIR}/${lang_terr:C/^.*://}.${enc2}
.endfor
.endfor
.endfor

.include <bsd.prog.mk>

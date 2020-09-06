# This version of librecrypto is from LibreSSL
.include "Makefile.inc"

LIB= 		private_crypto
SHLIB_MAJOR=	46
PRIVATELIB=	shpub

SHLIBDIR?=	/lib
WARNS?=		2
NO_STRICT_ALIASING=

INCS+=  	aes.h asn1.h asn1t.h bio.h blowfish.h bn.h
INCS+=  	buffer.h camellia.h cast.h chacha.h cmac.h comp.h conf.h
INCS+=  	conf_api.h crypto.h curve25519.h des.h dh.h dsa.h dso.h
INCS+=  	ec.h ecdh.h ecdsa.h err.h evp.h gost.h
INCS+=  	hkdf.h hmac.h idea.h lhash.h md4.h md5.h modes.h
INCS+=		obj_mac.h objects.h ocsp.h
INCS+=		opensslconf.h opensslfeatures.h opensslv.h ossl_typ.h
INCS+=  	pem.h pem2.h pkcs12.h pkcs7.h poly1305.h rand.h rc2.h rc4.h
INCS+=  	ripemd.h rsa.h safestack.h sha.h sm3.h sm4.h stack.h
INCS+=  	ts.h txt_db.h ui.h whrlpool.h x509.h x509_vfy.h x509v3.h
INCSDIR=	${INCLUDEDIR}/priv/openssl
# explicitly disabled: engine.h
# not needed: ui_compat.h

CFLAGS+=	-I${LCRYPTO_SRC}/modes -I${LCRYPTO_SRC}/asn1
CFLAGS+=	-I${LCRYPTO_SRC}/bn -I${LCRYPTO_SRC}/ec -I${LCRYPTO_SRC}/ecdsa
CFLAGS+=	-I${LCRYPTO_SRC}/ecdh -I${LCRYPTO_SRC}/cms
CFLAGS+=	-I${LCRYPTO_SRC}/evp -I${.OBJDIR}

CFLAGS+=	-DLIBRESSL_INTERNAL
CFLAGS+=	-D__STRICT_ALIGNMENT

# Do not use ASM if -flto for now
.if ${CFLAGS:M-flto}
CFLAGS+=	-DOPENSSL_NO_ASM
NOASM_AES=	aes_cbc.c aes_core.c
NOASM_CAMELLIA=	camellia.c cmll_cbc.c
NOASM_RC4=	rc4_enc.c rc4_skey.c
NOASM_WHRLPOOL=	wp_block.c
.else
FLAGS_GROUPS=	asms asm

asms_FLAGS=	-DHAVE_GNU_STACK
asms_FLAGS_FILES=${ASM_X86_64_ELF}

asm_FLAGS=	-DOPENSSL_CPUID_OBJ
asm_FLAGS+=	-DAES_ASM -DBSAES_ASM -DVPAES_ASM
asm_FLAGS+=	-DOPENSSL_IA32_SSE2
asm_FLAGS+=	-DOPENSSL_BN_ASM_MONT -DOPENSSL_BN_ASM_MONT5 -DOPENSSL_BN_ASM_GF2m
asm_FLAGS+=	-DGHASH_ASM
asm_FLAGS+=	-DMD5_ASM
asm_FLAGS+=	-DSHA1_ASM -DSHA256_ASM -DSHA512_ASM
asm_FLAGS+=	-DWHIRLPOOL_ASM
# unavailable: -DRSA_ASM
asm_FLAGS_FILES=	cryptlib.c
asm_FLAGS_FILES+=	aes_core.c e_aes.c e_aes_cbc_hmac_sha1.c
asm_FLAGS_FILES+=	e_aes.c gcm128.c
asm_FLAGS_FILES+=	bn_asm.c bn_exp.c bn_mont.c ec_cvt.c bn_gf2m.c
asm_FLAGS_FILES+=	gcm128.c
asm_FLAGS_FILES+=	md5_dgst.c
asm_FLAGS_FILES+=	sha1dgst.c sha256.c sha512.c
asm_FLAGS_FILES+=	wp_block.c

ASM_X86_64_AES=		aes-elf-x86_64.S bsaes-elf-x86_64.S vpaes-elf-x86_64.S \
			aesni-elf-x86_64.S aesni-sha1-elf-x86_64.S
ASM_X86_64_BN=		modexp512-elf-x86_64.S mont-elf-x86_64.S \
			mont5-elf-x86_64.S gf2m-elf-x86_64.S
ASM_X86_64_CAMELLIA=	cmll-elf-x86_64.S
ASM_X86_64_MD5=		md5-elf-x86_64.S
ASM_X86_64_MODES=	ghash-elf-x86_64.S
ASM_X86_64_RC4=		rc4-elf-x86_64.S rc4-md5-elf-x86_64.S
ASM_X86_64_SHA=		sha1-elf-x86_64.S sha256-elf-x86_64.S sha512-elf-x86_64.S
ASM_X86_64_WHRLPOOL=	wp-elf-x86_64.S
ASM_X86_64_ELF=	${ASM_X86_64_AES} ${ASM_X86_64_BN} ${ASM_X86_64_CAMELLIA} \
		${ASM_X86_64_MD5} ${ASM_X86_64_MODES} ${ASM_X86_64_RC4} \
		${ASM_X86_64_SHA} ${ASM_X86_64_WHRLPOOL} cpuid-elf-x86_64.S
NOASM_AES=
NOASM_CAMELLIA=
NOASM_RC4=
NOASM_WHRLPOOL=
.endif

# Based on vendor libcrypto_la_SOURCES definitions with subdirectories stripped out

SRC_TOP=	cpt_err.c cryptlib.c crypto_init.c crypto_lock.c cversion.c \
		ex_data.c malloc-wrapper.c mem_dbg.c o_init.c o_time.c
# not needed: mem_clr.c o_str.c
SRC_AES=	aes_ige.c aes_misc.c aes_wrap.c ${NOASM_AES}
# not needed: aes_cfb.c aes_ctr.c aes_ecb.c aes_ofb.c
SRC_ASN1=	a_bitstr.c a_bool.c a_d2i_fp.c a_digest.c a_dup.c a_enum.c \
		a_i2d_fp.c a_int.c a_mbstr.c a_object.c a_octet.c a_print.c \
		a_sign.c a_strex.c a_strnid.c a_time.c \
		a_time_tm.c a_type.c a_utf8.c a_verify.c ameth_lib.c \
		asn1_err.c asn1_gen.c asn1_lib.c asn1_par.c asn_mime.c \
		asn_moid.c asn_pack.c bio_asn1.c bio_ndef.c d2i_pr.c \
		evp_asn1.c f_int.c f_string.c i2d_pr.c \
		n_pkey.c nsseq.c p5_pbe.c p5_pbev2.c p8_pkey.c \
		t_crl.c t_pkey.c t_req.c t_spki.c t_x509.c t_x509a.c \
		tasn_dec.c tasn_enc.c tasn_fre.c tasn_new.c tasn_prn.c \
		tasn_typ.c tasn_utl.c x_algor.c x_attrib.c x_bignum.c \
		x_crl.c x_exten.c x_info.c x_long.c x_name.c x_nx509.c \
		x_pkey.c x_pubkey.c x_req.c x_sig.c x_spki.c \
		x_val.c x_x509.c x_x509a.c
# not needed: d2i_pu.c f_enum.c i2d_pu.c t_bitst.c
SRC_BF=		bf_cfb64.c bf_ecb.c bf_enc.c bf_ofb64.c bf_skey.c
SRC_BIO=	b_dump.c b_posix.c b_print.c b_sock.c bf_buff.c bf_nbio.c \
		bio_cb.c bio_err.c bio_lib.c bss_acpt.c \
		bss_conn.c bss_dgram.c bss_fd.c bss_file.c \
		bss_mem.c bss_null.c bss_sock.c
# not needed: bf_null.c bio_meth.c bss_bio.c bss_log.c
SRC_BN=		bn_add.c bn_asm.c bn_blind.c bn_const.c bn_ctx.c \
		bn_div.c bn_err.c bn_exp.c bn_exp2.c bn_gcd.c bn_gf2m.c \
		bn_kron.c bn_lib.c bn_mod.c bn_mont.c bn_mul.c \
		bn_nist.c bn_prime.c bn_print.c bn_rand.c bn_recp.c \
		bn_shift.c bn_sqr.c bn_sqrt.c bn_word.c
# not needed: bn_depr.c bn_mpi.c bn_x931p.c
SRC_BUFFER=	buf_err.c buffer.c
# not needed: buf_str.c
SRC_CAMELLIA=	cmll_cfb.c cmll_ctr.c cmll_ecb.c cmll_misc.c \
		cmll_ofb.c ${NOASM_CAMELLIA}
# not needed but included: cmll_ctr.c
SRC_CAST=	c_cfb64.c c_ecb.c c_enc.c c_ofb64.c c_skey.c
SRC_CHACHA=	chacha.c
SRC_CMAC=	cm_ameth.c cm_pmeth.c cmac.c
SRC_CMS=	cms_asn1.c cms_cd.c cms_enc.c cms_err.c cms_io.c cms_pwri.c \
		cms_smime.c cms_att.c cms_dd.c cms_env.c cms_ess.c cms_kari.c \
		cms_lib.c cms_sd.c
SRC_COMP=
# not needed: c_rle.c c_zlib.c comp_err.c comp_lib.c
SRC_CONF=	conf_api.c conf_def.c conf_err.c conf_lib.c \
		conf_mall.c conf_mod.c conf_sap.c
SRC_CURVE25519= curve25519-generic.c curve25519.c
SRC_DES=	cbc_enc.c cfb64ede.c cfb64enc.c cfb_enc.c \
		des_enc.c ecb3_enc.c ecb_enc.c \
		fcrypt.c fcrypt_b.c ofb64ede.c ofb64enc.c \
		rand_key.c set_key.c xcbc_enc.c
# not needed: cbc_cksm.c ede_cbcm_enc.c enc_read.c enc_writ.c
# not needed: ofb_enc.c pcbc_enc.c qud_cksm.c str2key.c
SRC_DH=		dh_ameth.c dh_asn1.c dh_check.c dh_err.c dh_gen.c \
		dh_key.c dh_lib.c dh_pmeth.c dh_prn.c
# deprecated: dh_depr.c
# needed: dh_prn.c (for openssh/kexdhc.c)
SRC_DSA=	dsa_ameth.c dsa_asn1.c dsa_err.c dsa_gen.c \
		dsa_key.c dsa_lib.c dsa_ossl.c dsa_pmeth.c dsa_prn.c \
		dsa_sign.c dsa_vrf.c dsa_depr.c dsa_meth.c
# deprecated: dsa_depr.c (needed for contrib/ldns/keys.c)
# not needed but included: dsa_meth.c
SRC_DSO=	dso_err.c dso_lib.c dso_null.c dso_openssl.c
# explictly disabled: dso_dlfcn.c
SRC_EC=		ec2_mult.c ec2_oct.c ec2_smpl.c ec_ameth.c ec_asn1.c \
		ec_check.c ec_curve.c ec_cvt.c ec_err.c ec_key.c ec_kmeth.c \
		ec_lib.c ec_mult.c ec_oct.c ec_pmeth.c ec_print.c \
		eck_prn.c ecp_mont.c ecp_nist.c ecp_oct.c ecp_smpl.c
SRC_ECDH=	ecdh_kdf.c ech_err.c ech_key.c ech_lib.c
SRC_ECDSA=	ecs_asn1.c ecs_err.c ecs_lib.c ecs_ossl.c ecs_sign.c ecs_vrf.c
SRC_ENGINE=	# explicitly disabled
#SRC_ENGINE=	eng_all.c eng_cnf.c eng_ctrl.c eng_dyn.c eng_err.c eng_fat.c \
#		eng_init.c eng_lib.c eng_list.c eng_openssl.c eng_pkey.c \
#		eng_table.c tb_asnmth.c tb_cipher.c tb_dh.c tb_digest.c \
#		tb_dsa.c tb_ecdh.c tb_ecdsa.c tb_eckey.c tb_pkmeth.c \
#		tb_rand.c tb_rsa.c tb_store.c
SRC_ERR=	err.c err_all.c err_prn.c
SRC_EVP=	bio_b64.c bio_enc.c bio_md.c c_all.c digest.c e_aes.c \
		e_aes_cbc_hmac_sha1.c e_bf.c e_camellia.c e_cast.c e_chacha.c \
		e_chacha20poly1305.c e_des.c e_des3.c e_gost2814789.c \
		e_idea.c e_null.c e_rc2.c e_rc4.c e_rc4_hmac_md5.c e_sm4.c \
		e_xcbc_d.c encode.c evp_aead.c evp_enc.c evp_err.c evp_key.c \
		evp_lib.c evp_pbe.c evp_pkey.c m_dss.c m_dss1.c m_ecdsa.c \
		m_gost2814789.c m_gostr341194.c m_md4.c m_md5.c m_md5_sha1.c \
		m_ripemd.c m_sha1.c m_sigver.c m_sm3.c m_streebog.c m_wp.c \
		names.c p5_crpt.c p5_crpt2.c p_lib.c p_sign.c p_verify.c \
		pmeth_fn.c pmeth_gn.c pmeth_lib.c
# not needed: e_old.c m_null.c p_dec.c p_enc.c p_open.c p_seal.c
SRC_GOST=	gost2814789.c gost89_keywrap.c gost89_params.c gost89imit_ameth.c \
		gost89imit_pmeth.c gost_asn1.c gost_err.c gostr341001.c \
		gostr341001_ameth.c gostr341001_key.c gostr341001_params.c \
		gostr341001_pmeth.c gostr341194.c streebog.c
SRC_HKDF=	hkdf.c
SRC_HMAC=	hm_ameth.c hm_pmeth.c hmac.c
SRC_IDEA=	i_cbc.c i_cfb64.c i_ecb.c i_ofb64.c i_skey.c
SRC_LHASH=	lh_stats.c lhash.c
SRC_MD4=	md4_dgst.c md4_one.c
SRC_MD5=	md5_dgst.c md5_one.c
SRC_MODES=	cbc128.c ccm128.c cfb128.c ctr128.c gcm128.c \
		ofb128.c xts128.c
# not needed: cts128.c
SRC_OBJECTS=	o_names.c obj_dat.c obj_err.c obj_lib.c obj_xref.c
SRC_OCSP=	ocsp_asn.c ocsp_cl.c ocsp_err.c ocsp_ext.c ocsp_ht.c \
		ocsp_lib.c ocsp_prn.c ocsp_srv.c ocsp_vfy.c
SRC_PEM=	pem_all.c pem_err.c pem_info.c pem_lib.c pem_oth.c pem_pk8.c \
		pem_pkey.c pem_x509.c pem_xaux.c pvkfmt.c
# not needed: pem_seal.c pem_sign.c
SRC_PKCS12=	p12_add.c p12_asn.c p12_attr.c p12_crpt.c p12_crt.c \
		p12_decr.c p12_init.c p12_key.c p12_kiss.c p12_mutl.c \
		p12_p8d.c p12_p8e.c p12_utl.c pk12err.c
# not needed: p12_npas.c
SRC_PKCS7=	pk7_asn1.c pk7_attr.c pk7_doit.c pk7_lib.c \
		pk7_mime.c pk7_smime.c pkcs7err.c
# not needed: bio_pk7.c
SRC_POLY1305=	poly1305.c
SRC_RAND=	rand_err.c rand_lib.c
# not needed: randfile.c
SRC_RC2=	rc2_cbc.c rc2_ecb.c rc2_skey.c rc2cfb64.c rc2ofb64.c
SRC_RC4=	${NOASM_RC4}
SRC_RIPEMD=	rmd_dgst.c rmd_one.c
SRC_RSA=	rsa_ameth.c rsa_asn1.c rsa_chk.c rsa_crpt.c rsa_depr.c \
		rsa_eay.c rsa_err.c rsa_gen.c rsa_lib.c rsa_meth.c rsa_none.c \
		rsa_oaep.c rsa_pk1.c rsa_pmeth.c rsa_prn.c rsa_pss.c \
		rsa_sign.c rsa_x931.c
# deprecated but needed: rsa_depr.c (for openssh/sshkey.c)
# not needed: rsa_saos.c
SRC_SHA=	sha1_one.c sha1dgst.c sha256.c sha512.c
SRC_SM3=	sm3.c
SRC_SM4=	sm4.c
SRC_STACK=	stack.c
SRC_TS=		ts_asn1.c ts_conf.c ts_err.c ts_lib.c ts_req_print.c \
		ts_req_utils.c ts_rsp_print.c ts_rsp_sign.c ts_rsp_utils.c \
		ts_rsp_verify.c ts_verify_ctx.c
SRC_TXT_DB=	txt_db.c
SRC_UI=		ui_err.c ui_lib.c ui_openssl.c
# not needed: ui_util.c
SRC_WHRLPOOL=	wp_dgst.c ${NOASM_WHRLPOOL}
SRC_X509=	by_dir.c by_file.c by_mem.c x509_att.c x509_cmp.c x509_d2.c \
		x509_def.c x509_err.c x509_ext.c x509_lu.c x509_obj.c \
		x509_r2x.c x509_req.c x509_set.c x509_trs.c x509_txt.c \
		x509_v3.c x509_vfy.c x509_vpm.c x509cset.c x509name.c \
		x509rset.c x509spki.c x509type.c x_all.c
# not needed but included: x509_r2x.c
SRC_X509V3=	pcy_cache.c pcy_data.c pcy_lib.c pcy_map.c pcy_node.c \
		pcy_tree.c v3_akey.c v3_akeya.c v3_alt.c v3_bcons.c \
		v3_bitst.c v3_conf.c v3_cpols.c v3_crld.c v3_enum.c \
		v3_extku.c v3_genn.c v3_ia5.c v3_info.c v3_int.c v3_lib.c \
		v3_ncons.c v3_ocsp.c v3_pci.c v3_pcia.c v3_pcons.c v3_pku.c \
		v3_pmaps.c v3_prn.c v3_purp.c v3_skey.c v3_sxnet.c v3_utl.c \
		v3err.c

SRCS=		${ASM_X86_64_ELF} ${SRC_TOP} ${SRC_AES} ${SRC_ASN1} \
		${SRC_BF} ${SRC_BIO} ${SRC_BN} ${SRC_BUFFER} ${SRC_CAMELLIA} \
		${SRC_CAST} ${SRC_CHACHA} ${SRC_CMAC} ${SRC_CMS} ${SRC_COMP} \
		${SRC_CONF} ${SRC_DES} ${SRC_DH} ${SRC_DSA} ${SRC_DSO} \
		${SRC_EC} ${SRC_ECDH} ${SRC_ECDSA} ${SRC_ENGINE} ${SRC_ERR} \
		${SRC_EVP} ${SRC_GOST} ${SRC_HMAC} ${SRC_HKDF} ${SRC_IDEA} \
		${SRC_LHASH} ${SRC_MD4} ${SRC_MD5} ${SRC_MODES} \
		${SRC_OBJECTS} ${SRC_OCSP} ${SRC_PEM} ${SRC_PKCS12} \
		${SRC_PKCS7} ${SRC_POLY1305} ${SRC_RAND} ${SRC_RC2} \
		${SRC_RIPEMD} ${SRC_RSA} ${SRC_SHA} ${SRC_STACK} ${SRC_TS} \
		${SRC_TXT_DB} ${SRC_UI} ${SRC_WHRLPOOL} ${SRC_X509} \
		${SRC_X509V3} ${SRC_CURVE25519} \
		${SRC_RC4} ${SRC_SM3} ${SRC_SM4}

.PATH:	${LCRYPTO_SRC} \
	${LCRYPTO_SRC}/aes \
	${LCRYPTO_SRC}/asn1 \
	${LCRYPTO_SRC}/bf \
	${LCRYPTO_SRC}/bio \
	${LCRYPTO_SRC}/bn \
	${LCRYPTO_SRC}/buffer \
	${LCRYPTO_SRC}/camellia \
	${LCRYPTO_SRC}/cast \
	${LCRYPTO_SRC}/chacha \
	${LCRYPTO_SRC}/cmac \
	${LCRYPTO_SRC}/cms \
	${LCRYPTO_SRC}/conf \
	${LCRYPTO_SRC}/curve25519 \
	${LCRYPTO_SRC}/des \
	${LCRYPTO_SRC}/dh \
	${LCRYPTO_SRC}/dsa \
	${LCRYPTO_SRC}/dso \
	${LCRYPTO_SRC}/ec \
	${LCRYPTO_SRC}/ecdh \
	${LCRYPTO_SRC}/ecdsa \
	${LCRYPTO_SRC}/err \
	${LCRYPTO_SRC}/evp \
	${LCRYPTO_SRC}/gost \
	${LCRYPTO_SRC}/hmac \
	${LCRYPTO_SRC}/hkdf \
	${LCRYPTO_SRC}/idea \
	${LCRYPTO_SRC}/lhash \
	${LCRYPTO_SRC}/md4 \
	${LCRYPTO_SRC}/md5 \
	${LCRYPTO_SRC}/modes \
	${LCRYPTO_SRC}/objects \
	${LCRYPTO_SRC}/ocsp \
	${LCRYPTO_SRC}/pem \
	${LCRYPTO_SRC}/pkcs12 \
	${LCRYPTO_SRC}/pkcs7 \
	${LCRYPTO_SRC}/poly1305 \
	${LCRYPTO_SRC}/rand \
	${LCRYPTO_SRC}/rc2 \
	${LCRYPTO_SRC}/rc4 \
	${LCRYPTO_SRC}/ripemd \
	${LCRYPTO_SRC}/rsa \
	${LCRYPTO_SRC}/sha \
	${LCRYPTO_SRC}/sm3 \
	${LCRYPTO_SRC}/sm4 \
	${LCRYPTO_SRC}/stack \
	${LCRYPTO_SRC}/ts \
	${LCRYPTO_SRC}/txt_db \
	${LCRYPTO_SRC}/ui \
	${LCRYPTO_SRC}/whrlpool \
	${LCRYPTO_SRC}/x509 \
	${LCRYPTO_SRC}/x509v3 \
	${LIBRESSL_SRC}/include/openssl

.include <bsd.lib.mk>

# This version of libcrypto is from LibreSSL
.include "Makefile.inc"

LIB=            private_crypto
SHLIB_MAJOR=    38
PRIVATELIB=

SHLIBDIR?=	/lib
WARNS?=		2
NO_STRICT_ALIASING=

INCS+=  	aes.h asn1.h asn1_mac.h asn1t.h
INCS+=  	bio.h blowfish.h bn.h buffer.h
INCS+=  	camellia.h cast.h chacha.h cmac.h cms.h comp.h conf.h conf_api.h crypto.h
INCS+=  	des.h dh.h dsa.h dso.h
INCS+=  	ec.h ecdh.h ecdsa.h engine.h err.h evp.h
INCS+=  	gost.h hmac.h idea.h krb5_asn.h lhash.h
INCS+=  	md4.h md5.h modes.h
INCS+=  	obj_mac.h objects.h ocsp.h opensslconf.h opensslfeatures.h opensslv.h ossl_typ.h
INCS+=  	pem.h pem2.h pkcs12.h pkcs7.h poly1305.h
INCS+=  	rand.h rc2.h rc4.h ripemd.h rsa.h
INCS+=  	safestack.h sha.h stack.h
INCS+=  	ts.h txt_db.h ui.h ui_compat.h
INCS+=  	whrlpool.h x509.h x509_vfy.h x509v3.h
INCSDIR=	${INCLUDEDIR}/priv/openssl

CFLAGS+=	-I${LCRYPTO_SRC}/modes -I${LCRYPTO_SRC}/asn1 -I${LCRYPTO_SRC}/evp -I${.OBJDIR}

CFLAGS+=	-DAES_ASM \
		-DBSAES_ASM \
		-DVPAES_ASM \
		-DOPENSSL_IA32_SSE2 \
		-DOPENSSL_BN_ASM_MONT \
		-DOPENSSL_BN_ASM_MONT5 \
		-DOPENSSL_BN_ASM_GF2m \
		-DMD5_ASM \
		-DGHASH_ASM \
		-DRSA_ASM \
		-DSHA1_ASM \
		-DSHA256_ASM \
		-DSHA512_ASM \
		-DWHIRLPOOL_ASM \
		-DOPENSSL_CPUID_OBJ

FLAGS_GROUPS=		bzero
bzero_FLAGS=		-O0
bzero_FLAGS_FILES=	explicit_bzero.c

# Vendor ASM_X86_64_ELF definition with directories stripped off and
# rearranged one per row
ASM_X86_64_ELF=	aes-elf-x86_64.s \
		bsaes-elf-x86_64.s \
		vpaes-elf-x86_64.s \
		aesni-elf-x86_64.s \
		aesni-sha1-elf-x86_64.s \
		modexp512-elf-x86_64.s \
		mont-elf-x86_64.s \
		mont5-elf-x86_64.s \
		gf2m-elf-x86_64.s \
		cmll-elf-x86_64.s \
		md5-elf-x86_64.s \
		ghash-elf-x86_64.s \
		rc4-elf-x86_64.s \
		rc4-md5-elf-x86_64.s \
		sha1-elf-x86_64.s \
		sha256-elf-x86_64.S \
		sha512-elf-x86_64.S \
		wp-elf-x86_64.s \
		cpuid-elf-x86_64.S

# Based on vendor libcrypto_la_SOURCES definitions with subdirectories stripped out

SRC_TOP=	cpt_err.c cryptlib.c cversion.c ex_data.c malloc-wrapper.c \
		mem_clr.c mem_dbg.c o_init.c o_str.c o_time.c
SRC_AES=	aes_cfb.c aes_ctr.c aes_ecb.c aes_ige.c aes_misc.c aes_ofb.c \
		aes_wrap.c
SRC_ASN1=	a_bitstr.c a_bool.c a_bytes.c a_d2i_fp.c a_digest.c a_dup.c \
		a_enum.c a_i2d_fp.c a_int.c a_mbstr.c a_object.c a_octet.c \
		a_print.c a_set.c a_sign.c a_strex.c a_strnid.c a_time.c \
		a_time_tm.c a_type.c a_utf8.c a_verify.c ameth_lib.c \
		asn1_err.c asn1_gen.c asn1_lib.c asn1_par.c asn_mime.c \
		asn_moid.c asn_pack.c bio_asn1.c bio_ndef.c d2i_pr.c d2i_pu.c \
		evp_asn1.c f_enum.c f_int.c f_string.c i2d_pr.c i2d_pu.c \
		n_pkey.c nsseq.c p5_pbe.c p5_pbev2.c p8_pkey.c t_bitst.c \
		t_crl.c t_pkey.c t_req.c t_spki.c t_x509.c t_x509a.c \
		tasn_dec.c tasn_enc.c tasn_fre.c tasn_new.c tasn_prn.c \
		tasn_typ.c tasn_utl.c x_algor.c x_attrib.c x_bignum.c \
		x_crl.c x_exten.c x_info.c x_long.c x_name.c x_nx509.c \
		x_pkey.c x_pubkey.c x_req.c x_sig.c x_spki.c x_val.c x_x509.c \
		x_x509a.c
SRC_BF=		bf_cfb64.c bf_ecb.c bf_enc.c bf_ofb64.c bf_skey.c
SRC_BIO=	b_dump.c b_posix.c b_print.c b_sock.c bf_buff.c bf_nbio.c \
		bf_null.c bio_cb.c bio_err.c bio_lib.c bss_acpt.c bss_bio.c \
		bss_conn.c bss_dgram.c bss_fd.c bss_file.c bss_log.c \
		bss_mem.c bss_null.c bss_sock.c
SRC_BN=		bn_add.c bn_asm.c bn_blind.c bn_const.c bn_ctx.c bn_depr.c \
		bn_div.c bn_err.c bn_exp.c bn_exp2.c bn_gcd.c bn_gf2m.c \
		bn_kron.c bn_lib.c bn_mod.c bn_mont.c bn_mpi.c bn_mul.c \
		bn_nist.c bn_prime.c bn_print.c bn_rand.c bn_recp.c \
		bn_shift.c bn_sqr.c bn_sqrt.c bn_word.c bn_x931p.c
SRC_BUFFER=	buf_err.c buf_str.c buffer.c
SRC_CAMELLIA=	cmll_cfb.c cmll_ctr.c cmll_ecb.c cmll_misc.c cmll_ofb.c
SRC_CAST=	c_cfb64.c c_ecb.c c_enc.c c_ofb64.c c_skey.c
SRC_CHACHA=	chacha.c
SRC_CMAC=	cm_ameth.c cm_pmeth.c cmac.c
SRC_COMP=	c_rle.c c_zlib.c
SRC_CONF=	comp_err.c comp_lib.c conf_api.c conf_def.c \
		conf_err.c conf_lib.c conf_mall.c conf_mod.c conf_sap.c
SRC_DES=	cbc_cksm.c cbc_enc.c cfb64ede.c cfb64enc.c cfb_enc.c \
		des_enc.c ecb3_enc.c ecb_enc.c ede_cbcm_enc.c enc_read.c \
		enc_writ.c fcrypt.c fcrypt_b.c ofb64ede.c ofb64enc.c \
		ofb_enc.c pcbc_enc.c qud_cksm.c rand_key.c set_key.c \
		str2key.c xcbc_enc.c
SRC_DH=		dh_ameth.c dh_asn1.c dh_check.c dh_depr.c dh_err.c dh_gen.c \
		dh_key.c dh_lib.c dh_pmeth.c dh_prn.c
SRC_DSA=	dsa_ameth.c dsa_asn1.c dsa_depr.c dsa_err.c dsa_gen.c \
		dsa_key.c dsa_lib.c dsa_ossl.c dsa_pmeth.c dsa_prn.c \
		dsa_sign.c dsa_vrf.c
SRC_DSO=	dso_dlfcn.c dso_err.c dso_lib.c dso_null.c dso_openssl.c
SRC_EC=		ec2_mult.c ec2_oct.c ec2_smpl.c ec_ameth.c ec_asn1.c \
		ec_check.c ec_curve.c ec_cvt.c ec_err.c ec_key.c ec_lib.c \
		ec_mult.c ec_oct.c ec_pmeth.c ec_print.c eck_prn.c ecp_mont.c \
		ecp_nist.c ecp_oct.c ecp_smpl.c
SRC_ECDH=	ech_err.c ech_key.c ech_lib.c
SRC_ECDSA=	ecs_asn1.c ecs_err.c ecs_lib.c ecs_ossl.c ecs_sign.c ecs_vrf.c
SRC_ENGINE=	eng_all.c eng_cnf.c eng_ctrl.c eng_dyn.c eng_err.c eng_fat.c \
		eng_init.c eng_lib.c eng_list.c eng_openssl.c eng_pkey.c \
		eng_table.c tb_asnmth.c tb_cipher.c tb_dh.c tb_digest.c \
		tb_dsa.c tb_ecdh.c tb_ecdsa.c tb_pkmeth.c tb_rand.c \
		tb_rsa.c tb_store.c
SRC_ERR=	err.c err_all.c err_prn.c
SRC_EVP=	bio_b64.c bio_enc.c bio_md.c c_all.c digest.c e_aes.c \
		e_aes_cbc_hmac_sha1.c e_bf.c e_camellia.c e_cast.c e_chacha.c \
		e_chacha20poly1305.c e_des.c e_des3.c e_gost2814789.c \
		e_idea.c e_null.c e_old.c e_rc2.c e_rc4.c e_rc4_hmac_md5.c \
		e_xcbc_d.c encode.c evp_aead.c evp_enc.c evp_err.c evp_key.c \
		evp_lib.c evp_pbe.c evp_pkey.c m_dss.c m_dss1.c m_ecdsa.c \
		m_gost2814789.c m_gostr341194.c m_md4.c m_md5.c m_null.c \
		m_ripemd.c m_sha1.c m_sigver.c m_streebog.c m_wp.c names.c \
		p5_crpt.c p5_crpt2.c p_dec.c p_enc.c p_lib.c p_open.c \
		p_seal.c p_sign.c p_verify.c pmeth_fn.c pmeth_gn.c \
		pmeth_lib.c
SRC_GOST=	gost2814789.c gost89_keywrap.c gost89_params.c gost89imit_ameth.c \
		gost89imit_pmeth.c gost_asn1.c gost_err.c gostr341001.c \
		gostr341001_ameth.c gostr341001_key.c gostr341001_params.c \
		gostr341001_pmeth.c gostr341194.c streebog.c
SRC_HMAC=	hm_ameth.c hm_pmeth.c hmac.c
SRC_IDEA=	i_cbc.c i_cfb64.c i_ecb.c i_ofb64.c i_skey.c
SRC_KRB5=	krb5_asn.c
SRC_LHASH=	lh_stats.c lhash.c
SRC_MD4=	md4_dgst.c md4_one.c
SRC_MD5=	md5_dgst.c md5_one.c
SRC_MODES=	cbc128.c ccm128.c cfb128.c ctr128.c cts128.c gcm128.c \
		ofb128.c xts128.c
SRC_OBJECTS=	o_names.c obj_dat.c obj_err.c obj_lib.c obj_xref.c
SRC_OCSP=	ocsp_asn.c ocsp_cl.c ocsp_err.c ocsp_ext.c ocsp_ht.c \
		ocsp_lib.c ocsp_prn.c ocsp_srv.c ocsp_vfy.c
SRC_PEM=	pem_all.c pem_err.c pem_info.c pem_lib.c pem_oth.c pem_pk8.c \
		pem_pkey.c pem_seal.c pem_sign.c pem_x509.c pem_xaux.c \
		pvkfmt.c
SRC_PKCS12=	p12_add.c p12_asn.c p12_attr.c p12_crpt.c p12_crt.c \
		p12_decr.c p12_init.c p12_key.c p12_kiss.c p12_mutl.c \
		p12_npas.c p12_p8d.c p12_p8e.c p12_utl.c pk12err.c
SRC_PKCS7=	bio_pk7.c pk7_asn1.c pk7_attr.c pk7_doit.c pk7_lib.c \
		pk7_mime.c pk7_smime.c pkcs7err.c
SRC_POLY1305=	poly1305.c
SRC_RAND=	rand_err.c rand_lib.c randfile.c
SRC_RC2=	rc2_cbc.c rc2_ecb.c rc2_skey.c rc2cfb64.c rc2ofb64.c
SRC_RIPEMD=	rmd_dgst.c rmd_one.c
SRC_RSA=	rsa_ameth.c rsa_asn1.c rsa_chk.c rsa_crpt.c rsa_depr.c \
		rsa_eay.c rsa_err.c rsa_gen.c rsa_lib.c rsa_none.c \
		rsa_oaep.c rsa_pk1.c rsa_pmeth.c rsa_prn.c rsa_pss.c \
		rsa_saos.c rsa_sign.c rsa_ssl.c rsa_x931.c
SRC_SHA=	sha1_one.c sha1dgst.c sha256.c sha512.c
SRC_STACK=	stack.c
SRC_TS=		ts_asn1.c ts_conf.c ts_err.c ts_lib.c ts_req_print.c \
		ts_req_utils.c ts_rsp_print.c ts_rsp_sign.c ts_rsp_utils.c \
		ts_rsp_verify.c ts_verify_ctx.c
SRC_TXT_DB=	txt_db.c
SRC_UI=		ui_err.c ui_lib.c ui_openssl.c ui_util.c
SRC_WHRLPOOL=	wp_dgst.c
SRC_X509=	by_dir.c by_file.c by_mem.c x509_att.c x509_cmp.c x509_d2.c \
		x509_def.c x509_err.c x509_ext.c x509_lu.c x509_obj.c \
		x509_r2x.c x509_req.c x509_set.c x509_trs.c x509_txt.c \
		x509_v3.c x509_vfy.c x509_vpm.c x509cset.c x509name.c \
		x509rset.c x509spki.c x509type.c x_all.c
SRC_X509V3=	pcy_cache.c pcy_data.c pcy_lib.c pcy_map.c pcy_node.c \
		pcy_tree.c v3_akey.c v3_akeya.c v3_alt.c v3_bcons.c \
		v3_bitst.c v3_conf.c v3_cpols.c v3_crld.c v3_enum.c \
		v3_extku.c v3_genn.c v3_ia5.c v3_info.c v3_int.c v3_lib.c \
		v3_ncons.c v3_ocsp.c v3_pci.c v3_pcia.c v3_pcons.c v3_pku.c \
		v3_pmaps.c v3_prn.c v3_purp.c v3_skey.c v3_sxnet.c v3_utl.c \
		v3err.c

SRCS=		${ASM_X86_64_ELF} ${SRC_TOP} ${SRC_AES} ${SRC_ASN1} \
		${SRC_BF} ${SRC_BIO} ${SRC_BN} ${SRC_BUFFER} ${SRC_CAMELLIA} \
		${SRC_CAST} ${SRC_CHACHA} ${SRC_CMAC} ${SRC_COMP} \
		${SRC_CONF} ${SRC_DES} ${SRC_DH} ${SRC_DSA} ${SRC_DSO} \
		${SRC_EC} ${SRC_ECDH} ${SRC_ECDSA} ${SRC_ENGINE} ${SRC_ERR} \
		${SRC_EVP} ${SRC_GOST} ${SRC_HMAC} ${SRC_IDEA} ${SRC_KRB5} \
		${SRC_LHASH} ${SRC_MD4} ${SRC_MD5} ${SRC_MODES} \
		${SRC_OBJECTS} ${SRC_OCSP} ${SRC_PEM} ${SRC_PKCS12} \
		${SRC_PKCS7} ${SRC_POLY1305} ${SRC_RAND} ${SRC_RC2} \
		${SRC_RIPEMD} ${SRC_RSA} ${SRC_SHA} ${SRC_STACK} ${SRC_TS} \
		${SRC_TXT_DB} ${SRC_UI} ${SRC_WHRLPOOL} ${SRC_X509} \
		${SRC_X509V3}

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
	${LCRYPTO_SRC}/comp \
	${LCRYPTO_SRC}/compat \
	${LCRYPTO_SRC}/conf \
	${LCRYPTO_SRC}/des \
	${LCRYPTO_SRC}/dh \
	${LCRYPTO_SRC}/dsa \
	${LCRYPTO_SRC}/dso \
	${LCRYPTO_SRC}/ec \
	${LCRYPTO_SRC}/ecdh \
	${LCRYPTO_SRC}/ecdsa \
	${LCRYPTO_SRC}/engine \
	${LCRYPTO_SRC}/err \
	${LCRYPTO_SRC}/evp \
	${LCRYPTO_SRC}/gost \
	${LCRYPTO_SRC}/hmac \
	${LCRYPTO_SRC}/idea \
	${LCRYPTO_SRC}/krb5 \
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
	${LCRYPTO_SRC}/stack \
	${LCRYPTO_SRC}/ts \
	${LCRYPTO_SRC}/txt_db \
	${LCRYPTO_SRC}/ui \
	${LCRYPTO_SRC}/whrlpool \
	${LCRYPTO_SRC}/x509 \
	${LCRYPTO_SRC}/x509v3 \
	${LIBRESSL_SRC}/include/openssl

# compat/
SRCS+=	explicit_bzero.c reallocarray.c timingsafe_bcmp.c timingsafe_memcmp.c

.include <bsd.lib.mk>

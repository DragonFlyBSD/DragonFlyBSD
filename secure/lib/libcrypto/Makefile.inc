# $FreeBSD: src/secure/lib/libcrypto/Makefile.inc,v 1.7.2.11 2003/02/20 15:07:32 nectar Exp $
# $DragonFly: src/secure/lib/libcrypto/Makefile.inc,v 1.18 2008/09/27 21:04:45 pavalos Exp $

OSSLVERSION=	1.0.0g
OSSLDATE=	2012-01-18
LCRYPTO_SRC=	${.CURDIR}/../../../crypto/openssl
LCRYPTO_DOC=	${LCRYPTO_SRC}/doc

CFLAGS+=	-DDSO_DLFCN -DHAVE_DLFCN_H -DL_ENDIAN -DTERMIOS
CFLAGS+=	-DOPENSSL_THREADS
CFLAGS+=	-DOPENSSL_IA32_SSE2 -DOPENSSL_BN_ASM_MONT -DSHA1_ASM \
		-DSHA256_ASM -DSHA512_ASM -DMD5_ASM -DAES_ASM -DWHIRLPOOL_ASM
.if ${MACHINE_ARCH} == "i386"
CFLAGS+= -DOPENSSL_BN_ASM_PART_WORDS -DRMD160_ASM
.endif
CFLAGS+=	-I${LCRYPTO_SRC} -I${LCRYPTO_SRC}/crypto \
		-I${LCRYPTO_SRC}/crypto/asn1 -I${LCRYPTO_SRC}/crypto/evp \
		-I${LCRYPTO_SRC}/crypto/engine -I${.OBJDIR}

MANDIR=	${SHAREDIR}/openssl/man/man

.if defined(LIB)
_docs=	${LIB}
_sec=	3
.else
_docs=	apps
_sec=	1
.endif

man-update:
.for manpage in ${MAN}
	@(sec=${manpage:E}; \
	pod=${manpage:R}.pod; \
	cp ${LCRYPTO_DOC}/${_docs}/$$pod .; \
	pod2man --section=$$sec --release="${OSSLVERSION}" \
	  --date="${OSSLDATE}" --center="OpenSSL" \
	  $$pod > ${.CURDIR}/man/${manpage}; \
	rm $$pod; \
	${ECHO} ${manpage})
.endfor

man-makefile-update:
	rm -f ${.CURDIR}/Makefile.man
	echo '# DO NOT EDIT: generated from man-makefile-update target' >> \
	    ${.CURDIR}/Makefile.man
	for i in ${LCRYPTO_DOC}/${_docs}/*.pod; do \
		fn=`basename $$i .pod`; \
		sec=`perl ${LCRYPTO_SRC}/util/extract-section.pl < $$i`; \
		sec=$${sec:-${_sec}}; \
		${ECHO} "MAN+= $$fn.$$sec" >> ${.CURDIR}/Makefile.man; \
		perl ${LCRYPTO_SRC}/util/extract-names.pl < $$i | \
		  sed -Ee "/^$$fn\$$/d;s/^(.*)/MLINKS+= $$fn.$$sec \1.$$sec/" >> \
		    ${.CURDIR}/Makefile.man; \
	done

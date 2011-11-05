LIB=	gmp
INTERNALLIB=	# yes

CONTRIBDIR=	${.CURDIR}/../../../../contrib/gmp
.PATH:	${CONTRIBDIR}

.include "${.CURDIR}/Makefile.${MACHINE_ARCH}"

CFLAGS+=	-D__GMP_WITHIN_GMP -I${CONTRIBDIR} -I${.OBJDIR} -I${.CURDIR}
CFLAGS+=	-I${CONTRIBDIR}/mpn/generic
CFLAGS+=	-D__LONG_INT_BITS=${LONG_INT_BITS}
CFLAGS+=	-D__INT_BITS=${INT_BITS}
CFLAGS+=	-D__SHORT_BITS=${SHORT_BITS}
CFLAGS+=	-DHAVE_HOST_CPU_FAMILY_${CPU_FAMILY}=1


# The following comes verbatim from Makefile.am

MPF_OBJECTS = mpf/init$U.lo mpf/init2$U.lo mpf/inits$U.lo mpf/set$U.lo	    \
  mpf/set_ui$U.lo mpf/set_si$U.lo mpf/set_str$U.lo mpf/set_d$U.lo	    \
  mpf/set_z$U.lo mpf/iset$U.lo mpf/iset_ui$U.lo mpf/iset_si$U.lo	    \
  mpf/iset_str$U.lo mpf/iset_d$U.lo mpf/clear$U.lo mpf/clears$U.lo	    \
  mpf/get_str$U.lo mpf/dump$U.lo mpf/size$U.lo mpf/eq$U.lo mpf/reldiff$U.lo \
  mpf/sqrt$U.lo mpf/random2$U.lo mpf/inp_str$U.lo mpf/out_str$U.lo	    \
  mpf/add$U.lo mpf/add_ui$U.lo mpf/sub$U.lo mpf/sub_ui$U.lo mpf/ui_sub$U.lo \
  mpf/mul$U.lo mpf/mul_ui$U.lo mpf/div$U.lo mpf/div_ui$U.lo		    \
  mpf/cmp$U.lo mpf/cmp_d$U.lo mpf/cmp_ui$U.lo mpf/cmp_si$U.lo		    \
  mpf/mul_2exp$U.lo mpf/div_2exp$U.lo mpf/abs$U.lo mpf/neg$U.lo		    \
  mpf/set_q$U.lo mpf/get_d$U.lo mpf/get_d_2exp$U.lo mpf/set_dfl_prec$U.lo   \
  mpf/set_prc$U.lo mpf/set_prc_raw$U.lo mpf/get_dfl_prec$U.lo               \
  mpf/get_prc$U.lo mpf/ui_div$U.lo mpf/sqrt_ui$U.lo                         \
  mpf/ceilfloor$U.lo mpf/trunc$U.lo mpf/pow_ui$U.lo			    \
  mpf/urandomb$U.lo mpf/swap$U.lo					    \
  mpf/fits_sint$U.lo mpf/fits_slong$U.lo mpf/fits_sshort$U.lo		    \
  mpf/fits_uint$U.lo mpf/fits_ulong$U.lo mpf/fits_ushort$U.lo		    \
  mpf/get_si$U.lo mpf/get_ui$U.lo					    \
  mpf/int_p$U.lo

MPZ_OBJECTS = mpz/abs$U.lo mpz/add$U.lo mpz/add_ui$U.lo			\
  mpz/aorsmul$U.lo mpz/aorsmul_i$U.lo mpz/and$U.lo mpz/array_init$U.lo	\
  mpz/bin_ui$U.lo mpz/bin_uiui$U.lo					\
  mpz/cdiv_q$U.lo mpz/cdiv_q_ui$U.lo					\
  mpz/cdiv_qr$U.lo mpz/cdiv_qr_ui$U.lo					\
  mpz/cdiv_r$U.lo mpz/cdiv_r_ui$U.lo mpz/cdiv_ui$U.lo			\
  mpz/cfdiv_q_2exp$U.lo mpz/cfdiv_r_2exp$U.lo				\
  mpz/clear$U.lo mpz/clears$U.lo mpz/clrbit$U.lo			\
  mpz/cmp$U.lo mpz/cmp_d$U.lo mpz/cmp_si$U.lo mpz/cmp_ui$U.lo		\
  mpz/cmpabs$U.lo mpz/cmpabs_d$U.lo mpz/cmpabs_ui$U.lo			\
  mpz/com$U.lo mpz/combit$U.lo						\
  mpz/cong$U.lo mpz/cong_2exp$U.lo mpz/cong_ui$U.lo			\
  mpz/divexact$U.lo mpz/divegcd$U.lo mpz/dive_ui$U.lo			\
  mpz/divis$U.lo mpz/divis_ui$U.lo mpz/divis_2exp$U.lo mpz/dump$U.lo	\
  mpz/export$U.lo mpz/fac_ui$U.lo mpz/fdiv_q$U.lo			\
  mpz/fdiv_q_ui$U.lo mpz/fdiv_qr$U.lo mpz/fdiv_qr_ui$U.lo		\
  mpz/fdiv_r$U.lo mpz/fdiv_r_ui$U.lo					\
  mpz/fdiv_ui$U.lo mpz/fib_ui$U.lo mpz/fib2_ui$U.lo mpz/fits_sint$U.lo	\
  mpz/fits_slong$U.lo mpz/fits_sshort$U.lo mpz/fits_uint$U.lo		\
  mpz/fits_ulong$U.lo mpz/fits_ushort$U.lo mpz/gcd$U.lo			\
  mpz/gcd_ui$U.lo mpz/gcdext$U.lo mpz/get_d$U.lo mpz/get_d_2exp$U.lo	\
  mpz/get_si$U.lo mpz/get_str$U.lo mpz/get_ui$U.lo mpz/getlimbn$U.lo	\
  mpz/hamdist$U.lo							\
  mpz/import$U.lo mpz/init$U.lo mpz/init2$U.lo mpz/inits$U.lo 		\
  mpz/inp_raw$U.lo mpz/inp_str$U.lo mpz/invert$U.lo			\
  mpz/ior$U.lo mpz/iset$U.lo mpz/iset_d$U.lo mpz/iset_si$U.lo		\
  mpz/iset_str$U.lo mpz/iset_ui$U.lo mpz/jacobi$U.lo mpz/kronsz$U.lo	\
  mpz/kronuz$U.lo mpz/kronzs$U.lo mpz/kronzu$U.lo			\
  mpz/lcm$U.lo mpz/lcm_ui$U.lo mpz/lucnum_ui$U.lo mpz/lucnum2_ui$U.lo	\
  mpz/millerrabin$U.lo mpz/mod$U.lo mpz/mul$U.lo mpz/mul_2exp$U.lo	\
  mpz/mul_si$U.lo mpz/mul_ui$U.lo					\
  mpz/n_pow_ui$U.lo mpz/neg$U.lo mpz/nextprime$U.lo			\
  mpz/out_raw$U.lo mpz/out_str$U.lo mpz/perfpow$U.lo mpz/perfsqr$U.lo	\
  mpz/popcount$U.lo mpz/pow_ui$U.lo mpz/powm$U.lo mpz/powm_sec$U.lo	\
  mpz/powm_ui$U.lo mpz/pprime_p$U.lo mpz/random$U.lo mpz/random2$U.lo	\
  mpz/realloc$U.lo mpz/realloc2$U.lo mpz/remove$U.lo			\
  mpz/root$U.lo mpz/rootrem$U.lo mpz/rrandomb$U.lo mpz/scan0$U.lo	\
  mpz/scan1$U.lo mpz/set$U.lo mpz/set_d$U.lo mpz/set_f$U.lo		\
  mpz/set_q$U.lo mpz/set_si$U.lo mpz/set_str$U.lo mpz/set_ui$U.lo	\
  mpz/setbit$U.lo							\
  mpz/size$U.lo mpz/sizeinbase$U.lo mpz/sqrt$U.lo			\
  mpz/sqrtrem$U.lo mpz/sub$U.lo mpz/sub_ui$U.lo mpz/swap$U.lo		\
  mpz/tdiv_ui$U.lo mpz/tdiv_q$U.lo mpz/tdiv_q_2exp$U.lo			\
  mpz/tdiv_q_ui$U.lo mpz/tdiv_qr$U.lo mpz/tdiv_qr_ui$U.lo		\
  mpz/tdiv_r$U.lo mpz/tdiv_r_2exp$U.lo mpz/tdiv_r_ui$U.lo		\
  mpz/tstbit$U.lo mpz/ui_pow_ui$U.lo mpz/ui_sub$U.lo mpz/urandomb$U.lo	\
  mpz/urandomm$U.lo mpz/xor$U.lo

MPQ_OBJECTS = mpq/abs$U.lo mpq/aors$U.lo				\
  mpq/canonicalize$U.lo mpq/clear$U.lo					\
  mpq/cmp$U.lo mpq/cmp_si$U.lo mpq/cmp_ui$U.lo mpq/div$U.lo		\
  mpq/get_d$U.lo mpq/get_den$U.lo mpq/get_num$U.lo mpq/get_str$U.lo	\
  mpq/init$U.lo mpq/inp_str$U.lo mpq/inv$U.lo				\
  mpq/md_2exp$U.lo mpq/mul$U.lo mpq/neg$U.lo mpq/out_str$U.lo		\
  mpq/set$U.lo mpq/set_den$U.lo mpq/set_num$U.lo			\
  mpq/set_si$U.lo mpq/set_str$U.lo mpq/set_ui$U.lo			\
  mpq/equal$U.lo mpq/set_z$U.lo mpq/set_d$U.lo				\
  mpq/set_f$U.lo mpq/swap$U.lo

MPN_OBJECTS = mpn/fib_table$U.lo mpn/mp_bases$U.lo

PRINTF_OBJECTS =							\
  printf/asprintf$U.lo printf/asprntffuns$U.lo				\
  printf/doprnt$U.lo printf/doprntf$U.lo printf/doprnti$U.lo		\
  printf/fprintf$U.lo							\
  printf/obprintf$U.lo printf/obvprintf$U.lo printf/obprntffuns$U.lo	\
  printf/printf$U.lo printf/printffuns$U.lo				\
  printf/snprintf$U.lo printf/snprntffuns$U.lo				\
  printf/sprintf$U.lo printf/sprintffuns$U.lo				\
  printf/vasprintf$U.lo printf/vfprintf$U.lo printf/vprintf$U.lo	\
  printf/vsnprintf$U.lo printf/vsprintf$U.lo				\
  printf/repl-vsnprintf$U.lo

SCANF_OBJECTS =							\
  scanf/doscan$U.lo scanf/fscanf$U.lo scanf/fscanffuns$U.lo	\
  scanf/scanf$U.lo scanf/sscanf$U.lo scanf/sscanffuns$U.lo	\
  scanf/vfscanf$U.lo scanf/vscanf$U.lo scanf/vsscanf$U.lo

libgmp_la_SOURCES = gmp-impl.h longlong.h randmt.h			\
  assert.c compat.c errno.c extract-dbl.c invalid.c memory.c		\
  mp_bpl.c mp_clz_tab.c mp_dv_tab.c mp_minv_tab.c mp_get_fns.c mp_set_fns.c \
  rand.c randclr.c randdef.c randiset.c randlc2s.c randlc2x.c randmt.c	\
  randmts.c rands.c randsd.c randsdui.c randbui.c randmui.c version.c	\
  nextprime.c

# From configure.in
gmp_mpn_functions_optional=umul udiv					\
  invert_limb sqr_diagonal						\
  mul_2 mul_3 mul_4							\
  addmul_2 addmul_3 addmul_4 addmul_5 addmul_6 addmul_7 addmul_8	\
  addlsh1_n sublsh1_n rsblsh1_n rsh1add_n rsh1sub_n			\
  addlsh2_n sublsh2_n rsblsh2_n						\
  addlsh_n sublsh_n rsblsh_n						\
  add_n_sub_n addaddmul_1msb0

gmp_mpn_functions=${extra_functions}					   \
  add add_1 add_n sub sub_1 sub_n neg com mul_1 addmul_1		   \
  submul_1 lshift rshift dive_1 diveby3 divis divrem divrem_1 divrem_2     \
  fib2_ui mod_1 mod_34lsub1 mode1o pre_divrem_1 pre_mod_1 dump		   \
  mod_1_1 mod_1_2 mod_1_3 mod_1_4 lshiftc				   \
  mul mul_fft mul_n sqr mul_basecase sqr_basecase nussbaumer_mul	   \
  random random2 pow_1							   \
  rootrem sqrtrem get_str set_str scan0 scan1 popcount hamdist cmp	   \
  perfsqr perfpow							   \
  gcd_1 gcd gcdext_1 gcdext gcd_lehmer gcd_subdiv_step			   \
  gcdext_lehmer gcdext_subdiv_step					   \
  div_q tdiv_qr jacbase get_d						   \
  matrix22_mul hgcd2 hgcd mullo_n mullo_basecase			   \
  toom22_mul toom32_mul toom42_mul toom52_mul toom62_mul		   \
  toom33_mul toom43_mul toom53_mul toom63_mul				   \
  toom44_mul								   \
  toom6h_mul toom6_sqr toom8h_mul toom8_sqr				   \
  toom_couple_handling							   \
  toom2_sqr toom3_sqr toom4_sqr						   \
  toom_eval_dgr3_pm1 toom_eval_dgr3_pm2 				   \
  toom_eval_pm1 toom_eval_pm2 toom_eval_pm2exp toom_eval_pm2rexp	   \
  toom_interpolate_5pts toom_interpolate_6pts toom_interpolate_7pts	   \
  toom_interpolate_8pts toom_interpolate_12pts toom_interpolate_16pts	   \
  invertappr invert binvert mulmod_bnm1 sqrmod_bnm1			   \
  sbpi1_div_q sbpi1_div_qr sbpi1_divappr_q				   \
  dcpi1_div_q dcpi1_div_qr dcpi1_divappr_q				   \
  mu_div_qr mu_divappr_q mu_div_q					   \
  bdiv_q_1								   \
  sbpi1_bdiv_q sbpi1_bdiv_qr						   \
  dcpi1_bdiv_q dcpi1_bdiv_qr						   \
  mu_bdiv_q mu_bdiv_qr							   \
  bdiv_q bdiv_qr							   \
  divexact bdiv_dbm1c redc_1 redc_2 redc_n powm powlo powm_sec subcnd_n	   \
  redc_1_sec trialdiv remove						   \
  and_n andn_n nand_n ior_n iorn_n nior_n xor_n xnor_n			   \
  copyi copyd zero							   \
  ${gmp_mpn_functions_optional}

# end verbatim copy

MULFUNC_add_n=	aors_n
MULFUNC_sub_n=	aors_n
MULFUNC_addmul_1=	aorsmul_1
MULFUNC_submul_1=	aorsmul_1
MULFUNC_popcount=	popham
MULFUNC_hamdist=	popham
MULFUNC_and_n=	logops_n
MULFUNC_andn_n=	logops_n
MULFUNC_nand_n=	logops_n
MULFUNC_ior_n=	logops_n
MULFUNC_iorn_n=	logops_n
MULFUNC_nior_n=	logops_n
MULFUNC_xor_n=	logops_n
MULFUNC_xnor_n=	logops_n
MULFUNC_lshift=	lorrshift
MULFUNC_rshift=	lorrshift
MULFUNC_addlsh1_n=	aorslsh1_n
MULFUNC_sublsh1_n=	aorslsh1_n
MULFUNC_rsh1add_n=	rsh1aors_n
MULFUNC_rsh1sub_n=	rsh1aors_n


U=
extra_functions=
LO_OBJECTS=	${MPF_OBJECTS} ${MPZ_OBJECTS} ${MPQ_OBJECTS} ${MPN_OBJECTS} \
		${PRINTF_OBJECTS} ${SCANF_OBJECTS}

.for func in ${gmp_mpn_functions}
.for mapfunc in ${func} ${MULFUNC_${func}}
.for mapsource in mpn/generic/${mapfunc}.c
.for source in mpn/generic/${func}.c
.if exists(${mapsource}) && empty(MPNSRC:M${source})

.if !exists(${source})
${source}: ${mapsource} mkdirs
	cp ${.ALLSRC:Nmkdirs} ${.TARGET}
CLEANFILES+=	${source}
.ORDER: mkdirs ${source}
.endif

MPNSRC+=	${source}
${source:.c=.o}: ${source}
	${CC} -o ${.TARGET} ${CFLAGS} -DOPERATION_${func} -c ${.ALLSRC}
.endif
.endfor
.endfor
.endfor
.endfor

SRCS=	${LO_OBJECTS:.lo=.c} ${libgmp_la_SOURCES} ${MPNSRC}
SRCS+=	tal-reent.c

# the following are generated sources, so we have to add them
# explicitly to SRCS
GENSRC=	fac_ui.h fib_table.h mpn/fib_table.c mp_bases.h perfsqr.h
GENSRC+= trialdivtab.h
SRCS+=	${GENSRC}
CLEANFILES+=	${GENSRC}


fac_ui.h: gen-fac_ui.nx
	${.OBJDIR}/gen-fac_ui.nx ${LONG_INT_BITS} 0 > ${.TARGET}

fib_table.h: gen-fib.nx
	${.OBJDIR}/gen-fib.nx header ${LONG_INT_BITS} 0 > ${.TARGET}

mpn/fib_table.c: gen-fib.nx
	${.OBJDIR}/gen-fib.nx table ${LONG_INT_BITS} 0 > ${.TARGET}

mp_bases.h: gen-bases.nx
	${.OBJDIR}/gen-bases.nx header ${LONG_INT_BITS} 0 > ${.TARGET}

mpn/mp_bases.c: gen-bases.nx
	${.OBJDIR}/gen-bases.nx table ${LONG_INT_BITS} 0 > ${.TARGET}

perfsqr.h: gen-psqr.nx
	${.OBJDIR}/gen-psqr.nx ${LONG_INT_BITS} 0 > ${.TARGET}

trialdivtab.h: gen-trialdivtab.nx
	${.OBJDIR}/gen-trialdivtab.nx $(LONG_INT_BITS) 8000 > ${.TARGET}

.for _gen in fac_ui fib bases psqr trialdivtab
gen-${_gen}.nx: gen-${_gen}.c dumbmp.c
	${NXCC} ${.ALLSRC:M*/gen-*} -o ${.TARGET} -lm
CLEANFILES+=	gen-${_gen}.nx
.endfor

GENDIRS=	mpf mpz mpq mpn mpn/generic printf scanf
CLEANDIRS+=	${GENDIRS}
beforedepend: mkdirs
mkdirs:
	mkdir -p ${GENDIRS}

.include <bsd.lib.mk>

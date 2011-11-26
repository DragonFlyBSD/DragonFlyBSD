CONTRIBDIR=	${.CURDIR}/../../../../contrib/mpfr/src
.PATH:	${CONTRIBDIR}

LIB=	mpfr
INTERNALLIB=	# yes

CFLAGS+=	-I${CONTRIBDIR}
CFLAGS+=	-I${.OBJDIR}
CFLAGS+=	-I${.CURDIR}/../gmp
CFLAGS+=-DHAVE_INTTYPES_H=1 -DHAVE_STDINT_H=1 -DTIME_WITH_SYS_TIME=1 \
	-DHAVE_LOCALE_H=1 -DHAVE_WCHAR_H=1 -DHAVE_STDARG=1 \
	-DHAVE_SYS_TIME_H=1 -DHAVE_STDINT_H=1 -DHAVE_SETLOCALE=1 \
	-DHAVE_GETTIMEOFDAY=1 -DHAVE_LONG_LONG=1 -DHAVE_INTMAX_T=1 \
	-DMPFR_HAVE_FESETROUND=1 -DHAVE_DENORMS=1 -DHAVE_ROUND=1 \
	-DHAVE_TRUNC=1 -DHAVE_FLOOR=1 -DHAVE_CEIL=1 \
	-DHAVE_LDOUBLE_IEEE_EXT_LITTLE=1 -DHAVE_ATTRIBUTE_MODE=1
CFLAGS+=-DHAVE_VA_COPY=1 -DMPFR_HAVE_INTMAX_MAX=1 -DHAVE_NEARBYINT=1 \
	-DMPFR_USE_THREAD_SAFE=1 -DHAVE___GMPN_ROOTREM=1 \
	-DHAVE___GMPN_SBPI1_DIVAPPR_Q=1

# almost verbatim copy from Makefile

SRCS = mpfr.h mpf2mpfr.h mpfr-gmp.h mpfr-impl.h mpfr-intmax.h \
	mpfr-longlong.h mpfr-thread.h exceptions.c extract.c uceil_exp2.c \
	uceil_log2.c ufloor_log2.c add.c add1.c add_ui.c agm.c clear.c cmp.c \
	cmp_abs.c cmp_si.c cmp_ui.c comparisons.c div_2exp.c div_2si.c	\
	div_2ui.c div.c div_ui.c dump.c eq.c exp10.c exp2.c exp3.c exp.c \
	frac.c frexp.c get_d.c get_exp.c get_str.c init.c inp_str.c \
	isinteger.c isinf.c isnan.c isnum.c const_log2.c log.c modf.c \
	mul_2exp.c mul_2si.c mul_2ui.c mul.c mul_ui.c neg.c next.c out_str.c \
	printf.c vasprintf.c const_pi.c pow.c pow_si.c pow_ui.c print_raw.c \
	print_rnd_mode.c reldiff.c round_prec.c set.c setmax.c setmin.c \
	set_d.c set_dfl_prec.c set_exp.c set_rnd.c set_f.c set_prc_raw.c \
	set_prec.c set_q.c set_si.c set_str.c set_str_raw.c set_ui.c set_z.c \
	sqrt.c sqrt_ui.c sub.c sub1.c sub_ui.c rint.c ui_div.c ui_sub.c \
	urandom.c urandomb.c get_z_exp.c swap.c factorial.c cosh.c sinh.c \
	tanh.c sinh_cosh.c acosh.c asinh.c atanh.c atan.c cmp2.c exp_2.c \
	asin.c const_euler.c cos.c sin.c tan.c fma.c fms.c hypot.c log1p.c \
	expm1.c log2.c log10.c ui_pow.c	ui_pow_ui.c minmax.c dim.c signbit.c \
	copysign.c setsign.c gmp_op.c init2.c acos.c sin_cos.c set_nan.c \
	set_inf.c set_zero.c powerof2.c	gamma.c set_ld.c get_ld.c cbrt.c \
	volatile.c fits_s.h fits_sshort.c fits_sint.c fits_slong.c fits_u.h \
	fits_ushort.c fits_uint.c fits_ulong.c fits_uintmax.c fits_intmax.c \
	get_si.c get_ui.c zeta.c cmp_d.c erf.c inits.c inits2.c clears.c \
	sgn.c check.c sub1sp.c version.c mpn_exp.c mpfr-gmp.c mp_clz_tab.c \
	sum.c add1sp.c free_cache.c si_op.c cmp_ld.c set_ui_2exp.c \
	set_si_2exp.c set_uj.c set_sj.c get_sj.c get_uj.c get_z.c iszero.c \
	cache.c sqr.c int_ceil_log2.c isqrt.c strtofr.c pow_z.c logging.c \
	mulders.c get_f.c round_p.c erfc.c atan2.c subnormal.c \
	const_catalan.c root.c gen_inverse.h sec.c csc.c cot.c eint.c sech.c \
	csch.c coth.c round_near_x.c constant.c abort_prec_max.c \
	stack_interface.c lngamma.c zeta_ui.c set_d64.c get_d64.c jn.c yn.c \
	rem1.c get_patches.c add_d.c sub_d.c d_sub.c mul_d.c div_d.c d_div.c \
	li2.c rec_sqrt.c min_prec.c buildopt.c digamma.c bernoulli.c \
	isregular.c set_flt.c get_flt.c	scale2.c set_z_exp.c ai.c \
	gammaonethird.c ieee_floats.h grandom.c

.include <bsd.lib.mk>

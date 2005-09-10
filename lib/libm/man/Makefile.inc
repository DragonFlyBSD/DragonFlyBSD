# $DragonFly: src/lib/libm/man/Makefile.inc,v 1.4 2005/09/10 11:36:11 swildner Exp $

.PATH:	${.CURDIR}/man

MAN+=	acos.3 acosh.3 asin.3 asinh.3 atan.3 atan2.3 atanh.3 ceil.3 \
	cos.3 cosh.3 erf.3 exp.3 fabs.3 floor.3 fmod.3 hypot.3 ieee.3 \
	ieee_test.3 j0.3 lgamma.3 math.3 rint.3 round.3 sin.3 \
	sinh.3 sqrt.3 tan.3 tanh.3
MLINKS+=	\
	acos.3 acosf.3 acosh.3 acoshf.3 asin.3 asinf.3 asinh.3 asinhf.3 \
	atan.3 atanf.3 atan2.3 atan2f.3 atanh.3 atanhf.3 ceil.3 ceilf.3 \
	cos.3 cosf.3 cosh.3 coshf.3 erf.3 erff.3 erf.3 erfc.3 erf.3 erfcf.3 \
	exp.3 expf.3 exp.3 expm1.3 exp.3 expm1f.3 exp.3 log.3 exp.3 logf.3 \
	exp.3 log10.3 exp.3 log10f.3 exp.3 log1p.3 exp.3 log1pf.3 \
	exp.3 pow.3 exp.3 powf.3 fabs.3 fabsf.3 floor.3 floorf.3 \
	fmod.3 fmodf.3 hypot.3 hypotf.3 hypot.3 cabs.3 hypot.3 cabsf.3 \
	ieee.3 copysign.3 ieee.3 copysignf.3 ieee.3 finite.3 ieee.3 finitef.3 \
	ieee.3 ilogb.3 ieee.3 ilogbf.3 ieee.3 nextafter.3 ieee.3 nextafterf.3 \
	ieee.3 remainder.3 ieee.3 remainderf.3 ieee.3 scalbn.3 \
	ieee.3 scalbnf.3 ieee_test.3 logb.3 ieee_test.3 logbf.3 \
	ieee_test.3 scalb.3 ieee_test.3 scalbf.3 ieee_test.3 significand.3 \
	ieee_test.3 significandf.3 j0.3 j0f.3 j0.3 j1.3 j0.3 j1f.3 j0.3 jn.3 \
	j0.3 jnf.3 j0.3 y0.3 j0.3 y0f.3 j0.3 y1.3 j0.3 y1f.3 j0.3 \
	yn.3 j0.3 ynf.3 lgamma.3 lgammaf.3 lgamma.3 lgamma_r.3 \
	lgamma.3 lgammaf_r.3 lgamma.3 gamma.3 lgamma.3 gammaf.3 \
	lgamma.3 gamma_r.3 lgamma.3 gammaf_r.3 rint.3 rintf.3 \
	round.3 roundf.3 sin.3 sinf.3 sinh.3 sinhf.3 sqrt.3 cbrt.3 \
	sqrt.3 cbrtf.3 sqrt.3 sqrtf.3 tan.3 tanf.3 tanh.3 tanhf.3

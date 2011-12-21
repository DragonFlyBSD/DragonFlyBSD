.PATH:	${.CURDIR}/man

MAN+=	acos.3 acosh.3 asin.3 asinh.3 atan.3 atan2.3 atanh.3 ceil.3 \
	cos.3 cosh.3 erf.3 exp.3 fabs.3 fdim.3 feclearexcept.3 feenableexcept.3 \
	fegetenv.3 fegetround.3 fenv.3 floor.3 fmax.3 fmod.3 hypot.3 \
	ieee_test.3 j0.3 lgamma.3 math.3 rint.3 round.3 sin.3 \
	sinh.3 sqrt.3 tan.3 tanh.3 trunc.3
MAN+=	copysign.3 frexp.3 ilogb.3 log.3 scalbn.3 nextafter.3 remainder.3 \
	finite.3
MAN+=	fma.3 lrint.3 lround.3

MLINKS+=acos.3 acosf.3 acos.3 acosl.3
MLINKS+=acosh.3 acoshf.3
MLINKS+=asin.3 asinf.3 asin.3 asinl.3
MLINKS+=asinh.3 asinhf.3
MLINKS+=atan.3 atanf.3 atan.3 atanl.3
MLINKS+=atan2.3 atan2f.3 atan2.3 atan2l.3
MLINKS+=atanh.3 atanhf.3
MLINKS+=ceil.3 ceilf.3 ceil.3 ceill.3
MLINKS+=cos.3 cosf.3 cos.3 cosl.3
MLINKS+=cosh.3 coshf.3
MLINKS+=erf.3 erff.3 erf.3 erfc.3 erf.3 erfcf.3
MLINKS+=log.3 logf.3 log.3 log10.3 log.3 log10f.3 log.3 log1p.3 log.3 log1pf.3 \
	log.3 log2.3 log.3 log2f.3 log.3 logl.3
MLINKS+=exp.3 expf.3 exp.3 exp2.3 exp.3 exp2f.3 exp.3 expm1.3 exp.3 expmlf.3
MLINKS+=exp.3 powf.3 exp.3 pow.3 exp.3 exp2l.3

MLINKS+=fabs.3 fabsf.3 fabs.3 fabsl.3
MLINKS+=fdim.3 fdimf.3 fdim.3 fdiml.3
MLINKS+=feclearexcept.3 fegetexceptflag.3 feclearexcept.3 feraiseexcept.3 \
	feclearexcept.3 fesetexceptflag.3 feclearexcept.3 fetestexcept.3
MLINKS+=feenableexcept.3 fedisableexcept.3 feenableexcept.3 fegetexcept.3
MLINKS+=fegetenv.3 feholdexcept.3 fegetenv.3 fesetenv.3 fegetenv.3 feupdateenv.3
MLINKS+=fegetround.3 fesetround.3
MLINKS+=floor.3 floorf.3 floor.3 floorl.3
MLINKS+=fmax.3 fmaxf.3 fmax.3 fmaxl.3 \
	fmax.3 fmin.3 fmax.3 fminf.3 fmax.3 fminl.3
MLINKS+=fmod.3 fmodf.3 fmod.3 fmodl.3
MLINKS+=hypot.3 hypotf.3 hypot.3 hypotl.3
MLINKS+=hypot.3 cabs.3 hypot.3 cabsf.3 hypot.3 cabsl.3
MLINKS+=ieee_test.3 logb.3 ieee_test.3 logbf.3 ieee_test.3 scalb.3 \
	ieee_test.3 scalbf.3 ieee_test.3 significand.3 \
	ieee_test.3 significandf.3
MLINKS+=j0.3 j0f.3 j0.3 j1.3 j0.3 j1f.3 j0.3 jn.3 j0.3 jnf.3 j0.3 y0.3 \
	j0.3 y0f.3 j0.3 y1.3 j0.3 y1f.3 j0.3 yn.3 j0.3 ynf.3
MLINKS+=lgamma.3 lgammaf.3 lgamma.3 lgamma_r.3 lgamma.3 lgammaf_r.3 \
	lgamma.3 gamma.3 lgamma.3 gammaf.3 lgamma.3 gamma_r.3 \
	lgamma.3 gammaf_r.3 lgamma.3 tgamma.3 lgamma.3 tgammaf.3
MLINKS+=rint.3 nearbyint.3 rint.3 nearbyintf.3 rint.3 nearbyintl.3
MLINKS+=rint.3 rintf.3 rint.3 rintl.3
MLINKS+=round.3 roundf.3 round.3 roundl.3
MLINKS+=sin.3 sinf.3 sin.3 sinl.3
MLINKS+=sinh.3 sinhf.3
MLINKS+=sqrt.3 cbrtf.3 sqrt.3 cbrtl.3 sqrt.3 cbrt.3
MLINKS+=sqrt.3 sqrtf.3 sqrt.3 sqrtl.3
MLINKS+=tan.3 tanf.3 tan.3 tanl.3
MLINKS+=tanh.3 tanhf.3
MLINKS+=trunc.3 truncf.3
MLINKS+=copysign.3 copysignf.3 copysign.3 copysignl.3
MLINKS+=scalbn.3 scalbnf.3 scalbn.3 scalbnl.3
MLINKS+=scalbn.3 scalbln.3 scalbn.3 scalblnf.3 scalbn.3 scalblnl.3
MLINKS+=scalbn.3 ldexp.3 scalbn.3 ldexpf.3 scalbn.3 ldexpl.3
MLINKS+=pow.3 powf.3
MLINKS+=frexp.3 frexpf.3 frexp.3 frexpl.3
MLINKS+=ilogb.3 logb.3 ilogb.3 logbf.3 logb.3 logbl.3
MLINKS+=ilogb.3 ilogbf.3 ilogb.3 ilogbl.3
MLINKS+=nextafter.3 nextafterf.3 nextafter.3 nextafterl.3
MLINKS+=nextafter.3 nexttoward.3 nextafter.3 nexttowardf.3
MLINKS+=remainder.3 remainderf.3 remainder.3 remainderl.3
MLINKS+=remainder.3 remquo.3 remainder.3 remquof.3 remainder.3 remquol.3
MLINKS+=finite.3 finitef.3
MLINKS+=fma.3 fmaf.3 fma.3 fmal.3
MLINKS+=lrint.3 lintf.3 lrint.3 lrintl.3
MLINKS+=lrint.3 llrint.3 lrint.3 llrintf.3 lrint.3 llrintl.3
MLINKS+=lround.3 lroundf.3 lround.3 lroundl.3
MLINKS+=lround.3 llround.3 lround.3 llroundf.3 lround.3 llroundl.3

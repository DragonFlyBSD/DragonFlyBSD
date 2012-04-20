#include <math.h>
#include <machine/endian.h>

/* bytes for quiet NaN (IEEE single precision) */
const union __float_u __nanf =
		{ {    0,    0, 0xc0, 0x7f } };

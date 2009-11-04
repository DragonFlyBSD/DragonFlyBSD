# Set default CPU compile flags for custom compilers. Not much to see here.

. if ${MACHINE_ARCH} == "i386"
MACHINE_CPU = i486
. elif ${MACHINE_ARCH} == "x86_64"
MACHINE_CPU = x86_64 sse2 sse
. endif

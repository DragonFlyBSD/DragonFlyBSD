NOMAN=
NOSHARED=
WARNS?=		6

# Define RESCUE for make and compiler
CRUNCH_BUILDOPTS+= -DRESCUE
CRUNCH_CFLAGS+= -DRESCUE

.if defined(DESTDIR) && ${DESTDIR} != "" && ${DESTDIR} != "/"
CRUNCH_CFLAGS+= --sysroot=${DESTDIR}
CRUNCHENV+= PATH=${DESTDIR}/sbin:${DESTDIR}/bin:${DESTDIR}/usr/sbin:${DESTDIR}/usr/bin
_SHLIBDIRPREFIX= ${DESTDIR}
.endif

# Apply the same flags to link the crunched binary because crunchgen(1) does
# not recognize ${CRUNCH_CFLAGS}.
CRUNCH_LINKOPTS+= ${CRUNCH_CFLAGS}

# Use soft links to create the crunched program links
#CRUNCH_USE_SYMLINKS=

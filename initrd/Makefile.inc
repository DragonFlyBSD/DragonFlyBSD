NOMAN=
NOSHARED=
WARNS?=		6

# Define RESCUE for make and compiler
CRUNCH_BUILDOPTS+= -DRESCUE
CRUNCH_CFLAGS+= -DRESCUE

# Crunchgen needs to use the right libraries.  Normally if DESTDIR is
# specified it should also use the target binaries, however if run
# from buildworld DESTDIR points to the obj world_x86_64 which is not
# entirely populated.  In that situation we leave the path alone, it
# will point to the cross tools.
#
.if defined(DESTDIR) && ${DESTDIR} != "" && ${DESTDIR} != "/"
.if !defined(WORLDBUILD)
CRUNCH_CFLAGS+= --sysroot=${DESTDIR}
CRUNCHENV+= PATH=${DESTDIR}/sbin:${DESTDIR}/bin:${DESTDIR}/usr/sbin:${DESTDIR}/usr/bin
.endif
_SHLIBDIRPREFIX= ${DESTDIR}
.endif

# Apply the same flags to link the crunched binary because crunchgen(1) does
# not recognize ${CRUNCH_CFLAGS}.
CRUNCH_LINKOPTS+= ${CRUNCH_CFLAGS}

# Use soft links to create the crunched program links
#CRUNCH_USE_SYMLINKS=

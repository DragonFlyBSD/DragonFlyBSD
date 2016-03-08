.if ${MACHINE_ARCH} == "x86_64"
.MAKEFLAGS:  MACHINE_ARCH=i386 MACHINE_PLATFORM=pc32 REALLY_X86_64=true
.else
.MAKEFLAGS:  REALLY_X86_64=false
.endif

# must come before boot2
SUBDIR=	btx

SUBDIR+=boot0 boot0sio boot2 cdboot libi386 loader loader_tftp mbr

# special boot programs, 'self-extracting boot2+loader'
SUBDIR+=pxeldr pxeldr_tftp

.include <bsd.subdir.mk>

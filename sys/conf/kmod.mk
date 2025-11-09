#	From: @(#)bsd.prog.mk	5.26 (Berkeley) 6/25/91
# $FreeBSD: src/sys/conf/kmod.mk,v 1.82.2.15 2003/02/10 13:11:50 nyan Exp $
#
# The include file <bsd.kmod.mk> handles installing Kernel Loadable Device
# drivers (KLD's).
#
#
# +++ variables +++
# CLEANFILES	Additional files to remove for the clean and cleandir targets.
#
# KMOD          The name of the kernel module to build.
#
# KMODDIR	Base path for kernel modules (see kld(4)).
#		[${DESTKERNDIR}]
#
# KMODOWN	KLD owner. [${BINOWN}]
#
# KMODGRP	KLD group. [${BINGRP}]
#
# KMODMODE	KLD mode. [${BINMODE}]
#
# KMODLOAD	Command to load a kernel module [/sbin/kldload]
#
# KMODUNLOAD	Command to unload a kernel module [/sbin/kldunload]
#
# PROG          The name of the kernel module to build.
#		If not supplied, ${KMOD}.o is used.
#
# SRCS          List of source files
#
# DESTKERNDIR	Change the tree where the kernel and the modules get
#		installed. [/boot]  ${DESTDIR} changes the root of the tree
#		pointed to by ${DESTKERNDIR}.
#
# MFILES	Optionally a list of interfaces used by the module.
#		This file contains a default list of interfaces.
#
# FIRMWS	List of firmware images in format filename:shortname:version
#
# FIRMWARE_LICENSE
#		Set to the name of the license the user has to agree on in
#		order to use this firmware. See /usr/share/doc/legal
#
# KERNBUILDDIR	Set to the location of the kernel build directory where
#		the opt_*.h files, .o's and kernel wind up.
#
# +++ targets +++
#
# 	install:
#               install the kernel module and its manual pages; if the Makefile
#               does not itself define the target install, the targets
#               beforeinstall and afterinstall may also be used to cause
#               actions immediately before and after the install target
#		is executed.
#
# 	load:
#		Load KLD.
#
# 	unload:
#		Unload KLD.
#
# bsd.obj.mk: clean, cleandir and obj
# bsd.dep.mk: cleandepend, depend and tags
#

OBJCOPY?=	objcopy
KMODLOAD?=	/sbin/kldload
KMODUNLOAD?=	/sbin/kldunload

# KERNEL is needed when running make install directly from
# the obj directory.
KERNEL?=	kernel

KMODDIR?=	${DESTKERNDIR}
KMODOWN?=	${BINOWN}
KMODGRP?=	${BINGRP}
KMODMODE?=	${BINMODE}

.include <bsd.init.mk>

.SUFFIXES: .out .o .c .cc .cxx .C .y .l .s .S

.if !defined(NO_WERROR) && (${CCVER} == "gcc47" || ${CCVER} == "gcc80")
WERROR=-Werror
.endif

COPTFLAGS?=-O2 -pipe

# always use external as(1)
.if ${CCVER:Mclang*}
COPTFLAGS+=	-no-integrated-as
.endif

# useful for debugging
#.warning "KMOD-PREFILTER ${CFLAGS}"

WORLD_CCOPTLEVEL=	# XXX prevent world opt level affecting kernel modules
CFLAGS=		${COPTFLAGS} ${KCFLAGS} ${COPTS} -D_KERNEL
CFLAGS+=	${CWARNFLAGS} -std=${CSTD} ${WERROR}
CFLAGS+=	-DKLD_MODULE

# Don't use any standard include directories.
# Since -nostdinc will annull any previous -I paths, we repeat all
# such paths after -nostdinc.  It doesn't seem to be possible to
# add to the front of `make' variable.
#
# Don't use -I- anymore, source-relative includes are desireable.
_ICFLAGS:=	${CFLAGS:M-I*}
CFLAGS+=	-nostdinc ${_ICFLAGS}

# Add -I paths for system headers.  Individual KLD makefiles don't
# need any -I paths for this.  Similar defaults for .PATH can't be
# set because there are no standard paths for non-headers.
#
# NOTE!  Traditional platform paths such as <platform/pc64/blah.h>
# must run through the "machine_base" softlink using
# <machine_base/blah.h>.  An explicit cross-platform path must
# operate relative to /usr/src/sys using e.g. <platform/pc64/isa/blah.h>
#
CFLAGS+=	-I.
.if defined(FREEBSD_COMPAT)
CFLAGS+=	-Idragonfly/freebsd_compat
CFLAGS+=	-DFREEBSD_COMPAT=1
.endif
CFLAGS+=	-Idragonfly

# Add -I paths for headers in the kernel build directory
#
.if defined(KERNBUILDDIR)
CFLAGS+=	-I${KERNBUILDDIR}
_MACHINE_FWD=	${KERNBUILDDIR}
.else
.if defined(MAKEOBJDIRPREFIX)
_MACHINE_FWD=	${MAKEOBJDIRPREFIX}/${SYSDIR}/forwarder_${MACHINE_ARCH}
.else
_MACHINE_FWD=	${.OBJDIR}/forwarder_${MACHINE_ARCH}
CLEANDIRS+=	${_MACHINE_FWD}
.endif
.endif
CFLAGS+=	-I${_MACHINE_FWD}/include

.include "kern.fwd.mk"

# Add a -I path to standard headers like <stddef.h>.  Use a relative
# path to src/include if possible.  If the dragonfly symlink hasn't been
# built yet, then we can't tell if the relative path exists.  Add both the
# potential relative path and an absolute path in that case.
.if exists(dragonfly)
.if exists(dragonfly/../include)
CFLAGS+=	-Idragonfly/../include
.else
CFLAGS+=	-I${DESTDIR}/usr/include
.endif
.else
CFLAGS+=	-Idragonfly/../include -I${DESTDIR}/usr/include
.endif

.if defined(KERNBUILDDIR) && \
    exists(${KERNBUILDDIR}/opt_global.h)
CFLAGS+=	-DHAVE_KERNEL_OPTION_HEADERS -include ${KERNBUILDDIR}/opt_global.h
.endif

CFLAGS+=	${DEBUG_FLAGS}
.if ${MACHINE_ARCH} == "x86_64"
CFLAGS+=	-fno-omit-frame-pointer
.endif

.if defined(FIRMWS)
#AWK=/usr/bin/awk
.if !exists(dragonfly)
${KMOD:S/$/.c/}: dragonfly
.else
${KMOD:S/$/.c/}: dragonfly/tools/fw_stub.awk
.endif
	${AWK} -f dragonfly/tools/fw_stub.awk ${FIRMWS} -m ${KMOD} -c ${KMOD:S/$/.c/g} \
	    ${FIRMWARE_LICENSE:C/.+/-l/}${FIRMWARE_LICENSE}

SRCS+=	${KMOD:S/$/.c/}
CLEANFILES+=	${KMOD:S/$/.c/}

.for _firmw in ${FIRMWS}
${_firmw:C/\:.*$/.fwo/}:	${_firmw:C/\:.*$//}
	@${ECHO} ${_firmw:C/\:.*$//} ${.ALLSRC:M*${_firmw:C/\:.*$//}}
	@if [ -e ${_firmw:C/\:.*$//} ]; then			\
		${LD} -b binary --no-warn-mismatch ${LDFLAGS}	\
		    -r -d -o ${.TARGET}	${_firmw:C/\:.*$//};	\
	else							\
		ln -s ${.ALLSRC:M*${_firmw:C/\:.*$//}} ${_firmw:C/\:.*$//}; \
		${LD} -b binary --no-warn-mismatch ${LDFLAGS}	\
		    -r -d -o ${.TARGET}	${_firmw:C/\:.*$//};	\
		rm ${_firmw:C/\:.*$//};				\
	fi

OBJS+=	${_firmw:C/\:.*$/.fwo/}
.endfor
.endif

OBJS+=  ${SRCS:N*.h:R:S/$/.o/g}

.if !defined(PROG)
PROG=	${KMOD}.ko
.endif

# In case of LTO provide all standard CFLAGS!
.if ${CFLAGS:M-flto}
ELDFLAGS+= ${CFLAGS}
.endif

.if ${MACHINE_ARCH} != x86_64
${PROG}: ${KMOD}.kld
	${CC} ${ELDFLAGS} -nostdlib -Wl,--hash-style=sysv \
	-Wl,-Bshareable ${LDFLAGS} -o ${.TARGET} ${KMOD}.kld
.endif

.if ${MACHINE_ARCH} != x86_64
${KMOD}.kld: ${OBJS}
	${CC} ${ELDFLAGS} -nostdlib -Wl,--hash-style=sysv \
	${LDFLAGS} -r -o ${.TARGET} ${OBJS}
.else
${PROG}: ${OBJS}
	${CC} ${ELDFLAGS} -nostdlib -Wl,--hash-style=sysv \
	${LDFLAGS} -r -Wl,-d -o ${.TARGET} ${OBJS}
.endif

# links to platform and cpu architecture include files.  If we are
# building with a kernel most of these already exist in the kernel build
# dir.
#
# 'dragonfly' is a link to system sources.
#
# Note that 'dragonfly', 'machine_base', and 'cpu_base' primarily exist
# when source files need to reference sources in the dragonfly codebase or
# when header files need to differentiate between compat headers and base
# system headers (e.g. when forwarding headers)
#
.if defined(KERNBUILDDIR)
_ILINKS=dragonfly
.else
_ILINKS=dragonfly machine_base machine cpu_base cpu
.endif

.if defined(ARCH)
_ILINKS+=${ARCH}
.endif

all: objwarn fwheaders ${PROG}

beforedepend: fwheaders
fwheaders: ${_ILINKS} ${FORWARD_HEADERS_COOKIE}
# Ensure that the links exist without depending on it when it exists which
# causes all the modules to be rebuilt when the directory pointed to changes.
.for _link in ${_ILINKS}
.if !exists(${.OBJDIR}/${_link})
${OBJS}: ${_link}
.endif
.endfor

# Search for kernel source tree in standard places.
.for _dir in ${.CURDIR}/../.. ${.CURDIR}/../../.. ${.CURDIR}/../../../.. /sys /usr/src/sys
.if !defined(SYSDIR) && exists(${_dir}/kern/)
SYSDIR=	${_dir}
.endif
.endfor
.if !defined(SYSDIR) || !exists(${SYSDIR}/kern)
.error "can't find kernel source tree"
.endif
S=	${SYSDIR}

#	path=`(cd $$path && /bin/pwd)` ;

${_ILINKS}:
	@case ${.TARGET} in \
	machine) \
		path=${SYSDIR}/platform/${MACHINE_PLATFORM}/include ;; \
	machine_base) \
		path=${SYSDIR}/platform/${MACHINE_PLATFORM} ;; \
	cpu) \
		path=${SYSDIR}/cpu/${MACHINE_ARCH}/include ;; \
	cpu_base) \
		path=${SYSDIR}/cpu/${MACHINE_ARCH} ;; \
	dragonfly) \
		path=${SYSDIR} ;; \
	arch_*) \
		path=${.CURDIR}/${MACHINE_ARCH} ;; \
	esac ; \
	${ECHO} ${.TARGET} "->" $$path ; \
	${LN} -s $$path ${.TARGET}

CLEANFILES+= ${PROG} ${KMOD}.kld ${OBJS} ${_ILINKS} symb.tmp tmp.o

.if !target(install)

_INSTALLFLAGS:=	${INSTALLFLAGS}
.for ie in ${INSTALLFLAGS_EDIT}
_INSTALLFLAGS:=	${_INSTALLFLAGS${ie}}
.endfor

.if !target(realinstall)
realinstall: _kmodinstall
.ORDER: beforeinstall _kmodinstall
_kmodinstall:
.if defined(INSTALLSTRIPPEDMODULES)
	${INSTALL} -o ${KMODOWN} -g ${KMODGRP} -m ${KMODMODE} \
	    ${_INSTALLFLAGS} ${PROG} ${DESTDIR}${KMODDIR}
	${OBJCOPY} --strip-debug ${DESTDIR}${KMODDIR}/${PROG}
.else
	${INSTALL} -o ${KMODOWN} -g ${KMODGRP} -m ${KMODMODE} \
	    ${_INSTALLFLAGS} ${PROG} ${DESTDIR}${KMODDIR}
.endif
.endif # !target(realinstall)

.include <bsd.links.mk>

.endif # !target(install)

.if !target(load)
load:	${PROG}
	${KMODLOAD} -v ./${KMOD}.ko
.endif

.if !target(unload)
unload:
	${KMODUNLOAD} -v ${KMOD}
.endif

.for _src in ${SRCS:Mopt_*.h} ${SRCS:Muse_*.h}
CLEANFILES+=	${_src}
.if !target(${_src})
.if defined(KERNBUILDDIR) && exists(${KERNBUILDDIR}/${_src})
${_src}: ${KERNBUILDDIR}/${_src}
# we do not have to copy these files any more, the kernel build
# directory is included in the path now.
#	cp ${KERNBUILDDIR}/${_src} ${.TARGET}
.else
${_src}:
	touch ${.TARGET}
.endif	# KERNBUILDDIR
.endif
.endfor

MFILES?= kern/bus_if.m kern/device_if.m bus/iicbus/iicbb_if.m \
    bus/iicbus/iicbus_if.m bus/isa/isa_if.m dev/netif/mii_layer/miibus_if.m \
    bus/pccard/card_if.m bus/pccard/power_if.m bus/pci/pci_if.m \
    bus/pci/pcib_if.m \
    bus/ppbus/ppbus_if.m bus/smbus/smbus_if.m bus/u4b/usb_if.m \
    dev/acpica/acpi_if.m dev/acpica/acpi_wmi_if.m dev/disk/nata/ata_if.m \
    dev/disk/sdhci/sdhci_if.m \
    dev/sound/pci/hda/hdac_if.m \
    dev/sound/pcm/ac97_if.m dev/sound/pcm/channel_if.m \
    dev/sound/pcm/feeder_if.m dev/sound/pcm/mixer_if.m \
    dev/sound/midi/mpu_if.m dev/sound/midi/mpufoi_if.m \
    dev/sound/midi/synth_if.m  \
    libiconv/iconv_converter_if.m dev/agp/agp_if.m \
    bus/mmc/mmcbus_if.m bus/mmc/mmcbr_if.m \
    dev/virtual/virtio/virtio/virtio_bus_if.m \
    dev/misc/backlight/backlight_if.m dev/misc/coremctl/coremctl_if.m kern/cpu_if.m \
    bus/gpio/gpio_if.m \
    freebsd/net/ifdi_if.m

.for _srcsrc in ${MFILES}
.for _ext in c h
.for _src in ${SRCS:M${_srcsrc:T:R}.${_ext}}
CLEANFILES+=	${_src}
.if !target(${_src})
${_src}: dragonfly
.if exists(dragonfly)
${_src}: dragonfly/tools/makeobjops.awk dragonfly/${_srcsrc}
.endif

.if defined(KERNBUILDDIR) && \
    exists(${KERNBUILDDIR}/${_src})
.else
	awk -f dragonfly/tools/makeobjops.awk -- -${_ext} dragonfly/${_srcsrc}
.endif
.endif
.endfor # _src
.endfor # _ext
.endfor # _srcsrc

.if !empty(SRCS:Mmiidevs.h)
CLEANFILES+=	miidevs.h
.if !exists(dragonfly)
miidevs.h: dragonfly
.else
miidevs.h: dragonfly/tools/miidevs2h.awk dragonfly/dev/netif/mii_layer/miidevs
.endif
	${AWK} -f dragonfly/tools/miidevs2h.awk dragonfly/dev/netif/mii_layer/miidevs
.endif

.if !empty(SRCS:Mpccarddevs.h)
CLEANFILES+=	pccarddevs.h
.if !exists(dragonfly)
pccarddevs.h: dragonfly
.else
pccarddevs.h: dragonfly/tools/pccarddevs2h.awk dragonfly/bus/pccard/pccarddevs
.endif
	${AWK} -f dragonfly/tools/pccarddevs2h.awk dragonfly/bus/pccard/pccarddevs
.endif

.if !empty(SRCS:Mpcidevs.h)
CLEANFILES+=	pcidevs.h
.if !exists(dragonfly)
pcidevs.h: dragonfly
.else
pcidevs.h: dragonfly/tools/pcidevs2h.awk dragonfly/bus/pci/pcidevs
.endif
	${AWK} -f dragonfly/tools/pcidevs2h.awk dragonfly/bus/pci/pcidevs
.endif

.if !empty(SRCS:Musbdevs.h)
CLEANFILES+=	usbdevs.h
.if !exists(dragonfly)
usbdevs.h: dragonfly
.else
usbdevs.h: dragonfly/tools/usbdevs2h.awk dragonfly/bus/u4b/usbdevs
.endif
	${AWK} -f dragonfly/tools/usbdevs2h.awk dragonfly/bus/u4b/usbdevs -h
.endif

.if !empty(SRCS:Musbdevs_data.h)
CLEANFILES+=	usbdevs_data.h
.if !exists(dragonfly)
usbdevs_data.h: dragonfly
.else
usbdevs_data.h: dragonfly/tools/usbdevs2h.awk dragonfly/bus/u4b/usbdevs
.endif
	${AWK} -f dragonfly/tools/usbdevs2h.awk dragonfly/bus/u4b/usbdevs -d
.endif

.if !empty(SRCS:Macpi_quirks.h)
CLEANFILES+=	acpi_quirks.h
.if !exists(dragonfly)
acpi_quirks.h: dragonfly
.else
acpi_quirks.h: dragonfly/tools/acpi_quirks2h.awk dragonfly/dev/acpica/acpi_quirks
.endif
	${AWK} -f dragonfly/tools/acpi_quirks2h.awk dragonfly/dev/acpica/acpi_quirks
.endif

.if !empty(SRCS:Massym.s)
CLEANFILES+=	assym.s genassym.o
assym.s: genassym.o
.if defined(KERNBUILDDIR)
genassym.o: opt_global.h
.endif
.if !exists(dragonfly)
assym.s: dragonfly
.else
assym.s: dragonfly/kern/genassym.sh
.endif
	sh dragonfly/kern/genassym.sh genassym.o > ${.TARGET}
.if exists(dragonfly)
genassym.o: dragonfly/platform/${MACHINE_PLATFORM}/${MACHINE_ARCH}/genassym.c
.endif
genassym.o: dragonfly ${SRCS:Mopt_*.h}
	${CC} -c ${CFLAGS:N-fno-common:N-flto:N-mcmodel=small} -fcommon \
	${WERROR} dragonfly/platform/${MACHINE_PLATFORM}/${MACHINE_ARCH}/genassym.c
.endif

regress:

.include <bsd.dep.mk>

.if !exists(${DEPENDFILE})
${OBJS}: ${SRCS:M*.h}
.endif

.include <bsd.obj.mk>
.include "bsd.kern.mk"

# Behaves like MODULE_OVERRIDE
.if defined(KLD_DEPS)
all: _kdeps_all
_kdeps_all: dragonfly
.for _mdep in ${KLD_DEPS}
	cd ${SYSDIR}/${_mdep} && make all
.endfor
depend: _kdeps_depend
_kdeps_depend: dragonfly
.for _mdep in ${KLD_DEPS}
	cd ${SYSDIR}/${_mdep} && make depend
.endfor
install: _kdeps_install
_kdeps_install: dragonfly
.for _mdep in ${KLD_DEPS}
	cd ${SYSDIR}/${_mdep} && make install
.endfor
clean: _kdeps_clean
_kdeps_clean: dragonfly
.for _mdep in ${KLD_DEPS}
	cd ${SYSDIR}/${_mdep} && make clean
.endfor
.endif

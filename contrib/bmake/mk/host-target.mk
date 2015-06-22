# RCSid:
#	$Id: host-target.mk,v 1.7 2014/05/16 17:54:52 sjg Exp $

# Host platform information; may be overridden
.if !defined(_HOST_OSNAME)
_HOST_OSNAME !=	uname -s
.export _HOST_OSNAME
.endif
.if !defined(_HOST_OSREL)
_HOST_OSREL  !=	uname -r
.export _HOST_OSREL
.endif
.if !defined(_HOST_ARCH)
_HOST_ARCH   !=	uname -p 2>/dev/null || uname -m
# uname -p may produce garbage on linux
.if ${_HOST_ARCH:[\#]} > 1
_HOST_ARCH != uname -m
.endif
.export _HOST_ARCH
.endif
.if !defined(HOST_MACHINE)
HOST_MACHINE != uname -m
.export HOST_MACHINE
.endif

HOST_OSMAJOR := ${_HOST_OSREL:C/[^0-9].*//}
HOST_OSTYPE  :=	${_HOST_OSNAME}-${_HOST_OSREL:C/\([^\)]*\)//}-${_HOST_ARCH}
HOST_OS      :=	${_HOST_OSNAME}
host_os      :=	${_HOST_OSNAME:tl}
HOST_TARGET  := ${host_os}${HOST_OSMAJOR}-${_HOST_ARCH}

# tr is insanely non-portable, accommodate the lowest common denominator
TR ?= tr
toLower = ${TR} 'ABCDEFGHIJKLMNOPQRSTUVWXYZ' 'abcdefghijklmnopqrstuvwxyz'
toUpper = ${TR} 'abcdefghijklmnopqrstuvwxyz' 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'

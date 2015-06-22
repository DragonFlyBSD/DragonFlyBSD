# $FreeBSD: src/share/mk/bsd.own.mk,v 1.27.2.4 2002/07/22 14:21:51 ru Exp $
#
# The include file <bsd.own.mk> set common variables for owner,
# group, mode, and directories. Defaults are in brackets.
#
#
# +++ variables +++
#
# DESTDIR	Change the tree where the file gets installed. [not set].
#		Typical usage is ${DESTDIR}/usr/bin/...  Note that this
#		variable is not used to determine where programs access
#		auxillary data, only where everything is installed.
#
# DISTDIR	Change the tree where the file for a distribution
# 		gets installed (see /usr/src/release/Makefile). [not set]
#
# TOOLS_PREFIX	Change the tree where the program will search for auxillary
#		program binaries.  Defaults to <empty>.  e.g. usage is in
#		the typical form ${TOOLS_PREFIX}/usr/libexec/...
#
#		This is primarily used when generating cross-building tools
#		where the cross-building tools must exec auxillary binaries
#		which are themselves cross-built tools.
#
#		This variable specifies how a program looks for data, it does
#		NOT specify where a program installs data.
#
# USRDATA_PREFIX
#		Change the tree where the program will search for auxillary
#		data files.  Defaults to ${TOOLS_PREFIX}
#
#		Note that while auxillary program binaries and auxillary
#		data files are usually installed in the same tree, there
#		are cases where they might not be.  For example, when
#		buildworld generates the cross compile tools it must install
#		auxillary binaries in the ctools obj hiearchy but those
#		binaries must access data from the world obj hierarchy.
#
#		This variable specifies how a program looks for data, it does
#		NOT specify where a program installs data.
#
# INCLUDEDIR
#		Change the tree where header files are to be installed.
#		Defaults to /usr/include.  Note that use of INCLUDEDIR
#		is typically prefixed by ${DESTDIR}.
#
# COMPRESS_CMD	Program to compress documents. 
#		Output is to stdout. [gzip -cn]
#
# COMPRESS_EXT	File name extension of ${COMPRESS_CMD} command. [.gz]
#
# STRIP		The flag passed to the install program to cause the binary
#		to be stripped.  This is to be used when building your
#		own install script so that the entire system can be made
#		stripped/not-stripped using a single knob. [-s]
#
# BINOWN	Binary owner. [root]
#
# BINGRP	Binary group. [wheel]
#
# BINMODE	Binary mode. [555]
#
# CCVER		Default compiler version
# GCCLIBDIR	Default gcc subdirectory [${LIBDIR}/${CCVER}]
# GCCSHLIBDIR	Default gcc subdirectory [${SHLIBDIR}/${CCVER}]
#
# NOBINMODE	Mode for non-executable files. [444]
#
# LIBDIR	Base path for libraries. [/usr/lib]
#
# LIBDATADIR	Base path for misc. utility data files. [/usr/libdata]
#
# LINTLIBDIR	Base path for lint libraries. [/usr/libdata/lint]
#
# SHLIBDIR	Base path for shared libraries. [${LIBDIR}]
#
# LIBOWN	Library mode. [${BINOWN}]
#
# LIBGRP	Library group. [${BINGRP}]
#
# LIBMODE	Library mode. [${NOBINMODE}]
#
#
# SHAREDIR	Base path for architecture-independent ascii
#		text files. [/usr/share]
#
# SHAREOWN	ASCII text file owner. [root]
#
# SHAREGRP	ASCII text file group. [wheel]
#
# SHAREMODE	ASCII text file mode. [${NOBINMODE}]
#
#
# DOCDIR	Base path for system documentation (e.g. PSD, USD,
#		handbook, FAQ etc.). [${SHAREDIR}/doc]
#
# DOCOWN	Documentation owner. [${SHAREOWN}]
#
# DOCGRP	Documentation group. [${SHAREGRP}]
#
# DOCMODE	Documentation mode. [${NOBINMODE}]
#
#
# INFODIR	Base path for GNU's hypertext system
#		called Info (see info(1)). [${SHAREDIR}/info]
#
# INFOOWN	Info owner. [${SHAREOWN}]
#
# INFOGRP	Info group. [${SHAREGRP}]
#
# INFOMODE	Info mode. [${NOBINMODE}]
#
#
# MANDIR	Base path for manual installation. [${SHAREDIR}/man/man]
#
# MANOWN	Manual owner. [${SHAREOWN}]
#
# MANGRP	Manual group. [${SHAREGRP}]
#
# MANMODE	Manual mode. [${NOBINMODE}]
#
#
# NLSDIR	Base path for National Language Support files
#		installation (see mklocale(1)). [${SHAREDIR}/nls]
#
# NLSGRP	National Language Support files group. [${SHAREOWN}]
#
# NLSOWN	National Language Support files owner. [${SHAREGRP}]
#
# NLSMODE	National Language Support files mode. [${NOBINMODE}]

.if !target(__<bsd.own.mk>__)
__<bsd.own.mk>__:

# Binaries
BINOWN?=	root
BINGRP?=	wheel
BINMODE?=	555
NOBINMODE?=	444

LIBDIR?=	/usr/lib
GCCLIBDIR?=	${LIBDIR}/${CCVER}
LIBDATADIR?=	/usr/libdata
LINTLIBDIR?=	/usr/libdata/lint
DEBUGLIBDIR?=	${LIBDIR}/debug
PROFLIBDIR?=	${LIBDIR}/profile
SHLIBDIR?=	${LIBDIR}
GCCSHLIBDIR?=	${SHLIBDIR}/${CCVER}
LIBOWN?=	${BINOWN}
LIBGRP?=	${BINGRP}
LIBMODE?=	${NOBINMODE}

TOOLS_PREFIX?=
USRDATA_PREFIX?= ${TOOLS_PREFIX}
INCLUDEDIR?=	/usr/include

# Share files
SHAREDIR?=	/usr/share
SHAREOWN?=	root
SHAREGRP?=	wheel
SHAREMODE?=	${NOBINMODE}

MANDIR?=	${SHAREDIR}/man/man
MANOWN?=	${SHAREOWN}
MANGRP?=	${SHAREGRP}
MANMODE?=	${NOBINMODE}

DOCDIR?=	${SHAREDIR}/doc
DOCOWN?=	${SHAREOWN}
DOCGRP?=	${SHAREGRP}
DOCMODE?=	${NOBINMODE}

INFODIR?=	${SHAREDIR}/info
INFOOWN?=	${SHAREOWN}
INFOGRP?=	${SHAREGRP}
INFOMODE?=	${NOBINMODE}

NLSDIR?=	${SHAREDIR}/nls
NLSGRP?=	${SHAREGRP}
NLSOWN?=	${SHAREOWN}
NLSMODE?=	${NOBINMODE}

LOCALEDIR?=	${SHAREDIR}/locale
LOCALEGRP?=	${SHAREGRP}
LOCALEOWN?=	${SHAREOWN}
LOCALEMODE?=	${NOBINMODE}

# Common variables
.if !defined(DEBUG_FLAGS)
STRIP?=		-s
.endif

COMPRESS_CMD?=	gzip -cn
COMPRESS_EXT?=	.gz

.endif # !target(__<bsd.own.mk>__)

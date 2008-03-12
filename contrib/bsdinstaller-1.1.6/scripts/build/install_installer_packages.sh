#!/bin/sh -x

# $Id: install_installer_packages.sh,v 1.32 2005/04/05 10:37:57 den Exp $
# Install packages for the installer into the ISO-to-be, using
# DragonFly's src/nrelease/Makefile.  This assumes a release (or
# quickrel etc) has already been built; it simply (re)installs pkgs.
# Note that this generally requires root privledges.

SCRIPT=`realpath $0`
SCRIPTDIR=`dirname $SCRIPT`

[ -r $SCRIPTDIR/build.conf ] && . $SCRIPTDIR/build.conf
. $SCRIPTDIR/build.conf.defaults
. $SCRIPTDIR/pver.conf

PVERSUFFIX=""
if [ "X$RELEASEBUILD" != "XYES" ]; then
	PVERSUFFIX=.`date "+%Y.%m%d"`
fi

INSTALLER_PACKAGES="libaura-${LIBAURA_VER}${PVERSUFFIX}
		    libdfui-${LIBDFUI_VER}${PVERSUFFIX}
		    libinstaller-${LIBINSTALLER_VER}${PVERSUFFIX}
		    dfuibe_installer-${DFUIBE_INSTALLER_VER}${PVERSUFFIX}
		    dfuife_curses-${DFUIFE_CURSES_VER}${PVERSUFFIX}
		    dfuife_cgi-${DFUIFE_CGI_VER}${PVERSUFFIX}
		    thttpd-notimeout-${THTTPD_NOTIMEOUT_VER}"

# dfuife_qt is not installed by default, since it requires X11.
if [ "X$INSTALL_DFUIFE_QT" = "XYES" ]; then
	INSTALLER_PACKAGES="$INSTALLER_PACKAGES
			    dfuife_qt-${DFUIFE_QT_VER}${PVERSUFFIX}"
	WITH_X11="YES"
fi

# i18n is not installed by default, only because bsd-gettext needs work.
if [ "X$WITH_NLS" = "XYES" ]; then
	INSTALLER_PACKAGES="libiconv-${LIBICONV_VER}
			    expat-${EXPAT_VER}
			    gettext-${GETTEXT_VER}
			    $INSTALLER_PACKAGES"
fi

# dfuibe_lua is not installed by default, since it's not ready yet.
if [ "X$INSTALL_DFUIBE_LUA" = "XYES" ]; then
	INSTALLER_PACKAGES="$INSTALLER_PACKAGES
			    lua50-${LUA50_VER}
			    lua50-compat51-${LUA50_COMPAT51_VER}
			    lua50-posix-${LUA50_POSIX_VER}
			    lua50-pty-${LUA50_PTY_VER}${PVERSUFFIX}
			    lua50-filename-${LUA50_FILENAME_VER}${PVERSUFFIX}
			    lua50-app-${LUA50_APP_VER}${PVERSUFFIX}
			    lua50-gettext-${LUA50_GETTEXT_VER}${PVERSUFFIX}
			    lua50-dfui-${LUA50_DFUI_VER}${PVERSUFFIX}
			    lua50-socket-${LUA50_SOCKET_VER}
			    dfuibe_lua-${DFUIBE_LUA_VER}${PVERSUFFIX}"
fi

if [ "X$WITH_X11" = "XYES" ]; then
	INSTALLER_PACKAGES="pkgconfig-${PKGCONFIG_VER}
			    freetype2-${FREETYPE2_VER}
			    fontconfig-${FONTCONFIG_VER}
			    X.org-${X_ORG_VER}
			    png-${PNG_VER}
			    jpeg-${JPEG_VER}
			    lcms-${LCMS_VER}
			    libmng-${LIBMNG_VER}
			    qt-${QT_VER}
			    $INSTALLER_PACKAGES"
fi

CLEAN_PACKAGES=""
for PKG in $INSTALLER_PACKAGES; do
	ANYPKG=`echo "$PKG" | sed 's/\\-.*$/\\-\\*/'`
	CLEAN_PACKAGES="$CLEAN_PACKAGES '$ANYPKG'"
done

cd $SRCDIR/nrelease && \
make pkgcleaniso EXTRA_PACKAGES="$CLEAN_PACKAGES" && \
make pkgaddiso EXTRA_PACKAGES="$INSTALLER_PACKAGES" && \
rm -rf $TMPDIR/root_installer && \
cp -pR $ROOTSKEL $TMPDIR/root_installer && \
chown -R root:wheel $TMPDIR/root_installer && \
chmod -R 755 \
	$TMPDIR/root_installer/etc/rc.d \
	$TMPDIR/root_installer/usr/local/bin && \
make customizeiso EXTRA_ROOTSKELS="$TMPDIR/root_installer $EXTRA_ROOTSKELS" && \
rm -rf $TMPDIR/root_installer

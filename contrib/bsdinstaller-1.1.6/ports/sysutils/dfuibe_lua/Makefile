# New ports collection makefile for:	dfuibe_lua
# Date created:			Nov 27, 2004
# Whom:				Chris Pressey <cpressey@catseye.mine.nu>

PORTNAME=	dfuibe_lua
PORTVERSION=    0.4
INTERNAL=	YES
CATEGORIES=	sysutils
DISTNAME=	${PORTNAME}-${PORTVERSION}
WRKSRC=		work/lua

LIB_DEPENDS=	lua50.5:${PORTSDIR}/lang/lua50

RUN_DEPENDS=	${LOCALBASE}/lib/lua/5.0/lpty.so:${PORTSDIR}/devel/lua50-pty \
		${LOCALBASE}/lib/lua/5.0/ldfui.so:${PORTSDIR}/devel/lua50-dfui \
		${LOCALBASE}/share/lua/5.0/app.lua:${PORTSDIR}/devel/lua50-app
.if defined(WITH_NLS)
RUN_DEPENDS:=	${RUN_DEPENDS} \
		${LOCALBASE}/lib/lua/5.0/lgettext.so:${PORTSDIR}/devel/lua50-gettext
.endif

MAINTAINER=	cpressey@catseye.mine.nu
COMMENT=	DFUI Lua backend

DFUIBE_LUA_DIR=	${LOCALBASE}/share/dfuibe_lua

.if defined(WITH_NLS)
LOCALEDIR=	/usr/local/share/locale
LANGUAGES=	ru
.endif

PLIST_FILES=	share/dfuibe_lua/main.lua \
		share/dfuibe_lua/200_install.lua \
		share/dfuibe_lua/400_configure.lua \
		share/dfuibe_lua/600_upgrade.lua \
		share/dfuibe_lua/800_netboot.lua \
		share/dfuibe_lua/lib/bitwise.lua \
		share/dfuibe_lua/lib/cmdchain.lua \
		share/dfuibe_lua/lib/configvars.lua \
		share/dfuibe_lua/lib/fsm.lua \
		share/dfuibe_lua/lib/menu.lua \
		share/dfuibe_lua/lib/mountpoint.lua \
		share/dfuibe_lua/lib/network.lua \
		share/dfuibe_lua/lib/package.lua \
		share/dfuibe_lua/lib/storage.lua \
		share/dfuibe_lua/lib/storage_ui.lua \
		share/dfuibe_lua/lib/target_system.lua \
		share/dfuibe_lua/conf/cmdnames.lua \
		share/dfuibe_lua/conf/sources.lua \
		share/dfuibe_lua/conf/mountpoints.lua \
		share/dfuibe_lua/conf/def_pkgs.lua \
		share/dfuibe_lua/pit/main.lua \
		share/dfuibe_lua/pit/100_select_language.lua \
		share/dfuibe_lua/pit/800_configure_network.lua \
		share/dfuibe_lua/install/main.lua \
		share/dfuibe_lua/install/100_welcome.lua \
		share/dfuibe_lua/install/200_select_disk.lua \
		share/dfuibe_lua/install/250_partition_disk.lua \
		share/dfuibe_lua/install/300_select_part.lua \
		share/dfuibe_lua/install/400_select_subparts.lua \
		share/dfuibe_lua/install/450_select_packages.lua \
		share/dfuibe_lua/install/500_install_os.lua \
		share/dfuibe_lua/install/700_install_bootblocks.lua \
		share/dfuibe_lua/install/800_finished.lua \
		share/dfuibe_lua/install/900_reboot.lua \
		share/dfuibe_lua/configure/main.lua \
		share/dfuibe_lua/configure/100_choose_where_from.lua \
		share/dfuibe_lua/configure/200_select_disk.lua \
		share/dfuibe_lua/configure/300_select_part.lua \
		share/dfuibe_lua/configure/400_mount_target_system.lua \
		share/dfuibe_lua/configure/500_menu.lua \
		share/dfuibe_lua/configure/600_unmount_target_system.lua \
		share/dfuibe_lua/configure/menu/main.lua \
		share/dfuibe_lua/configure/menu/100_set_root_password.lua \
		share/dfuibe_lua/configure/menu/200_add_user.lua \
		share/dfuibe_lua/configure/menu/300_set_timezone.lua \
		share/dfuibe_lua/configure/menu/350_set_datetime.lua \
		share/dfuibe_lua/configure/menu/400_change_console_settings.lua \
		share/dfuibe_lua/configure/menu/500_install_packages.lua \
		share/dfuibe_lua/configure/menu/550_remove_packages.lua \
		share/dfuibe_lua/upgrade/main.lua \
		share/dfuibe_lua/upgrade/200_select_disk.lua \
		share/dfuibe_lua/upgrade/300_select_part.lua \
		share/dfuibe_lua/upgrade/400_mount_target_system.lua \
		share/dfuibe_lua/upgrade/500_perform_upgrade.lua \
		share/dfuibe_lua/upgrade/600_unmount_target_system.lua \
		share/dfuibe_lua/netboot/main.lua \
		share/dfuibe_lua/netboot/500_setup_server.lua

.if defined(WITH_NLS)
PLIST_FILES+=	share/locale/ru/LC_MESSAGES/dfuibe_lua.mo
.endif

do-fetch:
	@if [ ! -f ${_DISTDIR}${DISTNAME}${EXTRACT_SUFX} ]; then		\
		${ECHO_MSG};							\
		${ECHO_MSG} "Distfile ${DISTNAME}${EXTRACT_SUFX} not found!";	\
		${ECHO_MSG};							\
		${ECHO_MSG} "This is an 'INTERNAL' port, meaning that the distfile is not retrieved";	\
		${ECHO_MSG} "from the Internet, but rather created from a local checkout of the";	\
		${ECHO_MSG} "BSD Installer CVS repository.  Please see the shell scripts in the";	\
		${ECHO_MSG} "scripts/build directory of the repository for more information on how";	\
		${ECHO_MSG} "to create these distfiles.";			\
		false;								\
	fi

do-install:
	${MKDIR} -p ${DFUIBE_LUA_DIR}
	${MKDIR} -p ${DFUIBE_LUA_DIR}/lib
	${MKDIR} -p ${DFUIBE_LUA_DIR}/conf
	${MKDIR} -p ${DFUIBE_LUA_DIR}/pit
	${MKDIR} -p ${DFUIBE_LUA_DIR}/install
	${MKDIR} -p ${DFUIBE_LUA_DIR}/configure
	${MKDIR} -p ${DFUIBE_LUA_DIR}/configure/menu
	${MKDIR} -p ${DFUIBE_LUA_DIR}/upgrade
	${MKDIR} -p ${DFUIBE_LUA_DIR}/netboot
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/*.lua ${DFUIBE_LUA_DIR}/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/lib/*.lua ${DFUIBE_LUA_DIR}/lib/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/conf/*.lua ${DFUIBE_LUA_DIR}/conf/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/pit/*.lua ${DFUIBE_LUA_DIR}/pit/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/install/*.lua ${DFUIBE_LUA_DIR}/install/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/configure/*.lua ${DFUIBE_LUA_DIR}/configure/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/configure/menu/*.lua ${DFUIBE_LUA_DIR}/configure/menu/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/upgrade/*.lua ${DFUIBE_LUA_DIR}/upgrade/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/netboot/*.lua ${DFUIBE_LUA_DIR}/netboot/
.if defined(WITH_NLS)
	for _language in ${LANGUAGES}; do \
		(cd ${WRKSRC}/po && msgfmt -o $${_language}.mo $${_language}); \
		${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/po/$${_language}.mo \
		    ${LOCALEDIR}/$${_language}/LC_MESSAGES/${PORTNAME}.mo; \
	done
.endif

OSTYPE!=sysctl -n kern.ostype

.if ${OSTYPE} == "FreeBSD"
.include <bsd.port.mk>
.endif

.if ${OSTYPE} == "DragonFly"
.include <bsd.dfport.mk>
.endif

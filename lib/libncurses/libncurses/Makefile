LIB=    private_ncurses${LIB_SUFFIX}
PRIVATELIB=

SHLIBDIR?=	/lib

NCURSESBASEDIR=		${NCURSESDIR}/ncurses/base
NCURSESSERIALDIR=	${NCURSESDIR}/ncurses/tty
NCURSESTINFODIR=	${NCURSESDIR}/ncurses/tinfo

ZNCURSES=	yes
CFLAGS+=	-DTRACE
CFLAGS+=	-DDRAGONFLY_NATIVE

GENHDRS= \
	curses.h \
	hashsize.h \
	init_keytry.h \
	ncurses_def.h \
	parametrized.h \
	term.h

GENSRCS= \
	codes.c \
	comp_captab.c \
	expanded.c \
	fallback.c \
	lib_gen.c \
	lib_keyname.c \
	names.c \
	unctrl.c

HEADERS=	curses.h term.h termcap.h unctrl.h
SRCHDRS=	ncurses_dll.h

term.h: MKterm.h.awk edit_cfg.sh Caps
	awk -f ${.CURDIR}/../include/MKterm.h.awk ${TERMINFO_CAPS} > ${.TARGET}.new
	sh ${NCURSESDIR}/include/edit_cfg.sh ${.CURDIR}/../include/ncurses_cfg.h ${.TARGET}.new
	mv -f ${.TARGET}.new ${.TARGET}

hashsize.h: MKhashsize.sh Caps
	sh ${NCURSESDIR}/include/MKhashsize.sh ${TERMINFO_CAPS} > ${.TARGET}

parametrized.h: MKparametrized.sh Caps
	sh ${NCURSESDIR}/include/MKparametrized.sh ${TERMINFO_CAPS} > ${.TARGET}

init_keytry.h: make_keys keys.list
	./make_keys keys.list > ${.TARGET}

make_keys: make_keys.c names.c ncurses_def.h ${VERSION_MAP} ${HEADERS}
	${NXCC} ${NXCFLAGS} -static ${NCURSESTINFODIR}/make_keys.c ${NXLDLIBS} -o ${.TARGET}

keys.list: MKkeys_list.sh Caps
	sh ${NCURSESTINFODIR}/MKkeys_list.sh ${TERMINFO_CAPS} | LC_ALL=C sort > ${.TARGET}

names.c: MKnames.awk Caps
	awk -f ${NCURSESTINFODIR}/MKnames.awk bigstrings=1 ${TERMINFO_CAPS} > ${.TARGET}

codes.c: MKcodes.awk Caps
	awk -f ${NCURSESTINFODIR}/MKcodes.awk bigstrings=1 ${TERMINFO_CAPS} > ${.TARGET}

comp_captab.c: make_hash MKcaptab.sh MKcaptab.awk Caps
	sh -e ${NCURSESTINFODIR}/MKcaptab.sh awk 1 ${NCURSESTINFODIR}/MKcaptab.awk ${TERMINFO_CAPS} > ${.TARGET}

make_hash: make_hash.c hashsize.h ncurses_def.h ${HEADERS}
	${NXCC} ${NXCFLAGS} -static -DMAIN_PROGRAM ${NXLDLIBS} -o ${.TARGET} ${NCURSESTINFODIR}/make_hash.c

expanded.c: MKexpanded.sh ${GENHDRS}
	sh ${NCURSESSERIALDIR}/MKexpanded.sh "${CC}" -E ${CFLAGS} > ${.TARGET}

fallback.c: MKfallback.sh
	sh ${NCURSESTINFODIR}/MKfallback.sh /usr/share/terminfo ${NCURSESDIR}/misc/terminfo.src > ${.TARGET}

lib_gen.c: MKlib_gen.sh curses.h
	sh ${NCURSESBASEDIR}/MKlib_gen.sh "${CC} -E ${CFLAGS}" awk generated <curses.h > ${.TARGET}

lib_keyname.c: MKkeyname.awk keys.list
	awk -f ${NCURSESBASEDIR}/MKkeyname.awk bigstrings=1 keys.list > ${.TARGET}

unctrl.c: MKunctrl.awk
	echo | awk -f ${NCURSESBASEDIR}/MKunctrl.awk bigstrings=1 > ${.TARGET}

CLEANFILES=     ${GENSRCS} ${GENHDRS} term.h.new curses.h.new
CLEANFILES+=	make_keys make_hash keys.list

# Base
SRCS=	define_key.c \
	key_defined.c \
	keybound.c \
	keyok.c \
	legacy_coding.c \
	lib_addch.c \
	lib_addstr.c \
	lib_beep.c \
	lib_bkgd.c \
	lib_box.c \
	lib_chgat.c \
	lib_clear.c \
	lib_clearok.c \
	lib_clrbot.c \
	lib_clreol.c \
	lib_color.c \
	lib_colorset.c \
	lib_delch.c \
	lib_delwin.c \
	lib_dft_fgbg.c \
	lib_echo.c \
	lib_endwin.c \
	lib_erase.c \
	lib_flash.c \
	lib_freeall.c \
	lib_getch.c \
	lib_getstr.c \
	lib_hline.c \
	lib_immedok.c \
	lib_inchstr.c \
	lib_initscr.c \
	lib_insch.c \
	lib_insdel.c \
	lib_insnstr.c \
	lib_instr.c \
	lib_isendwin.c \
	lib_leaveok.c \
	lib_mouse.c \
	lib_move.c \
	lib_mvwin.c \
	lib_newterm.c \
	lib_newwin.c \
	lib_nl.c \
	lib_overlay.c \
	lib_pad.c \
	lib_printw.c \
	lib_redrawln.c \
	lib_refresh.c \
	lib_restart.c \
	lib_scanw.c \
	lib_screen.c \
	lib_scroll.c \
	lib_scrollok.c \
	lib_scrreg.c \
	lib_set_term.c \
	lib_slk.c \
	lib_slkatr_set.c \
	lib_slkatrof.c \
	lib_slkatron.c \
	lib_slkatrset.c \
	lib_slkattr.c \
	lib_slkclear.c \
	lib_slkcolor.c \
	lib_slkinit.c \
	lib_slklab.c \
	lib_slkrefr.c \
	lib_slkset.c \
	lib_slktouch.c \
	lib_touch.c \
	lib_ungetch.c \
	lib_vline.c \
	lib_wattroff.c \
	lib_wattron.c \
	lib_winch.c \
	lib_window.c \
	nc_panel.c \
	resizeterm.c \
	safe_sprintf.c \
	tries.c \
	use_window.c \
	version.c \
	vsscanf.c \
	wresize.c

# Serial
SRCS+=	hardscroll.c \
	hashmap.c \
	lib_mvcur.c \
	lib_tstp.c \
	lib_twait.c \
	lib_vidattr.c \
	tty_update.c

# Tinfo
SRCS+=	access.c \
	add_tries.c \
	alloc_entry.c \
	alloc_ttype.c \
	captoinfo.c \
	comp_error.c \
	comp_expand.c \
	comp_hash.c \
	comp_parse.c \
	comp_scan.c \
	db_iterator.c \
	doalloc.c \
	entries.c \
	free_ttype.c \
	getenv_num.c \
	hashed_db.c \
	home_terminfo.c \
	init_keytry.c \
	lib_acs.c \
	lib_baudrate.c \
	lib_cur_term.c \
	lib_data.c \
	lib_has_cap.c \
	lib_kernel.c \
	lib_longname.c \
	lib_napms.c \
	lib_options.c \
	lib_print.c \
	lib_raw.c \
	lib_setup.c \
	lib_termcap.c \
	lib_termname.c \
	lib_tgoto.c \
	lib_ti.c \
	lib_tparm.c \
	lib_tputs.c \
	lib_ttyflags.c \
	name_match.c \
	obsolete.c \
	parse_entry.c \
	read_entry.c \
	read_termcap.c \
	strings.c \
	trim_sgr0.c \
	use_screen.c \
	write_entry.c

# Trace
SRCS+=	lib_trace.c \
	lib_traceatr.c \
	lib_tracebits.c \
	lib_tracechr.c \
	lib_tracedmp.c \
	lib_tracemse.c \
	trace_buf.c \
	trace_tries.c \
	trace_xnames.c \
	varargs.c \
	visbuf.c \

# Generated
SRCS+=	${GENSRCS} ${GENHDRS}

NCSTATIC=	libprivate_ncurses${LIB_SUFFIX}.a
NCDYN=		libprivate_ncurses${LIB_SUFFIX}.so

.if defined(ENABLE_WIDEC)
INCS=		${HEADERS} ${SRCHDRS}
INCSDIR=	${INCLUDEDIR}/priv/ncurses
INCSLINKS=	curses.h ${INCLUDEDIR}/priv/ncurses/ncurses.h

SRCS+=	charable.c \
	lib_add_wch.c \
	lib_box_set.c \
	lib_cchar.c \
	lib_erasewchar.c \
	lib_get_wch.c \
	lib_get_wstr.c \
	lib_hline_set.c \
	lib_in_wch.c \
	lib_in_wchnstr.c \
	lib_ins_wch.c \
	lib_inwstr.c \
	lib_key_name.c \
	lib_pecho_wchar.c \
	lib_slk_wset.c \
	lib_unget_wch.c \
	lib_vid_attr.c \
	lib_vline_set.c \
	lib_wacs.c \
	lib_wunctrl.c
.endif

.include <bsd.lib.mk>

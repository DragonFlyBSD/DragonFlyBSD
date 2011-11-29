# $DragonFly: src/lib/libncurses/libncurses/Makefile,v 1.11 2005/12/06 02:22:23 corecode Exp $

LIB=    ncurses

.include "../Makefile.inc"

NCURSESBASEDIR=${NCURSESDIR}/ncurses/base
NCURSESSERIALDIR=${NCURSESDIR}/ncurses/tty
NCURSESTINFODIR=${NCURSESDIR}/ncurses/tinfo
NCURSESTRACEDIR=${NCURSESDIR}/ncurses/trace
NCURSESWCHARDIR=${NCURSESDIR}/ncurses/widechar
.PATH: ${NCURSESBASEDIR} ${NCURSESSERIALDIR} ${NCURSESTINFODIR} \
	${NCURSESTRACEDIR} ${NCURSESWCHARDIR} ${.OBJDIR} ${.CURDIR}/../include \
	${NCURSESDIR}/include/

CFLAGS+= 	-I${NCURSESDIR}/ncurses/
CFLAGS+=	-DTRACE

TERMINFO_CAPS=  ${NCURSESDIR}/include/Caps
ncurses_def.h:
	sh ${NCURSESDIR}/include/MKncurses_def.sh ${NCURSESDIR}/include/ncurses_defs > ${.TARGET}
curses.h: $(TERMINFO_CAPS) \
		curses.head \
		${NCURSESDIR}/include/curses.tail \
		${NCURSESDIR}/include//MKkey_defs.sh
	cat  ${.CURDIR}/../include/curses.head >$@
	AWK=awk sh ${NCURSESDIR}/include/MKkey_defs.sh $(TERMINFO_CAPS) >>$@
	sh -c 'if test "cchar_t" = "cchar_t" ; then cat ${NCURSESDIR}/include/curses.wide >>$@ ; fi'
	cat ${NCURSESDIR}/include/curses.tail >>$@

#curses.h: curses.head curses.tail
#	cat ${.CURDIR}/../include/curses.head >$@
#	sh ${NCURSESDIR}/include/MKkey_defs.sh $(TERMINFO_CAPS) >>$@
#	cat ${NCURSESDIR}/include/curses.wide >>$@
#	cat ${NCURSESDIR}/include/curses.tail >>$@
term.h: MKterm.h.awk ncurses_cfg.h
	awk -f ${.CURDIR}/../include/MKterm.h.awk ${TERMINFO_CAPS} > $@
	sh ${NCURSESDIR}/include/edit_cfg.sh ${.CURDIR}/../include/ncurses_cfg.h $@
hashsize.h:
	sh ${NCURSESDIR}/include/MKhashsize.sh ${TERMINFO_CAPS} >$@
parametrized.h:
	sh ${NCURSESDIR}/include/MKparametrized.sh ${TERMINFO_CAPS} >$@
init_keytry.h: make_keys keys.list
	./make_keys keys.list > $@

make_keys: ${NCURSESTINFODIR}/make_keys.c names.c ncurses_def.h
	${NXCC} ${NXCFLAGS} ${NXLDFLAGS} ${NCURSESTINFODIR}/make_keys.c ${NXLDLIBS} -o ${.TARGET}

keys.list:
	sh ${NCURSESTINFODIR}/MKkeys_list.sh ${TERMINFO_CAPS} | sort > $@

names.c: ${NCURSESTINFODIR}/MKnames.awk
	awk -f ${NCURSESTINFODIR}/MKnames.awk bigstrings=$(USE_BIG_STRINGS) ${TERMINFO_CAPS} >$@
	#cat namehdr boolnames boolfnames numnames numfnames strnames strfnames nameftr >names.c
	#cat namehdr boolcodes numcodes strcodes codeftr >codes.c
codes.c: ${NCURSESTINFODIR}/MKcodes.awk
	awk -f ${NCURSESTINFODIR}/MKcodes.awk bigstrings=$(USE_BIG_STRINGS) ${TERMINFO_CAPS} >$@
comp_captab.c: make_hash
	sh -e ${NCURSESTINFODIR}/MKcaptab.sh awk 0 ${NCURSESTINFODIR}/MKcaptab.awk ${TERMINFO_CAPS} > $@

make_hash: ${NCURSESTINFODIR}/comp_hash.c
	${NXCC} ${NXCFLAGS} ${NXLDFLAGS} -DMAIN_PROGRAM ${NXLDLIBS} -o ${.TARGET} ${NCURSESTINFODIR}/comp_hash.c

make_hash: hashsize.h ncurses_def.h curses.h

expanded.c:
	sh ${NCURSESSERIALDIR}/MKexpanded.sh "${CC}" -E ${CFLAGS} > $@
fallback.c:
	sh ${NCURSESTINFODIR}/MKfallback.sh /usr/share/terminfo ${NCURSESDIR}/misc/terminfo.src >$@
lib_gen.c: curses.h
	sh ${NCURSESBASEDIR}/MKlib_gen.sh "${CC} -E ${CFLAGS}" awk generated <curses.h >$@
lib_keyname.c: keys.list
	awk -f ${NCURSESBASEDIR}/MKkeyname.awk keys.list > $@
unctrl.c:
	echo | awk -f ${NCURSESBASEDIR}/MKunctrl.awk >$@

CLEANFILES=     curses.h ncurses_def.h term.h hashsize.h parametrized.h comp_captab.c
CLEANFILES+=    namehdr boolnames boolfnames numnames numfnames strnames strfnames nameftr
CLEANFILES+=    codes.c init_keytry.h numcodes strcodes boolcodes codeftr make_keys names.c
CLEANFILES+=    make_hash expanded.c fallback.c lib_gen.c lib_keyname.c unctrl.c keys.list

SRCS=	\
	curses.h \
	ncurses_def.h \
	term.h \
	hashsize.h \
	parametrized.h \
	init_keytry.h

# Base
SRCS+=	\
	define_key.c \
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
	lib_instr.c \
	lib_insnstr.c \
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
	memmove.c \
	nc_panel.c \
	resizeterm.c \
	safe_sprintf.c \
	tries.c \
	use_window.c \
	version.c \
	vsscanf.c \
	wresize.c

# Serial
SRCS+=	\
	hardscroll.c \
	hashmap.c \
	lib_mvcur.c \
	lib_tstp.c \
	lib_twait.c \
	lib_vidattr.c \
	tty_update.c

# Tinfo
SRCS+=	\
	access.c \
	add_tries.c \
	alloc_entry.c \
	alloc_ttype.c \
	captoinfo.c \
	comp_error.c \
	comp_expand.c \
	comp_hash.c \
	comp_parse.c \
	comp_scan.c \
	doalloc.c \
	db_iterator.c \
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
	parse_entry.c \
	read_entry.c \
	setbuf.c \
	strings.c \
	trim_sgr0.c \
	use_screen.c \
	write_entry.c

# Trace
SRCS+=	\
	lib_trace.c \
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

# Widechar
SRCS+=	\
	charable.c \
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

# Generated
SRCS+=	\
	codes.c \
	comp_captab.c \
	expanded.c \
	fallback.c \
	lib_gen.c \
	lib_keyname.c \
	names.c \
	unctrl.c

# Use instead of read_termcap.c.
# From our old libtermcap.
SRCS+=  termcap.c

SYMLINKS+=libncurses.a ${LIBDIR}/libcurses.a
SYMLINKS+=libncurses.a ${LIBDIR}/libtinfo.a
SYMLINKS+=libncurses.a ${LIBDIR}/libmytinfo.a
SYMLINKS+=libncurses.a ${LIBDIR}/libtermlib.a
SYMLINKS+=libncurses.a ${LIBDIR}/libtermcap.a
.if !defined(NOPIC)
# no need for major at all, it's an ld-time redirection only
SYMLINKS+=libncurses.so ${SHLIBDIR}/libcurses.so
SYMLINKS+=libncurses.so ${SHLIBDIR}/libtermcap.so
SYMLINKS+=libncurses.so ${SHLIBDIR}/libtermlib.so
SYMLINKS+=libncurses.so ${SHLIBDIR}/libmytinfo.so
SYMLINKS+=libncurses.so ${SHLIBDIR}/libtinfo.so
.endif
.if !defined(NOPROFILE)
SYMLINKS+=libncurses.a ${PROFLIBDIR}/libcurses.a
SYMLINKS+=libncurses.a ${PROFLIBDIR}/libtermcap.a
SYMLINKS+=libncurses.a ${PROFLIBDIR}/libtermlib.a
SYMLINKS+=libncurses.a ${PROFLIBDIR}/libmytinfo.a
SYMLINKS+=libncurses.a ${PROFLIBDIR}/libtinfo.aa
.endif

INCS=	term.h ${.CURDIR}/../include/termcap.h \
	${.CURDIR}/../include/unctrl.h curses.h \
	${NCURSESDIR}/include/ncurses_dll.h
INCSLINKS=curses.h ${INCLUDEDIR}/ncurses.h

# Make the generated files depend on the headers, otherwise
# the make depend step cannot run -j parallel due to not yet
# knowing what the actual dependencies are.
#
codes.c: ${INCS}
comp_captab.c: ${INCS}
expanded.c: ${INCS}
fallback.c: ${INCS}
lib_gen.c: ${INCS}
lib_keyname.c: ${INCS}
names.c: ${INCS}
unctrl.c: ${INCS}

.include <bsd.lib.mk>

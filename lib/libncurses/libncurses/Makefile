# $DragonFly: src/lib/libncurses/libncurses/Makefile,v 1.2 2005/03/12 20:57:07 joerg Exp $

LIB=    ncurses
SHLIB_MAJOR=5
SHLIB_MINOR=4

.include "../Makefile.inc"

NCURSESBASEDIR=${NCURSESDIR}/ncurses/base
NCURSESSERIALDIR=${NCURSESDIR}/ncurses/tty
NCURSESTINFODIR=${NCURSESDIR}/ncurses/tinfo
NCURSESTRACEDIR=${NCURSESDIR}/ncurses/trace
.PATH: ${NCURSESBASEDIR} ${NCURSESSERIALDIR} ${NCURSESTINFODIR} \
	${NCURSESTRACEDIR} ${.OBJDIR} ${NCURSESDIR}/include/

CFLAGS+= 	-I${NCURSESDIR}/ncurses/
CFLAGS+=	-DTRACE

TERMINFO_CAPS=  ${NCURSESDIR}/include/Caps
ncurses_def.h:
	${NCURSESDIR}/include/MKncurses_def.sh ${NCURSESDIR}/include/ncurses_defs > ${.TARGET}
curses.h:
	cat ${.CURDIR}/../include/curses.head >$@
	${NCURSESDIR}/include/MKkey_defs.sh $(TERMINFO_CAPS) >>$@
	cat ${NCURSESDIR}/include/curses.tail >>$@
term.h:
	awk -f ${.CURDIR}/../include/MKterm.h.awk ${TERMINFO_CAPS} > $@
	${NCURSESDIR}/include/edit_cfg.sh ${.CURDIR}/../include/ncurses_cfg.h $@
hashsize.h:
	${NCURSESDIR}/include/MKhashsize.sh ${TERMINFO_CAPS} >$@
parametrized.h:
	${NCURSESDIR}/include/MKparametrized.sh ${TERMINFO_CAPS} >$@
init_keytry.h: make_keys
	${.OBJDIR}/make_keys keys.list > $@
keys.list:
	${NCURSESTINFODIR}/MKkeys_list.sh ${TERMINFO_CAPS} | sort > $@
make_keys: names.c
	${CC} -o $@ ${NCURSESTINFODIR}/make_keys.c ${CFLAGS}
names.c codes.c:
	awk -f ${NCURSESTINFODIR}/MKnames.awk ${TERMINFO_CAPS}
	cat namehdr boolnames boolfnames numnames numfnames strnames strfnames nameftr >names.c
	cat namehdr boolcodes numcodes strcodes codeftr >codes.c
comp_captab.c: make_hash
	sh ${NCURSESTINFODIR}/MKcaptab.awk awk ${TERMINFO_CAPS} > $@
make_hash: hashsize.h ncurses_def.h curses.h
	${CC} -o $@ ${CFLAGS} -DMAIN_PROGRAM ${NCURSESTINFODIR}/comp_hash.c
expanded.c:
	sh ${NCURSESSERIALDIR}/MKexpanded.sh "${CC}" ${CFLAGS} > $@
fallback.c:
	sh ${NCURSESTINFODIR}/MKfallback.sh /usr/share/terminfo ${NCURSESDIR}/misc/terminfo.src >$@
lib_gen.c: curses.h
	sh ${NCURSESBASEDIR}/MKlib_gen.sh "${CC} ${CFLAGS}" awk generated <curses.h >$@
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
	free_ttype.c \
	getenv_num.c \
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
SYMLINKS+=libncurses_p.a ${LIBDIR}/libcurses_p.a
SYMLINKS+=libncurses_p.a ${LIBDIR}/libtermcap_p.a
SYMLINKS+=libncurses_p.a ${LIBDIR}/libtermlib_p.a
SYMLINKS+=libncurses_p.a ${LIBDIR}/libmytinfo_p.a
SYMLINKS+=libncurses_p.a ${LIBDIR}/libtinfo_p.a
.endif

INCS=	term.h ${.CURDIR}/../include/termcap.h \
	${.CURDIR}/../include/unctrl.h curses.h
INCSLINKS=curses.h ${INCLUDEDIR}/ncurses.h

.include <bsd.lib.mk>

#
# $FreeBSD: head/usr.bin/vi/Makefile 254225 2013-08-11 20:03:12Z peter $
#

SRCDIR=	${.CURDIR}/../../contrib/nvi2

PROG=	nvi

LINKS=	${BINDIR}/${PROG} ${BINDIR}/vi \
	${BINDIR}/${PROG} ${BINDIR}/nex \
	${BINDIR}/${PROG} ${BINDIR}/ex \
	${BINDIR}/${PROG} ${BINDIR}/nview \
	${BINDIR}/${PROG} ${BINDIR}/view

MAN=	${SRCDIR}/man/vi.1
MLINKS+=vi.1 ex.1 \
	vi.1 view.1 \
	vi.1 nex.1 \
	vi.1 nview.1 \
	vi.1 nvi.1

.PATH:	${SRCDIR}/common
.PATH:	${SRCDIR}/ex
.PATH:	${SRCDIR}/cl
.PATH:	${SRCDIR}/vi
.PATH:	${SRCDIR}/regex

CFLAGS+=	-I${.CURDIR} -I${SRCDIR} -I${SRCDIR}/regex

DPADD=		${LIBUTIL}
LDADD=		-lutil

CFLAGS+=	-D__REGEX_PRIVATE
CFLAGS+=	-DUSE_WIDECHAR \
		-I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
DPADD+=		${LIBNCURSESW}
LDADD+=		-lprivate_ncursesw
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.if !defined(RESCUE)
CFLAGS+=	-DUSE_ICONV
.endif

# We call the _FOREACH_SAFE() macros as _FOREACH_MUTABLE() in <sys/queue.h>.
CFLAGS+=	-DSLIST_FOREACH_SAFE=SLIST_FOREACH_MUTABLE \
		-DTAILQ_FOREACH_SAFE=TAILQ_FOREACH_MUTABLE

WARNS?=	1

# Vi curses sources
CL_SRCS=	cl_funcs.c cl_main.c cl_read.c cl_screen.c cl_term.c

# General sources.
COMMON_SRCS=	conv.c cut.c delete.c encoding.c exf.c key.c line.c log.c \
		main.c mark.c msg.c options.c options_f.c \
		put.c recover.c screen.c search.c seq.c util.c

# Ex source.
EX_SRCS=	ex.c ex_abbrev.c ex_append.c ex_args.c ex_argv.c ex_at.c \
		ex_bang.c ex_cd.c ex_cmd.c ex_cscope.c ex_delete.c \
		ex_display.c ex_edit.c ex_equal.c ex_file.c ex_filter.c \
		ex_global.c ex_init.c ex_join.c ex_map.c ex_mark.c \
		ex_mkexrc.c ex_move.c ex_open.c ex_preserve.c ex_print.c \
		ex_put.c ex_quit.c ex_read.c ex_screen.c ex_script.c \
		ex_set.c ex_shell.c ex_shift.c ex_source.c ex_stop.c \
		ex_subst.c ex_tag.c ex_txt.c ex_undo.c ex_usage.c ex_util.c \
		ex_version.c ex_visual.c ex_write.c ex_yank.c ex_z.c

# Vi source.
VI_SRCS=	getc.c v_at.c v_ch.c v_cmd.c v_delete.c v_ex.c \
		v_increment.c v_init.c v_itxt.c v_left.c v_mark.c v_match.c \
		v_paragraph.c v_put.c v_redraw.c v_replace.c v_right.c \
		v_screen.c v_scroll.c v_search.c v_section.c v_sentence.c \
		v_status.c v_txt.c v_ulcase.c v_undo.c v_util.c v_word.c \
		v_xchar.c v_yank.c v_z.c v_zexit.c vi.c vs_line.c vs_msg.c \
		vs_refresh.c vs_relative.c vs_smap.c vs_split.c

# Wide char regex
REGEX_SRCS=	regcomp.c regerror.c regexec.c regfree.c

SRCS=		${CL_SRCS} ${COMMON_SRCS} ${EX_SRCS} ${VI_SRCS} ${REGEX_SRCS}

# Generate extern headers.
EXTERNS=	cl_extern.h common_extern.h ex_extern.h vi_extern.h
SRCS+=		${EXTERNS}
CLEANFILES+=	${EXTERNS}

cl_extern.h: ${CL_SRCS}
common_extern.h: ${COMMON_SRCS}
ex_extern.h: ${EX_SRCS}
vi_extern.h: ${VI_SRCS}
${EXTERNS}:
	sed -E -n 's/^ \* PUBLIC: (.*)$$/\1/p' ${.ALLSRC} >> ${.TARGET}

# Generate "ex_def.h" and "options_def.h".
SRCS+=		ex_def.h options_def.h
CLEANFILES+=	ex_def.h options_def.h
ex_def.h: ex_cmd.c
	awk -f ${SRCDIR}/ex/ex.awk ${.ALLSRC} > ${.TARGET}
options_def.h: options.c
	awk -f ${SRCDIR}/common/options.awk ${.ALLSRC} > ${.TARGET}

# Generate "ex/version.h", which must be placed in the "ex/" subdirectory
# because "common/recover.c" requires "../ex/version.h".
SRCS+=		ex/version.h
ex/version.h: ${SRCDIR}/README
	mkdir -p ${.OBJDIR}/ex
	sed -E -n 's/^.*version ([^)]+\)).*/\#define VI_VERSION "\1"/p;q' \
		${.ALLSRC} > ${.TARGET}

CFLAGS+=	-I${.OBJDIR} -I${.OBJDIR}/ex
CLEANDIRS+=	ex

.if !defined(NO_SHARE)
SUBDIR+=vidump catalog
.endif

.include <bsd.prog.mk>

SRCDIR=	${.CURDIR}/../../contrib/libpcap

LIB=    pcap
SRCS=	pcap-bpf.c \
	fad-getad.c \
	pcap.c inet.c gencode.c optimize.c nametoaddr.c etherent.c \
	savefile.c sf-pcap.c sf-pcap-ng.c pcap-common.c \
	bpf_image.c bpf_dump.c \
	scanner.l grammar.y bpf_filter.c version.c \
	fad-helpers.c sockutils.c \
	pcap_version.h tokdefs.h

INCS=	pcap.h \
	pcap-bpf.h \
	pcap-namedb.h

PCAPINCS= \
	pcap/bluetooth.h \
	pcap/bpf.h \
	pcap/can_socketcan.h \
	pcap/dlt.h \
	pcap/export-defs.h \
	pcap/ipnet.h \
	pcap/namedb.h \
	pcap/nflog.h \
	pcap/pcap.h \
	pcap/sll.h \
	pcap/usb.h \
	pcap/vlan.h

PCAPINCSDIR=	${INCLUDEDIR}/pcap
INCSGROUPS=	INCS PCAPINCS

MAN=	pcap.3 \
	pcap_compile.3 \
	pcap_datalink.3 \
	pcap_dump_open.3 \
	pcap_list_datalinks.3 \
	pcap_list_tstamp_types.3 \
	pcap_open_dead.3 \
	pcap_open_offline.3 \
	pcap_set_tstamp_type.3
MAN+=	pcap_activate.3 \
	pcap_breakloop.3 \
	pcap_can_set_rfmon.3 \
	pcap_close.3 \
	pcap_create.3 \
	pcap_datalink_name_to_val.3 \
	pcap_datalink_val_to_name.3 \
	pcap_dump.3 \
	pcap_dump_close.3 \
	pcap_dump_file.3 \
	pcap_dump_flush.3 \
	pcap_dump_ftell.3 \
	pcap_file.3 \
	pcap_fileno.3 \
	pcap_findalldevs.3 \
	pcap_freecode.3 \
	pcap_get_selectable_fd.3 \
	pcap_get_tstamp_precision.3 \
	pcap_geterr.3 \
	pcap_inject.3 \
	pcap_is_swapped.3 \
	pcap_lib_version.3 \
	pcap_lookupdev.3 \
	pcap_lookupnet.3 \
	pcap_loop.3 \
	pcap_major_version.3 \
	pcap_next_ex.3 \
	pcap_offline_filter.3 \
	pcap_open_live.3 \
	pcap_set_buffer_size.3 \
	pcap_set_datalink.3 \
	pcap_set_immediate_mode.3 \
	pcap_set_promisc.3 \
	pcap_set_rfmon.3 \
	pcap_set_snaplen.3 \
	pcap_set_timeout.3 \
	pcap_set_tstamp_precision.3 \
	pcap_setdirection.3 \
	pcap_setfilter.3 \
	pcap_setnonblock.3 \
	pcap_snapshot.3 \
	pcap_stats.3 \
	pcap_statustostr.3 \
	pcap_strerror.3 \
	pcap_tstamp_type_name_to_val.3 \
	pcap_tstamp_type_val_to_name.3
MAN+=	pcap-savefile.5
MAN+=	pcap-filter.7 \
	pcap-linktype.7 \
	pcap-tstamp.7
MLINKS=	pcap_datalink_val_to_name.3 pcap_datalink_val_to_description.3 \
	pcap_dump_open.3 pcap_dump_fopen.3 \
	pcap_findalldevs.3 pcap_freealldevs.3 \
	pcap_geterr.3 pcap_perror.3 \
	pcap_inject.3 pcap_sendpacket.3 \
	pcap_list_datalinks.3 pcap_free_datalinks.3 \
	pcap_list_tstamp_types.3 pcap_free_tstamp_types.3 \
	pcap_loop.3 pcap_dispatch.3 \
	pcap_major_version.3 pcap_minor_version.3 \
	pcap_next_ex.3 pcap_next.3 \
	pcap_open_offline.3 pcap_fopen_offline.3 \
	pcap_setnonblock.3 pcap_getnonblock.3

CLEANFILES=tokdefs.h version.c pcap_version.h
CLEANFILES+=${MAN}
WARNS?=	2

LFLAGS+=-Ppcap_ --header-file=${.OBJDIR}/scanner.h --nounput
YFLAGS+=-p pcap_
CFLAGS+=-DHAVE_CONFIG_H -I${.CURDIR} -I${.OBJDIR}
CFLAGS+=-D_U_="__attribute__((unused))" -DYYBISON
CFLAGS+=-DHAVE_STRUCT_ETHER_ADDR -DHAVE_NET_PFVAR_H
.if !defined(NOINET6)
CFLAGS+=-DINET6
.endif

SHLIB_MAJOR=3

#
# Magic to grab sources out of src/contrib
#
PCAP_DISTDIR?=${SRCDIR}
CFLAGS+=-I${PCAP_DISTDIR}
.PATH:	${PCAP_DISTDIR}
.PATH:	${PCAP_DISTDIR}/bpf/net

version.c: VERSION
	echo '#include <pcap/export-defs.h>' > ${.TARGET}
	echo 'PCAP_API_DEF' >> ${.TARGET}
	sed -e 's/.*/char pcap_version[] = "&";/' ${.ALLSRC} > ${.TARGET}

pcap_version.h: VERSION
	sed -e 's/.*/static const char pcap_version_string[] = "libpcap version &";/' \
		${.ALLSRC} > ${.TARGET}

tokdefs.h: grammar.h
	${LN} -sf grammar.h tokdefs.h

#
# Magic to convert the man pages to something non Solarish
#
.for _page in ${MAN}
${_page}:
	if [ -f ${PCAP_DISTDIR}/${_page:S/3$/3pcap/} ]; then		\
		F=${_page:S/3$/3pcap/}; 				\
	elif [ -f ${PCAP_DISTDIR}/${_page:S/3$/3pcap.in/} ]; then	\
		F=${_page:S/3$/3pcap.in/};				\
	elif [ -f ${PCAP_DISTDIR}/${_page:S/5$/manfile/} ]; then	\
		F=${_page:S/5$/manfile/};				\
	elif [ -f ${PCAP_DISTDIR}/${_page:S/5$/manfile.in/} ]; then	\
		F=${_page:S/5$/manfile.in/};				\
	elif [ -f ${PCAP_DISTDIR}/${_page:S/7$/manmisc/} ]; then	\
		F=${_page:S/7$/manmisc/};				\
	elif [ -f ${PCAP_DISTDIR}/${_page:S/7$/manmisc.in/} ]; then	\
		F=${_page:S/7$/manmisc.in/};				\
	fi;								\
	sed -e 's/3PCAP/3/g' -e 's/@MAN_FILE_FORMATS@/5/g'		\
		-e 's/@MAN_MISC_INFO@/7/g' ${PCAP_DISTDIR}/$$F > ${_page}
.endfor

.include <bsd.lib.mk>

# $FreeBSD: src/lib/libarchive/Makefile,v 1.50 2006/09/05 05:59:45 kientzle Exp $
# $DragonFly: src/lib/libarchive/Makefile,v 1.12 2007/05/09 19:39:23 pavalos Exp $

LIB=	archive

CONTRIBDIR=	${.CURDIR}/../../contrib/libarchive-2/libarchive
.PATH: ${CONTRIBDIR}

VERSION!=	cat ${CONTRIBDIR}/../version
ARCHIVE_API_MAJOR!=	echo ${VERSION} | sed -e 's/[^0-9]/./g' -e 's/\..*//'
ARCHIVE_API_MINOR!=	echo ${VERSION} | sed -e 's/[^0-9]/./g' -e 's/[0-9]*\.//' -e 's/\..*//'
SHLIB_MAJOR=	5
CFLAGS+=	-DPLATFORM_CONFIG_H=\"config.h\"
CFLAGS+=	-DPACKAGE_STRING=\"libarchive\ ${VERSION}\"
CFLAGS+=	-DPACKAGE_VERSION=\"${VERSION}\"
CFLAGS+=	-DVERSION=\"${VERSION}\"
CFLAGS+=	-I${.OBJDIR} -I${.CURDIR} -I${CONTRIBDIR} -I-
WARNS?=	6

INCS=	archive.h archive_entry.h

# Note: archive.h does need to be listed here, since it's built
SRCS=	archive.h					\
	archive_check_magic.c				\
	archive_entry.c					\
	archive_entry_copy_stat.c			\
	archive_entry_stat.c				\
	archive_read.c					\
	archive_read_data_into_fd.c			\
	archive_read_extract.c				\
	archive_read_open_fd.c				\
	archive_read_open_file.c			\
	archive_read_open_filename.c			\
	archive_read_open_memory.c			\
	archive_read_support_compression_all.c		\
	archive_read_support_compression_bzip2.c	\
	archive_read_support_compression_compress.c	\
	archive_read_support_compression_gzip.c		\
	archive_read_support_compression_none.c		\
	archive_read_support_compression_program.c	\
	archive_read_support_format_all.c		\
	archive_read_support_format_ar.c		\
	archive_read_support_format_cpio.c		\
	archive_read_support_format_empty.c		\
	archive_read_support_format_iso9660.c		\
	archive_read_support_format_tar.c		\
	archive_read_support_format_zip.c		\
	archive_string.c				\
	archive_string_sprintf.c			\
	archive_util.c					\
	archive_virtual.c				\
	archive_write.c					\
	archive_write_disk.c				\
	archive_write_disk_set_standard_lookup.c	\
	archive_write_open_fd.c				\
	archive_write_open_file.c			\
	archive_write_open_filename.c			\
	archive_write_open_memory.c			\
	archive_write_set_compression_bzip2.c		\
	archive_write_set_compression_gzip.c		\
	archive_write_set_compression_none.c		\
	archive_write_set_compression_program.c		\
	archive_write_set_format.c			\
	archive_write_set_format_ar.c			\
	archive_write_set_format_by_name.c		\
	archive_write_set_format_cpio.c			\
	archive_write_set_format_pax.c			\
	archive_write_set_format_shar.c			\
	archive_write_set_format_ustar.c		\
	filter_fork.c

MAN=	archive_entry.3					\
	archive_read.3					\
	archive_util.3					\
	archive_write.3					\
	archive_write_disk.3				\
	libarchive.3					\
	libarchive-formats.5				\
	libarchive_internals.3				\
	tar.5

MLINKS+=	archive_entry.3 archive_entry_acl_add_entry.3
MLINKS+=	archive_entry.3 archive_entry_acl_add_entry_w.3
MLINKS+=	archive_entry.3 archive_entry_acl_clear.3
MLINKS+=	archive_entry.3 archive_entry_acl_count.3
MLINKS+=	archive_entry.3 archive_entry_acl_next.3
MLINKS+=	archive_entry.3 archive_entry_acl_next_w.3
MLINKS+=	archive_entry.3 archive_entry_acl_reset.3
MLINKS+=	archive_entry.3 archive_entry_acl_text_w.3
MLINKS+=	archive_entry.3 archive_entry_atime.3
MLINKS+=	archive_entry.3 archive_entry_atime_nsec.3
MLINKS+=	archive_entry.3 archive_entry_clear.3
MLINKS+=	archive_entry.3 archive_entry_clone.3
MLINKS+=	archive_entry.3 archive_entry_copy_fflags_text_w.3
MLINKS+=	archive_entry.3 archive_entry_copy_gname_w.3
MLINKS+=	archive_entry.3 archive_entry_copy_hardlink.3
MLINKS+=	archive_entry.3 archive_entry_copy_hardlink_w.3
MLINKS+=	archive_entry.3 archive_entry_copy_pathname_w.3
MLINKS+=	archive_entry.3 archive_entry_copy_stat.3
MLINKS+=	archive_entry.3 archive_entry_copy_symlink.3
MLINKS+=	archive_entry.3 archive_entry_copy_symlink_w.3
MLINKS+=	archive_entry.3 archive_entry_copy_uname_w.3
MLINKS+=	archive_entry.3 archive_entry_dev.3
MLINKS+=	archive_entry.3 archive_entry_fflags.3
MLINKS+=	archive_entry.3 archive_entry_fflags_text.3
MLINKS+=	archive_entry.3 archive_entry_free.3
MLINKS+=	archive_entry.3 archive_entry_gid.3
MLINKS+=	archive_entry.3 archive_entry_gname.3
MLINKS+=	archive_entry.3 archive_entry_hardlink.3
MLINKS+=	archive_entry.3 archive_entry_ino.3
MLINKS+=	archive_entry.3 archive_entry_mode.3
MLINKS+=	archive_entry.3 archive_entry_mtime.3
MLINKS+=	archive_entry.3 archive_entry_mtime_nsec.3
MLINKS+=	archive_entry.3 archive_entry_new.3
MLINKS+=	archive_entry.3 archive_entry_pathname.3
MLINKS+=	archive_entry.3 archive_entry_pathname_w.3
MLINKS+=	archive_entry.3 archive_entry_rdev.3
MLINKS+=	archive_entry.3 archive_entry_rdevmajor.3
MLINKS+=	archive_entry.3 archive_entry_rdevminor.3
MLINKS+=	archive_entry.3 archive_entry_set_fflags.3
MLINKS+=	archive_entry.3 archive_entry_set_gid.3
MLINKS+=	archive_entry.3 archive_entry_set_gname.3
MLINKS+=	archive_entry.3 archive_entry_set_hardlink.3
MLINKS+=	archive_entry.3 archive_entry_set_link.3
MLINKS+=	archive_entry.3 archive_entry_set_mode.3
MLINKS+=	archive_entry.3 archive_entry_set_mtime.3
MLINKS+=	archive_entry.3 archive_entry_set_pathname.3
MLINKS+=	archive_entry.3 archive_entry_set_rdevmajor.3
MLINKS+=	archive_entry.3 archive_entry_set_rdevminor.3
MLINKS+=	archive_entry.3 archive_entry_set_size.3
MLINKS+=	archive_entry.3 archive_entry_set_symlink.3
MLINKS+=	archive_entry.3 archive_entry_set_uid.3
MLINKS+=	archive_entry.3 archive_entry_set_uname.3
MLINKS+=	archive_entry.3 archive_entry_size.3
MLINKS+=	archive_entry.3 archive_entry_stat.3
MLINKS+=	archive_entry.3 archive_entry_symlink.3
MLINKS+=	archive_entry.3 archive_entry_uid.3
MLINKS+=	archive_entry.3 archive_entry_uname.3
MLINKS+=	archive_read.3 archive_read_close.3
MLINKS+=	archive_read.3 archive_read_data.3
MLINKS+=	archive_read.3 archive_read_data_block.3
MLINKS+=	archive_read.3 archive_read_data_into_buffer.3
MLINKS+=	archive_read.3 archive_read_data_into_fd.3
MLINKS+=	archive_read.3 archive_read_data_skip.3
MLINKS+=	archive_read.3 archive_read_extract.3
MLINKS+=	archive_read.3 archive_read_extract_set_progress_callback.3
MLINKS+=	archive_read.3 archive_read_finish.3
MLINKS+=	archive_read.3 archive_read_new.3
MLINKS+=	archive_read.3 archive_read_next_header.3
MLINKS+=	archive_read.3 archive_read_open.3
MLINKS+=	archive_read.3 archive_read_open2.3
MLINKS+=	archive_read.3 archive_read_open_FILE.3
MLINKS+=	archive_read.3 archive_read_open_fd.3
MLINKS+=	archive_read.3 archive_read_open_filename.3
MLINKS+=	archive_read.3 archive_read_open_memory.3
MLINKS+=	archive_read.3 archive_read_support_compression_all.3
MLINKS+=	archive_read.3 archive_read_support_compression_bzip2.3
MLINKS+=	archive_read.3 archive_read_support_compression_compress.3
MLINKS+=	archive_read.3 archive_read_support_compression_gzip.3
MLINKS+=	archive_read.3 archive_read_support_compression_none.3
MLINKS+=	archive_read.3 archive_read_support_format_all.3
MLINKS+=	archive_read.3 archive_read_support_format_cpio.3
MLINKS+=	archive_read.3 archive_read_support_format_empty.3
MLINKS+=	archive_read.3 archive_read_support_format_iso9660.3
MLINKS+=	archive_read.3 archive_read_support_format_tar.3
MLINKS+=	archive_read.3 archive_read_support_format_zip.3
MLINKS+=	archive_util.3 archive_clear_error.3
MLINKS+=	archive_util.3 archive_compression.3
MLINKS+=	archive_util.3 archive_compression_name.3
MLINKS+=	archive_util.3 archive_errno.3
MLINKS+=	archive_util.3 archive_error_string.3
MLINKS+=	archive_util.3 archive_format.3
MLINKS+=	archive_util.3 archive_format_name.3
MLINKS+=	archive_util.3 archive_set_error.3
MLINKS+=	archive_write.3 archive_write_close.3
MLINKS+=	archive_write.3 archive_write_data.3
MLINKS+=	archive_write.3 archive_write_finish.3
MLINKS+=	archive_write.3 archive_write_finish_entry.3
MLINKS+=	archive_write.3 archive_write_get_bytes_per_block.3
MLINKS+=	archive_write.3 archive_write_header.3
MLINKS+=	archive_write.3 archive_write_new.3
MLINKS+=	archive_write.3 archive_write_open.3
MLINKS+=	archive_write.3 archive_write_open_FILE.3
MLINKS+=	archive_write.3 archive_write_open_fd.3
MLINKS+=	archive_write.3 archive_write_open_filename.3
MLINKS+=	archive_write.3 archive_write_open_memory.3
MLINKS+=	archive_write.3 archive_write_set_bytes_in_last_block.3
MLINKS+=	archive_write.3 archive_write_set_bytes_per_block.3
MLINKS+=	archive_write.3 archive_write_set_compression_bzip2.3
MLINKS+=	archive_write.3 archive_write_set_compression_gzip.3
MLINKS+=	archive_write.3 archive_write_set_format_cpio.3
MLINKS+=	archive_write.3 archive_write_set_format_pax.3
MLINKS+=	archive_write.3 archive_write_set_format_pax_restricted.3
MLINKS+=	archive_write.3 archive_write_set_format_shar.3
MLINKS+=	archive_write.3 archive_write_set_format_shar_binary.3
MLINKS+=	archive_write.3 archive_write_set_format_ustar.3
MLINKS+=	archive_write_disk.3 archive_write_disk_close.3
MLINKS+=	archive_write_disk.3 archive_write_disk_data.3
MLINKS+=	archive_write_disk.3 archive_write_disk_finish.3
MLINKS+=	archive_write_disk.3 archive_write_disk_finish_entry.3
MLINKS+=	archive_write_disk.3 archive_write_disk_header.3
MLINKS+=	archive_write_disk.3 archive_write_disk_new.3
MLINKS+=	archive_write_disk.3 archive_write_disk_set_group_lookup.3
MLINKS+=	archive_write_disk.3 archive_write_disk_set_options.3
MLINKS+=	archive_write_disk.3 archive_write_disk_set_skip_file.3
MLINKS+=	archive_write_disk.3 archive_write_disk_set_standard_lookup.3
MLINKS+=	archive_write_disk.3 archive_write_disk_set_user_lookup.3
MLINKS+=	libarchive.3 archive.3

# Build archive.h from archive.h.in
archive.h:	archive.h.in Makefile
	cat ${CONTRIBDIR}/archive.h.in |				\
		sed 's/@ARCHIVE_VERSION@/${VERSION}/g' |		\
		sed 's/@SHLIB_MAJOR@/${SHLIB_MAJOR}/g' |		\
		sed 's/@ARCHIVE_API_MAJOR@/${ARCHIVE_API_MAJOR}/g' |	\
		sed 's/@ARCHIVE_API_MINOR@/${ARCHIVE_API_MINOR}/g' |	\
		sed 's|@ARCHIVE_H_INCLUDE_INTTYPES_H@|#include <inttypes.h>  /* For int64_t */|g' |			\
		cat > archive.h

# archive.h needs to be cleaned
CLEANFILES+=	archive.h

.include <bsd.lib.mk>

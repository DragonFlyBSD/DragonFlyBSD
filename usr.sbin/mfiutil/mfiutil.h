/*-
 * Copyright (c) 2008, 2009 Yahoo!, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/usr.sbin/mfiutil/mfiutil.h,v 1.2 2011/06/20 21:28:50 bz Exp $
 */

#ifndef __MFIUTIL_H__
#define	__MFIUTIL_H__

#include <sys/linker_set.h>

#include <dev/raid/mfi/mfireg.h>

/* Constants for DDF RAID levels. */
#define	DDF_RAID0		0x00
#define	DDF_RAID1		0x01
#define	DDF_RAID3		0x03
#define	DDF_RAID5		0x05
#define	DDF_RAID6		0x06
#define	DDF_RAID1E		0x11
#define	DDF_JBOD		0x0f
#define	DDF_CONCAT		0x1f
#define	DDF_RAID5E		0x15
#define	DDF_RAID5EE		0x25

struct mfiutil_command {
	const char *name;
	int (*handler)(int ac, char **av);
};

#define	MFI_DATASET(name)	mfiutil_ ## name ## _table

#define	MFI_COMMAND(set, name, function)				\
	static struct mfiutil_command function ## _mfiutil_command =	\
	{ #name, function };						\
	DATA_SET(MFI_DATASET(set), function ## _mfiutil_command)

#define	MFI_TABLE(set, name)						\
	SET_DECLARE(MFI_DATASET(name), struct mfiutil_command);		\
									\
	static int							\
	mfiutil_ ## name ## _table_handler(int ac, char **av)		\
	{								\
		return (mfi_table_handler(SET_BEGIN(MFI_DATASET(name)), \
		    SET_LIMIT(MFI_DATASET(name)), ac, av));		\
	}								\
	MFI_COMMAND(set, name, mfiutil_ ## name ## _table_handler)

/* Drive name printing options */
#define	MFI_DNAME_ES		0x0001	/* E%u:S%u */
#define	MFI_DNAME_DEVICE_ID	0x0002	/* %u */
#define	MFI_DNAME_HONOR_OPTS	0x8000	/* Allow cmd line to override default */

extern int mfi_unit;
extern u_int mfi_opts;

void	mbox_store_ldref(uint8_t *mbox, union mfi_ld_ref *ref);
void	mbox_store_pdref(uint8_t *mbox, union mfi_pd_ref *ref);
void	mfi_display_progress(const char *label, struct mfi_progress *prog);
int	mfi_table_handler(struct mfiutil_command **start,
    struct mfiutil_command **end, int ac, char **av);
const char *mfi_raid_level(uint8_t primary_level, uint8_t secondary_level);
const char *mfi_ldstate(enum mfi_ld_state state);
const char *mfi_pdstate(enum mfi_pd_state state);
const char *mfi_pd_inq_string(struct mfi_pd_info *info);
const char *mfi_volume_name(int fd, uint8_t target_id);
int	mfi_volume_busy(int fd, uint8_t target_id);
int	mfi_config_read(int fd, struct mfi_config_data **configp);
int	mfi_config_read_opcode(int fd, uint32_t opcode,
    struct mfi_config_data **configp, uint8_t *mbox, size_t mboxlen);
int	mfi_lookup_drive(int fd, char *drive, uint16_t *device_id);
int	mfi_lookup_volume(int fd, const char *name, uint8_t *target_id);
int	mfi_dcmd_command(int fd, uint32_t opcode, void *buf, size_t bufsize,
    uint8_t *mbox, size_t mboxlen, uint8_t *statusp);
int	mfi_open(int unit);
int	mfi_ctrl_get_info(int fd, struct mfi_ctrl_info *info, uint8_t *statusp);
int	mfi_ld_get_info(int fd, uint8_t target_id, struct mfi_ld_info *info,
    uint8_t *statusp);
int	mfi_ld_get_list(int fd, struct mfi_ld_list *list, uint8_t *statusp);
int	mfi_pd_get_info(int fd, uint16_t device_id, struct mfi_pd_info *info,
    uint8_t *statusp);
int	mfi_pd_get_list(int fd, struct mfi_pd_list **listp, uint8_t *statusp);
int	mfi_reconfig_supported(void);
const char *mfi_status(u_int status_code);
const char *mfi_drive_name(struct mfi_pd_info *pinfo, uint16_t device_id,
    uint32_t def);
void	format_stripe(char *buf, size_t buflen, uint8_t stripe);
void	print_ld(struct mfi_ld_info *info, int state_len);
void	print_pd(struct mfi_pd_info *info, int state_len);
void	dump_config(int fd, struct mfi_config_data *config, const char* msg_prefix);

void	scan_firmware(struct mfi_info_component *comp);
void	display_firmware(struct mfi_info_component *comp, const char *tag);

#endif /* !__MFIUTIL_H__ */

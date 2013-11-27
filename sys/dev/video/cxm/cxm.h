/*
 * Copyright (c) 2003, 2004, 2005
 *	John Wehle <john@feith.com>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by John Wehle.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.	IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _CXM_H
#define _CXM_H

/*
 * Header file for the Conexant MPEG-2 Codec driver.
 */

#include <sys/event.h>

#include "pcidevs.h"

#define bswap32(X) ntohl(X)

#define NUM_ELEMENTS(array) (sizeof(array) / sizeof(*array))

/*
 * For simplicity several large buffers allocate during
 * driver attachment which normally occurs early on
 * (when large areas of memory are available) are used
 * to move data to / from the card.  It's not unusual
 * for the memory allocation to fail due to fragmentation
 * if the driver is loaded after the system has been
 * running for a while.  One solution is to allocate
 * several PAGE_SIZE buffers instead, however it doesn't
 * seem worth the trouble.
 */
enum cxm_byte_order { cxm_unknown_byte_order,
		      cxm_device_mpeg_byte_order, cxm_device_yuv12_byte_order,
		      cxm_host_byte_order };

struct cxm_buffer {
	char		*vaddr;
	bus_addr_t	baddr;
	bus_dmamap_t	dmamap;
	size_t		size;
	enum cxm_byte_order byte_order;
};

#define CXM_SG_BUFFERS 50

struct cxm_buffer_pool {
	bus_dma_tag_t		dmat;
	size_t			offset;
	unsigned int		read;
	volatile unsigned int	write;
	struct cxm_buffer	bufs[CXM_SG_BUFFERS];
};

/*
 * Audio format encoding
 *
 * 7 6 5 4 3 2 1 0
 *
 *             0 0  44.1 kHz
 *             0 1  48 kHz
 *             1 0  32 kHz
 *
 *         0 1  Layer 1
 *         1 0  Layer 2
 *         1 1  Layer 3
 *
 *          L1 / L2
 * 0 0 0 0  Free fmt
 * 0 0 0 1  32k / 32k
 * 0 0 1 0  64k / 48k
 * 0 0 1 1  96k / 56k
 * 0 1 0 0  128k / 64k
 * 0 1 0 1  160k / 80k
 * 0 1 1 0  192k / 96k
 * 0 1 1 1  224k / 112k
 * 1 0 0 0  256k / 128k
 * 1 0 0 1  288k / 160k
 * 1 0 1 0  320k / 192k
 * 1 0 1 1  352k / 224k
 * 1 1 0 0  384k / 256k
 * 1 1 0 1  416k / 320k
 * 1 1 1 0  448k / 384k
 */
struct cxm_codec_audio_format {
	unsigned int	sample_rate;
	uint32_t	format;
};

struct cxm_codec_profile {
	const char	*name;
	uint32_t	stream_type;
	uint32_t	fps;
	uint32_t	width;
	uint32_t	height;
	uint32_t	source_height;
	struct {
		uint32_t	start;
		uint32_t	nlines;
		uint32_t	cc;
	} vbi;
	uint32_t	aspect;
	uint32_t	pulldown;
	struct {
		uint32_t	mode;
		uint32_t	average;
		uint32_t	peak;
	} bitrate;
	struct {
		uint32_t	closure;
		uint32_t	frames;
		uint32_t	bframes;
	} gop;
	struct {
		uint32_t	mode;
		uint32_t	type;
		uint32_t	spatial;
		uint32_t	temporal;
	} dnr;

	unsigned int	audio_sample_rate;
};

struct cxm_dev {
	uint16_t	vid;
	uint16_t	did;
	char		*name;
};

#define CXM_MBX_FW_CMD_MAILBOX   0
#define CXM_MBX_FW_CMD_MAILBOXES 6

#define CXM_MBX_FW_DMA_MAILBOX   9

#define CXM_MBX_MAX_PARAMETERS   16

/* Mailbox flags bit definitions */
#define CXM_MBX_FLAG_DRV_DONE 0x00000002
#define CXM_MBX_FLAG_FW_DONE  0x00000004
#define CXM_MBX_FLAG_IN_USE   0x00000001

struct cxm_mailbox {
	uint32_t	flags;
	uint32_t	command;
	uint32_t	result;
	uint32_t	timeout;
	uint32_t	parameters[CXM_MBX_MAX_PARAMETERS];
} __attribute__ ((packed));

enum cxm_mailbox_name { cxm_unknown_mailbox,
			cxm_dec_mailbox, cxm_enc_mailbox };

/*
 * Scatter / gather is supported with the restriction
 * that the size of each piece must be a multiple of
 * 256 and less than 64k.
 */
#define CXM_SG_SEGMENT  (0xff00 & ~(PAGE_SIZE - 1))

struct cxm_sg_entry {
	uint32_t	src;
	uint32_t	dst;
	uint32_t	size;
} __attribute__ ((packed));

struct cxm_sg_list {
	bus_dma_tag_t	dmat;
	struct cxm_sg_entry *vaddr;
	bus_addr_t	baddr;
	bus_dmamap_t	dmamap;

};

enum cxm_source { cxm_unknown_source, cxm_fm_source, cxm_tuner_source,
		  cxm_line_in_source_composite, cxm_line_in_source_svideo };

enum cxm_source_format { cxm_unknown_source_format,
			 cxm_bw_50hz_source_format,
			 cxm_bw_60hz_source_format,
			 cxm_ntsc_50hz_source_format,
			 cxm_ntsc_60hz_source_format,
			 cxm_pal_50hz_source_format,
			 cxm_pal_60hz_source_format,
			 cxm_secam_50hz_source_format };

enum cxm_type { cxm_unknown_type, cxm_iTVC15_type, cxm_iTVC16_type };

/*
 * Conexant iTVC15 / iTVC16 info structure, one per card installed.
 */
struct cxm_softc {
	enum cxm_type	type;

	struct resource	*mem_res;	/* Resource descriptor for registers */
	bus_space_tag_t	btag;		/* Bus space access functions */
	bus_space_handle_t bhandle;	/* Bus space access functions */

	struct resource *irq_res;	/* Resource descriptor for interrupt */
	void            *ih_cookie;	/* Newbus interrupt handler cookie */

	uint32_t	irq_mask;

	bus_dma_tag_t	parent_dmat;

	struct cxm_buffer_pool	enc_pool;
	struct cxm_sg_list enc_sg;

	struct kqinfo	enc_kq;

	struct proc	*enc_proc;
	int		enc_signal;

	unsigned int	dec_mbx;
	unsigned int	enc_mbx;

	device_t	cxm_iic;
	device_t	iicbus;

	const struct cxm_tuner *tuner;
	const struct cxm_tuner_channels *tuner_channels;
	int		tuner_afc;
	unsigned long	tuner_freq;

	char		msp_name[10];

	const struct cxm_codec_profile *profile;

	enum cxm_source	source;

	device_t	dev;		/* bus attachment */
	cdev_t		cxm_dev_t;	/* control device */
	int		is_opened;
	int		mpeg;

	int		encoding;
	int		encoding_dma;
	int		encoding_eos;
	int		video_std;
};

/*
 * Conexant iTVC15 / iTVC16 I2C info structure, one per card installed.
 */
struct cxm_iic_softc {
	bus_space_tag_t	btag;		/* Bus space access functions */
	bus_space_handle_t bhandle;	/* Bus space access functions */

	device_t	iicbb;

};

/*
 * List of IVARS available to the I2C device driver
 */
#define CXM_IVAR_BHANDLE 0
#define CXM_IVAR_BTAG    1
#define CXM_IVAR_IICBUS  2

/*
 * Bus resource id
 */
#define CXM_RID PCIR_MAPS

/*
 * Access macros
 */
#define CSR_WRITE_4(sc, reg, val)       \
	bus_space_write_4((sc)->btag, (sc)->bhandle, (reg), (val))
#define CSR_WRITE_2(sc, reg, val)       \
	bus_space_write_2((sc)->btag, (sc)->bhandle, (reg), val))
#define CSR_WRITE_1(sc, reg, val)       \
	bus_space_write_1((sc)->btag, (sc)->bhandle, (reg), val))
#define CSR_READ_4(sc, reg)             \
	bus_space_read_4((sc)->btag, (sc)->bhandle, (reg))
#define CSR_READ_2(sc, reg)             \
	bus_space_read_2((sc)->btag, (sc)->bhandle, (reg))
#define CSR_READ_1(sc, reg)             \
	bus_space_read_1((sc)->btag, (sc)->bhandle, (reg))

/*
 * Decoder / encoder firmware
 */
extern const char cxm_dec_fw[];
extern const char cxm_enc_fw[];

#define CXM_FW_SIZE (256 * 1024)

/*
 * Decoder / encoder memory offsets
 */
#define CXM_MEM_DEC 0x01000000
#define CXM_MEM_ENC 0x00000000

#define CXM_MEM_DEC_SIZE 0x01000000
#define CXM_MEM_ENC_SIZE 0x01000000

/*
 * Register offsets
 */
#define CXM_REG_AO                  0x2002d00
#define CXM_REG_APU                 0x200a064
#define CXM_REG_DEC_SDRAM_PRECHARGE 0x20008fc
#define CXM_REG_DEC_SDRAM_REFRESH   0x20008f8
#define CXM_REG_DMA_STATUS          0x2000004
#define CXM_REG_ENC_SDRAM_PRECHARGE 0x20007fc
#define CXM_REG_ENC_SDRAM_REFRESH   0x20007f8
#define CXM_REG_HW_BLOCKS           0x2009054
#define CXM_REG_I2C_GETSCL          0x2007008
#define CXM_REG_I2C_GETSDA          0x200700c
#define CXM_REG_I2C_SETSCL          0x2007000
#define CXM_REG_I2C_SETSDA          0x2007004
#define CXM_REG_IRQ_MASK            0x2000048
#define CXM_REG_IRQ_STATUS          0x2000040
#define CXM_REG_SPU                 0x2009050
#define CXM_REG_VDM                 0x2002800
#define CXM_REG_VPU                 0x2009058

/*
 * Register values
 */
#define CXM_CMD_AO_STOP              0x00000005
#define CXM_CMD_APU_PING             0x00000000
#define CXM_CMD_HW_BLOCKS_RST        0xffffffff
#define CXM_CMD_SDRAM_PRECHARGE_INIT 0x0000001a
#define CXM_CMD_SDRAM_REFRESH_INIT   0x80000640
#define CXM_CMD_SPU_STOP             0x00000001
#define CXM_CMD_VDM_STOP             0x00000000
#define CXM_CMD_VPU_STOP15           0xfffffffe
#define CXM_CMD_VPU_STOP16           0xffffffee

#define CXM_DMA_ERROR_LIST           0x00000008
#define CXM_DMA_ERROR_READ           0x00000002
#define CXM_DMA_ERROR_WRITE          0x00000004
#define CXM_DMA_SUCCESS              0x00000001

#define CXM_IRQ_DEC_DMA_DONE         (1 << 20)
#define CXM_IRQ_DEC_DMA_REQUEST      (1 << 22)
#define CXM_IRQ_DEC_VSYNC            (1 << 10)
#define CXM_IRQ_ENC_DMA_DONE         (1 << 27)
#define CXM_IRQ_ENC_DMA_REQUEST      (1 << 31)
#define CXM_IRQ_ENC_EOS              (1 << 30)
#define CXM_IRQ_ENC_EVENT            (1 << 28)

#define CXM_IRQ_ENC (CXM_IRQ_ENC_DMA_REQUEST | CXM_IRQ_ENC_DMA_DONE \
		     | CXM_IRQ_ENC_EOS | CXM_IRQ_ENC_EVENT)

/*
 * Register masks
 */
#define CXM_MASK_SPU_ENABLE          0xfffffffe
#define CXM_MASK_VPU_ENABLE15        0xfffffff6
#define CXM_MASK_VPU_ENABLE16        0xfffffffb

/*
 * Firmware commands
 */
#define CXM_FW_CMD_ASSIGN_3_2_PULLDOWN          0x000000b1
#define CXM_FW_CMD_ASSIGN_ASPECT_RATIO          0x00000099
#define CXM_FW_CMD_ASSIGN_AUDIO_PROPERTIES      0x000000bd
#define CXM_FW_CMD_ASSIGN_BITRATES              0x00000095
#define CXM_FW_CMD_ASSIGN_CORING_LEVELS         0x0000009f
#define CXM_FW_CMD_ASSIGN_DMA_BLOCKLEN          0x000000c9
#define CXM_FW_CMD_ASSIGN_DNR_FILTER_MODE       0x0000009b
#define CXM_FW_CMD_ASSIGN_DNR_FILTER_PROPERTIES 0x0000009d
#define CXM_FW_CMD_ASSIGN_FRAME_DROP_RATE       0x000000d0
#define CXM_FW_CMD_ASSIGN_FRAME_RATE            0x0000008f
#define CXM_FW_CMD_ASSIGN_FRAME_SIZE            0x00000091
#define CXM_FW_CMD_ASSIGN_GOP_CLOSURE           0x000000c5
#define CXM_FW_CMD_ASSIGN_GOP_PROPERTIES        0x00000097
#define CXM_FW_CMD_ASSIGN_NUM_VSYNC_LINES       0x000000d6
#define CXM_FW_CMD_ASSIGN_OUTPUT_PORT           0x000000bb
#define CXM_FW_CMD_ASSIGN_PGM_INDEX_INFO        0x000000c7
#define CXM_FW_CMD_ASSIGN_PLACEHOLDER           0x000000d8
#define CXM_FW_CMD_ASSIGN_SPATIAL_FILTER_TYPE   0x000000a1
#define CXM_FW_CMD_ASSIGN_STREAM_TYPE           0x000000b9
#define CXM_FW_CMD_ASSIGN_VBI_LINE              0x000000b7
#define CXM_FW_CMD_ASSIGN_VBI_PROPERTIES        0x000000c8
#define CXM_FW_CMD_BEGIN_CAPTURE                0x00000081
#define CXM_FW_CMD_DEC_EVENT_NOTIFICATION       0x00000017
#define CXM_FW_CMD_DEC_GET_FW_VER               0x00000011
#define CXM_FW_CMD_DEC_HALT_FW                  0x0000000e
#define CXM_FW_CMD_ENC_EVENT_NOTIFICATION       0x000000d5
#define CXM_FW_CMD_ENC_GET_FW_VER               0x000000c4
#define CXM_FW_CMD_ENC_HALT_FW                  0x000000c3
#define CXM_FW_CMD_END_CAPTURE                  0x00000082
#define CXM_FW_CMD_INITIALIZE_VIDEO_INPUT       0x000000cd
#define CXM_FW_CMD_MUTE_VIDEO_INPUT             0x000000d9
#define CXM_FW_CMD_PAUSE_ENCODER                0x000000d2
#define CXM_FW_CMD_SCHED_DMA_TO_HOST            0x000000cc

#define CXM_FW_STD_TIMEOUT                          0x00010000

#define CXM_FW_CAPTURE_STREAM_TYPE_MPEG             0x00000000
#define CXM_FW_CAPTURE_STREAM_TYPE_RAW              0x00000001
#define CXM_FW_CAPTURE_STREAM_TYPE_RAW_PASSTHROUGH  0x00000002
#define CXM_FW_CAPTURE_STREAM_TYPE_VBI              0x00000003

#define CXM_FW_CAPTURE_STREAM_YUV                   0x00000001
#define CXM_FW_CAPTURE_STREAM_PCM_AUDIO             0x00000002
#define CXM_FW_CAPTURE_STREAM_VBI                   0x00000004

#define CXM_FW_STREAM_TYPE_DVD                      0x0000000a
#define CXM_FW_STREAM_TYPE_MPEG1                    0x00000002
#define CXM_FW_STREAM_TYPE_MPEG2_PROGRAM            0x00000000
#define CXM_FW_STREAM_TYPE_SVCD                     0x0000000c
#define CXM_FW_STREAM_TYPE_VCD                      0x0000000b

#define CXM_MACROBLOCK_HEIGHT 16
#define CXM_MACROBLOCK_WIDTH  16
#define CXM_MACROBLOCK_SIZE   (CXM_MACROBLOCK_HEIGHT * CXM_MACROBLOCK_WIDTH)

/*
 * I2C addresses
 */
#define CXM_I2C_CX2584x  0x88
#define CXM_I2C_EEPROM   0xa0
#define CXM_I2C_IR       0x30
#define CXM_I2C_MSP3400  0x80
#define CXM_I2C_SAA7115  0x42
#define CXM_I2C_TDA988x_W (0x43 << 1)		/* Write address */
#define CXM_I2C_TDA988x_R ((0x43 << 1) | 0x01)	/* Read address */
#define CXM_I2C_TUNER    0xc2
#define CXM_I2C_TUNER_IF 0x86
#define CXM_I2C_WM8775   0x36

#define CXM_I2C_TIMEOUT  1000

/*
 * EEPROM
 */
int cxm_eeprom_init(struct cxm_softc *sc);
int cxm_eeprom_tuner_type(struct cxm_softc *sc);

/*
 * Infrared remote
 */
int cxm_ir_init(struct cxm_softc *sc);
int cxm_ir_key(struct cxm_softc *sc, char *buf, int len);

/*
 * msp34xxx Audio decoder
 */
#define CXM_MSP3400C_DEM 0x10
#define CXM_MSP3400C_DFP 0x12

struct cxm_msp_setting {
	unsigned char	dev;
	unsigned int	addr;
	char		value[2];
};

struct cxm_msp_command {
	unsigned int nsettings;
	struct cxm_msp_setting settings[5];
};

int cxm_msp_init(struct cxm_softc *sc);
int cxm_msp_mute(struct cxm_softc *sc);
int cxm_msp_unmute(struct cxm_softc *sc);
int cxm_msp_is_muted(struct cxm_softc *sc);
int cxm_msp_select_source(struct cxm_softc *sc, enum cxm_source source);
enum cxm_source cxm_msp_selected_source(struct cxm_softc *sc);
int cxm_msp_autodetect_standard(struct cxm_softc *sc);
int cxm_msp_is_locked(struct cxm_softc *sc);
int cxm_msp_wait_for_lock(struct cxm_softc *sc);

/*
 * wm8775 Audio ADC
 */
struct cxm_wm8775_setting {
	unsigned char	addr;
	uint16_t	value;
};

struct cxm_wm8775_command {
	unsigned int nsettings;
	struct cxm_wm8775_setting settings[13];
};

int cxm_wm8775_init(struct cxm_softc *sc);

/*
 * tda988x Demodulator
 */
int cxm_tda988x_init(struct cxm_softc *sc);
int cxm_tda988x_diag(struct cxm_softc *sc);

/*
 * cx2584x Decoder
 */
struct cxm_cx2584x_setting {
	uint16_t	addr;
	unsigned char	value;
};

struct cxm_cx2584x_command {
	unsigned int nsettings;
	struct cxm_cx2584x_setting settings[21];
};

int cxm_cx2584x_init(struct cxm_softc *sc);
int cxm_cx2584x_mute(struct cxm_softc *sc);
int cxm_cx2584x_unmute(struct cxm_softc *sc);
int cxm_cx2584x_select_source(struct cxm_softc *sc, enum cxm_source source);
int cxm_cx2584x_set_std(struct cxm_softc *sc);

/*
 * Tuner
 */
#define CXM_TUNER_PHILIPS_FI1216_MK2   0
#define CXM_TUNER_PHILIPS_FM1216       1
#define CXM_TUNER_PHILIPS_FQ1216ME     2
#define CXM_TUNER_PHILIPS_FQ1216ME_MK3 3
#define CXM_TUNER_PHILIPS_FM1216ME_MK3 4
#define CXM_TUNER_PHILIPS_FI1236_MK2   5
#define CXM_TUNER_PHILIPS_FM1236       6
#define CXM_TUNER_PHILIPS_FI1246_MK2   7
#define CXM_TUNER_PHILIPS_FM1246       8
#define CXM_TUNER_TEMIC_4006_FH5       9
#define CXM_TUNER_TEMIC_4009_FR5      10
#define CXM_TUNER_TEMIC_4036_FY5      11
#define CXM_TUNER_TEMIC_4039_FR5      12
#define CXM_TUNER_TEMIC_4066_FY5      13
#define CXM_TUNER_LG_TPI8PSB11D       14
#define CXM_TUNER_LG_TPI8PSB01N       15
#define CXM_TUNER_LG_TAPC_H701F       16
#define CXM_TUNER_LG_TAPC_H001F       17
#define CXM_TUNER_LG_TAPE_H001F       18
#define CXM_TUNER_MICROTUNE_4049_FM5  19
#define CXM_TUNER_TCL_2002N_6A        20
#define CXM_TUNER_TYPES               21

#define CXM_TUNER_AFC_MASK           0x07

#define CXM_TUNER_AFC_FREQ_MINUS_125 0x00
#define CXM_TUNER_AFC_FREQ_MINUS_62  0x01
#define CXM_TUNER_AFC_FREQ_CENTERED  0x02
#define CXM_TUNER_AFC_FREQ_PLUS_62   0x03
#define CXM_TUNER_AFC_FREQ_PLUS_125  0x04

#define CXM_TUNER_PHASE_LOCKED       0x40

#define CXM_TUNER_FM_SYSTEM          0x01
#define CXM_TUNER_TV_SYSTEM_BG       0x02
#define CXM_TUNER_TV_SYSTEM_DK       0x04
#define CXM_TUNER_TV_SYSTEM_I        0x08
#define CXM_TUNER_TV_SYSTEM_MN       0x10
#define CXM_TUNER_TV_SYSTEM_L        0x20
#define CXM_TUNER_TV_SYSTEM_L_PRIME  0x40

struct cxm_tuner_band_code {
	unsigned long	freq;
	unsigned char	codes[2];
};

struct cxm_tuner_channel_assignment {
	unsigned int	channel;
	unsigned long	freq;
	unsigned long	step;
};

struct cxm_tuner_channels {
	const char	*name;
	unsigned int	chnlset;
	unsigned int	system;
	unsigned int	min_channel;
	unsigned int	max_channel;
	unsigned long	if_freq;
	struct cxm_tuner_channel_assignment assignments[17];
};

struct cxm_tuner_system_code {
	unsigned int	system;
	unsigned char	codes[4];
};

enum cxm_tuner_system_code_style { cxm_unknown_system_code_style,
				   cxm_none_system_code_style,
				   cxm_port_system_code_style,
				   cxm_if_system_code_style,
				   cxm_if_system_with_aux_code_style };

struct cxm_tuner_system {
	unsigned int				supported;
	enum cxm_tuner_system_code_style	code_style;
	struct cxm_tuner_system_code		codes[6];
};

struct cxm_tuner {
	const char	*name;
	struct cxm_tuner_system	systems;
	unsigned long	min_freq;
	unsigned long	max_freq;
	struct cxm_tuner_band_code band_codes[3];
	unsigned long fm_min_freq;
	unsigned long fm_max_freq;
	struct cxm_tuner_band_code fm_band_code;
	const struct cxm_tuner_channels *default_channels;
};

enum cxm_tuner_freq_type { cxm_tuner_unknown_freq_type, cxm_tuner_fm_freq_type,
			   cxm_tuner_tv_freq_type };

extern const struct cxm_tuner cxm_tuners[];

int cxm_tuner_init(struct cxm_softc *sc);
int cxm_tuner_select_channel_set(struct cxm_softc *sc,
				  unsigned int channel_set);
unsigned int cxm_tuner_selected_channel_set(struct cxm_softc *sc);
int cxm_tuner_select_frequency(struct cxm_softc *sc,
				enum cxm_tuner_freq_type type,
				unsigned long freq);
int cxm_tuner_select_channel(struct cxm_softc *sc, unsigned int channel);
int cxm_tuner_apply_afc(struct cxm_softc *sc);
int cxm_tuner_is_locked(struct cxm_softc *sc);
int cxm_tuner_wait_for_lock(struct cxm_softc *sc);
int cxm_tuner_status(struct cxm_softc *sc);

/*
 * Video decoder
 */
struct cxm_saa7115_setting {
	unsigned char	addr;
	unsigned int	nvalues;
	char		values[32];
};

struct cxm_saa7115_command {
	unsigned int nsettings;
	struct cxm_saa7115_setting settings[20];
};

struct cxm_saa7115_audio_clock {
	unsigned int sample_rate;
	unsigned int fps;
	const struct cxm_saa7115_command *clock;
};

struct cxm_saa7115_scaling {
	unsigned int width;
	unsigned int height;
	unsigned int fps;
	const struct cxm_saa7115_command *scaling;
};

int cxm_saa7115_init(struct cxm_softc *sc);
int cxm_saa7115_mute(struct cxm_softc *sc);
int cxm_saa7115_unmute(struct cxm_softc *sc);
int cxm_saa7115_select_source(struct cxm_softc *sc, enum cxm_source source);
int cxm_saa7115_configure(struct cxm_softc *sc,
			   unsigned int width, unsigned int height,
			   unsigned int fps, unsigned int audio_sample_rate);
enum cxm_source_format cxm_saa7115_detected_format(struct cxm_softc *sc);
int cxm_saa7115_detected_fps(struct cxm_softc *sc);
int cxm_saa7115_get_brightness(struct cxm_softc *sc);
int cxm_saa7115_set_brightness(struct cxm_softc *sc,
				unsigned char brightness);
int cxm_saa7115_get_chroma_saturation(struct cxm_softc *sc);
int cxm_saa7115_set_chroma_saturation(struct cxm_softc *sc,
				       unsigned char chroma_saturation);
int cxm_saa7115_get_contrast(struct cxm_softc *sc);
int cxm_saa7115_set_contrast(struct cxm_softc *sc, unsigned char contrast);
int cxm_saa7115_get_hue(struct cxm_softc *sc);
int cxm_saa7115_set_hue(struct cxm_softc *sc, unsigned char hue);
int cxm_saa7115_is_locked(struct cxm_softc *sc);
int cxm_saa7115_wait_for_lock(struct cxm_softc *sc);

#endif	/* !_CXM_H */

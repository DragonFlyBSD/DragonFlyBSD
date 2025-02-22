/*-
 * Copyright (c) 1998 - 2008 Søren Schmidt <sos@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/ata/ata-all.h,v 1.123 2007/04/08 19:18:51 sos Exp $
 */

#include <sys/param.h>
#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/nata.h>
#include <sys/objcache.h>
#include <sys/queue.h>
#include <sys/rman.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>

#include <machine/bus_dma.h>

/* ATA register defines */
#define ATA_DATA                        0       /* (RW) data */

#define ATA_FEATURE                     1       /* (W) feature */
#define         ATA_F_DMA               0x01    /* enable DMA */
#define         ATA_F_OVL               0x02    /* enable overlap */

#define ATA_COUNT                       2       /* (W) sector count */

#define ATA_SECTOR                      3       /* (RW) sector # */
#define ATA_CYL_LSB                     4       /* (RW) cylinder# LSB */
#define ATA_CYL_MSB                     5       /* (RW) cylinder# MSB */
#define ATA_DRIVE                       6       /* (W) Sector/Drive/Head */
#define         ATA_D_LBA               0x40    /* use LBA addressing */
#define         ATA_D_IBM               0xa0    /* 512 byte sectors, ECC */

#define ATA_COMMAND                     7       /* (W) command */

#define ATA_ERROR                       8       /* (R) error */
#define         ATA_E_ILI               0x01    /* illegal length */
#define         ATA_E_NM                0x02    /* no media */
#define         ATA_E_ABORT             0x04    /* command aborted */
#define         ATA_E_MCR               0x08    /* media change request */
#define         ATA_E_IDNF              0x10    /* ID not found */
#define         ATA_E_MC                0x20    /* media changed */
#define         ATA_E_UNC               0x40    /* uncorrectable data */
#define         ATA_E_ICRC              0x80    /* UDMA crc error */
#define		ATA_E_ATAPI_SENSE_MASK	0xf0	/* ATAPI sense key mask */

#define ATA_IREASON                     9       /* (R) interrupt reason */
#define         ATA_I_CMD               0x01    /* cmd (1) | data (0) */
#define         ATA_I_IN                0x02    /* read (1) | write (0) */
#define         ATA_I_RELEASE           0x04    /* released bus (1) */
#define         ATA_I_TAGMASK           0xf8    /* tag mask */

#define ATA_STATUS                      10      /* (R) status */
#define ATA_ALTSTAT                     11      /* (R) alternate status */
#define         ATA_S_ERROR             0x01    /* error */
#define         ATA_S_INDEX             0x02    /* index */
#define         ATA_S_CORR              0x04    /* data corrected */
#define         ATA_S_DRQ               0x08    /* data request */
#define         ATA_S_DSC               0x10    /* drive seek completed */
#define         ATA_S_SERVICE           0x10    /* drive needs service */
#define         ATA_S_DWF               0x20    /* drive write fault */
#define         ATA_S_DMA               0x20    /* DMA ready */
#define         ATA_S_READY             0x40    /* drive ready */
#define         ATA_S_BUSY              0x80    /* busy */

#define ATA_CONTROL                     12      /* (W) control */

#define ATA_CTLOFFSET                   0x206   /* control register offset */
#define ATA_PCCARD_CTLOFFSET            0x0e    /* do for PCCARD devices */
#define         ATA_A_IDS               0x02    /* disable interrupts */
#define         ATA_A_RESET             0x04    /* RESET controller */
#define         ATA_A_4BIT              0x08    /* 4 head bits */
#define         ATA_A_HOB               0x80    /* High Order Byte enable */

/* SATA register defines */
#define ATA_SSTATUS                     13
#define         ATA_SS_DET_MASK         0x0000000f
#define         ATA_SS_DET_NO_DEVICE    0x00000000
#define         ATA_SS_DET_DEV_PRESENT  0x00000001
#define         ATA_SS_DET_PHY_ONLINE   0x00000003
#define         ATA_SS_DET_PHY_OFFLINE  0x00000004

#define         ATA_SS_SPD_MASK         0x000000f0
#define         ATA_SS_SPD_NO_SPEED     0x00000000
#define         ATA_SS_SPD_GEN1         0x00000010
#define         ATA_SS_SPD_GEN2         0x00000020

#define         ATA_SS_IPM_MASK         0x00000f00
#define         ATA_SS_IPM_NO_DEVICE    0x00000000
#define         ATA_SS_IPM_ACTIVE       0x00000100
#define         ATA_SS_IPM_PARTIAL      0x00000200
#define         ATA_SS_IPM_SLUMBER      0x00000600

#define         ATA_SS_CONWELL_MASK \
		    (ATA_SS_DET_MASK|ATA_SS_SPD_MASK|ATA_SS_IPM_MASK)
#define         ATA_SS_CONWELL_GEN1 \
		    (ATA_SS_DET_PHY_ONLINE|ATA_SS_SPD_GEN1|ATA_SS_IPM_ACTIVE)
#define         ATA_SS_CONWELL_GEN2 \
		    (ATA_SS_DET_PHY_ONLINE|ATA_SS_SPD_GEN2|ATA_SS_IPM_ACTIVE)

#define ATA_SERROR                      14
#define         ATA_SE_DATA_CORRECTED   0x00000001
#define         ATA_SE_COMM_CORRECTED   0x00000002
#define         ATA_SE_DATA_ERR         0x00000100
#define         ATA_SE_COMM_ERR         0x00000200
#define         ATA_SE_PROT_ERR         0x00000400
#define         ATA_SE_HOST_ERR         0x00000800
#define         ATA_SE_PHY_CHANGED      0x00010000
#define         ATA_SE_PHY_IERROR       0x00020000
#define         ATA_SE_COMM_WAKE        0x00040000
#define         ATA_SE_DECODE_ERR       0x00080000
#define         ATA_SE_PARITY_ERR       0x00100000
#define         ATA_SE_CRC_ERR          0x00200000
#define         ATA_SE_HANDSHAKE_ERR    0x00400000
#define         ATA_SE_LINKSEQ_ERR      0x00800000
#define         ATA_SE_TRANSPORT_ERR    0x01000000
#define         ATA_SE_UNKNOWN_FIS      0x02000000

#define ATA_SCONTROL                    15
#define         ATA_SC_DET_MASK         0x0000000f
#define         ATA_SC_DET_IDLE         0x00000000
#define         ATA_SC_DET_RESET        0x00000001
#define         ATA_SC_DET_DISABLE      0x00000004

#define         ATA_SC_SPD_MASK         0x000000f0
#define         ATA_SC_SPD_NO_SPEED     0x00000000
#define         ATA_SC_SPD_SPEED_GEN1   0x00000010
#define         ATA_SC_SPD_SPEED_GEN2   0x00000020

#define         ATA_SC_IPM_MASK         0x00000f00
#define         ATA_SC_IPM_NONE         0x00000000
#define         ATA_SC_IPM_DIS_PARTIAL  0x00000100
#define         ATA_SC_IPM_DIS_SLUMBER  0x00000200

#define ATA_SACTIVE                     16

/* SATA AHCI v1.0 register defines */
#define ATA_AHCI_CAP                    0x00
#define         ATA_AHCI_NPMASK         0x1f
#define		ATA_AHCI_CAP_CLO	0x01000000
#define		ATA_AHCI_CAP_64BIT	0x80000000

#define ATA_AHCI_GHC                    0x04
#define         ATA_AHCI_GHC_AE         0x80000000
#define         ATA_AHCI_GHC_IE         0x00000002
#define         ATA_AHCI_GHC_HR         0x00000001

#define ATA_AHCI_IS                     0x08
#define ATA_AHCI_PI                     0x0c
#define ATA_AHCI_VS                     0x10

#define ATA_AHCI_OFFSET                 0x80

#define ATA_AHCI_P_CLB                  0x100
#define ATA_AHCI_P_CLBU                 0x104
#define ATA_AHCI_P_FB                   0x108
#define ATA_AHCI_P_FBU                  0x10c
#define ATA_AHCI_P_IS                   0x110
#define ATA_AHCI_P_IE                   0x114
#define         ATA_AHCI_P_IX_DHR       0x00000001
#define         ATA_AHCI_P_IX_PS        0x00000002
#define         ATA_AHCI_P_IX_DS        0x00000004
#define         ATA_AHCI_P_IX_SDB       0x00000008
#define         ATA_AHCI_P_IX_UF        0x00000010
#define         ATA_AHCI_P_IX_DP        0x00000020
#define         ATA_AHCI_P_IX_PC        0x00000040
#define         ATA_AHCI_P_IX_DI        0x00000080

#define         ATA_AHCI_P_IX_PRC       0x00400000
#define         ATA_AHCI_P_IX_IPM       0x00800000
#define         ATA_AHCI_P_IX_OF        0x01000000
#define         ATA_AHCI_P_IX_INF       0x04000000
#define         ATA_AHCI_P_IX_IF        0x08000000
#define         ATA_AHCI_P_IX_HBD       0x10000000
#define         ATA_AHCI_P_IX_HBF       0x20000000
#define         ATA_AHCI_P_IX_TFE       0x40000000
#define         ATA_AHCI_P_IX_CPD       0x80000000

#define ATA_AHCI_P_CMD                  0x118
#define         ATA_AHCI_P_CMD_ST       0x00000001
#define         ATA_AHCI_P_CMD_SUD      0x00000002
#define         ATA_AHCI_P_CMD_POD      0x00000004
#define         ATA_AHCI_P_CMD_CLO      0x00000008
#define         ATA_AHCI_P_CMD_FRE      0x00000010
#define         ATA_AHCI_P_CMD_CCS_MASK 0x00001f00
#define         ATA_AHCI_P_CMD_ISS      0x00002000
#define         ATA_AHCI_P_CMD_FR       0x00004000
#define         ATA_AHCI_P_CMD_CR       0x00008000
#define         ATA_AHCI_P_CMD_CPS      0x00010000
#define         ATA_AHCI_P_CMD_PMA      0x00020000
#define         ATA_AHCI_P_CMD_HPCP     0x00040000
#define         ATA_AHCI_P_CMD_ISP      0x00080000
#define         ATA_AHCI_P_CMD_CPD      0x00100000
#define         ATA_AHCI_P_CMD_ATAPI    0x01000000
#define         ATA_AHCI_P_CMD_DLAE     0x02000000
#define         ATA_AHCI_P_CMD_ALPE     0x04000000
#define         ATA_AHCI_P_CMD_ASP      0x08000000
#define         ATA_AHCI_P_CMD_ACTIVE   0x10000000

#define ATA_AHCI_P_TFD                  0x120
#define ATA_AHCI_P_SIG                  0x124
#define ATA_AHCI_P_SSTS                 0x128
#define ATA_AHCI_P_SCTL                 0x12c
#define ATA_AHCI_P_SERR                 0x130
#define ATA_AHCI_P_SACT                 0x134
#define ATA_AHCI_P_CI                   0x138

#define ATA_AHCI_CL_SIZE                32
#define ATA_AHCI_CL_OFFSET              0
#define ATA_AHCI_FB_OFFSET              1024
#define ATA_AHCI_CT_OFFSET              1024+256
#define ATA_AHCI_CT_SG_OFFSET           128
#define ATA_AHCI_CT_SIZE                256

struct ata_ahci_dma_prd {
    u_int64_t                   dba;
    u_int32_t                   reserved;
    u_int32_t                   dbc;            /* 0 based */
#define ATA_AHCI_PRD_MASK       0x003fffff      /* max 4MB */
#define ATA_AHCI_PRD_IPC        (1<<31)
} __packed;

struct ata_ahci_cmd_tab {
    u_int8_t                    cfis[64];
    u_int8_t                    acmd[32];
    u_int8_t                    reserved[32];
#define ATA_AHCI_DMA_ENTRIES            64
    struct ata_ahci_dma_prd     prd_tab[ATA_AHCI_DMA_ENTRIES];
} __packed;

struct ata_ahci_cmd_list {
    u_int16_t                   cmd_flags;
#define ATA_AHCI_CMD_ATAPI		0x0020
#define ATA_AHCI_CMD_WRITE		0x0040
#define ATA_AHCI_CMD_PREFETCH		0x0080

    u_int16_t                   prd_length;     /* PRD entries */
    u_int32_t                   bytecount;
    u_int64_t                   cmd_table_phys; /* 128byte aligned */
} __packed;


/* DMA register defines */
#define ATA_DMA_ENTRIES                 256
#define ATA_DMA_EOT                     0x80000000

#define ATA_BMCMD_PORT                  17
#define         ATA_BMCMD_START_STOP    0x01
#define         ATA_BMCMD_WRITE_READ    0x08

#define ATA_BMDEVSPEC_0                 18
#define ATA_BMSTAT_PORT                 19
#define         ATA_BMSTAT_ACTIVE       0x01
#define         ATA_BMSTAT_ERROR        0x02
#define         ATA_BMSTAT_INTERRUPT    0x04
#define         ATA_BMSTAT_MASK         0x07
#define         ATA_BMSTAT_DMA_MASTER   0x20
#define         ATA_BMSTAT_DMA_SLAVE    0x40
#define         ATA_BMSTAT_DMA_SIMPLEX  0x80

#define ATA_BMDEVSPEC_1                 20
#define ATA_BMDTP_PORT                  21

#define ATA_IDX_ADDR                    22
#define ATA_IDX_DATA                    23
#define ATA_MAX_RES                     24

/* misc defines */
#define ATA_PRIMARY                     0x1f0
#define ATA_SECONDARY                   0x170
#define ATA_IOSIZE                      0x08
#define ATA_CTLIOSIZE                   0x01
#define ATA_BMIOSIZE                    0x08
#define ATA_IOADDR_RID                  0
#define ATA_CTLADDR_RID                 1
#define ATA_BMADDR_RID                  0x20
#define ATA_IRQ_RID                     0
#define ATA_DEV(unit)                   ((unit > 0) ? 0x10 : 0)
#define ATA_CFA_MAGIC1                  0x844A
#define ATA_CFA_MAGIC2                  0x848A
#define ATA_CFA_MAGIC3                  0x8400
#define ATAPI_MAGIC_LSB                 0x14
#define ATAPI_MAGIC_MSB                 0xeb
#define ATAPI_P_READ                    (ATA_S_DRQ | ATA_I_IN)
#define ATAPI_P_WRITE                   (ATA_S_DRQ)
#define ATAPI_P_CMDOUT                  (ATA_S_DRQ | ATA_I_CMD)
#define ATAPI_P_DONEDRQ                 (ATA_S_DRQ | ATA_I_CMD | ATA_I_IN)
#define ATAPI_P_DONE                    (ATA_I_CMD | ATA_I_IN)
#define ATAPI_P_ABORT                   0
#define ATA_INTR_FLAGS                  (INTR_NOPOLL)
#define ATA_OP_CONTINUES                0
#define ATA_OP_FINISHED                 1
#define ATA_MAX_28BIT_LBA               268435455UL

/* Dragonfly: Default request timeout increased from 5 to 10 */
#define ATA_DEFAULT_TIMEOUT		10

/* structure used for composite atomic operations */
#define MAX_COMPOSITES          32              /* u_int32_t bits */
struct ata_composite {
    struct lock		lock;                   /* control lock */
    u_int32_t           rd_needed;              /* needed read subdisks */
    u_int32_t           rd_done;                /* done read subdisks */
    u_int32_t           wr_needed;              /* needed write subdisks */
    u_int32_t           wr_depend;              /* write depends on subdisks */
    u_int32_t           wr_done;                /* done write subdisks */
    struct ata_request  *request[MAX_COMPOSITES];
    u_int32_t           residual;               /* bytes still to transfer */
    caddr_t             data_1;     
    caddr_t             data_2;     
};

/* structure used to queue an ATA/ATAPI request */
struct ata_request {
    device_t                    dev;            /* device handle */
    device_t                    parent;         /* channel handle */
    union {
	struct {
	    u_int8_t            command;        /* command reg */
	    u_int16_t           feature;        /* feature reg */
	    u_int16_t           count;          /* count reg */
	    u_int64_t           lba;            /* lba reg */
	} ata;
	struct {
	    u_int8_t            ccb[16];        /* ATAPI command block */
	    struct atapi_sense  sense;          /* ATAPI request sense data */
	    u_int8_t            saved_cmd;      /* ATAPI saved command */
	} atapi;
    } u;
    u_int32_t                   bytecount;      /* bytes to transfer */
    u_int32_t                   transfersize;   /* bytes pr transfer */
    caddr_t                     data;           /* pointer to data buf */
    int                         flags;
#define         ATA_R_CONTROL           0x00000001
#define         ATA_R_READ              0x00000002
#define         ATA_R_WRITE             0x00000004
#define         ATA_R_ATAPI             0x00000008
#define         ATA_R_DMA               0x00000010
#define         ATA_R_QUIET             0x00000020
#define         ATA_R_TIMEOUT           0x00000040
#define		ATA_R_COMPLETED		0x00000080

#define         ATA_R_ORDERED           0x00000100
#define         ATA_R_AT_HEAD           0x00000200
#define         ATA_R_REQUEUE           0x00000400
#define         ATA_R_THREAD            0x00000800
#define         ATA_R_DIRECT            0x00001000

#define		ATA_R_HWCMDQUEUED	0x00010000

#define         ATA_R_DEBUG             0x10000000
#define         ATA_R_DANGER1           0x20000000
#define         ATA_R_DANGER2           0x40000000

    u_int8_t                    status;         /* ATA status */
    u_int8_t                    error;          /* ATA error */
    u_int8_t                    dmastat;        /* DMA status */
    u_int32_t                   donecount;      /* bytes transferred */
    int                         result;         /* result error code */
    void                        (*callback)(struct ata_request *request);
    struct lock			done;           /* request done sema */
    int                         retries;        /* retry count */
    int                         timeout;        /* timeout for this cmd */
    int				unused01;
    struct callout              callout;        /* callout management */
    struct task                 task;           /* task management */
    struct bio                  *bio;           /* bio for this request */
    int                         this;           /* this request ID */
    struct ata_composite        *composite;     /* for composite atomic ops */
    void                        *driver;        /* driver specific */
    TAILQ_ENTRY(ata_request)    chain;          /* list management */
};

/* define this for debugging request processing */
#if 0
#define ATA_DEBUG_RQ(request, string) \
    { \
    if (request->flags & ATA_R_DEBUG) \
	device_printf(request->dev, "req=%p %s " string "\n", \
		      request, ata_cmd2str(request)); \
    }
#else
#define ATA_DEBUG_RQ(request, string)
#endif


/* structure describing an ATA/ATAPI device */
struct ata_device {
    device_t                    dev;            /* device handle */
    int                         unit;           /* physical unit */
#define         ATA_MASTER              0x00
#define         ATA_SLAVE               0x01
#define         ATA_PM                  0x0f

    struct ata_params           param;          /* ata param structure */
    int                         mode;           /* current transfermode */
    u_int32_t                   max_iosize;     /* max IO size */
    int                         spindown;       /* idle spindown timeout */
    struct callout              spindown_timer;
    int                         spindown_state;
    int                         flags;
#define         ATA_D_USE_CHS           0x0001
#define         ATA_D_MEDIA_CHANGED     0x0002
#define         ATA_D_ENC_PRESENT       0x0004
#define         ATA_D_48BIT_ACTIVE      0x0008
    int				opencount;	/* when tracking needed */
};

/* structure for holding DMA Physical Region Descriptors (PRD) entries */
struct ata_dma_prdentry {
    u_int32_t addr;
    u_int32_t count;
};  

/* structure used by the setprd function */
struct ata_dmasetprd_args {
    void *dmatab;
    int nsegs;
    int error;
};

/* structure holding DMA related information */
struct ata_dma {
    bus_dma_tag_t               dmatag;         /* parent DMA tag */
    bus_dma_tag_t               sg_tag;         /* SG list DMA tag */
    bus_dmamap_t                sg_map;         /* SG list DMA map */
    void                        *sg;            /* DMA transfer table */
    bus_addr_t                  sg_bus;         /* bus address of dmatab */
    bus_dma_tag_t               data_tag;       /* data DMA tag */
    bus_dmamap_t                data_map;       /* data DMA map */
    bus_dma_tag_t               work_tag;       /* workspace DMA tag */
    bus_dmamap_t                work_map;       /* workspace DMA map */
    u_int8_t                    *work;          /* workspace */
    bus_addr_t                  work_bus;       /* bus address of dmatab */

    u_int32_t                   alignment;      /* DMA SG list alignment */
    u_int32_t                   boundary;       /* DMA SG list boundary */
    u_int32_t                   segsize;        /* DMA SG list segment size */
    u_int32_t                   max_iosize;     /* DMA data max IO size */
    u_int32_t                   cur_iosize;     /* DMA data current IO size */
    u_int64_t                   max_address;    /* highest DMA'able address */
    int                         flags;
#define ATA_DMA_READ                    0x01    /* transaction is a read */
#define ATA_DMA_LOADED                  0x02    /* DMA tables etc loaded */
#define ATA_DMA_ACTIVE                  0x04    /* DMA transfer in progress */

    void (*alloc)(device_t dev);
    void (*free)(device_t dev);
    void (*setprd)(void *xsc, bus_dma_segment_t *segs, int nsegs, int error);
    int (*load)(device_t dev, caddr_t data, int32_t count, int dir, void *addr, int *nsegs);
    int (*unload)(device_t dev);
    int (*start)(device_t dev);
    int (*stop)(device_t dev);
    void (*reset)(device_t dev);
};

/* structure holding lowlevel functions */
struct ata_lowlevel {
    u_int32_t (*softreset)(device_t dev, int pmport);
    int (*status)(device_t dev);
    int (*begin_transaction)(struct ata_request *request);
    int (*end_transaction)(struct ata_request *request);
    int (*command)(struct ata_request *request);
    void (*tf_read)(struct ata_request *request);
    void (*tf_write)(struct ata_request *request);
};

/* structure holding resources for an ATA channel */
struct ata_resource {
    struct resource             *res;
    int                         offset;
};

/* structure describing an ATA channel */
struct ata_channel {
    device_t                    dev;            /* device handle */
    int                         unit;           /* physical channel */
    struct ata_resource         r_io[ATA_MAX_RES];/* I/O resources */
    struct resource             *r_irq;         /* interrupt of this channel */
    void                        *ih;            /* interrupt handle */
    struct ata_lowlevel         hw;             /* lowlevel HW functions */
    struct ata_dma              *dma;           /* DMA data / functions */
    int                         flags;          /* channel flags */
#define         ATA_NO_SLAVE            0x01
#define         ATA_USE_16BIT           0x02
#define         ATA_ATAPI_DMA_RO        0x04
#define         ATA_NO_48BIT_DMA        0x08
#define         ATA_ALWAYS_DMASTAT      0x10

    int                         devices;        /* what is present */
#define         ATA_ATA_MASTER          0x00000001
#define         ATA_ATA_SLAVE           0x00000002
#define         ATA_PORTMULTIPLIER      0x00008000
#define         ATA_ATAPI_MASTER        0x00010000
#define         ATA_ATAPI_SLAVE         0x00020000

    struct lock			state_mtx;      /* state lock */
    int                         state;          /* ATA channel state */
#define         ATA_IDLE                0x0000
#define         ATA_ACTIVE              0x0001
#define         ATA_STALL_QUEUE         0x0002

    struct lock			queue_mtx;      /* queue lock */
    TAILQ_HEAD(, ata_request)   ata_queue;      /* head of ATA queue */
    int				reorder;	/* limit sort reordering */
    struct ata_request		*transition;
    struct ata_request          *running;       /* currently running request */
};

/* disk bay/enclosure related */
#define         ATA_LED_OFF             0x00
#define         ATA_LED_RED             0x01
#define         ATA_LED_GREEN           0x02
#define         ATA_LED_ORANGE          0x03
#define         ATA_LED_MASK            0x03

/* externs */
extern int (*ata_raid_ioctl_func)(u_long cmd, caddr_t data);
extern devclass_t ata_devclass;
extern int ata_wc;

/* public prototypes */
/* ata-all.c: */
int ata_probe(device_t dev);
int ata_attach(device_t dev);
int ata_detach(device_t dev);
int ata_reinit(device_t dev);
int ata_suspend(device_t dev);
int ata_resume(device_t dev);
int ata_interrupt(void *data);
int ata_device_ioctl(device_t dev, u_long cmd, caddr_t data);
int ata_identify(device_t dev);
void ata_default_registers(device_t dev);
void ata_modify_if_48bit(struct ata_request *request);
void ata_udelay(int interval);
const char *ata_unit2str(struct ata_device *atadev);
const char *ata_mode2str(int mode);
void ata_print_cable(device_t dev, u_int8_t *who);
int ata_atapi(device_t dev);
int ata_pmode(struct ata_params *ap);
int ata_wmode(struct ata_params *ap);
int ata_umode(struct ata_params *ap);
int ata_limit_mode(device_t dev, int mode, int maxmode);

/* ata-queue.c: */
int ata_controlcmd(device_t dev, u_int8_t command, u_int16_t feature, u_int64_t lba, u_int16_t count);
int ata_atapicmd(device_t dev, u_int8_t *ccb, caddr_t data, int count, int flags, int timeout);
void ata_drop_requests(device_t dev);
void ata_queue_init(struct ata_channel *ch);
void ata_queue_request(struct ata_request *request);
void ata_start(device_t dev);
void ata_finish(struct ata_request *request);
void ata_timeout(struct ata_request *);
void ata_catch_inflight(device_t dev);
void ata_fail_requests(device_t dev);
const char *ata_cmd2str(struct ata_request *request);

/* ata-lowlevel.c: */
void ata_generic_hw(device_t dev);
int ata_begin_transaction(struct ata_request *);
int ata_end_transaction(struct ata_request *);
void ata_generic_reset(device_t dev);
int ata_generic_command(struct ata_request *request);

/* ata-dma.c: */
void ata_dmainit(device_t);

/* ata-sata.c: */
void ata_sata_phy_check_events(device_t dev);
void ata_sata_phy_event(void *context, int dummy);
int ata_sata_phy_reset(device_t dev);
void ata_sata_setmode(device_t dev, int mode);
int ata_request2fis_h2d(struct ata_request *request, u_int8_t *fis);

/* macros for alloc/free of struct ata_request */
extern struct objcache *ata_request_cache;
#define ata_alloc_request() objcache_get(ata_request_cache, M_WAITOK)
/* zero the object so objects in the cache are guaranteed to be zero'ed */
#define ata_free_request(request) { \
	if (!(request->flags & ATA_R_DANGER2)) { \
	    callout_terminate(&request->callout); \
	    bzero(request, sizeof(struct ata_request)); \
	    objcache_put(ata_request_cache, request); \
	} \
}
/* macros for alloc/free of struct ata_composite */
extern struct objcache *ata_composite_cache;
#define ata_alloc_composite() objcache_get(ata_composite_cache, M_WAITOK)
/* zero the object so objects in the cache are guaranteed to be zero'ed */
#define ata_free_composite(composite) { \
	bzero(composite, sizeof(struct ata_composite)); \
	objcache_put(ata_composite_cache, composite); \
}

MALLOC_DECLARE(M_ATA);

/* misc newbus defines */
#define GRANDPARENT(dev)        device_get_parent(device_get_parent(dev))

/* macros to hide busspace uglyness */
#define ATA_INB(res, offset) \
	bus_space_read_1(rman_get_bustag((res)), \
			 rman_get_bushandle((res)), (offset))

#define ATA_INW(res, offset) \
	bus_space_read_2(rman_get_bustag((res)), \
			 rman_get_bushandle((res)), (offset))
#define ATA_INL(res, offset) \
	bus_space_read_4(rman_get_bustag((res)), \
			 rman_get_bushandle((res)), (offset))
#define ATA_INSW(res, offset, addr, count) \
	bus_space_read_multi_2(rman_get_bustag((res)), \
			       rman_get_bushandle((res)), \
			       (offset), (addr), (count))
#define ATA_INSW_STRM(res, offset, addr, count) \
	bus_space_read_multi_stream_2(rman_get_bustag((res)), \
				      rman_get_bushandle((res)), \
				      (offset), (addr), (count))
#define ATA_INSL(res, offset, addr, count) \
	bus_space_read_multi_4(rman_get_bustag((res)), \
			       rman_get_bushandle((res)), \
			       (offset), (addr), (count))
#define ATA_INSL_STRM(res, offset, addr, count) \
	bus_space_read_multi_stream_4(rman_get_bustag((res)), \
				      rman_get_bushandle((res)), \
				      (offset), (addr), (count))
#define ATA_OUTB(res, offset, value) \
	bus_space_write_1(rman_get_bustag((res)), \
			  rman_get_bushandle((res)), (offset), (value))
#define ATA_OUTW(res, offset, value) \
	bus_space_write_2(rman_get_bustag((res)), \
			  rman_get_bushandle((res)), (offset), (value))
#define ATA_OUTL(res, offset, value) \
	bus_space_write_4(rman_get_bustag((res)), \
			  rman_get_bushandle((res)), (offset), (value))
#define ATA_OUTSW(res, offset, addr, count) \
	bus_space_write_multi_2(rman_get_bustag((res)), \
				rman_get_bushandle((res)), \
				(offset), (addr), (count))
#define ATA_OUTSW_STRM(res, offset, addr, count) \
	bus_space_write_multi_stream_2(rman_get_bustag((res)), \
				       rman_get_bushandle((res)), \
				       (offset), (addr), (count))
#define ATA_OUTSL(res, offset, addr, count) \
	bus_space_write_multi_4(rman_get_bustag((res)), \
				rman_get_bushandle((res)), \
				(offset), (addr), (count))
#define ATA_OUTSL_STRM(res, offset, addr, count) \
	bus_space_write_multi_stream_4(rman_get_bustag((res)), \
				       rman_get_bushandle((res)), \
				       (offset), (addr), (count))

#define ATA_IDX_INB(ch, idx) \
	ATA_INB(ch->r_io[idx].res, ch->r_io[idx].offset)

#define ATA_IDX_INW(ch, idx) \
	ATA_INW(ch->r_io[idx].res, ch->r_io[idx].offset)

#define ATA_IDX_INL(ch, idx) \
	ATA_INL(ch->r_io[idx].res, ch->r_io[idx].offset)

#define ATA_IDX_INSW(ch, idx, addr, count) \
	ATA_INSW(ch->r_io[idx].res, ch->r_io[idx].offset, addr, count)

#define ATA_IDX_INSW_STRM(ch, idx, addr, count) \
	ATA_INSW_STRM(ch->r_io[idx].res, ch->r_io[idx].offset, addr, count)

#define ATA_IDX_INSL(ch, idx, addr, count) \
	ATA_INSL(ch->r_io[idx].res, ch->r_io[idx].offset, addr, count)

#define ATA_IDX_INSL_STRM(ch, idx, addr, count) \
	ATA_INSL_STRM(ch->r_io[idx].res, ch->r_io[idx].offset, addr, count)

#define ATA_IDX_OUTB(ch, idx, value) \
	ATA_OUTB(ch->r_io[idx].res, ch->r_io[idx].offset, value)

#define ATA_IDX_OUTW(ch, idx, value) \
	ATA_OUTW(ch->r_io[idx].res, ch->r_io[idx].offset, value)

#define ATA_IDX_OUTL(ch, idx, value) \
	ATA_OUTL(ch->r_io[idx].res, ch->r_io[idx].offset, value)

#define ATA_IDX_OUTSW(ch, idx, addr, count) \
	ATA_OUTSW(ch->r_io[idx].res, ch->r_io[idx].offset, addr, count)

#define ATA_IDX_OUTSW_STRM(ch, idx, addr, count) \
	ATA_OUTSW_STRM(ch->r_io[idx].res, ch->r_io[idx].offset, addr, count)

#define ATA_IDX_OUTSL(ch, idx, addr, count) \
	ATA_OUTSL(ch->r_io[idx].res, ch->r_io[idx].offset, addr, count)

#define ATA_IDX_OUTSL_STRM(ch, idx, addr, count) \
	ATA_OUTSL_STRM(ch->r_io[idx].res, ch->r_io[idx].offset, addr, count)

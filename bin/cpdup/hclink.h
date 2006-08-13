/*
 * HCLINK.H
 *
 * $DragonFly: src/bin/cpdup/hclink.h,v 1.1 2006/08/13 20:51:40 dillon Exp $
 */

#ifndef _HCLINK_H_
#define _HCLINK_H_

struct HCHostDesc {
    struct HCHostDesc *next;
    int desc;
    int type;
    void *data;
};

struct HostConf {
    char *host;		/* [user@]host */
    int fdin;		/* pipe */
    int fdout;		/* pipe */
    int error;		/* permanent failure code */
    pid_t pid;
    int windex;		/* output buffer index */
    struct HCHostDesc *hostdescs;
    char rbuf[65536];	/* output buffer */
    char wbuf[65536];	/* output buffer */
};

struct HCHead {
    int32_t magic;		/* magic number / byte ordering */
    int32_t bytes;		/* size of packet */
    int16_t cmd;		/* command code */
    int16_t id;			/* transaction id */
    int32_t error;		/* error code (response) */
};

#define HCMAGIC		0x48435052	/* compatible byte ordering */
#define HCMAGIC_REV	0x52504348	/* reverse byte ordering */
#define HCC_ALIGN(bytes)	(((bytes) + 7) & ~7)

struct HCLeaf {
    int16_t leafid;
    int16_t reserved;		/* reserved must be 0 */
    int32_t bytes;
};

#define HCF_REPLY	0x8000		/* reply */

#define LCF_TYPEMASK	0x0F00
#define LCF_INT32	0x0100		/* 4 byte integer */
#define LCF_INT64	0x0200		/* 8 byte integer */
#define LCF_STRING	0x0300		/* string, must be 0-terminated */
#define LCF_BINARY	0x0F00		/* binary data */

#define LCF_NESTED	0x8000

struct HCDesc {
    int16_t cmd;
    int (*func)(struct HostConf *, struct HCHead *);
};

/*
 * Item extraction macros
 */
#define HCC_STRING(item)	((const char *)((item) + 1))
#define HCC_INT32(item)		(*(int32_t *)((item) + 1))
#define HCC_INT64(item)		(*(int64_t *)((item) + 1))
#define HCC_BINARYDATA(item)	((void *)((item) + 1))

/*
 * Prototypes
 */
int hcc_connect(struct HostConf *hc);
int hcc_slave(int fdin, int fdout, struct HCDesc *descs, int count);

void hcc_start_command(struct HostConf *hc, int16_t cmd);
struct HCHead *hcc_finish_command(struct HostConf *hc);
void hcc_leaf_string(struct HostConf *hc, int16_t leafid, const char *str);
void hcc_leaf_data(struct HostConf *hc, int16_t leafid, const void *ptr, int bytes);
void hcc_leaf_int32(struct HostConf *hc, int16_t leafid, int32_t value);
void hcc_leaf_int64(struct HostConf *hc, int16_t leafid, int64_t value);

int hcc_alloc_descriptor(struct HostConf *hc, void *ptr, int type);
void *hcc_get_descriptor(struct HostConf *hc, int desc, int type);
void hcc_set_descriptor(struct HostConf *hc, int desc, void *ptr, int type);

struct HCLeaf *hcc_firstitem(struct HCHead *head);
struct HCLeaf *hcc_nextitem(struct HCHead *head, struct HCLeaf *item);

void hcc_debug_dump(struct HCHead *head);

#endif


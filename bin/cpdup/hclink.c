/*
 * HCLINK.C
 *
 * This module implements a simple remote control protocol
 *
 * $DragonFly: src/bin/cpdup/hclink.c,v 1.2 2006/08/14 02:41:10 dillon Exp $
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <assert.h>
#include <errno.h>

#include "hclink.h"
#include "hcproto.h"

static struct HCHead *hcc_read_command(struct HostConf *hc);

int
hcc_connect(struct HostConf *hc)
{
    int fdin[2];
    int fdout[2];

    if (hc == NULL || hc->host == NULL)
	return(0);

    if (pipe(fdin) < 0)
	return(-1);
    if (pipe(fdout) < 0) {
	close(fdin[0]);
	close(fdin[1]);
	return(-1);
    }
    if ((hc->pid = fork()) == 0) {
	/*
	 * Child process
	 */
	dup2(fdin[1], 1);
	close(fdin[0]);
	close(fdin[1]);
	dup2(fdout[0], 0);
	close(fdout[0]);
	close(fdout[1]);
	execl("/usr/bin/ssh", "ssh", "-T", hc->host, "cpdup", "-S", NULL);
	_exit(1);
    } else if (hc->pid < 0) {
	return(-1);
    } else {
	/*
	 * Parent process.  Do the initial handshake to make sure we are
	 * actually talking to a cpdup slave.
	 */
	close(fdin[1]);
	hc->fdin = fdin[0];
	close(fdout[0]);
	hc->fdout = fdout[1];
	return(0);
    }
}

static int
rc_badop(struct HostConf *hc __unused, struct HCHead *head)
{
    head->error = EOPNOTSUPP;
    return(0);
}

int
hcc_slave(int fdin, int fdout, struct HCDesc *descs, int count)
{
    struct HostConf hcslave;
    struct HCHead *head;
    struct HCHead *whead;
    int (*dispatch[256])(struct HostConf *, struct HCHead *);
    int aligned_bytes;
    int i;
    int r;

    bzero(&hcslave, sizeof(hcslave));
    for (i = 0; i < count; ++i) {
	struct HCDesc *desc = &descs[i];
	assert(desc->cmd >= 0 && desc->cmd < 256);
	dispatch[desc->cmd] = desc->func;
    }
    for (i = 0; i < 256; ++i) {
	if (dispatch[i] == NULL)
	    dispatch[i] = rc_badop;
    }
    hcslave.fdin = fdin;
    hcslave.fdout = fdout;

    /*
     * Process commands on fdin and write out results on fdout
     */
    for (;;) {
	/*
	 * Get the command
	 */
	head = hcc_read_command(&hcslave);
	if (head == NULL)
	    break;

	/*
	 * Start the reply and dispatch, then process the return code.
	 */
	head->error = 0;
	hcc_start_command(&hcslave, head->cmd | HCF_REPLY);
	r = dispatch[head->cmd & 255](&hcslave, head);
	switch(r) {
	case -2:
		head->error = EINVAL;
		break;
	case -1:
		head->error = errno;
		break;
	case 0:
		break;
	default:
		assert(0);
		break;
	}

	/*
	 * Write out the reply
	 */
	whead = (void *)hcslave.wbuf;
	whead->bytes = hcslave.windex;
	whead->error = head->error;
	aligned_bytes = HCC_ALIGN(hcslave.windex);
#ifdef DEBUG
	hcc_debug_dump(whead);
#endif
	if (write(hcslave.fdout, whead, aligned_bytes) != aligned_bytes)
	    break;
    }
    return(0);
}

/*
 * This reads a command from fdin, fixes up the byte ordering, and returns
 * a pointer to HCHead.
 */
static
struct HCHead *
hcc_read_command(struct HostConf *hc)
{
    struct HCHead *head;
    int aligned_bytes;
    int n;
    int r;

    n = 0;
    while (n < (int)sizeof(struct HCHead)) {
	r = read(hc->fdin, hc->rbuf + n, sizeof(struct HCHead) - n);
	if (r <= 0)
	    return(NULL);
	n += r;
    }
    head = (void *)hc->rbuf;
    assert(head->bytes >= (int)sizeof(*head) && head->bytes < 65536);
    assert(head->magic == HCMAGIC);
    aligned_bytes = HCC_ALIGN(head->bytes);
    while (n < aligned_bytes) {
	r = read(hc->fdin, hc->rbuf + n, aligned_bytes - n);
	if (r <= 0)
	    return(NULL);
	n += r;
    }
#ifdef DEBUG
    hcc_debug_dump(head);
#endif
    return(head);
}

/*
 * Initialize for a new command
 */
void
hcc_start_command(struct HostConf *hc, int16_t cmd)
{
    struct HCHead *whead = (void *)hc->wbuf;

    whead->magic = HCMAGIC;
    whead->bytes = 0;
    whead->cmd = cmd;
    whead->id = 0;
    whead->error = 0;
    hc->windex = sizeof(*whead);
}

/*
 * Finish constructing a command, transmit it, and await the reply.
 * Return the HCHead of the reply.
 */
struct HCHead *
hcc_finish_command(struct HostConf *hc)
{
    struct HCHead *whead;
    struct HCHead *rhead;
    int aligned_bytes;

    whead = (void *)hc->wbuf;
    whead->bytes = hc->windex;
    aligned_bytes = HCC_ALIGN(hc->windex);
    if (write(hc->fdout, whead, aligned_bytes) != aligned_bytes) {
#ifdef __error
	*__error = EIO;
#else
	errno = EIO;
#endif
	if (whead->cmd < 0x0010)
		return(NULL);
	fprintf(stderr, "cpdup lost connection to %s\n", hc->host);
	exit(1);
    }
    if ((rhead = hcc_read_command(hc)) == NULL) {
#ifdef __error
	*__error = EIO;
#else
	errno = EIO;
#endif
	if (whead->cmd < 0x0010)
		return(NULL);
	fprintf(stderr, "cpdup lost connection to %s\n", hc->host);
	exit(1);
    }
    if (rhead->error) {
#ifdef __error
	*__error = rhead->error;
#else
	errno = rhead->error;
#endif
    }
    return (rhead);
}

void
hcc_leaf_string(struct HostConf *hc, int16_t leafid, const char *str)
{
    struct HCLeaf *item;
    int bytes = strlen(str) + 1;

    item = (void *)(hc->wbuf + hc->windex);
    assert(hc->windex + sizeof(*item) + bytes < 65536);
    item->leafid = leafid;
    item->reserved = 0;
    item->bytes = sizeof(*item) + bytes;
    bcopy(str, item + 1, bytes);
    hc->windex = HCC_ALIGN(hc->windex + item->bytes);
}

void
hcc_leaf_data(struct HostConf *hc, int16_t leafid, const void *ptr, int bytes)
{
    struct HCLeaf *item;

    item = (void *)(hc->wbuf + hc->windex);
    assert(hc->windex + sizeof(*item) + bytes < 65536);
    item->leafid = leafid;
    item->reserved = 0;
    item->bytes = sizeof(*item) + bytes;
    bcopy(ptr, item + 1, bytes);
    hc->windex = HCC_ALIGN(hc->windex + item->bytes);
}

void
hcc_leaf_int32(struct HostConf *hc, int16_t leafid, int32_t value)
{
    struct HCLeaf *item;

    item = (void *)(hc->wbuf + hc->windex);
    assert(hc->windex + sizeof(*item) + sizeof(value) < 65536);
    item->leafid = leafid;
    item->reserved = 0;
    item->bytes = sizeof(*item) + sizeof(value);
    *(int32_t *)(item + 1) = value;
    hc->windex = HCC_ALIGN(hc->windex + item->bytes);
}

void
hcc_leaf_int64(struct HostConf *hc, int16_t leafid, int64_t value)
{
    struct HCLeaf *item;

    item = (void *)(hc->wbuf + hc->windex);
    assert(hc->windex + sizeof(*item) + sizeof(value) < 65536);
    item->leafid = leafid;
    item->reserved = 0;
    item->bytes = sizeof(*item) + sizeof(value);
    *(int64_t *)(item + 1) = value;
    hc->windex = HCC_ALIGN(hc->windex + item->bytes);
}

int
hcc_alloc_descriptor(struct HostConf *hc, void *ptr, int type)
{
    struct HCHostDesc *hd;
    struct HCHostDesc *hnew;

    hnew = malloc(sizeof(struct HCHostDesc));
    hnew->type = type;
    hnew->data = ptr;

    if ((hd = hc->hostdescs) != NULL) {
	hnew->desc = hd->desc + 1;
    } else {
	hnew->desc = 1;
    }
    hnew->next = hd;
    hc->hostdescs = hnew;
    return(hnew->desc);
}

void *
hcc_get_descriptor(struct HostConf *hc, int desc, int type)
{
    struct HCHostDesc *hd;

    for (hd = hc->hostdescs; hd; hd = hd->next) {
	if (hd->desc == desc && hd->type == type)
	    return(hd->data);
    }
    return(NULL);
}

void
hcc_set_descriptor(struct HostConf *hc, int desc, void *ptr, int type)
{
    struct HCHostDesc *hd;

    for (hd = hc->hostdescs; hd; hd = hd->next) {
	if (hd->desc == desc) {
	    hd->data = ptr;
	    hd->type = type;
	    return;
	}
    }
    hd = malloc(sizeof(*hd));
    hd->desc = desc;
    hd->type = type;
    hd->data = ptr;
    hd->next = hc->hostdescs;
    hc->hostdescs = hd;
}

struct HCLeaf *
hcc_firstitem(struct HCHead *head)
{
    struct HCLeaf *item;
    int offset;

    offset = sizeof(*head);
    if (offset == head->bytes)
	return(NULL);
    assert(head->bytes >= offset + (int)sizeof(*item));
    item = (void *)(head + 1);
    assert(head->bytes >= offset + item->bytes);
    assert(item->bytes >= (int)sizeof(*item) && item->bytes < 65536 - offset);
    return (item);
}

struct HCLeaf *
hcc_nextitem(struct HCHead *head, struct HCLeaf *item)
{
    int offset;

    item = (void *)((char *)item + HCC_ALIGN(item->bytes));
    offset = (char *)item - (char *)head;
    if (offset == head->bytes)
	return(NULL);
    assert(head->bytes >= offset + (int)sizeof(*item));
    assert(head->bytes >= offset + item->bytes);
    assert(item->bytes >= (int)sizeof(*item) && item->bytes < 65536 - offset);
    return (item);
}

#ifdef DEBUG

void
hcc_debug_dump(struct HCHead *head)
{
    struct HCLeaf *item;
    int aligned_bytes = HCC_ALIGN(head->bytes);

    fprintf(stderr, "DUMP %04x (%d)", (u_int16_t)head->cmd, aligned_bytes);
    if (head->cmd & HCF_REPLY)
	fprintf(stderr, " error %d", head->error);
    fprintf(stderr, "\n");
    for (item = hcc_firstitem(head); item; item = hcc_nextitem(head, item)) {
	fprintf(stderr, "    ITEM %04x DATA ", item->leafid);
	switch(item->leafid & LCF_TYPEMASK) {
	case LCF_INT32:
	    fprintf(stderr, "int32 %d\n", *(int32_t *)(item + 1));
	    break;
	case LCF_INT64:
	    fprintf(stderr, "int64 %lld\n", *(int64_t *)(item + 1));
	    break;
	case LCF_STRING:
	    fprintf(stderr, "\"%s\"\n", (char *)(item + 1));
	    break;
	case LCF_BINARY:
	    fprintf(stderr, "(binary)\n");
	    break;
	default:
	    printf("?\n");
	}
    }
}

#endif

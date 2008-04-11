/*
 * HCLINK.C
 *
 * This module implements a simple remote control protocol
 *
 * $DragonFly: src/bin/cpdup/hclink.c,v 1.6 2008/04/11 07:31:05 dillon Exp $
 */

#include "cpdup.h"
#include "hclink.h"
#include "hcproto.h"

static struct HCHead *hcc_read_command(hctransaction_t trans, int anypkt);
static void hcc_start_reply(hctransaction_t trans, struct HCHead *rhead);

int
hcc_connect(struct HostConf *hc)
{
    int fdin[2];
    int fdout[2];
    const char *av[16];

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
	int n;

	dup2(fdin[1], 1);
	close(fdin[0]);
	close(fdin[1]);
	dup2(fdout[0], 0);
	close(fdout[0]);
	close(fdout[1]);

	n = 0;
	av[n++] = "ssh";
	if (CompressOpt)
	    av[n++] = "-C";
	av[n++] = "-T";
	av[n++] = hc->host;
	av[n++] = "cpdup";
	av[n++] = "-S";
	av[n++] = NULL;

	execv("/usr/bin/ssh", (void *)av);
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
rc_badop(hctransaction_t trans __unused, struct HCHead *head)
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
    struct HCTransaction trans;
    int (*dispatch[256])(hctransaction_t, struct HCHead *);
    int aligned_bytes;
    int i;
    int r;

    bzero(&hcslave, sizeof(hcslave));
    bzero(&trans, sizeof(trans));
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
    trans.hc = &hcslave;

    /*
     * Process commands on fdin and write out results on fdout
     */
    for (;;) {
	/*
	 * Get the command
	 */
	head = hcc_read_command(&trans, 1);
	if (head == NULL)
	    break;

	/*
	 * Start the reply and dispatch, then process the return code.
	 */
	head->error = 0;
	hcc_start_reply(&trans, head);

	r = dispatch[head->cmd & 255](&trans, head);

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
	whead = (void *)trans.wbuf;
	whead->bytes = trans.windex;
	whead->error = head->error;
	aligned_bytes = HCC_ALIGN(trans.windex);
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
 *
 * If id is -1 we do not match response id's
 */
static
struct HCHead *
hcc_read_command(hctransaction_t trans, int anypkt)
{
    struct HostConf *hc = trans->hc;
    hctransaction_t fill;
    struct HCHead tmp;
    int aligned_bytes;
    int n;
    int r;

#if USE_PTHREADS
    /*
     * Shortcut if the reply has already been read in by another thread.
     */
    if (trans->state == HCT_REPLIED) {
	return((void *)trans->rbuf);
    }
    pthread_mutex_unlock(&MasterMutex);
    pthread_mutex_lock(&hc->read_mutex);
#endif
    while (trans->state != HCT_REPLIED) {
	n = 0;
	while (n < (int)sizeof(struct HCHead)) {
	    r = read(hc->fdin, (char *)&tmp + n, sizeof(struct HCHead) - n);
	    if (r <= 0)
		goto fail;
	    n += r;
	}

	assert(tmp.bytes >= (int)sizeof(tmp) && tmp.bytes < 65536);
	assert(tmp.magic == HCMAGIC);

	if (anypkt == 0 && tmp.id != trans->id && trans->hc->version > 0) {
#if USE_PTHREADS
	    for (fill = trans->hc->hct_hash[tmp.id & HCTHASH_MASK];
		 fill;
		 fill = fill->next)
	    {
		if (fill->state == HCT_SENT && fill->id == tmp.id)
			break;
	    }
	    if (fill == NULL)
#endif
	    {
		fprintf(stderr, 
			"cpdup hlink protocol error with %s (%04x %04x)\n",
			trans->hc->host, trans->id, tmp.id);
		exit(1);
	    }
	} else {
	    fill = trans;
	}

	bcopy(&tmp, fill->rbuf, n);
	aligned_bytes = HCC_ALIGN(tmp.bytes);

	while (n < aligned_bytes) {
	    r = read(hc->fdin, fill->rbuf + n, aligned_bytes - n);
	    if (r <= 0)
		goto fail;
	    n += r;
	}
#ifdef DEBUG
	hcc_debug_dump(head);
#endif
	fill->state = HCT_REPLIED;
    }
#if USE_PTHREADS
    pthread_mutex_unlock(&hc->read_mutex);
    pthread_mutex_lock(&MasterMutex);
#endif
    return((void *)trans->rbuf);
fail:
#if USE_PTHREADS
    pthread_mutex_unlock(&hc->read_mutex);
    pthread_mutex_lock(&MasterMutex);
#endif
    return(NULL);
}

#if USE_PTHREADS

static
hctransaction_t
hcc_get_trans(struct HostConf *hc)
{
    hctransaction_t trans;
    hctransaction_t scan;
    pthread_t tid = pthread_self();
    int i;

    i = ((intptr_t)tid >> 7) & HCTHASH_MASK;

    for (trans = hc->hct_hash[i]; trans; trans = trans->next) {
	if (trans->tid == tid)
		break;
    }
    if (trans == NULL) {
	trans = malloc(sizeof(*trans));
	bzero(trans, sizeof(*trans));
	trans->tid = tid;
	trans->id = i;
	do {
		for (scan = hc->hct_hash[i]; scan; scan = scan->next) {
			if (scan->id == trans->id) {
				trans->id += HCTHASH_SIZE;
				break;
			}
		}
	} while (scan != NULL);

	trans->next = hc->hct_hash[i];
	hc->hct_hash[i] = trans;
    }
    return(trans);
}

#else

static
hctransaction_t
hcc_get_trans(struct HostConf *hc)
{
    return(&hc->trans);
}

#endif

/*
 * Initialize for a new command
 */
hctransaction_t
hcc_start_command(struct HostConf *hc, int16_t cmd)
{
    struct HCHead *whead;
    hctransaction_t trans;

    trans = hcc_get_trans(hc);

    whead = (void *)trans->wbuf;
    whead->magic = HCMAGIC;
    whead->bytes = 0;
    whead->cmd = cmd;
    whead->id = trans->id;
    whead->error = 0;

    trans->windex = sizeof(*whead);
    trans->hc = hc;
    trans->state = HCT_IDLE;

    return(trans);
}

static void
hcc_start_reply(hctransaction_t trans, struct HCHead *rhead)
{
    struct HCHead *whead = (void *)trans->wbuf;

    whead->magic = HCMAGIC;
    whead->bytes = 0;
    whead->cmd = rhead->cmd | HCF_REPLY;
    whead->id = rhead->id;
    whead->error = 0;

    trans->windex = sizeof(*whead);
}

/*
 * Finish constructing a command, transmit it, and await the reply.
 * Return the HCHead of the reply.
 */
struct HCHead *
hcc_finish_command(hctransaction_t trans)
{
    struct HCHead *whead;
    struct HCHead *rhead;
    int aligned_bytes;
    int16_t wcmd;

    whead = (void *)trans->wbuf;
    whead->bytes = trans->windex;
    aligned_bytes = HCC_ALIGN(trans->windex);

    trans->state = HCT_SENT;

    if (write(trans->hc->fdout, whead, aligned_bytes) != aligned_bytes) {
#ifdef __error
	*__error = EIO;
#else
	errno = EIO;
#endif
	if (whead->cmd < 0x0010)
		return(NULL);
	fprintf(stderr, "cpdup lost connection to %s\n", trans->hc->host);
	exit(1);
    }

    wcmd = whead->cmd;

    /*
     * whead is invalid when we call hcc_read_command() because
     * we may switch to another thread.
     */
    if ((rhead = hcc_read_command(trans, 0)) == NULL) {
#ifdef __error
	*__error = EIO;
#else
	errno = EIO;
#endif
	if (wcmd < 0x0010)
		return(NULL);
	fprintf(stderr, "cpdup lost connection to %s\n", trans->hc->host);
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
hcc_leaf_string(hctransaction_t trans, int16_t leafid, const char *str)
{
    struct HCLeaf *item;
    int bytes = strlen(str) + 1;

    item = (void *)(trans->wbuf + trans->windex);
    assert(trans->windex + sizeof(*item) + bytes < 65536);
    item->leafid = leafid;
    item->reserved = 0;
    item->bytes = sizeof(*item) + bytes;
    bcopy(str, item + 1, bytes);
    trans->windex = HCC_ALIGN(trans->windex + item->bytes);
}

void
hcc_leaf_data(hctransaction_t trans, int16_t leafid, const void *ptr, int bytes)
{
    struct HCLeaf *item;

    item = (void *)(trans->wbuf + trans->windex);
    assert(trans->windex + sizeof(*item) + bytes < 65536);
    item->leafid = leafid;
    item->reserved = 0;
    item->bytes = sizeof(*item) + bytes;
    bcopy(ptr, item + 1, bytes);
    trans->windex = HCC_ALIGN(trans->windex + item->bytes);
}

void
hcc_leaf_int32(hctransaction_t trans, int16_t leafid, int32_t value)
{
    struct HCLeaf *item;

    item = (void *)(trans->wbuf + trans->windex);
    assert(trans->windex + sizeof(*item) + sizeof(value) < 65536);
    item->leafid = leafid;
    item->reserved = 0;
    item->bytes = sizeof(*item) + sizeof(value);
    *(int32_t *)(item + 1) = value;
    trans->windex = HCC_ALIGN(trans->windex + item->bytes);
}

void
hcc_leaf_int64(hctransaction_t trans, int16_t leafid, int64_t value)
{
    struct HCLeaf *item;

    item = (void *)(trans->wbuf + trans->windex);
    assert(trans->windex + sizeof(*item) + sizeof(value) < 65536);
    item->leafid = leafid;
    item->reserved = 0;
    item->bytes = sizeof(*item) + sizeof(value);
    *(int64_t *)(item + 1) = value;
    trans->windex = HCC_ALIGN(trans->windex + item->bytes);
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
    struct HCHostDesc **hdp;

    for (hdp = &hc->hostdescs; (hd = *hdp) != NULL; hdp = &hd->next) {
	if (hd->desc == desc) {
	    if (ptr) {
		hd->data = ptr;
		hd->type = type;
	    } else {
		*hdp = hd->next;
		free(hd);
	    }
	    return;
	}
    }
    if (ptr) {
	hd = malloc(sizeof(*hd));
	hd->desc = desc;
	hd->type = type;
	hd->data = ptr;
	hd->next = hc->hostdescs;
	hc->hostdescs = hd;
    }
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

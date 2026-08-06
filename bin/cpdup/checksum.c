/*-
 * checksum.c
 *
 * (c) Copyright 1997-1999 by Matthew Dillon and Dima Ruban.  Permission to
 *     use and distribute based on the FreeBSD copyright.  Supplied as-is,
 *     USE WITH EXTREME CAUTION.
 */

#include "cpdup.h"

#include <openssl/evp.h>

typedef struct CSUMNode {
    struct CSUMNode *csum_Next;
    char *csum_Name;
    char *csum_Code;
    int csum_Accessed;
} CSUMNode;

static CSUMNode *csum_lookup(const char *sfile);
static void csum_cache(const char *spath, int sdirlen);
static char *doCsumFile(const EVP_MD *algo, const char *filename, char *buf, int is_target);

static char *CSUMSCache;		/* cache source directory name */
static CSUMNode *CSUMBase;
static int CSUMSCacheDirLen;
static int CSUMSCacheDirty;

void
csum_flush(void)
{
    if (CSUMSCacheDirty && CSUMSCache && NotForRealOpt == 0) {
	FILE *fo;

	if ((fo = fopen(CSUMSCache, "w")) != NULL) {
	    CSUMNode *node;

	    for (node = CSUMBase; node; node = node->csum_Next) {
		if (node->csum_Accessed && node->csum_Code) {
		    fprintf(fo, "%s %zu %s\n",
			node->csum_Code,
			strlen(node->csum_Name),
			node->csum_Name
		    );
		}
	    }
	    fclose(fo);
	}
    }

    CSUMSCacheDirty = 0;

    if (CSUMSCache) {
	CSUMNode *node;

	while ((node = CSUMBase) != NULL) {
	    CSUMBase = node->csum_Next;

	    if (node->csum_Code)
		free(node->csum_Code);
	    if (node->csum_Name)
		free(node->csum_Name);
	    free(node);
	}
	free(CSUMSCache);
	CSUMSCache = NULL;
    }
}

static void
csum_cache(const char *spath, int sdirlen)
{
    FILE *fi;

    /*
     * Already cached
     */

    if (
	CSUMSCache &&
	sdirlen == CSUMSCacheDirLen &&
	strncmp(spath, CSUMSCache, sdirlen) == 0
    ) {
	return;
    }

    /*
     * Different cache, flush old cache
     */

    if (CSUMSCache != NULL)
	csum_flush();

    /*
     * Create new cache
     */

    CSUMSCacheDirLen = sdirlen;
    CSUMSCache = mprintf("%*.*s%s", sdirlen, sdirlen, spath, CsumCacheFile);

    if ((fi = fopen(CSUMSCache, "r")) != NULL) {
	CSUMNode **pnode = &CSUMBase;
	int c;

	c = fgetc(fi);
	while (c != EOF) {
	    CSUMNode *node = *pnode = malloc(sizeof(CSUMNode));
	    char *s;
	    int nlen;

	    nlen = 0;

	    if (pnode == NULL || node == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(EXIT_FAILURE);
	    }

	    bzero(node, sizeof(CSUMNode));
	    node->csum_Code = fextract(fi, -1, &c, ' ');
	    node->csum_Accessed = 1;
	    if ((s = fextract(fi, -1, &c, ' ')) != NULL) {
		nlen = strtol(s, NULL, 0);
		free(s);
	    }
	    /*
	     * extracting csum_Name - name may contain embedded control
	     * characters.
	     */
	    CountSourceReadBytes += nlen+1;
	    node->csum_Name = fextract(fi, nlen, &c, EOF);
	    if (c != '\n') {
		fprintf(stderr, "Error parsing CSUM Cache: %s (%c)\n", CSUMSCache, c);
		while (c != EOF && c != '\n')
		    c = fgetc(fi);
	    }
	    if (c != EOF)
		c = fgetc(fi);
	    pnode = &node->csum_Next;
	}
	fclose(fi);
    }
}

/*
 * csum_lookup:	lookup/create csum entry
 */

static CSUMNode *
csum_lookup(const char *sfile)
{
    CSUMNode **pnode;
    CSUMNode *node;

    for (pnode = &CSUMBase; (node = *pnode) != NULL; pnode = &node->csum_Next) {
	if (strcmp(sfile, node->csum_Name) == 0) {
	    break;
	}
    }
    if (node == NULL) {

	if ((node = *pnode = malloc(sizeof(CSUMNode))) == NULL) {
		fprintf(stderr,"out of memory\n");
		exit(EXIT_FAILURE);
	}

	bzero(node, sizeof(CSUMNode));
	node->csum_Name = strdup(sfile);
    }
    node->csum_Accessed = 1;
    return(node);
}

/*
 * csum_check:  check CSUM against file
 *
 *	Return -1 if check failed
 *	Return 0  if check succeeded
 *
 * dpath can be NULL, in which case we are force-updating
 * the source CSUM.
 */
int
csum_check(const EVP_MD *algo, const char *spath, const char *dpath)
{
    const char *sfile;
    char *dcode;
    int sdirlen;
    int r;
    CSUMNode *node;

    r = -1;

    if ((sfile = strrchr(spath, '/')) != NULL)
	++sfile;
    else
	sfile = spath;
    sdirlen = sfile - spath;

    csum_cache(spath, sdirlen);

    node = csum_lookup(sfile);

    /*
     * If dpath == NULL, we are force-updating the source .CSUM* files
     */

    if (dpath == NULL) {
	char *scode = doCsumFile(algo, spath, NULL, 0);

	r = 0;
	if (node->csum_Code == NULL) {
	    r = -1;
	    node->csum_Code = scode;
	    CSUMSCacheDirty = 1;
	} else if (strcmp(scode, node->csum_Code) != 0) {
	    r = -1;
	    free(node->csum_Code);
	    node->csum_Code = scode;
	    CSUMSCacheDirty = 1;
	} else {
	    free(scode);
	}
	return(r);
    }

    /*
     * Otherwise the .CSUM* file is used as a cache.
     */

    if (node->csum_Code == NULL) {
	node->csum_Code = doCsumFile(algo, spath, NULL, 0);
	CSUMSCacheDirty = 1;
    }

    dcode = doCsumFile(algo, dpath, NULL, 1);
    if (dcode) {
	if (strcmp(node->csum_Code, dcode) == 0) {
	    r = 0;
	} else {
	    char *scode = doCsumFile(algo, spath, NULL, 0);

	    if (strcmp(node->csum_Code, scode) == 0) {
		    free(scode);
	    } else {
		    free(node->csum_Code);
		    node->csum_Code = scode;
		    CSUMSCacheDirty = 1;
		    if (strcmp(node->csum_Code, dcode) == 0)
			r = 0;
	    }
	}
	free(dcode);
    }
    return(r);
}

static char *
csum_file(const EVP_MD *algo, const char *filename, char *buf)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    static const char hex[] = "0123456789abcdef";
    EVP_MD_CTX *ctx;
    unsigned char buffer[4096];
    struct stat st;
    off_t size;
    int fd, bytes;
    unsigned int i, csum_len;

    fd = open(filename, O_RDONLY);
    if (fd < 0)
	return NULL;
    if (fstat(fd, &st) < 0) {
	bytes = -1;
	goto err;
    }

    ctx = EVP_MD_CTX_new();
    if (!EVP_DigestInit_ex(ctx, algo, NULL)) {
	fprintf(stderr, "Unable to initialize CSUM digest.\n");
	exit(1);
    }
    size = st.st_size;
    bytes = 0;
    while (size > 0) {
	if ((size_t)size > sizeof(buffer))
	     bytes = read(fd, buffer, sizeof(buffer));
	else
	     bytes = read(fd, buffer, size);
	if (bytes < 0)
	     break;
	if (!EVP_DigestUpdate(ctx, buffer, bytes)) {
	     EVP_MD_CTX_free(ctx);
	     fprintf(stderr, "Unable to update CSUM digest.\n");
	     exit(1);
	}
	size -= bytes;
    }

err:
    close(fd);
    if (bytes < 0)
	return NULL;

    if (!EVP_DigestFinal(ctx, digest, &csum_len)) {
	EVP_MD_CTX_free(ctx);
	fprintf(stderr, "Unable to finalize CSUM digest.\n");
	exit(1);
    }
    EVP_MD_CTX_free(ctx);

    if (!buf)
	buf = malloc(csum_len * 2 + 1);
    if (!buf)
	return NULL;

    for (i = 0; i < csum_len; i++) {
	buf[2*i] = hex[digest[i] >> 4];
	buf[2*i+1] = hex[digest[i] & 0x0f];
    }
    buf[csum_len * 2] = '\0';

    return buf;
}

char *
doCsumFile(const EVP_MD *algo, const char *filename, char *buf, int is_target)
{
    if (SummaryOpt) {
	struct stat st;
	if (stat(filename, &st) == 0) {
	    uint64_t size = st.st_size;
	    if (is_target)
		    CountTargetReadBytes += size;
	    else
		    CountSourceReadBytes += size;
	}
    }
    return csum_file(algo, filename, buf);
}

/*-
 * FSMID.C
 *
 * (c) Copyright 1997-1999,2006 by Matthew Dillon.  Permission to
 *     use and distribute based on the FreeBSD copyright.
 *
 * $DragonFly: src/bin/cpdup/fsmid.c,v 1.3 2008/11/10 14:30:02 swildner Exp $
 */

#include "cpdup.h"

typedef struct FSMIDNode {
    struct FSMIDNode *fid_Next;
    char *fid_Name;
    int64_t fid_Code;
    int fid_Accessed;
} FSMIDNode;

static FSMIDNode *fsmid_lookup(const char *sfile);
static void fsmid_cache(const char *dpath, int ddirlen);

static char *FSMIDDCache;	/* cache source directory name */
static FSMIDNode *FSMIDBase;
static int FSMIDDCacheDirLen;
static int FSMIDDCacheDirty;

void 
fsmid_flush(void)
{
    if (FSMIDDCacheDirty && FSMIDDCache && NotForRealOpt == 0) {
	FILE *fo;

	if ((fo = fopen(FSMIDDCache, "w")) != NULL) {
	    FSMIDNode *node;

	    for (node = FSMIDBase; node; node = node->fid_Next) {
		if (node->fid_Accessed && node->fid_Code) {
		    fprintf(fo, "%016jx %zu %s\n", 
			(intmax_t)node->fid_Code, 
			strlen(node->fid_Name),
			node->fid_Name
		    );
		}
	    }
	    fclose(fo);
	}
    }

    FSMIDDCacheDirty = 0;

    if (FSMIDDCache) {
	FSMIDNode *node;

	while ((node = FSMIDBase) != NULL) {
	    FSMIDBase = node->fid_Next;

	    if (node->fid_Name)
		free(node->fid_Name);
	    free(node);
	}
	free(FSMIDDCache);
	FSMIDDCache = NULL;
    }
}

static void
fsmid_cache(const char *dpath, int ddirlen)
{
    FILE *fi;

    /*
     * Already cached
     */

    if (
	FSMIDDCache &&
	ddirlen == FSMIDDCacheDirLen &&
	strncmp(dpath, FSMIDDCache, ddirlen) == 0
    ) {
	return;
    }

    /*
     * Different cache, flush old cache
     */

    if (FSMIDDCache != NULL)
	fsmid_flush();

    /*
     * Create new cache
     */

    FSMIDDCacheDirLen = ddirlen;
    FSMIDDCache = mprintf("%*.*s%s", ddirlen, ddirlen, dpath, FSMIDCacheFile);

    if ((fi = fopen(FSMIDDCache, "r")) != NULL) {
	FSMIDNode **pnode = &FSMIDBase;
	int c;

	c = fgetc(fi);
	while (c != EOF) {
	    FSMIDNode *node = *pnode = malloc(sizeof(FSMIDNode));
	    char *s;
	    int nlen;

	    nlen = 0;

	    if (pnode == NULL || node == NULL)
		fatal("out of memory");

	    bzero(node, sizeof(FSMIDNode));
	    node->fid_Code = strtoull(fextract(fi, -1, &c, ' '), NULL, 16);
	    node->fid_Accessed = 1;
	    if ((s = fextract(fi, -1, &c, ' ')) != NULL) {
		nlen = strtol(s, NULL, 0);
		free(s);
	    }
	    /*
	     * extracting fid_Name - name may contain embedded control 
	     * characters.
	     */
	    CountSourceReadBytes += nlen+1;
	    node->fid_Name = fextract(fi, nlen, &c, EOF);
	    if (c != '\n') {
		fprintf(stderr, "Error parsing FSMID Cache: %s (%c)\n", FSMIDDCache, c);
		while (c != EOF && c != '\n')
		    c = fgetc(fi);
	    }
	    if (c != EOF)
		c = fgetc(fi);
	    pnode = &node->fid_Next;
	}
	fclose(fi);
    }
}

/*
 * fsmid_lookup:	lookup/create fsmid entry
 */

static FSMIDNode *
fsmid_lookup(const char *sfile)
{
    FSMIDNode **pnode;
    FSMIDNode *node;

    for (pnode = &FSMIDBase; (node = *pnode) != NULL; pnode = &node->fid_Next) {
	if (strcmp(sfile, node->fid_Name) == 0) {
	    break;
	}
    }
    if (node == NULL) {
	if ((node = *pnode = malloc(sizeof(FSMIDNode))) == NULL)
	    fatal("out of memory");
	bzero(node, sizeof(FSMIDNode));
	node->fid_Name = strdup(sfile);
	FSMIDDCacheDirty = 1;
    }
    node->fid_Accessed = 1;
    return(node);
}

/*
 * fsmid_check:  check FSMID against file
 *
 *	Return -1 if check failed
 *	Return 0  if check succeeded
 *
 * dpath can be NULL, in which case we are force-updating
 * the source FSMID.
 */
int
fsmid_check(int64_t fsmid, const char *dpath)
{
    const char *dfile;
    int ddirlen;
    FSMIDNode *node;

    if ((dfile = strrchr(dpath, '/')) != NULL)
	++dfile;
    else
	dfile = dpath;
    ddirlen = dfile - dpath;

    fsmid_cache(dpath, ddirlen);

    node = fsmid_lookup(dfile);

    if (node->fid_Code != fsmid) {
	node->fid_Code = fsmid;
	FSMIDDCacheDirty = 1;
	return(1);
    }
    return(0);
}


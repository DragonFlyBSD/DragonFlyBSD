/*
 * RCONFIG/CLIENT.C
 *
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "defs.h"

#define LONG_ALIGN(n)	roundup2(n, sizeof(long))

static void load_client_broadcast_tags(tag_t tag, const char *tagName);

void
doClient(void)
{
    int done = 0;
    tag_t tag;

    /*
     * The server list is in the form host[:tag]
     */
    chdir(WorkDir);
    for (tag = AddrBase; tag && !done; tag = tag->next) {
	struct sockaddr_in sain;
	struct sockaddr_in rsin;
	char *tagName;
	char *host = NULL;
	char *res = NULL;
	char *buf = NULL;
	int len;
	int ufd = -1;
	FILE *fi = NULL;
	FILE *fo = NULL;
	int rc;

	bzero(&sain, sizeof(sain));
	if (tag->name == NULL) {
	    load_client_broadcast_tags(tag, "auto");
	    continue;
	}
	if (tag->name[0] == ':') {
	    load_client_broadcast_tags(tag, tag->name + 1);
	    continue;
	}
	host = strdup(tag->name);
	if ((tagName = strchr(host, ':')) != NULL) {
	    *tagName++ = 0;
	    tagName = strdup(tagName);
	} else {
	    tagName = strdup("auto");
	}
	if (inet_aton(host, &sain.sin_addr) == 0) {
	    struct hostent *hp;
	    if ((hp = gethostbyname2(host, AF_INET)) == NULL) {
		fprintf(stderr, "Unable to resolve %s\n", host);
		exit(1);
	    }
	    bcopy(hp->h_addr_list[0], &sain.sin_addr, hp->h_length);
	    free(host);
	    host = strdup(hp->h_name);
	    endhostent();
	}
	sain.sin_port = htons(257);
	sain.sin_len = sizeof(sain);
	sain.sin_family = AF_INET;

	/*
	 * Do a couple of UDP transactions to locate the tag on the server.
	 */
	printf("%s:%s - ", host, tagName);
	fflush(stdout);
	rc = udp_transact(&sain, &rsin, &ufd, &res, &len, "TAG %s\r\n", tagName);
	if (rc != 101 || res == NULL) {
	    printf("NO LUCK %s\n", (res ? res : ""));
	} else {
	    printf("%s -", res);
	    fflush(stdout);
	    rc = tcp_transact(&rsin, &fi, &fo, &buf, &len, "TAG %s\r\n", tagName);
	    if (rc == 201 && buf) {
		int ffd;
		char *path;

		asprintf(&path, "%s/%s.sh", WorkDir, tagName);
		ffd = open(path, O_CREAT|O_TRUNC|O_RDWR, 0755);
		if (ffd >= 0 && write(ffd, buf, len) == len) {
		    printf("running %s [%d] in", path, len);
		    close(ffd);
		    ffd = -1;
		    for (rc = 5; rc > 0; --rc) {
			printf(" %d", rc);
			fflush(stdout);
			sleep(1);
		    }
		    printf(" 0\n");
		    fflush(stdout);
		    rc = system(path);
		    if (rc)
			printf("rconfig script exit code %d\n", rc);
		    done = 1;
		} else {
		    if (ffd >= 0) {
			remove(path);
			close(ffd);
			ffd = -1;
		    }
		    printf(" unable to create %s [%d] - DOWNLOAD FAILED\n",
			    path, len);
		}
	    } else {
		printf(" DOWNLOAD FAILED\n");
	    }
	}
	if (ufd >= 0) {
	    close(ufd);
	    ufd = -1;
	}
	if (fi != NULL) {
	    fclose(fi);
	    fi = NULL;
	}
	if (fo != NULL) {
	    fclose(fo);
	    fo = NULL;
	}
	if (buf)
	    free(buf);
	if (res)
	    free(res);
	free(host);
	free(tagName);
    }
}

static
void
load_client_broadcast_tags(tag_t tag, const char *tagName)
{
    struct sockaddr_dl *sdl;
    struct if_msghdr *ifm;
    int mib[6];
    char *buf;
    size_t bytes;
    int i;

    mib[0] = CTL_NET;
    mib[1] = PF_ROUTE;
    mib[2] = 0;
    mib[3] = AF_INET;
    mib[4] = NET_RT_IFLIST;
    mib[5] = 0;

    if (sysctl(mib, 6, NULL, &bytes, NULL, 0) < 0) {
	printf("no interfaces!\n");
	exit(1);
    }
    buf = malloc(bytes);
    if (sysctl(mib, 6, buf, &bytes, NULL, 0) < 0) {
	printf("no interfaces!\n");
	exit(1);
    }
    ifm = (void *)buf;
    sdl = NULL;
    while ((char *)ifm < buf + bytes && ifm->ifm_msglen) {
	switch(ifm->ifm_type) {
	case RTM_IFINFO:
	    if (ifm->ifm_flags & IFF_UP) {
		sdl = (void *)(ifm + 1);
	    } else {
		sdl = NULL;
	    }
	    break;
	case RTM_NEWADDR:
	    if (sdl) {
		struct sockaddr_in *sain;
		struct ifa_msghdr *ifam;
		char *scan;
		char *name;
		tag_t ntag;

		ifam = (void *)ifm;
		scan = (char *)(ifam + 1);
		for (i = 0; i < RTAX_MAX; ++i) {
		    if ((1 << i) & ifam->ifam_addrs) {
			sain = (void *)scan;
			if (i == RTAX_BRD) {
			    asprintf(&name, "%s:%s",
				    inet_ntoa(sain->sin_addr), tagName);
			    ntag = calloc(sizeof(struct tag), 1);
			    ntag->name = name;
			    ntag->flags = 0;
			    ntag->next = tag->next;
			    tag->next = ntag;
			    tag = ntag;
			    if (VerboseOpt)
				printf("add: %s (%s)\n", sdl->sdl_data, tag->name);
			}
			scan = scan + LONG_ALIGN(sain->sin_len);
		    }
		}
	    }
	    break;
	}
	ifm = (void *)((char *)ifm + ifm->ifm_msglen);
    }
}


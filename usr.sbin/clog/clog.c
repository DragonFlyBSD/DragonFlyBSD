/*-
 * Copyright (c) 2001
 *     Jeff Wheelhouse (jdw@wwwi.com)
 *
 * This code was originally developed by Jeff Wheelhouse (jdw@wwwi.com).
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistribution of source code must retail the above copyright 
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY JEFF WHEELHOUSE ``AS IS'' AND ANY EXPRESS OR 
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN 
 * NO EVENT SHALL JEFF WHEELHOUSE BE LIABLE FOR ANY DIRECT, INDIRECT, 
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING BUT NOT 
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF 
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id: clog.c,v 1.3 2001/10/02 18:51:26 jdw Exp $
 * $DragonFly: src/usr.sbin/clog/clog.c,v 1.2 2004/12/18 22:48:03 swildner Exp $
 */


#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>


#include "clog.h"


/*
 *  The BUFFER_SIZE value is just used to allocate a buffer full of NULLs 
 *  so that a new logfile can be extended to its full size.
 *
 *  Compiling with -pedantic complains when the buffer array is declared
 *  if I declare this as a const instead of a #define.
 */
#define BUFFER_SIZE 16384

void init_log __P((const char *lname, size_t size));
void read_log __P((const char *lname, int optf));
void usage __P((void));

const char *pname;

int main(int argc, char **argv) {
	int ch;
	int init = 0;
	int size = 0;
	int optf = 0;

	pname = argv[0];

	while ((ch = getopt(argc, argv, "fis:")) != -1)
		switch(ch) {
		case 'i':
			init = 1;
			break;
		case 's':
			size = atol(optarg);
			if (size==0) usage();
			break;
		case 'f':
			optf = 1;
		}

	if ((size>0)&&(init==0)) {
		fprintf(stderr,"%s: WARNING: -s argument ignored without -i.\n",pname);
		size = 0;
	}
	if (argv[optind]==NULL) {
		fprintf(stderr,"%s: ERROR: log_file argument must be specified.\n",pname);
		usage();
	}
	if ((init==1)&&(size==0)) {
		fprintf(stderr,"%s: ERROR: -i argument requires -s.\n",pname);
		usage();
	}
	if ((init==1)&&(optf==1)) {
		fprintf(stderr,"%s: ERROR: flags -f and -i are incompatible.\n",pname);
		usage();
	}

	if (init==1) init_log(argv[optind],size);
	/* if (optf==1) follow_log(artv[optind]); */
	read_log(argv[optind],optf);
		
	return 0;
}


void usage() {
  fprintf(stderr,"usage: %s [-i -s log_size] [ -f ] log_file\n",pname);
  exit(1);
}


void read_log(const char *lname, int optf) {
	int fd;
	struct stat sb;
	struct clog_footer *pcf;
	char *pbuffer;
	struct iovec iov[2];
	int iovcnt = 0;
	uint32_t start = 0;
	uint32_t next;
	struct pollfd pfd;

	pfd.fd = -1;

	fd = open(lname,O_RDONLY);
	if (fd==-1) {
		fprintf(stderr,"%s: ERROR: could not open %s (%s)\n",pname,lname,strerror(errno));
		exit(11);
	}

	if (fstat(fd,&sb)==-1) {
		fprintf(stderr,"%s: ERROR: could not stat %s (%s)\n",pname,lname,strerror(errno));
		exit(13);
	}
	pbuffer = mmap(NULL,sb.st_size,PROT_READ,MAP_SHARED,fd,0);
	if (pbuffer==NULL) {
		fprintf(stderr,"%s: ERROR: could not mmap %s body (%s)\n",pname,lname,strerror(errno));
		exit(14);
	}
	pcf = (struct clog_footer*)(pbuffer + sb.st_size - sizeof(struct clog_footer));

	if (pcf->cf_wrap==1) start = pcf->cf_next + 1;
	while(1) {
		while(pcf->cf_lock==1) sched_yield();
		next = pcf->cf_next;
		iovcnt = 0;
		if (start>next) {
			iov[iovcnt].iov_base = pbuffer + start;
			iov[iovcnt++].iov_len = pcf->cf_max - start;
			start = 0;
		}
		iov[iovcnt].iov_base = pbuffer + start;
		iov[iovcnt++].iov_len = next - start;
		if (writev(1,iov,iovcnt)==-1) {
			fprintf(stderr,"%s: ERROR: could not write output (%s)\n",pname,strerror(errno));
			exit(15);
		}
		start = next;
		if (optf==0) break;
		if (poll(&pfd,1,50)==-1) {
			fprintf(stderr,"%s: ERROR: could not poll (%s)\n",pname,strerror(errno));
			exit(16);
		}
	}
	
	munmap(pbuffer,sb.st_size);
	close(fd);

	exit(0);
}


void init_log(const char *lname, size_t size) {
	int fd;
	size_t fill = size;
	char buffer[BUFFER_SIZE];
	struct clog_footer cf;

	memcpy(&cf.cf_magic,MAGIC_CONST,4);
	cf.cf_max = size - sizeof(struct clog_footer);

	memset(buffer,0,BUFFER_SIZE);

	fd = open(lname,O_RDWR|O_CREAT,0666); 
	if (fd==-1) {
		fprintf(stderr,"%s: ERROR: could not open %s (%s)\n",pname,lname,strerror(errno));
		exit(2);
	}
	if (ftruncate(fd,(off_t)0)==-1) {
		fprintf(stderr,"%s: ERROR: could not truncate %s (%s)\n",pname,lname,strerror(errno));
		exit(3);
	}
	
	while(fill>BUFFER_SIZE) {
		if (write(fd,buffer,BUFFER_SIZE)==-1){
			fprintf(stderr,"%s: ERROR: could not write %s (%s)\n",pname,lname,strerror(errno));
			exit(4);
		}
		fill -= BUFFER_SIZE;
	}
	assert(fill<=BUFFER_SIZE);
	if (fill>0) {
		if (write(fd,buffer,fill)==-1) {
			fprintf(stderr,"%s: ERROR: could not write %s (%s)\n",pname,lname,strerror(errno));
			exit(5);
		}
	}
	if (lseek(fd,-(off_t)(sizeof(struct clog_footer)),SEEK_END)==-1) {
		fprintf(stderr,"%s: ERROR: could not seek in %s (%s)\n",pname,lname,strerror(errno));
		exit(6);
	}
	if (write(fd,&cf,sizeof(cf))==-1) {
		fprintf(stderr,"%s: ERROR: could not write magic in %s (%s)\n",pname,lname,strerror(errno));
		exit(7);
	}
	close(fd);
	exit(0);
}



/*
 *	Random supply helper
 * Copyright 2004, Clemens Fruhwirth <clemens@endorphin.org>
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

static int randomfd = -1; 

int openRandom() {
    if(randomfd == -1) 
	randomfd = open("/dev/urandom", O_RDONLY);
    return randomfd;
}

/* This method leaks a file descriptor that can be obtained by calling
   closeRandom */
int getRandom(char *buf, size_t len)
{
    if(openRandom() == -1) {
	perror("getRandom:");
	return -EINVAL;
    }
    while(len) {
	int r;
	r = read(randomfd,buf,len);
	if (-1 == r && errno != -EINTR) {	
	    perror("read: "); return -EINVAL;
	}
	len-= r; buf += r;
    }
    return 0;
}

void closeRandom() {
    if(randomfd != -1) {
	close(randomfd);
	randomfd = -1;
    }
}

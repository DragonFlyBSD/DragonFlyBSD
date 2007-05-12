/*
 * $DragonFly: src/test/stress/fsstress/xfscompat.h,v 1.2 2007/05/12 21:46:49 swildner Exp $
 */
#define MAXNAMELEN 1024
struct dioattr {
	int d_miniosz, d_maxiosz, d_mem;
}; 

#ifndef __DragonFly__
#define MIN(a,b) ((a)<(b) ? (a):(b))
#define MAX(a,b) ((a)>(b) ? (a):(b))
#endif

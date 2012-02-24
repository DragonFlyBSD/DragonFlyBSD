#include <sys/types.h>
#include <stdlib.h>
#include <errno.h>

void *
aligned_alloc(size_t alignment, size_t size) 
{
	void *ptr;
	int rc;
	
	ptr = NULL;
	rc = posix_memalign(&ptr, alignment, size);
	if (rc)
		errno = rc;
	
	return (ptr);
}

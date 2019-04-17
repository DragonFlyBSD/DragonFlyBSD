
#include <sys/types.h>
#include <sys/param.h>
#include <stdlib.h>
#include <unistd.h>

#include "local_xsi.h"

/*
 * "setkey" routine (for backwards compatibility)
 */
int
setkey(const char *key)
{
	return __crypt_setkey(key);
}

/*
 * "encrypt" routine (for backwards compatibility)
 */
int
encrypt(char *block, int flag)
{
	return __crypt_encrypt(block, flag);
}

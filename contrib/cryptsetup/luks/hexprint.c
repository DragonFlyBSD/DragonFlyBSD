
#include <stdio.h>

void hexprint(char *d, int n)
{
	int i;
	for(i = 0; i < n; i++)
	{
		printf("%02hhx ", (char)d[i]);
	}
}


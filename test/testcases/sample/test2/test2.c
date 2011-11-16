#include <stdio.h>

int main(void)
{
	char *a = NULL;
	printf("Hi <string>!\n");

	fprintf(stderr, "Gonna segfault now!\n");

	*a = '\0';

	return 0;
}

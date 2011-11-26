#include <stdio.h>

int main(int argc, char *argv[])
{
	int i;

	printf("Hi <string>!\n");
	printf("argc=%d\n", argc);

	for (i = 0; i < argc; i++)
		printf("argv[%d] = %s\n", i, argv[i]);

	fprintf(stderr, "Ho!\n");
	return 0;
}

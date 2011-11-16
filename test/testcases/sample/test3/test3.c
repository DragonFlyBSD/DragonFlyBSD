#include <stdio.h>

int main(void)
{
	printf("Hi <string>!\n");

	fprintf(stderr, "Ho, I'll fail miserably with exit code 33!\n");
	return 33;
}

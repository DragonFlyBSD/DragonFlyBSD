#include <stdio.h>
#include <unistd.h>

int main(void)
{
	int i;

	printf("Hi, starting to run now!\n");

	while(1) {
		sleep(1);
		i++;

		fprintf(stdout, "Ran for %d seconds\n", i);
		fprintf(stderr, "Ran for %d seconds\n", i);
	}

	fprintf(stderr, "Ho... finished the loop!\n");
	return 0;
}

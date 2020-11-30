#include <assert.h>
#include <stdio.h>
#include <dict.h>
#include <string.h>

const int methods[3] = {
	AURA_DICT_HASH,
	AURA_DICT_LIST,
	AURA_DICT_SORTED_LIST
};

const char *key = "key";

static struct aura_dict *
dict_create(int method)
{
	struct aura_dict *d;
	char *first, *second;

	first = malloc(16);
	second = malloc(16);

	d = aura_dict_new(2, method);

	snprintf(first, strlen("first") + 1, "%s", "first");
	snprintf(second, strlen("second") + 1, "%s", "second");

	aura_dict_store(d, key, strlen(key), first, strlen("first") + 1);
	aura_dict_store(d, key, strlen(key), second, strlen("second") + 1);

	return d;
}

int
main(void)
{
	struct aura_dict *d;

	for (int i = 1; i <= 3; i++) {
		char *v;
		size_t len = 0;

		d = dict_create(i);

		aura_dict_fetch(d, "key", strlen("key"), (void **)&v, &len);
		if (!strcmp("second", v) == 0) {
			fprintf(stderr,
			    "aura_dict_store did not handle duplicates\n");
			return 1;
		}
	}

	return 0;
}

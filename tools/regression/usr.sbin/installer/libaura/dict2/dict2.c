#include <assert.h>
#include <stdio.h>
#include <dict.h>
#include <string.h>

#define ELEMS	16

/* Make starwars fans happy */
const char keys[][16] = {
	"It",
	"is",
	"a",
	"period",
	"of",
	"civil",
	"war.",
	"Rebel",
	"spaceships,",
	"striking",
	"from a",
	"hidden",
	"base,",
	"have",
	"won",
	"their..."
};

int values[16];

const int methods[3] = {
	AURA_DICT_HASH,
	AURA_DICT_LIST,
	AURA_DICT_SORTED_LIST
};

const char *
m2s(int method)
{
	switch(method) {
	case AURA_DICT_HASH:
		return "hash";
		break;	/* NOTREACHED */
	case AURA_DICT_LIST:
		return "list";
		break;	/* NOTREACHED */
	case AURA_DICT_SORTED_LIST:
		return "sorted_list";
		break;	/* NOTREACHED */
	default:
		return NULL;
		break;	/* NOTREACHED */
	}
}

static struct aura_dict *
test_store(int method)
{
	struct aura_dict *d;

	d = aura_dict_new(ELEMS, method);
	for (int i = 0; i < ELEMS; i++) {
		values[i] = i;
		aura_dict_store(d, keys[i], strlen(keys[i]),
		    &values[i], sizeof(int));
		printf("%s_store: %s=>%d\n",
		    m2s(method), keys[i], values[i]);
	}
	return d;
}

static int
test_fetch(struct aura_dict *d, int method)
{
	if (d == NULL)
		return -1;

	aura_dict_rewind(d);
	for (int i = 0; i < ELEMS; i++) {
		int *val = malloc(sizeof(int));
		size_t len = sizeof(int);
		aura_dict_fetch(d, keys[i], strlen(keys[i]),
		    (void *)&val, &len);
		if (*val != values[i]) {
			fprintf(stderr,
			    "mismatch *val=%d values[%d]=%d\n",
			    *val, i, values[i]);
			return -1;
		}
		printf("%s_fetch: %s=>%d\n", m2s(method), keys[i], *val);
		free(val);
	}
	aura_dict_free(d);

	return 0;
}

int
main(void)
{
	struct aura_dict *d;

	for (int i = 1; i <= 3; i++) {
		d = test_store(i);

		if ((test_fetch(d, i)) == -1) {
			return 1;
		}
	}

	return 0;
}

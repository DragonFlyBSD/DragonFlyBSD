#include <assert.h>
#include <stdio.h>
#include <dict.h>

int
main(void)
{
	struct aura_dict *d;

	/* Overflow test */
	d = aura_dict_new(-1, AURA_DICT_HASH);
	assert (d == NULL);

	/* Invalid method check */
	d = aura_dict_new(1, -1);
	assert(d == NULL);

	return 0;
}

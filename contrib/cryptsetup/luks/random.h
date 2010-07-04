#ifndef INCLUDED_CRYPTSETUP_LUKS_RANDOM_H
#define INCLUDED_CRYPTSETUP_LUKS_RANDOM_H

#include <stddef.h>

int getRandom(char *buf, size_t len);

#endif
